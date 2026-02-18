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

  // Initialize SID file player
  sidFilePlayer = std::make_unique<SidFilePlayer>();

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

  // Initialize SID file player + resampler
  sidFilePlayer->prepare(sampleRate);
  sidResampleRatio = SidFilePlayer::ENGINE_SAMPLE_RATE / sampleRate;
  sidResamplerL.reset();
  sidResamplerR.reset();
  // Pre-allocate resampler input buffers (enough for one block at engine rate +
  // margin)
  size_t resampleBufSize =
      static_cast<size_t>(samplesPerBlock * sidResampleRatio) + 64;
  sidResampleBufL.resize(resampleBufSize, 0.0f);
  sidResampleBufR.resize(resampleBufSize, 0.0f);
  sidResampleBufCapacity = resampleBufSize;

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
  const auto cpuTimerStart = juce::Time::getHighResolutionTicks();

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
  pitchBendRange = static_cast<int>(pitchBendRangePtr->load());

  // Sync LFO from APVTS
  lfo.enabled = lfoEnablePtr->load() > 0.5f;
  lfo.waveform = static_cast<LFOWaveform>(static_cast<int>(lfoWavePtr->load()));
  lfo.rate = lfoRatePtr->load();
  lfo.depthFilter = lfoDepthFiltPtr->load();
  lfo.depthPulseWidth = lfoDepthPWPtr->load();
  lfo.depthPitch = lfoDepthPitchPtr->load();

  // Sync LFO2 from APVTS
  lfo2.enabled = lfo2EnablePtr->load() > 0.5f;
  lfo2.waveform =
      static_cast<LFOWaveform>(static_cast<int>(lfo2WavePtr->load()));
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
    wavetable.steps[i].pitchOffset =
        static_cast<int>(wtStepPtrs[i].pitch->load());
    wavetable.steps[i].pulseWidth = static_cast<int>(wtStepPtrs[i].pw->load());
  }

  // Sync Arp from APVTS
  arpEnabled = arpEnablePtr->load() > 0.5f;
  arpPattern = static_cast<ArpPattern>(static_cast<int>(arpPatternPtr->load()));
  arpRateHz = arpRatePtr->load();
  arpOctaves = static_cast<int>(arpOctavesPtr->load());

  // Sync Chord Memory from APVTS
  chordMemory.enabled = chordEnablePtr->load() > 0.5f;
  chordMemory.activeSlot =
      juce::jlimit(0, 3, static_cast<int>(chordSlotPtr->load()));
  for (int s = 0; s < 4; ++s)
    for (int i = 0; i < 5; ++i)
      chordMemory.intervals[s][i] =
          static_cast<int>(chordSlotPtrs[s].intervals[i]->load());

  // Chord memory and arpeggiator are mutually exclusive. If both are enabled
  // via host automation, chord mode wins and arp state is held idle.
  if (chordMemory.enabled && arpEnabled) {
    arpHeldCount = 0;
    arpSeqCount = 0;
    arpIndex = 0;
    lastArpNote = -1;
  }

  // Detect chip model / clock mode changes — set dirty flags, defer the heavy
  // reinit to a non-RT context (handled via timerCallback or prepareToPlay).
  auto newLeftModel =
      static_cast<SIDEngine::ChipModel>(static_cast<int>(chipLeftPtr->load()));
  auto newRightModel =
      static_cast<SIDEngine::ChipModel>(static_cast<int>(chipRightPtr->load()));
  if (newLeftModel != chipModelLeft || newRightModel != chipModelRight) {
    chipModelLeft = newLeftModel;
    chipModelRight = newRightModel;
    chipModelDirty.store(true, std::memory_order_relaxed);
  }
  auto newClockMode =
      static_cast<SIDEngine::ClockMode>(static_cast<int>(clockModePtr->load()));
  if (newClockMode != clockMode) {
    clockMode = newClockMode;
    clockModeDirty.store(true, std::memory_order_relaxed);
  }

  // Apply deferred heavy operations (chip model, clock mode)
  // These involve heap allocs in reSIDfp, so we apply them here at the
  // start of processBlock where latency is most tolerable, rather than
  // blocking mid-render. In practice these are rare (user changes a knob).
  if (chipModelDirty.exchange(false, std::memory_order_relaxed)) {
    sidLeft.setChipModel(chipModelLeft);
    sidRight.setChipModel(chipModelRight);
  }
  if (clockModeDirty.exchange(false, std::memory_order_relaxed)) {
    setClockMode(clockMode);
  }

  // Sync voice settings every block for now.
  // TODO: Wire APVTS change listener to set voiceSettingsDirty[v] = true on
  // parameter change, then guard this with the dirty flag for efficiency.
  for (int v = 0; v < 6; ++v) {
    applyVoiceSettings(v);
  }

  // Handle MIDI
  for (const auto metadata : midiMessages) {
    handleMidiEvent(metadata.getMessage());
  }

  // Process arpeggiator (disabled while chord memory is active)
  const int numSamples = buffer.getNumSamples();
  if (!chordMemory.enabled && arpEnabled && arpSeqCount > 0) {
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

  // PWM Sweep phase (triangle oscillator, block-rate)
  if (pwmSweepEnablePtr->load() > 0.5f) {
    float sweepRate = pwmSweepRatePtr->load();
    pwmSweepPhase +=
        (static_cast<double>(sweepRate) * numSamples) / hostSampleRate;
    pwmSweepPhase -= std::floor(pwmSweepPhase);
    float p = static_cast<float>(pwmSweepPhase);
    pwmSweepCurrentValue = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
  } else {
    pwmSweepCurrentValue = 0.0f;
  }

  applyLFOModulation(); // Apply LFO1+LFO2+PWM sweep to PW and pitch

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

  // Mod matrix (additional routing from any source to any dest)
  applyModMatrix();

  auto *leftChannel = buffer.getWritePointer(0);
  auto *rightChannel =
      buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

  // Get input channels from external input bus for external audio routing
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

  // === SID FILE PLAYER MIX (with resampling from 44100 to host rate) ===
  if (sidFilePlayer->isPlaying()) {
    // How many samples we need from the SID engine to produce numSamples at
    // host rate
    int sourceSamples = static_cast<int>(numSamples * sidResampleRatio) + 4;
    // Clamp to pre-allocated capacity to avoid heap alloc on RT thread
    if (static_cast<size_t>(sourceSamples) > sidResampleBufCapacity) {
      sourceSamples = static_cast<int>(sidResampleBufCapacity);
    }
    // Read from ring buffer at engine rate
    sidFilePlayer->readSamples(sidResampleBufL.data(), sidResampleBufR.data(),
                               sourceSamples);

    if (std::abs(sidResampleRatio - 1.0) < 0.001) {
      // No resampling needed (host rate == engine rate)
      for (int i = 0; i < numSamples; ++i) {
        leftChannel[i] += sidResampleBufL[static_cast<size_t>(i)];
        if (rightChannel)
          rightChannel[i] += sidResampleBufR[static_cast<size_t>(i)];
      }
    } else {
      // Resample from ENGINE_SAMPLE_RATE to host rate using Lagrange
      // interpolation Stack-allocate output buffers (typical block size <=
      // 4096)
      float resampledL[4096];
      float resampledR[4096];
      int outSamples = std::min(numSamples, 4096);

      sidResamplerL.process(sidResampleRatio, sidResampleBufL.data(),
                            resampledL, outSamples);
      sidResamplerR.process(sidResampleRatio, sidResampleBufR.data(),
                            resampledR, outSamples);

      for (int i = 0; i < outSamples; ++i) {
        leftChannel[i] += resampledL[i];
        if (rightChannel)
          rightChannel[i] += resampledR[i];
      }
    }
    sidPlayerActive.store(true, std::memory_order_relaxed);
  } else {
    sidPlayerActive.store(false, std::memory_order_relaxed);
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

  // Envelope-following noise gate with attack/hold/release smoothing
  const float gateThreshold =
      noiseGateThresholdPtr->load(std::memory_order_relaxed);
  if (gateThreshold > 0.0001f) {
    const float sr = static_cast<float>(getSampleRate());
    const float attackMs = gateAttackPtr->load(std::memory_order_relaxed);
    const float releaseMs = gateReleasePtr->load(std::memory_order_relaxed);
    const float holdMs = gateHoldPtr->load(std::memory_order_relaxed);

    // Time constants: coeff = 1 - exp(-1 / (time_seconds * sampleRate))
    const float envAttackCoeff =
        1.0f - std::exp(-1.0f / (0.001f * sr)); // Fast envelope attack (~1ms)
    const float envReleaseCoeff =
        1.0f - std::exp(-1.0f / (0.05f * sr)); // Envelope release (~50ms)
    const float gainAttackCoeff =
        1.0f - std::exp(-1.0f / (attackMs * 0.001f * sr));
    const float gainReleaseCoeff =
        1.0f - std::exp(-1.0f / (releaseMs * 0.001f * sr));
    const int holdSamples = static_cast<int>(holdMs * 0.001f * sr);
    const float closeThreshold = gateThreshold * 0.5f; // 6dB hysteresis

    for (int ch = 0; ch < buffer.getNumChannels() && ch < 2; ++ch) {
      auto *channelData = buffer.getWritePointer(ch);
      auto &gs = gateState[static_cast<size_t>(ch)];

      for (int i = 0; i < numSamples; ++i) {
        const float inputLevel = std::abs(channelData[i]);

        // Peak envelope follower
        if (inputLevel > gs.envelope)
          gs.envelope += envAttackCoeff * (inputLevel - gs.envelope);
        else
          gs.envelope += envReleaseCoeff * (inputLevel - gs.envelope);

        // Gate state with hysteresis
        bool gateOpen;
        if (gs.envelope >= gateThreshold) {
          gateOpen = true;
          gs.holdCounter = holdSamples;
        } else if (gs.holdCounter > 0) {
          gs.holdCounter--;
          gateOpen = true;
        } else {
          gateOpen = gs.envelope >= closeThreshold;
        }

        // Smooth gain transition
        const float targetGain = gateOpen ? 1.0f : 0.0f;
        if (targetGain > gs.gain)
          gs.gain += gainAttackCoeff * (targetGain - gs.gain);
        else
          gs.gain += gainReleaseCoeff * (targetGain - gs.gain);

        channelData[i] *= gs.gain;
      }
    }
  }

  // Measure CPU load: time spent / time available
  const auto cpuTimerEnd = juce::Time::getHighResolutionTicks();
  const double elapsed =
      juce::Time::highResolutionTicksToSeconds(cpuTimerEnd - cpuTimerStart);
  const double budget = static_cast<double>(numSamples) / getSampleRate();
  cpuLoadPercent.store(static_cast<float>(elapsed / budget * 100.0),
                       std::memory_order_relaxed);
}

void BreadbinProcessor::handleMidiEvent(const juce::MidiMessage &msg) {
  if (msg.isNoteOn()) {
    const int note = msg.getNoteNumber();
    lastVelocity = msg.getVelocity();
    const int channel = msg.getChannel();

    // Chord learn mode: capture notes without triggering
    if (chordLearnActive) {
      // Try to lock — if GUI holds it, skip this note (acceptable for learn)
      std::unique_lock<std::mutex> lock(chordLearnMutex, std::try_to_lock);
      if (lock.owns_lock() &&
          chordLearnCount < static_cast<int>(chordLearnNotes.size())) {
        bool found = false;
        for (int ci = 0; ci < chordLearnCount; ++ci) {
          if (chordLearnNotes[ci] == note) {
            found = true;
            break;
          }
        }
        if (!found)
          chordLearnNotes[chordLearnCount++] = note;
      }
    }

    // Chord memory takes priority and does not feed arp tracking.
    if (chordMemory.enabled) {
      if (dualMode == DualMode::Multitimbral) {
        if (channel == 2)
          triggerChord(false, note, lastVelocity);
        else
          triggerChord(true, note, lastVelocity);
      } else {
        triggerChordDualSID(note, lastVelocity);
      }
      return;
    }

    // Track for arpeggiator
    {
      bool alreadyHeld = false;
      for (int ai = 0; ai < arpHeldCount; ++ai) {
        if (arpHeldNotes[ai] == note) {
          alreadyHeld = true;
          break;
        }
      }
      if (!alreadyHeld &&
          arpHeldCount < static_cast<int>(arpHeldNotes.size())) {
        arpHeldNotes[arpHeldCount++] = note;
        rebuildArpSequence();
      }
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

    // Chord memory release (stays independent of arp state)
    if (chordMemory.enabled) {
      if (!sustainActive) {
        if (dualMode == DualMode::Multitimbral) {
          if (msg.getChannel() == 2)
            releaseChord(false);
          else
            releaseChord(true);
        } else {
          releaseChord(true);
          releaseChord(false);
        }
      }
      return;
    }

    // Remove from arp tracking
    for (int ai = 0; ai < arpHeldCount; ++ai) {
      if (arpHeldNotes[ai] == note) {
        arpHeldNotes[ai] = arpHeldNotes[--arpHeldCount];
        rebuildArpSequence();
        break;
      }
    }

    // If arp is enabled, handle release when no notes held
    if (arpEnabled) {
      if (arpHeldCount == 0 && lastArpNote >= 0 && !sustainActive) {
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
    arpHeldCount = 0;
    arpSeqCount = 0;
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
              bool found = false;
              for (int ai = 0; ai < arpHeldCount; ++ai) {
                if (arpHeldNotes[ai] == n) {
                  found = true;
                  break;
                }
              }
              if (!found)
                leftNoteQueue.remove(i);
            }
            updateSIDFromQueue(true);

            // Check right SID queue
            for (int i = rightNoteQueue.size(); --i >= 0;) {
              int n = rightNoteQueue[i];
              bool found = false;
              for (int ai = 0; ai < arpHeldCount; ++ai) {
                if (arpHeldNotes[ai] == n) {
                  found = true;
                  break;
                }
              }
              if (!found)
                rightNoteQueue.remove(i);
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

    // Apply frequency offset for sync/ring mod on THIS voice only.
    //
    // How sync works on the real C64: composers would dedicate one voice as
    // the modulator (base freq, no sync bit) and another as the carrier
    // (higher freq, sync bit ON). The carrier's oscillator resets when the
    // modulator completes a cycle, creating distinctive harmonics.
    //
    // SID sync pairing: v0 syncs to v2, v1 syncs to v0, v2 syncs to v1.
    // When all voices have sync=1 (for consistent timbre), we need exactly
    // ONE voice to run at the offset frequency while its modulator stays
    // at the base note. We pick SID voice 2 as the carrier (syncs to v1).
    // Voices 0 and 1 stay at base frequency.
    bool hasSync = voiceParamPtrs[voiceIndex].sync->load() > 0.5f;
    bool hasRing = voiceParamPtrs[voiceIndex].ringMod->load() > 0.5f;
    float offsetSemitones = voiceParamPtrs[voiceIndex].modOffset->load();

    if ((hasSync || hasRing) && std::abs(offsetSemitones) > 0.01f) {
      // Only SID voice 2 gets the offset (carrier). Voices 0,1 stay at
      // base frequency to serve as modulators.
      if (sidVoice == 2) {
        double offsetNote = static_cast<double>(midiNote) + (detune / 100.0) +
                            static_cast<double>(offsetSemitones);
        double offsetHz = 440.0 * std::pow(2.0, (offsetNote - 69.0) / 12.0);

        sid.setFrequency(sidVoice, offsetHz);
        voices[voiceIndex].currentHz = offsetHz;
      } else {
        // Modulator stays at base frequency (no action needed)
      }
    }
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

  float dt =
      static_cast<float>(numSamples) / static_cast<float>(hostSampleRate);
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
  // Unified filter cutoff modulation: base + mod wheel + LFO + filter
  // envelope
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
  int leftCutoff = juce::jlimit(0, 2047, baseFilterCutoffLeft + modOffsetLeft);
  int rightCutoff =
      juce::jlimit(0, 2047, baseFilterCutoffRight + modOffsetRight);
  lastAppliedCutoffLeft = leftCutoff;
  lastAppliedCutoffRight = rightCutoff;
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

  // Persist non-APVTS filter state
  apvts.state.setProperty("filterCutoffL", baseFilterCutoffLeft, nullptr);
  apvts.state.setProperty("filterResL", baseFilterResLeft, nullptr);
  apvts.state.setProperty("filterCutoffR", baseFilterCutoffRight, nullptr);
  apvts.state.setProperty("filterResR", baseFilterResRight, nullptr);

  // Persist global preset selection
  apvts.state.setProperty("globalPresetId", globalPresetId, nullptr);

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

      // Restore non-APVTS filter state
      baseFilterCutoffLeft = apvts.state.getProperty("filterCutoffL", 1024);
      baseFilterResLeft = apvts.state.getProperty("filterResL", 0);
      baseFilterCutoffRight = apvts.state.getProperty("filterCutoffR", 1024);
      baseFilterResRight = apvts.state.getProperty("filterResR", 0);

      // Restore global preset selection
      globalPresetId = apvts.state.getProperty("globalPresetId", 1);

      stateRestored = true;
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
  arpSeqCount = 0;
  if (arpHeldCount == 0)
    return;

  // Sort held notes into a stack-local array (no heap)
  std::array<int, 128> sorted{};
  for (int i = 0; i < arpHeldCount; ++i)
    sorted[i] = arpHeldNotes[i];
  std::sort(sorted.begin(), sorted.begin() + arpHeldCount);

  // Build base sequence with octave expansion
  int baseCount = 0;
  std::array<int, 512> base{};
  for (int oct = 0; oct < arpOctaves; ++oct) {
    for (int i = 0; i < arpHeldCount; ++i) {
      int transposed = sorted[i] + (oct * 12);
      if (transposed <= 127 && baseCount < static_cast<int>(base.size())) {
        base[baseCount++] = transposed;
      }
    }
  }

  // Apply pattern
  switch (arpPattern) {
  case ArpPattern::Up:
    for (int i = 0; i < baseCount; ++i)
      arpSequence[i] = base[i];
    arpSeqCount = baseCount;
    break;

  case ArpPattern::Down:
    for (int i = 0; i < baseCount; ++i)
      arpSequence[i] = base[baseCount - 1 - i];
    arpSeqCount = baseCount;
    break;

  case ArpPattern::UpDown:
    for (int i = 0; i < baseCount; ++i)
      arpSequence[i] = base[i];
    arpSeqCount = baseCount;
    if (baseCount > 1) {
      for (int i = baseCount - 2; i > 0; --i) {
        if (arpSeqCount < static_cast<int>(arpSequence.size()))
          arpSequence[arpSeqCount++] = base[i];
      }
    }
    break;

  case ArpPattern::Random:
    for (int i = 0; i < baseCount; ++i)
      arpSequence[i] = base[i];
    arpSeqCount = baseCount;
    {
      static std::mt19937 rng(std::random_device{}());
      std::shuffle(arpSequence.begin(), arpSequence.begin() + arpSeqCount, rng);
    }
    break;
  }

  // Reset index if out of bounds
  if (arpIndex >= arpSeqCount) {
    arpIndex = 0;
  }
}

void BreadbinProcessor::processArpeggiator(int numSamples) {
  if (arpSeqCount == 0)
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
    arpIndex = (arpIndex + 1) % arpSeqCount;

    // Reshuffle on wrap for random mode
    if (arpPattern == ArpPattern::Random && arpIndex == 0) {
      static std::mt19937 rng(std::random_device{}());
      std::shuffle(arpSequence.begin(), arpSequence.begin() + arpSeqCount, rng);
    }
  }
}

void BreadbinProcessor::triggerChord(bool isLeftSID, int rootNote,
                                     int velocity) {
  const int base = isLeftSID ? 0 : 3;
  const int slot = chordMemory.activeSlot;

  // Per-SID allocation: root + up to 2 non-zero intervals (3 voices
  // available).
  int notes[3] = {rootNote, -1, -1};
  int count = 1;
  for (int i = 0; i < 5 && count < 3; ++i) {
    int interval = chordMemory.intervals[slot][i];
    if (interval != 0)
      notes[count++] = juce::jlimit(0, 127, rootNote + interval);
  }

  // Trigger voices with chord notes
  for (int v = 0; v < 3; ++v) {
    if (v < count && voiceSettings[base + v].enabled)
      triggerNote(base + v, notes[v], velocity);
    else if (voices[base + v].active)
      releaseNote(base + v);
  }
}

void BreadbinProcessor::triggerChordDualSID(int rootNote, int velocity) {
  const int slot = chordMemory.activeSlot;

  // Dual-SID allocation: root + up to 5 intervals -> up to 6 voices.
  int notes[6] = {rootNote, -1, -1, -1, -1, -1};
  int count = 1;
  for (int i = 0; i < 5 && count < 6; ++i) {
    int interval = chordMemory.intervals[slot][i];
    if (interval != 0)
      notes[count++] = juce::jlimit(0, 127, rootNote + interval);
  }

  for (int v = 0; v < 6; ++v) {
    if (v < count && voiceSettings[v].enabled)
      triggerNote(v, notes[v], velocity);
    else if (voices[v].active)
      releaseNote(v);
  }
}

void BreadbinProcessor::releaseChord(bool isLeftSID) {
  const int base = isLeftSID ? 0 : 3;
  for (int v = base; v < base + 3; ++v)
    if (voices[v].active)
      releaseNote(v);
}

void BreadbinProcessor::startChordLearn(int slot) {
  std::lock_guard<std::mutex> lock(chordLearnMutex);
  chordLearnSlot = juce::jlimit(0, 3, slot);
  chordLearnCount = 0;
  chordLearnActive = true;
}

void BreadbinProcessor::stopChordLearn() {
  std::lock_guard<std::mutex> lock(chordLearnMutex);
  chordLearnActive = false;
  chordLearnCount = 0;
}

std::vector<int> BreadbinProcessor::getChordLearnNotes() {
  std::lock_guard<std::mutex> lock(chordLearnMutex);
  return std::vector<int>(chordLearnNotes.begin(),
                          chordLearnNotes.begin() + chordLearnCount);
}

void BreadbinProcessor::applyModMatrix() {
  // Accumulate per-destination modulation
  float filterMod = 0.0f;
  float pwMod = 0.0f;
  float pitchMod = 0.0f;
  float resMod = 0.0f;

  for (int i = 0; i < kModSlots; ++i) {
    const bool rowEnabled = (modSlotPtrs[i].enable != nullptr)
                                ? (modSlotPtrs[i].enable->load() > 0.5f)
                                : true;
    auto src =
        static_cast<ModSource>(static_cast<int>(modSlotPtrs[i].src->load()));
    auto dst =
        static_cast<ModDest>(static_cast<int>(modSlotPtrs[i].dst->load()));
    float amt = modSlotPtrs[i].amt->load();

    if (!rowEnabled || src == ModSource::None || dst == ModDest::None ||
        amt == 0.0f) {
      modSlotDisplay[i].sourceValue.store(0.0f);
      modSlotDisplay[i].contribution.store(0.0f);
      continue;
    }

    // Get source value (-1.0 to 1.0)
    float sourceVal = 0.0f;
    switch (src) {
    case ModSource::LFO1:
      sourceVal = lfo.enabled ? lfo.currentValue : 0.0f;
      break;
    case ModSource::LFO2:
      sourceVal = lfo2.enabled ? lfo2.currentValue : 0.0f;
      break;
    case ModSource::FilterEnv:
      sourceVal = filterEnv.currentValue; // 0.0 to 1.0
      break;
    case ModSource::ModWheel:
      sourceVal = modWheelValue; // 0.0 to 1.0
      break;
    case ModSource::Velocity:
      sourceVal = lastVelocity / 127.0f; // 0.0 to 1.0
      break;
    default:
      break;
    }

    float contribution = sourceVal * amt;

    // Store per-slot display values for UI
    modSlotDisplay[i].sourceValue.store(sourceVal);
    modSlotDisplay[i].contribution.store(contribution);

    switch (dst) {
    case ModDest::FilterCutoff:
      filterMod += contribution;
      break;
    case ModDest::PulseWidth:
      pwMod += contribution;
      break;
    case ModDest::Pitch:
      pitchMod += contribution;
      break;
    case ModDest::Resonance:
      resMod += contribution;
      break;
    default:
      break;
    }
  }
  modTotals.filterCutoff.store(filterMod);
  modTotals.pulseWidth.store(pwMod);
  modTotals.pitch.store(pitchMod);
  modTotals.resonance.store(resMod);

  // Apply filter cutoff mod (additive, on top of applyFilterModulation
  // result)
  if (filterMod != 0.0f) {
    int offset = static_cast<int>(filterMod * 1024.0f);
    int leftCutoff = juce::jlimit(0, 2047, lastAppliedCutoffLeft + offset);
    int rightCutoff = juce::jlimit(0, 2047, lastAppliedCutoffRight + offset);
    sidLeft.setFilterCutoff(leftCutoff);
    sidRight.setFilterCutoff(rightCutoff);
  }

  // Apply pulse width mod
  if (pwMod != 0.0f) {
    int pwOffset = static_cast<int>(pwMod * 2048.0f);
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active) {
        SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
        int basePW = voiceSettings[v].pulseWidth;
        int modPW = std::clamp(basePW + pwOffset, 0, 4095);
        sid.setPulseWidth(v % 3, modPW);
      }
    }
  }

  // Apply pitch mod (semitones)
  if (pitchMod != 0.0f) {
    float semitoneMod = pitchMod * 2.0f; // ±2 semitones at full depth
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active) {
        double baseHz =
            voices[v].isGliding ? voices[v].currentHz : voices[v].targetHz;
        double modHz = baseHz * std::pow(2.0, semitoneMod / 12.0);
        SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
        sid.setFrequency(v % 3, modHz);
      }
    }
  }

  // Apply resonance: always write each block to avoid stale values
  {
    int resOffset = static_cast<int>(resMod * 15.0f);
    int leftRes = juce::jlimit(0, 15, baseFilterResLeft + resOffset);
    int rightRes = juce::jlimit(0, 15, baseFilterResRight + resOffset);
    sidLeft.setFilterResonance(leftRes);
    sidRight.setFilterResonance(rightRes);
    lastAppliedResLeft.store(leftRes);
    lastAppliedResRight.store(rightRes);
  }
}

