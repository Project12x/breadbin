#pragma once

#include "DigiSampler.h"
#include "SIDEngine.h"
#include "SidFilePlayer.h"
#include <ghostmoon/CpuSectionProfiler.h>
#include <ghostmoon/ReverbSC.h>
#include <ghostmoon/SilenceGate.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

class BreadbinProcessor : public juce::AudioProcessor {
public:
  // Dual SID routing modes
  enum class DualMode { StereoSplit, Unison, Multitimbral };

  // Voice allocation modes
  enum class VoiceMode { Mono = 0, Paraphonic = 1, Polyphonic = 2, PolyPara = 3 };
  enum class EcoMode { Off = 0, Manual = 1 };
  enum class PolySidBudget { Hybrid = 0, Ultra = 1, MaxEco = 2 };
  enum class PolyStereoAnchor { Oldest = 0, Newest = 1 };
  enum class PolySidRenderRole { Pair = 0, LeftMono = 1, RightMono = 2 };

  // Paraphonic per-voice state
  struct ParaVoiceState {
    int midiNote;
    int velocity;
  };

  // Arpeggiator patterns
  enum class ArpPattern { Up, Down, UpDown, Random };
  enum class LFOWaveform { Triangle, Sawtooth, Square, SampleAndHold, Sine };
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

  // Filter envelope state (software ADSR for filter cutoff modulation)
  struct FilterEnvelopeState {
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Stage::Idle;
    bool gateWasOn = false;
    float currentValue = 0.0f; // 0.0-1.0 envelope output

    // Advance ADSR by dt seconds with given parameters
    void tick(float dt, float attack, float decay, float sustain, float release) {
      switch (stage) {
      case Stage::Attack:
        currentValue += dt / attack;
        if (currentValue >= 1.0f) {
          currentValue = 1.0f;
          stage = Stage::Decay;
        }
        break;
      case Stage::Decay: {
        float rate = dt / decay;
        currentValue -= rate * (currentValue - sustain);
        if (currentValue <= sustain + 0.001f) {
          currentValue = sustain;
          stage = Stage::Sustain;
        }
        break;
      }
      case Stage::Sustain:
        currentValue = sustain;
        break;
      case Stage::Release: {
        float rate = dt / release;
        currentValue -= rate * currentValue;
        if (currentValue <= 0.001f) {
          currentValue = 0.0f;
          stage = Stage::Idle;
        }
        break;
      }
      case Stage::Idle:
        currentValue = 0.0f;
        break;
      }
    }
  };

  // Polyphony: each held note gets its own L/R SID engine pair
  struct PolyVoice {
    std::unique_ptr<SIDEngine> sidLeft;
    std::unique_ptr<SIDEngine> sidRight;
    int midiNote = -1;
    int velocity = 0;
    bool active = false;
    bool releasing = false;          // Gate off, ADSR releasing
    double currentHz = 0.0;
    double targetHz = 0.0;
    bool isGliding = false;
    uint32_t startSample = 0;       // Monotonic counter for voice stealing
    int releaseSamplesRemaining = 0; // Fallback release timer
    int releaseSamplesTotal = 0;     // Total release duration (for fade overlap)
    FilterEnvelopeState filterEnv;   // Per-voice filter envelope
    // Per-voice fade envelope (prevents DC offset pops on activate/deactivate)
    float fadeGain = 0.0f;           // 0=silent, 1=full — smoothed per-sample
    bool fadingOut = false;          // true = ramping down to deactivation
    gm::SilenceGate sidRenderGate;   // gates released voices after output tail is silent
    float sidRenderTailPeak = 0.0f;
    bool sidRenderWasSkipping = false;
    PolySidRenderRole sidRenderRole = PolySidRenderRole::Pair;
    // Poly+Para: per-SID-voice note tracking
    int paraNote[3] = {-1, -1, -1};
    int paraVelocity[3] = {0, 0, 0};
    int paraCount = 0;
  };
  static constexpr int MAX_POLY = 12;

  // Wavetable step sequencer
  struct WavetableStep {
    int waveform = 2;      // 0=Tri,1=Saw,2=Pulse,3=Noise
    int pitchOffset = 0;   // -24 to +24 semitones
    int pulseWidth = 2048; // 0-4095
  };
  struct WavetableState {
    bool enabled = false;
    int numSteps = 4;
    float rateHz = 50.0f;
    bool loop = true;
    int currentStep = 0;
    double timer = 0.0;
    std::array<WavetableStep, 16> steps;
  };

