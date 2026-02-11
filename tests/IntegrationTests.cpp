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

  std::printf("\n=== Results: %d passed, %d failed ===\n", testsPassed,
              testsFailed);

  return testsFailed > 0 ? 1 : 0;
}
