// LFO and Oscillator Mathematical Test Suite
// Tests waveform generation, phase accumulation, modulation ranges,
// and boundary conditions without requiring audio playback.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

// ============================================================================
// Standalone LFO math (mirrors BreadbinProcessor::processLFO logic)
// Extracted here so tests don't depend on JUCE.
// ============================================================================

enum class LFOWaveform { Triangle, Sawtooth, Square, SampleAndHold };

struct LFOState {
  bool enabled = false;
  LFOWaveform waveform = LFOWaveform::Triangle;
  float rate = 2.0f;
  float depthFilter = 0.0f;
  float depthPulseWidth = 0.0f;
  float depthPitch = 0.0f;
  double phase = 0.0;
  float currentValue = 0.0f;
  float shValue = 0.0f;
};

// Mirrors the processLFO function exactly
void processLFO(LFOState &lfo, int numSamples, double sampleRate) {
  double phaseInc = (static_cast<double>(lfo.rate) * numSamples) / sampleRate;
  double oldPhase = lfo.phase;
  lfo.phase += phaseInc;
  lfo.phase -= std::floor(lfo.phase);

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
    if (lfo.phase < oldPhase) {
      static std::mt19937 rng(42); // Fixed seed for deterministic tests
      std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
      lfo.shValue = dist(rng);
    }
    lfo.currentValue = lfo.shValue;
    break;
  }
}

// ============================================================================
// Test helpers
// ============================================================================

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_NEAR(actual, expected, epsilon, msg)                            \
  do {                                                                         \
    double _a = (actual);                                                      \
    double _e = (expected);                                                    \
    double _eps = (epsilon);                                                   \
    if (std::abs(_a - _e) > _eps) {                                            \
      std::printf("  FAIL: %s\n    expected: %.6f, got: %.6f (eps: %.6f)\n",   \
                  msg, _e, _a, _eps);                                          \
      testsFailed++;                                                           \
    } else {                                                                   \
      testsPassed++;                                                           \
    }                                                                          \
  } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  FAIL: %s\n", msg);                                        \
      testsFailed++;                                                           \
    } else {                                                                   \
      testsPassed++;                                                           \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(actual, expected, msg)                                       \
  do {                                                                         \
    auto _a = (actual);                                                        \
    auto _e = (expected);                                                      \
    if (_a != _e) {                                                            \
      std::printf("  FAIL: %s (expected %d, got %d)\n", msg,                   \
                  static_cast<int>(_e), static_cast<int>(_a));                 \
      testsFailed++;                                                           \
    } else {                                                                   \
      testsPassed++;                                                           \
    }                                                                          \
  } while (0)

// ============================================================================
// Triangle waveform tests
// ============================================================================

void testTriangleWaveform() {
  std::printf("--- Triangle Waveform ---\n");

  // Triangle at phase 0.0 should be -1.0 (start of upward ramp)
  LFOState lfo;
  lfo.waveform = LFOWaveform::Triangle;
  lfo.phase = 0.0;
  float p = 0.0f;
  float val = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
  ASSERT_NEAR(val, -1.0f, 0.001f, "Triangle at phase 0.0 = -1.0");

  // Triangle at phase 0.25 should be 0.0
  p = 0.25f;
  val = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
  ASSERT_NEAR(val, 0.0f, 0.001f, "Triangle at phase 0.25 = 0.0");

  // Triangle at phase 0.5 should be +1.0 (peak)
  // Note: at exactly 0.5, the formula uses the second branch: 3 - 4*0.5 = 1.0
  p = 0.5f;
  val = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
  ASSERT_NEAR(val, 1.0f, 0.001f, "Triangle at phase 0.5 = +1.0");

  // Triangle at phase 0.75 should be 0.0
  p = 0.75f;
  val = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
  ASSERT_NEAR(val, 0.0f, 0.001f, "Triangle at phase 0.75 = 0.0");

  // Triangle at phase 1.0 (wraps to 0.0) should be -1.0
  p = 0.0f; // After wrap
  val = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
  ASSERT_NEAR(val, -1.0f, 0.001f, "Triangle at phase 1.0 (wrap) = -1.0");

  // Verify symmetry: value at 0.1 should equal value at 0.9
  float p1 = 0.1f, p2 = 0.9f;
  float v1 = (p1 < 0.5f) ? (4.0f * p1 - 1.0f) : (3.0f - 4.0f * p1);
  float v2 = (p2 < 0.5f) ? (4.0f * p2 - 1.0f) : (3.0f - 4.0f * p2);
  ASSERT_NEAR(v1, v2, 0.001f, "Triangle symmetric: f(0.1) == f(0.9)");

  // Range check: triangle should never exceed [-1, +1]
  bool inRange = true;
  for (int i = 0; i <= 1000; ++i) {
    float pp = static_cast<float>(i) / 1000.0f;
    float vv = (pp < 0.5f) ? (4.0f * pp - 1.0f) : (3.0f - 4.0f * pp);
    if (vv < -1.001f || vv > 1.001f) {
      inRange = false;
      break;
    }
  }
  ASSERT_TRUE(inRange, "Triangle range always in [-1, +1]");
}

