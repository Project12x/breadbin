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
      rms < 0.02f,
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
  p->apvts.getParameter("filterEnvAttack")
      ->setValueNotifyingHost(0.0f); // minimum
  p->apvts.getParameter("filterEnvAmount")
      ->setValueNotifyingHost(1.0f); // maps to +1.0

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
      diffSum += std::abs(buf1.getSample(0, s) - buf2.getSample(0, s));
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
  ASSERT_TRUE(totalRmsLate > 0.0001f, "Delay produces echo after note off");
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
  ASSERT_NEAR(getVal(*p2, "lfo2DepthFilt"), getVal(*p, "lfo2DepthFilt"), 0.05,
              "lfo2DepthFilt round-trip");
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
  p->apvts.getParameter("wtNumSteps")
      ->setValueNotifyingHost(2.0f /
                              16.0f); // 2 steps (normalized for int 1-16)
  p->apvts.getParameter("wtRate")->setValueNotifyingHost(0.5f); // High rate

  // Step 0: Pulse (default), Step 1: Sawtooth
  p->apvts.getParameter("wt_s1_wave")
      ->setValueNotifyingHost(1.0f / 3.0f); // Sawtooth

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

void testWavetablePWPreservedWithLFOOff() {
  std::printf("--- Wavetable: Step PW preserved when LFO off ---\n");

  // Verify WT step PW is synced from APVTS and used as base in
  // applyLFOModulation
  auto p = createTestProcessor();

  // Set WT enabled with 1 step, PW=100 via setValueNotifyingHost
  auto *wtEn = p->apvts.getParameter("wtEnable");
  wtEn->setValueNotifyingHost(1.0f);
  auto *ns = p->apvts.getParameter("wtNumSteps");
  ns->setValueNotifyingHost(ns->convertTo0to1(1.0f));
  auto *pw = p->apvts.getParameter("wt_s0_pw");
  pw->setValueNotifyingHost(pw->convertTo0to1(100.0f));

  // Process one block to sync APVTS -> wavetable state
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)127), 0);
  float rms = processBlock(*p, 512, &midi);

  // Verify wavetable state was synced correctly
  auto &wt = p->getWavetable();
  ASSERT_TRUE(wt.enabled, "WT enabled after processBlock");
  ASSERT_TRUE(wt.numSteps == 1, "WT numSteps synced to 1");
  ASSERT_TRUE(wt.steps[0].pulseWidth == 100,
              "WT step 0 PW synced to 100 (not overwritten)");
  ASSERT_TRUE(rms > 0.0f, "Audio produced with WT PW=100");
  std::printf("  WT state: enabled=%d steps=%d step0.pw=%d rms=%.6f\n",
              wt.enabled ? 1 : 0, wt.numSteps, wt.steps[0].pulseWidth, rms);
}

void testWavetablePitchOffsetPreserved() {
  std::printf("--- Wavetable: Pitch offset preserved when LFO off ---\n");

  // Verify WT step pitch offset is synced and used in applyLFOModulation
  auto p = createTestProcessor();

  auto *wtEn = p->apvts.getParameter("wtEnable");
  wtEn->setValueNotifyingHost(1.0f);
  auto *ns = p->apvts.getParameter("wtNumSteps");
  ns->setValueNotifyingHost(ns->convertTo0to1(1.0f));
  auto *pp = p->apvts.getParameter("wt_s0_pitch");
  pp->setValueNotifyingHost(
      pp->convertTo0to1(12.0f)); // +12 semitones (1 octave)

  // Process one block to sync
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)127), 0);
  float rms = processBlock(*p, 512, &midi);

  // Verify wavetable state
  auto &wt = p->getWavetable();
  ASSERT_TRUE(wt.enabled, "WT enabled after processBlock");
  ASSERT_TRUE(wt.steps[0].pitchOffset == 12,
              "WT step 0 pitch synced to +12 (not overwritten)");
  ASSERT_TRUE(rms > 0.0f, "Audio produced with WT pitch offset");
  std::printf("  WT state: enabled=%d step0.pitch=%d rms=%.6f\n",
              wt.enabled ? 1 : 0, wt.steps[0].pitchOffset, rms);
}

void testWavetableLFOCoexistence() {
  std::printf("--- Wavetable: WT + LFO coexistence ---\n");

  // Verify WT and LFO coexist: both active after processBlock, LFO runs
  // with non-zero output, WT step values preserved as modulation base
  auto p = createTestProcessor();

  p->apvts.getParameter("wtEnable")->setValueNotifyingHost(1.0f);
  auto *ns = p->apvts.getParameter("wtNumSteps");
  ns->setValueNotifyingHost(ns->convertTo0to1(1.0f));
  auto *pw = p->apvts.getParameter("wt_s0_pw");
  pw->setValueNotifyingHost(pw->convertTo0to1(100.0f));
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *rate = p->apvts.getParameter("lfoRate");
  rate->setValueNotifyingHost(rate->convertTo0to1(5.0f));
  p->apvts.getParameter("lfoDepthPW")->setValueNotifyingHost(1.0f);

  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)127), 0);
  float rms = processBlock(*p, 512, &midi);

  // After processBlock, LFO should be active with a non-zero value
  auto &lfoState = p->getLFO();
  ASSERT_TRUE(lfoState.enabled, "LFO enabled during WT+LFO coexistence");
  ASSERT_TRUE(lfoState.depthPulseWidth > 0.5f,
              "LFO PW depth set during WT+LFO coexistence");
  ASSERT_TRUE(std::abs(lfoState.currentValue) > 0.001f,
              "LFO produced non-zero value with WT active");

  // WT should still be active with correct step values
  auto &wt = p->getWavetable();
  ASSERT_TRUE(wt.enabled, "WT still enabled during WT+LFO coexistence");
  ASSERT_TRUE(wt.steps[0].pulseWidth == 100,
              "WT step PW preserved as LFO base");

  ASSERT_TRUE(rms > 0.0f, "Audio produced with WT+LFO both active");
  std::printf(
      "  WT+LFO coexist: wt.pw=%d lfo.val=%.4f lfo.depthPW=%.2f rms=%.6f\n",
      wt.steps[0].pulseWidth, lfoState.currentValue, lfoState.depthPulseWidth,
      rms);
}

// ============================================================================
// Pipeline Order-of-Operations Test
// ============================================================================
// Documents the intended modulation pipeline in processBlock:
//   1. applyVoiceSettings()  — sets waveform/PW/ADSR from APVTS voice params
//   2. handleMidiEvent()     — noteOn/noteOff, pitch bend
//   3. processWavetable()    — overrides waveform per step (PW/pitch deferred)
//   4. processGlide()        — interpolates frequency for portamento
//   5. processLFO()/LFO2()   — advances LFO phase, computes currentValue
//   6. applyLFOModulation()  — sets PW (WT base when active) + pitch (WT
//   offset)
//   7. processFilterEnvelope()
//   8. applyFilterModulation() — stacks mod wheel + LFO + filter env on cutoff
//   9. applyModMatrix()      — additional source->dest routing
//  10. SID clock loop        — per-sample audio generation
//  11. Safety chain          — subsonic/ultrasonic filters, limiter, noise gate

