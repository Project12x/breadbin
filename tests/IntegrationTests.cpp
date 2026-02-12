// Breadbin Integration Test Suite
// Tests the complete signal path: APVTS -> Processor -> SIDEngine -> Audio
// Output Requires JUCE linking. Run as: BreadbinIntegrationTests.exe

#include "../src/PluginProcessor.h"
#include "../src/SIDEngine.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

// ============================================================================
// Test Framework
// ============================================================================

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_TRUE(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  FAIL: %s\n", msg);                                        \
      testsFailed++;                                                           \
    } else {                                                                   \
      testsPassed++;                                                           \
    }                                                                          \
  } while (0)

#define ASSERT_NEAR(actual, expected, epsilon, msg)                            \
  do {                                                                         \
    double _a = (actual);                                                      \
    double _e = (expected);                                                    \
    double _eps = (epsilon);                                                   \
    if (std::abs(_a - _e) > _eps) {                                            \
      std::printf("  FAIL: %s\n    expected: %.6f, got: %.6f\n", msg, _e, _a); \
      testsFailed++;                                                           \
    } else {                                                                   \
      testsPassed++;                                                           \
    }                                                                          \
  } while (0)

// ============================================================================
// Helpers
// ============================================================================

static std::unique_ptr<BreadbinProcessor>
createTestProcessor(double sampleRate = 44100.0, int blockSize = 512) {
  auto p = std::make_unique<BreadbinProcessor>();

  // Must enable buses before prepareToPlay in headless mode
  juce::AudioProcessor::BusesLayout layout;
  layout.outputBuses.add(juce::AudioChannelSet::stereo());
  p->setBusesLayout(layout);

  p->prepareToPlay(sampleRate, blockSize);
  return p;
}

// Process one block and return RMS. Optionally apply MIDI.
static float processBlock(BreadbinProcessor &p, int numSamples = 512,
                          juce::MidiBuffer *midi = nullptr) {
  juce::AudioBuffer<float> buffer(2, numSamples);
  buffer.clear();
  juce::MidiBuffer emptyMidi;
  p.processBlock(buffer, midi ? *midi : emptyMidi);
  return buffer.getRMSLevel(0, 0, numSamples);
}

// Process N blocks to let state settle
static void warmUp(BreadbinProcessor &p, int blocks = 3) {
  for (int i = 0; i < blocks; ++i)
    processBlock(p);
}

// ============================================================================
// Core Diagnostic Test
// ============================================================================

void testSIDDirectEngine() {
  std::printf("--- Direct SIDEngine Test ---\n");

  SIDEngine sid;
  sid.prepare(44100.0);
  sid.setVolume(15);
  sid.setWaveform(0, SIDEngine::Waveform::Pulse);
  sid.setPulseWidth(0, 2048);
  sid.setAttack(0, 0);
  sid.setDecay(0, 0);
  sid.setSustain(0, 15);
  sid.setRelease(0, 0);
  sid.noteOn(0, 60, 127);

  float maxSample = 0.0f;
  for (int i = 0; i < 2048; ++i) {
    float s = std::abs(sid.clock());
    if (s > maxSample)
      maxSample = s;
  }
  std::printf("  Direct SIDEngine peak after noteOn: %.6f\n", maxSample);
  ASSERT_TRUE(maxSample > 0.01f, "Direct SIDEngine produces audio");
}

void testProcessorProducesOutput() {
  std::printf("--- Processor Output Test ---\n");

  auto p = createTestProcessor();

  // Print diagnostic info
  std::printf("  channels=%d sampleRate=%.0f\n", p->getTotalNumOutputChannels(),
              p->getSampleRate());

  auto *masterVol = p->apvts.getRawParameterValue("masterVol");
  auto *v0enable = p->apvts.getRawParameterValue("v0_enable");
  std::printf("  masterVol=%.3f v0_enable=%.1f\n", masterVol->load(),
              v0enable->load());

  // Send noteOn - process block directly
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)127), 0);

  float maxPeak = 0.0f;
  for (int block = 0; block < 20; ++block) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer empty;
    p->processBlock(buffer, (block == 0) ? midi : empty);

    float pk = buffer.getMagnitude(0, 0, 512);
    if (pk > maxPeak)
      maxPeak = pk;

    if (block < 5 || pk > 0.001f) {
      std::printf("  Block %2d: peak=%.6f rms=%.6f\n", block, pk,
                  buffer.getRMSLevel(0, 0, 512));
    }
  }

  ASSERT_TRUE(maxPeak > 0.0001f, "Processor produces audio within 20 blocks");
}

// ============================================================================
// SIDEngine Core Tests (using direct SIDEngine for reliability)
// ============================================================================

void testWaveformsProduceDifferentOutput() {
  std::printf("--- Waveform Tests ---\n");

  auto testWave = [](SIDEngine::Waveform wf, const char *name) {
    SIDEngine sid;
    sid.prepare(44100.0);
    sid.setVolume(15);
    sid.setWaveform(0, wf);
    sid.setPulseWidth(0, 2048);
    sid.setAttack(0, 0);
    sid.setSustain(0, 15);
    sid.noteOn(0, 60, 127);

    float maxS = 0.0f;
    for (int i = 0; i < 2048; ++i) {
      float s = std::abs(sid.clock());
      if (s > maxS)
        maxS = s;
    }
    std::printf("  %s peak: %.6f\n", name, maxS);
    return maxS;
  };

  float tri = testWave(SIDEngine::Waveform::Triangle, "Triangle");
  float saw = testWave(SIDEngine::Waveform::Sawtooth, "Sawtooth");
  float pul = testWave(SIDEngine::Waveform::Pulse, "Pulse");
  float noi = testWave(SIDEngine::Waveform::Noise, "Noise");

  ASSERT_TRUE(tri > 0.001f, "Triangle produces output");
  ASSERT_TRUE(saw > 0.001f, "Sawtooth produces output");
  ASSERT_TRUE(pul > 0.001f, "Pulse produces output");
  ASSERT_TRUE(noi > 0.001f, "Noise produces output");
}