// ============================================================================
// Sawtooth waveform tests
// ============================================================================

void testSawtoothWaveform() {
  std::printf("--- Sawtooth Waveform ---\n");

  // Saw at phase 0.0: 2*0 - 1 = -1.0
  float p = 0.0f;
  float val = 2.0f * p - 1.0f;
  ASSERT_NEAR(val, -1.0f, 0.001f, "Sawtooth at phase 0.0 = -1.0");

  // Saw at phase 0.5: 2*0.5 - 1 = 0.0
  p = 0.5f;
  val = 2.0f * p - 1.0f;
  ASSERT_NEAR(val, 0.0f, 0.001f, "Sawtooth at phase 0.5 = 0.0");

  // Saw at phase ~1.0: approaches +1.0
  p = 0.999f;
  val = 2.0f * p - 1.0f;
  ASSERT_NEAR(val, 0.998f, 0.001f, "Sawtooth at phase 0.999 ~ +1.0");

  // Linearity check: slope should be constant (2.0 per unit phase)
  float p1 = 0.2f, p2 = 0.7f;
  float v1 = 2.0f * p1 - 1.0f;
  float v2 = 2.0f * p2 - 1.0f;
  float slope = (v2 - v1) / (p2 - p1);
  ASSERT_NEAR(slope, 2.0f, 0.001f, "Sawtooth slope is 2.0");

  // Range check
  bool inRange = true;
  for (int i = 0; i <= 1000; ++i) {
    float pp = static_cast<float>(i) / 1000.0f;
    float vv = 2.0f * pp - 1.0f;
    if (vv < -1.001f || vv > 1.001f) {
      inRange = false;
      break;
    }
  }
  ASSERT_TRUE(inRange, "Sawtooth range always in [-1, +1]");
}

// ============================================================================
// Square waveform tests
// ============================================================================

void testSquareWaveform() {
  std::printf("--- Square Waveform ---\n");

  // Square at phase 0.0: +1.0
  float p = 0.0f;
  float val = (p < 0.5f) ? 1.0f : -1.0f;
  ASSERT_NEAR(val, 1.0f, 0.001f, "Square at phase 0.0 = +1.0");

  // Square at phase 0.49: still +1.0
  p = 0.49f;
  val = (p < 0.5f) ? 1.0f : -1.0f;
  ASSERT_NEAR(val, 1.0f, 0.001f, "Square at phase 0.49 = +1.0");

  // Square at phase 0.5: -1.0
  p = 0.5f;
  val = (p < 0.5f) ? 1.0f : -1.0f;
  ASSERT_NEAR(val, -1.0f, 0.001f, "Square at phase 0.5 = -1.0");

  // Square at phase 0.99: -1.0
  p = 0.99f;
  val = (p < 0.5f) ? 1.0f : -1.0f;
  ASSERT_NEAR(val, -1.0f, 0.001f, "Square at phase 0.99 = -1.0");

  // 50% duty cycle: count +1 vs -1 over 1000 steps
  int posCount = 0, negCount = 0;
  for (int i = 0; i < 1000; ++i) {
    float pp = static_cast<float>(i) / 1000.0f;
    if (pp < 0.5f)
      posCount++;
    else
      negCount++;
  }
  ASSERT_EQ(posCount, 500, "Square duty cycle: 500 positive samples");
  ASSERT_EQ(negCount, 500, "Square duty cycle: 500 negative samples");
}

// ============================================================================
// Phase accumulator tests
// ============================================================================