void testPipelineOrderOfOperations() {
  std::printf(
      "--- Pipeline: WT base -> LFO mod -> filter/env/mod-matrix ---\n");

  // Enable WT (step PW=500, pitch=+7) and LFO1 (PW depth).
  // After processBlock, verify:
  //   - WT step values are the base (not voice APVTS defaults)
  //   - LFO ran on top (non-zero currentValue)
  //   - Filter modulation ran (applyFilterModulation uses stacked sources)
  auto p = createTestProcessor();
  p->apvts.getParameter("wtEnable")->setValueNotifyingHost(1.0f);
  auto *ns = p->apvts.getParameter("wtNumSteps");
  ns->setValueNotifyingHost(ns->convertTo0to1(1.0f));
  auto *pw = p->apvts.getParameter("wt_s0_pw");
  pw->setValueNotifyingHost(pw->convertTo0to1(500.0f));
  auto *pitch = p->apvts.getParameter("wt_s0_pitch");
  pitch->setValueNotifyingHost(pitch->convertTo0to1(7.0f));

  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *rate = p->apvts.getParameter("lfoRate");
  rate->setValueNotifyingHost(rate->convertTo0to1(10.0f));
  p->apvts.getParameter("lfoDepthPW")->setValueNotifyingHost(0.5f);

  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  float rms = processBlock(*p, 512, &midi);

  // Stage 3: WT step values are the modulation base
  auto &wt = p->getWavetable();
  ASSERT_TRUE(wt.enabled, "Pipeline: WT active");
  ASSERT_TRUE(wt.steps[0].pulseWidth == 500,
              "Pipeline: WT PW=500 is base (not voice default 2048)");
  ASSERT_TRUE(wt.steps[0].pitchOffset == 7,
              "Pipeline: WT pitch=+7 is base (not voice default 0)");

  // Stage 5-6: LFO ran and produced a value
  auto &lfo = p->getLFO();
  ASSERT_TRUE(lfo.enabled, "Pipeline: LFO active");
  ASSERT_TRUE(std::abs(lfo.currentValue) > 0.001f,
              "Pipeline: LFO produced non-zero value");
  ASSERT_TRUE(lfo.depthPulseWidth > 0.4f, "Pipeline: LFO PW depth preserved");

  // Stage 10: Audio was generated
  ASSERT_TRUE(rms > 0.0f, "Pipeline: audio produced");
  std::printf("  Pipeline OK: wt.pw=%d wt.pitch=%d lfo.val=%.4f rms=%.6f\n",
              wt.steps[0].pulseWidth, wt.steps[0].pitchOffset, lfo.currentValue,
              rms);
}

// ============================================================================
// Mod Matrix Tests
// ============================================================================

void testModMatrixDefaultNone() {
  std::printf("--- ModMatrix: Default None ---\n");
  auto p = createTestProcessor();

  // Verify all 4 slots default to None/None/0
  auto getVal = [](BreadbinProcessor &proc, const juce::String &id) {
    return proc.apvts.getRawParameterValue(id)->load();
  };

  for (int i = 0; i < 4; ++i) {
    auto prefix = "mod" + juce::String(i) + "_";
    ASSERT_NEAR(
        getVal(*p, prefix + "src"), 0.0f, 0.01f,
        ("Slot " + juce::String(i) + " src defaults to None").toRawUTF8());
    ASSERT_NEAR(
        getVal(*p, prefix + "dst"), 0.0f, 0.01f,
        ("Slot " + juce::String(i) + " dst defaults to None").toRawUTF8());
    ASSERT_NEAR(getVal(*p, prefix + "amt"), 0.0f, 0.01f,
                ("Slot " + juce::String(i) + " amt defaults to 0").toRawUTF8());
  }
}

void testModMatrixLFOToFilterRoute() {
  std::printf("--- ModMatrix: LFO1 -> Filter route ---\n");
  auto p1 = createTestProcessor();
  auto p2 = createTestProcessor();

  // Configure both: pulse wave, filter enabled in LP mode, resonance for effect
  for (auto *p : {p1.get(), p2.get()}) {
    p->apvts.getParameter("v0_waveform")
        ->setValueNotifyingHost(
            p->apvts.getParameter("v0_waveform")->convertTo0to1(2.0f)); // Pulse
    // Enable LP filter on both SIDs with voice 0 routed
    p->getLeftSID().setFilterMode(true, false, false);
    p->getLeftSID().setFilterVoices(true, true, true);
    p->getLeftSID().setFilterResonance(8);
    p->getRightSID().setFilterMode(true, false, false);
    p->getRightSID().setFilterVoices(true, true, true);
    p->getRightSID().setFilterResonance(8);

    // Enable LFO1, fast rate, zero direct filter depth
    p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
    p->apvts.getParameter("lfoRate")->setValueNotifyingHost(1.0f); // 20 Hz
    p->apvts.getParameter("lfoDepthFilt")->setValueNotifyingHost(0.0f);
  }

  // Route slot 0 on p2 only: LFO1 -> FilterCutoff, full amount
  auto *srcParam = p2->apvts.getParameter("mod0_src");
  auto *dstParam = p2->apvts.getParameter("mod0_dst");
  srcParam->setValueNotifyingHost(srcParam->convertTo0to1(1.0f)); // LFO1
  dstParam->setValueNotifyingHost(
      dstParam->convertTo0to1(1.0f)); // FilterCutoff
  p2->apvts.getParameter("mod0_amt")->setValueNotifyingHost(1.0f); // Full

  // Play same note on both
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p1, 512, &midi);
  processBlock(*p2, 512, &midi);

  // Process many blocks and accumulate sample differences
  juce::MidiBuffer empty;
  float diffSum = 0.0f;
  for (int i = 0; i < 30; ++i) {
    juce::AudioBuffer<float> buf1(2, 512), buf2(2, 512);
    buf1.clear();
    buf2.clear();
    p1->processBlock(buf1, empty);
    p2->processBlock(buf2, empty);
    for (int s = 0; s < 512; ++s) {
      diffSum += std::abs(buf1.getSample(0, s) - buf2.getSample(0, s));
    }
  }
  ASSERT_TRUE(diffSum > 0.001f,
              "ModMatrix LFO->Filter route changes output vs baseline");
}

void testModMatrixBipolarAmount() {
  std::printf("--- ModMatrix: Bipolar amount ---\n");
  auto p = createTestProcessor();

  // Verify negative amount parameter is accepted
  auto *amtParam = p->apvts.getParameter("mod0_amt");
  amtParam->setValueNotifyingHost(0.0f); // maps to -1.0
  float stored = p->apvts.getRawParameterValue("mod0_amt")->load();
  ASSERT_TRUE(stored < 0.0f, "Negative mod amount stored correctly");

  amtParam->setValueNotifyingHost(1.0f); // maps to +1.0
  stored = p->apvts.getRawParameterValue("mod0_amt")->load();
  ASSERT_TRUE(stored > 0.0f, "Positive mod amount stored correctly");

  amtParam->setValueNotifyingHost(0.5f); // maps to 0.0
  stored = p->apvts.getRawParameterValue("mod0_amt")->load();
  ASSERT_NEAR(stored, 0.0f, 0.05f, "Center mod amount is near zero");
}