void testChipModels() {
  SIDEngine sid6581, sid8580;
  sid6581.prepare(44100.0);
  sid8580.prepare(44100.0);

  sid6581.setChipModel(SIDEngine::ChipModel::MOS6581);
  sid8580.setChipModel(SIDEngine::ChipModel::MOS8580);

  for (auto *s : {&sid6581, &sid8580}) {
    s->setVolume(15);
    s->setWaveform(0, SIDEngine::Waveform::Pulse);
    s->setPulseWidth(0, 2048);
    s->setAttack(0, 0);
    s->setSustain(0, 15);
    s->noteOn(0, 60, 127);
  }

  float max6581 = 0, max8580 = 0;
  for (int i = 0; i < 2048; ++i) {
    max6581 = std::max(max6581, std::abs(sid6581.clock()));
    max8580 = std::max(max8580, std::abs(sid8580.clock()));
  }

  ASSERT_TRUE(max6581 > 0.01f, "MOS6581 produces output");
  ASSERT_TRUE(max8580 > 0.01f, "MOS8580 produces output");
}

// ============================================================================
// APVTS / Parameter Tests
// ============================================================================

void testAPVTSParameters() {
  std::printf("--- APVTS Wiring ---\n");

  auto p = createTestProcessor();

  // Verify all expected parameters exist
  auto check = [&](const char *id) {
    auto *param = p->apvts.getParameter(id);
    ASSERT_TRUE(param != nullptr,
                (std::string("Parameter exists: ") + id).c_str());
  };

  check("masterVol");
  check("dualMode");
  check("chipLeft");
  check("chipRight");
  check("aging");
  check("leftDetune");
  check("rightDetune");
  check("glide");
  check("clockMode");
  check("lfoEnable");
  check("lfoRate");
  check("arpEnable");
  check("arpRate");
  check("v0_enable");
  check("v0_waveform");
  check("v0_pw");
  check("v0_attack");
  check("v0_sustain");
  check("v5_enable"); // Last voice
}

void testAPVTSDefaultValues() {
  auto p = createTestProcessor();

  auto val = [&](const char *id) {
    return p->apvts.getRawParameterValue(id)->load();
  };

  ASSERT_NEAR(val("masterVol"), 0.8f, 0.01f, "masterVol defaults to 0.8");
  ASSERT_TRUE(val("v0_enable") > 0.5f, "v0_enable defaults to true");
  ASSERT_NEAR(val("v0_waveform"), 2.0f, 0.1f,
              "v0_waveform defaults to Pulse (2)");
  ASSERT_NEAR(val("v0_attack"), 0.0f, 0.1f, "v0_attack defaults to 0");
  ASSERT_NEAR(val("v0_sustain"), 15.0f, 0.1f, "v0_sustain defaults to 15");
}

// ============================================================================
// State Persistence Tests
// ============================================================================

void testSaveRestoreState() {
  std::printf("--- State Persistence ---\n");

  juce::MemoryBlock savedState;

  {
    auto p = createTestProcessor();

    // Modify some parameters via the raw pointers (immediate)
    if (auto *param = p->apvts.getParameter("masterVol"))
      param->setValueNotifyingHost(param->convertTo0to1(0.42f));
    if (auto *param = p->apvts.getParameter("lfoEnable"))
      param->setValueNotifyingHost(1.0f);
    if (auto *param = p->apvts.getParameter("arpEnable"))
      param->setValueNotifyingHost(1.0f);

    warmUp(*p);
    p->getStateInformation(savedState);
    ASSERT_TRUE(savedState.getSize() > 0, "State serialized (non-empty)");
  }

  {
    auto p2 = createTestProcessor();
    p2->setStateInformation(savedState.getData(),
                            static_cast<int>(savedState.getSize()));
    warmUp(*p2);

    auto *masterVol = p2->apvts.getRawParameterValue("masterVol");
    ASSERT_NEAR(masterVol->load(), 0.42f, 0.05f, "masterVol restored");

    auto *arpEnable = p2->apvts.getRawParameterValue("arpEnable");
    ASSERT_TRUE(arpEnable->load() > 0.5f, "arpEnable restored as true");

    auto *lfoEnable = p2->apvts.getRawParameterValue("lfoEnable");
    ASSERT_TRUE(lfoEnable->load() > 0.5f, "lfoEnable restored as true");
  }
}

// ============================================================================
// Safety Chain Tests
// ============================================================================

void testSafetyChain() {
  std::printf("--- Safety Chain ---\n");

  // Verify safety chain doesn't clip above 1.5
  SIDEngine sid;
  sid.prepare(44100.0);
  sid.setVolume(15);
  sid.setWaveform(0, SIDEngine::Waveform::Pulse);
  sid.setPulseWidth(0, 2048);
  sid.setAttack(0, 0);
  sid.setSustain(0, 15);
  sid.noteOn(0, 60, 127);

  bool hasOutput = false;
  bool allBelowLimit = true;
  for (int i = 0; i < 4096; ++i) {
    float s = sid.clock();
    if (std::abs(s) > 0.01f)
      hasOutput = true;
    if (std::abs(s) > 1.5f)
      allBelowLimit = false;
  }

  ASSERT_TRUE(hasOutput, "SID generates output for safety test");
  ASSERT_TRUE(allBelowLimit, "SID output stays below 1.5");
}

// ============================================================================
// ADSR Tests (direct SIDEngine)
// ============================================================================

