#include "PluginProcessor.h"
#include "PluginEditor.h"

BreadbinProcessor::BreadbinProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {
  // Initialize both SIDs with default models
  sidLeft.setChipModel(chipModelLeft);
  sidRight.setChipModel(chipModelRight);

  // Initialize MIDI mappings
  midiMappings.fill(ControlParam::None);
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

  // Process glide/portamento for all voices
  if (glideTimeMs > 0.0f) {
    // Calculate glide rate: how much to move per sample
    double glideTimeSec = glideTimeMs / 1000.0;
    double samplesPerGlide = hostSampleRate * glideTimeSec;

    for (int v = 0; v < 6; ++v) {
      if (voices[v].active && voices[v].isGliding) {
        double currentHz = voices[v].currentHz;
        double targetHz = voices[v].targetHz;

        if (std::abs(currentHz - targetHz) < 0.1) {
          // Close enough - snap to target
          voices[v].currentHz = targetHz;
          voices[v].isGliding = false;
        } else {
          // Exponential glide for musical feel
          // Move a fraction of the distance per block
          double glideRate = static_cast<double>(numSamples) / samplesPerGlide;
          double newHz = currentHz + (targetHz - currentHz) * glideRate;
          voices[v].currentHz = newHz;

          // Update SID frequency
          SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
          int sidVoice = v % 3;
          sid.setFrequency(sidVoice, newHz);
        }
      }
    }
  }

  // Process LFO modulation
  if (lfo.enabled) {
    processLFO(numSamples);
  }
  applyLFOModulation(); // Always apply to handle resets when disabled

  auto *leftChannel = buffer.getWritePointer(0);
  auto *rightChannel =
      buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

  // Get input channels for external audio routing
  const float *inputLeft = nullptr;
  const float *inputRight = nullptr;
  if (extInputEnabled && buffer.getNumChannels() > 0) {
    inputLeft = buffer.getReadPointer(0);
    inputRight =
        buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inputLeft;
  }

  // Generate audio from SID engines
  for (int i = 0; i < numSamples; ++i) {
    // Feed external audio to SID filters if enabled
    if (extInputEnabled && inputLeft != nullptr) {
      float extL = inputLeft[i] * extInputLevel;
      float extR = inputRight[i] * extInputLevel;
      sidLeft.setExternalInput(extL);
      sidRight.setExternalInput(extR);
    }

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
      if (arpHeldNotes.empty() && lastArpNote >= 0 && !sustainActive) {
        // Release all voices
        for (int v = 0; v < 6; ++v) {
          releaseNote(v);
        }
        lastArpNote = -1;
      }
      return;
    }

    // If sustain is active, don't remove from queue yet
    if (sustainActive)
      return;

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
  } else if (msg.isPitchWheel()) {
    // Convert 14-bit value (0-16383, center=8192) to -1.0 to +1.0
    pitchBendValue = (msg.getPitchWheelValue() - 8192) / 8192.0f;
    updateAllVoiceFrequencies();
  } else if (msg.isController()) {
    int cc = msg.getControllerNumber();
    int value = msg.getControllerValue();

    handleCC(cc, value);

    if (cc == 1) { // Mod wheel
      modWheelValue = value / 127.0f;
      applyModWheelToFilter();
    } else if (cc == 64) { // Sustain pedal
      bool pedalDown = (value >= 64);
      if (sustainActive != pedalDown) {
        sustainActive = pedalDown;

        // DEBUG: Log sustain activity
        juce::Logger::writeToLog("Sustain: " + (sustainActive
                                                    ? juce::String("ON")
                                                    : juce::String("OFF")));

        if (!sustainActive) {
          // If arp is NOT enabled, we need to release notes that are only
          // held by sustain
          if (!arpEnabled) {
            // Check left SID queue
            for (int i = leftNoteQueue.size(); --i >= 0;) {
              int n = leftNoteQueue[i];
              if (std::find(arpHeldNotes.begin(), arpHeldNotes.end(), n) ==
                  arpHeldNotes.end()) {
                leftNoteQueue.remove(i);
              }
            }
            updateSIDFromQueue(true);

            // Check right SID queue
            for (int i = rightNoteQueue.size(); --i >= 0;) {
              int n = rightNoteQueue[i];
              if (std::find(arpHeldNotes.begin(), arpHeldNotes.end(), n) ==
                  arpHeldNotes.end()) {
                rightNoteQueue.remove(i);
              }
            }
            updateSIDFromQueue(false);
          }
        }
      }
    }
  }
}