void testPhaseAccumulator() {
  std::printf("--- Phase Accumulator ---\n");

  const double sampleRate = 44100.0;
  const int blockSize = 512;

  // 1 Hz LFO: verify phase advances correctly over many blocks
  LFOState lfo;
  lfo.rate = 1.0f;
  lfo.waveform = LFOWaveform::Triangle;
  lfo.phase = 0.0;

  int blocksPerSecond = static_cast<int>(sampleRate / blockSize);
  for (int i = 0; i < blocksPerSecond; ++i) {
    processLFO(lfo, blockSize, sampleRate);
  }
  // Exact expected phase: (rate * blocks * blockSize) / sampleRate, mod 1.0
  double expectedPhase = (1.0 * blocksPerSecond * blockSize) / sampleRate;
  expectedPhase -= std::floor(expectedPhase);
  ASSERT_NEAR(lfo.phase, expectedPhase, 0.001,
              "1Hz LFO: phase matches analytical value");

  // 10 Hz: verify phase accumulation is rate-proportional
  lfo.rate = 10.0f;
  lfo.phase = 0.0;
  for (int i = 0; i < blocksPerSecond; ++i) {
    processLFO(lfo, blockSize, sampleRate);
  }
  expectedPhase = (10.0 * blocksPerSecond * blockSize) / sampleRate;
  expectedPhase -= std::floor(expectedPhase);
  ASSERT_NEAR(lfo.phase, expectedPhase, 0.001,
              "10Hz LFO: phase matches analytical value");

  // Phase should always stay in [0, 1)
  lfo.rate = 20.0f;
  lfo.phase = 0.0;
  bool phaseValid = true;
  for (int i = 0; i < 10000; ++i) {
    processLFO(lfo, blockSize, sampleRate);
    if (lfo.phase < 0.0 || lfo.phase >= 1.0) {
      phaseValid = false;
      break;
    }
  }
  ASSERT_TRUE(phaseValid, "Phase always in [0, 1) over 10000 blocks");
}

// ============================================================================
// Sample & Hold tests
// ============================================================================

void testSampleAndHold() {
  std::printf("--- Sample & Hold ---\n");

  const double sampleRate = 44100.0;
  const int blockSize = 512;

  LFOState lfo;
  lfo.rate = 1.0f;
  lfo.waveform = LFOWaveform::SampleAndHold;
  lfo.phase = 0.0;
  lfo.shValue = 0.0f;

  // Process enough to trigger several wraps
  float lastValue = 0.0f;
  int changes = 0;
  int totalBlocks = static_cast<int>((sampleRate / blockSize) * 5); // 5 seconds

  for (int i = 0; i < totalBlocks; ++i) {
    processLFO(lfo, blockSize, sampleRate);
    if (i > 0 && lfo.currentValue != lastValue) {
      changes++;
    }
    lastValue = lfo.currentValue;
    // Value should always be in [-1, +1]
    ASSERT_TRUE(lfo.currentValue >= -1.0f && lfo.currentValue <= 1.0f,
                "S&H value in [-1, +1]");
  }

  // At 1 Hz over 5 seconds, should have ~5 value changes (one per cycle wrap)
  ASSERT_TRUE(changes >= 3 && changes <= 7,
              "S&H: ~5 value changes over 5 seconds at 1Hz");

  // Value should be held (constant) between wraps
  lfo.rate = 0.5f; // Slow: 0.5 Hz = 2 second cycle
  lfo.phase = 0.1; // Start mid-cycle
  processLFO(lfo, blockSize, sampleRate);
  float held = lfo.currentValue;

  // Process a few more blocks (shouldn't wrap yet)
  bool isHeld = true;
  for (int i = 0; i < 10; ++i) {
    processLFO(lfo, blockSize, sampleRate);
    if (lfo.currentValue != held) {
      isHeld = false;
      break;
    }
  }
  ASSERT_TRUE(isHeld, "S&H holds value between wraps");
}

// ============================================================================
// Modulation range tests
// ============================================================================

