#include "PluginProcessor.h"
#include "PluginEditor.h"

BreadbinProcessor::BreadbinProcessor()
    : juce::AudioProcessor(
          juce::AudioProcessor::BusesProperties()
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {
  // Initialize both SIDs with default models
  sidLeft.setChipModel(chipModelLeft);
  sidRight.setChipModel(chipModelRight);

  // Link parameter pointers for fast access
  initializeParameterPointers();

  // Initialize MIDI mappings
  midiMappings.fill(ControlParam::None);
}

BreadbinProcessor::~BreadbinProcessor() = default;

bool BreadbinProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  // Output must be stereo
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

  // Input (sidechain) is optional: disabled or stereo
  auto inputSet = layouts.getMainInputChannelSet();
  if (!inputSet.isDisabled() && inputSet != juce::AudioChannelSet::stereo())
    return false;

  return true;
}

void BreadbinProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  // Set base class rate so getSampleRate() works in headless/test mode
  setRateAndBufferSizeDetails(sampleRate, samplesPerBlock);

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

  // Sync global parameters from APVTS
  masterVolume = masterVolPtr->load();
  setMasterVolume(masterVolume); // Sync SID volume register from APVTS
  dualMode = static_cast<DualMode>(static_cast<int>(dualModePtr->load()));
  setAgingFactor(agingPtr->load());
  leftDetuneCents = leftDetunePtr->load();
  rightDetuneCents = rightDetunePtr->load();
  glideTimeMs = glidePtr->load();
  extInputEnabled = extInputEnablePtr->load() > 0.5f;
  extInputLevel = extInputLevelPtr->load();

  // Sync LFO from APVTS
  lfo.enabled = lfoEnablePtr->load() > 0.5f;
  lfo.waveform = static_cast<LFOWaveform>(static_cast<int>(lfoWavePtr->load()));
  lfo.rate = lfoRatePtr->load();
  lfo.depthFilter = lfoDepthFiltPtr->load();
  lfo.depthPulseWidth = lfoDepthPWPtr->load();
  lfo.depthPitch = lfoDepthPitchPtr->load();

  // Sync Arp from APVTS
  arpEnabled = arpEnablePtr->load() > 0.5f;
  arpPattern = static_cast<ArpPattern>(static_cast<int>(arpPatternPtr->load()));
  arpRateHz = arpRatePtr->load();
  arpOctaves = static_cast<int>(arpOctavesPtr->load());

  // Check for discrete changes (handled by SIDEngine/Processor comparisons
  // internally)
  auto newLeftModel =
      static_cast<SIDEngine::ChipModel>(static_cast<int>(chipLeftPtr->load()));
  if (newLeftModel != chipModelLeft) {
    sidLeft.setChipModel(newLeftModel);
    chipModelLeft = newLeftModel;
  }

  auto newRightModel =
      static_cast<SIDEngine::ChipModel>(static_cast<int>(chipRightPtr->load()));
  if (newRightModel != chipModelRight) {
    sidRight.setChipModel(newRightModel);
    chipModelRight = newRightModel;
  }

  auto newClockMode =
      static_cast<SIDEngine::ClockMode>(static_cast<int>(clockModePtr->load()));
  if (newClockMode != clockMode)
    setClockMode(newClockMode);

  // Sync all voice settings (ideally we should only do this on change,
  // but let's do it for now to ensure reactivity)
  for (int v = 0; v < 6; ++v) {
    applyVoiceSettings(v);
  }

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

  // Get input channels from sidechain bus for external audio routing
  const float *inputLeft = nullptr;
  const float *inputRight = nullptr;
  if (extInputEnabled) {
    auto inputBus = getBusBuffer(buffer, true, 0);
    if (inputBus.getNumChannels() > 0) {
      inputLeft = inputBus.getReadPointer(0);
      inputRight = inputBus.getNumChannels() > 1 ? inputBus.getReadPointer(1)
                                                 : inputLeft;
    }
  }

  // Compute per-SID pan from voice settings (average of active voice pans)
  float leftSidPan = 0.0f;
  float rightSidPan = 0.0f;
  {
    int leftCount = 0, rightCount = 0;
    for (int v = 0; v < 3; ++v) {
      if (voiceSettings[v].enabled) {
        leftSidPan += voiceSettings[v].pan;
        ++leftCount;
      }
      if (voiceSettings[v + 3].enabled) {
        rightSidPan += voiceSettings[v + 3].pan;
        ++rightCount;
      }
    }
    if (leftCount > 0)
      leftSidPan /= static_cast<float>(leftCount);
    if (rightCount > 0)
      rightSidPan /= static_cast<float>(rightCount);
  }

  // Per-SID pan: offset from natural position (left SID→left, right SID→right)
  // pan=0 preserves original stereo split behavior (backward compatible)
  // pan=-1 shifts toward left, pan=+1 shifts toward right
  constexpr float piOver2 = 1.5707963267948966f; // pi/2
  // Left SID natural position = full left (angle 0); shift toward center/right
  float leftAngle =
      juce::jlimit(0.0f, piOver2, leftSidPan * piOver2 * 0.5f + 0.0f);
  float leftGainL = std::cos(leftAngle);
  float leftGainR = std::sin(leftAngle);
  // Right SID natural position = full right (angle pi/2); shift toward
  // center/left
  float rightAngle =
      juce::jlimit(0.0f, piOver2, piOver2 + rightSidPan * piOver2 * -0.5f);
  float rightGainL = std::cos(rightAngle);
  float rightGainR = std::sin(rightAngle);

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

    // Apply per-voice pan (averaged per SID)
    float outL = sampleL * leftGainL + sampleR * rightGainL;
    float outR = sampleL * leftGainR + sampleR * rightGainR;

    switch (dualMode) {
    case DualMode::StereoSplit:
    case DualMode::Multitimbral:
      leftChannel[i] = outL;
      if (rightChannel)
        rightChannel[i] = outR;
      break;

    case DualMode::Unison:
      // Mix both SIDs to both channels (pan still applies for spatial width)
      leftChannel[i] = (outL + outR) * 0.5f;
      if (rightChannel)
        rightChannel[i] = leftChannel[i];
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

  auto &ptrs = voiceParamPtrs[voice];
  auto &vs = voiceSettings[voice];

  // Update cache from APVTS pointers
  vs.enabled = ptrs.enable->load() > 0.5f;

  // Waveform mapping (Triangle=0, Sawtooth=1, Pulse=2, Noise=3)
  int waveIdx = static_cast<int>(ptrs.waveform->load());
  switch (waveIdx) {
  case 0:
    vs.waveform = SIDEngine::Waveform::Triangle;
    break;
  case 1:
    vs.waveform = SIDEngine::Waveform::Sawtooth;
    break;
  case 2:
    vs.waveform = SIDEngine::Waveform::Pulse;
    break;
  case 3:
    vs.waveform = SIDEngine::Waveform::Noise;
    break;
  default:
    vs.waveform = SIDEngine::Waveform::Triangle;
    break;
  }

  vs.pulseWidth = static_cast<int>(ptrs.pw->load());
  vs.attack = static_cast<int>(ptrs.attack->load());
  vs.decay = static_cast<int>(ptrs.decay->load());
  vs.sustain = static_cast<int>(ptrs.sustain->load());
  vs.release = static_cast<int>(ptrs.release->load());
  vs.pan = ptrs.pan->load();
  vs.ringMod = ptrs.ringMod->load() > 0.5f;
  vs.sync = ptrs.sync->load() > 0.5f;
  vs.filterEnabled = ptrs.filter->load() > 0.5f;

  SIDEngine &sid = (voice < 3) ? sidLeft : sidRight;
  int sidVoice = voice % 3;

  sid.setWaveform(sidVoice, vs.waveform);
  sid.setPulseWidth(sidVoice, vs.pulseWidth);
  sid.setAttack(sidVoice, vs.attack);
  sid.setDecay(sidVoice, vs.decay);
  sid.setSustain(sidVoice, vs.sustain);
  sid.setRelease(sidVoice, vs.release);
  sid.setRingMod(sidVoice, vs.ringMod);
  sid.setSync(sidVoice, vs.sync);

  // Update per-voice filter routing on this SID
  bool isLeft = (voice < 3);
  int startV = isLeft ? 0 : 3;
  sid.setFilterVoices(voiceParamPtrs[startV + 0].filter->load() > 0.5f,
                      voiceParamPtrs[startV + 1].filter->load() > 0.5f,
                      voiceParamPtrs[startV + 2].filter->load() > 0.5f);
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
  // Store non-APVTS data into apvts.state as properties/children
  // (APVTS parameters are already stored in apvts.state automatically)

  // Remove old MIDI mappings child if present (avoid duplicates)
  auto existingMappings = apvts.state.getChildWithName("MidiMappings");
  if (existingMappings.isValid())
    apvts.state.removeChild(existingMappings, nullptr);

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
  apvts.state.addChild(mappings, -1, nullptr);
  apvts.state.setProperty("selectedVoice", selectedVoice, nullptr);

  auto apvtsState = apvts.copyState();
  std::unique_ptr<juce::XmlElement> xml(apvtsState.createXml());
  copyXmlToBinary(*xml, destData);
}

