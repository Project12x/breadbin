#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sstream>

namespace {
static void addSidCounters(SIDEngine::PerfCounters &dst,
                           const SIDEngine::PerfCounters &src) {
  dst.setFrequencyCalls += src.setFrequencyCalls;
  dst.setFrequencySame += src.setFrequencySame;
  dst.setPulseWidthCalls += src.setPulseWidthCalls;
  dst.setPulseWidthSame += src.setPulseWidthSame;
  dst.setFilterCutoffCalls += src.setFilterCutoffCalls;
  dst.setFilterCutoffSame += src.setFilterCutoffSame;
  dst.setFilterResonanceCalls += src.setFilterResonanceCalls;
  dst.setFilterResonanceSame += src.setFilterResonanceSame;
  dst.setFilterModeCalls += src.setFilterModeCalls;
  dst.setFilterModeSame += src.setFilterModeSame;
  dst.setFilterVoicesCalls += src.setFilterVoicesCalls;
  dst.setFilterVoicesSame += src.setFilterVoicesSame;
  dst.writeRegisterCalls += src.writeRegisterCalls;
  dst.writeRegisterSame += src.writeRegisterSame;
}

static bool usesLeft(BreadbinProcessor::PolySidRenderRole role) {
  return role == BreadbinProcessor::PolySidRenderRole::Pair ||
         role == BreadbinProcessor::PolySidRenderRole::LeftMono;
}

static bool usesRight(BreadbinProcessor::PolySidRenderRole role) {
  return role == BreadbinProcessor::PolySidRenderRole::Pair ||
         role == BreadbinProcessor::PolySidRenderRole::RightMono;
}
} // namespace

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

  // Pre-allocate poly voice pool (24 SID engines, ~1.2MB)
  for (auto &pv : polyVoices) {
    pv.sidLeft = std::make_unique<SIDEngine>();
    pv.sidRight = std::make_unique<SIDEngine>();
  }

  // Initialize SID file player
  sidFilePlayer = std::make_unique<SidFilePlayer>();

  // Link parameter pointers for fast access
  initializeParameterPointers();

  // Initialize MIDI mappings
  midiMappings.fill(ControlParam::None);

  cpuSections.paramsTransport = cpuProfiler.registerSection("Params/Transport");
  cpuSections.deferredUpdates = cpuProfiler.registerSection("DeferredUpdates");
  cpuSections.voiceSettings = cpuProfiler.registerSection("VoiceSettings");
  cpuSections.midi = cpuProfiler.registerSection("MIDI");
  cpuSections.arpeggiator = cpuProfiler.registerSection("Arpeggiator");
  cpuSections.wavetable = cpuProfiler.registerSection("Wavetable");
  cpuSections.glide = cpuProfiler.registerSection("Glide");
  cpuSections.lfo = cpuProfiler.registerSection("LFO");
  cpuSections.modulation = cpuProfiler.registerSection("Modulation");
  cpuSections.polyMod = cpuProfiler.registerSection("PolyMod");
  cpuSections.sidRender = cpuProfiler.registerSection("SIDRender");
  cpuSections.sidFile = cpuProfiler.registerSection("SIDFile");
  cpuSections.chorus = cpuProfiler.registerSection("Chorus");
  cpuSections.delay = cpuProfiler.registerSection("Delay");
  cpuSections.reverb = cpuProfiler.registerSection("Reverb");
  cpuSections.safetyFilters = cpuProfiler.registerSection("SafetyFilters");
  cpuSections.limiter = cpuProfiler.registerSection("Limiter");
  cpuSections.noiseGate = cpuProfiler.registerSection("NoiseGate");
  cpuSections.analysis = cpuProfiler.registerSection("Analysis");
}

BreadbinProcessor::~BreadbinProcessor() = default;

void BreadbinProcessor::resetCpuAuditCounters() {
  cpuAuditCounters = {};
  sidLeft.resetPerfCounters();
  sidRight.resetPerfCounters();
  sidLeft.setPerfCountersEnabled(true);
  sidRight.setPerfCountersEnabled(true);
  for (auto &pv : polyVoices) {
    pv.sidLeft->resetPerfCounters();
    pv.sidRight->resetPerfCounters();
    pv.sidLeft->setPerfCountersEnabled(true);
    pv.sidRight->setPerfCountersEnabled(true);
  }
}

std::string
BreadbinProcessor::getCpuAuditCountersJson(int measuredBlocks) const {
  SIDEngine::PerfCounters total;
  addSidCounters(total, sidLeft.getPerfCounters());
  addSidCounters(total, sidRight.getPerfCounters());
  for (const auto &pv : polyVoices) {
    addSidCounters(total, pv.sidLeft->getPerfCounters());
    addSidCounters(total, pv.sidRight->getPerfCounters());
  }

  const auto denom =
      measuredBlocks > 0 ? static_cast<double>(measuredBlocks) : 1.0;
  std::ostringstream json;
  json << "{"
       << "\"blocks\":" << cpuAuditCounters.blocks
       << ",\"activePolyVoicesAvg\":"
       << (cpuAuditCounters.activePolyVoices / denom)
       << ",\"activePolyNoteSlotsAvg\":"
       << (cpuAuditCounters.activePolyNoteSlots / denom)
       << ",\"polySidRenderSkipBlocks\":"
       << cpuAuditCounters.polySidRenderSkipBlocks
       << ",\"polyPairVoiceBlocks\":" << cpuAuditCounters.polyPairVoiceBlocks
       << ",\"polyLeftMonoVoiceBlocks\":"
       << cpuAuditCounters.polyLeftMonoVoiceBlocks
       << ",\"polyRightMonoVoiceBlocks\":"
       << cpuAuditCounters.polyRightMonoVoiceBlocks
       << ",\"setFrequencyCalls\":" << total.setFrequencyCalls
       << ",\"setFrequencySame\":" << total.setFrequencySame
       << ",\"setPulseWidthCalls\":" << total.setPulseWidthCalls
       << ",\"setPulseWidthSame\":" << total.setPulseWidthSame
       << ",\"setFilterCutoffCalls\":" << total.setFilterCutoffCalls
       << ",\"setFilterCutoffSame\":" << total.setFilterCutoffSame
       << ",\"setFilterResonanceCalls\":" << total.setFilterResonanceCalls
       << ",\"setFilterResonanceSame\":" << total.setFilterResonanceSame
       << ",\"setFilterModeCalls\":" << total.setFilterModeCalls
       << ",\"setFilterModeSame\":" << total.setFilterModeSame
       << ",\"setFilterVoicesCalls\":" << total.setFilterVoicesCalls
       << ",\"setFilterVoicesSame\":" << total.setFilterVoicesSame
       << ",\"writeRegisterCalls\":" << total.writeRegisterCalls
       << ",\"writeRegisterSame\":" << total.writeRegisterSame
       << "}";
  return json.str();
}

std::array<BreadbinProcessor::PolyVoiceRoleDebug, BreadbinProcessor::MAX_POLY>
BreadbinProcessor::getPolyVoiceRoleDebug() const {
  std::array<PolyVoiceRoleDebug, MAX_POLY> out{};
  for (int i = 0; i < MAX_POLY; ++i) {
    const auto &pv = polyVoices[i];
    out[i] = {pv.midiNote, pv.startSample, pv.active, pv.releasing,
              pv.sidRenderRole};
  }
  return out;
}

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
  sidRenderSilenceGate.prepare(sampleRate, samplesPerBlock);
  sidRenderSilenceGate.setThreshold(1.0e-5f);
  sidRenderSilenceGate.setHoldBlocks(8);
  sidRenderTailPeak = 0.0f;
  sidRenderWasSkipping = false;

  // Prepare all poly voice SID engines
  for (auto &pv : polyVoices) {
    pv.sidLeft->prepare(sampleRate);
    pv.sidRight->prepare(sampleRate);
    pv.sidRenderGate.prepare(sampleRate, samplesPerBlock);
    pv.sidRenderGate.setThreshold(1.0e-2f);
    pv.sidRenderGate.setHoldBlocks(8);
    pv.sidRenderTailPeak = 0.0f;
    pv.sidRenderWasSkipping = false;
    pv.sidLeft->setChipModel(chipModelLeft);
    pv.sidRight->setChipModel(chipModelRight);
    pv.sidLeft->setClockMode(clockMode);
    pv.sidRight->setClockMode(clockMode);
  }

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

  reverb.prepare(static_cast<float>(sampleRate));

  // Gain smoothing coefficient: ~5ms time constant
  gainSmoothCoeff = 1.0f - std::exp(-1.0f / (0.005f * static_cast<float>(sampleRate)));
}

void BreadbinProcessor::releaseResources() {}

void BreadbinProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                     juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  const auto cpuTimerStart = juce::Time::getHighResolutionTicks();

  cpuProfiler.beginSection(cpuSections.paramsTransport);

  // Add messages from virtual keyboard (standalone mode)
  midiCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());

  // Sync global parameters from APVTS
  masterVolume = masterVolPtr->load();
  // Master volume is applied as output gain in the sample loop.
  // SID volume register set to max for voice rendering; digi modes
  // manage the register themselves (4-bit writes $D418, 8-bit zeroes it).
  dualMode = static_cast<DualMode>(static_cast<int>(dualModePtr->load()));
  float newLeftDetune = leftDetunePtr->load();
  float newRightDetune = rightDetunePtr->load();
  if (newLeftDetune != leftDetuneCents) {
    leftDetuneCents = newLeftDetune;
    cachedDetuneL = std::pow(2.0, leftDetuneCents / 1200.0);
  }
  if (newRightDetune != rightDetuneCents) {
    rightDetuneCents = newRightDetune;
    cachedDetuneR = std::pow(2.0, rightDetuneCents / 1200.0);
  }
  glideTimeMs = glidePtr->load();
  extInputEnabled = extInputEnablePtr->load() > 0.5f;
  extInputLevel = extInputLevelPtr->load();
  pitchBendRange = static_cast<int>(pitchBendRangePtr->load());
  voiceMode = static_cast<VoiceMode>(
      juce::jlimit(0, 3, static_cast<int>(voiceModePtr->load())));
  const auto oldEcoMode = ecoMode;
  const auto oldPolySidBudget = polySidBudget;
  const auto oldPolyStereoAnchor = polyStereoAnchor;
  ecoMode = static_cast<EcoMode>(
      juce::jlimit(0, 1, static_cast<int>(ecoModePtr->load())));
  polySidBudget = static_cast<PolySidBudget>(
      juce::jlimit(0, 2, static_cast<int>(polySidBudgetPtr->load())));
  polyStereoAnchor = static_cast<PolyStereoAnchor>(
      juce::jlimit(0, 1, static_cast<int>(polyStereoAnchorPtr->load())));
  polyMaxNotes = juce::jlimit(1, MAX_POLY, static_cast<int>(polyMaxNotesPtr->load()));
  if (oldEcoMode != ecoMode || oldPolySidBudget != polySidBudget ||
      oldPolyStereoAnchor != polyStereoAnchor) {
    const auto oldRoles = snapshotPolyRenderRoles();
    rebalancePolyRenderRoles();
    syncPromotedPolyRenderSides(oldRoles);
  }
  if (cpuProfiler.isEnabled()) {
    ++cpuAuditCounters.blocks;
    for (int pi = 0; pi < polyMaxNotes; ++pi) {
      const auto &pv = polyVoices[pi];
      if (pv.active || pv.releasing) {
        ++cpuAuditCounters.activePolyVoices;
        cpuAuditCounters.activePolyNoteSlots +=
            (voiceMode == VoiceMode::PolyPara)
                ? static_cast<uint64_t>(pv.paraCount)
                : 1u;
      }
    }
  }
  bool digiEnabled = digiEnablePtr->load() > 0.5f;
  digiSampler.setRootNote(static_cast<int>(digiRootNotePtr->load()));
  digiSampler.setLooping(digiLoopPtr->load() > 0.5f);
  int digiBitDepth = static_cast<int>(digiBitDepthPtr->load()) == 1 ? 8 : 4;
  digiSampler.setBitDepth(digiBitDepth);

  // Query DAW playhead for BPM (fallback 120 in standalone / no transport)
  double bpm = 120.0;
  if (auto *ph = getPlayHead())
    if (auto pos = ph->getPosition())
      if (pos->getBpm().hasValue())
        bpm = *pos->getBpm();

  // Sync LFO from APVTS
  lfo.enabled = lfoEnablePtr->load() > 0.5f;
  lfo.waveform = static_cast<LFOWaveform>(static_cast<int>(lfoWavePtr->load()));
  lfo.rate = lfoRatePtr->load();
  if (isLfoSynced()) {
    int idx = juce::jlimit(0, 10, juce::roundToInt(lfoSyncDivPtr->load()));
    lfo.rate = static_cast<float>(bpm / 60.0 / kSyncDivBeats[idx]);
  }
  lfo.depthFilter = lfoDepthFiltPtr->load();
  lfo.depthPulseWidth = lfoDepthPWPtr->load();
  lfo.depthPitch = lfoDepthPitchPtr->load();

  // Sync LFO2 from APVTS
  lfo2.enabled = lfo2EnablePtr->load() > 0.5f;
  lfo2.waveform =
      static_cast<LFOWaveform>(static_cast<int>(lfo2WavePtr->load()));
  lfo2.rate = lfo2RatePtr->load();
  if (isLfo2Synced()) {
    int idx = juce::jlimit(0, 10, juce::roundToInt(lfo2SyncDivPtr->load()));
    lfo2.rate = static_cast<float>(bpm / 60.0 / kSyncDivBeats[idx]);
  }
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

  cpuProfiler.endSection(cpuSections.paramsTransport);
  cpuProfiler.beginSection(cpuSections.deferredUpdates);

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
    if (isPolyActive()) {
      for (int pi = 0; pi < polyMaxNotes; ++pi) {
        polyVoices[pi].sidLeft->setChipModel(chipModelLeft);
        polyVoices[pi].sidRight->setChipModel(chipModelRight);
      }
    }
  }
  if (clockModeDirty.exchange(false, std::memory_order_relaxed)) {
    setClockMode(clockMode);
    if (isPolyActive()) {
      for (int pi = 0; pi < polyMaxNotes; ++pi) {
        polyVoices[pi].sidLeft->setClockMode(clockMode);
        polyVoices[pi].sidRight->setClockMode(clockMode);
      }
    }
  }

  cpuProfiler.endSection(cpuSections.deferredUpdates);
  cpuProfiler.beginSection(cpuSections.voiceSettings);

  // Sync voice settings from APVTS (skips SID writes if unchanged)
  for (int v = 0; v < 6; ++v) {
    applyVoiceSettings(v);
    // Release voice immediately when toggled off
    if (!voiceSettings[v].enabled && voices[v].active) {
      releaseNote(v);
    }
  }

  // Sync voice settings to active poly voices (waveform, ADSR, filter, etc.)
  if (isPolyActive()) {
    for (int pi = 0; pi < polyMaxNotes; ++pi)
      if (polyVoices[pi].active || polyVoices[pi].releasing)
        applySettingsToPolyVoice(pi);
  }

  cpuProfiler.endSection(cpuSections.voiceSettings);
  cpuProfiler.beginSection(cpuSections.midi);

  // Handle MIDI
  for (const auto metadata : midiMessages) {
    handleMidiEvent(metadata.getMessage());
  }
  cpuProfiler.endSection(cpuSections.midi);

  // Process arpeggiator (disabled while chord memory is active)
  const int numSamples = buffer.getNumSamples();
  cpuProfiler.beginSection(cpuSections.arpeggiator);
  if (!chordMemory.enabled && arpEnabled && arpSeqCount > 0) {
    processArpeggiator(numSamples);
  }
  cpuProfiler.endSection(cpuSections.arpeggiator);

  // Process wavetable step sequencer (after arp, before LFO)
  cpuProfiler.beginSection(cpuSections.wavetable);
  if (wavetable.enabled) {
    processWavetable(numSamples);
  }
  cpuProfiler.endSection(cpuSections.wavetable);

  // Process glide/portamento
  cpuProfiler.beginSection(cpuSections.glide);
  if (glideTimeMs > 0.0f) {
    double glideTimeSec = glideTimeMs / 1000.0;
    double samplesPerGlide = hostSampleRate * glideTimeSec;
    double glideRate = static_cast<double>(numSamples) / samplesPerGlide;

    if (isPolyActive()) {
      // Poly glide: process per-poly-voice
      for (int pi = 0; pi < polyMaxNotes; ++pi) {
        auto &pv = polyVoices[pi];
        if (!pv.active || !pv.isGliding) continue;
        const bool useLeft = pv.sidRenderRole == PolySidRenderRole::Pair ||
                             pv.sidRenderRole == PolySidRenderRole::LeftMono;
        const bool useRight = pv.sidRenderRole == PolySidRenderRole::Pair ||
                              pv.sidRenderRole == PolySidRenderRole::RightMono;
        if (std::abs(pv.currentHz - pv.targetHz) < 0.1) {
          pv.currentHz = pv.targetHz;
          pv.isGliding = false;
        } else {
          pv.currentHz += (pv.targetHz - pv.currentHz) * glideRate;
          for (int v = 0; v < 3; ++v) {
            if (useLeft)
              pv.sidLeft->setFrequency(v, pv.currentHz);
            if (useRight)
              pv.sidRight->setFrequency(v, pv.currentHz);
          }
        }
      }
    } else {
      // Mono glide
      for (int v = 0; v < 6; ++v) {
        if (voices[v].active && voices[v].isGliding) {
          double currentHz = voices[v].currentHz;
          double targetHz = voices[v].targetHz;
          if (std::abs(currentHz - targetHz) < 0.1) {
            voices[v].currentHz = targetHz;
            voices[v].isGliding = false;
          } else {
            double newHz = currentHz + (targetHz - currentHz) * glideRate;
            voices[v].currentHz = newHz;
            SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
            sid.setFrequency(v % 3, newHz);
          }
        }
      }
    }
  }
  cpuProfiler.endSection(cpuSections.glide);

  // Process LFO modulation
  cpuProfiler.beginSection(cpuSections.lfo);
  if (lfo.enabled)
    tickLFO(lfo, lfoRng, numSamples);
  if (lfo2.enabled)
    tickLFO(lfo2, lfo2Rng, numSamples);
  cpuProfiler.endSection(cpuSections.lfo);

  cpuProfiler.beginSection(cpuSections.modulation);
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
  cpuProfiler.endSection(cpuSections.modulation);

  // Poly modulation: propagate global modulation state to all active poly voices.
  // LFO/mod matrix/PWM sweep have already computed their values and written to
  // the mono SIDs (sidLeft/sidRight). We extract the effective offsets and apply
  // them to each poly voice's SID pair for PW, pitch, and filter cutoff.
  cpuProfiler.beginSection(cpuSections.polyMod);
  if (isPolyActive()) {
    // Compute global PW modulation offset
    float val1 = lfo.enabled ? lfo.currentValue : 0.0f;
    float val2 = lfo2.enabled ? lfo2.currentValue : 0.0f;
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

    // Compute global pitch modulation offset (semitones)
    float pitchDepth1 = lfo.enabled ? lfo.depthPitch : 0.0f;
    float pitchDepth2 = lfo2.enabled ? lfo2.depthPitch : 0.0f;
    float semitoneMod = 0.0f;
    if (pitchDepth1 > 0.0f || pitchDepth2 > 0.0f)
      semitoneMod = val1 * pitchDepth1 * 2.0f + val2 * pitchDepth2 * 2.0f;
    bool wtActive = wavetable.enabled;
    auto &wtStep = wavetable.steps[wavetable.currentStep];
    if (wtActive)
      semitoneMod += static_cast<float>(wtStep.pitchOffset);

    int filterModOffset = computeFilterModOffset();

    double pitchMultiplier =
        (semitoneMod != 0.0f) ? std::pow(2.0, semitoneMod / 12.0) : 1.0;

    // Process per-voice filter envelope and apply all modulation to poly voices
    bool filterEnvOn = filterEnvEnablePtr->load() > 0.5f;
    float filterEnvAmt = filterEnvOn ? filterEnvAmountPtr->load() : 0.0f;
    double bendMultiplier = 1.0;
    bool bendMultiplierReady = false;

    for (int pi = 0; pi < polyMaxNotes; ++pi) {
      auto &pv = polyVoices[pi];
      if (!pv.active && !pv.releasing) continue;
      const bool useLeft = pv.sidRenderRole == PolySidRenderRole::Pair ||
                           pv.sidRenderRole == PolySidRenderRole::LeftMono;
      const bool useRight = pv.sidRenderRole == PolySidRenderRole::Pair ||
                            pv.sidRenderRole == PolySidRenderRole::RightMono;

      // Per-voice filter envelope
      if (filterEnvOn)
        processPolyFilterEnvelope(pi, numSamples);
      int perVoiceEnvOffset =
          static_cast<int>(pv.filterEnv.currentValue * filterEnvAmt * 2047.0f);

      // Apply filter cutoff: base + global mod + per-voice envelope
      int leftCutoff = juce::jlimit(
          0, 2047, baseFilterCutoffLeft + filterModOffset + perVoiceEnvOffset);
      int rightCutoff = juce::jlimit(
          0, 2047, baseFilterCutoffRight + filterModOffset + perVoiceEnvOffset);
      if (useLeft)
        pv.sidLeft->setFilterCutoff(leftCutoff);
      if (useRight)
        pv.sidRight->setFilterCutoff(rightCutoff);

      // Apply PW modulation to all voices on this poly pair
      for (int v = 0; v < 3; ++v) {
        int basePWL = wtActive ? wtStep.pulseWidth : voiceSettings[v].pulseWidth;
        if (useLeft)
          pv.sidLeft->setPulseWidth(v, std::clamp(basePWL + pwMod, 0, 4095));
        int basePWR = wtActive ? wtStep.pulseWidth : voiceSettings[v + 3].pulseWidth;
        if (useRight)
          pv.sidRight->setPulseWidth(v, std::clamp(basePWR + pwMod, 0, 4095));
      }

      // Apply pitch modulation + pitch bend to all voices
      if (!bendMultiplierReady) {
        double bendSemitones = pitchBendValue * pitchBendRange;
        bendMultiplier = std::pow(2.0, bendSemitones / 12.0);
        bendMultiplierReady = true;
      }
      if (voiceMode == VoiceMode::PolyPara && pv.paraCount > 0) {
        // PolyPara: each SID voice has its own note frequency
        for (int v = 0; v < 3; ++v) {
          if (pv.paraNote[v] >= 0) {
            double hz = 440.0 * std::pow(2.0,
                (static_cast<double>(pv.paraNote[v]) - 69.0) / 12.0);
            double modHz = hz * pitchMultiplier * bendMultiplier;
            if (useLeft && voiceSettings[v].enabled)
              pv.sidLeft->setFrequency(v, modHz * cachedDetuneL);
            if (useRight && voiceSettings[v + 3].enabled)
              pv.sidRight->setFrequency(v, modHz * cachedDetuneR);
          }
        }
      } else {
        double modHz = pv.currentHz * pitchMultiplier * bendMultiplier;
        for (int v = 0; v < 3; ++v) {
          if (useLeft && voiceSettings[v].enabled)
            pv.sidLeft->setFrequency(v, modHz * cachedDetuneL);
          if (useRight && voiceSettings[v + 3].enabled)
            pv.sidRight->setFrequency(v, modHz * cachedDetuneR);
        }
      }
    }
  }
  cpuProfiler.endSection(cpuSections.polyMod);

  cpuProfiler.beginSection(cpuSections.sidRender);
  generateAudio(buffer);
  cpuProfiler.endSection(cpuSections.sidRender);
  cpuProfiler.beginSection(cpuSections.sidFile);
  mixSidFilePlayer(buffer);
  cpuProfiler.endSection(cpuSections.sidFile);
  processFXChain(buffer);
  applySafetyChain(buffer);

  // Measure CPU load: time spent / time available
  cpuProfiler.beginSection(cpuSections.analysis);
  const auto cpuTimerEnd = juce::Time::getHighResolutionTicks();
  const double elapsed =
      juce::Time::highResolutionTicksToSeconds(cpuTimerEnd - cpuTimerStart);
  const double budget = static_cast<double>(numSamples) / getSampleRate();
  cpuLoadPercent.store(static_cast<float>(elapsed / budget * 100.0),
                       std::memory_order_relaxed);
  cpuProfiler.endSection(cpuSections.analysis);
}

// ==========================================================================
// processBlock subsystems (extracted for readability)
// ==========================================================================

bool BreadbinProcessor::hasActiveMonoSidSource(bool digiPlaying,
                                               const float *inputLeft) const {
  if (digiPlaying)
    return true;

  if (extInputEnabled && inputLeft != nullptr && extInputLevel > 0.0f)
    return true;

  for (const auto &voice : voices)
    if (voice.active)
      return true;

  for (const auto &paraVoice : paraVoices)
    if (paraVoice.midiNote >= 0)
      return true;

  return false;
}

bool BreadbinProcessor::shouldSkipMonoSidRender(
    juce::AudioBuffer<float> &buffer, bool sourceActive, float targetLeftVG,
    float targetRightVG) {
  const float sourcePeak = sourceActive ? 1.0f : 0.0f;
  const bool skip = sidRenderSilenceGate.update(sourcePeak, sidRenderTailPeak);

  if (!skip) {
    sidRenderWasSkipping = false;
    return false;
  }

  if (!sidRenderWasSkipping) {
    sidLeft.resetRuntimeSilenceState();
    sidRight.resetRuntimeSilenceState();
    sidRenderTailPeak = 0.0f;
  }

  sidRenderWasSkipping = true;
  smoothedMasterVol = masterVolume;
  smoothedLeftVoiceGain = targetLeftVG;
  smoothedRightVoiceGain = targetRightVG;
  buffer.clear();
  return true;
}