void BreadbinProcessor::processWavetable(int numSamples) {
  // Block-rate timer: advance by block duration
  double samplesPerStep =
      hostSampleRate / static_cast<double>(wavetable.rateHz);
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

  // Apply current step's waveform to all active voices.
  // PW and pitch are applied later in applyLFOModulation() which
  // uses wavetable step values as the base when WT is active.
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
    sid.setWaveform(v % 3, wf);
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
  bool anyVoiceActive = false;
  for (int v = 0; v < 6; ++v) {
    if (voices[v].active) {
      anyVoiceActive = true;
      break;
    }
  }

  // Filter cutoff modulation is now handled by applyFilterModulation()
  // which stacks mod wheel + LFO1 + LFO2 + filter envelope contributions.

  // When wavetable is active, use its step values as base instead of voice
  // settings
  bool wtActive = wavetable.enabled;
  auto &wtStep = wavetable.steps[wavetable.currentStep];

  // Pulse width modulation (sum LFO1 + LFO2 + PWM sweep, stacked on
  // wavetable PW if active)
  float pwDepth1 = lfo.enabled ? lfo.depthPulseWidth : 0.0f;
  float pwDepth2 = lfo2.enabled ? lfo2.depthPulseWidth : 0.0f;
  float sweepDepth =
      (pwmSweepEnablePtr->load() > 0.5f) ? pwmSweepDepthPtr->load() : 0.0f;
  int pwMod = 0;
  if (pwDepth1 > 0.0f || pwDepth2 > 0.0f)
    pwMod = static_cast<int>(val1 * pwDepth1 * 2048.0f) +
            static_cast<int>(val2 * pwDepth2 * 2048.0f);
  if (sweepDepth > 0.0f)
    pwMod += static_cast<int>(pwmSweepCurrentValue * sweepDepth * 2048.0f);
  if (anyVoiceActive) {
    for (int v = 0; v < 6; ++v) {
      if (!voices[v].active)
        continue;
      int basePW = wtActive ? wtStep.pulseWidth : voiceSettings[v].pulseWidth;
      int modPW = std::clamp(basePW + pwMod, 0, 4095);
      SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
      sid.setPulseWidth(v % 3, modPW);
    }
  }
  // Store representative PW for UI meter (voice 0), but avoid idle
  // modulation movement when no voice is sounding.
  int uiPWBase = wtActive ? wtStep.pulseWidth : voiceSettings[0].pulseWidth;
  lastAppliedPW.store(anyVoiceActive ? std::clamp(uiPWBase + pwMod, 0, 4095)
                                     : uiPWBase);

  // Pitch modulation (vibrato) - sum LFO1 + LFO2, stacked on wavetable
  // pitch if active
  float pitchDepth1 = lfo.enabled ? lfo.depthPitch : 0.0f;
  float pitchDepth2 = lfo2.enabled ? lfo2.depthPitch : 0.0f;
  float semitoneMod = 0.0f;
  if (pitchDepth1 > 0.0f || pitchDepth2 > 0.0f)
    semitoneMod = val1 * pitchDepth1 * 2.0f + val2 * pitchDepth2 * 2.0f;
  // Add wavetable pitch offset
  if (wtActive)
    semitoneMod += static_cast<float>(wtStep.pitchOffset);
  if (anyVoiceActive) {
    for (int v = 0; v < 6; ++v) {
      if (!voices[v].active)
        continue;
      // Use currentHz which includes sync/ring-mod offset from triggerNote.
      // targetHz is always the base note without offset.
      double baseHz = voices[v].currentHz;
      double modHz = baseHz * std::pow(2.0, semitoneMod / 12.0);
      SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
      sid.setFrequency(v % 3, modHz);
    }
  }
  // Store pitch offset for UI meter
  lastAppliedPitchOffsetSemitones.store(anyVoiceActive ? semitoneMod : 0.0f);
}