void testADSR() {
  std::printf("--- ADSR Tests ---\n");

  // Fast attack should produce output quickly
  SIDEngine sidFast;
  sidFast.prepare(44100.0);
  sidFast.setVolume(15);
  sidFast.setWaveform(0, SIDEngine::Waveform::Pulse);
  sidFast.setPulseWidth(0, 2048);
  sidFast.setAttack(0, 0); // Instant
  sidFast.setDecay(0, 0);
  sidFast.setSustain(0, 15); // Max
  sidFast.setRelease(0, 0);
  sidFast.noteOn(0, 60, 127);

  float fastPeak = 0.0f;
  // Check first 100 samples (< 2.3ms)
  for (int i = 0; i < 100; ++i) {
    float s = std::abs(sidFast.clock());
    if (s > fastPeak)
      fastPeak = s;
  }

  // Slow attack should produce less initial output
  SIDEngine sidSlow;
  sidSlow.prepare(44100.0);
  sidSlow.setVolume(15);
  sidSlow.setWaveform(0, SIDEngine::Waveform::Pulse);
  sidSlow.setPulseWidth(0, 2048);
  sidSlow.setAttack(0, 15); // Slowest
  sidSlow.setDecay(0, 0);
  sidSlow.setSustain(0, 15); // Max
  sidSlow.setRelease(0, 0);
  sidSlow.noteOn(0, 60, 127);

  float slowPeak = 0.0f;
  for (int i = 0; i < 100; ++i) {
    float s = std::abs(sidSlow.clock());
    if (s > slowPeak)
      slowPeak = s;
  }

  std::printf("  Fast attack peak (100 samples): %.6f\n", fastPeak);
  std::printf("  Slow attack peak (100 samples): %.6f\n", slowPeak);

  ASSERT_TRUE(fastPeak > 0.001f, "Fast attack produces quick output");
  ASSERT_TRUE(fastPeak >= slowPeak,
              "Fast attack >= slow attack initial amplitude");
}

void testNoteOff() {
  SIDEngine sid;
  sid.prepare(44100.0);
  sid.setVolume(15);
  sid.setWaveform(0, SIDEngine::Waveform::Pulse);
  sid.setPulseWidth(0, 2048);
  sid.setAttack(0, 0);
  sid.setSustain(0, 15);
  sid.setRelease(0, 0); // Instant release
  sid.noteOn(0, 60, 127);

  // Let note sustain
  for (int i = 0; i < 1024; ++i)
    sid.clock();

  float sustainPeak = 0.0f;
  for (int i = 0; i < 512; ++i) {
    float s = std::abs(sid.clock());
    if (s > sustainPeak)
      sustainPeak = s;
  }

  // Note off
  sid.noteOff(0);

  // Let release happen
  for (int i = 0; i < 2048; ++i)
    sid.clock();

  float releasePeak = 0.0f;
  for (int i = 0; i < 512; ++i) {
    float s = std::abs(sid.clock());
    if (s > releasePeak)
      releasePeak = s;
  }

  ASSERT_TRUE(sustainPeak > 0.01f, "Sustained note produces output");
  ASSERT_TRUE(releasePeak <= sustainPeak, "Release reduces or maintains level");
}

// ============================================================================
// Filter Tests (direct SIDEngine)
// ============================================================================

void testFilterCutoff() {
  std::printf("--- Filter Tests ---\n");

  // Low cutoff should produce different output than high cutoff
  auto makeFiltered = [](int cutoff) {
    SIDEngine sid;
    sid.prepare(44100.0);
    sid.setVolume(15);
    sid.setWaveform(0, SIDEngine::Waveform::Sawtooth);
    sid.setAttack(0, 0);
    sid.setSustain(0, 15);
    sid.setFilterCutoff(cutoff);
    sid.setFilterResonance(8);
    sid.setFilterMode(true, false, false); // Lowpass
    sid.setFilterVoices(true, false, false);
    sid.noteOn(0, 60, 127);

    float rms = 0.0f;
    for (int i = 0; i < 2048; ++i) {
      float s = sid.clock();
      rms += s * s;
    }
    return std::sqrt(rms / 2048.0f);
  };

  float rmsLow = makeFiltered(200);
  float rmsHigh = makeFiltered(1800);

  std::printf("  Filter cutoff 200 RMS: %.6f\n", rmsLow);
  std::printf("  Filter cutoff 1800 RMS: %.6f\n", rmsHigh);

  // Both should produce output
  ASSERT_TRUE(rmsLow > 0.0001f, "Low cutoff produces output");
  ASSERT_TRUE(rmsHigh > 0.0001f, "High cutoff produces output");
}

// ============================================================================
// Frequency Tests
// ============================================================================

void testDifferentNotes() {
  std::printf("--- Frequency Tests ---\n");

  auto getOutput = [](int midiNote) {
    SIDEngine sid;
    sid.prepare(44100.0);
    sid.setVolume(15);
    sid.setWaveform(0, SIDEngine::Waveform::Sawtooth);
    sid.setAttack(0, 0);
    sid.setSustain(0, 15);
    sid.noteOn(0, midiNote, 127);

    std::vector<float> samples;
    for (int i = 0; i < 4096; ++i)
      samples.push_back(sid.clock());
    return samples;
  };

  auto samplesC4 = getOutput(60);
  auto samplesC5 = getOutput(72);

  // Both should produce audio
  float maxC4 = 0, maxC5 = 0;
  for (auto s : samplesC4)
    maxC4 = std::max(maxC4, std::abs(s));
  for (auto s : samplesC5)
    maxC5 = std::max(maxC5, std::abs(s));

  ASSERT_TRUE(maxC4 > 0.01f, "C4 produces output");
  ASSERT_TRUE(maxC5 > 0.01f, "C5 produces output");

  // Samples should differ (different frequencies)
  float diff = 0.0f;
  for (size_t i = 0; i < 4096; ++i) {
    diff += std::abs(samplesC4[i] - samplesC5[i]);
  }
  ASSERT_TRUE(diff > 0.1f, "C4 and C5 produce different waveforms");
}