  // Mod Matrix
  enum class ModSource { None = 0, LFO1, LFO2, FilterEnv, ModWheel, Velocity };
  enum class ModDest { None = 0, FilterCutoff, PulseWidth, Pitch, Resonance };
  static constexpr int kModSlots = 4;

  enum class ControlParam {
    None = 0,
    MasterVolume,
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
    VoiceRingMod,
    VoiceSync,
    VoiceFilterEnable,
    ArpRate,
    ExtInputLevel,
    LeftDetune,
    RightDetune,
    // --- New entries (append-only for serialization stability) ---
    LeftPan,
    RightPan,
    FilterEnvAttack,
    FilterEnvDecay,
    FilterEnvSustain,
    FilterEnvRelease,
    FilterEnvAmount,
    ChorusRate,
    ChorusDepth,
    ChorusMix,
    DelayTimeL,
    DelayTimeR,
    DelayFeedback,
    DelayMix,
    ReverbDecay,
    ReverbDamping,
    ReverbMix,
    PwmSweepRate,
    PwmSweepDepth,
    ArpEnable,
    ArpOctaves,
    ArpPattern,
    ChorusEnable,
    DelayEnable,
    ReverbEnable,
    FilterEnvEnable,
    ExtInputEnable,
    ClockMode,
    DualMode,
    PwmSweepEnable,
    WtEnable,
    LfoEnable,
    Lfo2Enable,
    ChordEnable,
    LFO2Rate,
    LFO2DepthFilter,
    LFO2DepthPW,
    LFO2DepthPitch,
    LfoWave,
    Lfo2Wave,
    DigiEnable,
    DigiRootNote,
    DigiLoop,
    DigiBitDepth,
    VoiceMode,
    PolyMaxNotes,
    ParaSpread,
    ParaFilterRetrig,
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
  double getTailLengthSeconds() const override { return 10.0; }

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

  // MIDI collector for virtual keyboard (standalone)
  juce::MidiMessageCollector &getMidiMessageCollector() {
    return midiCollector;
  }

  // Synth parameter accessors for GUI
  SIDEngine &getLeftSID() { return sidLeft; }
  SIDEngine &getRightSID() { return sidRight; }
  DigiSampler &getDigiSampler() { return digiSampler; }

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
    // per-voice pan removed (now per-SID: leftPan/rightPan)
    bool ringMod = false;      // Ring modulation with previous voice
    bool sync = false;         // Hard sync with previous voice
    bool filterEnabled = true; // Route this voice through filter

    bool operator==(const VoiceSettings &o) const {
      return enabled == o.enabled && waveform == o.waveform &&
             pulseWidth == o.pulseWidth && attack == o.attack &&
             decay == o.decay && sustain == o.sustain &&
             release == o.release && ringMod == o.ringMod &&
             sync == o.sync && filterEnabled == o.filterEnabled;
    }
    bool operator!=(const VoiceSettings &o) const { return !(*this == o); }
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

  // Base filter values (updated by editor sliders, used by modulation system)
  void setBaseFilterCutoff(bool isLeft, int value) {
    if (isLeft)
      baseFilterCutoffLeft = value;
    else
      baseFilterCutoffRight = value;
  }
  void setBaseFilterResonance(bool isLeft, int value) {
    if (isLeft)
      baseFilterResLeft = value;
    else
      baseFilterResRight = value;
  }

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

  // True after setStateInformation has been called (saved state exists)
  bool wasStateRestored() const { return stateRestored; }

  // True after editor has been opened at least once (prevents preset reset on
  // reopen)
  bool wasEditorOpened() const { return editorWasOpened; }
  void markEditorOpened() { editorWasOpened = true; }

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
  LFOState &getLFO2() { return lfo2; }
  const LFOState &getLFO2() const { return lfo2; }
  bool isLfoSynced()  const { return lfoSyncPtr  && lfoSyncPtr->load()  > 0.5f; }
  bool isLfo2Synced() const { return lfo2SyncPtr && lfo2SyncPtr->load() > 0.5f; }

  // Wavetable
  const WavetableState &getWavetable() const { return wavetable; }