void BreadbinProcessor::triggerNote(int voiceIndex, int midiNote,
                                    int velocity) {
  // Calculate target frequency with detune
  float detune = (voiceIndex < 3) ? leftDetuneCents : rightDetuneCents;
  double detuneNote = static_cast<double>(midiNote) + (detune / 100.0);
  double targetHz = 440.0 * std::pow(2.0, (detuneNote - 69.0) / 12.0);

  bool wasActive = voices[voiceIndex].active;
  double previousHz = voices[voiceIndex].currentHz;

  voices[voiceIndex].note = midiNote;
  voices[voiceIndex].active = true;
  voices[voiceIndex].targetHz = targetHz;

  SIDEngine &sid = (voiceIndex < 3) ? sidLeft : sidRight;
  int sidVoice = voiceIndex % 3;

  // Glide: if voice was already playing and glide is enabled, slide pitch
  if (wasActive && glideTimeMs > 0.0f) {
    // Start gliding from current frequency to target
    voices[voiceIndex].isGliding = true;
    // currentHz stays at previousHz - processBlock will interpolate
    // Send immediate frequency update to start from current position
    sid.setFrequency(sidVoice, previousHz);
  } else {
    // No glide - normal note trigger
    voices[voiceIndex].currentHz = targetHz;
    voices[voiceIndex].isGliding = false;
    sid.noteOn(sidVoice, midiNote, velocity, detune);
  }
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
  sid.setRingMod(sidVoice, settings.ringMod);
  sid.setSync(sidVoice, settings.sync);

  // Update per-voice filter routing on this SID
  bool isLeft = (voice < 3);
  auto &v0 = voiceSettings[isLeft ? 0 : 3];
  auto &v1 = voiceSettings[isLeft ? 1 : 4];
  auto &v2 = voiceSettings[isLeft ? 2 : 5];
  sid.setFilterVoices(v0.filterEnabled, v1.filterEnabled, v2.filterEnabled);
}

void BreadbinProcessor::updateAllVoiceFrequencies() {
  // Apply pitch bend to all active voices
  for (int v = 0; v < 6; ++v) {
    if (voices[v].active) {
      float detune = (v < 3) ? leftDetuneCents : rightDetuneCents;
      float bendSemitones = pitchBendValue * static_cast<float>(pitchBendRange);
      double note = static_cast<double>(voices[v].note) + bendSemitones +
                    (detune / 100.0);
      double hz = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);

      SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
      sid.setFrequency(v % 3, hz);
    }
  }
}

void BreadbinProcessor::applyModWheelToFilter() {
  // Mod wheel adds 0-1000 to filter cutoff (opening the filter)
  int modOffset = static_cast<int>(modWheelValue * 1000.0f);

  // Apply to both SIDs (clamped to valid range 0-2047)
  int leftCutoff = juce::jlimit(0, 2047, baseFilterCutoffLeft + modOffset);
  int rightCutoff = juce::jlimit(0, 2047, baseFilterCutoffRight + modOffset);

  sidLeft.setFilterCutoff(leftCutoff);
  sidRight.setFilterCutoff(rightCutoff);
}