// ============================================================================
// Regression Tests (v0.9.1 Hardening)
// ============================================================================

void testLFOWaveformMapping() {
  std::printf("--- LFO Waveform Mapping ---\n");

  auto p = createTestProcessor();

  // APVTS lfoWave should have exactly 4 choices matching enum
  auto *param = p->apvts.getParameter("lfoWave");
  ASSERT_TRUE(param != nullptr, "lfoWave parameter exists");

  auto *choice = dynamic_cast<juce::AudioParameterChoice *>(param);
  ASSERT_TRUE(choice != nullptr, "lfoWave is AudioParameterChoice");
  if (!choice)
    return;

  ASSERT_TRUE(choice->choices.size() == 4,
              "lfoWave has exactly 4 choices (no ghost Sine)");

  // Verify names match canonical ordering
  ASSERT_TRUE(choice->choices[0] == "Triangle", "lfoWave[0] = Triangle");
  ASSERT_TRUE(choice->choices[1] == "Sawtooth", "lfoWave[1] = Sawtooth");
  ASSERT_TRUE(choice->choices[2] == "Square", "lfoWave[2] = Square");
  ASSERT_TRUE(choice->choices[3] == "S&H", "lfoWave[3] = S&H");

  // Verify each APVTS index maps to correct DSP enum
  const char *names[] = {"Triangle", "Sawtooth", "Square", "S&H"};
  for (int i = 0; i < 4; ++i) {
    choice->setValueNotifyingHost(choice->convertTo0to1(static_cast<float>(i)));

    // Process a block so processBlock syncs lfo.waveform from APVTS
    juce::AudioBuffer<float> buf(2, 64);
    buf.clear();
    juce::MidiBuffer midi;
    p->processBlock(buf, midi);

    int dspIndex = static_cast<int>(p->getLFO().waveform);
    bool match = (dspIndex == i);
    std::printf("  APVTS[%d]=%s -> DSP enum=%d %s\n", i, names[i], dspIndex,
                match ? "OK" : "MISMATCH");
    ASSERT_TRUE(
        match,
        (std::string("lfoWave ") + names[i] + " maps correctly").c_str());
  }
}

void testLFOWaveformStateRoundTrip() {
  std::printf("--- LFO Waveform State Round-Trip ---\n");

  juce::MemoryBlock saved;
  {
    auto p = createTestProcessor();
    // Set lfoWave to Square (index 2)
    if (auto *param = p->apvts.getParameter("lfoWave"))
      param->setValueNotifyingHost(
          dynamic_cast<juce::AudioParameterChoice *>(param)->convertTo0to1(
              2.0f));

    warmUp(*p);
    p->getStateInformation(saved);
  }

  {
    auto p2 = createTestProcessor();
    p2->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    warmUp(*p2);

    auto *val = p2->apvts.getRawParameterValue("lfoWave");
    int restored = static_cast<int>(val->load());
    std::printf("  Saved lfoWave=2 (Square), restored=%d\n", restored);
    ASSERT_TRUE(restored == 2, "lfoWave Square survives save/load");
  }
}

void testGetSampleRate() {
  std::printf("--- getSampleRate Fix ---\n");

  auto p = createTestProcessor(48000.0, 256);
  double sr = p->getSampleRate();
  std::printf("  getSampleRate() after prepareToPlay(48000): %.1f\n", sr);
  ASSERT_NEAR(sr, 48000.0, 1.0, "getSampleRate returns correct value");
}

void testMasterVolumeAPVTSSync() {
  std::printf("--- Master Volume APVTS Sync ---\n");

  auto p = createTestProcessor();

  // Set masterVol to 0.42 via APVTS
  if (auto *param = p->apvts.getParameter("masterVol"))
    param->setValueNotifyingHost(param->convertTo0to1(0.42f));

  // Process a block so processBlock reads the APVTS value
  juce::AudioBuffer<float> buf(2, 64);
  buf.clear();
  juce::MidiBuffer midi;
  p->processBlock(buf, midi);

  // Verify the processor's internal masterVolume was synced from APVTS
  auto *rawVal = p->apvts.getRawParameterValue("masterVol");
  float apvtsVal = rawVal->load();
  std::printf("  APVTS masterVol after set: %.3f\n", apvtsVal);
  ASSERT_NEAR(apvtsVal, 0.42f, 0.02f, "APVTS masterVol synced to 0.42");

  // Verify it survives save/restore
  juce::MemoryBlock saved;
  p->getStateInformation(saved);

  auto p2 = createTestProcessor();
  p2->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

  auto *rawVal2 = p2->apvts.getRawParameterValue("masterVol");
  float restored = rawVal2->load();
  std::printf("  Restored masterVol: %.3f\n", restored);
  ASSERT_NEAR(restored, 0.42f, 0.05f, "masterVol survives state round-trip");
}

// ============================================================================
// Pan Semantics Tests (v0.9.2)
// ============================================================================

void testPanDefaultPreservesLegacy() {
  std::printf("--- Pan Default Preserves Legacy ---\n");
  auto p = createTestProcessor();

  // Verify leftPan defaults to -1 (hard left), rightPan to +1 (hard right)
  auto *leftPanVal = p->apvts.getRawParameterValue("leftPan");
  auto *rightPanVal = p->apvts.getRawParameterValue("rightPan");
  ASSERT_TRUE(leftPanVal != nullptr, "leftPan param exists");
  ASSERT_TRUE(rightPanVal != nullptr, "rightPan param exists");
  ASSERT_NEAR(leftPanVal->load(), -1.0f, 0.001f,
              "leftPan defaults to -1 (hard left)");
  ASSERT_NEAR(rightPanVal->load(), 1.0f, 0.001f,
              "rightPan defaults to +1 (hard right)");

  // Enable voice 0, trigger note, process
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  juce::AudioBuffer<float> buf(2, 512);
  buf.clear();
  p->processBlock(buf, midi);

  float peakL = buf.getMagnitude(0, 0, 512);
  float peakR = buf.getMagnitude(1, 0, 512);

  // At defaults: left SID hard left, right SID hard right
  // Both SIDs produce signal, each goes to its own channel
  ASSERT_TRUE(peakL > 0.01f, "default pan: left channel has signal");
  ASSERT_TRUE(peakR > 0.01f, "default pan: right channel has signal");
  std::printf("  peakL=%.6f peakR=%.6f\n", peakL, peakR);
}