  // Post-modulation getters for UI meters
  int getLastAppliedCutoffLeft() const { return lastAppliedCutoffLeft; }
  int getLastAppliedCutoffRight() const { return lastAppliedCutoffRight; }
  int getLastAppliedPW() const { return lastAppliedPW.load(); }
  float getLastAppliedPitchOffset() const {
    return lastAppliedPitchOffsetSemitones.load();
  }
  int getLastAppliedResLeft() const { return lastAppliedResLeft.load(); }
  int getLastAppliedResRight() const { return lastAppliedResRight.load(); }

  // Base value getters (for meters to show base vs modded)
  int getBaseFilterCutoff(bool isLeft) const {
    return isLeft ? baseFilterCutoffLeft : baseFilterCutoffRight;
  }
  int getBaseFilterResonance(bool isLeft) const {
    return isLeft ? baseFilterResLeft : baseFilterResRight;
  }

  // Mod matrix display getters
  float getModSlotSourceValue(int slot) const {
    return (slot >= 0 && slot < kModSlots)
               ? modSlotDisplay[slot].sourceValue.load()
               : 0.0f;
  }
  float getModSlotContribution(int slot) const {
    return (slot >= 0 && slot < kModSlots)
               ? modSlotDisplay[slot].contribution.load()
               : 0.0f;
  }
  float getModTotalFilterCutoff() const {
    return modTotals.filterCutoff.load();
  }
  float getModTotalPulseWidth() const { return modTotals.pulseWidth.load(); }
  float getModTotalPitch() const { return modTotals.pitch.load(); }
  float getModTotalResonance() const { return modTotals.resonance.load(); }

  // Preset dirty-state detection
  void snapshotPresetState();
  bool isPresetDirty() const;

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
  DigiSampler digiSampler;

  // SID file player (background thread + ring buffer)
  std::unique_ptr<SidFilePlayer> sidFilePlayer;

  // Resampler: SID player outputs at 44100, host may run at different rate
  juce::LagrangeInterpolator sidResamplerL, sidResamplerR;
  std::vector<float> sidResampleBufL,
      sidResampleBufR;           // pre-allocated in prepareToPlay
  double sidResampleRatio = 1.0; // ENGINE_RATE / hostRate
  size_t sidResampleBufCapacity =
      0; // Track allocated capacity to avoid RT resize

  // Voice mode state
  VoiceMode voiceMode = VoiceMode::Mono;
  EcoMode ecoMode = EcoMode::Off;
  PolySidBudget polySidBudget = PolySidBudget::Hybrid;
  PolyStereoAnchor polyStereoAnchor = PolyStereoAnchor::Oldest;
  std::atomic<float> *voiceModePtr = nullptr;
  std::atomic<float> *ecoModePtr = nullptr;
  std::atomic<float> *polySidBudgetPtr = nullptr;
  std::atomic<float> *polyStereoAnchorPtr = nullptr;

  // Poly voice pool (pre-allocated, activated on demand)
  std::array<PolyVoice, MAX_POLY> polyVoices;
  uint32_t polyNoteCounter = 0;
  int polyMaxNotes = 4;
  std::atomic<float> *polyMaxNotesPtr = nullptr;

  // Paraphonic: per-SID-voice note allocation (mono SIDs only)
  std::array<ParaVoiceState, 6> paraVoices{{
      {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}}};

  // Paraphonic held-note tracking (for voice stacking / redistribution)
  struct ParaHeldNote { int midiNote = -1; int velocity = 0; };
  std::array<ParaHeldNote, 3> paraHeldNotes{};
  int paraHeldCount = 0;
  std::array<float, 3> paraVoiceSpreadCents{};  // per-voice spread offset

  // Para feature pointers
  std::atomic<float> *paraSpreadPtr = nullptr;
  std::atomic<float> *paraFilterRetrigPtr = nullptr;
  bool filterEnvRetriggerFlag = false;

  // Cached filter mode for poly voice replication (set by editor)
  bool filterLPLeft = true, filterBPLeft = false, filterHPLeft = false;
  bool filterLPRight = true, filterBPRight = false, filterHPRight = false;
public:
  void cacheFilterMode(bool lpL, bool bpL, bool hpL,
                       bool lpR, bool bpR, bool hpR) {
    filterLPLeft = lpL; filterBPLeft = bpL; filterHPLeft = hpL;
    filterLPRight = lpR; filterBPRight = bpR; filterHPRight = hpR;
  }
private:

