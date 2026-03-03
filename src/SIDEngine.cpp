#include "SIDEngine.h"
#include "SID.h"
#include <cmath>

SIDEngine::SIDEngine() {
  sid = std::make_unique<reSIDfp::SID>();
  sid->reset();

  // Default to 6581
  setChipModel(ChipModel::MOS6581);

  // Initialize with default settings
  setVolume(15);
  setFilterCutoff(1024);
  setFilterResonance(0);
  setFilterMode(true, false,
                false); // Default to lowpass so filtered voices produce output
}

SIDEngine::~SIDEngine() = default;

void SIDEngine::prepare(double sampleRate) {
  hostSampleRate = sampleRate;
  double clockHz =
      (currentClockMode == ClockMode::NTSC) ? SID_CLOCK_NTSC : SID_CLOCK_PAL;
  clockRatio = clockHz / hostSampleRate;
  clockAccumulator = 0.0;

  // Configure SID sampling
  sid->setSamplingParameters(clockHz, reSIDfp::SamplingMethod::RESAMPLE,
                             sampleRate);

  // Initialize all voice registers so noteOn will work immediately
  for (int v = 0; v < 3; ++v) {
    updateVoiceRegisters(v);
    // Set default pulse width
    int baseReg = v * 7;
    writeRegister(baseReg + 2, voiceCache[v].pulseWidth & 0xFF);
    writeRegister(baseReg + 3, (voiceCache[v].pulseWidth >> 8) & 0x0F);
  }

  // Initialize filter
  updateFilterRegisters();

  // Warm up reSIDfp's external filter (1.6Hz HPF) so its internal state
  // converges to the chip's idle bias. Without this, Vlp/Vhp start at 0
  // and the filter takes ~100ms to charge up, causing a DC surge on startup.
  {
    short warmupBuf[64];
    int warmupCycles = static_cast<int>(clockHz * 0.060); // 60ms warmup
    while (warmupCycles > 0) {
      int batch = std::min(warmupCycles, 1000);
      sid->clock(static_cast<unsigned int>(batch), warmupBuf);
      warmupCycles -= batch;
    }
  }

  // Calibrate idle DC offset: with all voices gated off and volume at max,
  // read the resting output level. This captures the chip's inherent bias
  // (6581: ~5V, 8580: ~4.8V) which we subtract from every sample.
  {
    short calBuf[64];
    // Clock a few more host-samples to get a stable reading
    int calSamples = 0;
    float calSum = 0.0f;
    for (int i = 0; i < 8; ++i) {
      int n = sid->clock(static_cast<unsigned int>(clockRatio) + 1, calBuf);
      for (int j = 0; j < n; ++j) {
        calSum += static_cast<float>(calBuf[j]) / 32768.0f;
        ++calSamples;
      }
    }
    idleOffset = calSamples > 0 ? calSum / static_cast<float>(calSamples) : 0.0f;
    dcEstimate = 0.0f;
  }

  // Adaptive DC estimator alphas (sample-rate dependent)
  // Slow: ~7Hz tracking for steady-state envelope drift
  // Fast: ~70Hz tracking for gate/waveform DC steps
  dcAlphaSlow = static_cast<float>(2.0 * 3.14159265358979323846 * 7.0 / sampleRate);
  dcAlphaFast = static_cast<float>(2.0 * 3.14159265358979323846 * 70.0 / sampleRate);
  dcStepThreshold = 0.005f;

  // Clear sample buffer so we don't return stale warmup/calibration data
  sampleBufferSize = 0;
  sampleBufferPos = 0;
}

void SIDEngine::setChipModel(ChipModel model) {
  // Set base silicon model (6581 family or 8580 family)
  bool is6581 = (model == ChipModel::MOS6581 || model == ChipModel::MOS6581R2 ||
                 model == ChipModel::MOS6581R3 || model == ChipModel::MOS6581R4);
  sid->setChipModel(is6581 ? reSIDfp::ChipModel::MOS6581
                           : reSIDfp::ChipModel::MOS8580);
  applyChipProfile(model);
}

