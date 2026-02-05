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

  // Currently selected voice (0-5)
  int selectedVoice = 0;

  // Title and global controls
  juce::Label titleLabel;
  juce::ComboBox dualModeSelector;
  juce::Label modeLabel;
  juce::ComboBox presetSelector;
  juce::Label presetLabel;
  juce::TextButton savePresetButton{"Save"};
  juce::TextButton loadPresetButton{"Load"};

  // Time Machine (aging)
  juce::Slider agingSlider;
  juce::Label agingLabel;
  juce::Label agingStartLabel;
  juce::Label agingEndLabel;

  // ========== LEFT SID SECTION ==========
  juce::Label leftSIDLabel;
  juce::ComboBox leftChipSelector;
  // L SID Voices (0-2)
  std::array<juce::TextButton, 3> leftVoiceButtons;
  std::array<juce::ToggleButton, 3> leftVoiceEnables;
  // L SID Filter
  juce::Slider leftCutoffSlider;
  juce::Slider leftResonanceSlider;
  juce::ToggleButton leftLPButton{"LP"};
  juce::ToggleButton leftBPButton{"BP"};
  juce::ToggleButton leftHPButton{"HP"};
  juce::ToggleButton leftFilterEnableButton{"Flt"};
  juce::Label leftCutoffLabel, leftResonanceLabel;
  juce::Slider leftDetuneSlider;
  juce::Label leftDetuneLabel;

  // ========== RIGHT SID SECTION ==========
  juce::Label rightSIDLabel;
  juce::ComboBox rightChipSelector;
  // R SID Voices (3-5)
  std::array<juce::TextButton, 3> rightVoiceButtons;
  std::array<juce::ToggleButton, 3> rightVoiceEnables;
  // R SID Filter
  juce::Slider rightCutoffSlider;
  juce::Slider rightResonanceSlider;
  juce::ToggleButton rightLPButton{"LP"};
  juce::ToggleButton rightBPButton{"BP"};
  juce::ToggleButton rightHPButton{"HP"};
  juce::ToggleButton rightFilterEnableButton{"Flt"};
  juce::Label rightCutoffLabel, rightResonanceLabel;
  juce::Slider rightDetuneSlider;
  juce::Label rightDetuneLabel;

  // ========== VOICE EDITOR (edits selected voice) ==========
  juce::Label voiceEditorLabel;
  juce::ComboBox waveformSelector;
  juce::Slider pulseWidthSlider;
  juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
  juce::Slider panSlider;
  juce::Label waveformLabel, pwLabel;
  juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
  juce::Label panLabel;

  // ========== ARPEGGIATOR ==========
  juce::ToggleButton arpEnableButton{"Arp"};
  juce::ComboBox arpPatternSelector;
  juce::Slider arpRateSlider;
  juce::ComboBox arpOctaveSelector;
  juce::Label arpRateLabel;

  // ========== GLIDE/PORTAMENTO ==========
  juce::Slider glideTimeSlider;
  juce::Label glideTimeLabel;

  // Virtual keyboard
  juce::MidiKeyboardState keyboardState;
  juce::MidiKeyboardComponent keyboard;

  // Background image
  juce::Image backgroundImage;

  void setupControls();
  void setupLeftSID();
  void setupRightSID();
  void setupVoiceEditor();
  void selectVoice(int voice);
  void loadVoiceToUI(int voice);
  void saveUIToVoice(int voice);
  void updateVoiceButtonStates();
  void updateFiltersFromUI();
  void applyPreset(int presetId);
  void savePresetToFile();
  void loadPresetFromFile();

  // MidiKeyboardState::Listener
  void handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                    int midiNoteNumber, float velocity) override;
  void handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                     int midiNoteNumber, float velocity) override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinEditor)
};
