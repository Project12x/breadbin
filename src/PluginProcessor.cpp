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

  // Apply voice settings to all 6 voices so they're ready for polyphony
  for (int v = 0; v < 6; ++v) {
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

  // Process arpeggiator
  const int numSamples = buffer.getNumSamples();
  if (arpEnabled && !arpSequence.empty()) {
    processArpeggiator(numSamples);
  }
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

  // Apply safety chain (DC blocker, ultrasonic filter, limiter)
  juce::dsp::AudioBlock<float> block(buffer);
  juce::dsp::ProcessContextReplacing<float> context(block);
  subsonicFilter.process(context);
  ultrasonicFilter.process(context);
  safetyLimiter.process(context);

  // Simple noise gate to silence residual drone (-40dB threshold)
  constexpr float noiseGateThreshold = 0.01f; // ~-40dB
  for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
    auto *channelData = buffer.getWritePointer(ch);
    for (int i = 0; i < numSamples; ++i) {
      if (std::abs(channelData[i]) < noiseGateThreshold) {
        channelData[i] = 0.0f;
      }
    }
  }
}

void BreadbinProcessor::handleMidiEvent(const juce::MidiMessage &msg) {
  if (msg.isNoteOn()) {
    const int note = msg.getNoteNumber();
    lastVelocity = msg.getVelocity();
    const int channel = msg.getChannel();

    // Track for arpeggiator
    if (std::find(arpHeldNotes.begin(), arpHeldNotes.end(), note) ==
        arpHeldNotes.end()) {
      arpHeldNotes.push_back(note);
      rebuildArpSequence();
    }

    // If arp is enabled, don't do normal note handling
    if (arpEnabled)
      return;

    if (dualMode == DualMode::Multitimbral) {
      // Channel 1 -> left SID, Channel 2 -> right SID
      if (channel == 2) {
        rightNoteQueue.addIfNotAlreadyThere(note);
        updateSIDFromQueue(false); // right SID
      } else {
        leftNoteQueue.addIfNotAlreadyThere(note);
        updateSIDFromQueue(true); // left SID
      }
    } else {
      // Stereo/Unison: both SIDs play the same notes
      leftNoteQueue.addIfNotAlreadyThere(note);
      rightNoteQueue.addIfNotAlreadyThere(note);
      updateSIDFromQueue(true);  // left
      updateSIDFromQueue(false); // right
    }
  } else if (msg.isNoteOff()) {
    const int note = msg.getNoteNumber();

    // Remove from arp tracking
    auto it = std::find(arpHeldNotes.begin(), arpHeldNotes.end(), note);
    if (it != arpHeldNotes.end()) {
      arpHeldNotes.erase(it);
      rebuildArpSequence();
    }

    // If arp is enabled, handle release when no notes held
    if (arpEnabled) {
      if (arpHeldNotes.empty() && lastArpNote >= 0) {
        // Release all voices
        for (int v = 0; v < 6; ++v) {
          releaseNote(v);
        }
        lastArpNote = -1;
      }
      return;
    }

    // Remove from queue(s)
    leftNoteQueue.removeFirstMatchingValue(note);
    rightNoteQueue.removeFirstMatchingValue(note);

    // Update SIDs to play the previous note (or off if queue empty)
    updateSIDFromQueue(true);  // left
    updateSIDFromQueue(false); // right
  } else if (msg.isAllNotesOff()) {
    arpHeldNotes.clear();
    arpSequence.clear();
    lastArpNote = -1;

    leftNoteQueue.clear();
    rightNoteQueue.clear();
    // Turn off all voices
    for (int v = 0; v < 6; ++v) {
      releaseNote(v);
    }
  }
}

void BreadbinProcessor::triggerNote(int voiceIndex, int midiNote,
                                    int velocity) {
  voices[voiceIndex].note = midiNote;
  voices[voiceIndex].active = true;

  // Route to appropriate SID with detune applied
  SIDEngine &sid = (voiceIndex < 3) ? sidLeft : sidRight;
  float detune = (voiceIndex < 3) ? leftDetuneCents : rightDetuneCents;
  int sidVoice = voiceIndex % 3;

  sid.noteOn(sidVoice, midiNote, velocity, detune);
}

void BreadbinProcessor::releaseNote(int voiceIndex) {
  voices[voiceIndex].active = false;

  SIDEngine &sid = (voiceIndex < 3) ? sidLeft : sidRight;
  int sidVoice = voiceIndex % 3;

  sid.noteOff(sidVoice);
}