  // Poly voice management
  void normalizePolyNoteCounters();
  int findFreePolyVoice() const;
  int findStealablePolyVoice() const;
  void polyNoteOn(int midiNote, int velocity);
  void polyNoteOff(int midiNote);
  void polyAllNotesOff();
  void applySettingsToPolyVoice(int polyIdx);
  void processPolyFilterEnvelope(int polyIdx, int numSamples);

  // Paraphonic voice management (mono SIDs)
  void paraNoteOn(int midiNote, int velocity);
  void paraNoteOnSID(bool isLeftSID, int midiNote, int velocity);
  void paraNoteOff(int midiNote);
  void paraNoteOffSID(bool isLeftSID, int midiNote);
  void paraAllNotesOff();
  void redistributeParaVoices();

  // Poly+Para voice management
  void polyParaNoteOn(int midiNote, int velocity);
  void polyParaNoteOff(int midiNote);
  void polyParaAllNotesOff();

  DualMode dualMode = DualMode::StereoSplit;
  SIDEngine::ChipModel chipModelLeft = SIDEngine::ChipModel::MOS6581;
  SIDEngine::ChipModel chipModelRight = SIDEngine::ChipModel::MOS8580;
  float leftDetuneCents = 0.0f;
  float rightDetuneCents = 0.0f;
  double cachedDetuneL = 1.0; // std::pow(2, leftDetuneCents/1200), cached
  double cachedDetuneR = 1.0; // std::pow(2, rightDetuneCents/1200), cached
  // Cached pan law (recalculated only on parameter change)
  float cachedLeftPanVal = 0.0f;  // last seen pan value
  float cachedLeftPanL = 0.707107f;  // cos(pi/4) = center default
  float cachedLeftPanR = 0.707107f;  // sin(pi/4) = center default
  float cachedRightPanVal = 0.0f;
  float cachedRightPanL = 0.707107f;
  float cachedRightPanR = 0.707107f;
  float glideTimeMs = 0.0f;
  SIDEngine::ClockMode clockMode = SIDEngine::ClockMode::PAL;

  // Dirty flags: defer heavy operations (chip model/clock mode) off RT thread
  std::atomic<bool> chipModelDirty{false};
  std::atomic<bool> clockModeDirty{false};
  std::array<bool, 6> voiceSettingsDirty = {true, true, true, true, true, true};

  bool stateRestored = false;
  bool editorWasOpened = false;

  // Pitch bend and mod wheel
  float pitchBendValue = 0.0f;     // -1.0 to +1.0 (normalized)
  int pitchBendRange = 2;          // Semitones (±2 to ±12)
  float modWheelValue = 0.0f;      // 0.0 to 1.0
  int baseFilterCutoffLeft = 1024; // Store base cutoff for mod wheel
  int baseFilterCutoffRight = 1024;
  int baseFilterResLeft = 0; // Store base resonance for mod matrix
  int baseFilterResRight = 0;
  int lastAppliedCutoffLeft =
      1024; // Post-modulation cutoff (set by applyFilterModulation)
  int lastAppliedCutoffRight = 1024;

  // Post-modulation values for UI meters (audio thread writes, UI thread reads)
  std::atomic<int> lastAppliedPW{2048}; // representative (voice 0), 0-4095
  std::atomic<float> lastAppliedPitchOffsetSemitones{
      0.0f};                               // total semitone offset
  std::atomic<int> lastAppliedResLeft{0};  // 0-15
  std::atomic<int> lastAppliedResRight{0}; // 0-15

  // Mod matrix per-slot display values
  struct ModSlotDisplay {
    std::atomic<float> sourceValue{0.0f};
    std::atomic<float> contribution{0.0f};
  };
  std::array<ModSlotDisplay, kModSlots> modSlotDisplay;
  struct ModTotals {
    std::atomic<float> filterCutoff{0.0f};
    std::atomic<float> pulseWidth{0.0f};
    std::atomic<float> pitch{0.0f};
    std::atomic<float> resonance{0.0f};
  };
  ModTotals modTotals;

  // Preset dirty-state detection
  std::map<juce::String, float> presetParamSnapshot;
  int presetBaseFilterCutoffL = 1024, presetBaseFilterCutoffR = 1024;
  int presetBaseFilterResL = 0, presetBaseFilterResR = 0;