// Preset dirty-state detection
void BreadbinProcessor::snapshotPresetState() {
  presetParamSnapshot.clear();
  for (auto *param : getParameters()) {
    auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(param);
    if (ranged)
      presetParamSnapshot[ranged->paramID] = ranged->getValue();
  }
  presetBaseFilterCutoffL = baseFilterCutoffLeft;
  presetBaseFilterCutoffR = baseFilterCutoffRight;
  presetBaseFilterResL = baseFilterResLeft;
  presetBaseFilterResR = baseFilterResRight;
}

bool BreadbinProcessor::isPresetDirty() const {
  if (presetParamSnapshot.empty())
    return false;
  for (auto *param : getParameters()) {
    auto *ranged = dynamic_cast<const juce::RangedAudioParameter *>(param);
    if (!ranged)
      continue;
    auto it = presetParamSnapshot.find(ranged->paramID);
    if (it != presetParamSnapshot.end())
      if (std::abs(ranged->getValue() - it->second) > 1e-6f)
        return true;
  }
  if (baseFilterCutoffLeft != presetBaseFilterCutoffL)
    return true;
  if (baseFilterCutoffRight != presetBaseFilterCutoffR)
    return true;
  if (baseFilterResLeft != presetBaseFilterResL)
    return true;
  if (baseFilterResRight != presetBaseFilterResR)
    return true;
  return false;
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
    baseFilterCutoffLeft = static_cast<int>(normalized * 2047.0f);
    sidLeft.setFilterCutoff(baseFilterCutoffLeft);
    break;
  case ControlParam::LeftResonance:
    baseFilterResLeft = static_cast<int>(normalized * 15.0f);
    sidLeft.setFilterResonance(baseFilterResLeft);
    break;
  case ControlParam::RightCutoff:
    baseFilterCutoffRight = static_cast<int>(normalized * 2047.0f);
    sidRight.setFilterCutoff(baseFilterCutoffRight);
    break;
  case ControlParam::RightResonance:
    baseFilterResRight = static_cast<int>(normalized * 15.0f);
    sidRight.setFilterResonance(baseFilterResRight);
    break;
  case ControlParam::GlobalGlide:
    setGlideTimeMs(normalized * 2000.0f);
    break;
  case ControlParam::PitchBendRange: {
    auto *p = apvts.getParameter("pitchBendRange");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(2.0f + normalized * 10.0f));
    break;
  }
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

  // --- New APVTS-backed params ---
  case ControlParam::LeftPan: {
    auto *p = apvts.getParameter("leftPan");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(-1.0f + normalized * 2.0f));
    break;
  }
  case ControlParam::RightPan: {
    auto *p = apvts.getParameter("rightPan");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(-1.0f + normalized * 2.0f));
    break;
  }
  case ControlParam::FilterEnvAttack: {
    auto *p = apvts.getParameter("filterEnvAttack");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::FilterEnvDecay: {
    auto *p = apvts.getParameter("filterEnvDecay");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::FilterEnvSustain: {
    auto *p = apvts.getParameter("filterEnvSustain");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::FilterEnvRelease: {
    auto *p = apvts.getParameter("filterEnvRelease");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::FilterEnvAmount: {
    auto *p = apvts.getParameter("filterEnvAmount");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(-1.0f + normalized * 2.0f));
    break;
  }
  case ControlParam::ChorusRate: {
    auto *p = apvts.getParameter("chorusRate");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::ChorusDepth: {
    auto *p = apvts.getParameter("chorusDepth");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::ChorusMix: {
    auto *p = apvts.getParameter("chorusMix");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::DelayTimeL: {
    auto *p = apvts.getParameter("delayTimeL");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::DelayTimeR: {
    auto *p = apvts.getParameter("delayTimeR");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::DelayFeedback: {
    auto *p = apvts.getParameter("delayFeedback");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::DelayMix: {
    auto *p = apvts.getParameter("delayMix");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::PwmSweepRate: {
    auto *p = apvts.getParameter("pwmSweepRate");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::PwmSweepDepth: {
    auto *p = apvts.getParameter("pwmSweepDepth");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  // Toggle params: >= 64 = on, < 64 = off
  case ControlParam::ArpEnable: {
    auto *p = apvts.getParameter("arpEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::ArpOctaves: {
    auto *p = apvts.getParameter("arpOctaves");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(1.0f + normalized * 3.0f));
    break;
  }
  case ControlParam::ArpPattern: {
    auto *p = apvts.getParameter("arpPattern");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(normalized * 3.0f));
    break;
  }
  case ControlParam::ChorusEnable: {
    auto *p = apvts.getParameter("chorusEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::DelayEnable: {
    auto *p = apvts.getParameter("delayEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::FilterEnvEnable: {
    auto *p = apvts.getParameter("filterEnvEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::ExtInputEnable: {
    auto *p = apvts.getParameter("extInputEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::ClockMode: {
    auto *p = apvts.getParameter("clockMode");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::DualMode: {
    auto *p = apvts.getParameter("dualMode");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(normalized * 2.0f));
    break;
  }
  case ControlParam::PwmSweepEnable: {
    auto *p = apvts.getParameter("pwmSweepEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::WtEnable: {
    auto *p = apvts.getParameter("wtEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::LfoEnable: {
    auto *p = apvts.getParameter("lfoEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::Lfo2Enable: {
    auto *p = apvts.getParameter("lfo2Enable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::ChordEnable: {
    auto *p = apvts.getParameter("chordEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::LFO2Rate: {
    auto *p = apvts.getParameter("lfo2Rate");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::LFO2DepthFilter: {
    auto *p = apvts.getParameter("lfo2DepthFilt");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::LFO2DepthPW: {
    auto *p = apvts.getParameter("lfo2DepthPW");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::LFO2DepthPitch: {
    auto *p = apvts.getParameter("lfo2DepthPitch");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::LfoWave: {
    auto *p = apvts.getParameter("lfoWave");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(normalized * 3.0f));
    break;
  }
  case ControlParam::Lfo2Wave: {
    auto *p = apvts.getParameter("lfo2Wave");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(normalized * 3.0f));
    break;
  }
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
  case ControlParam::LeftPan:
    return "Left SID Pan";
  case ControlParam::RightPan:
    return "Right SID Pan";
  case ControlParam::FilterEnvAttack:
    return "Filter Env Attack";
  case ControlParam::FilterEnvDecay:
    return "Filter Env Decay";
  case ControlParam::FilterEnvSustain:
    return "Filter Env Sustain";
  case ControlParam::FilterEnvRelease:
    return "Filter Env Release";
  case ControlParam::FilterEnvAmount:
    return "Filter Env Amount";
  case ControlParam::ChorusRate:
    return "Chorus Rate";
  case ControlParam::ChorusDepth:
    return "Chorus Depth";
  case ControlParam::ChorusMix:
    return "Chorus Mix";
  case ControlParam::DelayTimeL:
    return "Delay Time L";
  case ControlParam::DelayTimeR:
    return "Delay Time R";
  case ControlParam::DelayFeedback:
    return "Delay Feedback";
  case ControlParam::DelayMix:
    return "Delay Mix";
  case ControlParam::PwmSweepRate:
    return "PWM Sweep Rate";
  case ControlParam::PwmSweepDepth:
    return "PWM Sweep Depth";
  case ControlParam::ArpEnable:
    return "Arp Enable";
  case ControlParam::ArpOctaves:
    return "Arp Octaves";
  case ControlParam::ArpPattern:
    return "Arp Pattern";
  case ControlParam::ChorusEnable:
    return "Chorus Enable";
  case ControlParam::DelayEnable:
    return "Delay Enable";
  case ControlParam::FilterEnvEnable:
    return "Filter Env Enable";
  case ControlParam::ExtInputEnable:
    return "Ext Input Enable";
  case ControlParam::ClockMode:
    return "Clock Mode";
  case ControlParam::DualMode:
    return "Dual Mode";
  case ControlParam::PwmSweepEnable:
    return "PWM Sweep Enable";
  case ControlParam::WtEnable:
    return "Wavetable Enable";
  case ControlParam::LfoEnable:
    return "LFO Enable";
  case ControlParam::Lfo2Enable:
    return "LFO2 Enable";
  case ControlParam::ChordEnable:
    return "Chord Enable";
  case ControlParam::LFO2Rate:
    return "LFO2 Rate";
  case ControlParam::LFO2DepthFilter:
    return "LFO2 Filter Depth";
  case ControlParam::LFO2DepthPW:
    return "LFO2 PWM Depth";
  case ControlParam::LFO2DepthPitch:
    return "LFO2 Pitch Depth";
  case ControlParam::LfoWave:
    return "LFO Waveform";
  case ControlParam::Lfo2Wave:
    return "LFO2 Waveform";
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
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"noiseGateThreshold", 1}, "Noise Gate Threshold", 0.0f,
      0.1f, 0.01f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"gateAttack", 1}, "Gate Attack",
      juce::NormalisableRange<float>(0.1f, 50.0f, 0.1f), 1.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"gateRelease", 1}, "Gate Release",
      juce::NormalisableRange<float>(1.0f, 500.0f, 1.0f), 50.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"gateHold", 1}, "Gate Hold",
      juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 10.0f));
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

  // Pitch Bend Range
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"pitchBendRange", 1}, "Pitch Bend Range", 2, 12, 2));

  // LFO
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"lfoEnable", 1}, "LFO Enable", false));
  // Indices must match LFOWaveform enum: Triangle=0, Sawtooth=1, Square=2,
  // S&H=3 Note: pre-v0.9.1 states had a ghost "Sine" at index 0; old index
  // 0 mapped to Triangle in DSP anyway, so this removal is backward-safe.
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

  // PWM Sweep
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"pwmSweepEnable", 1}, "PWM Sweep Enable", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"pwmSweepRate", 1}, "PWM Sweep Rate", 0.05f, 10.0f,
      0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"pwmSweepDepth", 1}, "PWM Sweep Depth", 0.0f, 1.0f,
      0.0f));

  // Chord Memory
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"chordEnable", 1}, "Chord Memory Enable", false));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"chordSlot", 1}, "Chord Active Slot", 0, 3, 0));
  for (int s = 0; s < 4; ++s) {
    for (int i = 0; i < 5; ++i) {
      auto id = "chord_s" + juce::String(s) + "_i" + juce::String(i);
      layout.add(std::make_unique<juce::AudioParameterInt>(
          juce::ParameterID{id, 1},
          "Chord " + juce::String(s) + " Int " + juce::String(i), -24, 24, 0));
    }
  }

  // Filter Envelope
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"filterEnvEnable", 1}, "Filter Env Enable", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvAttack", 1}, "Filter Env Attack", 0.001f,
      10.0f, 0.01f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvDecay", 1}, "Filter Env Decay", 0.001f, 10.0f,
      0.3f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvSustain", 1}, "Filter Env Sustain", 0.0f,
      1.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvRelease", 1}, "Filter Env Release", 0.001f,
      10.0f, 0.5f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterEnvAmount", 1}, "Filter Env Amount", -1.0f, 1.0f,
      0.5f));

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

  // Mod Matrix (4 slots)
  for (int i = 0; i < kModSlots; ++i) {
    auto prefix = "mod" + juce::String(i) + "_";
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{prefix + "enable", 1},
        "Mod " + juce::String(i) + " Enable", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{prefix + "src", 1},
        "Mod " + juce::String(i) + " Source",
        juce::StringArray{"None", "LFO1", "LFO2", "FiltEnv", "ModWheel",
                          "Velocity"},
        0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{prefix + "dst", 1},
        "Mod " + juce::String(i) + " Dest",
        juce::StringArray{"None", "Filter", "PW", "Pitch", "Resonance"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{prefix + "amt", 1},
        "Mod " + juce::String(i) + " Amount", -1.0f, 1.0f, 0.0f));
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
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{prefix + "modOffset", 1}, label + "Mod Offset",
        -24.0f, 24.0f, 7.0f));
  }

  return layout;
}

