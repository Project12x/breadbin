#pragma once

#include <array>
#include <cstdint>
#include <memory>

// Forward declare reSIDfp class
namespace reSIDfp {
class SID;
}

class SIDEngine {
public:
  enum class ChipModel { MOS6581, MOS8580 };
  enum class Waveform {
    Triangle = 0x10,
    Sawtooth = 0x20,
    Pulse = 0x40,
    Noise = 0x80
  };

  SIDEngine();
  ~SIDEngine();

  void prepare(double sampleRate);
  void setChipModel(ChipModel model);
  void setAgingFactor(float aging); // 0.0 = fresh, 1.0 = vintage

  // Generate one sample at host sample rate (handles internal clock/resample)
  float clock();

  // Voice control (0-2)
  void noteOn(int voice, int midiNote, int velocity);
  void noteOn(int voice, int midiNote, int velocity, float detuneCents);
  void noteOff(int voice);
  void setFrequency(int voice,
                    double hz); // For portamento - no envelope retrigger

  // Per-voice parameters
  void setWaveform(int voice, Waveform waveform);
  void setPulseWidth(int voice, int pw);   // 0-4095
  void setAttack(int voice, int attack);   // 0-15
  void setDecay(int voice, int decay);     // 0-15
  void setSustain(int voice, int sustain); // 0-15
  void setRelease(int voice, int release); // 0-15

  // Filter parameters
  void setFilterCutoff(int cutoff);       // 0-2047
  void setFilterResonance(int resonance); // 0-15
  void setFilterMode(bool lowpass, bool bandpass, bool highpass);
  void setFilterVoices(bool v1, bool v2, bool v3);

  // Master volume
  void setVolume(int volume); // 0-15

private:
  std::unique_ptr<reSIDfp::SID> sid;

  double hostSampleRate = 44100.0;
  static constexpr double SID_CLOCK_PAL = 985248.0;

  // Resampling state
  double clockAccumulator = 0.0;
  double clockRatio = 1.0;

  // Aging simulation
  float agingFactor = 0.0f;
  int agingCutoffOffset = 0;

  // Sample output buffer (reSIDfp doesn't produce 1 sample per call)
  float sampleBuffer[8] = {};
  int sampleBufferPos = 0;
  int sampleBufferSize = 0;

  // Voice state cache
  struct VoiceCache {
    uint8_t waveform = 0x10; // Triangle default
    uint16_t pulseWidth = 2048;
    uint8_t attack = 0;
    uint8_t decay = 0;
    uint8_t sustain = 15;
    uint8_t release = 0;
    bool gateOn = false;
  };
  std::array<VoiceCache, 3> voiceCache;

  // Filter state cache
  uint16_t filterCutoff = 1024;
  uint8_t filterResonance = 0;
  uint8_t filterMode = 0;
  uint8_t filterVoiceMask = 0;
  uint8_t masterVolume = 15;

  void writeRegister(uint8_t reg, uint8_t value);
  void updateVoiceRegisters(int voice);
  void updateFilterRegisters();
  uint16_t midiNoteToFrequency(int midiNote);

  // Non-copyable
  SIDEngine(const SIDEngine &) = delete;
  SIDEngine &operator=(const SIDEngine &) = delete;
};