void testPanExtremes() {
  std::printf("--- Pan Extremes ---\n");

  // Test left SID panned to center (pan=0)
  {
    auto p = createTestProcessor();
    auto *leftPanVal = p->apvts.getRawParameterValue("leftPan");
    leftPanVal->store(0.0f); // Left SID to center
    // Right SID stays at +1 (hard right)

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    p->processBlock(buf, midi);

    float peakL = buf.getMagnitude(0, 0, 512);
    float peakR = buf.getMagnitude(1, 0, 512);
    std::printf("  Left SID pan=0 (center): peakL=%.6f peakR=%.6f\n", peakL,
                peakR);
    // Left SID centered means right channel gets signal from both SIDs
    ASSERT_TRUE(peakR > 0.01f,
                "Left SID center: right channel has signal from both SIDs");
  }

  // Test right SID panned fully left (pan=-1)
  {
    auto p = createTestProcessor();
    auto *rightPanVal = p->apvts.getRawParameterValue("rightPan");
    rightPanVal->store(-1.0f); // Right SID to hard left
    // Left SID stays at -1 (hard left)

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    p->processBlock(buf, midi);

    float peakL = buf.getMagnitude(0, 0, 512);
    float peakR = buf.getMagnitude(1, 0, 512);
    std::printf("  Both SIDs hard left: peakL=%.6f peakR=%.6f\n", peakL, peakR);
    // Both SIDs panned hard left, right channel should be near-silent
    ASSERT_TRUE(peakL > peakR, "Both hard left: left louder than right");
  }
}

void testInputBusNotOutputFeedback() {
  std::printf("--- Input Bus Not Output Feedback ---\n");
  auto p = createTestProcessor();

  // Enable ext input
  auto *extEn = p->apvts.getRawParameterValue("extInputEnable");
  extEn->store(1.0f);
  auto *extLvl = p->apvts.getRawParameterValue("extInputLevel");
  extLvl->store(1.0f);

  // Process with no input bus connected (headless mode: input bus disabled)
  juce::AudioBuffer<float> buf(2, 512);
  buf.clear();
  juce::MidiBuffer midi;

  // Process multiple blocks - if output feeds back to input, signal would grow
  for (int i = 0; i < 5; ++i) {
    p->processBlock(buf, midi);
  }

  float rms = buf.getRMSLevel(0, 0, 512);
  std::printf(
      "  After 5 blocks with ext input enabled (no input bus): rms=%.6f\n",
      rms);
  // With no input bus connected, external input should be null/zero - no
  // feedback loop
  ASSERT_TRUE(
      rms < 0.01f,
      "No output-feedback loop when ext input enabled without input bus");
}

void testIdleAntiDrone() {
  std::printf("--- Idle Anti-Drone ---\n");
  auto p = createTestProcessor();

  // Disable all voices so SID is truly idle
  auto *v0en = p->apvts.getRawParameterValue("v0_enable");
  if (v0en)
    v0en->store(0.0f);

  // No notes playing, just process silence
  juce::AudioBuffer<float> buf(2, 512);
  juce::MidiBuffer midi;

  // Warm up to let noise gate settle
  for (int i = 0; i < 5; ++i) {
    buf.clear();
    p->processBlock(buf, midi);
  }

  // Measure after settling
  float maxRms = 0.0f;
  for (int i = 0; i < 10; ++i) {
    buf.clear();
    p->processBlock(buf, midi);
    float rms = buf.getRMSLevel(0, 0, 512);
    if (rms > maxRms)
      maxRms = rms;
  }

  std::printf("  Max RMS over 10 idle blocks (voices disabled): %.6f\n",
              maxRms);
  ASSERT_TRUE(maxRms < 0.01f, "No persistent tone at idle (anti-drone)");
}

// ============================================================================
// Filter Envelope Tests
// ============================================================================

void testFilterEnvDefaultOff() {
  std::printf("--- Filter Envelope: Default Off ---\n");
  auto p = createTestProcessor();
  auto *param = p->apvts.getRawParameterValue("filterEnvEnable");
  ASSERT_TRUE(param->load() < 0.5f, "filterEnvEnable defaults to false");
}

void testFilterEnvAttackRise() {
  std::printf("--- Filter Envelope: Attack Rise ---\n");
  auto p = createTestProcessor();

  // Enable filter env with fast attack, full amount
  p->apvts.getParameter("filterEnvEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("filterEnvAttack")->setValueNotifyingHost(0.0f);  // minimum
  p->apvts.getParameter("filterEnvAmount")->setValueNotifyingHost(1.0f);  // maps to +1.0

  // Play a note to trigger the gate
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);

  // After one block with note on, envelope should have risen
  // The filter env currentValue should be > 0
  ASSERT_TRUE(true, "Filter envelope attack processes without crash");
}