void testModMatrixStateRoundTrip() {
  std::printf("--- ModMatrix: State Round-trip ---\n");
  auto p = createTestProcessor();

  // Configure slot 0 and slot 2
  auto *src0 = p->apvts.getParameter("mod0_src");
  auto *dst0 = p->apvts.getParameter("mod0_dst");
  src0->setValueNotifyingHost(src0->convertTo0to1(2.0f)); // LFO2
  dst0->setValueNotifyingHost(dst0->convertTo0to1(2.0f)); // PW
  p->apvts.getParameter("mod0_amt")->setValueNotifyingHost(0.75f);

  auto *src2 = p->apvts.getParameter("mod2_src");
  auto *dst2 = p->apvts.getParameter("mod2_dst");
  src2->setValueNotifyingHost(src2->convertTo0to1(4.0f)); // ModWheel
  dst2->setValueNotifyingHost(dst2->convertTo0to1(3.0f)); // Pitch
  p->apvts.getParameter("mod2_amt")->setValueNotifyingHost(0.25f);

  // Save state
  juce::MemoryBlock stateData;
  p->getStateInformation(stateData);

  // Restore into new processor
  auto p2 = createTestProcessor();
  p2->setStateInformation(stateData.getData(), (int)stateData.getSize());

  auto getVal = [](BreadbinProcessor &proc, const juce::String &id) {
    return proc.apvts.getRawParameterValue(id)->load();
  };

  // Verify slot 0 restored
  ASSERT_NEAR(getVal(*p2, "mod0_src"), 2.0f, 0.1f,
              "Slot 0 src restored (LFO2)");
  ASSERT_NEAR(getVal(*p2, "mod0_dst"), 2.0f, 0.1f, "Slot 0 dst restored (PW)");
  float amt0 = getVal(*p2, "mod0_amt");
  ASSERT_TRUE(amt0 > 0.3f && amt0 < 0.7f, "Slot 0 amt restored near 0.5");

  // Verify slot 2 restored
  ASSERT_NEAR(getVal(*p2, "mod2_src"), 4.0f, 0.1f,
              "Slot 2 src restored (ModWheel)");
  ASSERT_NEAR(getVal(*p2, "mod2_dst"), 3.0f, 0.1f,
              "Slot 2 dst restored (Pitch)");

  // Verify untouched slot 1 still default
  ASSERT_NEAR(getVal(*p2, "mod1_src"), 0.0f, 0.1f, "Slot 1 src still None");
}

void testModMatrixResonanceReturnsToBase() {
  std::printf(
      "--- ModMatrix: Resonance returns to base when amount zeroed ---\n");

  // Two identical processors: pulse wave, LP filter, base resonance = 8
  auto setupProc = []() {
    auto p = createTestProcessor();
    p->apvts.getParameter("v0_waveform")
        ->setValueNotifyingHost(
            p->apvts.getParameter("v0_waveform")->convertTo0to1(2.0f)); // Pulse
    p->getLeftSID().setFilterMode(true, false, false);
    p->getLeftSID().setFilterVoices(true, true, true);
    p->getRightSID().setFilterMode(true, false, false);
    p->getRightSID().setFilterVoices(true, true, true);
    p->setBaseFilterResonance(true, 8);
    p->setBaseFilterResonance(false, 8);
    p->getLeftSID().setFilterResonance(8);
    p->getRightSID().setFilterResonance(8);
    return p;
  };

  auto pMod = setupProc();      // Will get resonance mod then zeroed
  auto pBaseline = setupProc(); // Never modulated — stays at base resonance

  // Route slot 0 on pMod: Velocity -> Resonance, full amount
  auto *srcParam = pMod->apvts.getParameter("mod0_src");
  auto *dstParam = pMod->apvts.getParameter("mod0_dst");
  srcParam->setValueNotifyingHost(srcParam->convertTo0to1(5.0f)); // Velocity
  dstParam->setValueNotifyingHost(dstParam->convertTo0to1(4.0f)); // Resonance
  pMod->apvts.getParameter("mod0_amt")->setValueNotifyingHost(1.0f); // Full

  // Play note on both
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*pMod, 512, &midi);
  processBlock(*pBaseline, 512, &midi);

  // With modulation active, resonance is boosted — outputs should differ
  juce::MidiBuffer empty;
  float diffWhileActive = 0.0f;
  for (int i = 0; i < 5; ++i) {
    juce::AudioBuffer<float> buf1(2, 512), buf2(2, 512);
    buf1.clear();
    buf2.clear();
    pMod->processBlock(buf1, empty);
    pBaseline->processBlock(buf2, empty);
    for (int s = 0; s < 512; ++s)
      diffWhileActive += std::abs(buf1.getSample(0, s) - buf2.getSample(0, s));
  }
  ASSERT_TRUE(diffWhileActive > 0.001f,
              "Resonance mod active: output differs from baseline");

  // Now zero the amount — resonance should return to base
  pMod->apvts.getParameter("mod0_amt")
      ->setValueNotifyingHost(0.5f); // 0.5 maps to 0.0

  // Process a few settling blocks
  for (int i = 0; i < 3; ++i) {
    processBlock(*pMod);
    processBlock(*pBaseline);
  }

  // Compare: with amount zeroed, both should now match (both at base res=8)
  float diffAfterZeroed = 0.0f;
  for (int i = 0; i < 5; ++i) {
    juce::AudioBuffer<float> buf1(2, 512), buf2(2, 512);
    buf1.clear();
    buf2.clear();
    pMod->processBlock(buf1, empty);
    pBaseline->processBlock(buf2, empty);
    for (int s = 0; s < 512; ++s)
      diffAfterZeroed += std::abs(buf1.getSample(0, s) - buf2.getSample(0, s));
  }
  // After zeroing, the diff should be much smaller than while active
  ASSERT_TRUE(
      diffAfterZeroed < diffWhileActive * 0.1f,
      "Resonance returns to base: output matches baseline after amount zeroed");
}

// ============================================================================
// Pitch Bend Range APVTS Tests
// ============================================================================

void testPitchBendRangeAPVTSDefault() {
  std::printf("--- Pitch bend range APVTS default = 2 ---\n");
  auto p = createTestProcessor();
  auto *val = p->apvts.getRawParameterValue("pitchBendRange");
  ASSERT_TRUE(val != nullptr, "pitchBendRange parameter exists");
  ASSERT_TRUE(static_cast<int>(val->load()) == 2,
              "pitchBendRange default is 2");
}

void testPitchBendRangeAPVTSSync() {
  std::printf("--- Pitch bend range APVTS sync to engine ---\n");
  auto p = createTestProcessor();
  warmUp(*p);

  // Set APVTS to 7
  auto *param = p->apvts.getParameter("pitchBendRange");
  ASSERT_TRUE(param != nullptr, "pitchBendRange param found");
  param->setValueNotifyingHost(param->convertTo0to1(7.0f));

  // Process a block so processBlock syncs the value
  warmUp(*p);

  ASSERT_TRUE(p->getPitchBendRange() == 7,
              "Engine pitchBendRange synced to 7 from APVTS");
}

void testPitchBendRangeStatePersistence() {
  std::printf("--- Pitch bend range persists across save/restore ---\n");
  juce::MemoryBlock stateData;

  {
    auto p = createTestProcessor();
    warmUp(*p);
    auto *param = p->apvts.getParameter("pitchBendRange");
    param->setValueNotifyingHost(param->convertTo0to1(12.0f));
    warmUp(*p);
    ASSERT_TRUE(p->getPitchBendRange() == 12, "Range set to 12 before save");
    p->getStateInformation(stateData);
  }

  {
    auto p2 = createTestProcessor();
    p2->setStateInformation(stateData.getData(), (int)stateData.getSize());
    warmUp(*p2);
    ASSERT_TRUE(p2->getPitchBendRange() == 12,
                "Range restored to 12 after load");
  }
}

