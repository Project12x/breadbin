#pragma once

#include "SIDEngine.h"
#include <algorithm>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <random>
#include <vector>

class BreadbinProcessor : public juce::AudioProcessor {
public:
  // Dual SID routing modes
  enum class DualMode { StereoSplit, Unison, Multitimbral };

  // Arpeggiator patterns
  enum class ArpPattern { Up, Down, UpDown, Random };
  enum class LFOWaveform { Triangle, Sawtooth, Square, SampleAndHold };
  struct LFOState {
    bool enabled = false;
    LFOWaveform waveform = LFOWaveform::Triangle;
    float rate = 2.0f;            // Hz (0.1 - 20.0)
    float depthFilter = 0.0f;     // 0.0 - 1.0
    float depthPulseWidth = 0.0f; // 0.0 - 1.0
    float depthPitch = 0.0f;      // 0.0 - 1.0
    // Runtime (not persisted)
    double phase = 0.0;
    float currentValue = 0.0f;
    float shValue = 0.0f; // Sample & Hold latched value
  };

  enum class ControlParam {
    None = 0,
    MasterVolume,
    Aging,
    LeftCutoff,
    LeftResonance,
    RightCutoff,
    RightResonance,
    GlobalGlide,
    PitchBendRange,
    LFORate,
    LFODepthFilter,
    LFODepthPW,
    LFODepthPitch,
    VoiceWaveform,
    VoicePW,
    VoiceAttack,
    VoiceDecay,
    VoiceSustain,
    VoiceRelease,
    VoicePan,
    VoiceRingMod,
    VoiceSync,
    VoiceFilterEnable,
    ArpRate,
    ExtInputLevel,
    LeftDetune,
    RightDetune
  };

  static juce::String getParamName(ControlParam param);