void testModulationRanges() {
  std::printf("--- Modulation Ranges ---\n");

  // Filter cutoff modulation: baseCutoff ± (depth * 1024), clamped 0-2047
  int baseCutoff = 1024;
  float depth = 1.0f;
  float lfoVal = 1.0f; // Max positive

  int modAmount = static_cast<int>(lfoVal * depth * 1024.0f);
  int result = std::clamp(baseCutoff + modAmount, 0, 2047);
  ASSERT_EQ(result, 2047, "Filter mod: max positive clamps to 2047");

  lfoVal = -1.0f;
  modAmount = static_cast<int>(lfoVal * depth * 1024.0f);
  result = std::clamp(baseCutoff + modAmount, 0, 2047);
  ASSERT_EQ(result, 0, "Filter mod: max negative clamps to 0");

  // Half depth
  lfoVal = 1.0f;
  depth = 0.5f;
  modAmount = static_cast<int>(lfoVal * depth * 1024.0f);
  result = std::clamp(baseCutoff + modAmount, 0, 2047);
  ASSERT_EQ(result, 1536, "Filter mod: 50% depth, +1.0 LFO = 1536");

  // Pulse width modulation: basePW ± (depth * 2048), clamped 0-4095
  int basePW = 2048;
  depth = 1.0f;
  lfoVal = 1.0f;
  int pwMod = static_cast<int>(lfoVal * depth * 2048.0f);
  result = std::clamp(basePW + pwMod, 0, 4095);
  ASSERT_EQ(result, 4095, "PW mod: max positive = 4095 (clamped)");

  lfoVal = -1.0f;
  pwMod = static_cast<int>(lfoVal * depth * 2048.0f);
  result = std::clamp(basePW + pwMod, 0, 4095);
  ASSERT_EQ(result, 0, "PW mod: max negative = 0");

  // Pitch modulation: ±2 semitones max
  depth = 1.0f;
  lfoVal = 1.0f;
  float semitoneMod = lfoVal * depth * 2.0f;
  ASSERT_NEAR(semitoneMod, 2.0f, 0.001f, "Pitch mod: max = +2 semitones");

  lfoVal = -1.0f;
  semitoneMod = lfoVal * depth * 2.0f;
  ASSERT_NEAR(semitoneMod, -2.0f, 0.001f, "Pitch mod: min = -2 semitones");

  // Pitch Hz calculation: A4 = 440 Hz, +2 semitones = ~493.88 Hz
  double baseHz = 440.0;
  double modHz = baseHz * std::pow(2.0, 2.0 / 12.0);
  ASSERT_NEAR(modHz, 493.88, 0.1, "Pitch +2 semitones: 440 -> 493.88 Hz");

  modHz = baseHz * std::pow(2.0, -2.0 / 12.0);
  ASSERT_NEAR(modHz, 392.0, 0.1, "Pitch -2 semitones: 440 -> 392.0 Hz");

  // Zero depth should produce no modulation
  depth = 0.0f;
  lfoVal = 1.0f;
  modAmount = static_cast<int>(lfoVal * depth * 1024.0f);
  ASSERT_EQ(modAmount, 0, "Zero depth = zero modulation");
}

// ============================================================================
// Edge case tests
// ============================================================================

void testEdgeCases() {
  std::printf("--- Edge Cases ---\n");

  const double sampleRate = 44100.0;

  // Very small block size (1 sample)
  LFOState lfo;
  lfo.rate = 10.0f;
  lfo.waveform = LFOWaveform::Triangle;
  lfo.phase = 0.0;

  bool valid = true;
  for (int i = 0; i < 44100; ++i) { // 1 second at 1 sample per block
    processLFO(lfo, 1, sampleRate);
    if (lfo.currentValue < -1.001f || lfo.currentValue > 1.001f) {
      valid = false;
      break;
    }
  }
  ASSERT_TRUE(valid, "Single-sample blocks: output in range");

  // Very large block size (4096 samples)
  lfo.phase = 0.0;
  lfo.rate = 5.0f;
  valid = true;
  for (int i = 0; i < 100; ++i) {
    processLFO(lfo, 4096, sampleRate);
    if (lfo.currentValue < -1.001f || lfo.currentValue > 1.001f ||
        lfo.phase < 0.0 || lfo.phase >= 1.0) {
      valid = false;
      break;
    }
  }
  ASSERT_TRUE(valid, "Large block size: output/phase in range");

  // Minimum rate (0.1 Hz)
  lfo.rate = 0.1f;
  lfo.phase = 0.0;
  processLFO(lfo, 512, sampleRate);
  ASSERT_TRUE(lfo.phase > 0.0, "Min rate: phase advances");
  ASSERT_TRUE(lfo.phase < 0.01, "Min rate: phase advances slowly");

  // Maximum rate (20 Hz)
  lfo.rate = 20.0f;
  lfo.phase = 0.0;
  processLFO(lfo, 512, sampleRate);
  ASSERT_TRUE(lfo.phase > 0.0, "Max rate: phase advances");

  // Rate exactly at Nyquist boundary (sampleRate / 2 / blockSize)
  // This tests that we don't alias or break with fast rates
  lfo.rate = 20.0f;
  lfo.phase = 0.0;
  valid = true;
  for (int i = 0; i < 1000; ++i) {
    processLFO(lfo, 64, sampleRate);
    if (lfo.phase < 0.0 || lfo.phase >= 1.0) {
      valid = false;
      break;
    }
  }
  ASSERT_TRUE(valid, "High rate + small blocks: phase valid");
}