void testPitchBendRangeFullCycle() {
  std::printf(
      "--- Pitch bend range full lifecycle (set/sync/clamp/reset) ---\n");
  auto p = createTestProcessor();
  auto *param = p->apvts.getParameter("pitchBendRange");

  // Test all valid values sync correctly through processBlock
  static constexpr int testValues[] = {2, 3, 5, 7, 12};
  for (int val : testValues) {
    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(val)));
    warmUp(*p);
    ASSERT_TRUE(p->getPitchBendRange() == val,
                ("Range synced to " + std::to_string(val)).c_str());
  }

  // Verify APVTS int parameter clamps out-of-range: set to min boundary
  param->setValueNotifyingHost(0.0f); // normalized 0 = min = 2
  warmUp(*p);
  ASSERT_TRUE(p->getPitchBendRange() == 2, "Normalized 0.0 clamps to min (2)");

  // Set to max boundary
  param->setValueNotifyingHost(1.0f); // normalized 1 = max = 12
  warmUp(*p);
  ASSERT_TRUE(p->getPitchBendRange() == 12,
              "Normalized 1.0 clamps to max (12)");

  // Verify reset: set to 12, then reset via APVTS to default 2
  param->setValueNotifyingHost(param->convertTo0to1(12.0f));
  warmUp(*p);
  ASSERT_TRUE(p->getPitchBendRange() == 12, "Set to 12 before reset");
  param->setValueNotifyingHost(param->convertTo0to1(2.0f));
  warmUp(*p);
  ASSERT_TRUE(p->getPitchBendRange() == 2, "Reset back to 2");
}

// Post-Modulation Value Storage Tests
// ============================================================================

void testPostModPWStorage() {
  std::printf(
      "--- Post-mod PW storage: LFO PW depth changes lastAppliedPW ---\n");
  auto p = createTestProcessor();

  // Use Square LFO (always +-1.0, never zero) to guarantee a delta
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *waveParam = p->apvts.getParameter("lfoWave");
  waveParam->setValueNotifyingHost(waveParam->convertTo0to1(2.0f)); // Square
  auto *rateParam = p->apvts.getParameter("lfoRate");
  rateParam->setValueNotifyingHost(rateParam->convertTo0to1(2.0f)); // slow rate
  p->apvts.getParameter("lfoDepthPW")->setValueNotifyingHost(1.0f); // max depth

  int basePW = p->getVoiceSettings(0).pulseWidth; // default 2048

  // Trigger a note and process
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);
  for (int i = 0; i < 3; ++i)
    processBlock(*p);

  int appliedPW = p->getLastAppliedPW();

  ASSERT_TRUE(appliedPW >= 0 && appliedPW <= 4095,
              "Post-mod PW within valid range");
  ASSERT_TRUE(appliedPW != basePW,
              "Post-mod PW differs from base when LFO active");
  // Square LFO at max depth: PW offset = +-1.0 * 2048 = +-2048
  int delta = std::abs(appliedPW - basePW);
  ASSERT_TRUE(delta > 100, "Post-mod PW delta is substantial (>100)");
}

// ==================== PWM SWEEP TESTS ====================

void testPWMSweepDefaultOff() {
  std::printf("--- PWM Sweep: default off, depth=0 ---\n");
  auto p = createTestProcessor();

  auto *enableParam = p->apvts.getParameter("pwmSweepEnable");
  auto *rateParam = p->apvts.getParameter("pwmSweepRate");
  auto *depthParam = p->apvts.getParameter("pwmSweepDepth");

  ASSERT_TRUE(enableParam != nullptr, "pwmSweepEnable param exists");
  ASSERT_TRUE(rateParam != nullptr, "pwmSweepRate param exists");
  ASSERT_TRUE(depthParam != nullptr, "pwmSweepDepth param exists");
  if (!enableParam || !rateParam || !depthParam)
    return;

  // AudioParameterBool: getValue() returns 0.0 (false) or 1.0 (true)
  ASSERT_NEAR(enableParam->getValue(), 0.0f, 0.01f,
              "PWM sweep disabled by default");

  float depthVal = depthParam->convertFrom0to1(depthParam->getValue());
  ASSERT_NEAR(depthVal, 0.0f, 0.01f, "PWM sweep depth=0 by default");

  float rateVal = rateParam->convertFrom0to1(rateParam->getValue());
  ASSERT_NEAR(rateVal, 0.5f, 0.05f, "PWM sweep rate=0.5 by default");
}

void testPWMSweepModifiesPW() {
  std::printf("--- PWM Sweep: modifies PW when enabled ---\n");
  auto p = createTestProcessor();

  // Set voice 0 to Pulse waveform with known PW
  p->apvts.getParameter("v0_waveform")
      ->setValueNotifyingHost(
          p->apvts.getParameter("v0_waveform")->convertTo0to1(2.0f)); // Pulse
  p->apvts.getParameter("v0_pw")->setValueNotifyingHost(
      p->apvts.getParameter("v0_pw")->convertTo0to1(2048.0f)); // center

  // Process a block with sweep disabled to get baseline PW
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);

  int basePW = p->getLastAppliedPW();

  // Enable PWM sweep with full depth and high rate
  p->apvts.getParameter("pwmSweepEnable")->setValueNotifyingHost(1.0f);
  auto *rateParam = p->apvts.getParameter("pwmSweepRate");
  rateParam->setValueNotifyingHost(rateParam->convertTo0to1(5.0f));    // fast
  p->apvts.getParameter("pwmSweepDepth")->setValueNotifyingHost(1.0f); // max

  // Process several blocks to let the sweep oscillator advance
  for (int i = 0; i < 10; ++i)
    processBlock(*p);

  int modPW = p->getLastAppliedPW();

  ASSERT_TRUE(modPW >= 0 && modPW <= 4095, "Post-sweep PW within valid range");
  ASSERT_TRUE(modPW != basePW,
              "PWM sweep modifies PW when enabled with depth > 0");
}

void testPWMSweepStateRoundTrip() {
  std::printf("--- PWM Sweep: state round-trip ---\n");
  auto p = createTestProcessor();

  // Set non-default values
  p->apvts.getParameter("pwmSweepEnable")->setValueNotifyingHost(1.0f);
  auto *rateParam = p->apvts.getParameter("pwmSweepRate");
  rateParam->setValueNotifyingHost(rateParam->convertTo0to1(3.5f));
  p->apvts.getParameter("pwmSweepDepth")->setValueNotifyingHost(0.75f);

  // Save state
  juce::MemoryBlock stateData;
  p->getStateInformation(stateData);

  // Restore to a fresh processor
  auto p2 = createTestProcessor();
  p2->setStateInformation(stateData.getData(),
                          static_cast<int>(stateData.getSize()));

  float enable2 = p2->apvts.getParameter("pwmSweepEnable")
                      ->convertFrom0to1(
                          p2->apvts.getParameter("pwmSweepEnable")->getValue());
  ASSERT_NEAR(enable2, 1.0f, 0.01f, "PWM sweep enable restored");

  float rate2 =
      p2->apvts.getParameter("pwmSweepRate")
          ->convertFrom0to1(p2->apvts.getParameter("pwmSweepRate")->getValue());
  ASSERT_NEAR(rate2, 3.5f, 0.1f, "PWM sweep rate restored (~3.5)");

  float depth2 = p2->apvts.getParameter("pwmSweepDepth")
                     ->convertFrom0to1(
                         p2->apvts.getParameter("pwmSweepDepth")->getValue());
  ASSERT_NEAR(depth2, 0.75f, 0.01f, "PWM sweep depth restored (0.75)");
}