void BreadbinProcessor::generateAudio(juce::AudioBuffer<float> &buffer) {
  const int numSamples = buffer.getNumSamples();
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

  // Read per-SID pan from APVTS — recalculate trig only when changed
  updatePanCache(leftPanPtr->load(), cachedLeftPanVal, cachedLeftPanL, cachedLeftPanR);
  updatePanCache(rightPanPtr->load(), cachedRightPanVal, cachedRightPanL, cachedRightPanR);
  float leftGainL = cachedLeftPanL;
  float leftGainR = cachedLeftPanR;
  float rightGainL = cachedRightPanL;
  float rightGainR = cachedRightPanR;

  // Voice count gain compensation: normalize output level regardless of
  // how many voices are enabled. 3 voices per SID is the reference level.
  // In Para mode, count only voices that are actually playing a note.
  int leftVoiceCount = 0, rightVoiceCount = 0;
  bool isPara = (voiceMode == VoiceMode::Paraphonic);
  for (int v = 0; v < 3; ++v)
    if (voiceSettings[v].enabled && (!isPara || paraVoices[v].midiNote >= 0))
      ++leftVoiceCount;
  for (int v = 3; v < 6; ++v)
    if (voiceSettings[v].enabled && (!isPara || paraVoices[v].midiNote >= 0))
      ++rightVoiceCount;
  float targetLeftVG = leftVoiceCount > 0 ? 3.0f / static_cast<float>(leftVoiceCount) : 1.0f;
  float targetRightVG = rightVoiceCount > 0 ? 3.0f / static_cast<float>(rightVoiceCount) : 1.0f;

  bool digiEnabled = digiEnablePtr->load() > 0.5f;
  int digiBitDepth = static_cast<int>(digiBitDepthPtr->load()) == 1 ? 8 : 4;

  if (isPolyActive()) {
    sidRenderSilenceGate.reset();
    sidRenderTailPeak = 0.0f;
    sidRenderWasSkipping = false;

    // === POLY SAMPLE GENERATION ===
    int activePolyIdx[MAX_POLY];
    bool skipPolySid[MAX_POLY] = {};
    float polyTailPeak[MAX_POLY] = {};
    int activePolyCount = 0;
    int activeCount = 0;
    for (int pi = 0; pi < polyMaxNotes; ++pi) {
      if (polyVoices[pi].active || polyVoices[pi].releasing) {
        activePolyIdx[activePolyCount++] = pi;
        if (voiceMode == VoiceMode::PolyPara)
          activeCount += std::max(1, polyVoices[pi].paraCount);
        else
          ++activeCount;
      }
    }
    float targetPolyNorm = activeCount > 0
        ? 1.0f / std::sqrt(static_cast<float>(activeCount))
        : 1.0f;

    for (int ai = 0; ai < activePolyCount; ++ai) {
      auto &pv = polyVoices[activePolyIdx[ai]];
      if (!pv.releasing) {
        pv.sidRenderGate.reset();
        pv.sidRenderWasSkipping = false;
        continue;
      }

      skipPolySid[ai] = pv.sidRenderGate.update(0.0f, pv.sidRenderTailPeak);
      if (skipPolySid[ai]) {
        pv.sidRenderWasSkipping = true;
        pv.sidRenderTailPeak = 0.0f;
        ++cpuAuditCounters.polySidRenderSkipBlocks;
      } else {
        pv.sidRenderWasSkipping = false;
      }
    }

    for (int ai = 0; ai < activePolyCount; ++ai) {
      if (skipPolySid[ai])
        continue;

      auto &pv = polyVoices[activePolyIdx[ai]];
      switch (pv.sidRenderRole) {
      case PolySidRenderRole::Pair:
        ++cpuAuditCounters.polyPairVoiceBlocks;
        break;
      case PolySidRenderRole::LeftMono:
        ++cpuAuditCounters.polyLeftMonoVoiceBlocks;
        break;
      case PolySidRenderRole::RightMono:
        ++cpuAuditCounters.polyRightMonoVoiceBlocks;
        break;
      }
    }

    for (int i = 0; i < numSamples; ++i) {
      smoothedPolyNorm += gainSmoothCoeff * (targetPolyNorm - smoothedPolyNorm);
      smoothedMasterVol += gainSmoothCoeff * (masterVolume - smoothedMasterVol);
      smoothedLeftVoiceGain += gainSmoothCoeff * (targetLeftVG - smoothedLeftVoiceGain);
      smoothedRightVoiceGain += gainSmoothCoeff * (targetRightVG - smoothedRightVoiceGain);

      float outL = 0.0f;
      float outR = 0.0f;

      for (int ai = 0; ai < activePolyCount; ++ai) {
        auto &pv = polyVoices[activePolyIdx[ai]];
        float fadeTarget = pv.fadingOut ? 0.0f : 1.0f;

        pv.fadeGain += gainSmoothCoeff * (fadeTarget - pv.fadeGain);

        if (skipPolySid[ai]) {
          continue;
        }

        float voiceL = 0.0f;
        float voiceR = 0.0f;
        switch (pv.sidRenderRole) {
        case PolySidRenderRole::Pair: {
          const float sL = pv.sidLeft->clock();
          const float sR = pv.sidRight->clock();
          voiceL = sL * pv.fadeGain;
          voiceR = sR * pv.fadeGain;
          break;
        }
        case PolySidRenderRole::LeftMono:
          voiceL = pv.sidLeft->clock() * pv.fadeGain;
          break;
        case PolySidRenderRole::RightMono:
          voiceR = pv.sidRight->clock() * pv.fadeGain;
          break;
        }
        outL += voiceL * leftGainL * smoothedLeftVoiceGain
              + voiceR * rightGainL * smoothedRightVoiceGain;
        outR += voiceL * leftGainR * smoothedLeftVoiceGain
              + voiceR * rightGainR * smoothedRightVoiceGain;
        polyTailPeak[ai] = std::max(
            polyTailPeak[ai], std::max(std::abs(voiceL), std::abs(voiceR)));
      }

      outL *= smoothedPolyNorm * smoothedMasterVol;
      outR *= smoothedPolyNorm * smoothedMasterVol;

      leftChannel[i] = outL;
      if (rightChannel)
        rightChannel[i] = outR;
    }

    // Release detection: begin fade-out before ADSR finishes
    for (int ai = 0; ai < activePolyCount; ++ai) {
      auto &pv = polyVoices[activePolyIdx[ai]];
      if (!skipPolySid[ai])
        pv.sidRenderTailPeak = polyTailPeak[ai];
    }

    for (int pi = 0; pi < polyMaxNotes; ++pi) {
      auto &pv = polyVoices[pi];
      if (pv.releasing && !pv.fadingOut) {
        pv.releaseSamplesRemaining -= numSamples;
        int fadeThreshold = pv.releaseSamplesTotal / 5;
        if (pv.releaseSamplesRemaining <= fadeThreshold) {
          pv.fadingOut = true;
        }
      }
      if (pv.fadingOut && pv.fadeGain < 0.0001f) {
        pv.active = false;
        pv.releasing = false;
        pv.fadingOut = false;
        pv.fadeGain = 0.0f;
        pv.midiNote = -1;
        pv.filterEnv = FilterEnvelopeState{};
        pv.sidRenderRole = PolySidRenderRole::Pair;
        pv.sidRenderGate.reset();
        pv.sidRenderTailPeak = 0.0f;
        pv.sidRenderWasSkipping = false;
      }
    }
  } else {
    // === MONO/PARAPHONIC SAMPLE GENERATION ===
    bool digiPlaying = digiEnabled && digiSampler.isPlaying();
    float digiGain = digiPlaying ? 3.0f : 1.0f;
    const bool sourceActive = hasActiveMonoSidSource(digiPlaying, inputLeft);
    if (shouldSkipMonoSidRender(buffer, sourceActive, targetLeftVG,
                                targetRightVG))
      return;

    sidLeft.setVolume(15);
    sidRight.setVolume(15);

    if (digiPlaying && leftVoiceCount == 0) {
      sidLeft.muteVoices();
    } else {
      sidLeft.unmuteVoices();
    }
    if (digiPlaying && rightVoiceCount == 0) {
      sidRight.muteVoices();
    } else {
      sidRight.unmuteVoices();
    }

    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
      smoothedMasterVol += gainSmoothCoeff * (masterVolume - smoothedMasterVol);
      smoothedLeftVoiceGain += gainSmoothCoeff * (targetLeftVG - smoothedLeftVoiceGain);
      smoothedRightVoiceGain += gainSmoothCoeff * (targetRightVG - smoothedRightVoiceGain);

      if (extInputEnabled && inputLeft != nullptr) {
        float extL = inputLeft[i] * extInputLevel;
        float extR = inputRight[i] * extInputLevel;
        sidLeft.setExternalInput(extL);
        sidRight.setExternalInput(extR);
      }

      float digi8bitSample = 0.0f;
      if (digiEnabled && digiSampler.isPlaying()) {
        int digiVal = digiSampler.getNextSample();
        if (digiVal >= 0) {
          if (digiBitDepth == 4) {
            sidLeft.writeVolumeRegister(static_cast<uint8_t>(digiVal));
            sidRight.writeVolumeRegister(static_cast<uint8_t>(digiVal));
          } else {
            digi8bitSample = (static_cast<float>(digiVal) - 128.0f) / 128.0f * 0.15f;
          }
        }
      }

      float sampleL = sidLeft.clock();
      float sampleR = sidRight.clock();

      float outL = (sampleL * leftGainL * smoothedLeftVoiceGain
                  + sampleR * rightGainL * smoothedRightVoiceGain) * digiGain;
      float outR = (sampleL * leftGainR * smoothedLeftVoiceGain
                  + sampleR * rightGainR * smoothedRightVoiceGain) * digiGain;

      if (digiBitDepth == 8 && digi8bitSample != 0.0f) {
        outL += digi8bitSample * digiGain;
        outR += digi8bitSample * digiGain;
      }

      outL *= smoothedMasterVol;
      outR *= smoothedMasterVol;

      leftChannel[i] = outL;
      if (rightChannel)
        rightChannel[i] = outR;

      blockPeak = std::max(blockPeak, std::abs(outL));
      blockPeak = std::max(blockPeak, std::abs(outR));
    }
    sidRenderTailPeak = blockPeak;
  }
}