  // External audio input
  bool extInputEnabled = false;
  float extInputLevel = 1.0f; // 0.0 to 2.0 (0-200%)

  double hostSampleRate = 44100.0;
  juce::MidiMessageCollector midiCollector;

  // Per-voice settings storage
  std::array<VoiceSettings, 6> voiceSettings;
  std::array<VoiceSettings, 6> prevVoiceSettings; // For dirty-check skip

  // Note queues for last-note priority (one per SID)
  juce::Array<int> leftNoteQueue;  // Notes held on left SID
  juce::Array<int> rightNoteQueue; // Notes held on right SID
  int lastVelocity = 100;

  std::array<ControlParam, 128> midiMappings;
  ControlParam learningParam = ControlParam::None;
  int selectedVoice = 0;
  int globalPresetId = 1; // Persisted global preset selector ID
  std::atomic<float> cpuLoadPercent{0.0f};

public:
  void setGlobalPresetId(int id) { globalPresetId = id; }
  int getGlobalPresetId() const { return globalPresetId; }

  // CPU load measurement (updated per processBlock)
  float getCpuLoad() const {
    return cpuLoadPercent.load(std::memory_order_relaxed);
  }

  // Headless per-section CPU attribution. Disabled unless the test harness
  // enables it, so Release plugin cost is one relaxed load per boundary.
  gm::CpuSectionProfiler &getCpuProfiler() { return cpuProfiler; }
  void resetCpuAuditCounters();
  std::string getCpuAuditCountersJson(int measuredBlocks) const;
  struct CpuSections {
    int paramsTransport = -1;
    int deferredUpdates = -1;
    int voiceSettings = -1;
    int midi = -1;
    int arpeggiator = -1;
    int wavetable = -1;
    int glide = -1;
    int lfo = -1;
    int modulation = -1;
    int polyMod = -1;
    int sidRender = -1;
    int sidFile = -1;
    int chorus = -1;
    int delay = -1;
    int reverb = -1;
    int safetyFilters = -1;
    int limiter = -1;
    int noiseGate = -1;
    int analysis = -1;
  };
  const CpuSections &getCpuSections() const { return cpuSections; }

  // Runtime voice-state accessors (used by integration diagnostics/tests)
  bool isVoiceActiveRuntime(int voiceIndex) const {
    return (voiceIndex >= 0 && voiceIndex < 6) ? voices[voiceIndex].active
                                               : false;
  }
  int getVoiceNoteRuntime(int voiceIndex) const {
    return (voiceIndex >= 0 && voiceIndex < 6) ? voices[voiceIndex].note : -1;
  }
  int getActiveVoiceCountRuntime() const {
    int count = 0;
    for (const auto &voice : voices)
      if (voice.active)
        ++count;
    return count;
  }

  // Voice mode accessors
  VoiceMode getVoiceMode() const { return voiceMode; }
  bool isPolyActive() const {
    return voiceMode == VoiceMode::Polyphonic || voiceMode == VoiceMode::PolyPara;
  }
  bool isParaActive() const {
    return voiceMode == VoiceMode::Paraphonic || voiceMode == VoiceMode::PolyPara;
  }
  int getPolyMaxNotes() const { return polyMaxNotes; }
  int getActivePolyVoiceCount() const {
    int count = 0;
    for (int i = 0; i < polyMaxNotes; ++i)
      if (polyVoices[i].active || polyVoices[i].releasing)
        ++count;
    return count;
  }
  int getActiveParaVoiceCount() const {
    int count = 0;
    for (int i = 0; i < 6; ++i)
      if (paraVoices[i].midiNote != -1)
        ++count;
    return count;
  }
  int getTotalActiveNoteCount() const {
    int count = 0;
    for (int i = 0; i < polyMaxNotes; ++i)
      if (polyVoices[i].active || polyVoices[i].releasing)
        count += polyVoices[i].paraCount;
    return count;
  }

  // SID file player
  SidFilePlayer &getSidFilePlayer() { return *sidFilePlayer; }
  std::atomic<bool> sidPlayerActive{false};
  void snapshotSidPlayerToAPVTS();

  // Chord Learn mode
  void startChordLearn(int slot);
  void stopChordLearn();
  bool isChordLearning() const { return chordLearnActive; }
  int getChordLearnSlot() const { return chordLearnSlot; }
  std::vector<int> getChordLearnNotes();

