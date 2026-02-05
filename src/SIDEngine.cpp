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
}

SIDEngine::~SIDEngine() = default;

void SIDEngine::prepare(double sampleRate) {
  hostSampleRate = sampleRate;
  clockRatio = SID_CLOCK_PAL / hostSampleRate;
  clockAccumulator = 0.0;

  // Configure SID sampling
  sid->setSamplingParameters(SID_CLOCK_PAL, reSIDfp::SamplingMethod::RESAMPLE,
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
}

void SIDEngine::setChipModel(ChipModel model) {
  if (model == ChipModel::MOS6581) {
    sid->setChipModel(reSIDfp::ChipModel::MOS6581);
  } else {
    sid->setChipModel(reSIDfp::ChipModel::MOS8580);
  }
}

void SIDEngine::setAgingFactor(float aging) {
  agingFactor = std::clamp(aging, 0.0f, 1.0f);

  // Aging shifts filter cutoff down and adds slight nonlinearity
  // Real aged SIDs can have cutoff drift of ~10-20%
  agingCutoffOffset = static_cast<int>(-200.0f * agingFactor);

  // Update filter with new aging offset
  updateFilterRegisters();
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

  // Convert to float and buffer
  if (samplesProduced > 0) {
    sampleBufferSize = samplesProduced;
    sampleBufferPos = 0;
    for (int i = 0; i < samplesProduced && i < 8; ++i) {
      sampleBuffer[i] = static_cast<float>(outputBuffer[i]) / 32768.0f;
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
  double fn = (hz * 16777216.0) / SID_CLOCK_PAL;
  uint16_t freq = static_cast<uint16_t>(std::clamp(fn, 0.0, 65535.0));

  // Set frequency registers
  int baseReg = voice * 7;
  writeRegister(baseReg + 0, freq & 0xFF);        // Freq Lo
  writeRegister(baseReg + 1, (freq >> 8) & 0xFF); // Freq Hi

  // Gate on with current waveform
  voiceCache[voice].gateOn = true;
  updateVoiceRegisters(voice);
}

void SIDEngine::noteOff(int voice) {
  if (voice < 0 || voice > 2)
    return;

  voiceCache[voice].gateOn = false;
  updateVoiceRegisters(voice);
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

void SIDEngine::writeRegister(uint8_t reg, uint8_t value) {
  sid->write(reg, value);
}

void SIDEngine::updateVoiceRegisters(int voice) {
  int baseReg = voice * 7;
  auto &vc = voiceCache[voice];

  // Control register: waveform + gate
  uint8_t control = vc.waveform | (vc.gateOn ? 0x01 : 0x00);
  writeRegister(baseReg + 4, control);

  // Attack/Decay
  writeRegister(baseReg + 5, (vc.attack << 4) | vc.decay);

  // Sustain/Release
  writeRegister(baseReg + 6, (vc.sustain << 4) | vc.release);
}

void SIDEngine::updateFilterRegisters() {
  // Apply aging offset to cutoff
  int adjustedCutoff = static_cast<int>(filterCutoff) + agingCutoffOffset;
  adjustedCutoff = std::clamp(adjustedCutoff, 0, 2047);

  // FC Lo (3 bits)
  writeRegister(0x15, adjustedCutoff & 0x07);
  // FC Hi (8 bits)
  writeRegister(0x16, (adjustedCutoff >> 3) & 0xFF);

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
  double fn = (hz * 16777216.0) / SID_CLOCK_PAL;

  return static_cast<uint16_t>(std::clamp(fn, 0.0, 65535.0));
}