void BreadbinProcessor::initializeParameterPointers() {
  masterVolPtr = apvts.getRawParameterValue("masterVol");
  noiseGateThresholdPtr = apvts.getRawParameterValue("noiseGateThreshold");
  gateAttackPtr = apvts.getRawParameterValue("gateAttack");
  gateReleasePtr = apvts.getRawParameterValue("gateRelease");
  gateHoldPtr = apvts.getRawParameterValue("gateHold");
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
  pitchBendRangePtr = apvts.getRawParameterValue("pitchBendRange");

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

  // PWM Sweep
  pwmSweepEnablePtr = apvts.getRawParameterValue("pwmSweepEnable");
  pwmSweepRatePtr = apvts.getRawParameterValue("pwmSweepRate");
  pwmSweepDepthPtr = apvts.getRawParameterValue("pwmSweepDepth");

  // Chord Memory
  chordEnablePtr = apvts.getRawParameterValue("chordEnable");
  chordSlotPtr = apvts.getRawParameterValue("chordSlot");
  for (int s = 0; s < 4; ++s) {
    for (int i = 0; i < 5; ++i) {
      auto id = "chord_s" + juce::String(s) + "_i" + juce::String(i);
      chordSlotPtrs[s].intervals[i] = apvts.getRawParameterValue(id);
    }
  }

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

  // Mod Matrix
  for (int i = 0; i < kModSlots; ++i) {
    auto prefix = "mod" + juce::String(i) + "_";
    modSlotPtrs[i].enable = apvts.getRawParameterValue(prefix + "enable");
    modSlotPtrs[i].src = apvts.getRawParameterValue(prefix + "src");
    modSlotPtrs[i].dst = apvts.getRawParameterValue(prefix + "dst");
    modSlotPtrs[i].amt = apvts.getRawParameterValue(prefix + "amt");
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
    ptrs.modOffset = apvts.getRawParameterValue(prefix + "modOffset");
    ptrs.filter = apvts.getRawParameterValue(prefix + "filter");
  }
}