  // State management - public for editor attachment access
  juce::AudioProcessorValueTreeState apvts;

private:
  juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

  // APVTS Parameter Pointers for fast access in audio thread
  std::atomic<float> *masterVolPtr = nullptr;
  std::atomic<float> *noiseGateThresholdPtr = nullptr;
  std::atomic<float> *gateAttackPtr = nullptr;
  std::atomic<float> *gateReleasePtr = nullptr;
  std::atomic<float> *gateHoldPtr = nullptr;
  std::atomic<float> *dualModePtr = nullptr;
  std::atomic<float> *chipLeftPtr = nullptr;
  std::atomic<float> *chipRightPtr = nullptr;
  std::atomic<float> *leftDetunePtr = nullptr;
  std::atomic<float> *rightDetunePtr = nullptr;
  std::atomic<float> *glidePtr = nullptr;
  std::atomic<float> *clockModePtr = nullptr;
  std::atomic<float> *extInputEnablePtr = nullptr;
  std::atomic<float> *extInputLevelPtr = nullptr;
  std::atomic<float> *digiEnablePtr = nullptr;
  std::atomic<float> *digiRootNotePtr = nullptr;
  std::atomic<float> *digiLoopPtr = nullptr;
  std::atomic<float> *digiBitDepthPtr = nullptr;
  std::atomic<float> *leftPanPtr = nullptr;
  std::atomic<float> *rightPanPtr = nullptr;
  std::atomic<float> *pitchBendRangePtr = nullptr;

  std::atomic<float> *lfoEnablePtr = nullptr;
  std::atomic<float> *lfoWavePtr = nullptr;
  std::atomic<float> *lfoRatePtr = nullptr;
  std::atomic<float> *lfoDepthFiltPtr = nullptr;
  std::atomic<float> *lfoDepthPWPtr = nullptr;
  std::atomic<float> *lfoDepthPitchPtr = nullptr;

  // LFO2 APVTS pointers
  std::atomic<float> *lfo2EnablePtr = nullptr;
  std::atomic<float> *lfo2WavePtr = nullptr;
  std::atomic<float> *lfo2RatePtr = nullptr;
  std::atomic<float> *lfo2DepthFiltPtr = nullptr;
  std::atomic<float> *lfo2DepthPWPtr = nullptr;
  std::atomic<float> *lfo2DepthPitchPtr = nullptr;

  // LFO tempo sync APVTS pointers
  std::atomic<float> *lfoSyncPtr     = nullptr;
  std::atomic<float> *lfoSyncDivPtr  = nullptr;
  std::atomic<float> *lfo2SyncPtr    = nullptr;
  std::atomic<float> *lfo2SyncDivPtr = nullptr;

  // Sync division lookup: beats-per-cycle indexed 0..10
  // {"4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/4D","1/8D","1/4T","1/8T"}
  static constexpr float kSyncDivBeats[11] = {
      16.0f, 8.0f, 4.0f, 2.0f,
      1.0f,                        // 1/4 quarter note (default, index 4)
      0.5f, 0.25f,
      1.5f, 0.75f,                 // dotted quarter, dotted eighth
      2.0f / 3.0f, 1.0f / 3.0f   // quarter triplet, eighth triplet
  };

  // PWM Sweep APVTS pointers
  std::atomic<float> *pwmSweepEnablePtr = nullptr;
  std::atomic<float> *pwmSweepRatePtr = nullptr;
  std::atomic<float> *pwmSweepDepthPtr = nullptr;

  // Chord Memory APVTS pointers
  std::atomic<float> *chordEnablePtr = nullptr;
  std::atomic<float> *chordSlotPtr = nullptr;
  struct ChordSlotPtrs {
    std::array<std::atomic<float> *, 5> intervals;
  };
  std::array<ChordSlotPtrs, 4> chordSlotPtrs;

  std::atomic<float> *arpEnablePtr = nullptr;
  std::atomic<float> *arpPatternPtr = nullptr;
  std::atomic<float> *arpRatePtr = nullptr;
  std::atomic<float> *arpOctavesPtr = nullptr;

  // Filter Envelope APVTS pointers
  std::atomic<float> *filterEnvEnablePtr = nullptr;
  std::atomic<float> *filterEnvAttackPtr = nullptr;
  std::atomic<float> *filterEnvDecayPtr = nullptr;
  std::atomic<float> *filterEnvSustainPtr = nullptr;
  std::atomic<float> *filterEnvReleasePtr = nullptr;
  std::atomic<float> *filterEnvAmountPtr = nullptr;