  BreadbinProcessor();
  ~BreadbinProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "Breadbin"; }

  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  // Public accessors for editor
  DualMode getDualMode() const { return dualMode; }
  void setDualMode(DualMode mode) { dualMode = mode; }

  // Per-channel chip model accessors
  SIDEngine::ChipModel getLeftChipModel() const { return chipModelLeft; }
  SIDEngine::ChipModel getRightChipModel() const { return chipModelRight; }
  void setLeftChipModel(SIDEngine::ChipModel model);
  void setRightChipModel(SIDEngine::ChipModel model);
  // Convenience: set both at once
  void setBothChipModels(SIDEngine::ChipModel model);

  float getAgingFactor() const { return agingFactor; }
  void setAgingFactor(float aging);

  // MIDI collector for virtual keyboard (standalone)
  juce::MidiMessageCollector &getMidiMessageCollector() {
    return midiCollector;
  }

  // Synth parameter accessors for GUI
  SIDEngine &getLeftSID() { return sidLeft; }
  SIDEngine &getRightSID() { return sidRight; }

  // Per-voice settings (6 voices: 0-2 = SID L, 3-5 = SID R)
  struct VoiceSettings {
    bool enabled = true;
    int presetId = 1; // 1 = Custom
    SIDEngine::Waveform waveform = SIDEngine::Waveform::Triangle;
    int pulseWidth = 2048;
    int attack = 0;
    int decay = 0;
    int sustain = 15;
    int release = 0;
    float pan = 0.0f;     // -1.0 = full left, 0.0 = center, 1.0 = full right
    bool ringMod = false; // Ring modulation with previous voice
    bool sync = false;    // Hard sync with previous voice
    bool filterEnabled = true; // Route this voice through filter
  };
  VoiceSettings &getVoiceSettings(int voice) { return voiceSettings[voice]; }
  const VoiceSettings &getVoiceSettings(int voice) const {
    return voiceSettings[voice];
  }
  void applyVoiceSettings(int voice); // Apply settings to SID engine

  // Arpeggiator controls
  bool isArpEnabled() const { return arpEnabled; }
  void setArpEnabled(bool enabled) { arpEnabled = enabled; }
  ArpPattern getArpPattern() const { return arpPattern; }
  void setArpPattern(ArpPattern pattern);
  float getArpRate() const { return arpRateHz; }
  void setArpRate(float hz) { arpRateHz = juce::jlimit(1.0f, 100.0f, hz); }
  int getArpOctaves() const { return arpOctaves; }
  void setArpOctaves(int octaves) { arpOctaves = juce::jlimit(1, 4, octaves); }

  // Detune per SID (-50 to +50 cents)
  float getLeftDetune() const { return leftDetuneCents; }
  float getRightDetune() const { return rightDetuneCents; }
  void setLeftDetune(float cents) {
    leftDetuneCents = juce::jlimit(-50.0f, 50.0f, cents);
  }
  void setRightDetune(float cents) {
    rightDetuneCents = juce::jlimit(-50.0f, 50.0f, cents);
  }

  // Portamento/Glide (0-2000 ms)
  float getGlideTimeMs() const { return glideTimeMs; }
  void setGlideTimeMs(float ms) {
    glideTimeMs = juce::jlimit(0.0f, 2000.0f, ms);
  }

  // Pitch Bend (±2 to ±12 semitones range)
  int getPitchBendRange() const { return pitchBendRange; }
  void setPitchBendRange(int semitones) {
    pitchBendRange = juce::jlimit(2, 12, semitones);
  }
  float getPitchBendValue() const { return pitchBendValue; }

  // Mod Wheel (CC1) -> Filter cutoff modulation
  float getModWheelValue() const { return modWheelValue; }

  // External audio input (routes through SID filter)
  bool isExtInputEnabled() const { return extInputEnabled; }
  void setExtInputEnabled(bool enabled) { extInputEnabled = enabled; }
  float getExtInputLevel() const { return extInputLevel; }
  void setExtInputLevel(float level) {
    extInputLevel = juce::jlimit(0.0f, 2.0f, level);
  }

  // Master Volume (0.0 - 1.0)
  float getMasterVolume() const { return masterVolume; }
  void setMasterVolume(float vol);

  // Preset helpers for granular saving/loading
  juce::ValueTree getVoiceState(int voiceIndex) const;
  void setVoiceState(int voiceIndex, const juce::ValueTree &state);

  // Clock mode (PAL/NTSC)
  SIDEngine::ClockMode getClockMode() const { return clockMode; }
  void setClockMode(SIDEngine::ClockMode mode) {
    clockMode = mode;
    sidLeft.setClockMode(mode);
    sidRight.setClockMode(mode);
  }

  // LFO
  LFOState &getLFO() { return lfo; }
  const LFOState &getLFO() const { return lfo; }

  // MIDI Learn
  void startLearning(ControlParam param) { learningParam = param; }
  void stopLearning() { learningParam = ControlParam::None; }
  bool isLearning() const { return learningParam != ControlParam::None; }
  ControlParam getLearningParam() const { return learningParam; }

  void setSelectedVoice(int v) { selectedVoice = juce::jlimit(0, 5, v); }
  int getSelectedVoice() const { return selectedVoice; }

  void setMIDIMapping(int cc, ControlParam param);
  ControlParam getMIDIMapping(int cc) const;
  void clearMIDIMapping(int cc);
  void clearMIDIMappingForParam(ControlParam param);

private:
  SIDEngine sidLeft;
  SIDEngine sidRight;

  DualMode dualMode = DualMode::StereoSplit;
  SIDEngine::ChipModel chipModelLeft = SIDEngine::ChipModel::MOS6581;
  SIDEngine::ChipModel chipModelRight = SIDEngine::ChipModel::MOS6581;
  float agingFactor = 0.0f;
  float leftDetuneCents = 0.0f;
  float rightDetuneCents = 0.0f;
  float glideTimeMs = 0.0f;
  SIDEngine::ClockMode clockMode = SIDEngine::ClockMode::PAL;

  // Pitch bend and mod wheel
  float pitchBendValue = 0.0f;     // -1.0 to +1.0 (normalized)
  int pitchBendRange = 2;          // Semitones (±2 to ±12)
  float modWheelValue = 0.0f;      // 0.0 to 1.0
  int baseFilterCutoffLeft = 1024; // Store base cutoff for mod wheel
  int baseFilterCutoffRight = 1024;

  // External audio input
  bool extInputEnabled = false;
  float extInputLevel = 1.0f; // 0.0 to 2.0 (0-200%)

  double hostSampleRate = 44100.0;
  juce::MidiMessageCollector midiCollector;

  // Per-voice settings storage
  std::array<VoiceSettings, 6> voiceSettings;

  // Note queues for last-note priority (one per SID)
  juce::Array<int> leftNoteQueue;  // Notes held on left SID
  juce::Array<int> rightNoteQueue; // Notes held on right SID
  int lastVelocity = 100;

  std::array<ControlParam, 128> midiMappings;
  ControlParam learningParam = ControlParam::None;
  int selectedVoice = 0;