void BreadbinProcessor::updateSIDFromQueue(bool isLeftSID) {
  auto &queue = isLeftSID ? leftNoteQueue : rightNoteQueue;
  const int startVoice = isLeftSID ? 0 : 3;

  if (queue.isEmpty()) {
    // No notes held - release all voices on this SID
    for (int v = startVoice; v < startVoice + 3; ++v) {
      if (voices[v].active) {
        releaseNote(v);
      }
    }
  } else {
    // Play latest note (last in queue) on ALL enabled voices
    const int note = queue.getLast();
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

  // Save per-voice settings
  for (int v = 0; v < 6; ++v) {
    juce::ValueTree voiceState("Voice" + juce::String(v));
    const auto &vs = voiceSettings[v];
    voiceState.setProperty("enabled", vs.enabled, nullptr);
    voiceState.setProperty("waveform", static_cast<int>(vs.waveform), nullptr);
    voiceState.setProperty("pulseWidth", vs.pulseWidth, nullptr);
    voiceState.setProperty("attack", vs.attack, nullptr);
    voiceState.setProperty("decay", vs.decay, nullptr);
    voiceState.setProperty("sustain", vs.sustain, nullptr);
    voiceState.setProperty("release", vs.release, nullptr);
    voiceState.setProperty("pan", vs.pan, nullptr);
    voiceState.setProperty("presetId", vs.presetId, nullptr);
    state.addChild(voiceState, -1, nullptr);
  }

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

    // Restore per-voice settings
    for (int v = 0; v < 6; ++v) {
      auto voiceState = state.getChildWithName("Voice" + juce::String(v));
      if (voiceState.isValid()) {
        auto &vs = voiceSettings[v];
        vs.enabled = voiceState.getProperty("enabled", true);
        vs.waveform = static_cast<SIDEngine::Waveform>(
            static_cast<int>(voiceState.getProperty("waveform", 0x10)));
        vs.pulseWidth = voiceState.getProperty("pulseWidth", 2048);
        vs.attack = voiceState.getProperty("attack", 0);
        vs.decay = voiceState.getProperty("decay", 0);
        vs.sustain = voiceState.getProperty("sustain", 15);
        vs.release = voiceState.getProperty("release", 0);
        vs.pan = voiceState.getProperty("pan", 0.0f);
        vs.presetId = voiceState.getProperty("presetId", 1);
        applyVoiceSettings(v);
      }
    }
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

// ============ ARPEGGIATOR ============

void BreadbinProcessor::setArpPattern(ArpPattern pattern) {
  arpPattern = pattern;
  rebuildArpSequence();
}

void BreadbinProcessor::rebuildArpSequence() {
  arpSequence.clear();
  if (arpHeldNotes.empty())
    return;

  // Sort held notes
  std::vector<int> sorted = arpHeldNotes;
  std::sort(sorted.begin(), sorted.end());

  // Build base sequence with octave expansion
  std::vector<int> base;
  for (int oct = 0; oct < arpOctaves; ++oct) {
    for (int note : sorted) {
      int transposed = note + (oct * 12);
      if (transposed <= 127) {
        base.push_back(transposed);
      }
    }
  }

  // Apply pattern
  switch (arpPattern) {
  case ArpPattern::Up:
    arpSequence = base;
    break;

  case ArpPattern::Down:
    arpSequence = base;
    std::reverse(arpSequence.begin(), arpSequence.end());
    break;

  case ArpPattern::UpDown:
    arpSequence = base;
    if (base.size() > 1) {
      // Add reversed (excluding first and last to avoid doubles)
      for (int i = static_cast<int>(base.size()) - 2; i > 0; --i) {
        arpSequence.push_back(base[i]);
      }
    }
    break;

  case ArpPattern::Random:
    arpSequence = base;
    {
      static std::mt19937 rng(std::random_device{}());
      std::shuffle(arpSequence.begin(), arpSequence.end(), rng);
    }
    break;
  }

  // Reset index if out of bounds
  if (arpIndex >= static_cast<int>(arpSequence.size())) {
    arpIndex = 0;
  }
}

void BreadbinProcessor::processArpeggiator(int numSamples) {
  if (arpSequence.empty())
    return;

  double samplesPerStep = hostSampleRate / static_cast<double>(arpRateHz);
  arpTimer += numSamples;

  while (arpTimer >= samplesPerStep) {
    arpTimer -= samplesPerStep;

    // Get next note
    int note = arpSequence[arpIndex];

    // Release previous note if different
    if (lastArpNote >= 0 && lastArpNote != note) {
      for (int v = 0; v < 6; ++v) {
        if (voices[v].note == lastArpNote) {
          releaseNote(v);
        }
      }
    }

    // Trigger new note on all enabled voices
    for (int v = 0; v < 6; ++v) {
      if (voiceSettings[v].enabled) {
        triggerNote(v, note, lastVelocity);
      }
    }

    lastArpNote = note;

    // Advance index
    arpIndex = (arpIndex + 1) % static_cast<int>(arpSequence.size());

    // Reshuffle on wrap for random mode
    if (arpPattern == ArpPattern::Random && arpIndex == 0) {
      static std::mt19937 rng(std::random_device{}());
      std::shuffle(arpSequence.begin(), arpSequence.end(), rng);
    }
  }
}

// Plugin instantiation
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new BreadbinProcessor();
}