// ============================================================================
// Continuity test — triangle and sawtooth should be continuous
// ============================================================================

void testContinuity() {
  std::printf("--- Waveform Continuity ---\n");

  const double sampleRate = 44100.0;
  const int blockSize = 32; // Small blocks for finer resolution

  // Triangle continuity: adjacent samples should be close
  LFOState lfo;
  lfo.rate = 5.0f;
  lfo.waveform = LFOWaveform::Triangle;
  lfo.phase = 0.0;

  float maxDelta = 0.0f;
  float prevValue = 0.0f;
  processLFO(lfo, blockSize, sampleRate);
  prevValue = lfo.currentValue;

  for (int i = 1; i < 1000; ++i) {
    processLFO(lfo, blockSize, sampleRate);
    float delta = std::abs(lfo.currentValue - prevValue);
    if (delta > maxDelta)
      maxDelta = delta;
    prevValue = lfo.currentValue;
  }

  // At 5Hz, 32 samples per block at 44100, phase step = 5*32/44100 = 0.00363
  // Triangle slope = 4, so max delta per block = 4 * 0.00363 = 0.0145
  ASSERT_TRUE(maxDelta < 0.05f,
              "Triangle continuity: max delta between blocks < 0.05");

  // Sawtooth continuity (except at wrap point)
  lfo.waveform = LFOWaveform::Sawtooth;
  lfo.phase = 0.0;
  maxDelta = 0.0f;
  processLFO(lfo, blockSize, sampleRate);
  prevValue = lfo.currentValue;
  int largeJumps = 0;

  for (int i = 1; i < 1000; ++i) {
    processLFO(lfo, blockSize, sampleRate);
    float delta = std::abs(lfo.currentValue - prevValue);
    if (delta > 0.5f)
      largeJumps++; // Wrap points
    else if (delta > maxDelta)
      maxDelta = delta;
    prevValue = lfo.currentValue;
  }

  ASSERT_TRUE(maxDelta < 0.05f, "Sawtooth continuity: smooth between wraps");
  ASSERT_TRUE(largeJumps >= 3 && largeJumps <= 7,
              "Sawtooth wraps: ~5 large jumps at 5Hz over ~0.7s");
}

// ============================================================================
// All waveform range sweep test
// ============================================================================

void testAllWaveformRanges() {
  std::printf("--- All Waveform Full Range Sweep ---\n");

  const double sampleRate = 44100.0;
  const int blockSize = 64;

  LFOWaveform waveforms[] = {LFOWaveform::Triangle, LFOWaveform::Sawtooth,
                             LFOWaveform::Square, LFOWaveform::SampleAndHold};
  const char *names[] = {"Triangle", "Sawtooth", "Square", "S&H"};

  for (int w = 0; w < 4; ++w) {
    LFOState lfo;
    lfo.rate = 5.0f;
    lfo.waveform = waveforms[w];
    lfo.phase = 0.0;

    float minVal = 2.0f, maxVal = -2.0f;
    for (int i = 0; i < 5000; ++i) {
      processLFO(lfo, blockSize, sampleRate);
      if (lfo.currentValue < minVal)
        minVal = lfo.currentValue;
      if (lfo.currentValue > maxVal)
        maxVal = lfo.currentValue;
    }

    char msg[64];
    std::snprintf(msg, sizeof(msg), "%s reaches near -1.0", names[w]);
    ASSERT_TRUE(minVal <= -0.9f, msg);

    std::snprintf(msg, sizeof(msg), "%s reaches near +1.0", names[w]);
    ASSERT_TRUE(maxVal >= 0.9f, msg);

    std::snprintf(msg, sizeof(msg), "%s never exceeds [-1, +1]", names[w]);
    ASSERT_TRUE(minVal >= -1.001f && maxVal <= 1.001f, msg);
  }
}

// ============================================================================
// Main
// ============================================================================

int main() {
  std::printf("=== Breadbin LFO & Oscillator Test Suite ===\n\n");

  testTriangleWaveform();
  testSawtoothWaveform();
  testSquareWaveform();
  testPhaseAccumulator();
  testSampleAndHold();
  testModulationRanges();
  testEdgeCases();
  testContinuity();
  testAllWaveformRanges();

  std::printf("\n=== Results: %d passed, %d failed ===\n", testsPassed,
              testsFailed);

  return testsFailed > 0 ? 1 : 0;
}
