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
    float pan = 0.0f; // -1.0 = full left, 0.0 = center, 1.0 = full right
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

  double hostSampleRate = 44100.0;
  juce::MidiMessageCollector midiCollector;

  // Per-voice settings storage
  std::array<VoiceSettings, 6> voiceSettings;

  // Note queues for last-note priority (one per SID)
  juce::Array<int> leftNoteQueue;  // Notes held on left SID
  juce::Array<int> rightNoteQueue; // Notes held on right SID
  int lastVelocity = 100;

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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinProcessor)
};