void SIDEngine::applyChipProfile(ChipModel model) {
  switch (model) {
  case ChipModel::MOS6581:
    // Standard early 6581: warm, gritty, iconic filter sweep
    sid->setFilter6581Curve(0.5);
    sid->setFilter6581Range(0.5);
    sid->setCombinedWaveforms(reSIDfp::CombinedWaveforms::AVERAGE);
    break;
  case ChipModel::MOS6581R2:
    // Early revision: bright filter, weak combined waveforms, variable
    sid->setFilter6581Curve(0.3);
    sid->setFilter6581Range(0.35);
    sid->setCombinedWaveforms(reSIDfp::CombinedWaveforms::WEAK);
    break;
  case ChipModel::MOS6581R3:
    // Most common 6581: slightly dark, the "classic" sound
    sid->setFilter6581Curve(0.6);
    sid->setFilter6581Range(0.55);
    sid->setCombinedWaveforms(reSIDfp::CombinedWaveforms::AVERAGE);
    break;
  case ChipModel::MOS6581R4:
    // Late revision: brighter filter, stronger combined waveforms
    sid->setFilter6581Curve(0.8);
    sid->setFilter6581Range(0.7);
    sid->setCombinedWaveforms(reSIDfp::CombinedWaveforms::STRONG);
    break;
  case ChipModel::MOS8580:
    // Standard 8580: cleaner, tighter bass, balanced
    sid->setFilter8580Curve(0.5);
    sid->setCombinedWaveforms(reSIDfp::CombinedWaveforms::AVERAGE);
    break;
  case ChipModel::MOS8580R5:
    // Late 8580: darker filter, stronger combined waveforms
    sid->setFilter8580Curve(0.65);
    sid->setCombinedWaveforms(reSIDfp::CombinedWaveforms::STRONG);
    break;
  case ChipModel::CSG9580:
    // Final production run: bright and clean
    sid->setFilter8580Curve(0.3);
    sid->setCombinedWaveforms(reSIDfp::CombinedWaveforms::AVERAGE);
    break;
  case ChipModel::MOS8580D:
    // Digiboost era: mellower, weaker combined waveforms
    sid->setFilter8580Curve(0.35);
    sid->setCombinedWaveforms(reSIDfp::CombinedWaveforms::WEAK);
    break;
  }
}

void SIDEngine::setClockMode(ClockMode mode) {
  currentClockMode = mode;
  if (hostSampleRate > 0)
    prepare(hostSampleRate);
}


float SIDEngine::clock() {
  // If we have a buffered sample, return it
  if (sampleBufferPos < sampleBufferSize) {
    return sampleBuffer[sampleBufferPos++];
  }

  // Need to generate more samples - clock the SID until we get output
  // Calculate how many SID clocks we need to run for ~1 host sample
  // SID clock: ~985,248 Hz (PAL), Host: e.g. 44,100 Hz
  // Ratio: ~22.3 SID clocks per host sample

  const int cyclesToRun = static_cast<int>(clockRatio) + 1;

  // Output buffer for reSIDfp (enough for one host sample)
  short outputBuffer[8];
  int samplesProduced = 0;

  // Run SID cycles until we produce at least one sample
  int totalCycles = 0;
  while (samplesProduced == 0 && totalCycles < 100) {
    samplesProduced =
        sid->clock(static_cast<unsigned int>(cyclesToRun), outputBuffer);
    totalCycles += cyclesToRun;
  }

  // Convert to float, remove DC offset, and buffer
  if (samplesProduced > 0) {
    sampleBufferSize = samplesProduced;
    sampleBufferPos = 0;
    for (int i = 0; i < samplesProduced && i < 8; ++i) {
      float x = static_cast<float>(outputBuffer[i]) / 32768.0f;
      // Layer 1: subtract calibrated idle offset (removes static bias)
      x -= idleOffset;
      // Layer 2: adaptive DC estimator — fast for large steps, slow for drift.
      // Also uses fast alpha when a gate transition was recently signalled.
      float diff = x - dcEstimate;
      bool forceFast = dcFastSamplesLeft > 0;
      if (forceFast) --dcFastSamplesLeft;
      float alpha = (forceFast || std::abs(diff) > dcStepThreshold)
                        ? dcAlphaFast : dcAlphaSlow;
      dcEstimate += alpha * diff;
      sampleBuffer[i] = x - dcEstimate;
    }
    return sampleBuffer[sampleBufferPos++];
  }

  return 0.0f;
}

void SIDEngine::noteOn(int voice, int midiNote, int velocity) {
  noteOn(voice, midiNote, velocity, 0.0f);
}