  struct VoiceParamPtrs {
    std::atomic<float> *enable = nullptr;
    std::atomic<float> *waveform = nullptr;
    std::atomic<float> *pw = nullptr;
    std::atomic<float> *attack = nullptr;
    std::atomic<float> *decay = nullptr;
    std::atomic<float> *sustain = nullptr;
    std::atomic<float> *release = nullptr;
    // per-voice pan removed (now per-SID: leftPan/rightPan)
    std::atomic<float> *ringMod = nullptr;
    std::atomic<float> *sync = nullptr;
    std::atomic<float> *modOffset =
        nullptr; // Semitone offset for modulator voice (sync/ring)
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

  // Wavetable APVTS pointers
  std::atomic<float> *wtEnablePtr = nullptr;
  std::atomic<float> *wtNumStepsPtr = nullptr;
  std::atomic<float> *wtRatePtr = nullptr;
  std::atomic<float> *wtLoopPtr = nullptr;
  struct WTStepPtrs {
    std::atomic<float> *wave = nullptr;
    std::atomic<float> *pitch = nullptr;
    std::atomic<float> *pw = nullptr;
  };
  std::array<WTStepPtrs, 16> wtStepPtrs;

  // Mod Matrix APVTS pointers
  struct ModSlotPtrs {
    std::atomic<float> *enable = nullptr;
    std::atomic<float> *src = nullptr;
    std::atomic<float> *dst = nullptr;
    std::atomic<float> *amt = nullptr;
  };
  std::array<ModSlotPtrs, kModSlots> modSlotPtrs;
  void
  applyModMatrix(); // Apply mod matrix routing after all mod sources computed

  // FX CHAIN
  juce::dsp::Chorus<float> chorus;
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>
      delayLineL{88200};
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>
      delayLineR{88200};
  gm::ReverbSC reverb;

  // FX APVTS pointers
  std::atomic<float> *chorusEnablePtr = nullptr;
  std::atomic<float> *chorusRatePtr = nullptr;
  std::atomic<float> *chorusDepthPtr = nullptr;
  std::atomic<float> *chorusMixPtr = nullptr;
  std::atomic<float> *delayEnablePtr = nullptr;
  std::atomic<float> *delayTimeLPtr = nullptr;
  std::atomic<float> *delayTimeRPtr = nullptr;
  std::atomic<float> *delayFeedbackPtr = nullptr;
  std::atomic<float> *delayMixPtr = nullptr;
  std::atomic<float> *reverbEnablePtr = nullptr;
  std::atomic<float> *reverbDecayPtr = nullptr;
  std::atomic<float> *reverbDampingPtr = nullptr;
  std::atomic<float> *reverbMixPtr = nullptr;

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
  void handleNoteOn(int note, int channel);
  void handleNoteOff(int note, int channel);
  void handleAllNotesOff();
  void handleSustainPedal(int value);
  void triggerNote(int voiceIndex, int midiNote, int velocity);
  void releaseNote(int voiceIndex);
  void updateSIDFromQueue(bool isLeftSID); // Trigger all enabled voices on SID
  void prepareSafetyChain(double sampleRate, int samplesPerBlock);
  void updateAllVoiceFrequencies(); // Apply pitch bend to all active voices
  void tickLFO(LFOState &state, std::mt19937 &rng, int numSamples);
  void applyLFOModulation();    // Apply LFO1+LFO2 to PW and pitch destinations
  void applyFilterModulation(); // Unified filter cutoff modulation (mod wheel +
                                // LFO + filter env)
  int computeFilterModOffset() const; // Mod wheel + LFO1 + LFO2 filter offset
  void updatePanCache(float panValue, float &cachedPan, float &gainL, float &gainR);
  void processFilterEnvelope(int numSamples); // Advance filter envelope

  // processBlock subsystems (extracted for readability)
  void generateAudio(juce::AudioBuffer<float> &buffer);
  void mixSidFilePlayer(juce::AudioBuffer<float> &buffer);
  void processFXChain(juce::AudioBuffer<float> &buffer);
  void applySafetyChain(juce::AudioBuffer<float> &buffer);
  bool hasActiveMonoSidSource(bool digiPlaying,
                              const float *inputLeft) const;
  bool shouldSkipMonoSidRender(juce::AudioBuffer<float> &buffer,
                               bool sourceActive,
                               float targetLeftVG,
                               float targetRightVG);