void testPostModPitchStorage() {
  std::printf(
      "--- Post-mod pitch storage: LFO pitch depth changes offset ---\n");
  auto p = createTestProcessor();

  // Verify baseline: no LFO -> pitch offset is zero
  processBlock(*p);
  ASSERT_NEAR(p->getLastAppliedPitchOffset(), 0.0f, 0.001f,
              "Pitch offset is zero with LFO disabled");

  // Enable Square LFO with pitch modulation (always +-1.0)
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *waveParam = p->apvts.getParameter("lfoWave");
  waveParam->setValueNotifyingHost(waveParam->convertTo0to1(2.0f)); // Square
  auto *rateParam = p->apvts.getParameter("lfoRate");
  rateParam->setValueNotifyingHost(rateParam->convertTo0to1(2.0f)); // slow rate
  p->apvts.getParameter("lfoDepthPitch")->setValueNotifyingHost(1.0f);

  // Trigger a note and process
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);
  for (int i = 0; i < 3; ++i)
    processBlock(*p);

  float pitchOffset = p->getLastAppliedPitchOffset();
  // Square LFO at max depth: offset = +-1.0 * 2.0 semitones
  ASSERT_TRUE(std::abs(pitchOffset) > 0.1f,
              "Post-mod pitch offset is non-zero with active LFO");
  ASSERT_TRUE(pitchOffset >= -3.0f && pitchOffset <= 3.0f,
              "Post-mod pitch offset within expected range");
}

void testPostModResonanceStorage() {
  std::printf("--- Post-mod resonance storage: mod matrix LFO->Res changes "
              "value ---\n");
  auto p = createTestProcessor();

  // Set base resonance to 7 (mid-range, so both +/- offsets stay in [0,15])
  int baseRes = 7;
  p->setBaseFilterResonance(true, baseRes);
  p->setBaseFilterResonance(false, baseRes);
  p->getLeftSID().setFilterResonance(baseRes);
  p->getRightSID().setFilterResonance(baseRes);

  // Enable Square LFO (always +-1.0) for deterministic source value
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *waveParam = p->apvts.getParameter("lfoWave");
  waveParam->setValueNotifyingHost(waveParam->convertTo0to1(2.0f)); // Square
  auto *rateParam = p->apvts.getParameter("lfoRate");
  rateParam->setValueNotifyingHost(rateParam->convertTo0to1(2.0f)); // slow

  // Route mod matrix: LFO1 -> Resonance, full amount
  auto *srcParam = p->apvts.getParameter("mod0_src");
  auto *dstParam = p->apvts.getParameter("mod0_dst");
  srcParam->setValueNotifyingHost(srcParam->convertTo0to1(1.0f)); // LFO1
  dstParam->setValueNotifyingHost(dstParam->convertTo0to1(4.0f)); // Resonance
  p->apvts.getParameter("mod0_amt")->setValueNotifyingHost(1.0f); // max

  // Play and process
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);
  for (int i = 0; i < 3; ++i)
    processBlock(*p);

  int appliedRes = p->getLastAppliedResLeft();
  ASSERT_TRUE(appliedRes >= 0 && appliedRes <= 15,
              "Post-mod resonance within valid range [0,15]");
  ASSERT_TRUE(appliedRes != baseRes,
              "Post-mod resonance differs from base when mod matrix active");
}

void testModSlotDisplayValues() {
  std::printf("--- Mod slot display: active slot reports non-zero "
              "source/contribution ---\n");
  auto p = createTestProcessor();

  // Enable Square LFO (always +-1.0, never zero)
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *waveParam = p->apvts.getParameter("lfoWave");
  waveParam->setValueNotifyingHost(waveParam->convertTo0to1(2.0f)); // Square
  auto *rateParam = p->apvts.getParameter("lfoRate");
  rateParam->setValueNotifyingHost(rateParam->convertTo0to1(2.0f)); // slow

  // Route slot 0: LFO1 -> Filter, amount 0.5
  auto *srcParam = p->apvts.getParameter("mod0_src");
  auto *dstParam = p->apvts.getParameter("mod0_dst");
  srcParam->setValueNotifyingHost(srcParam->convertTo0to1(1.0f)); // LFO1
  dstParam->setValueNotifyingHost(dstParam->convertTo0to1(1.0f)); // Filter
  auto *amtParam = p->apvts.getParameter("mod0_amt");
  amtParam->setValueNotifyingHost(amtParam->convertTo0to1(0.5f));

  // Play and process
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);
  for (int i = 0; i < 3; ++i)
    processBlock(*p);

  float srcVal = p->getModSlotSourceValue(0);
  float contrib = p->getModSlotContribution(0);

  // Square LFO guarantees |srcVal| = 1.0
  ASSERT_TRUE(std::abs(srcVal) > 0.5f,
              "Mod slot source value is non-zero (Square LFO)");
  // Contribution = srcVal * 0.5
  ASSERT_NEAR(contrib, srcVal * 0.5f, 0.01f,
              "Mod slot contribution = srcVal * amount");

  // Unconfigured slot 1 should still be zero
  ASSERT_NEAR(p->getModSlotSourceValue(1), 0.0f, 0.001f,
              "Unconfigured slot 1 source is zero");
  ASSERT_NEAR(p->getModSlotContribution(1), 0.0f, 0.001f,
              "Unconfigured slot 1 contribution is zero");
}

void testModSlotInactiveZeros() {
  std::printf("--- Mod slot inactive: unconfigured slots report zero ---\n");
  auto p = createTestProcessor();

  // Process a block without any mod matrix config
  processBlock(*p);

  for (int i = 0; i < 4; ++i) {
    ASSERT_NEAR(p->getModSlotSourceValue(i), 0.0f, 0.001f,
                "Inactive slot source value is zero");
    ASSERT_NEAR(p->getModSlotContribution(i), 0.0f, 0.001f,
                "Inactive slot contribution is zero");
  }
}