void BreadbinProcessor::mixSidFilePlayer(juce::AudioBuffer<float> &buffer) {
  const int numSamples = buffer.getNumSamples();
  auto *leftChannel = buffer.getWritePointer(0);
  auto *rightChannel =
      buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

  if (sidFilePlayer->isPlaying()) {
    int sourceSamples = static_cast<int>(numSamples * sidResampleRatio) + 4;
    if (static_cast<size_t>(sourceSamples) > sidResampleBufCapacity) {
      sourceSamples = static_cast<int>(sidResampleBufCapacity);
    }
    sidFilePlayer->readSamples(sidResampleBufL.data(), sidResampleBufR.data(),
                               sourceSamples);

    if (std::abs(sidResampleRatio - 1.0) < 0.001) {
      for (int i = 0; i < numSamples; ++i) {
        leftChannel[i] += sidResampleBufL[static_cast<size_t>(i)];
        if (rightChannel)
          rightChannel[i] += sidResampleBufR[static_cast<size_t>(i)];
      }
    } else {
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
}

void BreadbinProcessor::processFXChain(juce::AudioBuffer<float> &buffer) {
  const int numSamples = buffer.getNumSamples();

  // Chorus
  cpuProfiler.beginSection(cpuSections.chorus);
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
  cpuProfiler.endSection(cpuSections.chorus);

  // Stereo delay
  cpuProfiler.beginSection(cpuSections.delay);
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
  cpuProfiler.endSection(cpuSections.delay);

  // Reverb
  cpuProfiler.beginSection(cpuSections.reverb);
  if (reverbEnablePtr->load() > 0.5f) {
    reverb.setFeedback(reverbDecayPtr->load());
    reverb.setLPFreq(reverbDampingPtr->load());
    reverb.setMix(reverbMixPtr->load());

    auto *left = buffer.getWritePointer(0);
    auto *right =
        buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < numSamples; ++i) {
      float inL = left[i];
      float inR = right ? right[i] : inL;
      float outL, outR;
      reverb.processSample(inL, inR, outL, outR);
      left[i] = outL;
      if (right)
        right[i] = outR;
    }
  }
  cpuProfiler.endSection(cpuSections.reverb);
}

void BreadbinProcessor::applySafetyChain(juce::AudioBuffer<float> &buffer) {
  const int numSamples = buffer.getNumSamples();

  // DC blocker, ultrasonic filter, limiter
  cpuProfiler.beginSection(cpuSections.safetyFilters);
  juce::dsp::AudioBlock<float> block(buffer);
  juce::dsp::ProcessContextReplacing<float> context(block);
  subsonicFilter.process(context);
  ultrasonicFilter.process(context);
  cpuProfiler.endSection(cpuSections.safetyFilters);
  cpuProfiler.beginSection(cpuSections.limiter);
  safetyLimiter.process(context);
  cpuProfiler.endSection(cpuSections.limiter);

  // Envelope-following noise gate with attack/hold/release smoothing
  cpuProfiler.beginSection(cpuSections.noiseGate);
  const float gateThreshold =
      noiseGateThresholdPtr->load(std::memory_order_relaxed);
  if (gateThreshold > 0.0001f) {
    const float attackMs = gateAttackPtr->load(std::memory_order_relaxed);
    const float releaseMs = gateReleasePtr->load(std::memory_order_relaxed);
    const float holdMs = gateHoldPtr->load(std::memory_order_relaxed);
    if (attackMs != gateCache.prevAttack || releaseMs != gateCache.prevRelease ||
        holdMs != gateCache.prevHold || gateThreshold != gateCache.prevThreshold) {
      const float sr = static_cast<float>(getSampleRate());
      gateCache.prevAttack = attackMs;
      gateCache.prevRelease = releaseMs;
      gateCache.prevHold = holdMs;
      gateCache.prevThreshold = gateThreshold;
      gateCache.envAttackCoeff = 1.0f - std::exp(-1.0f / (0.001f * sr));
      gateCache.envReleaseCoeff = 1.0f - std::exp(-1.0f / (0.05f * sr));
      gateCache.gainAttackCoeff = 1.0f - std::exp(-1.0f / (attackMs * 0.001f * sr));
      gateCache.gainReleaseCoeff = 1.0f - std::exp(-1.0f / (releaseMs * 0.001f * sr));
      gateCache.holdSamples = static_cast<int>(holdMs * 0.001f * sr);
      gateCache.closeThreshold = gateThreshold * 0.5f;
    }

    for (int ch = 0; ch < buffer.getNumChannels() && ch < 2; ++ch) {
      auto *channelData = buffer.getWritePointer(ch);
      auto &gs = gateState[static_cast<size_t>(ch)];

      for (int i = 0; i < numSamples; ++i) {
        const float inputLevel = std::abs(channelData[i]);

        if (inputLevel > gs.envelope)
          gs.envelope += gateCache.envAttackCoeff * (inputLevel - gs.envelope);
        else
          gs.envelope += gateCache.envReleaseCoeff * (inputLevel - gs.envelope);

        bool gateOpen;
        if (gs.envelope >= gateThreshold) {
          gateOpen = true;
          gs.holdCounter = gateCache.holdSamples;
        } else if (gs.holdCounter > 0) {
          gs.holdCounter--;
          gateOpen = true;
        } else {
          gateOpen = gs.envelope >= gateCache.closeThreshold;
        }

        const float targetGain = gateOpen ? 1.0f : 0.0f;
        if (targetGain > gs.gain)
          gs.gain += gateCache.gainAttackCoeff * (targetGain - gs.gain);
        else
          gs.gain += gateCache.gainReleaseCoeff * (targetGain - gs.gain);

        channelData[i] *= gs.gain;
      }
    }
  }
  cpuProfiler.endSection(cpuSections.noiseGate);
}

void BreadbinProcessor::handleMidiEvent(const juce::MidiMessage &msg) {
  if (msg.isNoteOn()) {
    lastVelocity = msg.getVelocity();
    handleNoteOn(msg.getNoteNumber(), msg.getChannel());
  } else if (msg.isNoteOff()) {
    handleNoteOff(msg.getNoteNumber(), msg.getChannel());
  } else if (msg.isAllNotesOff()) {
    handleAllNotesOff();
  } else if (msg.isPitchWheel()) {
    pitchBendValue = (msg.getPitchWheelValue() - 8192) / 8192.0f;
    updateAllVoiceFrequencies();
  } else if (msg.isController()) {
    int cc = msg.getControllerNumber();
    int value = msg.getControllerValue();
    handleCC(cc, value);
    if (cc == 1) {
      modWheelValue = value / 127.0f;
    } else if (cc == 64) {
      handleSustainPedal(value);
    }
  }
}

void BreadbinProcessor::handleNoteOn(int note, int channel) {
  // Chord learn mode: capture notes without triggering
  if (chordLearnActive) {
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

  // Digi sampler: trigger on note-on (mono only)
  if (voiceMode == VoiceMode::Mono && digiEnablePtr->load() > 0.5f && digiSampler.isLoaded())
    digiSampler.noteOn(note, hostSampleRate);

  // Chord memory takes priority and does not feed arp tracking
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

  if (arpEnabled)
    return;

  // Route note-on through voice mode
  switch (voiceMode) {
  case VoiceMode::Mono:
    if (dualMode == DualMode::Multitimbral) {
      if (channel == 2) {
        rightNoteQueue.addIfNotAlreadyThere(note);
        updateSIDFromQueue(false);
      } else {
        leftNoteQueue.addIfNotAlreadyThere(note);
        updateSIDFromQueue(true);
      }
    } else {
      leftNoteQueue.addIfNotAlreadyThere(note);
      rightNoteQueue.addIfNotAlreadyThere(note);
      updateSIDFromQueue(true);
      updateSIDFromQueue(false);
    }
    break;
  case VoiceMode::Paraphonic:
    if (dualMode == DualMode::Multitimbral) {
      if (channel == 2)
        paraNoteOnSID(false, note, lastVelocity);
      else
        paraNoteOnSID(true, note, lastVelocity);
    } else {
      paraNoteOn(note, lastVelocity);
    }
    break;
  case VoiceMode::Polyphonic:
    polyNoteOn(note, lastVelocity);
    break;
  case VoiceMode::PolyPara:
    polyParaNoteOn(note, lastVelocity);
    break;
  }
}

void BreadbinProcessor::handleNoteOff(int note, int channel) {
  if (digiEnablePtr->load() > 0.5f && digiSampler.isPlaying())
    digiSampler.noteOff();

  // Chord memory release
  if (chordMemory.enabled) {
    if (!sustainActive) {
      if (dualMode == DualMode::Multitimbral) {
        if (channel == 2)
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
      switch (voiceMode) {
      case VoiceMode::Mono:
        for (int v = 0; v < 6; ++v) releaseNote(v);
        break;
      case VoiceMode::Paraphonic:
        paraAllNotesOff();
        break;
      case VoiceMode::Polyphonic:
        polyAllNotesOff();
        break;
      case VoiceMode::PolyPara:
        polyParaAllNotesOff();
        break;
      }
      lastArpNote = -1;
    }
    return;
  }

  if (sustainActive)
    return;

  // Route note-off through voice mode
  switch (voiceMode) {
  case VoiceMode::Mono:
    leftNoteQueue.removeFirstMatchingValue(note);
    rightNoteQueue.removeFirstMatchingValue(note);
    updateSIDFromQueue(true);
    updateSIDFromQueue(false);
    break;
  case VoiceMode::Paraphonic:
    if (dualMode == DualMode::Multitimbral) {
      if (channel == 2)
        paraNoteOffSID(false, note);
      else
        paraNoteOffSID(true, note);
    } else {
      paraNoteOff(note);
    }
    break;
  case VoiceMode::Polyphonic:
    polyNoteOff(note);
    break;
  case VoiceMode::PolyPara:
    polyParaNoteOff(note);
    break;
  }
}

void BreadbinProcessor::handleAllNotesOff() {
  arpHeldCount = 0;
  arpSeqCount = 0;
  lastArpNote = -1;

  switch (voiceMode) {
  case VoiceMode::Mono:
    leftNoteQueue.clear();
    rightNoteQueue.clear();
    for (int v = 0; v < 6; ++v)
      releaseNote(v);
    break;
  case VoiceMode::Paraphonic:
    paraAllNotesOff();
    break;
  case VoiceMode::Polyphonic:
    polyAllNotesOff();
    break;
  case VoiceMode::PolyPara:
    polyParaAllNotesOff();
    break;
  }
}

void BreadbinProcessor::handleSustainPedal(int value) {
  bool pedalDown = (value >= 64);
  if (sustainActive == pedalDown)
    return;
  sustainActive = pedalDown;

  if (sustainActive || arpEnabled)
    return;

  // Release notes that are no longer physically held
  if (isPolyActive()) {
    for (int i = 0; i < polyMaxNotes; ++i) {
      auto &pv = polyVoices[i];
      if (pv.active && !pv.releasing) {
        bool held = false;
        for (int ai = 0; ai < arpHeldCount; ++ai) {
          if (arpHeldNotes[ai] == pv.midiNote) {
            held = true;
            break;
          }
        }
        if (!held) {
          if (voiceMode == VoiceMode::PolyPara)
            polyParaNoteOff(pv.midiNote);
          else
            polyNoteOff(pv.midiNote);
        }
      }
    }
  } else if (voiceMode == VoiceMode::Paraphonic) {
    for (int v = 0; v < 6; ++v) {
      if (paraVoices[v].midiNote >= 0) {
        bool held = false;
        for (int ai = 0; ai < arpHeldCount; ++ai) {
          if (arpHeldNotes[ai] == paraVoices[v].midiNote) {
            held = true;
            break;
          }
        }
        if (!held)
          paraNoteOff(paraVoices[v].midiNote);
      }
    }
  } else {
    // Mono: clean up note queues
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

    // Skip mod offset in para modes — ring/sync are disabled so the offset
    // has no musical purpose, and it would detune voice 2's para note
    bool isPara = (voiceMode == VoiceMode::Paraphonic);
    if (!isPara && (hasSync || hasRing) && std::abs(offsetSemitones) > 0.01f) {
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
  vs.ringMod = ptrs.ringMod->load() > 0.5f;
  vs.sync = ptrs.sync->load() > 0.5f;
  vs.filterEnabled = ptrs.filter->load() > 0.5f;

  // Skip SID register writes if nothing changed since last block
  if (vs == prevVoiceSettings[voice])
    return;
  prevVoiceSettings[voice] = vs;

  SIDEngine &sid = (voice < 3) ? sidLeft : sidRight;
  int sidVoice = voice % 3;
  bool isLeft = (voice < 3);
  bool isPara = (voiceMode == VoiceMode::Paraphonic);

  // In Para mode, all SID voices share the master voice's timbre settings
  // (voice 0 for left SID, voice 3 for right SID) so chord notes match.
  // This mirrors real paraphonic synths where only pitch is per-voice.
  if (isPara && sidVoice != 0) {
    auto &master = voiceSettings[isLeft ? 0 : 3];
    sid.setWaveform(sidVoice, master.waveform);
    sid.setPulseWidth(sidVoice, master.pulseWidth);
    sid.setAttack(sidVoice, master.attack);
    sid.setDecay(sidVoice, master.decay);
    sid.setSustain(sidVoice, master.sustain);
    sid.setRelease(sidVoice, master.release);
  } else {
    sid.setWaveform(sidVoice, vs.waveform);
    sid.setPulseWidth(sidVoice, vs.pulseWidth);
    sid.setAttack(sidVoice, vs.attack);
    sid.setDecay(sidVoice, vs.decay);
    sid.setSustain(sidVoice, vs.sustain);
    sid.setRelease(sidVoice, vs.release);
  }

  // In Para mode, ring mod and sync cause cross-voice intermodulation
  // noise since voices play independent notes, so force them off
  sid.setRingMod(sidVoice, isPara ? false : vs.ringMod);
  sid.setSync(sidVoice, isPara ? false : vs.sync);

  // Update per-voice filter routing on this SID
  int startV = isLeft ? 0 : 3;
  if (isPara) {
    // All voices share master voice's filter routing for consistent timbre
    bool masterFilter = voiceParamPtrs[startV].filter->load() > 0.5f;
    sid.setFilterVoices(masterFilter, masterFilter, masterFilter);
  } else {
    sid.setFilterVoices(voiceParamPtrs[startV + 0].filter->load() > 0.5f,
                        voiceParamPtrs[startV + 1].filter->load() > 0.5f,
                        voiceParamPtrs[startV + 2].filter->load() > 0.5f);
  }
}

void BreadbinProcessor::updateAllVoiceFrequencies() {
  float bendSemitones = pitchBendValue * static_cast<float>(pitchBendRange);

  if (isPolyActive()) {
    // Poly/PolyPara: apply pitch bend to all active poly voice SIDs
    double bendMult = std::pow(2.0, bendSemitones / 12.0);
    for (int pi = 0; pi < polyMaxNotes; ++pi) {
      auto &pv = polyVoices[pi];
      if (!pv.active && !pv.releasing) continue;
      const bool useLeft = pv.sidRenderRole == PolySidRenderRole::Pair ||
                           pv.sidRenderRole == PolySidRenderRole::LeftMono;
      const bool useRight = pv.sidRenderRole == PolySidRenderRole::Pair ||
                            pv.sidRenderRole == PolySidRenderRole::RightMono;
      if (voiceMode == VoiceMode::PolyPara && pv.paraCount > 0) {
        // PolyPara: each SID voice has its own note
        for (int v = 0; v < 3; ++v) {
          if (pv.paraNote[v] >= 0) {
            double hz = 440.0 * std::pow(2.0,
                (static_cast<double>(pv.paraNote[v]) - 69.0) / 12.0) * bendMult;
            if (useLeft)
              pv.sidLeft->setFrequency(v, hz * cachedDetuneL);
            if (useRight)
              pv.sidRight->setFrequency(v, hz * cachedDetuneR);
          }
        }
      } else {
        double hz = pv.currentHz * bendMult;
        for (int v = 0; v < 3; ++v) {
          if (useLeft)
            pv.sidLeft->setFrequency(v, hz * cachedDetuneL);
          if (useRight)
            pv.sidRight->setFrequency(v, hz * cachedDetuneR);
        }
      }
    }
  } else {
    // Mono/Para: apply pitch bend to all active voices
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active) {
        float detune = (v < 3) ? leftDetuneCents : rightDetuneCents;
        // Add per-voice spread in paraphonic mode
        if (voiceMode == VoiceMode::Paraphonic)
          detune += paraVoiceSpreadCents[v % 3];
        double note = static_cast<double>(voices[v].note) + bendSemitones +
                      (detune / 100.0);
        double hz = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
        SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
        sid.setFrequency(v % 3, hz);
      }
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
  } else if (gateOn && filterEnv.gateWasOn && filterEnvRetriggerFlag) {
    // Multi-trigger: retrigger attack on each new noteOn in para mode
    bool retrig = paraFilterRetrigPtr && paraFilterRetrigPtr->load() > 0.5f;
    if (retrig) {
      filterEnv.stage = FilterEnvelopeState::Stage::Attack;
    }
    filterEnvRetriggerFlag = false;
  }
  filterEnv.gateWasOn = gateOn;

  float dt =
      static_cast<float>(numSamples) / static_cast<float>(hostSampleRate);
  filterEnv.tick(dt, filterEnvAttackPtr->load(), filterEnvDecayPtr->load(),
                 filterEnvSustainPtr->load(), filterEnvReleasePtr->load());
}

void BreadbinProcessor::applyFilterModulation() {
  int modOffset = computeFilterModOffset();

  // Filter envelope (global, for mono/para paths)
  if (filterEnvEnablePtr->load() > 0.5f) {
    float envAmount = filterEnvAmountPtr->load();
    modOffset +=
        static_cast<int>(filterEnv.currentValue * envAmount * 2047.0f);
  }

  // Apply combined modulation
  int leftCutoff = juce::jlimit(0, 2047, baseFilterCutoffLeft + modOffset);
  int rightCutoff =
      juce::jlimit(0, 2047, baseFilterCutoffRight + modOffset);
  lastAppliedCutoffLeft = leftCutoff;
  lastAppliedCutoffRight = rightCutoff;
  sidLeft.setFilterCutoff(leftCutoff);
  sidRight.setFilterCutoff(rightCutoff);
}

int BreadbinProcessor::computeFilterModOffset() const {
  int offset = static_cast<int>(modWheelValue * 1000.0f);
  if (lfo.enabled && lfo.depthFilter > 0.0f)
    offset += static_cast<int>(lfo.currentValue * lfo.depthFilter * 1024.0f);
  if (lfo2.enabled && lfo2.depthFilter > 0.0f)
    offset += static_cast<int>(lfo2.currentValue * lfo2.depthFilter * 1024.0f);
  return offset;
}

void BreadbinProcessor::updatePanCache(float panValue, float &cachedPan,
                                       float &gainL, float &gainR) {
  if (panValue == cachedPan) return;
  cachedPan = panValue;
  constexpr float piOver2 = 1.5707963267948966f;
  float angle = (panValue + 1.0f) * 0.5f * piOver2;
  gainL = std::cos(angle);
  gainR = std::sin(angle);
}

void BreadbinProcessor::setMasterVolume(float vol) {
  masterVolume = juce::jlimit(0.0f, 1.0f, vol);
  // Volume is applied as output gain in processBlock, not via SID register,
  // so that digi playback also responds to the master volume knob.
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


// ==================== POLY VOICE MANAGEMENT ====================

// SID ADSR release times in seconds (indexed 0-15)
static constexpr float kSidReleaseTimes[16] = {
    0.006f,  0.024f, 0.048f,  0.072f, 0.114f, 0.168f, 0.204f, 0.240f,
    0.300f,  0.750f, 1.500f,  2.400f, 3.000f, 9.000f, 15.000f, 24.000f};

int BreadbinProcessor::findFreePolyVoice() const {
  int limit = polyMaxNotes;
  // Prefer truly inactive slots
  for (int i = 0; i < limit; ++i) {
    if (!polyVoices[i].active && !polyVoices[i].releasing)
      return i;
  }
  // Fall back to releasing slots (can be reused)
  for (int i = 0; i < limit; ++i) {
    if (polyVoices[i].releasing)
      return i;
  }
  return -1;
}

void BreadbinProcessor::normalizePolyNoteCounters() {
  // Prevent uint32_t wrap by renumbering when counter gets large.
  // Preserves relative order of all active voices.
  if (polyNoteCounter < 0x80000000u)
    return;
  uint32_t minVal = UINT32_MAX;
  for (int i = 0; i < MAX_POLY; ++i)
    if (polyVoices[i].active || polyVoices[i].releasing)
      minVal = std::min(minVal, polyVoices[i].startSample);
  if (minVal == UINT32_MAX)
    minVal = 0;
  for (int i = 0; i < MAX_POLY; ++i)
    if (polyVoices[i].active || polyVoices[i].releasing)
      polyVoices[i].startSample -= minVal;
  polyNoteCounter -= minVal;
}

int BreadbinProcessor::findStealablePolyVoice() const {
  int limit = polyMaxNotes;
  int oldest = -1;
  uint32_t oldestTime = UINT32_MAX;
  for (int i = 0; i < limit; ++i) {
    if (polyVoices[i].startSample < oldestTime) {
      oldestTime = polyVoices[i].startSample;
      oldest = i;
    }
  }
  return oldest;
}

bool BreadbinProcessor::isEcoPolyBudgetActive() const noexcept {
  return ecoMode == EcoMode::Manual && isPolyActive();
}

BreadbinProcessor::PolySidRenderRole
BreadbinProcessor::chooseNewPolyRenderRoleForSlot(int targetIdx) const noexcept {
  if (!isEcoPolyBudgetActive() || polySidBudget == PolySidBudget::Ultra)
    return PolySidRenderRole::Pair;

  if (polySidBudget == PolySidBudget::MaxEco) {
    int monoCount = 0;
    for (int i = 0; i < polyMaxNotes; ++i)
      if (i != targetIdx && (polyVoices[i].active || polyVoices[i].releasing))
        ++monoCount;
    return (monoCount % 2 == 0) ? PolySidRenderRole::LeftMono
                                : PolySidRenderRole::RightMono;
  }

  bool hasPair = false;
  int monoCount = 0;
  for (int i = 0; i < polyMaxNotes; ++i) {
    if (i == targetIdx)
      continue;
    const auto &pv = polyVoices[i];
    if (!pv.active && !pv.releasing)
      continue;
    if (pv.sidRenderRole == PolySidRenderRole::Pair)
      hasPair = true;
    else
      ++monoCount;
  }
  if (!hasPair || polyStereoAnchor == PolyStereoAnchor::Newest)
    return PolySidRenderRole::Pair;
  return (monoCount % 2 == 0) ? PolySidRenderRole::LeftMono
                              : PolySidRenderRole::RightMono;
}

void BreadbinProcessor::demoteExistingPolyPairForNewestAnchor() noexcept {
  if (!isEcoPolyBudgetActive() || polySidBudget != PolySidBudget::Hybrid ||
      polyStereoAnchor != PolyStereoAnchor::Newest)
    return;

  int monoCount = 0;
  for (int i = 0; i < polyMaxNotes; ++i) {
    auto &pv = polyVoices[i];
    if (!pv.active && !pv.releasing)
      continue;
    if (pv.sidRenderRole == PolySidRenderRole::Pair) {
      pv.sidRenderRole = (monoCount % 2 == 0) ? PolySidRenderRole::LeftMono
                                              : PolySidRenderRole::RightMono;
      ++monoCount;
    } else {
      ++monoCount;
    }
  }
}

void BreadbinProcessor::rebalancePolyRenderRoles() noexcept {
  if (!isEcoPolyBudgetActive() || polySidBudget == PolySidBudget::Ultra) {
    for (int i = 0; i < polyMaxNotes; ++i) {
      auto &pv = polyVoices[i];
      if (pv.active || pv.releasing)
        pv.sidRenderRole = PolySidRenderRole::Pair;
    }
    return;
  }

  int pairIdx = -1;
  if (polySidBudget == PolySidBudget::Hybrid) {
    uint32_t selectedTime =
        (polyStereoAnchor == PolyStereoAnchor::Oldest) ? UINT32_MAX : 0;
    for (int i = 0; i < polyMaxNotes; ++i) {
      const auto &pv = polyVoices[i];
      if (!pv.active && !pv.releasing)
        continue;
      if (pairIdx < 0 ||
          (polyStereoAnchor == PolyStereoAnchor::Oldest &&
           pv.startSample < selectedTime) ||
          (polyStereoAnchor == PolyStereoAnchor::Newest &&
           pv.startSample >= selectedTime)) {
        pairIdx = i;
        selectedTime = pv.startSample;
      }
    }
  }

  int monoCount = 0;
  for (int i = 0; i < polyMaxNotes; ++i) {
    auto &pv = polyVoices[i];
    if (!pv.active && !pv.releasing)
      continue;

    if (i == pairIdx) {
      pv.sidRenderRole = PolySidRenderRole::Pair;
      continue;
    }

    pv.sidRenderRole = (monoCount % 2 == 0) ? PolySidRenderRole::LeftMono
                                            : PolySidRenderRole::RightMono;
    ++monoCount;
  }
}

BreadbinProcessor::PolyRoleSnapshot
BreadbinProcessor::snapshotPolyRenderRoles() const noexcept {
  PolyRoleSnapshot roles{};
  for (int i = 0; i < MAX_POLY; ++i)
    roles[i] = polyVoices[i].sidRenderRole;
  return roles;
}

void BreadbinProcessor::syncPromotedPolyRenderSides(
    const PolyRoleSnapshot &oldRoles) {
  if (!isPolyActive())
    return;

  for (int i = 0; i < polyMaxNotes; ++i) {
    auto &pv = polyVoices[i];
    if (!pv.active || pv.releasing)
      continue;

    const bool gainedLeft = !usesLeft(oldRoles[i]) && usesLeft(pv.sidRenderRole);
    const bool gainedRight =
        !usesRight(oldRoles[i]) && usesRight(pv.sidRenderRole);
    if (!gainedLeft && !gainedRight)
      continue;

    applySettingsToPolyVoice(i);

    if (voiceMode == VoiceMode::PolyPara && pv.paraCount > 0) {
      for (int slot = 0; slot < 3; ++slot) {
        if (pv.paraNote[slot] < 0)
          continue;
        if (gainedLeft && voiceSettings[slot].enabled)
          pv.sidLeft->noteOn(slot, pv.paraNote[slot], pv.paraVelocity[slot],
                             leftDetuneCents);
        if (gainedRight && voiceSettings[slot + 3].enabled)
          pv.sidRight->noteOn(slot, pv.paraNote[slot], pv.paraVelocity[slot],
                              rightDetuneCents);
      }
    } else if (pv.midiNote >= 0) {
      for (int v = 0; v < 3; ++v) {
        if (gainedLeft && voiceSettings[v].enabled)
          pv.sidLeft->noteOn(v, pv.midiNote, pv.velocity, leftDetuneCents);
        if (gainedRight && voiceSettings[v + 3].enabled)
          pv.sidRight->noteOn(v, pv.midiNote, pv.velocity, rightDetuneCents);
      }
    }
  }
}

void BreadbinProcessor::polyNoteOn(int midiNote, int velocity) {
  demoteExistingPolyPairForNewestAnchor();

  int idx = findFreePolyVoice();
  if (idx < 0)
    idx = findStealablePolyVoice();
  if (idx < 0)
    return;

  auto &pv = polyVoices[idx];
  const auto newRole = chooseNewPolyRenderRoleForSlot(idx);

  // If stealing, release the old note first
  if (pv.active || pv.releasing) {
    for (int v = 0; v < 3; ++v) {
      pv.sidLeft->noteOff(v);
      pv.sidRight->noteOff(v);
    }
  }

  pv.midiNote = midiNote;
  pv.velocity = velocity;
  pv.active = true;
  pv.releasing = false;
  pv.fadingOut = false;
  pv.fadeGain = 0.0f; // ramps up per-sample to prevent DC offset pop
  pv.sidRenderRole = newRole;
  pv.sidRenderGate.reset();
  pv.sidRenderTailPeak = 0.0f;
  pv.sidRenderWasSkipping = false;
  pv.startSample = polyNoteCounter++;
  pv.filterEnv = FilterEnvelopeState{};
  const auto oldRoles = snapshotPolyRenderRoles();
  rebalancePolyRenderRoles();
  syncPromotedPolyRenderSides(oldRoles);

  // Calculate frequency
  pv.targetHz = 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
  pv.currentHz = pv.targetHz;
  pv.isGliding = false;

  // Apply current voice settings and filter state to this poly voice
  applySettingsToPolyVoice(idx);

  const bool useLeft = pv.sidRenderRole == PolySidRenderRole::Pair ||
                       pv.sidRenderRole == PolySidRenderRole::LeftMono;
  const bool useRight = pv.sidRenderRole == PolySidRenderRole::Pair ||
                        pv.sidRenderRole == PolySidRenderRole::RightMono;

  // Trigger enabled voices only on the rendered SID side(s).
  if (useLeft) {
    for (int v = 0; v < 3; ++v) {
      if (voiceSettings[v].enabled)
        pv.sidLeft->noteOn(v, midiNote, velocity, leftDetuneCents);
    }
  }
  if (useRight) {
    for (int v = 0; v < 3; ++v) {
      if (voiceSettings[v + 3].enabled)
        pv.sidRight->noteOn(v, midiNote, velocity, rightDetuneCents);
    }
  }
}

void BreadbinProcessor::polyNoteOff(int midiNote) {
  for (int i = 0; i < polyMaxNotes; ++i) {
    auto &pv = polyVoices[i];
    if (pv.active && !pv.releasing && pv.midiNote == midiNote) {
      pv.releasing = true;
      for (int v = 0; v < 3; ++v) {
        pv.sidLeft->noteOff(v);
        pv.sidRight->noteOff(v);
      }
      // Start filter envelope release
      pv.filterEnv.stage = FilterEnvelopeState::Stage::Release;

      // Set release timer based on longest enabled SID ADSR release, capped
      // at 3 seconds to prevent poly voices from hanging with high release values
      int maxRel = 0;
      for (int v = 0; v < 6; ++v)
        if (voiceSettings[v].enabled)
          maxRel = std::max(maxRel, voiceSettings[v].release);
      float releaseSec = std::min(kSidReleaseTimes[maxRel], 3.0f);
      pv.releaseSamplesTotal =
          static_cast<int>(releaseSec * hostSampleRate) + 512;
      pv.releaseSamplesRemaining = pv.releaseSamplesTotal;
      return; // Only release one voice per noteOff
    }
  }
}

void BreadbinProcessor::polyAllNotesOff() {
  for (int i = 0; i < MAX_POLY; ++i) {
    auto &pv = polyVoices[i];
    if (pv.active || pv.releasing) {
      for (int v = 0; v < 3; ++v) {
        pv.sidLeft->noteOff(v);
        pv.sidRight->noteOff(v);
      }
      pv.active = false;
      pv.releasing = false;
      pv.midiNote = -1;
      pv.filterEnv = FilterEnvelopeState{};
      pv.sidRenderRole = PolySidRenderRole::Pair;
      pv.sidRenderGate.reset();
      pv.sidRenderTailPeak = 0.0f;
      pv.sidRenderWasSkipping = false;
    }
  }
}

// ==================== PARAPHONIC VOICE MANAGEMENT ====================

void BreadbinProcessor::redistributeParaVoices() {
  // Voice stacking with detune spread for analog thickness (Korg Mono/Poly).
  // Stacking only activates when spread > 0 — without spread, digital
  // oscillators at the same frequency just drive the SID filter harder
  // with no audible benefit. With spread=0, use simple 1:1 voice mapping.
  //
  // With spread > 0:
  //   1 held: all 3 voices on that note, spread = [-s, 0, +s]
  //   2 held: voices 0,1 on note[0] (spread -s/2, +s/2), voice 2 on note[1]
  //   3 held: 1:1:1, no spread
  //
  // With spread = 0: first-free-slot assignment (1 voice per note)

  float spreadCents = paraSpreadPtr ? paraSpreadPtr->load() : 0.0f;
  bool stackingEnabled = spreadCents > 0.01f;

  // Build target assignment: which note each SID voice should play
  struct VoiceTarget { int midiNote = -1; int velocity = 0; float spread = 0.0f; };
  std::array<VoiceTarget, 3> target{};

  if (paraHeldCount == 0) {
    // All released — nothing to assign
  } else if (!stackingEnabled) {
    // No spread: simple 1:1 mapping (1 voice per held note, rest silent)
    for (int i = 0; i < paraHeldCount && i < 3; ++i) {
      target[i].midiNote = paraHeldNotes[i].midiNote;
      target[i].velocity = paraHeldNotes[i].velocity;
    }
  } else if (paraHeldCount == 1) {
    // Stack all 3 voices on the single note with spread
    for (int v = 0; v < 3; ++v) {
      target[v].midiNote = paraHeldNotes[0].midiNote;
      target[v].velocity = paraHeldNotes[0].velocity;
    }
    target[0].spread = -spreadCents;
    target[1].spread = 0.0f;
    target[2].spread = spreadCents;
  } else if (paraHeldCount == 2) {
    // 2+1: voices 0,1 on note[0] with half spread, voice 2 on note[1]
    target[0] = {paraHeldNotes[0].midiNote, paraHeldNotes[0].velocity, -spreadCents * 0.5f};
    target[1] = {paraHeldNotes[0].midiNote, paraHeldNotes[0].velocity,  spreadCents * 0.5f};
    target[2] = {paraHeldNotes[1].midiNote, paraHeldNotes[1].velocity, 0.0f};
  } else {
    // 1:1:1 — standard paraphonic, no spread
    for (int v = 0; v < 3; ++v) {
      target[v].midiNote = paraHeldNotes[v].midiNote;
      target[v].velocity = paraHeldNotes[v].velocity;
      target[v].spread = 0.0f;
    }
  }

  // Store spread for pitch bend updates
  for (int v = 0; v < 3; ++v)
    paraVoiceSpreadCents[v] = target[v].spread;

  // Apply assignments to SID voices, minimizing gate retriggers
  for (int v = 0; v < 3; ++v) {
    int prevNote = paraVoices[v].midiNote;
    int newNote = target[v].midiNote;

    if (newNote == -1) {
      // Voice should be silent
      if (prevNote != -1) {
        paraVoices[v] = {-1, 0};
        paraVoices[v + 3] = {-1, 0};
        releaseNote(v);
        if (voiceSettings[v + 3].enabled)
          releaseNote(v + 3);
      }
    } else if (prevNote == newNote) {
      // Same note — just update frequency for spread change (no gate retrigger)
      float detune = leftDetuneCents + target[v].spread;
      double note = static_cast<double>(newNote) + (detune / 100.0);
      double hz = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
      sidLeft.setFrequency(v, hz);
      voices[v].currentHz = hz;
      voices[v].targetHz = hz;

      if (voiceSettings[v + 3].enabled) {
        float detuneR = rightDetuneCents + target[v].spread;
        double noteR = static_cast<double>(newNote) + (detuneR / 100.0);
        double hzR = 440.0 * std::pow(2.0, (noteR - 69.0) / 12.0);
        sidRight.setFrequency(v, hzR);
        voices[v + 3].currentHz = hzR;
        voices[v + 3].targetHz = hzR;
      }
      paraVoices[v] = {newNote, target[v].velocity};
      paraVoices[v + 3] = {newNote, target[v].velocity};
    } else {
      // Different note — full gate trigger
      paraVoices[v] = {newNote, target[v].velocity};
      paraVoices[v + 3] = {newNote, target[v].velocity};
      sidLeft.setRingMod(v, false);
      sidLeft.setSync(v, false);
      triggerNote(v, newNote, target[v].velocity);
      if (voiceSettings[v + 3].enabled) {
        sidRight.setRingMod(v, false);
        sidRight.setSync(v, false);
        triggerNote(v + 3, newNote, target[v].velocity);
      }
    }
  }
}

void BreadbinProcessor::paraNoteOn(int midiNote, int velocity) {
  // Check if note already held (ignore duplicates)
  for (int i = 0; i < paraHeldCount; ++i) {
    if (paraHeldNotes[i].midiNote == midiNote)
      return;
  }
  // Pool full
  if (paraHeldCount >= 3)
    return;

  // Add to held notes
  paraHeldNotes[paraHeldCount++] = {midiNote, velocity};

  // Set filter retrigger flag for multi-trigger mode
  filterEnvRetriggerFlag = true;

  // Redistribute voices across held notes
  redistributeParaVoices();
}

void BreadbinProcessor::paraNoteOnSID(bool isLeftSID, int midiNote, int velocity) {
  // PolyPara uses this path — keep original behavior (no stacking)
  int base = isLeftSID ? 0 : 3;
  SIDEngine &sid = isLeftSID ? sidLeft : sidRight;
  for (int v = base; v < base + 3; ++v) {
    if (!voiceSettings[v].enabled) continue;
    if (paraVoices[v].midiNote == -1) {
      paraVoices[v] = {midiNote, velocity};
      sid.setRingMod(v % 3, false);
      sid.setSync(v % 3, false);
      triggerNote(v, midiNote, velocity);
      return;
    }
  }
}

void BreadbinProcessor::paraNoteOff(int midiNote) {
  // Find and remove from held notes
  bool found = false;
  for (int i = 0; i < paraHeldCount; ++i) {
    if (paraHeldNotes[i].midiNote == midiNote) {
      found = true;
      // Shift remaining notes down
      for (int j = i; j < paraHeldCount - 1; ++j)
        paraHeldNotes[j] = paraHeldNotes[j + 1];
      paraHeldNotes[--paraHeldCount] = {-1, 0};
      break;
    }
  }
  if (!found) return;

  // Redistribute remaining voices
  redistributeParaVoices();
}

void BreadbinProcessor::paraNoteOffSID(bool isLeftSID, int midiNote) {
  // PolyPara uses this path — keep original behavior
  int base = isLeftSID ? 0 : 3;
  for (int v = base; v < base + 3; ++v) {
    if (paraVoices[v].midiNote == midiNote) {
      paraVoices[v] = {-1, 0};
      releaseNote(v);
      return;
    }
  }
}

void BreadbinProcessor::paraAllNotesOff() {
  paraHeldCount = 0;
  for (int i = 0; i < 3; ++i) {
    paraHeldNotes[i] = {-1, 0};
    paraVoiceSpreadCents[i] = 0.0f;
  }
  for (int v = 0; v < 6; ++v) {
    paraVoices[v] = {-1, 0};
    if (voices[v].active)
      releaseNote(v);
  }
}

// ==================== POLY+PARA VOICE MANAGEMENT ====================

void BreadbinProcessor::polyParaNoteOn(int midiNote, int velocity) {
  // 1. Check existing active poly voices for a free SID voice slot
  for (int pi = 0; pi < polyMaxNotes; ++pi) {
    auto &pv = polyVoices[pi];
    if (pv.active && !pv.releasing && pv.paraCount < 3) {
      int slot = -1;
      for (int s = 0; s < 3; ++s) {
        if (pv.paraNote[s] == -1) { slot = s; break; }
      }
      if (slot < 0) continue;

      pv.paraNote[slot] = midiNote;
      pv.paraVelocity[slot] = velocity;
      pv.paraCount++;

      // Disable ring mod/sync — para voices play independent notes
      pv.sidLeft->setRingMod(slot, false);
      pv.sidLeft->setSync(slot, false);
      pv.sidRight->setRingMod(slot, false);
      pv.sidRight->setSync(slot, false);
      const bool useLeft = pv.sidRenderRole == PolySidRenderRole::Pair ||
                           pv.sidRenderRole == PolySidRenderRole::LeftMono;
      const bool useRight = pv.sidRenderRole == PolySidRenderRole::Pair ||
                            pv.sidRenderRole == PolySidRenderRole::RightMono;
      // Trigger on this specific SID voice slot
      if (useLeft && voiceSettings[slot].enabled)
        pv.sidLeft->noteOn(slot, midiNote, velocity, leftDetuneCents);
      if (useRight && voiceSettings[slot + 3].enabled)
        pv.sidRight->noteOn(slot, midiNote, velocity, rightDetuneCents);
      return;
    }
  }

  // 2. No free slots — allocate a new poly voice
  demoteExistingPolyPairForNewestAnchor();

  int idx = findFreePolyVoice();
  if (idx < 0)
    idx = findStealablePolyVoice();
  if (idx < 0)
    return;

  auto &pv = polyVoices[idx];
  const auto newRole = chooseNewPolyRenderRoleForSlot(idx);

  // If stealing, release the old note first
  if (pv.active || pv.releasing) {
    for (int v = 0; v < 3; ++v) {
      pv.sidLeft->noteOff(v);
      pv.sidRight->noteOff(v);
    }
  }

  pv.midiNote = midiNote;
  pv.velocity = velocity;
  pv.active = true;
  pv.releasing = false;
  pv.fadingOut = false;
  pv.fadeGain = 0.0f; // ramps up per-sample to prevent DC offset pop
  pv.sidRenderRole = newRole;
  pv.sidRenderGate.reset();
  pv.sidRenderTailPeak = 0.0f;
  pv.sidRenderWasSkipping = false;
  pv.startSample = polyNoteCounter++;
  pv.filterEnv = FilterEnvelopeState{};
  const auto oldRoles = snapshotPolyRenderRoles();
  rebalancePolyRenderRoles();
  syncPromotedPolyRenderSides(oldRoles);
  pv.targetHz = 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
  pv.currentHz = pv.targetHz;
  pv.isGliding = false;

  // Initialize para tracking
  pv.paraNote[0] = midiNote;
  pv.paraVelocity[0] = velocity;
  pv.paraCount = 1;
  for (int i = 1; i < 3; ++i) {
    pv.paraNote[i] = -1;
    pv.paraVelocity[i] = 0;
  }

  applySettingsToPolyVoice(idx);

  const bool useLeft = pv.sidRenderRole == PolySidRenderRole::Pair ||
                       pv.sidRenderRole == PolySidRenderRole::LeftMono;
  const bool useRight = pv.sidRenderRole == PolySidRenderRole::Pair ||
                        pv.sidRenderRole == PolySidRenderRole::RightMono;

  // Trigger only voice 0 (first para slot) on the rendered SID side(s).
  if (useLeft && voiceSettings[0].enabled)
    pv.sidLeft->noteOn(0, midiNote, velocity, leftDetuneCents);
  if (useRight && voiceSettings[3].enabled)
    pv.sidRight->noteOn(0, midiNote, velocity, rightDetuneCents);
}

void BreadbinProcessor::polyParaNoteOff(int midiNote) {
  for (int pi = 0; pi < polyMaxNotes; ++pi) {
    auto &pv = polyVoices[pi];
    if (!pv.active || pv.releasing) continue;

    for (int slot = 0; slot < 3; ++slot) {
      if (pv.paraNote[slot] == midiNote) {
        pv.sidLeft->noteOff(slot);
        pv.sidRight->noteOff(slot);
        pv.paraNote[slot] = -1;
        pv.paraVelocity[slot] = 0;
        pv.paraCount--;

        // If no more para notes, start releasing the whole poly voice
        if (pv.paraCount <= 0) {
          pv.paraCount = 0;
          pv.releasing = true;
          pv.filterEnv.stage = FilterEnvelopeState::Stage::Release;
          int maxRel = 0;
          for (int v = 0; v < 6; ++v)
            if (voiceSettings[v].enabled)
              maxRel = std::max(maxRel, voiceSettings[v].release);
          float releaseSec = std::min(kSidReleaseTimes[maxRel], 3.0f);
          pv.releaseSamplesTotal =
              static_cast<int>(releaseSec * hostSampleRate) + 512;
          pv.releaseSamplesRemaining = pv.releaseSamplesTotal;
        }
        return;
      }
    }
  }
}

void BreadbinProcessor::polyParaAllNotesOff() {
  for (int i = 0; i < MAX_POLY; ++i) {
    auto &pv = polyVoices[i];
    if (pv.active || pv.releasing) {
      for (int v = 0; v < 3; ++v) {
        pv.sidLeft->noteOff(v);
        pv.sidRight->noteOff(v);
        pv.paraNote[v] = -1;
        pv.paraVelocity[v] = 0;
      }
      pv.paraCount = 0;
      pv.active = false;
      pv.releasing = false;
      pv.midiNote = -1;
      pv.filterEnv = FilterEnvelopeState{};
      pv.sidRenderRole = PolySidRenderRole::Pair;
    }
  }
}

void BreadbinProcessor::applySettingsToPolyVoice(int polyIdx) {
  auto &pv = polyVoices[polyIdx];

  // In PolyPara mode, each voice plays an independent note — ring mod
  // and sync cause cross-voice intermodulation noise, so disable them.
  bool isPolyPara = (voiceMode == VoiceMode::PolyPara);

  // Apply L SID voice settings (voices 0-2)
  for (int v = 0; v < 3; ++v) {
    auto &vs = voiceSettings[v];
    pv.sidLeft->setWaveform(v, vs.waveform);
    pv.sidLeft->setPulseWidth(v, vs.pulseWidth);
    pv.sidLeft->setAttack(v, vs.attack);
    pv.sidLeft->setDecay(v, vs.decay);
    pv.sidLeft->setSustain(v, vs.sustain);
    pv.sidLeft->setRelease(v, vs.release);
    pv.sidLeft->setRingMod(v, isPolyPara ? false : vs.ringMod);
    pv.sidLeft->setSync(v, isPolyPara ? false : vs.sync);
  }
  pv.sidLeft->setFilterVoices(voiceSettings[0].filterEnabled,
                               voiceSettings[1].filterEnabled,
                               voiceSettings[2].filterEnabled);

  // Apply R SID voice settings (voices 3-5)
  for (int v = 0; v < 3; ++v) {
    auto &vs = voiceSettings[v + 3];
    pv.sidRight->setWaveform(v, vs.waveform);
    pv.sidRight->setPulseWidth(v, vs.pulseWidth);
    pv.sidRight->setAttack(v, vs.attack);
    pv.sidRight->setDecay(v, vs.decay);
    pv.sidRight->setSustain(v, vs.sustain);
    pv.sidRight->setRelease(v, vs.release);
    pv.sidRight->setRingMod(v, isPolyPara ? false : vs.ringMod);
    pv.sidRight->setSync(v, isPolyPara ? false : vs.sync);
  }
  pv.sidRight->setFilterVoices(voiceSettings[3].filterEnabled,
                                voiceSettings[4].filterEnabled,
                                voiceSettings[5].filterEnabled);

  // Volume always max (output gain applied post-mix)
  pv.sidLeft->setVolume(15);
  pv.sidRight->setVolume(15);

  // Filter mode and base cutoff/resonance
  pv.sidLeft->setFilterMode(filterLPLeft, filterBPLeft, filterHPLeft);
  pv.sidRight->setFilterMode(filterLPRight, filterBPRight, filterHPRight);
  pv.sidLeft->setFilterCutoff(baseFilterCutoffLeft);
  pv.sidRight->setFilterCutoff(baseFilterCutoffRight);
  pv.sidLeft->setFilterResonance(baseFilterResLeft);
  pv.sidRight->setFilterResonance(baseFilterResRight);
}

void BreadbinProcessor::processPolyFilterEnvelope(int polyIdx, int numSamples) {
  auto &pv = polyVoices[polyIdx];
  auto &env = pv.filterEnv;
  bool gateOn = pv.active && !pv.releasing;

  // Gate transitions
  if (gateOn && !env.gateWasOn)
    env.stage = FilterEnvelopeState::Stage::Attack;
  else if (!gateOn && env.gateWasOn)
    env.stage = FilterEnvelopeState::Stage::Release;
  env.gateWasOn = gateOn;

  float dt = static_cast<float>(numSamples) / static_cast<float>(hostSampleRate);
  env.tick(dt, filterEnvAttackPtr->load(), filterEnvDecayPtr->load(),
           filterEnvSustainPtr->load(), filterEnvReleasePtr->load());
}

// ==================== END POLY VOICE MANAGEMENT ====================

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

  // Persist digi sample data
  auto existingDigi = apvts.state.getChildWithName("DigiSampler");
  if (existingDigi.isValid())
    apvts.state.removeChild(existingDigi, nullptr);
  if (digiSampler.isLoaded()) {
    juce::ValueTree digiState("DigiSampler");
    digiState.setProperty("numSamples", digiSampler.getNumSamples(), nullptr);
    digiState.setProperty("sampleRate", digiSampler.getSourceSampleRate(),
                          nullptr);
    digiState.setProperty("filePath",
                          juce::String(digiSampler.getFilePath()), nullptr);
    const auto &packed = digiSampler.getPackedData();
    if (!packed.empty()) {
      juce::MemoryBlock mb(packed.data(), packed.size());
      digiState.setProperty("sampleData", mb.toBase64Encoding(), nullptr);
    }
    const auto &data8 = digiSampler.getData8bit();
    if (!data8.empty()) {
      juce::MemoryBlock mb8(data8.data(), data8.size());
      digiState.setProperty("sampleData8bit", mb8.toBase64Encoding(), nullptr);
    }
    apvts.state.addChild(digiState, -1, nullptr);
  }

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

      // Restore digi sample data
      auto digiState = apvts.state.getChildWithName("DigiSampler");
      if (digiState.isValid()) {
        int numSamp = digiState.getProperty("numSamples", 0);
        double sr = digiState.getProperty("sampleRate", 44100.0);
        juce::String path = digiState.getProperty("filePath", "");
        // Prefer 8-bit data if available, fall back to 4-bit packed
        juce::String b64_8 = digiState.getProperty("sampleData8bit", "");
        if (numSamp > 0 && b64_8.isNotEmpty()) {
          juce::MemoryBlock mb8;
          mb8.fromBase64Encoding(b64_8);
          std::vector<uint8_t> data8(
              static_cast<const uint8_t *>(mb8.getData()),
              static_cast<const uint8_t *>(mb8.getData()) + mb8.getSize());
          digiSampler.setData8bit(data8, numSamp, sr, path.toStdString());
        } else {
          juce::String b64 = digiState.getProperty("sampleData", "");
          if (numSamp > 0 && b64.isNotEmpty()) {
            juce::MemoryBlock mb;
            mb.fromBase64Encoding(b64);
            std::vector<uint8_t> packed(
                static_cast<const uint8_t *>(mb.getData()),
                static_cast<const uint8_t *>(mb.getData()) + mb.getSize());
            digiSampler.setPackedData(packed, numSamp, sr,
                                      path.toStdString());
          }
        }
      }

      // Kill any active voices on state restore
      polyAllNotesOff();
      polyParaAllNotesOff();
      paraAllNotesOff();

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

  // 20Hz 2nd-order Butterworth DC blocker (kills SID DC offset).
  // 20Hz is below audible range but fast enough to track and remove
  // the MOS6581's significant idle DC offset.
  *subsonicFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(
      sampleRate, 20.0f, 0.707f);
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

  // Sort held notes into pre-allocated work array (RT-safe, no stack pressure)
  for (int i = 0; i < arpHeldCount; ++i)
    arpSortBuf[i] = arpHeldNotes[i];
  std::sort(arpSortBuf.begin(), arpSortBuf.begin() + arpHeldCount);

  // Build base sequence with octave expansion
  int baseCount = 0;
  for (int oct = 0; oct < arpOctaves; ++oct) {
    for (int i = 0; i < arpHeldCount; ++i) {
      int transposed = arpSortBuf[i] + (oct * 12);
      if (transposed <= 127 && baseCount < static_cast<int>(arpBaseBuf.size())) {
        arpBaseBuf[baseCount++] = transposed;
      }
    }
  }

  // Apply pattern
  switch (arpPattern) {
  case ArpPattern::Up:
    for (int i = 0; i < baseCount; ++i)
      arpSequence[i] = arpBaseBuf[i];
    arpSeqCount = baseCount;
    break;

  case ArpPattern::Down:
    for (int i = 0; i < baseCount; ++i)
      arpSequence[i] = arpBaseBuf[baseCount - 1 - i];
    arpSeqCount = baseCount;
    break;

  case ArpPattern::UpDown:
    for (int i = 0; i < baseCount; ++i)
      arpSequence[i] = arpBaseBuf[i];
    arpSeqCount = baseCount;
    if (baseCount > 1) {
      for (int i = baseCount - 2; i > 0; --i) {
        if (arpSeqCount < static_cast<int>(arpSequence.size()))
          arpSequence[arpSeqCount++] = arpBaseBuf[i];
      }
    }
    break;

  case ArpPattern::Random:
    for (int i = 0; i < baseCount; ++i)
      arpSequence[i] = arpBaseBuf[i];
    arpSeqCount = baseCount;
    std::shuffle(arpSequence.begin(), arpSequence.begin() + arpSeqCount, arpRng);
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
      switch (voiceMode) {
      case VoiceMode::Mono:
        for (int v = 0; v < 6; ++v)
          if (voices[v].note == lastArpNote) releaseNote(v);
        break;
      case VoiceMode::Paraphonic:
        paraNoteOff(lastArpNote);
        break;
      case VoiceMode::Polyphonic:
        polyNoteOff(lastArpNote);
        break;
      case VoiceMode::PolyPara:
        polyParaNoteOff(lastArpNote);
        break;
      }
    }

    // Trigger new note
    switch (voiceMode) {
    case VoiceMode::Mono:
      for (int v = 0; v < 6; ++v)
        if (voiceSettings[v].enabled) triggerNote(v, note, lastVelocity);
      break;
    case VoiceMode::Paraphonic:
      paraNoteOn(note, lastVelocity);
      break;
    case VoiceMode::Polyphonic:
      polyNoteOn(note, lastVelocity);
      break;
    case VoiceMode::PolyPara:
      polyParaNoteOn(note, lastVelocity);
      break;
    }

    lastArpNote = note;

    // Advance index
    arpIndex = (arpIndex + 1) % arpSeqCount;

    // Reshuffle on wrap for random mode
    if (arpPattern == ArpPattern::Random && arpIndex == 0) {
      std::shuffle(arpSequence.begin(), arpSequence.begin() + arpSeqCount, arpRng);
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

  switch (voiceMode) {
  case VoiceMode::Polyphonic:
    for (int n = 0; n < count; ++n)
      polyNoteOn(notes[n], velocity);
    break;
  case VoiceMode::PolyPara:
    for (int n = 0; n < count; ++n)
      polyParaNoteOn(notes[n], velocity);
    break;
  case VoiceMode::Paraphonic:
  case VoiceMode::Mono:
  default:
    for (int v = 0; v < 6; ++v) {
      if (v < count && voiceSettings[v].enabled)
        triggerNote(v, notes[v], velocity);
      else if (voices[v].active)
        releaseNote(v);
    }
    break;
  }
}

void BreadbinProcessor::releaseChord(bool isLeftSID) {
  switch (voiceMode) {
  case VoiceMode::Polyphonic:
    polyAllNotesOff();
    break;
  case VoiceMode::PolyPara:
    polyParaAllNotesOff();
    break;
  case VoiceMode::Paraphonic:
    paraAllNotesOff();
    break;
  case VoiceMode::Mono:
  default:
    {
      const int base = isLeftSID ? 0 : 3;
      for (int v = base; v < base + 3; ++v)
        if (voices[v].active)
          releaseNote(v);
    }
    break;
  }
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
    bool isPara = (voiceMode == VoiceMode::Paraphonic);
    int pwOffset = static_cast<int>(pwMod * 2048.0f);
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active) {
        SIDEngine &sid = (v < 3) ? sidLeft : sidRight;
        // In para mode, all voices use master voice's PW (timbre inheritance)
        int masterIdx = (v < 3) ? 0 : 3;
        int basePW = isPara ? voiceSettings[masterIdx].pulseWidth
                            : voiceSettings[v].pulseWidth;
        int modPW = std::clamp(basePW + pwOffset, 0, 4095);
        sid.setPulseWidth(v % 3, modPW);
      }
    }
  }

  // Apply pitch mod (semitones)
  if (pitchMod != 0.0f) {
    float semitoneMod = pitchMod * 2.0f; // ±2 semitones at full depth
    double pitchMultiplier = std::pow(2.0, semitoneMod / 12.0);
    for (int v = 0; v < 6; ++v) {
      if (voices[v].active) {
        double baseHz =
            voices[v].isGliding ? voices[v].currentHz : voices[v].targetHz;
        double modHz = baseHz * pitchMultiplier;
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

  while (wavetable.timer >= samplesPerStep) {
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

void BreadbinProcessor::tickLFO(LFOState &s, std::mt19937 &rng,
                                int numSamples) {
  double phaseInc =
      (static_cast<double>(s.rate) * numSamples) / hostSampleRate;
  double oldPhase = s.phase;
  s.phase += phaseInc;
  s.phase -= std::floor(s.phase);

  float p = static_cast<float>(s.phase);
  switch (s.waveform) {
  case LFOWaveform::Triangle:
    s.currentValue = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    break;
  case LFOWaveform::Sawtooth:
    s.currentValue = 2.0f * p - 1.0f;
    break;
  case LFOWaveform::Square:
    s.currentValue = (p < 0.5f) ? 1.0f : -1.0f;
    break;
  case LFOWaveform::SampleAndHold:
    if (s.phase < oldPhase) {
      std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
      s.shValue = dist(rng);
    }
    s.currentValue = s.shValue;
    break;
  case LFOWaveform::Sine:
    s.currentValue = std::sin(p * juce::MathConstants<float>::twoPi);
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
    bool isPara = (voiceMode == VoiceMode::Paraphonic);
    for (int v = 0; v < 6; ++v) {
      if (!voices[v].active)
        continue;
      // In para mode, all voices use master voice's PW (timbre inheritance)
      int masterIdx = (v < 3) ? 0 : 3;
      int basePW = wtActive ? wtStep.pulseWidth
                   : (isPara ? voiceSettings[masterIdx].pulseWidth
                             : voiceSettings[v].pulseWidth);
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
    double pitchMultiplier = std::pow(2.0, semitoneMod / 12.0);
    for (int v = 0; v < 6; ++v) {
      if (!voices[v].active)
        continue;
      // Use currentHz which includes sync/ring-mod offset from triggerNote.
      // targetHz is always the base note without offset.
      double baseHz = voices[v].currentHz;
      double modHz = baseHz * pitchMultiplier;
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
  case ControlParam::ReverbDecay: {
    auto *p = apvts.getParameter("reverbDecay");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::ReverbDamping: {
    auto *p = apvts.getParameter("reverbDamping");
    if (p)
      p->setValueNotifyingHost(normalized);
    break;
  }
  case ControlParam::ReverbMix: {
    auto *p = apvts.getParameter("reverbMix");
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
  case ControlParam::ReverbEnable: {
    auto *p = apvts.getParameter("reverbEnable");
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
  case ControlParam::DigiEnable: {
    auto *p = apvts.getParameter("digiEnable");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::DigiLoop: {
    auto *p = apvts.getParameter("digiLoop");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::DigiBitDepth: {
    auto *p = apvts.getParameter("digiBitDepth");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
    break;
  }
  case ControlParam::ParaSpread: {
    auto *p = apvts.getParameter("paraSpread");
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(value / 127.0f * 50.0f));
    break;
  }
  case ControlParam::ParaFilterRetrig: {
    auto *p = apvts.getParameter("paraFilterRetrig");
    if (p)
      p->setValueNotifyingHost(value >= 64 ? 1.0f : 0.0f);
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
  case ControlParam::ReverbDecay:
    return "Reverb Decay";
  case ControlParam::ReverbDamping:
    return "Reverb Damping";
  case ControlParam::ReverbMix:
    return "Reverb Mix";
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
  case ControlParam::ReverbEnable:
    return "Reverb Enable";
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
  case ControlParam::DigiEnable:
    return "Digi Enable";
  case ControlParam::DigiRootNote:
    return "Digi Root Note";
  case ControlParam::DigiLoop:
    return "Digi Loop";
  case ControlParam::DigiBitDepth:
    return "Digi Bit Depth";
  case ControlParam::VoiceMode:
    return "Voice Mode";
  case ControlParam::PolyMaxNotes:
    return "Poly Max Notes";
  case ControlParam::ParaSpread:
    return "Para Spread";
  case ControlParam::ParaFilterRetrig:
    return "Para Filter Retrig";
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
      0.1f, 0.02f));
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
      juce::ParameterID{"chipLeft", 3}, "Left Chip Model",
      juce::StringArray{"MOS 6581", "MOS 6581 R2", "MOS 6581 R3", "MOS 6581 R4",
                         "MOS 8580", "MOS 8580 R5", "CSG 9580", "MOS 8580D"},
      0));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"chipRight", 3}, "Right Chip Model",
      juce::StringArray{"MOS 6581", "MOS 6581 R2", "MOS 6581 R3", "MOS 6581 R4",
                         "MOS 8580", "MOS 8580 R5", "CSG 9580", "MOS 8580D"},
      4));
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

  // Digi Sampler
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"digiEnable", 1}, "Digi Enable", false));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"digiRootNote", 1}, "Digi Root Note", 24, 96, 60));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"digiLoop", 1}, "Digi Loop", false));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"digiBitDepth", 1}, "Digi Bit Depth",
      juce::StringArray{"4-bit", "8-bit"}, 0));

  // Voice Mode + Polyphony
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"voiceMode", 1}, "Voice Mode",
      juce::StringArray{"Mono", "Para", "Poly", "Poly+Para"}, 0));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"polyMaxNotes", 1}, "Poly Max Notes", 1, 8, 4));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"ecoMode", 1}, "ECO Mode",
      juce::StringArray{"Off", "Manual"}, 0));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"polySidBudget", 1}, "Poly SID Budget",
      juce::StringArray{"Hybrid", "Ultra", "Max ECO"}, 0));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"polyStereoAnchor", 1}, "Poly Stereo Anchor",
      juce::StringArray{"Oldest", "Newest"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"paraSpread", 1}, "Para Spread", 0.0f, 50.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"paraFilterRetrig", 1}, "Para Filter Retrigger", true));

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
  // S&H=3, Sine=4. Adding Sine at the end is backwards-compatible.
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"lfoWave", 1}, "LFO Waveform",
      juce::StringArray{"Triangle", "Sawtooth", "Square", "S&H", "Sine"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfoRate", 2}, "LFO Rate", 0.1f, 10.0f, 2.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfoDepthFilt", 1}, "LFO Filter Depth", 0.0f, 1.0f,
      0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfoDepthPW", 1}, "LFO PW Depth", 0.0f, 1.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfoDepthPitch", 1}, "LFO Pitch Depth", 0.0f, 1.0f,
      0.0f));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"lfoSync", 1}, "LFO Sync", false));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"lfoSyncDiv", 1}, "LFO Sync Division",
      juce::StringArray{"4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/4D","1/8D","1/4T","1/8T"}, 4));

  // LFO2
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"lfo2Enable", 1}, "LFO2 Enable", false));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"lfo2Wave", 1}, "LFO2 Waveform",
      juce::StringArray{"Triangle", "Sawtooth", "Square", "S&H", "Sine"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfo2Rate", 2}, "LFO2 Rate", 0.1f, 10.0f, 3.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfo2DepthFilt", 1}, "LFO2 Filter Depth", 0.0f, 1.0f,
      0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfo2DepthPW", 1}, "LFO2 PW Depth", 0.0f, 1.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"lfo2DepthPitch", 1}, "LFO2 Pitch Depth", 0.0f, 1.0f,
      0.0f));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"lfo2Sync", 1}, "LFO2 Sync", false));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"lfo2SyncDiv", 1}, "LFO2 Sync Division",
      juce::StringArray{"4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/4D","1/8D","1/4T","1/8T"}, 4));

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

  // FX: Reverb
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"reverbEnable", 1}, "Reverb Enable", false));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"reverbDecay", 1}, "Reverb Decay", 0.1f, 0.95f,
      0.7f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"reverbDamping", 1}, "Reverb Damping", 1000.0f,
      16000.0f, 10000.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"reverbMix", 1}, "Reverb Mix", 0.0f, 1.0f, 0.3f));

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
  leftDetunePtr = apvts.getRawParameterValue("leftDetune");
  rightDetunePtr = apvts.getRawParameterValue("rightDetune");
  glidePtr = apvts.getRawParameterValue("glide");
  clockModePtr = apvts.getRawParameterValue("clockMode");
  extInputEnablePtr = apvts.getRawParameterValue("extInputEnable");
  extInputLevelPtr = apvts.getRawParameterValue("extInputLevel");
  digiEnablePtr = apvts.getRawParameterValue("digiEnable");
  digiRootNotePtr = apvts.getRawParameterValue("digiRootNote");
  digiLoopPtr = apvts.getRawParameterValue("digiLoop");
  digiBitDepthPtr = apvts.getRawParameterValue("digiBitDepth");
  voiceModePtr = apvts.getRawParameterValue("voiceMode");
  polyMaxNotesPtr = apvts.getRawParameterValue("polyMaxNotes");
  ecoModePtr = apvts.getRawParameterValue("ecoMode");
  polySidBudgetPtr = apvts.getRawParameterValue("polySidBudget");
  polyStereoAnchorPtr = apvts.getRawParameterValue("polyStereoAnchor");
  paraSpreadPtr = apvts.getRawParameterValue("paraSpread");
  paraFilterRetrigPtr = apvts.getRawParameterValue("paraFilterRetrig");
  leftPanPtr = apvts.getRawParameterValue("leftPan");
  rightPanPtr = apvts.getRawParameterValue("rightPan");
  pitchBendRangePtr = apvts.getRawParameterValue("pitchBendRange");

  lfoEnablePtr = apvts.getRawParameterValue("lfoEnable");
  lfoWavePtr = apvts.getRawParameterValue("lfoWave");
  lfoRatePtr = apvts.getRawParameterValue("lfoRate");
  lfoDepthFiltPtr = apvts.getRawParameterValue("lfoDepthFilt");
  lfoDepthPWPtr = apvts.getRawParameterValue("lfoDepthPW");
  lfoDepthPitchPtr = apvts.getRawParameterValue("lfoDepthPitch");
  lfoSyncPtr = apvts.getRawParameterValue("lfoSync");
  lfoSyncDivPtr = apvts.getRawParameterValue("lfoSyncDiv");

  lfo2EnablePtr = apvts.getRawParameterValue("lfo2Enable");
  lfo2WavePtr = apvts.getRawParameterValue("lfo2Wave");
  lfo2RatePtr = apvts.getRawParameterValue("lfo2Rate");
  lfo2DepthFiltPtr = apvts.getRawParameterValue("lfo2DepthFilt");
  lfo2DepthPWPtr = apvts.getRawParameterValue("lfo2DepthPW");
  lfo2DepthPitchPtr = apvts.getRawParameterValue("lfo2DepthPitch");
  lfo2SyncPtr = apvts.getRawParameterValue("lfo2Sync");
  lfo2SyncDivPtr = apvts.getRawParameterValue("lfo2SyncDiv");

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
  reverbEnablePtr = apvts.getRawParameterValue("reverbEnable");
  reverbDecayPtr = apvts.getRawParameterValue("reverbDecay");
  reverbDampingPtr = apvts.getRawParameterValue("reverbDamping");
  reverbMixPtr = apvts.getRawParameterValue("reverbMix");

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
  // Not an APVTS param — set via processor fields + SID engines directly
  int cutoff = (snapshot.regs[0x15] & 0x07) | (snapshot.regs[0x16] << 3);
  setBaseFilterCutoff(true, cutoff);
  setBaseFilterCutoff(false, cutoff);
  sidLeft.setFilterCutoff(cutoff);
  sidRight.setFilterCutoff(cutoff);

  // Filter resonance (reg 0x17 hi nibble, 0-15)
  int resonance = (snapshot.regs[0x17] >> 4) & 0x0F;
  setBaseFilterResonance(true, resonance);
  setBaseFilterResonance(false, resonance);
  sidLeft.setFilterResonance(resonance);
  sidRight.setFilterResonance(resonance);

  // Filter mode (reg 0x18 bits 4-6)
  uint8_t modeReg = snapshot.regs[0x18];
  bool lp = (modeReg & 0x10) != 0;
  bool bp = (modeReg & 0x20) != 0;
  bool hp = (modeReg & 0x40) != 0;
  sidLeft.setFilterMode(lp, bp, hp);
  sidRight.setFilterMode(lp, bp, hp);
  cacheFilterMode(lp, bp, hp, lp, bp, hp);

  // Master volume (reg 0x18 lo nibble, 0-15 -> 0.0-1.0)
  int sidVol = modeReg & 0x0F;
  setParam("masterVol", static_cast<float>(sidVol) / 15.0f);
}
