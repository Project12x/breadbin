#pragma once

#include "SIDEngine.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class BreadbinProcessor : public juce::AudioProcessor {
public:
  // Dual SID routing modes
  enum class DualMode { StereoSplit, Unison, Multitimbral };

  // Arpeggiator patterns
  enum class ArpPattern { Up, Down, UpDown, Random };

  // Arpeggiator rate modes
  enum class ArpRateMode {
    PAL50Hz,  // C64 PAL: 50 Hz (authentic)
    NTSC60Hz, // C64 NTSC: 60 Hz (authentic)
    Sync      // Sync to host tempo
  };

  BreadbinProcessor();
  ~BreadbinProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

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
  void setDualMode(DualMode mode);

  // Per-SID pan accessors
  float getLeftSIDPan() const { return leftSIDPan; }
  float getRightSIDPan() const { return rightSIDPan; }
  void setLeftSIDPan(float pan) { leftSIDPan = juce::jlimit(-1.0f, 1.0f, pan); }
  void setRightSIDPan(float pan) {
    rightSIDPan = juce::jlimit(-1.0f, 1.0f, pan);
  }

  // Per-channel chip model accessors
  SIDEngine::ChipModel getLeftChipModel() const { return chipModelLeft; }
  SIDEngine::ChipModel getRightChipModel() const { return chipModelRight; }
  void setLeftChipModel(SIDEngine::ChipModel model);
  void setRightChipModel(SIDEngine::ChipModel model);
  // Convenience: set both at once
  void setBothChipModels(SIDEngine::ChipModel model);

  float getAgingFactor() const { return agingFactor; }
  void setAgingFactor(float aging);

  // Arpeggiator accessors
  bool getArpEnabled() const { return arpEnabled; }
  void setArpEnabled(bool enabled) { arpEnabled = enabled; }
  ArpPattern getArpPattern() const { return arpPattern; }
  void setArpPattern(ArpPattern pattern) { arpPattern = pattern; }
  ArpRateMode getArpRateMode() const { return arpRateMode; }
  void setArpRateMode(ArpRateMode mode) { arpRateMode = mode; }
  int getArpOctaveRange() const { return arpOctaveRange; }
  void setArpOctaveRange(int octaves) {
    arpOctaveRange = juce::jlimit(1, 3, octaves);
  }

  // MIDI collector for virtual keyboard (standalone)
  juce::MidiMessageCollector &getMidiMessageCollector() {
    return midiCollector;
  }

  // Synth parameter accessors for GUI
  SIDEngine &getLeftSID() { return sidLeft; }
  SIDEngine &getCenterSID() { return sidCenter; }
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
    float pan = 0.0f;      // -1.0 = full left, 0.0 = center, 1.0 = full right
    bool ringMod = false;  // Ring modulation with previous voice
    bool hardSync = false; // Hard sync from previous voice
  };
  VoiceSettings &getVoiceSettings(int voice) { return voiceSettings[voice]; }
  const VoiceSettings &getVoiceSettings(int voice) const {
    return voiceSettings[voice];
  }
  void applyVoiceSettings(int voice); // Apply settings to SID engine

private:
  SIDEngine sidLeft;
  SIDEngine sidCenter;
  SIDEngine sidRight;

  DualMode dualMode = DualMode::StereoSplit;
  SIDEngine::ChipModel chipModelLeft = SIDEngine::ChipModel::MOS6581;
  SIDEngine::ChipModel chipModelCenter = SIDEngine::ChipModel::MOS6581;
  SIDEngine::ChipModel chipModelRight = SIDEngine::ChipModel::MOS6581;
  float agingFactor = 0.0f;

  // Per-SID stereo panning (-1.0 = left, 0.0 = center, +1.0 = right)
  float leftSIDPan = -0.75f; // Default: left SID panned 75% left
  float centerSIDPan = 0.0f; // Default: center SID panned center
  float rightSIDPan = 0.75f; // Default: right SID panned 75% right

  double hostSampleRate = 44100.0;
  juce::MidiMessageCollector midiCollector;

  // Arpeggiator state
  bool arpEnabled = false;
  ArpPattern arpPattern = ArpPattern::Up;
  ArpRateMode arpRateMode = ArpRateMode::PAL50Hz;
  int arpOctaveRange = 1;        // 1-3 octaves
  int arpSampleCounter = 0;      // Sample counter for timing
  int arpNoteIndex = 0;          // Current note in sequence
  int arpDirection = 1;          // For UpDown pattern: 1 = up, -1 = down
  juce::Array<int> arpHeldNotes; // Notes currently held by user
  juce::Array<int> arpSequence;  // Computed sequence (with octaves)
  int arpCurrentNote = -1;       // Note currently playing from arp

  // Per-voice settings storage (9 voices: 3 per SID)
  // Voices 0-2: Left SID, Voices 3-5: Center SID, Voices 6-8: Right SID
  std::array<VoiceSettings, 9> voiceSettings;

  // Note queues for last-note priority (one per SID)
  juce::Array<int> leftNoteQueue;   // Notes held on left SID
  juce::Array<int> centerNoteQueue; // Notes held on center SID
  juce::Array<int> rightNoteQueue;  // Notes held on right SID
  int lastVelocity = 100;

  // Voice state for MIDI (9 voices: 3 per SID)
  struct VoiceState {
    int note = -1;
    bool active = false;
  };
  std::array<VoiceState, 9> voices;

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
  // sidIndex: 0 = left, 1 = center, 2 = right
  void updateSIDFromQueue(int sidIndex);
  void prepareSafetyChain(double sampleRate, int samplesPerBlock);

  // Arpeggiator helpers
  void rebuildArpSequence(); // Build sequence from held notes + octave range
  void processArpeggiator(int numSamples); // Process arp timing per block
  void triggerArpNote(int note);           // Trigger arp note on all SIDs

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinProcessor)
};