public:
  // State management - public for editor attachment access
  juce::AudioProcessorValueTreeState apvts;

private:
  juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

  // APVTS Parameter Pointers for fast access in audio thread
  std::atomic<float> *masterVolPtr = nullptr;
  std::atomic<float> *dualModePtr = nullptr;
  std::atomic<float> *chipLeftPtr = nullptr;
  std::atomic<float> *chipRightPtr = nullptr;
  std::atomic<float> *agingPtr = nullptr;
  std::atomic<float> *leftDetunePtr = nullptr;
  std::atomic<float> *rightDetunePtr = nullptr;
  std::atomic<float> *glidePtr = nullptr;
  std::atomic<float> *clockModePtr = nullptr;
  std::atomic<float> *extInputEnablePtr = nullptr;
  std::atomic<float> *extInputLevelPtr = nullptr;

  std::atomic<float> *lfoEnablePtr = nullptr;
  std::atomic<float> *lfoWavePtr = nullptr;
  std::atomic<float> *lfoRatePtr = nullptr;
  std::atomic<float> *lfoDepthFiltPtr = nullptr;
  std::atomic<float> *lfoDepthPWPtr = nullptr;
  std::atomic<float> *lfoDepthPitchPtr = nullptr;

  std::atomic<float> *arpEnablePtr = nullptr;
  std::atomic<float> *arpPatternPtr = nullptr;
  std::atomic<float> *arpRatePtr = nullptr;
  std::atomic<float> *arpOctavesPtr = nullptr;

  struct VoiceParamPtrs {
    std::atomic<float> *enable = nullptr;
    std::atomic<float> *waveform = nullptr;
    std::atomic<float> *pw = nullptr;
    std::atomic<float> *attack = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *sustain = nullptr;
    std::atomic<float> *release = nullptr;
    std::atomic<float> *pan = nullptr;
    std::atomic<float> *ringMod = nullptr;
    std::atomic<float> *sync = nullptr;
    std::atomic<float> *filter = nullptr;
  };
  std::array<VoiceParamPtrs, 6> voiceParamPtrs;

  void initializeParameterPointers();

  void handleCC(int cc, int value);
  void applyMappedParameter(ControlParam param, int value);

  // Voice state for MIDI
  struct VoiceState {
    int note = -1;
    bool active = false;
    // Glide state
    double currentHz = 440.0;
    double targetHz = 440.0;
    bool isGliding = false;
  };
  std::array<VoiceState, 6> voices;

  // SAFETY DSP CHAIN
  // DC blocker / subsonic filter (20Hz high-pass)
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                 juce::dsp::IIR::Coefficients<float>>
      subsonicFilter;
  // Anti-aliasing / ultrasonic filter (20kHz low-pass)
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                 juce::dsp::IIR::Coefficients<float>>
      ultrasonicFilter;
  // Safety limiter
  juce::dsp::Limiter<float> safetyLimiter;

  void handleMidiEvent(const juce::MidiMessage &msg);
  void triggerNote(int voiceIndex, int midiNote, int velocity);
  void releaseNote(int voiceIndex);
  void updateSIDFromQueue(bool isLeftSID); // Trigger all enabled voices on SID
  void prepareSafetyChain(double sampleRate, int samplesPerBlock);
  void updateAllVoiceFrequencies(); // Apply pitch bend to all active voices
  void applyModWheelToFilter();     // Apply mod wheel to filter cutoff
  void processLFO(int numSamples);  // Advance LFO phase
  void applyLFOModulation();        // Apply LFO to destinations

  // Arpeggiator
  bool arpEnabled = false;
  ArpPattern arpPattern = ArpPattern::Up;
  float arpRateHz = 50.0f; // PAL default
  int arpOctaves = 1;
  std::vector<int> arpHeldNotes; // Notes currently held
  std::vector<int> arpSequence;  // Generated sequence
  int arpIndex = 0;
  double arpTimer = 0.0;
  int lastArpNote = -1;
  void processArpeggiator(int numSamples);
  void rebuildArpSequence();

  // LFO state
  LFOState lfo;

  // Master Control & MIDI
  float masterVolume = 0.8f;
  bool sustainActive = false;
  juce::Array<int> sustainedNotes;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinProcessor)
};
