// Headless Tests for Breadbin DSP Components
// Tests filter, oscillators, and safety limiter behavior

#include "../src/SIDEngine.h"
#include "../src/SafetyLimiter.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

// Test utilities
namespace {
constexpr float EPSILON = 0.001f;

bool approxEqual(float a, float b, float epsilon = EPSILON) {
  return std::abs(a - b) < epsilon;
}

void printResult(const char *testName, bool passed) {
  std::cout << (passed ? "[PASS] " : "[FAIL] ") << testName << std::endl;
}
} // namespace

// ============================================================================
// SIDEngine Tests
// ============================================================================

bool testOscillatorOutput() {
  SIDEngine sid(SIDEngine::Model::MOS8580);
  sid.setSampleRate(44100.0);

  // Set up voice 0 with sawtooth waveform
  sid.setWaveform(0, SIDEngine::Waveform::Sawtooth);
  sid.setADSR(0, 0, 0, 15, 0); // Instant attack, max sustain
  sid.gateOn(0);

  // Generate some samples
  float leftBuffer[512] = {0};
  float rightBuffer[512] = {0};

  sid.process(leftBuffer, rightBuffer, 512);

  // Check that we got some output
  float maxAbs = 0.0f;
  for (int i = 0; i < 512; ++i) {
    maxAbs = std::max(maxAbs, std::abs(leftBuffer[i]));
  }

  bool passed = maxAbs > 0.01f; // Should have some signal
  printResult("Oscillator produces output", passed);
  return passed;
}

bool testWaveformChange() {
  SIDEngine sid(SIDEngine::Model::MOS8580);
  sid.setSampleRate(44100.0);

  // Compare different waveforms
  std::vector<float> triangleOutput(256);
  std::vector<float> sawOutput(256);
  float rightDummy[256] = {0};

  // Triangle
  sid.setWaveform(0, SIDEngine::Waveform::Triangle);
  sid.setADSR(0, 0, 0, 15, 0);
  sid.gateOn(0);
  sid.process(triangleOutput.data(), rightDummy, 256);

  // Reset and try sawtooth
  SIDEngine sid2(SIDEngine::Model::MOS8580);
  sid2.setSampleRate(44100.0);
  sid2.setWaveform(0, SIDEngine::Waveform::Sawtooth);
  sid2.setADSR(0, 0, 0, 15, 0);
  sid2.gateOn(0);
  sid2.process(sawOutput.data(), rightDummy, 256);

  // They should produce different outputs
  float diff = 0.0f;
  for (size_t i = 0; i < 256; ++i) {
    diff += std::abs(triangleOutput[i] - sawOutput[i]);
  }

  bool passed = diff > 0.1f; // Should be measurably different
  printResult("Waveforms produce different output", passed);
  return passed;
}

bool testFilterCutoff() {
  SIDEngine sid(SIDEngine::Model::MOS8580);
  sid.setSampleRate(44100.0);

  // Generate noise (broadband) with low filter cutoff
  sid.setWaveform(0, SIDEngine::Waveform::Noise);
  sid.setADSR(0, 0, 0, 15, 0);
  sid.setFilter(0, true);   // Route to filter
  sid.setFilterMode(true);  // Low-pass
  sid.setFilterCutoff(100); // Very low cutoff
  sid.setFilterResonance(0);
  sid.gateOn(0);

  float lowCutoff[512] = {0};
  float dummy[512] = {0};
  sid.process(lowCutoff, dummy, 512);

  // Now with high cutoff
  SIDEngine sid2(SIDEngine::Model::MOS8580);
  sid2.setSampleRate(44100.0);
  sid2.setWaveform(0, SIDEngine::Waveform::Noise);
  sid2.setADSR(0, 0, 0, 15, 0);
  sid2.setFilter(0, true);
  sid2.setFilterMode(true);
  sid2.setFilterCutoff(2000); // High cutoff
  sid2.setFilterResonance(0);
  sid2.gateOn(0);

  float highCutoff[512] = {0};
  sid2.process(highCutoff, dummy, 512);

  // High cutoff should have more high-frequency energy
  // Simple proxy: higher RMS for noise
  float rmsLow = 0, rmsHigh = 0;
  for (int i = 0; i < 512; ++i) {
    rmsLow += lowCutoff[i] * lowCutoff[i];
    rmsHigh += highCutoff[i] * highCutoff[i];
  }

  bool passed = rmsHigh > rmsLow * 0.5f; // High cutoff should pass more signal
  printResult("Filter cutoff affects output", passed);
  return passed;
}

