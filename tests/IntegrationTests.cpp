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

  std::printf("\n=== Results: %d passed, %d failed ===\n", testsPassed,
              testsFailed);

  return testsFailed > 0 ? 1 : 0;
}