void SIDEngine::noteOn(int voice, int midiNote, int velocity,
                       float detuneCents) {
  if (voice < 0 || voice > 2)
    return;

  // Calculate detuned frequency
  // cents to semitones: cents/100, MIDI note with fractional part
  double detuneNote = static_cast<double>(midiNote) + (detuneCents / 100.0);

  // MIDI note to Hz: f = 440 * 2^((n-69)/12)
  double hz = 440.0 * std::pow(2.0, (detuneNote - 69.0) / 12.0);

  // Convert to SID frequency register value
  double fn = (hz * 16777216.0) / getClockHz();
  uint16_t freq = static_cast<uint16_t>(std::clamp(fn, 0.0, 65535.0));

  // Set frequency registers
  int baseReg = voice * 7;
  writeRegister(baseReg + 0, freq & 0xFF);        // Freq Lo
  writeRegister(baseReg + 1, (freq >> 8) & 0xFF); // Freq Hi

  // Gate on with current waveform
  voiceCache[voice].gateOn = true;
  updateVoiceRegisters(voice);

  // Hint DC estimator: gate transition shifts envelope-dependent voice bias
  dcFastSamplesLeft = static_cast<int>(hostSampleRate * 0.020);
}

void SIDEngine::noteOff(int voice) {
  if (voice < 0 || voice > 2)
    return;

  voiceCache[voice].gateOn = false;
  updateVoiceRegisters(voice);

  // Hint DC estimator: gate transition causes a DC shift from the envelope-
  // dependent voice bias. Force fast-track mode for 20ms to catch it.
  dcFastSamplesLeft = static_cast<int>(hostSampleRate * 0.020);
}

void SIDEngine::setFrequency(int voice, double hz) {
  if (voice < 0 || voice > 2)
    return;

  // Convert Hz to SID frequency register value
  double fn = (hz * 16777216.0) / getClockHz();
  uint16_t freq = static_cast<uint16_t>(std::clamp(fn, 0.0, 65535.0));

  // Set frequency registers only - no gate change
  int baseReg = voice * 7;
  writeRegister(baseReg + 0, freq & 0xFF);        // Freq Lo
  writeRegister(baseReg + 1, (freq >> 8) & 0xFF); // Freq Hi
}

void SIDEngine::setWaveform(int voice, Waveform waveform) {
  if (voice < 0 || voice > 2)
    return;
  voiceCache[voice].waveform = static_cast<uint8_t>(waveform);
  updateVoiceRegisters(voice);
}

void SIDEngine::setPulseWidth(int voice, int pw) {
  if (voice < 0 || voice > 2)
    return;
  voiceCache[voice].pulseWidth = static_cast<uint16_t>(std::clamp(pw, 0, 4095));

  int baseReg = voice * 7;
  writeRegister(baseReg + 2, voiceCache[voice].pulseWidth & 0xFF);
  writeRegister(baseReg + 3, (voiceCache[voice].pulseWidth >> 8) & 0x0F);
}

void SIDEngine::setAttack(int voice, int attack) {
  if (voice < 0 || voice > 2)
    return;
  voiceCache[voice].attack = static_cast<uint8_t>(std::clamp(attack, 0, 15));
  updateVoiceRegisters(voice);
}

void SIDEngine::setDecay(int voice, int decay) {
  if (voice < 0 || voice > 2)
    return;
  voiceCache[voice].decay = static_cast<uint8_t>(std::clamp(decay, 0, 15));
  updateVoiceRegisters(voice);
}

void SIDEngine::setSustain(int voice, int sustain) {
  if (voice < 0 || voice > 2)
    return;
  voiceCache[voice].sustain = static_cast<uint8_t>(std::clamp(sustain, 0, 15));
  updateVoiceRegisters(voice);
}

void SIDEngine::setRelease(int voice, int release) {
  if (voice < 0 || voice > 2)
    return;
  voiceCache[voice].release = static_cast<uint8_t>(std::clamp(release, 0, 15));
  updateVoiceRegisters(voice);
}

void SIDEngine::setRingMod(int voice, bool enabled) {
  if (voice < 0 || voice > 2)
    return;
  voiceCache[voice].ringMod = enabled;
  // Don't call updateVoiceRegisters - ring mod bit will be applied on next
  // noteOn or waveform change. Calling it here can interfere with gate timing.
}

void SIDEngine::setSync(int voice, bool enabled) {
  if (voice < 0 || voice > 2)
    return;
  voiceCache[voice].sync = enabled;
  // Don't call updateVoiceRegisters - sync bit will be applied on next noteOn
  // or waveform change. Calling it here can interfere with gate timing.
}

