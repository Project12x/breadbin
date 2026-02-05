#include "PluginProcessor.h"
#include "PluginEditor.h"

BreadbinProcessor::BreadbinProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {
  // Initialize all three SIDs with default models
  sidLeft.setChipModel(chipModelLeft);
  sidCenter.setChipModel(chipModelCenter);
  sidRight.setChipModel(chipModelRight);
}

BreadbinProcessor::~BreadbinProcessor() = default;

void BreadbinProcessor::setDualMode(DualMode mode) {
  dualMode = mode;

  // Set per-SID panning based on mode
  switch (mode) {
  case DualMode::StereoSplit:
  case DualMode::Multitimbral:
    leftSIDPan = -0.75f; // 75% left
    centerSIDPan = 0.0f; // Center
    rightSIDPan = 0.75f; // 75% right
    break;
  case DualMode::Unison:
    leftSIDPan = 0.0f;   // Center
    centerSIDPan = 0.0f; // Center
    rightSIDPan = 0.0f;  // Center
    break;
  }
}

void BreadbinProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  hostSampleRate = sampleRate;
  sidLeft.prepare(sampleRate);
  sidCenter.prepare(sampleRate);
  sidRight.prepare(sampleRate);

  // Initialize MIDI collector for virtual keyboard
  midiCollector.reset(sampleRate);

  // Apply voice settings to all 9 voices (3 SIDs x 3 voices)
  for (int v = 0; v < 9; ++v) {
    applyVoiceSettings(v);
  }

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

  // Generate audio from SID engines with per-SID panning
  for (int i = 0; i < numSamples; ++i) {
    float sampleL = sidLeft.clock();
    float sampleC = sidCenter.clock();
    float sampleR = sidRight.clock();

    // Apply per-SID panning
    // Pan formula: pan -1.0 = full left, 0.0 = center, +1.0 = full right
    // leftGain = (1 - pan) * 0.5, rightGain = (1 + pan) * 0.5
    const float leftL = sampleL * (1.0f - leftSIDPan) * 0.5f;
    const float leftR = sampleL * (1.0f + leftSIDPan) * 0.5f;
    const float centerL = sampleC * (1.0f - centerSIDPan) * 0.5f;
    const float centerR = sampleC * (1.0f + centerSIDPan) * 0.5f;
    const float rightL = sampleR * (1.0f - rightSIDPan) * 0.5f;
    const float rightR = sampleR * (1.0f + rightSIDPan) * 0.5f;

    leftChannel[i] = leftL + centerL + rightL;
    if (rightChannel)
      rightChannel[i] = leftR + centerR + rightR;
  }
}

void BreadbinProcessor::handleMidiEvent(const juce::MidiMessage &msg) {
  if (msg.isNoteOn()) {
    const int note = msg.getNoteNumber();
    lastVelocity = msg.getVelocity();

    if (dualMode == DualMode::Multitimbral) {
      // Split keyboard: below C4 -> left, C4-B4 -> center, C5+ -> right
      const int splitLow = 60;  // Middle C
      const int splitHigh = 72; // C5
      if (note >= splitHigh) {
        rightNoteQueue.addIfNotAlreadyThere(note);
        updateSIDFromQueue(2); // right SID
      } else if (note >= splitLow) {
        centerNoteQueue.addIfNotAlreadyThere(note);
        updateSIDFromQueue(1); // center SID
      } else {
        leftNoteQueue.addIfNotAlreadyThere(note);
        updateSIDFromQueue(0); // left SID
      }
    } else {
      // Stereo/Unison: all 3 SIDs play the same notes
      leftNoteQueue.addIfNotAlreadyThere(note);
      centerNoteQueue.addIfNotAlreadyThere(note);
      rightNoteQueue.addIfNotAlreadyThere(note);
      updateSIDFromQueue(0); // left
      updateSIDFromQueue(1); // center
      updateSIDFromQueue(2); // right
    }
  } else if (msg.isNoteOff()) {
    const int note = msg.getNoteNumber();

    // Remove from all queues
    leftNoteQueue.removeFirstMatchingValue(note);
    centerNoteQueue.removeFirstMatchingValue(note);
    rightNoteQueue.removeFirstMatchingValue(note);

    // Update all SIDs to play the previous note (or off if queue empty)
    updateSIDFromQueue(0); // left
    updateSIDFromQueue(1); // center
    updateSIDFromQueue(2); // right
  } else if (msg.isAllNotesOff()) {
    leftNoteQueue.clear();
    centerNoteQueue.clear();
    rightNoteQueue.clear();
    // Turn off all 9 voices
    for (int v = 0; v < 9; ++v) {
      releaseNote(v);
    }
  }
}

void BreadbinProcessor::triggerNote(int voiceIndex, int midiNote,
                                    int velocity) {
  voices[voiceIndex].note = midiNote;
  voices[voiceIndex].active = true;

  // Route to appropriate SID (0-2=left, 3-5=center, 6-8=right)
  SIDEngine *sid;
  if (voiceIndex < 3)
    sid = &sidLeft;
  else if (voiceIndex < 6)
    sid = &sidCenter;
  else
    sid = &sidRight;

  int sidVoice = voiceIndex % 3;
  sid->noteOn(sidVoice, midiNote, velocity);
}

void BreadbinProcessor::releaseNote(int voiceIndex) {
  voices[voiceIndex].active = false;

  // Route to appropriate SID (0-2=left, 3-5=center, 6-8=right)
  SIDEngine *sid;
  if (voiceIndex < 3)
    sid = &sidLeft;
  else if (voiceIndex < 6)
    sid = &sidCenter;
  else
    sid = &sidRight;

  int sidVoice = voiceIndex % 3;
  sid->noteOff(sidVoice);
}

void BreadbinProcessor::updateSIDFromQueue(int sidIndex) {
  // sidIndex: 0=left, 1=center, 2=right
  juce::Array<int> *queue;
  switch (sidIndex) {
  case 0:
    queue = &leftNoteQueue;
    break;
  case 1:
    queue = &centerNoteQueue;
    break;
  default:
    queue = &rightNoteQueue;
    break;
  }

  const int startVoice = sidIndex * 3; // 0, 3, or 6

  if (queue->isEmpty()) {
    // No notes held - release all voices on this SID
    for (int v = startVoice; v < startVoice + 3; ++v) {
      if (voices[v].active) {
        releaseNote(v);
      }
    }
  } else {
    // Play latest note (last in queue) on ALL enabled voices
    const int note = queue->getLast();
    for (int v = startVoice; v < startVoice + 3; ++v) {
      if (voiceSettings[v].enabled) {
        triggerNote(v, note, lastVelocity);
      } else if (voices[v].active) {
        releaseNote(v);
      }
    }
  }
}

void BreadbinProcessor::applyVoiceSettings(int voice) {
  if (voice < 0 || voice > 8)
    return;

  // Route to appropriate SID (0-2=left, 3-5=center, 6-8=right)
  SIDEngine *sid;
  if (voice < 3)
    sid = &sidLeft;
  else if (voice < 6)
    sid = &sidCenter;
  else
    sid = &sidRight;

  int sidVoice = voice % 3;
  const auto &settings = voiceSettings[voice];

  sid->setWaveform(sidVoice, settings.waveform);
  sid->setPulseWidth(sidVoice, settings.pulseWidth);
  sid->setAttack(sidVoice, settings.attack);
  sid->setDecay(sidVoice, settings.decay);
  sid->setSustain(sidVoice, settings.sustain);
  sid->setRelease(sidVoice, settings.release);
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