void BreadbinProcessor::setMasterVolume(float vol) {
  masterVolume = juce::jlimit(0.0f, 1.0f, vol);
  int sidVol = static_cast<int>(masterVolume * 15.0f);
  sidLeft.setVolume(sidVol);
  sidRight.setVolume(sidVol);
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

juce::ValueTree BreadbinProcessor::getVoiceState(int v) const {
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
  voiceState.setProperty("filterEnabled", vs.filterEnabled, nullptr);
  voiceState.setProperty("ringMod", vs.ringMod, nullptr);
  voiceState.setProperty("sync", vs.sync, nullptr);
  return voiceState;
}

void BreadbinProcessor::setVoiceState(int v,
                                      const juce::ValueTree &voiceState) {
  if (!voiceState.isValid())
    return;

  auto &vs = voiceSettings[v];
  vs.enabled = voiceState.getProperty("enabled", true);
  vs.waveform = static_cast<SIDEngine::Waveform>(
      static_cast<int>(voiceState.getProperty("waveform", 0)));
  vs.pulseWidth = voiceState.getProperty("pulseWidth", 2048);
  vs.attack = voiceState.getProperty("attack", 0);
  vs.decay = voiceState.getProperty("decay", 0);
  vs.sustain = voiceState.getProperty("sustain", 15);
  vs.release = voiceState.getProperty("release", 0);
  vs.pan = voiceState.getProperty("pan", 0.0f);
  vs.presetId = voiceState.getProperty("presetId", 1);
  vs.filterEnabled = voiceState.getProperty("filterEnabled", true);
  vs.ringMod = voiceState.getProperty("ringMod", false);
  vs.sync = voiceState.getProperty("sync", false);

  applyVoiceSettings(v);
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
  state.setProperty("leftDetune", leftDetuneCents, nullptr);
  state.setProperty("rightDetune", rightDetuneCents, nullptr);
  state.setProperty("glideTime", glideTimeMs, nullptr);
  state.setProperty("pitchBendRange", pitchBendRange, nullptr);
  state.setProperty("masterVolume", masterVolume, nullptr);
  state.setProperty("extInputEnabled", extInputEnabled, nullptr);
  state.setProperty("extInputLevel", extInputLevel, nullptr);
  state.setProperty("clockMode", static_cast<int>(clockMode), nullptr);

  // Save LFO settings
  state.setProperty("lfoEnabled", lfo.enabled, nullptr);
  state.setProperty("lfoWaveform", static_cast<int>(lfo.waveform), nullptr);
  state.setProperty("lfoRate", lfo.rate, nullptr);
  state.setProperty("lfoDepthFilter", lfo.depthFilter, nullptr);
  state.setProperty("lfoDepthPW", lfo.depthPulseWidth, nullptr);
  state.setProperty("lfoDepthPitch", lfo.depthPitch, nullptr);

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
    voiceState.setProperty("filterEnabled", vs.filterEnabled, nullptr);
    state.addChild(voiceState, -1, nullptr);
  }

  // Save MIDI mappings
  juce::ValueTree mappings("MidiMappings");
  for (int i = 0; i < 128; ++i) {
    if (midiMappings[i] != ControlParam::None) {
      juce::ValueTree m("Map");
      m.setProperty("cc", i, nullptr);
      m.setProperty("param", static_cast<int>(midiMappings[i]), nullptr);
      mappings.addChild(m, -1, nullptr);
    }
  }
  state.addChild(mappings, -1, nullptr);
  state.setProperty("selectedVoice", selectedVoice, nullptr);

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
    setLeftDetune(state.getProperty("leftDetune", 0.0f));
    setRightDetune(state.getProperty("rightDetune", 0.0f));
    setGlideTimeMs(state.getProperty("glideTime", 0.0f));
    setPitchBendRange(state.getProperty("pitchBendRange", 2));
    setMasterVolume(state.getProperty("masterVolume", 0.8f));
    extInputEnabled = state.getProperty("extInputEnabled", false);
    extInputLevel = state.getProperty("extInputLevel", 1.0f);
    setClockMode(static_cast<SIDEngine::ClockMode>(
        static_cast<int>(state.getProperty("clockMode", 0))));

    // Restore LFO settings
    lfo.enabled = state.getProperty("lfoEnabled", false);
    lfo.waveform = static_cast<LFOWaveform>(
        static_cast<int>(state.getProperty("lfoWaveform", 0)));
    lfo.rate = state.getProperty("lfoRate", 2.0f);
    lfo.depthFilter = state.getProperty("lfoDepthFilter", 0.0f);
    lfo.depthPulseWidth = state.getProperty("lfoDepthPW", 0.0f);
    lfo.depthPitch = state.getProperty("lfoDepthPitch", 0.0f);

    // Restore MIDI mappings
    midiMappings.fill(ControlParam::None);
    auto mappings = state.getChildWithName("MidiMappings");
    if (mappings.isValid()) {
      for (int i = 0; i < mappings.getNumChildren(); ++i) {
        auto m = mappings.getChild(i);
        int cc = m.getProperty("cc", -1);
        int param = m.getProperty("param", 0);
        if (cc >= 0 && cc < 128) {
          midiMappings[cc] = static_cast<ControlParam>(param);
        }
      }
    }
    selectedVoice = state.getProperty("selectedVoice", 0);

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
        vs.filterEnabled = voiceState.getProperty("filterEnabled", true);
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

  // Sync Arp clock with chip clock mode (NTSC = 1.2x speed)
  double clockScale = (clockMode == SIDEngine::ClockMode::NTSC) ? 1.2 : 1.0;
  double samplesPerStep =
      hostSampleRate / (static_cast<double>(arpRateHz) * clockScale);
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

void BreadbinProcessor::processLFO(int numSamples) {
  // Advance phase
  double phaseInc =
      (static_cast<double>(lfo.rate) * numSamples) / hostSampleRate;
  double oldPhase = lfo.phase;
  lfo.phase += phaseInc;
  lfo.phase -= std::floor(lfo.phase); // Wrap to 0-1

  // Generate waveform value (-1.0 to +1.0)
  float p = static_cast<float>(lfo.phase);
  switch (lfo.waveform) {
  case LFOWaveform::Triangle:
    lfo.currentValue = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    break;
  case LFOWaveform::Sawtooth:
    lfo.currentValue = 2.0f * p - 1.0f;
    break;
  case LFOWaveform::Square:
    lfo.currentValue = (p < 0.5f) ? 1.0f : -1.0f;
    break;
  case LFOWaveform::SampleAndHold:
    // Latch new random value on phase wrap
    if (lfo.phase < oldPhase) {
      static std::mt19937 rng(std::random_device{}());
      std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
      lfo.shValue = dist(rng);
    }
    lfo.currentValue = lfo.shValue;
    break;
  }
}

void BreadbinProcessor::applyLFOModulation() {
  float val = lfo.enabled ? lfo.currentValue : 0.0f;

  // Filter cutoff modulation
  if (lfo.depthFilter > 0.0f) {
    int modAmount = static_cast<int>(val * lfo.depthFilter * 1024.0f);
    int leftCutoff = std::clamp(baseFilterCutoffLeft + modAmount, 0, 2047);
    int rightCutoff = std::clamp(baseFilterCutoffRight + modAmount, 0, 2047);
    sidLeft.setFilterCutoff(leftCutoff);
    sidRight.setFilterCutoff(rightCutoff);
  } else {
    sidLeft.setFilterCutoff(baseFilterCutoffLeft);
    sidRight.setFilterCutoff(baseFilterCutoffRight);
  }

  // Pulse width modulation
  if (lfo.depthPulseWidth > 0.0f) {
    int pwMod = static_cast<int>(val * lfo.depthPulseWidth * 2048.0f);
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active) {
        int basePW = voiceSettings[v].pulseWidth;
        int modPW = std::clamp(basePW + pwMod, 0, 4095);
        SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
        sid.setPulseWidth(v % 3, modPW);
      }
    }
  } else {
    for (int v = 0; v < 6; ++v) {
      SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
      sid.setPulseWidth(v % 3, voiceSettings[v].pulseWidth);
    }
  }

  // Pitch modulation (vibrato) - ±2 semitones max
  if (lfo.depthPitch > 0.0f) {
    float semitoneMod = val * lfo.depthPitch * 2.0f;
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active) {
        double baseHz =
            voices[v].isGliding ? voices[v].currentHz : voices[v].targetHz;
        double modHz = baseHz * std::pow(2.0, semitoneMod / 12.0);
        SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
        sid.setFrequency(v % 3, modHz);
      }
    }
  } else {
    for (int v = 0; v < 6; ++v) {
      double resetHz =
          voices[v].isGliding ? voices[v].currentHz : voices[v].targetHz;
      SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
      sid.setFrequency(v % 3, resetHz);
    }
  }
}

