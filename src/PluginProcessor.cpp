#include "PluginProcessor.h"
#include "PluginEditor.h"

BreadbinProcessor::BreadbinProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {
  // Initialize both SIDs with default models
  sidLeft.setChipModel(chipModelLeft);
  sidRight.setChipModel(chipModelRight);
}

BreadbinProcessor::~BreadbinProcessor() = default;

void BreadbinProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  hostSampleRate = sampleRate;
  sidLeft.prepare(sampleRate);
  sidRight.prepare(sampleRate);

  // Initialize MIDI collector for virtual keyboard
  midiCollector.reset(sampleRate);

  // Initialize safety chain
  prepareSafetyChain(sampleRate, samplesPerBlock);
}

void BreadbinProcessor::releaseResources() {}

void BreadbinProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                     juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;

  // Add messages from virtual keyboard (standalone mode)
  midiCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());

  // Handle MIDI
  for (const auto metadata : midiMessages) {
    handleMidiEvent(metadata.getMessage());
  }

  const int numSamples = buffer.getNumSamples();
  auto *leftChannel = buffer.getWritePointer(0);
  auto *rightChannel =
      buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

  // Generate audio from SID engines
  for (int i = 0; i < numSamples; ++i) {
    float sampleL = sidLeft.clock();
    float sampleR = sidRight.clock();

    switch (dualMode) {
    case DualMode::StereoSplit:
      // Left SID -> Left, Right SID -> Right
      leftChannel[i] = sampleL;
      if (rightChannel)
        rightChannel[i] = sampleR;
      break;

    case DualMode::Unison:
      // Mix both SIDs to both channels
      leftChannel[i] = (sampleL + sampleR) * 0.5f;
      if (rightChannel)
        rightChannel[i] = leftChannel[i];
      break;

    case DualMode::Multitimbral:
      // Same as stereo split, but MIDI routing differs
      leftChannel[i] = sampleL;
      if (rightChannel)
        rightChannel[i] = sampleR;
      break;
    }
  }
}

void BreadbinProcessor::handleMidiEvent(const juce::MidiMessage &msg) {
  if (msg.isNoteOn()) {
    const int note = msg.getNoteNumber();
    const int velocity = msg.getVelocity();
    const int channel = msg.getChannel();

    if (dualMode == DualMode::Multitimbral) {
      // Channel 1 -> voices 0-2 (left SID), Channel 2 -> voices 3-5 (right SID)
      int startVoice = (channel == 2) ? 3 : 0;
      for (int v = startVoice; v < startVoice + 3; ++v) {
        if (!voices[v].active) {
          triggerNote(v, note, velocity);
          break;
        }
      }
    } else if (dualMode == DualMode::StereoSplit ||
               dualMode == DualMode::Unison) {
      // Find a free enabled voice on left SID (0-2)
      for (int v = 0; v < 3; ++v) {
        if (voiceSettings[v].enabled && !voices[v].active) {
          triggerNote(v, note, velocity);
          break;
        }
      }
      // Find a free enabled voice on right SID (3-5)
      for (int v = 3; v < 6; ++v) {
        if (voiceSettings[v].enabled && !voices[v].active) {
          triggerNote(v, note, velocity);
          break;
        }
      }
    }
  } else if (msg.isNoteOff()) {
    const int note = msg.getNoteNumber();
    // Release all voices playing this note (may be paired in Stereo/Unison)
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active && voices[v].note == note) {
        releaseNote(v);
      }
    }
  }
}

void BreadbinProcessor::triggerNote(int voiceIndex, int midiNote,
                                    int velocity) {
  voices[voiceIndex].note = midiNote;
  voices[voiceIndex].active = true;

  // Route to appropriate SID
  SIDEngine &sid = (voiceIndex < 3) ? sidLeft : sidRight;
  int sidVoice = voiceIndex % 3;

  sid.noteOn(sidVoice, midiNote, velocity);
}

void BreadbinProcessor::releaseNote(int voiceIndex) {
  voices[voiceIndex].active = false;

  SIDEngine &sid = (voiceIndex < 3) ? sidLeft : sidRight;
  int sidVoice = voiceIndex % 3;

  sid.noteOff(sidVoice);
}

void BreadbinProcessor::applyVoiceSettings(int voice) {
  if (voice < 0 || voice > 5)
    return;

  SIDEngine &sid = (voice < 3) ? sidLeft : sidRight;
  int sidVoice = voice % 3;
  const auto &settings = voiceSettings[voice];

  sid.setWaveform(sidVoice, settings.waveform);
  sid.setPulseWidth(sidVoice, settings.pulseWidth);
  sid.setAttack(sidVoice, settings.attack);
  sid.setDecay(sidVoice, settings.decay);
  sid.setSustain(sidVoice, settings.sustain);
  sid.setRelease(sidVoice, settings.release);
}

void BreadbinProcessor::setLeftChipModel(SIDEngine::ChipModel model) {
  chipModelLeft = model;
  sidLeft.setChipModel(model);
}

void BreadbinProcessor::setRightChipModel(SIDEngine::ChipModel model) {
  chipModelRight = model;
  sidRight.setChipModel(model);
}

void BreadbinProcessor::setBothChipModels(SIDEngine::ChipModel model) {
  setLeftChipModel(model);
  setRightChipModel(model);
}

void BreadbinProcessor::setAgingFactor(float aging) {
  agingFactor = aging;
  sidLeft.setAgingFactor(aging);
  sidRight.setAgingFactor(aging);
}

juce::AudioProcessorEditor *BreadbinProcessor::createEditor() {
  return new BreadbinEditor(*this);
}

void BreadbinProcessor::getStateInformation(juce::MemoryBlock &destData) {
  juce::ValueTree state("BreadbinState");
  state.setProperty("dualMode", static_cast<int>(dualMode), nullptr);
  state.setProperty("chipModelLeft", static_cast<int>(chipModelLeft), nullptr);
  state.setProperty("chipModelRight", static_cast<int>(chipModelRight),
                    nullptr);
  state.setProperty("agingFactor", agingFactor, nullptr);

  juce::MemoryOutputStream stream(destData, false);
  state.writeToStream(stream);
}

void BreadbinProcessor::setStateInformation(const void *data, int sizeInBytes) {
  auto state =
      juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
  if (state.isValid()) {
    dualMode = static_cast<DualMode>(
        static_cast<int>(state.getProperty("dualMode", 0)));
    setLeftChipModel(static_cast<SIDEngine::ChipModel>(
        static_cast<int>(state.getProperty("chipModelLeft", 0))));
    setRightChipModel(static_cast<SIDEngine::ChipModel>(
        static_cast<int>(state.getProperty("chipModelRight", 0))));
    setAgingFactor(state.getProperty("agingFactor", 0.0f));
  }
}

void BreadbinProcessor::prepareSafetyChain(double sampleRate,
                                           int samplesPerBlock) {
  juce::dsp::ProcessSpec spec;
  spec.sampleRate = sampleRate;
  spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
  spec.numChannels = 2;

  // 20Hz high-pass (subsonic filter)
  *subsonicFilter.state =
      *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 20.0f);
  subsonicFilter.prepare(spec);

  // 20kHz low-pass (ultrasonic filter)
  *ultrasonicFilter.state =
      *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 20000.0f);
  ultrasonicFilter.prepare(spec);

  // Safety limiter at -3dB
  safetyLimiter.setThreshold(-3.0f);
  safetyLimiter.setRelease(100.0f);
  safetyLimiter.prepare(spec);
}

// Plugin instantiation
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new BreadbinProcessor();
}