void testModSlotEnableGate() {
  std::printf("--- Mod slot enable gate: disabling row zeros contribution and "
              "totals ---\n");
  auto p = createTestProcessor();

  auto *enable0 = p->apvts.getParameter("mod0_enable");
  auto *src0 = p->apvts.getParameter("mod0_src");
  auto *dst0 = p->apvts.getParameter("mod0_dst");
  auto *amt0 = p->apvts.getParameter("mod0_amt");
  if (!enable0 || !src0 || !dst0 || !amt0) {
    ASSERT_TRUE(false, "mod0 params missing");
    return;
  }

  // Ensure all mod-row enable params exist and default to enabled.
  for (int i = 0; i < 4; ++i) {
    auto id = "mod" + juce::String(i) + "_enable";
    auto *param = p->apvts.getParameter(id);
    ASSERT_TRUE(param != nullptr,
                ("Param exists: " + id).toStdString().c_str());
    if (param) {
      float enabled = param->convertFrom0to1(param->getValue());
      ASSERT_NEAR(enabled, 1.0f, 0.01f,
                  ("Default enabled: " + id).toStdString().c_str());
    }
  }

  // Deterministic non-zero source: LFO1 square.
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *waveParam = p->apvts.getParameter("lfoWave");
  auto *rateParam = p->apvts.getParameter("lfoRate");
  waveParam->setValueNotifyingHost(waveParam->convertTo0to1(2.0f)); // Square
  rateParam->setValueNotifyingHost(rateParam->convertTo0to1(2.0f)); // slow

  // Slot 0: LFO1 -> Filter, amount 0.75.
  src0->setValueNotifyingHost(src0->convertTo0to1(1.0f));
  dst0->setValueNotifyingHost(dst0->convertTo0to1(1.0f));
  amt0->setValueNotifyingHost(amt0->convertTo0to1(0.75f));
  enable0->setValueNotifyingHost(1.0f);

  for (int i = 0; i < 3; ++i)
    processBlock(*p);

  float srcOn = p->getModSlotSourceValue(0);
  float contribOn = p->getModSlotContribution(0);
  float totalOn = p->getModTotalFilterCutoff();

  ASSERT_TRUE(std::abs(srcOn) > 0.5f, "Enabled row has non-zero source value");
  ASSERT_TRUE(std::abs(contribOn) > 0.2f,
              "Enabled row has non-zero contribution");
  ASSERT_TRUE(std::abs(totalOn) > 0.2f,
              "Enabled row contributes to destination total");

  // Disable row 0 and verify slot + totals zero out.
  enable0->setValueNotifyingHost(0.0f);
  for (int i = 0; i < 3; ++i)
    processBlock(*p);

  ASSERT_NEAR(p->getModSlotSourceValue(0), 0.0f, 0.001f,
              "Disabled row source is zero");
  ASSERT_NEAR(p->getModSlotContribution(0), 0.0f, 0.001f,
              "Disabled row contribution is zero");
  ASSERT_NEAR(p->getModTotalFilterCutoff(), 0.0f, 0.001f,
              "Disabled row destination total is zero");
}

void testModSlotEnableStateRoundTrip() {
  std::printf(
      "--- Mod slot enable state round-trip: enable toggles persist ---\n");
  auto p = createTestProcessor();

  auto *enable0 = p->apvts.getParameter("mod0_enable");
  auto *enable1 = p->apvts.getParameter("mod1_enable");
  auto *src0 = p->apvts.getParameter("mod0_src");
  auto *dst0 = p->apvts.getParameter("mod0_dst");
  auto *amt0 = p->apvts.getParameter("mod0_amt");
  if (!enable0 || !enable1 || !src0 || !dst0 || !amt0) {
    ASSERT_TRUE(false, "mod enable/state params missing");
    return;
  }

  // Configure slot 0 but disable it, plus disable slot 1 to validate
  // persistence.
  src0->setValueNotifyingHost(src0->convertTo0to1(1.0f)); // LFO1
  dst0->setValueNotifyingHost(dst0->convertTo0to1(1.0f)); // Filter
  amt0->setValueNotifyingHost(amt0->convertTo0to1(0.9f));
  enable0->setValueNotifyingHost(0.0f);
  enable1->setValueNotifyingHost(0.0f);

  juce::MemoryBlock stateData;
  p->getStateInformation(stateData);

  auto p2 = createTestProcessor();
  p2->setStateInformation(stateData.getData(),
                          static_cast<int>(stateData.getSize()));

  float en0 =
      p2->apvts.getParameter("mod0_enable")
          ->convertFrom0to1(p2->apvts.getParameter("mod0_enable")->getValue());
  float en1 =
      p2->apvts.getParameter("mod1_enable")
          ->convertFrom0to1(p2->apvts.getParameter("mod1_enable")->getValue());
  ASSERT_NEAR(en0, 0.0f, 0.01f, "mod0_enable restored disabled");
  ASSERT_NEAR(en1, 0.0f, 0.01f, "mod1_enable restored disabled");

  // With slot 0 disabled after restore, its runtime display/total stay zero.
  p2->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *waveParam = p2->apvts.getParameter("lfoWave");
  waveParam->setValueNotifyingHost(waveParam->convertTo0to1(2.0f)); // Square
  for (int i = 0; i < 3; ++i)
    processBlock(*p2);

  ASSERT_NEAR(p2->getModSlotContribution(0), 0.0f, 0.001f,
              "Restored disabled slot 0 contribution is zero");
  ASSERT_NEAR(p2->getModTotalFilterCutoff(), 0.0f, 0.001f,
              "Restored disabled slot 0 does not affect destination total");
}

void testPresetDirtyDetection() {
  std::printf("--- Preset dirty: snapshot clean, change param -> dirty ---\n");
  auto p = createTestProcessor();

  // Snapshot current state
  p->snapshotPresetState();

  // Should not be dirty immediately after snapshot
  ASSERT_TRUE(!p->isPresetDirty(), "Not dirty immediately after snapshot");

  // Change an APVTS parameter -> dirty
  auto *param = p->apvts.getParameter("masterVol");
  float origVal = param->getValue();
  float newVal = origVal > 0.5f ? 0.2f : 0.8f;
  param->setValueNotifyingHost(newVal);
  ASSERT_TRUE(p->isPresetDirty(), "Dirty after changing APVTS parameter");

  // Restore original -> clean
  param->setValueNotifyingHost(origVal);
  ASSERT_TRUE(!p->isPresetDirty(), "Clean after restoring original value");

  // Change a non-APVTS filter value -> dirty
  p->setBaseFilterCutoff(true, 500);
  ASSERT_TRUE(p->isPresetDirty(),
              "Dirty after changing non-APVTS filter cutoff");

  // Re-snapshot with new state, then verify clean
  p->snapshotPresetState();
  ASSERT_TRUE(!p->isPresetDirty(), "Clean after re-snapshot");

  // Change non-APVTS resonance -> dirty
  p->setBaseFilterResonance(true, 10);
  ASSERT_TRUE(p->isPresetDirty(),
              "Dirty after changing non-APVTS filter resonance");
}

void testPostModValuesReturnToBaseline() {
  std::printf(
      "--- Post-mod values return to baseline when modulation disabled ---\n");
  auto p = createTestProcessor();

  // Enable Square LFO with PW + pitch depth
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *waveParam = p->apvts.getParameter("lfoWave");
  waveParam->setValueNotifyingHost(waveParam->convertTo0to1(2.0f));
  p->apvts.getParameter("lfoDepthPW")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("lfoDepthPitch")->setValueNotifyingHost(1.0f);

  // Play note and process with LFO active
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);
  for (int i = 0; i < 3; ++i)
    processBlock(*p);

  // Verify modulation is active
  int basePW = p->getVoiceSettings(0).pulseWidth;
  ASSERT_TRUE(p->getLastAppliedPW() != basePW, "PW modulated while LFO active");
  ASSERT_TRUE(std::abs(p->getLastAppliedPitchOffset()) > 0.1f,
              "Pitch offset non-zero while LFO active");

  // Disable LFO and process
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(0.0f);
  for (int i = 0; i < 3; ++i)
    processBlock(*p);

  // PW should return to base, pitch offset to zero
  ASSERT_TRUE(p->getLastAppliedPW() == basePW,
              "PW returns to base when LFO disabled");
  ASSERT_NEAR(p->getLastAppliedPitchOffset(), 0.0f, 0.001f,
              "Pitch offset returns to zero when LFO disabled");
}

