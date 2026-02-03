#include "PluginProcessor.h"
#include "PluginEditor.h"

BreadbinProcessor::BreadbinProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {
  // Initialize both SIDs with default model
  sidLeft.setChipModel(chipModel);
  sidRight.setChipModel(chipModel);
}

BreadbinProcessor::~BreadbinProcessor() = default;

void BreadbinProcessor::prepareToPlay(double sampleRate,
                                      int /*samplesPerBlock*/) {
  hostSampleRate = sampleRate;
  sidLeft.prepare(sampleRate);
  sidRight.prepare(sampleRate);
}

void BreadbinProcessor::releaseResources() {}

void BreadbinProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                     juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;

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

    // Find free voice based on mode
    int voiceIndex = -1;

    if (dualMode == DualMode::Multitimbral) {
      // Channel 1 -> voices 0-2 (left SID), Channel 2 -> voices 3-5 (right SID)
      int startVoice = (channel == 2) ? 3 : 0;
      for (int v = startVoice; v < startVoice + 3; ++v) {
        if (!voices[v].active) {
          voiceIndex = v;
          break;
        }
      }
    } else {
      // Stereo/Unison: use voices round-robin
      for (int v = 0; v < 6; ++v) {
        if (!voices[v].active) {
          voiceIndex = v;
          break;
        }
      }
    }

    if (voiceIndex >= 0) {
      triggerNote(voiceIndex, note, velocity);
    }
  } else if (msg.isNoteOff()) {
    const int note = msg.getNoteNumber();
    // Find and release matching voice
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active && voices[v].note == note) {
        releaseNote(v);
        break;
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

void BreadbinProcessor::setChipModel(SIDEngine::ChipModel model) {
  chipModel = model;
  sidLeft.setChipModel(model);
  sidRight.setChipModel(model);
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
  state.setProperty("chipModel", static_cast<int>(chipModel), nullptr);
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
    setChipModel(static_cast<SIDEngine::ChipModel>(
        static_cast<int>(state.getProperty("chipModel", 0))));
    setAgingFactor(state.getProperty("agingFactor", 0.0f));
  }
}

// Plugin instantiation
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new BreadbinProcessor();
}
