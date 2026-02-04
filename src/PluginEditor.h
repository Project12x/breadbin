#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

class BreadbinEditor : public juce::AudioProcessorEditor,
                       private juce::MidiKeyboardState::Listener {
public:
  explicit BreadbinEditor(BreadbinProcessor &);
  ~BreadbinEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  BreadbinProcessor &processor;

  // Mode selectors
  juce::ComboBox dualModeSelector;
  juce::Slider agingSlider;
  juce::Label titleLabel;
  juce::Label modeLabel;
  juce::Label agingLabel;
  juce::Label agingStartLabel; // "1982"
  juce::Label agingEndLabel;   // "NOW"

  // Per-channel chip selection
  juce::ComboBox leftChipSelector;
  juce::ComboBox rightChipSelector;
  juce::Label leftChipLabel;
  juce::Label rightChipLabel;

  // Presets - per voice
  juce::ComboBox leftPresetSelector;
  juce::ComboBox rightPresetSelector;
  juce::Label leftPresetLabel;
  juce::Label rightPresetLabel;

  // Synth controls - per voice waveform
  juce::ComboBox leftWaveformSelector;
  juce::ComboBox rightWaveformSelector;
  juce::Slider leftPulseWidthSlider;
  juce::Slider rightPulseWidthSlider;
  juce::Label leftWaveformLabel, rightWaveformLabel;
  juce::Label leftPWLabel, rightPWLabel;

  // ADSR - per voice (Left SID)
  juce::Slider leftAttackSlider, leftDecaySlider, leftSustainSlider,
      leftReleaseSlider;
  juce::Label leftADSRLabel;
  juce::Label leftAttackLabel, leftDecayLabel, leftSustainLabel,
      leftReleaseLabel;

  // ADSR - per voice (Right SID)
  juce::Slider rightAttackSlider, rightDecaySlider, rightSustainSlider,
      rightReleaseSlider;
  juce::Label rightADSRLabel;
  juce::Label rightAttackLabel, rightDecayLabel, rightSustainLabel,
      rightReleaseLabel;

  // Filter controls - Left SID
  juce::Slider leftCutoffSlider;
  juce::Slider leftResonanceSlider;
  juce::ToggleButton leftLPButton{"LP"};
  juce::ToggleButton leftBPButton{"BP"};
  juce::ToggleButton leftHPButton{"HP"};
  juce::Label leftFilterLabel;
  juce::Label leftCutoffLabel, leftResonanceLabel;

  // Filter controls - Right SID
  juce::Slider rightCutoffSlider;
  juce::Slider rightResonanceSlider;
  juce::ToggleButton rightLPButton{"LP"};
  juce::ToggleButton rightBPButton{"BP"};
  juce::ToggleButton rightHPButton{"HP"};
  juce::Label rightFilterLabel;
  juce::Label rightCutoffLabel, rightResonanceLabel;

  // Virtual keyboard for standalone testing
  juce::MidiKeyboardState keyboardState;
  juce::MidiKeyboardComponent keyboard;

  // Background image
  juce::Image backgroundImage;

  void setupControls();
  void setupSynthControls();
  void setupFilterControls();
  void updateSynthFromControls();
  void applyPreset(int presetId);

  // MidiKeyboardState::Listener
  void handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                    int midiNoteNumber, float velocity) override;
  void handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                     int midiNoteNumber, float velocity) override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinEditor)
};