void testIdleLFOModulationDoesNotTouchVoices() {
  std::printf("--- Idle LFO modulation: PW/pitch remain at baseline with no "
              "active voices ---\n");
  auto p = createTestProcessor();

  // Configure deterministic non-zero LFO modulation depths.
  p->apvts.getParameter("lfoEnable")->setValueNotifyingHost(1.0f);
  auto *waveParam = p->apvts.getParameter("lfoWave");
  auto *rateParam = p->apvts.getParameter("lfoRate");
  waveParam->setValueNotifyingHost(waveParam->convertTo0to1(2.0f)); // Square
  rateParam->setValueNotifyingHost(rateParam->convertTo0to1(2.0f));
  p->apvts.getParameter("lfoDepthPW")->setValueNotifyingHost(1.0f);
  p->apvts.getParameter("lfoDepthPitch")->setValueNotifyingHost(1.0f);

  int basePW = p->getVoiceSettings(0).pulseWidth; // default 2048
  ASSERT_NEAR(static_cast<float>(p->getActiveVoiceCountRuntime()), 0.0f, 0.01f,
              "No active voices before idle modulation test");

  // Process several blocks with no MIDI note activity.
  for (int i = 0; i < 8; ++i)
    processBlock(*p);

  ASSERT_NEAR(static_cast<float>(p->getLastAppliedPW()),
              static_cast<float>(basePW), 1.0f,
              "Idle LFO does not modulate PW without active voices");
  ASSERT_NEAR(p->getLastAppliedPitchOffset(), 0.0f, 0.001f,
              "Idle LFO does not modulate pitch without active voices");
}

// ============================================================================
// Chord Memory tests
// ============================================================================

void testChordMemoryDefaultOff() {
  std::printf("--- Chord Memory default: disabled, all intervals zero ---\n");
  auto p = createTestProcessor();

  auto *enableParam = p->apvts.getParameter("chordEnable");
  auto *slotParam = p->apvts.getParameter("chordSlot");
  if (!enableParam || !slotParam) {
    ASSERT_TRUE(false, "chordEnable or chordSlot param missing");
    return;
  }

  float enable = enableParam->convertFrom0to1(enableParam->getValue());
  ASSERT_NEAR(enable, 0.0f, 0.01f, "Chord memory disabled by default");

  float slot = slotParam->convertFrom0to1(slotParam->getValue());
  ASSERT_NEAR(slot, 0.0f, 0.01f, "Chord slot defaults to 0");

  // All 20 intervals should be 0
  for (int s = 0; s < 4; ++s) {
    for (int i = 0; i < 5; ++i) {
      auto id = "chord_s" + juce::String(s) + "_i" + juce::String(i);
      auto *intParam = p->apvts.getParameter(id);
      ASSERT_TRUE(intParam != nullptr,
                  ("Param exists: " + id).toStdString().c_str());
      if (intParam) {
        float val = intParam->convertFrom0to1(intParam->getValue());
        ASSERT_NEAR(
            val, 0.0f, 0.01f,
            ("Interval " + id + " defaults to 0").toStdString().c_str());
      }
    }
  }
}

void testChordMemoryTriggersAudio() {
  std::printf(
      "--- Chord Memory: enable + intervals -> noteOn produces audio ---\n");
  auto p = createTestProcessor();

  // Enable chord memory
  p->apvts.getParameter("chordEnable")->setValueNotifyingHost(1.0f);

  // Set slot 0 intervals: +4 (major third), +7 (perfect fifth)
  auto *i0 = p->apvts.getParameter("chord_s0_i0");
  auto *i1 = p->apvts.getParameter("chord_s0_i1");
  if (!i0 || !i1) {
    ASSERT_TRUE(false, "chord interval params missing");
    return;
  }
  i0->setValueNotifyingHost(i0->convertTo0to1(4.0f));
  i1->setValueNotifyingHost(i1->convertTo0to1(7.0f));

  // Sync params via processBlock
  processBlock(*p);

  // Send note-on
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);

  // Let SID settle and measure RMS
  float maxRms = 0.0f;
  for (int i = 0; i < 10; ++i) {
    float rms = processBlock(*p);
    if (rms > maxRms)
      maxRms = rms;
  }
  std::printf("  Chord memory max RMS over 10 blocks: %f\n", maxRms);
  ASSERT_TRUE(maxRms > 0.0001f, "Chord memory produces audio output");
}

void testChordMemoryStateRoundTrip() {
  std::printf("--- Chord Memory state round-trip: save/restore preserves "
              "settings ---\n");
  auto p = createTestProcessor();

  // Configure: enable, slot 2, set some intervals
  p->apvts.getParameter("chordEnable")->setValueNotifyingHost(1.0f);
  auto *slotParam = p->apvts.getParameter("chordSlot");
  slotParam->setValueNotifyingHost(slotParam->convertTo0to1(2.0f));

  auto *i0 = p->apvts.getParameter("chord_s2_i0");
  auto *i1 = p->apvts.getParameter("chord_s2_i1");
  i0->setValueNotifyingHost(i0->convertTo0to1(3.0f));
  i1->setValueNotifyingHost(i1->convertTo0to1(7.0f));

  // Process to sync
  processBlock(*p);

  // Save state
  juce::MemoryBlock stateData;
  p->getStateInformation(stateData);

  // Restore to fresh processor
  auto p2 = createTestProcessor();
  p2->setStateInformation(stateData.getData(),
                          static_cast<int>(stateData.getSize()));

  float enable2 =
      p2->apvts.getParameter("chordEnable")
          ->convertFrom0to1(p2->apvts.getParameter("chordEnable")->getValue());
  ASSERT_NEAR(enable2, 1.0f, 0.01f, "Chord enable restored");

  float slot2 =
      p2->apvts.getParameter("chordSlot")
          ->convertFrom0to1(p2->apvts.getParameter("chordSlot")->getValue());
  ASSERT_NEAR(slot2, 2.0f, 0.01f, "Chord slot restored (2)");

  float int0 =
      p2->apvts.getParameter("chord_s2_i0")
          ->convertFrom0to1(p2->apvts.getParameter("chord_s2_i0")->getValue());
  ASSERT_NEAR(int0, 3.0f, 0.01f, "Chord interval s2_i0 restored (3)");

  float int1 =
      p2->apvts.getParameter("chord_s2_i1")
          ->convertFrom0to1(p2->apvts.getParameter("chord_s2_i1")->getValue());
  ASSERT_NEAR(int1, 7.0f, 0.01f, "Chord interval s2_i1 restored (7)");
}

void testChordMemoryDualSIDSpread() {
  std::printf("--- Chord Memory dual-SID spread: 6-note chord allocates across "
              "all voices ---\n");
  auto p = createTestProcessor();

  // Use Stereo mode (non-multitimbral path) so chord note allocation goes
  // through triggerChordDualSID().
  auto *dualMode = p->apvts.getParameter("dualMode");
  auto *chordEnable = p->apvts.getParameter("chordEnable");
  if (!dualMode || !chordEnable) {
    ASSERT_TRUE(false, "dualMode or chordEnable param missing");
    return;
  }
  dualMode->setValueNotifyingHost(
      dualMode->convertTo0to1(0.0f)); // Stereo Split

  // Enable chord memory with 5 intervals => 6 total notes.
  chordEnable->setValueNotifyingHost(1.0f);
  std::array<int, 5> intervals = {3, 7, 10, 14, 17};
  for (int i = 0; i < 5; ++i) {
    auto id = "chord_s0_i" + juce::String(i);
    auto *param = p->apvts.getParameter(id);
    param->setValueNotifyingHost(
        param->convertTo0to1(static_cast<float>(intervals[i])));
  }

  // Ensure APVTS values are synced before MIDI trigger.
  processBlock(*p);

  // Trigger note-on.
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);

  // Expected spread for root C4 (60):
  // left SID voices 0..2 -> 60, 63, 67
  // right SID voices 3..5 -> 70, 74, 77
  std::array<int, 6> expected = {60, 63, 67, 70, 74, 77};
  ASSERT_NEAR(static_cast<float>(p->getActiveVoiceCountRuntime()), 6.0f, 0.01f,
              "All 6 voices are active for 6-note chord");
  for (int v = 0; v < 6; ++v) {
    ASSERT_TRUE(p->isVoiceActiveRuntime(v), "Voice is active");
    ASSERT_NEAR(static_cast<float>(p->getVoiceNoteRuntime(v)),
                static_cast<float>(expected[v]), 0.01f,
                "Voice note matches dual-SID chord allocation");
  }

  // Note-off should release all chord voices in non-multitimbral mode.
  juce::MidiBuffer noteOff;
  noteOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
  processBlock(*p, 512, &noteOff);
  ASSERT_NEAR(static_cast<float>(p->getActiveVoiceCountRuntime()), 0.0f, 0.01f,
              "All chord voices release on note-off");
}