void BreadbinProcessor::setStateInformation(const void *data, int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xmlState(
      getXmlFromBinary(data, sizeInBytes));
  if (xmlState != nullptr) {
    if (xmlState->hasTagName(apvts.state.getType())) {
      apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

      // Restore MIDI mappings and other non-APVTS state
      midiMappings.fill(ControlParam::None);
      auto mappings = apvts.state.getChildWithName("MidiMappings");
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
      selectedVoice = apvts.state.getProperty("selectedVoice", 0);
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

juce::String BreadbinProcessor::getParamName(ControlParam param) {
  switch (param) {
  case ControlParam::MasterVolume:
    return "Master Volume";
  case ControlParam::Aging:
    return "Chip Age";
  case ControlParam::LeftCutoff:
    return "Left SID Cutoff";
  case ControlParam::LeftResonance:
    return "Left SID Resonance";
  case ControlParam::RightCutoff:
    return "Right SID Cutoff";
  case ControlParam::RightResonance:
    return "Right SID Resonance";
  case ControlParam::GlobalGlide:
    return "Portamento";
  case ControlParam::PitchBendRange:
    return "PB Range";
  case ControlParam::LFORate:
    return "LFO Rate";
  case ControlParam::LFODepthFilter:
    return "LFO Filter Depth";
  case ControlParam::LFODepthPW:
    return "LFO PWM Depth";
  case ControlParam::LFODepthPitch:
    return "LFO Pitch Depth";
  case ControlParam::VoiceWaveform:
    return "Voice Waveform";
  case ControlParam::VoicePW:
    return "Voice Pulse Width";
  case ControlParam::VoiceAttack:
    return "Voice Attack";
  case ControlParam::VoiceDecay:
    return "Voice Decay";
  case ControlParam::VoiceSustain:
    return "Voice Sustain";
  case ControlParam::VoiceRelease:
    return "Voice Release";
  case ControlParam::VoicePan:
    return "Voice Pan";
  case ControlParam::VoiceRingMod:
    return "Voice Ring Mod";
  case ControlParam::VoiceSync:
    return "Voice Hard Sync";
  case ControlParam::VoiceFilterEnable:
    return "Voice Filter Enable";
  case ControlParam::ArpRate:
    return "Arp Rate";
  case ControlParam::ExtInputLevel:
    return "Ext Input Level";
  case ControlParam::LeftDetune:
    return "Left SID Detune";
  case ControlParam::RightDetune:
    return "Right SID Detune";
  default:
    return "Unknown";
  }
}

juce::AudioProcessorValueTreeState::ParameterLayout
BreadbinProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  // Global
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"masterVol", 1}, "Master Volume", 0.0f, 1.0f, 0.8f));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"dualMode", 1}, "Dual SID Mode",
      juce::StringArray{"Stereo Split", "Unison", "Multitimbral"}, 0));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"chipLeft", 1}, "Left Chip Model",
      juce::StringArray{"MOS6581", "MOS8580"}, 0));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"chipRight", 1}, "Right Chip Model",
      juce::StringArray{"MOS6581", "MOS8580"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"aging", 1}, "Chip Age", 0.0f, 1.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"leftDetune", 1}, "Left Detune", -50.0f, 50.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"rightDetune", 1}, "Right Detune", -50.0f, 50.0f,
      0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"glide", 1}, "Glide Time", 0.0f, 2000.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"clockMode", 1}, "Clock Mode",
      juce::StringArray{"PAL", "NTSC"}, 0));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"extInputEnable", 1}, "Ext Input Enable", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"extInputLevel", 1}, "Ext Input Level", 0.0f, 2.0f,
      1.0f));

  // LFO
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"lfoEnable", 1}, "LFO Enable", false));
  // Indices must match LFOWaveform enum: Triangle=0, Sawtooth=1, Square=2,
  // S&H=3 Note: pre-v0.9.1 states had a ghost "Sine" at index 0; old index 0
  // mapped to Triangle in DSP anyway, so this removal is backward-safe.
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"lfoWave", 1}, "LFO Waveform",
      juce::StringArray{"Triangle", "Sawtooth", "Square", "S&H"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfoRate", 1}, "LFO Rate", 0.1f, 20.0f, 2.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfoDepthFilt", 1}, "LFO Filter Depth", 0.0f, 1.0f,
      0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfoDepthPW", 1}, "LFO PW Depth", 0.0f, 1.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfoDepthPitch", 1}, "LFO Pitch Depth", 0.0f, 1.0f,
      0.0f));

  // Arpeggiator
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"arpEnable", 1}, "Arpeggiator", false));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"arpPattern", 1}, "Arp Pattern",
      juce::StringArray{"Up", "Down", "UpDown", "Random"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"arpRate", 1}, "Arp Rate", 1.0f, 100.0f, 50.0f));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"arpOctaves", 1}, "Arp Octaves", 1, 4, 1));

  // Per-Voice (6 voices)
  for (int v = 0; v < 6; ++v) {
    juce::String prefix = "v" + juce::String(v) + "_";
    juce::String label = "Voice " + juce::String(v + 1) + " ";
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{prefix + "enable", 1}, label + "Enable", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{prefix + "waveform", 1}, label + "Waveform",
        juce::StringArray{"Triangle", "Sawtooth", "Pulse", "Noise"}, 2));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{prefix + "pw", 1}, label + "Pulse Width", 0, 4095,
        2048));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{prefix + "attack", 1}, label + "Attack", 0, 15, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{prefix + "decay", 1}, label + "Decay", 0, 15, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{prefix + "sustain", 1}, label + "Sustain", 0, 15,
        15));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{prefix + "release", 1}, label + "Release", 0, 15, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{prefix + "pan", 1}, label + "Pan", -1.0f, 1.0f,
        0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{prefix + "ringMod", 1}, label + "Ring Mod", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{prefix + "sync", 1}, label + "Hard Sync", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{prefix + "filter", 1}, label + "Filter Enable",
        true));
  }

  return layout;
}

