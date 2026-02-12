#include "PluginProcessor.h"
#include "PluginEditor.h"

BreadbinProcessor::BreadbinProcessor()
    : juce::AudioProcessor(
          juce::AudioProcessor::BusesProperties()
              .withInput("External Input", juce::AudioChannelSet::stereo(),
                         false)
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

  // Initialize FX chain
  juce::dsp::ProcessSpec fxSpec;
  fxSpec.sampleRate = sampleRate;
  fxSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
  fxSpec.numChannels = 2;

  chorus.prepare(fxSpec);
  chorus.reset();

  // DelayLine uses single-channel push/pop, prepare with 1 channel
  juce::dsp::ProcessSpec delaySpec = fxSpec;
  delaySpec.numChannels = 1;
  delayLineL.prepare(delaySpec);
  delayLineR.prepare(delaySpec);
  delayLineL.reset();
  delayLineR.reset();
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

  // Sync LFO2 from APVTS
  lfo2.enabled = lfo2EnablePtr->load() > 0.5f;
  lfo2.waveform = static_cast<LFOWaveform>(static_cast<int>(lfo2WavePtr->load()));
  lfo2.rate = lfo2RatePtr->load();
  lfo2.depthFilter = lfo2DepthFiltPtr->load();
  lfo2.depthPulseWidth = lfo2DepthPWPtr->load();
  lfo2.depthPitch = lfo2DepthPitchPtr->load();

  // Sync Wavetable from APVTS
  wavetable.enabled = wtEnablePtr->load() > 0.5f;
  wavetable.numSteps = static_cast<int>(wtNumStepsPtr->load());
  wavetable.rateHz = wtRatePtr->load();
  wavetable.loop = wtLoopPtr->load() > 0.5f;
  for (int i = 0; i < 16; ++i) {
    wavetable.steps[i].waveform = static_cast<int>(wtStepPtrs[i].wave->load());
    wavetable.steps[i].pitchOffset = static_cast<int>(wtStepPtrs[i].pitch->load());
    wavetable.steps[i].pulseWidth = static_cast<int>(wtStepPtrs[i].pw->load());
  }

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

  // Process wavetable step sequencer (after arp, before LFO)
  if (wavetable.enabled) {
    processWavetable(numSamples);
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
  if (lfo2.enabled) {
    processLFO2(numSamples);
  }
  applyLFOModulation(); // Apply LFO1+LFO2 to PW and pitch

  // Process filter envelope
  if (filterEnvEnablePtr->load() > 0.5f) {
    processFilterEnvelope(numSamples);
  } else if (filterEnv.currentValue > 0.0f) {
    // Reset envelope when disabled
    filterEnv.currentValue = 0.0f;
    filterEnv.stage = FilterEnvelopeState::Stage::Idle;
    filterEnv.gateWasOn = false;
  }

  // Unified filter modulation (stacks mod wheel + LFO + filter envelope)
  applyFilterModulation();

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

  // Read per-SID pan from APVTS (-1=left, 0=center, +1=right)
  float leftSidPan = leftPanPtr->load();
  float rightSidPan = rightPanPtr->load();

  // Standard equal-power pan law: convert [-1,+1] to angle [0, pi/2]
  constexpr float piOver2 = 1.5707963267948966f;
  float leftAngle = (leftSidPan + 1.0f) * 0.5f * piOver2;
  float leftGainL = std::cos(leftAngle);
  float leftGainR = std::sin(leftAngle);
  float rightAngle = (rightSidPan + 1.0f) * 0.5f * piOver2;
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

    // Apply per-SID pan (equal-power pan law)
    float outL = sampleL * leftGainL + sampleR * rightGainL;
    float outR = sampleL * leftGainR + sampleR * rightGainR;

    // All modes use the same stereo output path.
    // In Unison, both SIDs render the same notes independently,
    // so per-SID pan still provides stereo width.
    leftChannel[i] = outL;
    if (rightChannel)
      rightChannel[i] = outR;
  }

  // === FX CHAIN ===
  // Chorus
  if (chorusEnablePtr->load() > 0.5f) {
    chorus.setRate(chorusRatePtr->load());
    chorus.setDepth(chorusDepthPtr->load());
    chorus.setMix(chorusMixPtr->load());
    chorus.setCentreDelay(7.0f);
    chorus.setFeedback(0.0f);

    juce::dsp::AudioBlock<float> chorusBlock(buffer);
    juce::dsp::ProcessContextReplacing<float> chorusCtx(chorusBlock);
    chorus.process(chorusCtx);
  }

  // Stereo delay
  if (delayEnablePtr->load() > 0.5f) {
    float delayTimeLMs = delayTimeLPtr->load();
    float delayTimeRMs = delayTimeRPtr->load();
    float feedback = delayFeedbackPtr->load();
    float mix = delayMixPtr->load();

    float delaySamplesL =
        (delayTimeLMs / 1000.0f) * static_cast<float>(hostSampleRate);
    float delaySamplesR =
        (delayTimeRMs / 1000.0f) * static_cast<float>(hostSampleRate);

    auto *left = buffer.getWritePointer(0);
    auto *right =
        buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
      float dryL = left[i];
      float dryR = right ? right[i] : dryL;

      float wetL = delayLineL.popSample(0, delaySamplesL);
      float wetR = delayLineR.popSample(0, delaySamplesR);

      delayLineL.pushSample(0, dryL + wetL * feedback);
      delayLineR.pushSample(0, dryR + wetR * feedback);

      left[i] = dryL * (1.0f - mix) + wetL * mix;
      if (right)
        right[i] = dryR * (1.0f - mix) + wetR * mix;
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
      // Filter modulation applied per-block via applyFilterModulation()
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
  // per-voice pan removed (now per-SID: leftPan/rightPan)
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

void BreadbinProcessor::processFilterEnvelope(int numSamples) {
  // Determine gate: any voice active?
  bool gateOn = false;
  for (int v = 0; v < 6; ++v) {
    if (voices[v].active) {
      gateOn = true;
      break;
    }
  }

  // Gate transitions
  if (gateOn && !filterEnv.gateWasOn) {
    filterEnv.stage = FilterEnvelopeState::Stage::Attack;
  } else if (!gateOn && filterEnv.gateWasOn) {
    filterEnv.stage = FilterEnvelopeState::Stage::Release;
  }
  filterEnv.gateWasOn = gateOn;

  float dt = static_cast<float>(numSamples) / static_cast<float>(hostSampleRate);
  float attack = filterEnvAttackPtr->load();
  float decay = filterEnvDecayPtr->load();
  float sustain = filterEnvSustainPtr->load();
  float release = filterEnvReleasePtr->load();

  switch (filterEnv.stage) {
  case FilterEnvelopeState::Stage::Attack:
    filterEnv.currentValue += dt / attack;
    if (filterEnv.currentValue >= 1.0f) {
      filterEnv.currentValue = 1.0f;
      filterEnv.stage = FilterEnvelopeState::Stage::Decay;
    }
    break;
  case FilterEnvelopeState::Stage::Decay: {
    float decayRate = dt / decay;
    filterEnv.currentValue -= decayRate * (filterEnv.currentValue - sustain);
    if (filterEnv.currentValue <= sustain + 0.001f) {
      filterEnv.currentValue = sustain;
      filterEnv.stage = FilterEnvelopeState::Stage::Sustain;
    }
    break;
  }
  case FilterEnvelopeState::Stage::Sustain:
    filterEnv.currentValue = sustain;
    break;
  case FilterEnvelopeState::Stage::Release: {
    float releaseRate = dt / release;
    filterEnv.currentValue -= releaseRate * filterEnv.currentValue;
    if (filterEnv.currentValue <= 0.001f) {
      filterEnv.currentValue = 0.0f;
      filterEnv.stage = FilterEnvelopeState::Stage::Idle;
    }
    break;
  }
  case FilterEnvelopeState::Stage::Idle:
    filterEnv.currentValue = 0.0f;
    break;
  }
}

void BreadbinProcessor::applyFilterModulation() {
  // Unified filter cutoff modulation: base + mod wheel + LFO + filter envelope
  int modOffsetLeft = 0;
  int modOffsetRight = 0;

  // Mod wheel: adds 0-1000 to filter cutoff
  int modWheelOffset = static_cast<int>(modWheelValue * 1000.0f);
  modOffsetLeft += modWheelOffset;
  modOffsetRight += modWheelOffset;

  // LFO1 filter depth
  if (lfo.enabled && lfo.depthFilter > 0.0f) {
    int lfoOffset =
        static_cast<int>(lfo.currentValue * lfo.depthFilter * 1024.0f);
    modOffsetLeft += lfoOffset;
    modOffsetRight += lfoOffset;
  }

  // LFO2 filter depth
  if (lfo2.enabled && lfo2.depthFilter > 0.0f) {
    int lfo2Offset =
        static_cast<int>(lfo2.currentValue * lfo2.depthFilter * 1024.0f);
    modOffsetLeft += lfo2Offset;
    modOffsetRight += lfo2Offset;
  }

  // Filter envelope
  if (filterEnvEnablePtr->load() > 0.5f) {
    float envAmount = filterEnvAmountPtr->load();
    int envOffset =
        static_cast<int>(filterEnv.currentValue * envAmount * 2047.0f);
    modOffsetLeft += envOffset;
    modOffsetRight += envOffset;
  }

  // Apply combined modulation
  int leftCutoff =
      juce::jlimit(0, 2047, baseFilterCutoffLeft + modOffsetLeft);
  int rightCutoff =
      juce::jlimit(0, 2047, baseFilterCutoffRight + modOffsetRight);
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
  // per-voice pan no longer saved (now per-SID: leftPan/rightPan)
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
  // per-voice pan no longer loaded (now per-SID: leftPan/rightPan)
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

void BreadbinProcessor::processWavetable(int numSamples) {
  // Block-rate timer: advance by block duration
  double samplesPerStep = hostSampleRate / static_cast<double>(wavetable.rateHz);
  wavetable.timer += numSamples;

  if (wavetable.timer >= samplesPerStep) {
    wavetable.timer -= samplesPerStep;

    // Advance step
    int nextStep = wavetable.currentStep + 1;
    if (nextStep >= wavetable.numSteps) {
      if (wavetable.loop)
        nextStep = 0;
      else
        nextStep = wavetable.numSteps - 1; // Stay on last step
    }
    wavetable.currentStep = nextStep;
  }

  // Apply current step to all active voices
  auto &step = wavetable.steps[wavetable.currentStep];

  // Map waveform index to SIDEngine::Waveform
  SIDEngine::Waveform wf;
  switch (step.waveform) {
  case 0:
    wf = SIDEngine::Waveform::Triangle;
    break;
  case 1:
    wf = SIDEngine::Waveform::Sawtooth;
    break;
  case 2:
    wf = SIDEngine::Waveform::Pulse;
    break;
  case 3:
    wf = SIDEngine::Waveform::Noise;
    break;
  default:
    wf = SIDEngine::Waveform::Pulse;
    break;
  }

  for (int v = 0; v < 6; ++v) {
    if (!voices[v].active)
      continue;

    SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
    int sidVoice = v % 3;

    // Set waveform
    sid.setWaveform(sidVoice, wf);

    // Set pulse width
    sid.setPulseWidth(sidVoice, step.pulseWidth);

    // Apply pitch offset
    if (step.pitchOffset != 0) {
      double baseHz =
          voices[v].isGliding ? voices[v].currentHz : voices[v].targetHz;
      double modHz =
          baseHz * std::pow(2.0, step.pitchOffset / 12.0);
      sid.setFrequency(sidVoice, modHz);
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

void BreadbinProcessor::processLFO2(int numSamples) {
  double phaseInc =
      (static_cast<double>(lfo2.rate) * numSamples) / hostSampleRate;
  double oldPhase = lfo2.phase;
  lfo2.phase += phaseInc;
  lfo2.phase -= std::floor(lfo2.phase);

  float p = static_cast<float>(lfo2.phase);
  switch (lfo2.waveform) {
  case LFOWaveform::Triangle:
    lfo2.currentValue = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    break;
  case LFOWaveform::Sawtooth:
    lfo2.currentValue = 2.0f * p - 1.0f;
    break;
  case LFOWaveform::Square:
    lfo2.currentValue = (p < 0.5f) ? 1.0f : -1.0f;
    break;
  case LFOWaveform::SampleAndHold:
    if (lfo2.phase < oldPhase) {
      static std::mt19937 rng2(std::random_device{}());
      std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
      lfo2.shValue = dist(rng2);
    }
    lfo2.currentValue = lfo2.shValue;
    break;
  }
}

void BreadbinProcessor::applyLFOModulation() {
  float val1 = lfo.enabled ? lfo.currentValue : 0.0f;
  float val2 = lfo2.enabled ? lfo2.currentValue : 0.0f;

  // Filter cutoff modulation is now handled by applyFilterModulation()
  // which stacks mod wheel + LFO1 + LFO2 + filter envelope contributions.

  // Pulse width modulation (sum LFO1 + LFO2)
  float pwDepth1 = lfo.enabled ? lfo.depthPulseWidth : 0.0f;
  float pwDepth2 = lfo2.enabled ? lfo2.depthPulseWidth : 0.0f;
  if (pwDepth1 > 0.0f || pwDepth2 > 0.0f) {
    int pwMod = static_cast<int>(val1 * pwDepth1 * 2048.0f)
              + static_cast<int>(val2 * pwDepth2 * 2048.0f);
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

  // Pitch modulation (vibrato) - sum LFO1 + LFO2, ±2 semitones each
  float pitchDepth1 = lfo.enabled ? lfo.depthPitch : 0.0f;
  float pitchDepth2 = lfo2.enabled ? lfo2.depthPitch : 0.0f;
  if (pitchDepth1 > 0.0f || pitchDepth2 > 0.0f) {
    float semitoneMod = val1 * pitchDepth1 * 2.0f
                      + val2 * pitchDepth2 * 2.0f;
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
    // Per-voice pan is no longer used; per-SID pan (leftPan/rightPan APVTS)
    // controls stereo positioning. MIDI mapping kept for compatibility.
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
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"leftPan", 1}, "Left SID Pan", -1.0f, 1.0f, -1.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"rightPan", 1}, "Right SID Pan", -1.0f, 1.0f, 1.0f));

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

  // LFO2
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"lfo2Enable", 1}, "LFO2 Enable", false));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"lfo2Wave", 1}, "LFO2 Waveform",
      juce::StringArray{"Triangle", "Sawtooth", "Square", "S&H"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfo2Rate", 1}, "LFO2 Rate", 0.1f, 20.0f, 3.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfo2DepthFilt", 1}, "LFO2 Filter Depth", 0.0f, 1.0f,
      0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfo2DepthPW", 1}, "LFO2 PW Depth", 0.0f, 1.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfo2DepthPitch", 1}, "LFO2 Pitch Depth", 0.0f, 1.0f,
      0.0f));

  // Filter Envelope
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"filterEnvEnable", 1}, "Filter Env Enable", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvAttack", 1}, "Filter Env Attack", 0.001f,
      10.0f, 0.01f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvDecay", 1}, "Filter Env Decay", 0.001f,
      10.0f, 0.3f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvSustain", 1}, "Filter Env Sustain", 0.0f,
      1.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvRelease", 1}, "Filter Env Release", 0.001f,
      10.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvAmount", 1}, "Filter Env Amount", -1.0f,
      1.0f, 0.5f));

  // Wavetable Step Sequencer
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"wtEnable", 1}, "Wavetable Enable", false));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"wtNumSteps", 1}, "Wavetable Steps", 1, 16, 4));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"wtRate", 1}, "Wavetable Rate", 1.0f, 200.0f, 50.0f));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"wtLoop", 1}, "Wavetable Loop", true));
  // Per-step parameters (16 steps)
  for (int i = 0; i < 16; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{prefix + "wave", 1},
        "WT Step " + juce::String(i) + " Wave",
        juce::StringArray{"Tri", "Saw", "Pulse", "Noise"}, 2));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{prefix + "pitch", 1},
        "WT Step " + juce::String(i) + " Pitch", -24, 24, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{prefix + "pw", 1},
        "WT Step " + juce::String(i) + " PW", 0, 4095, 2048));
  }

  // FX: Chorus
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"chorusEnable", 1}, "Chorus Enable", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"chorusRate", 1}, "Chorus Rate", 0.1f, 10.0f, 1.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"chorusDepth", 1}, "Chorus Depth", 0.0f, 1.0f, 0.3f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"chorusMix", 1}, "Chorus Mix", 0.0f, 1.0f, 0.5f));

  // FX: Delay
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"delayEnable", 1}, "Delay Enable", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"delayTimeL", 1}, "Delay Time L", 1.0f, 1000.0f,
      375.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"delayTimeR", 1}, "Delay Time R", 1.0f, 1000.0f,
      500.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"delayFeedback", 1}, "Delay Feedback", 0.0f, 0.95f,
      0.3f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"delayMix", 1}, "Delay Mix", 0.0f, 1.0f, 0.3f));

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
    // per-voice pan removed (now per-SID: leftPan/rightPan)
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
  leftPanPtr = apvts.getRawParameterValue("leftPan");
  rightPanPtr = apvts.getRawParameterValue("rightPan");

  lfoEnablePtr = apvts.getRawParameterValue("lfoEnable");
  lfoWavePtr = apvts.getRawParameterValue("lfoWave");
  lfoRatePtr = apvts.getRawParameterValue("lfoRate");
  lfoDepthFiltPtr = apvts.getRawParameterValue("lfoDepthFilt");
  lfoDepthPWPtr = apvts.getRawParameterValue("lfoDepthPW");
  lfoDepthPitchPtr = apvts.getRawParameterValue("lfoDepthPitch");

  lfo2EnablePtr = apvts.getRawParameterValue("lfo2Enable");
  lfo2WavePtr = apvts.getRawParameterValue("lfo2Wave");
  lfo2RatePtr = apvts.getRawParameterValue("lfo2Rate");
  lfo2DepthFiltPtr = apvts.getRawParameterValue("lfo2DepthFilt");
  lfo2DepthPWPtr = apvts.getRawParameterValue("lfo2DepthPW");
  lfo2DepthPitchPtr = apvts.getRawParameterValue("lfo2DepthPitch");

  arpEnablePtr = apvts.getRawParameterValue("arpEnable");
  arpPatternPtr = apvts.getRawParameterValue("arpPattern");
  arpRatePtr = apvts.getRawParameterValue("arpRate");
  arpOctavesPtr = apvts.getRawParameterValue("arpOctaves");

  // Filter Envelope
  filterEnvEnablePtr = apvts.getRawParameterValue("filterEnvEnable");
  filterEnvAttackPtr = apvts.getRawParameterValue("filterEnvAttack");
  filterEnvDecayPtr = apvts.getRawParameterValue("filterEnvDecay");
  filterEnvSustainPtr = apvts.getRawParameterValue("filterEnvSustain");
  filterEnvReleasePtr = apvts.getRawParameterValue("filterEnvRelease");
  filterEnvAmountPtr = apvts.getRawParameterValue("filterEnvAmount");

  // Wavetable
  wtEnablePtr = apvts.getRawParameterValue("wtEnable");
  wtNumStepsPtr = apvts.getRawParameterValue("wtNumSteps");
  wtRatePtr = apvts.getRawParameterValue("wtRate");
  wtLoopPtr = apvts.getRawParameterValue("wtLoop");
  for (int i = 0; i < 16; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    wtStepPtrs[i].wave = apvts.getRawParameterValue(prefix + "wave");
    wtStepPtrs[i].pitch = apvts.getRawParameterValue(prefix + "pitch");
    wtStepPtrs[i].pw = apvts.getRawParameterValue(prefix + "pw");
  }

  // FX
  chorusEnablePtr = apvts.getRawParameterValue("chorusEnable");
  chorusRatePtr = apvts.getRawParameterValue("chorusRate");
  chorusDepthPtr = apvts.getRawParameterValue("chorusDepth");
  chorusMixPtr = apvts.getRawParameterValue("chorusMix");
  delayEnablePtr = apvts.getRawParameterValue("delayEnable");
  delayTimeLPtr = apvts.getRawParameterValue("delayTimeL");
  delayTimeRPtr = apvts.getRawParameterValue("delayTimeR");
  delayFeedbackPtr = apvts.getRawParameterValue("delayFeedback");
  delayMixPtr = apvts.getRawParameterValue("delayMix");

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
    // per-voice pan removed (now per-SID: leftPan/rightPan)
    ptrs.ringMod = apvts.getRawParameterValue(prefix + "ringMod");
    ptrs.sync = apvts.getRawParameterValue(prefix + "sync");
    ptrs.filter = apvts.getRawParameterValue(prefix + "filter");
  }
}