void BreadbinProcessor::snapshotSidPlayerToAPVTS() {
  auto snapshot = sidFilePlayer->getRegisterSnapshot();
  if (!snapshot.valid)
    return;

  // Helper: set APVTS param by ID and denormalized value
  auto setParam = [this](const juce::String &id, float val) {
    auto *p = apvts.getParameter(id);
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(val));
  };

  // SID register layout per voice (7 bytes each):
  // +0: Freq lo, +1: Freq hi, +2: PW lo, +3: PW hi (bits 0-3)
  // +4: Control (gate, sync, ring, test, waveform bits 4-7)
  // +5: Attack (hi nibble), Decay (lo nibble)
  // +6: Sustain (hi nibble), Release (lo nibble)
  for (int voice = 0; voice < 3; ++voice) {
    int base = voice * 7;
    juce::String vp = "v" + juce::String(voice) + "_";

    // Pulse width (12-bit: lo byte + hi nibble)
    int pw = snapshot.regs[base + 2] | ((snapshot.regs[base + 3] & 0x0F) << 8);
    setParam(vp + "pw", static_cast<float>(pw));

    // Waveform from control register bits 4-7
    uint8_t ctrl = snapshot.regs[base + 4];
    int waveIdx = 0; // Triangle
    if (ctrl & 0x20)
      waveIdx = 1; // Sawtooth
    else if (ctrl & 0x40)
      waveIdx = 2; // Pulse
    else if (ctrl & 0x80)
      waveIdx = 3; // Noise
    else if (ctrl & 0x10)
      waveIdx = 0; // Triangle
    setParam(vp + "waveform", static_cast<float>(waveIdx));

    // Sync and Ring mod
    setParam(vp + "sync", (ctrl & 0x02) ? 1.0f : 0.0f);
    setParam(vp + "ringMod", (ctrl & 0x04) ? 1.0f : 0.0f);

    // ADSR
    setParam(vp + "attack",
             static_cast<float>((snapshot.regs[base + 5] >> 4) & 0x0F));
    setParam(vp + "decay", static_cast<float>(snapshot.regs[base + 5] & 0x0F));
    setParam(vp + "sustain",
             static_cast<float>((snapshot.regs[base + 6] >> 4) & 0x0F));
    setParam(vp + "release",
             static_cast<float>(snapshot.regs[base + 6] & 0x0F));

    // Filter routing per voice (register 0x17, bits 0-2)
    bool voiceFiltered = (snapshot.regs[0x17] >> voice) & 0x01;
    setParam(vp + "filter", voiceFiltered ? 1.0f : 0.0f);
  }

  // Filter cutoff (11-bit: reg 0x15 bits 0-2 + reg 0x16)
  int cutoff = (snapshot.regs[0x15] & 0x07) | (snapshot.regs[0x16] << 3);
  setParam("leftCutoff", static_cast<float>(cutoff));

  // Filter resonance (reg 0x17 hi nibble, 0-15)
  int resonance = (snapshot.regs[0x17] >> 4) & 0x0F;
  setParam("leftResonance", static_cast<float>(resonance));

  // Filter mode (reg 0x18 bits 4-6)
  uint8_t modeReg = snapshot.regs[0x18];
  setParam("leftLP", (modeReg & 0x10) ? 1.0f : 0.0f);
  setParam("leftBP", (modeReg & 0x20) ? 1.0f : 0.0f);
  setParam("leftHP", (modeReg & 0x40) ? 1.0f : 0.0f);

  // Master volume (reg 0x18 lo nibble, 0-15 -> 0.0-1.0)
  int sidVol = modeReg & 0x0F;
  setParam("masterVolume", static_cast<float>(sidVol) / 15.0f);
}