void BreadbinProcessor::initializeParameterPointers() {
  masterVolPtr = apvts.getRawParameterValue("masterVol");
  dualModePtr = apvts.getRawParameterValue("dualMode");
  chipLeftPtr = apvts.getRawParameterValue("chipLeft");
  chipRightPtr = apvts.getRawParameterValue("chipRight");
  agingPtr = apvts.getRawParameterValue("aging");
  leftDetunePtr = apvts.getRawParameterValue("leftDetune");
  rightDetunePtr = apvts.getRawParameterValue("rightDetune");
  glidePtr = apvts.getRawParameterValue("glide");
  clockModePtr = apvts.getRawParameterValue("clockMode");
  extInputEnablePtr = apvts.getRawParameterValue("extInputEnable");
  extInputLevelPtr = apvts.getRawParameterValue("extInputLevel");

  lfoEnablePtr = apvts.getRawParameterValue("lfoEnable");
  lfoWavePtr = apvts.getRawParameterValue("lfoWave");
  lfoRatePtr = apvts.getRawParameterValue("lfoRate");
  lfoDepthFiltPtr = apvts.getRawParameterValue("lfoDepthFilt");
  lfoDepthPWPtr = apvts.getRawParameterValue("lfoDepthPW");
  lfoDepthPitchPtr = apvts.getRawParameterValue("lfoDepthPitch");

  arpEnablePtr = apvts.getRawParameterValue("arpEnable");
  arpPatternPtr = apvts.getRawParameterValue("arpPattern");
  arpRatePtr = apvts.getRawParameterValue("arpRate");
  arpOctavesPtr = apvts.getRawParameterValue("arpOctaves");

  for (int v = 0; v < 6; ++v) {
    juce::String prefix = "v" + juce::String(v) + "_";
    auto &ptrs = voiceParamPtrs[v];
    ptrs.enable = apvts.getRawParameterValue(prefix + "enable");
    ptrs.waveform = apvts.getRawParameterValue(prefix + "waveform");
    ptrs.pw = apvts.getRawParameterValue(prefix + "pw");
    ptrs.attack = apvts.getRawParameterValue(prefix + "attack");
    ptrs.decay = apvts.getRawParameterValue(prefix + "decay");
    ptrs.sustain = apvts.getRawParameterValue(prefix + "sustain");
    ptrs.release = apvts.getRawParameterValue(prefix + "release");
    ptrs.pan = apvts.getRawParameterValue(prefix + "pan");
    ptrs.ringMod = apvts.getRawParameterValue(prefix + "ringMod");
    ptrs.sync = apvts.getRawParameterValue(prefix + "sync");
    ptrs.filter = apvts.getRawParameterValue(prefix + "filter");
  }
}