// Plugin instantiation
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new BreadbinProcessor();
}

void BreadbinProcessor::handleCC(int cc, int value) {
  if (cc < 0 || cc >= 128)
    return;

  // Learn mode logic
  if (learningParam != ControlParam::None) {
    midiMappings[cc] = learningParam;
    learningParam = ControlParam::None; // Stop learning once mapped

    // DEBUG: Log new mapping
    juce::Logger::writeToLog("MIDI Mapped: CC " + juce::String(cc) +
                             " to Param " +
                             juce::String(static_cast<int>(midiMappings[cc])));
    return;
  }

  // Normal mapping logic
  ControlParam mapped = midiMappings[cc];
  if (mapped != ControlParam::None) {
    applyMappedParameter(mapped, value);
  }
}

void BreadbinProcessor::applyMappedParameter(ControlParam param, int value) {
  float normalized = value / 127.0f;

  switch (param) {
  case ControlParam::MasterVolume:
    setMasterVolume(normalized);
    break;
  case ControlParam::Aging:
    setAgingFactor(normalized);
    break;
  case ControlParam::LeftCutoff:
    sidLeft.setFilterCutoff(static_cast<int>(normalized * 2047.0f));
    break;
  case ControlParam::LeftResonance:
    sidLeft.setFilterResonance(static_cast<int>(normalized * 15.0f));
    break;
  case ControlParam::RightCutoff:
    sidRight.setFilterCutoff(static_cast<int>(normalized * 2047.0f));
    break;
  case ControlParam::RightResonance:
    sidRight.setFilterResonance(static_cast<int>(normalized * 15.0f));
    break;
  case ControlParam::GlobalGlide:
    setGlideTimeMs(normalized * 2000.0f);
    break;
  case ControlParam::PitchBendRange:
    setPitchBendRange(2 + static_cast<int>(normalized * 10.0f));
    break;
  case ControlParam::LFORate:
    lfo.rate = 0.1f + (normalized * 19.9f);
    break;
  case ControlParam::LFODepthFilter:
    lfo.depthFilter = normalized;
    break;
  case ControlParam::LFODepthPW:
    lfo.depthPulseWidth = normalized;
    break;
  case ControlParam::LFODepthPitch:
    lfo.depthPitch = normalized;
    break;

  // Per-voice parameters (mapped to selectedVoice)
  case ControlParam::VoiceWaveform: {
    int wfIdx = static_cast<int>(normalized * 3.0f);
    SIDEngine::Waveform wfs[] = {
        SIDEngine::Waveform::Triangle, SIDEngine::Waveform::Sawtooth,
        SIDEngine::Waveform::Pulse, SIDEngine::Waveform::Noise};
    voiceSettings[selectedVoice].waveform = wfs[wfIdx];
    applyVoiceSettings(selectedVoice);
  } break;
  case ControlParam::VoicePW:
    voiceSettings[selectedVoice].pulseWidth =
        static_cast<int>(normalized * 4095.0f);
    applyVoiceSettings(selectedVoice);
    break;
  case ControlParam::VoiceAttack:
    voiceSettings[selectedVoice].attack = static_cast<int>(normalized * 15.0f);
    applyVoiceSettings(selectedVoice);
    break;
  case ControlParam::VoiceDecay:
    voiceSettings[selectedVoice].decay = static_cast<int>(normalized * 15.0f);
    applyVoiceSettings(selectedVoice);
    break;
  case ControlParam::VoiceSustain:
    voiceSettings[selectedVoice].sustain = static_cast<int>(normalized * 15.0f);
    applyVoiceSettings(selectedVoice);
    break;
  case ControlParam::VoiceRelease:
    voiceSettings[selectedVoice].release = static_cast<int>(normalized * 15.0f);
    applyVoiceSettings(selectedVoice);
    break;
  case ControlParam::VoicePan:
    voiceSettings[selectedVoice].pan = (normalized * 2.0f) - 1.0f;
    break;
  case ControlParam::VoiceRingMod:
    voiceSettings[selectedVoice].ringMod = (value >= 64);
    applyVoiceSettings(selectedVoice);
    break;
  case ControlParam::VoiceSync:
    voiceSettings[selectedVoice].sync = (value >= 64);
    applyVoiceSettings(selectedVoice);
    break;
  case ControlParam::VoiceFilterEnable:
    voiceSettings[selectedVoice].filterEnabled = (value >= 64);
    applyVoiceSettings(selectedVoice);
    break;
  case ControlParam::ArpRate:
    setArpRate(1.0f + (normalized * 99.0f));
    break;
  case ControlParam::ExtInputLevel:
    setExtInputLevel(normalized * 2.0f);
    break;
  default:
    break;
  }
}

void BreadbinProcessor::setMIDIMapping(int cc, ControlParam param) {
  if (cc >= 0 && cc < 128)
    midiMappings[cc] = param;
}

BreadbinProcessor::ControlParam
BreadbinProcessor::getMIDIMapping(int cc) const {
  if (cc >= 0 && cc < 128)
    return midiMappings[cc];
  return ControlParam::None;
}

void BreadbinProcessor::clearMIDIMapping(int cc) {
  if (cc >= 0 && cc < 128)
    midiMappings[cc] = ControlParam::None;
}

void BreadbinProcessor::clearMIDIMappingForParam(ControlParam param) {
  for (int i = 0; i < 128; ++i) {
    if (midiMappings[i] == param) {
      midiMappings[i] = ControlParam::None;
    }
  }
}