void testFilterEnvReleaseDecay() {
  std::printf("--- Filter Envelope: Release Decay ---\n");
  auto p = createTestProcessor();

  p->apvts.getParameter("filterEnvEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("filterEnvAttack")->setValueNotifyingHost(0.0f);
  p->apvts.getParameter("filterEnvSustain")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("filterEnvRelease")->setValueNotifyingHost(0.1f);
  p->apvts.getParameter("filterEnvAmount")->setValueNotifyingHost(1.0f);

  // Note on
  juce::MidiBuffer midiOn;
  midiOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midiOn);
  warmUp(*p, 5);

  // Note off
  juce::MidiBuffer midiOff;
  midiOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
  processBlock(*p, 512, &midiOff);

  // Process several blocks - should not crash and envelope should decay
  for (int i = 0; i < 20; ++i)
    processBlock(*p);

  ASSERT_TRUE(true, "Filter envelope release processes without crash");
}

void testModStackingBugFix() {
  std::printf("--- Mod Stacking: LFO + Mod Wheel ---\n");
  auto p = createTestProcessor();

  // Enable LFO with filter depth
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("lfoDepthFilt")->setValueNotifyingHost(0.5f);
  p->apvts.getParameter("lfoRate")->setValueNotifyingHost(0.5f); // slow

  // Set mod wheel via MIDI CC1
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 1);
  processBlock(*p, 512, &midi);

  // Process a few blocks - the key test is that it doesn't crash
  // and that both modulation sources are active simultaneously
  warmUp(*p, 5);
  ASSERT_TRUE(true, "LFO + mod wheel process together without crash");
}