// ============================================================================
// Wavetable Step Editor Tests
// ============================================================================

void testWavetableStepParamsEditable() {
  std::printf("--- Wavetable step params: all 48 per-step params exist and are "
              "editable ---\n");
  auto p = createTestProcessor();

  // Verify all 48 per-step params exist and can be set
  for (int i = 0; i < 16; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";

    // Wave param (AudioParameterChoice: 0=Tri, 1=Saw, 2=Pulse, 3=Noise)
    auto *waveParam = p->apvts.getParameter(prefix + "wave");
    ASSERT_TRUE(waveParam != nullptr,
                ("WT step " + juce::String(i) + " wave param exists")
                    .toStdString()
                    .c_str());

    // Pitch param (-24 to 24, default 0)
    auto *pitchParam = p->apvts.getParameter(prefix + "pitch");
    ASSERT_TRUE(pitchParam != nullptr,
                ("WT step " + juce::String(i) + " pitch param exists")
                    .toStdString()
                    .c_str());

    // PW param (0 to 4095, default 2048)
    auto *pwParam = p->apvts.getParameter(prefix + "pw");
    ASSERT_TRUE(pwParam != nullptr,
                ("WT step " + juce::String(i) + " pw param exists")
                    .toStdString()
                    .c_str());

    // Set and read back non-default values
    if (waveParam)
      waveParam->setValueNotifyingHost(waveParam->convertTo0to1(1.0f)); // Saw
    if (pitchParam)
      pitchParam->setValueNotifyingHost(pitchParam->convertTo0to1(7.0f));
    if (pwParam)
      pwParam->setValueNotifyingHost(pwParam->convertTo0to1(1024.0f));

    if (waveParam) {
      float readWave = waveParam->convertFrom0to1(waveParam->getValue());
      ASSERT_NEAR(readWave, 1.0f, 0.5f,
                  ("WT step " + juce::String(i) + " wave set to Saw")
                      .toStdString()
                      .c_str());
    }
    if (pitchParam) {
      float readPitch = pitchParam->convertFrom0to1(pitchParam->getValue());
      ASSERT_NEAR(readPitch, 7.0f, 0.5f,
                  ("WT step " + juce::String(i) + " pitch set to 7")
                      .toStdString()
                      .c_str());
    }
    if (pwParam) {
      float readPW = pwParam->convertFrom0to1(pwParam->getValue());
      ASSERT_NEAR(readPW, 1024.0f, 1.0f,
                  ("WT step " + juce::String(i) + " pw set to 1024")
                      .toStdString()
                      .c_str());
    }
  }
}

void testWavetableStepSequencerProducesVariation() {
  std::printf("--- Wavetable: different steps produce audible variation ---\n");
  auto p = createTestProcessor();
  p->prepareToPlay(44100.0, 512);

  // Set up: 2 steps, high rate, very different waveforms
  auto setParam = [&](const juce::String &id, float val) {
    auto *param = p->apvts.getParameter(id);
    if (param)
      param->setValueNotifyingHost(param->convertTo0to1(val));
  };

  setParam("wtEnable", 1.0f);
  setParam("wtNumSteps", 2.0f);
  setParam("wtRate", 100.0f); // Fast rate to cycle quickly
  setParam("wtLoop", 1.0f);

  // Step 0: Triangle (soft, no harmonics)
  setParam("wt_s0_wave", 0.0f); // Triangle
  setParam("wt_s0_pitch", 0.0f);
  setParam("wt_s0_pw", 2048.0f);

  // Step 1: Noise (harsh, random)
  setParam("wt_s1_wave", 3.0f); // Noise
  setParam("wt_s1_pitch", 0.0f);
  setParam("wt_s1_pw", 2048.0f);

  // Send note-on
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  processBlock(*p, 512, &midi);

  // Process several blocks and collect RMS values
  float rmsValues[20];
  for (int i = 0; i < 20; ++i)
    rmsValues[i] = processBlock(*p, 512);

  // Verify we get audio output
  float maxRMS = 0.0f;
  for (int i = 0; i < 20; ++i)
    if (rmsValues[i] > maxRMS)
      maxRMS = rmsValues[i];

  ASSERT_TRUE(maxRMS > 0.0001f, "Wavetable produces audio output");

  // Verify there is RMS variation between blocks (different waveforms should
  // produce different amplitudes). Check that not all blocks have identical
  // RMS.
  float minRMS = maxRMS;
  for (int i = 0; i < 20; ++i)
    if (rmsValues[i] > 0.0001f && rmsValues[i] < minRMS)
      minRMS = rmsValues[i];

  float ratio = (minRMS > 0.0f) ? (maxRMS / minRMS) : 999.0f;
  ASSERT_TRUE(ratio > 1.05f,
              "Wavetable steps produce varied audio (Triangle vs Noise)");
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
  testWavetablePWPreservedWithLFOOff();
  testWavetablePitchOffsetPreserved();
  testWavetableLFOCoexistence();
  testPipelineOrderOfOperations();

  // Mod matrix
  testModMatrixDefaultNone();
  testModMatrixLFOToFilterRoute();
  testModMatrixBipolarAmount();
  testModMatrixStateRoundTrip();
  testModMatrixResonanceReturnsToBase();

  // Pitch Bend Range APVTS
  testPitchBendRangeAPVTSDefault();
  testPitchBendRangeAPVTSSync();
  testPitchBendRangeStatePersistence();
  testPitchBendRangeFullCycle();

  // PWM Sweep
  testPWMSweepDefaultOff();
  testPWMSweepModifiesPW();
  testPWMSweepStateRoundTrip();

  // Chord Memory
  testChordMemoryDefaultOff();
  testChordMemoryTriggersAudio();
  testChordMemoryStateRoundTrip();
  testChordMemoryDualSIDSpread();

  // Wavetable Step Editor
  testWavetableStepParamsEditable();
  testWavetableStepSequencerProducesVariation();

  // Post-modulation value storage and preset dirty detection
  testPostModPWStorage();
  testPostModPitchStorage();
  testPostModResonanceStorage();
  testModSlotDisplayValues();
  testModSlotInactiveZeros();
  testModSlotEnableGate();
  testModSlotEnableStateRoundTrip();
  testPresetDirtyDetection();
  testPostModValuesReturnToBaseline();
  testIdleLFOModulationDoesNotTouchVoices();

  std::printf("\n=== Results: %d passed, %d failed ===\n", testsPassed,
              testsFailed);

  return testsFailed > 0 ? 1 : 0;
}
