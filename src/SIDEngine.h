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
  enum class ChipModel {
    MOS6581,    // 0: Standard early 6581
    MOS6581R2,  // 1: Early revision, bright, weak combined waveforms
    MOS6581R3,  // 2: Most common 6581, slightly dark
    MOS6581R4,  // 3: Late revision, bright filter, strong combined waveforms
    MOS8580,    // 4: Standard 8580, clean
    MOS8580R5,  // 5: Late 8580, darker, stronger combined waveforms
    CSG9580,    // 6: Final production run, bright and clean
    MOS8580D    // 7: Digiboost era, mellow
  };
  enum class ClockMode { PAL, NTSC };
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
  void setClockMode(ClockMode mode);
  ClockMode getClockMode() const { return currentClockMode; }
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

  // Voice modulation
  void setRingMod(int voice, bool enabled); // Ring mod with voice N-1
  void setSync(int voice, bool enabled);    // Hard sync with voice N-1

  // Filter parameters
  void setFilterCutoff(int cutoff);       // 0-2047
  void setFilterResonance(int resonance); // 0-15
  void setFilterMode(bool lowpass, bool bandpass, bool highpass);
  void setFilterVoices(bool v1, bool v2, bool v3);

  // Master volume
  void setVolume(int volume); // 0-15

  // External audio input (routes through filter)
  void setExternalInput(float sample); // -1.0 to +1.0

  // Write 4-bit volume to $D418, preserving filter mode bits.
  // Used for digi sample playback. Call before clock() each sample.
  void writeVolumeRegister(uint8_t vol4bit);

  // Silence all voice oscillators via test bit (bit 3 of control register).
  // Used during digi playback to prevent voice output from mixing with the
  // $D418 volume DAC signal. Call unmuteVoices() to restore normal operation.
  void muteVoices();
  void unmuteVoices();

private:
  std::unique_ptr<reSIDfp::SID> sid;

  double hostSampleRate = 44100.0;
  static constexpr double SID_CLOCK_PAL = 985248.0;
  static constexpr double SID_CLOCK_NTSC = 1022727.0;
  ClockMode currentClockMode = ClockMode::PAL;

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
    bool ringMod = false; // Ring modulation with voice N-1
    bool sync = false;    // Hard sync with voice N-1
  };
  std::array<VoiceCache, 3> voiceCache;

  // Filter state cache
  uint16_t filterCutoff = 1024;
  uint8_t filterResonance = 0;
  uint8_t filterMode =
      0x10; // Default lowpass so filtered voices produce output
  uint8_t filterVoiceMask = 0;
  uint8_t masterVolume = 15;
  bool voicesMuted = false; // True when digi mute is active

  void writeRegister(uint8_t reg, uint8_t value);
  void updateVoiceRegisters(int voice);
  void updateFilterRegisters();
  void applyChipProfile(ChipModel model);
  uint16_t midiNoteToFrequency(int midiNote);
  double getClockHz() const;

  // Non-copyable
  SIDEngine(const SIDEngine &) = delete;
  SIDEngine &operator=(const SIDEngine &) = delete;
};