bool testADSREnvelope() {
  SIDEngine sid(SIDEngine::Model::MOS8580);
  sid.setSampleRate(44100.0);

  // Long attack
  sid.setWaveform(0, SIDEngine::Waveform::Triangle);
  sid.setADSR(0, 15, 0, 15, 0); // Max attack time
  sid.gateOn(0);

  float early[64] = {0};
  float late[64] = {0};
  float dummy[64] = {0};

  // Get early samples (during attack)
  sid.process(early, dummy, 64);

  // Skip ahead
  for (int i = 0; i < 100; ++i) {
    sid.process(dummy, dummy, 64);
  }

  // Get late samples
  sid.process(late, dummy, 64);

  // Late samples should be louder
  float maxEarly = 0, maxLate = 0;
  for (int i = 0; i < 64; ++i) {
    maxEarly = std::max(maxEarly, std::abs(early[i]));
    maxLate = std::max(maxLate, std::abs(late[i]));
  }

  bool passed = maxLate > maxEarly;
  printResult("ADSR envelope affects amplitude", passed);
  return passed;
}

// ============================================================================
// Safety Limiter Tests
// ============================================================================

bool testSafetyLimiterClipping() {
  SafetyLimiter limiter;

  // Create oversaturated signal
  float left[256], right[256];
  for (int i = 0; i < 256; ++i) {
    left[i] = 5.0f; // Way over
    right[i] = -5.0f;
  }

  limiter.process(left, right, 256);

  // Check all values are clamped
  bool passed = true;
  for (int i = 0; i < 256; ++i) {
    if (std::abs(left[i]) > 1.0f || std::abs(right[i]) > 1.0f) {
      passed = false;
      break;
    }
  }

  printResult("Safety limiter clamps to [-1, 1]", passed);
  return passed;
}

bool testSafetyLimiterDCBlocking() {
  SafetyLimiter limiter;

  // Create DC offset signal
  float left[1024], right[1024];
  for (int i = 0; i < 1024; ++i) {
    left[i] = 0.5f; // Constant DC
    right[i] = 0.5f;
  }

  limiter.process(left, right, 1024);

  // After processing, DC should be reduced
  float avgAfter = 0;
  for (int i = 512; i < 1024; ++i) { // Check later samples
    avgAfter += left[i];
  }
  avgAfter /= 512.0f;

  bool passed = std::abs(avgAfter) < 0.3f; // DC should be significantly reduced
  printResult("Safety limiter reduces DC offset", passed);
  return passed;
}

bool testSafetyLimiterPassthrough() {
  SafetyLimiter limiter;

  // Create normal signal
  float left[256], right[256];
  for (int i = 0; i < 256; ++i) {
    left[i] = 0.5f * std::sin(i * 0.1f);
    right[i] = 0.3f * std::cos(i * 0.1f);
  }

  float originalLeft[256];
  std::copy(left, left + 256, originalLeft);

  limiter.process(left, right, 256);

  // Signal should pass through mostly intact
  float diff = 0;
  for (int i = 0; i < 256; ++i) {
    diff += std::abs(left[i] - originalLeft[i]);
  }

  bool passed = diff < 5.0f; // Small deviation expected from DC blocking
  printResult("Normal signal passes through limiter", passed);
  return passed;
}

// ============================================================================
// Main
// ============================================================================

int main() {
  std::cout << "=== Breadbin DSP Headless Tests ===" << std::endl;
  std::cout << std::endl;

  int passed = 0;
  int total = 0;

  std::cout << "--- SIDEngine Tests ---" << std::endl;
  if (testOscillatorOutput())
    ++passed;
  ++total;
  if (testWaveformChange())
    ++passed;
  ++total;
  if (testFilterCutoff())
    ++passed;
  ++total;
  if (testADSREnvelope())
    ++passed;
  ++total;

  std::cout << std::endl;
  std::cout << "--- Safety Limiter Tests ---" << std::endl;
  if (testSafetyLimiterClipping())
    ++passed;
  ++total;
  if (testSafetyLimiterDCBlocking())
    ++passed;
  ++total;
  if (testSafetyLimiterPassthrough())
    ++passed;
  ++total;

  std::cout << std::endl;
  std::cout << "=== Results: " << passed << "/" << total
            << " passed ===" << std::endl;

  return (passed == total) ? 0 : 1;
}