  // Arpeggiator
  bool arpEnabled = false;
  ArpPattern arpPattern = ArpPattern::Up;
  float arpRateHz = 50.0f; // PAL default
  int arpOctaves = 1;
  // Fixed-size arrays to avoid heap allocations on RT thread
  std::array<int, 128> arpHeldNotes{}; // Notes currently held (max MIDI notes)
  int arpHeldCount = 0;
  std::array<int, 512>
      arpSequence{}; // Generated sequence (128 notes * 4 octaves max)
  int arpSeqCount = 0;
  int arpIndex = 0;
  double arpTimer = 0.0;
  int lastArpNote = -1;
  void processArpeggiator(int numSamples);
  void rebuildArpSequence();
  // Pre-allocated work arrays for rebuildArpSequence (avoids 2.4 KB stack alloc)
  std::array<int, 128> arpSortBuf{};
  std::array<int, 512> arpBaseBuf{};
  // RT-safe RNG (seeded in constructor, never touches std::random_device on audio thread)
  std::mt19937 arpRng{12345u};

  // LFO state
  LFOState lfo;
  LFOState lfo2;
  std::mt19937 lfoRng{67890u};
  std::mt19937 lfo2Rng{24680u};

  // PWM Sweep state
  double pwmSweepPhase = 0.0;
  float pwmSweepCurrentValue = 0.0f; // -1 to +1 triangle

  // Chord Memory state
  struct ChordMemoryState {
    bool enabled = false;
    int activeSlot = 0;
    std::array<std::array<int, 5>, 4> intervals{}; // [slot][idx], 0=unused
  };
  ChordMemoryState chordMemory;
  void triggerChord(bool isLeftSID, int rootNote, int velocity);
  void triggerChordDualSID(int rootNote, int velocity);
  void releaseChord(bool isLeftSID);

  // Chord Learn mode (data is private, methods exposed below)
  bool chordLearnActive = false;
  int chordLearnSlot = 0;
  std::array<int, 32>
      chordLearnNotes{}; // Fixed-size, max 32 notes during learn
  int chordLearnCount = 0;
  std::mutex chordLearnMutex; // Only locked from GUI thread; RT uses try_lock

  // Filter Envelope state
  FilterEnvelopeState filterEnv;

  // Wavetable step sequencer state
  WavetableState wavetable;
  void processWavetable(int numSamples);

  // Master Control & MIDI
  float masterVolume = 0.8f;

  // Gain smoothing state (prevents pops on parameter/voice-count changes)
  // ~5ms exponential smoothing time constant
  float gainSmoothCoeff = 0.0f; // computed in prepareToPlay
  float smoothedMasterVol = 0.8f;
  float smoothedPolyNorm = 1.0f;
  float smoothedLeftVoiceGain = 1.0f;
  float smoothedRightVoiceGain = 1.0f;
  bool sustainActive = false;

  gm::SilenceGate sidRenderSilenceGate;
  float sidRenderTailPeak = 0.0f;
  bool sidRenderWasSkipping = false;

  // Noise gate state (per-channel envelope follower)
  struct NoiseGateState {
    float envelope = 0.0f;
    float gain = 0.0f;
    int holdCounter = 0;
  };
  std::array<NoiseGateState, 2> gateState;
  // Cached noise gate coefficients (recalculated only on parameter change)
  struct GateCoeffCache {
    float prevAttack = -1.0f, prevRelease = -1.0f, prevHold = -1.0f;
    float prevThreshold = -1.0f;
    float envAttackCoeff = 0.0f, envReleaseCoeff = 0.0f;
    float gainAttackCoeff = 0.0f, gainReleaseCoeff = 0.0f;
    float closeThreshold = 0.0f;
    int holdSamples = 0;
  };
  GateCoeffCache gateCache;

  struct CpuAuditCounters {
    uint64_t blocks = 0;
    uint64_t activePolyVoices = 0;
    uint64_t activePolyNoteSlots = 0;
    uint64_t polySidRenderSkipBlocks = 0;
  };
  CpuAuditCounters cpuAuditCounters;
  gm::CpuSectionProfiler cpuProfiler;
  CpuSections cpuSections;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinProcessor)
};