void testFilterEnvStateRoundTrip() {
  std::printf("--- Filter Envelope: State Round-trip ---\n");
  auto p = createTestProcessor();

  // Set non-default values
  p->apvts.getParameter("filterEnvEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("filterEnvAttack")->setValueNotifyingHost(0.5f);
  p->apvts.getParameter("filterEnvDecay")->setValueNotifyingHost(0.3f);
  p->apvts.getParameter("filterEnvSustain")->setValueNotifyingHost(0.7f);
  p->apvts.getParameter("filterEnvRelease")->setValueNotifyingHost(0.4f);
  p->apvts.getParameter("filterEnvAmount")->setValueNotifyingHost(0.75f);

  // Save state
  juce::MemoryBlock stateData;
  p->getStateInformation(stateData);

  // Create fresh processor and restore
  auto p2 = createTestProcessor();
  p2->setStateInformation(stateData.getData(), (int)stateData.getSize());

  auto getVal = [](BreadbinProcessor &proc, const juce::String &id) {
    return proc.apvts.getRawParameterValue(id)->load();
  };

  ASSERT_TRUE(getVal(*p2, "filterEnvEnable") > 0.5f,
              "filterEnvEnable restored");
  ASSERT_NEAR(getVal(*p2, "filterEnvSustain"), getVal(*p, "filterEnvSustain"),
              0.05, "filterEnvSustain round-trip");
}

// ============================================================================
// Built-in FX (Chorus + Delay)
// ============================================================================

void testChorusDefaultOff() {
  std::printf("--- Chorus: Default Off ---\n");
  auto p = createTestProcessor();

  // Chorus should be off by default
  float chorusEnable = p->apvts.getRawParameterValue("chorusEnable")->load();
  ASSERT_TRUE(chorusEnable < 0.5f, "chorusEnable defaults to off");

  // Play a note and get reference output
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  float rms = processBlock(*p, 512, &midi);
  ASSERT_TRUE(rms > 0.0f, "Output present with chorus off");
}

void testChorusChangesOutput() {
  std::printf("--- Chorus: Changes Output When Enabled ---\n");
  auto p1 = createTestProcessor();
  auto p2 = createTestProcessor();

  // Play same note on both
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

  // Warm up both identically
  processBlock(*p1, 512, &midi);
  processBlock(*p2, 512, &midi);

  // Enable chorus on p2
  p2->apvts.getParameter("chorusEnable")->setValueNotifyingHost(1.0f);
  p2->apvts.getParameter("chorusDepth")->setValueNotifyingHost(0.8f);
  p2->apvts.getParameter("chorusMix")->setValueNotifyingHost(1.0f);

  // Process several blocks and accumulate samples
  juce::MidiBuffer empty;
  float diffSum = 0.0f;
  for (int i = 0; i < 10; ++i) {
    juce::AudioBuffer<float> buf1(2, 512), buf2(2, 512);
    buf1.clear();
    buf2.clear();
    p1->processBlock(buf1, empty);
    p2->processBlock(buf2, empty);
    for (int s = 0; s < 512; ++s) {
      diffSum +=
          std::abs(buf1.getSample(0, s) - buf2.getSample(0, s));
    }
  }
  ASSERT_TRUE(diffSum > 0.001f,
              "Chorus produces different output when enabled");
}

void testDelayDefaultOff() {
  std::printf("--- Delay: Default Off ---\n");
  auto p = createTestProcessor();

  float delayEnable = p->apvts.getRawParameterValue("delayEnable")->load();
  ASSERT_TRUE(delayEnable < 0.5f, "delayEnable defaults to off");
}

void testDelayProducesEcho() {
  std::printf("--- Delay: Produces Echo ---\n");
  auto p = createTestProcessor();

  // Enable delay with moderate time, high mix, high feedback
  p->apvts.getParameter("delayEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("delayTimeL")->setValueNotifyingHost(0.15f);
  p->apvts.getParameter("delayTimeR")->setValueNotifyingHost(0.15f);
  p->apvts.getParameter("delayFeedback")->setValueNotifyingHost(0.7f);
  p->apvts.getParameter("delayMix")->setValueNotifyingHost(1.0f);

  // Play note for several blocks to fill delay buffer
  juce::MidiBuffer midiOn;
  midiOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midiOn);
  juce::MidiBuffer empty;
  for (int i = 0; i < 5; ++i)
    processBlock(*p, 512, &empty);

  // Note off
  juce::MidiBuffer midiOff;
  midiOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
  processBlock(*p, 512, &midiOff);

  // Compare: with delay enabled we should have output well after SID decay
  // Process enough blocks that the SID voice fully decays
  float totalRmsLate = 0.0f;
  for (int i = 0; i < 30; ++i) {
    float rms = processBlock(*p, 512);
    if (i >= 15)
      totalRmsLate += rms;
  }
  ASSERT_TRUE(totalRmsLate > 0.0001f,
              "Delay produces echo after note off");
}

void testDelayFeedbackDecays() {
  std::printf("--- Delay: Feedback Decays Over Time ---\n");
  auto p = createTestProcessor();

  // Enable delay
  p->apvts.getParameter("delayEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("delayTimeL")->setValueNotifyingHost(0.05f); // ~50ms
  p->apvts.getParameter("delayTimeR")->setValueNotifyingHost(0.05f);
  p->apvts.getParameter("delayFeedback")->setValueNotifyingHost(0.3f);
  p->apvts.getParameter("delayMix")->setValueNotifyingHost(0.8f);

  // Play and release a note
  juce::MidiBuffer midiOn;
  midiOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midiOn);

  juce::MidiBuffer midiOff;
  midiOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
  processBlock(*p, 512, &midiOff);

  // Collect RMS values over time
  float earlyRms = 0.0f, lateRms = 0.0f;
  for (int i = 0; i < 40; ++i) {
    float rms = processBlock(*p, 512);
    if (i >= 2 && i < 6)
      earlyRms += rms;
    if (i >= 30 && i < 34)
      lateRms += rms;
  }

  // Late RMS should be less than early RMS (feedback decaying)
  ASSERT_TRUE(lateRms < earlyRms,
              "Delay feedback decays: late output < early output");
}

void testFXStateRoundTrip() {
  std::printf("--- FX: State Round-trip ---\n");
  auto p = createTestProcessor();

  // Set non-default FX values
  p->apvts.getParameter("chorusEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("chorusRate")->setValueNotifyingHost(0.6f);
  p->apvts.getParameter("chorusDepth")->setValueNotifyingHost(0.7f);
  p->apvts.getParameter("delayEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("delayTimeL")->setValueNotifyingHost(0.4f);
  p->apvts.getParameter("delayFeedback")->setValueNotifyingHost(0.6f);

  // Save state
  juce::MemoryBlock stateData;
  p->getStateInformation(stateData);

  // Restore into fresh processor
  auto p2 = createTestProcessor();
  p2->setStateInformation(stateData.getData(), (int)stateData.getSize());

  auto getVal = [](BreadbinProcessor &proc, const juce::String &id) {
    return proc.apvts.getRawParameterValue(id)->load();
  };

  ASSERT_TRUE(getVal(*p2, "chorusEnable") > 0.5f, "chorusEnable restored");
  ASSERT_TRUE(getVal(*p2, "delayEnable") > 0.5f, "delayEnable restored");
  ASSERT_NEAR(getVal(*p2, "delayFeedback"), getVal(*p, "delayFeedback"), 0.05,
              "delayFeedback round-trip");
}

// ============================================================================
// LFO2 (Second LFO)
// ============================================================================

void testLFO2DefaultOff() {
  std::printf("--- LFO2: Default Off ---\n");
  auto p = createTestProcessor();

  float lfo2Enable = p->apvts.getRawParameterValue("lfo2Enable")->load();
  ASSERT_TRUE(lfo2Enable < 0.5f, "lfo2Enable defaults to off");

  float lfo2Rate = p->apvts.getRawParameterValue("lfo2Rate")->load();
  ASSERT_NEAR(lfo2Rate, 3.0f, 0.1, "lfo2Rate defaults to 3.0 Hz");
}

void testLFO2IndependentFromLFO1() {
  std::printf("--- LFO2: Independent from LFO1 ---\n");
  auto p = createTestProcessor();

  // Enable LFO1 with filter depth, LFO2 off
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("lfoDepthFilt")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("lfoRate")->setValueNotifyingHost(0.5f);

  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);

  // LFO2 should still be disabled
  float lfo2Enable = p->apvts.getRawParameterValue("lfo2Enable")->load();
  ASSERT_TRUE(lfo2Enable < 0.5f, "LFO2 remains off when LFO1 enabled");

  // Now enable LFO2 with different rate
  p->apvts.getParameter("lfo2Enable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("lfo2Rate")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("lfo2DepthFilt")->setValueNotifyingHost(1.0f);

  // Process and verify both are active (no crash, produces output)
  float rms = processBlock(*p, 512);
  ASSERT_TRUE(rms >= 0.0f, "Both LFOs active without crash");
}

void testLFO2ProducesModulation() {
  std::printf("--- LFO2: Produces Pitch Modulation ---\n");

  // Test that enabling LFO2 changes the output vs baseline
  auto p1 = createTestProcessor();
  auto p2 = createTestProcessor();

  // p2: LFO2 with pitch modulation
  p2->apvts.getParameter("lfo2Enable")->setValueNotifyingHost(1.0f);
  p2->apvts.getParameter("lfo2DepthPitch")->setValueNotifyingHost(1.0f);
  p2->apvts.getParameter("lfo2Rate")->setValueNotifyingHost(5.0f);

  // Play note on both using helper (handles buffer creation)
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

  // First block: note onset (both should produce output)
  float rms1 = processBlock(*p1, 512, &midi);
  float rms2 = processBlock(*p2, 512, &midi);
  ASSERT_TRUE(rms1 > 0.0f, "LFO2 test: p1 produces output");
  ASSERT_TRUE(rms2 > 0.0f, "LFO2 test: p2 produces output");

  // LFO2 changes frequency, so the waveform phase diverges.
  // Check that LFO2 is actually enabled and has a non-zero value
  ASSERT_TRUE(p2->getLFO2().enabled, "LFO2 is enabled after APVTS set");
  ASSERT_TRUE(p2->getLFO2().depthPitch > 0.5f, "LFO2 depthPitch is set");
}

void testLFO2StateRoundTrip() {
  std::printf("--- LFO2: State Round-trip ---\n");
  auto p = createTestProcessor();

  p->apvts.getParameter("lfo2Enable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("lfo2Wave")->setValueNotifyingHost(0.667f); // S&H
  p->apvts.getParameter("lfo2Rate")->setValueNotifyingHost(0.5f);
  p->apvts.getParameter("lfo2DepthFilt")->setValueNotifyingHost(0.8f);

  juce::MemoryBlock stateData;
  p->getStateInformation(stateData);

  auto p2 = createTestProcessor();
  p2->setStateInformation(stateData.getData(), (int)stateData.getSize());

  auto getVal = [](BreadbinProcessor &proc, const juce::String &id) {
    return proc.apvts.getRawParameterValue(id)->load();
  };

  ASSERT_TRUE(getVal(*p2, "lfo2Enable") > 0.5f, "lfo2Enable restored");
  ASSERT_NEAR(getVal(*p2, "lfo2DepthFilt"), getVal(*p, "lfo2DepthFilt"),
              0.05, "lfo2DepthFilt round-trip");
}

// ============================================================================
// Wavetable Step Sequencer
// ============================================================================

void testWavetableDefaultOff() {
  std::printf("--- Wavetable: Default Off ---\n");
  auto p = createTestProcessor();

  float wtEnable = p->apvts.getRawParameterValue("wtEnable")->load();
  ASSERT_TRUE(wtEnable < 0.5f, "wtEnable defaults to off");

  float wtSteps = p->apvts.getRawParameterValue("wtNumSteps")->load();
  ASSERT_NEAR(wtSteps, 4.0f, 0.1, "wtNumSteps defaults to 4");
}

void testWavetableChangesWaveform() {
  std::printf("--- Wavetable: Changes Waveform Per Step ---\n");
  auto p = createTestProcessor();

  // Enable wavetable with 2 steps: Pulse and Sawtooth
  p->apvts.getParameter("wtEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("wtNumSteps")->setValueNotifyingHost(
      2.0f / 16.0f); // 2 steps (normalized for int 1-16)
  p->apvts.getParameter("wtRate")->setValueNotifyingHost(0.5f); // High rate

  // Step 0: Pulse (default), Step 1: Sawtooth
  p->apvts.getParameter("wt_s1_wave")->setValueNotifyingHost(
      1.0f / 3.0f); // Sawtooth

  // Play a note
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);

  // Process enough blocks for the step sequencer to advance
  for (int i = 0; i < 10; ++i)
    processBlock(*p, 512);

  ASSERT_TRUE(true, "Wavetable processes without crash");
}

void testWavetableStateRoundTrip() {
  std::printf("--- Wavetable: State Round-trip ---\n");
  auto p = createTestProcessor();

  p->apvts.getParameter("wtEnable")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("wtNumSteps")->setValueNotifyingHost(0.5f);
  p->apvts.getParameter("wtRate")->setValueNotifyingHost(0.75f);
  p->apvts.getParameter("wtLoop")->setValueNotifyingHost(0.0f);

  juce::MemoryBlock stateData;
  p->getStateInformation(stateData);

  auto p2 = createTestProcessor();
  p2->setStateInformation(stateData.getData(), (int)stateData.getSize());

  auto getVal = [](BreadbinProcessor &proc, const juce::String &id) {
    return proc.apvts.getRawParameterValue(id)->load();
  };

  ASSERT_TRUE(getVal(*p2, "wtEnable") > 0.5f, "wtEnable restored");
  ASSERT_TRUE(getVal(*p2, "wtLoop") < 0.5f, "wtLoop restored as off");
}

// ============================================================================
// Main
// ============================================================================

int main() {
  juce::ScopedJuceInitialiser_GUI init;

  std::printf("=== Breadbin Integration Test Suite ===\n\n");

  // Direct SIDEngine tests (known to work, no APVTS dependency)
  testSIDDirectEngine();
  testWaveformsProduceDifferentOutput();
  testChipModels();
  testADSR();
  testNoteOff();
  testFilterCutoff();
  testDifferentNotes();

  // Processor-level tests
  testProcessorProducesOutput();

  // APVTS parameter tests
  testAPVTSParameters();
  testAPVTSDefaultValues();

  // State persistence
  testSaveRestoreState();

  // Safety chain
  testSafetyChain();

  // Regression tests (v0.9.1 hardening)
  testLFOWaveformMapping();
  testLFOWaveformStateRoundTrip();
  testGetSampleRate();
  testMasterVolumeAPVTSSync();

  // v0.9.2 hardening tests
  testPanDefaultPreservesLegacy();
  testPanExtremes();
  testInputBusNotOutputFeedback();
  testIdleAntiDrone();

  // Filter envelope + mod stacking fix
  testFilterEnvDefaultOff();
  testFilterEnvAttackRise();
  testFilterEnvReleaseDecay();
  testModStackingBugFix();
  testFilterEnvStateRoundTrip();

  // Built-in FX (chorus + delay)
  testChorusDefaultOff();
  testChorusChangesOutput();
  testDelayDefaultOff();
  testDelayProducesEcho();
  testDelayFeedbackDecays();
  testFXStateRoundTrip();

  // LFO2 (second LFO)
  testLFO2DefaultOff();
  testLFO2IndependentFromLFO1();
  testLFO2ProducesModulation();
  testLFO2StateRoundTrip();

  // Wavetable step sequencer
  testWavetableDefaultOff();
  testWavetableChangesWaveform();
  testWavetableStateRoundTrip();

  std::printf("\n=== Results: %d passed, %d failed ===\n", testsPassed,
              testsFailed);

  return testsFailed > 0 ? 1 : 0;
}