void SIDEngine::setFilterCutoff(int cutoff) {
  filterCutoff = static_cast<uint16_t>(std::clamp(cutoff, 0, 2047));
  updateFilterRegisters();
}

void SIDEngine::setFilterResonance(int resonance) {
  filterResonance = static_cast<uint8_t>(std::clamp(resonance, 0, 15));
  updateFilterRegisters();
}

void SIDEngine::setFilterMode(bool lowpass, bool bandpass, bool highpass) {
  filterMode = 0;
  if (lowpass)
    filterMode |= 0x10;
  if (bandpass)
    filterMode |= 0x20;
  if (highpass)
    filterMode |= 0x40;
  updateFilterRegisters();
}

void SIDEngine::setFilterVoices(bool v1, bool v2, bool v3) {
  filterVoiceMask = 0;
  if (v1)
    filterVoiceMask |= 0x01;
  if (v2)
    filterVoiceMask |= 0x02;
  if (v3)
    filterVoiceMask |= 0x04;
  updateFilterRegisters();
}

void SIDEngine::setVolume(int volume) {
  masterVolume = static_cast<uint8_t>(std::clamp(volume, 0, 15));
  updateFilterRegisters();
}

void SIDEngine::setExternalInput(float sample) {
  // Convert float (-1.0 to +1.0) to 16-bit signed integer
  // The reSIDfp input() function accepts a 16-bit sample value
  int value = static_cast<int>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
  sid->input(value);
}

void SIDEngine::writeVolumeRegister(uint8_t vol4bit) {
  // Write filter mode (upper nibble) | volume (lower nibble) to register $D418.
  // Preserves current filter routing while overriding volume for digi playback.
  writeRegister(0x18, filterMode | (vol4bit & 0x0F));
}

void SIDEngine::muteVoices() {
  if (voicesMuted) return;
  voicesMuted = true;
  // Set test bit (bit 3) on all voice control registers to reset oscillators.
  // This silences voices while preserving their cached state for restoration.
  for (int v = 0; v < 3; ++v) {
    int baseReg = v * 7;
    writeRegister(baseReg + 4, 0x08); // Test bit only, no waveform/gate
  }
}

void SIDEngine::unmuteVoices() {
  if (!voicesMuted) return;
  voicesMuted = false;
  // Restore all voice control registers from cache
  for (int v = 0; v < 3; ++v) {
    updateVoiceRegisters(v);
  }
}

void SIDEngine::writeRegister(uint8_t reg, uint8_t value) {
  sid->write(reg, value);
}

void SIDEngine::updateVoiceRegisters(int voice) {
  int baseReg = voice * 7;
  auto &vc = voiceCache[voice];

  // Control register: waveform + ring mod + sync + gate
  uint8_t control = vc.waveform | (vc.gateOn ? 0x01 : 0x00);
  if (vc.sync)
    control |= 0x02; // Bit 1: hard sync
  if (vc.ringMod)
    control |= 0x04; // Bit 2: ring modulation

  writeRegister(baseReg + 4, control);

  // Attack/Decay
  writeRegister(baseReg + 5, (vc.attack << 4) | vc.decay);

  // Sustain/Release
  writeRegister(baseReg + 6, (vc.sustain << 4) | vc.release);
}

void SIDEngine::updateFilterRegisters() {
  // FC Lo (3 bits)
  writeRegister(0x15, filterCutoff & 0x07);
  // FC Hi (8 bits)
  writeRegister(0x16, (filterCutoff >> 3) & 0xFF);

  // Resonance + voice routing
  writeRegister(0x17, (filterResonance << 4) | filterVoiceMask);

  // Mode + volume
  writeRegister(0x18, filterMode | masterVolume);
}

uint16_t SIDEngine::midiNoteToFrequency(int midiNote) {
  // SID frequency calculation
  // f = (Fn * Fclk/16777216) where Fn is the 16-bit frequency value
  // Rearranging: Fn = (f * 16777216) / Fclk

  // MIDI note to Hz: f = 440 * 2^((n-69)/12)
  double hz = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);

  // Convert to SID frequency register value
  double fn = (hz * 16777216.0) / getClockHz();

  return static_cast<uint16_t>(std::clamp(fn, 0.0, 65535.0));
}

double SIDEngine::getClockHz() const {
  return (currentClockMode == ClockMode::NTSC) ? SID_CLOCK_NTSC : SID_CLOCK_PAL;
}
