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

  // Mode/global controls
  juce::ComboBox dualModeSelector;
  juce::Slider agingSlider;
  juce::Label titleLabel;
  juce::Label modeLabel;
  juce::Label agingLabel;
  juce::Label agingStartLabel;
  juce::Label agingEndLabel;

  // Per-SID chip selection
  juce::ComboBox leftChipSelector;
  juce::ComboBox rightChipSelector;
  juce::Label leftChipLabel;
  juce::Label rightChipLabel;

  // Presets
  juce::ComboBox presetSelector;
  juce::Label presetLabel;

  // Voice Selector (6 buttons)
  std::array<juce::TextButton, 6> voiceButtons;
  juce::Label voiceSelectorLabel;

  // Per-voice controls (single set - edits selectedVoice)
  juce::ComboBox waveformSelector;
  juce::Slider pulseWidthSlider;
  juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
  juce::Slider panSlider;
  juce::Label waveformLabel, pwLabel;
  juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
  juce::Label panLabel;

  // Filter controls - Left SID (voices 1-3)
  juce::Slider leftCutoffSlider;
  juce::Slider leftResonanceSlider;
  juce::ToggleButton leftLPButton{"LP"};
  juce::ToggleButton leftBPButton{"BP"};
  juce::ToggleButton leftHPButton{"HP"};
  juce::Label leftFilterLabel;
  juce::Label leftCutoffLabel, leftResonanceLabel;

  // Filter controls - Right SID (voices 4-6)
  juce::Slider rightCutoffSlider;
  juce::Slider rightResonanceSlider;
  juce::ToggleButton rightLPButton{"LP"};
  juce::ToggleButton rightBPButton{"BP"};
  juce::ToggleButton rightHPButton{"HP"};
  juce::Label rightFilterLabel;
  juce::Label rightCutoffLabel, rightResonanceLabel;

  // Virtual keyboard
  juce::MidiKeyboardState keyboardState;
  juce::MidiKeyboardComponent keyboard;

  // Background image
  juce::Image backgroundImage;

  void setupControls();
  void setupVoiceSelector();
  void setupVoiceControls();
  void setupFilterControls();
  void selectVoice(int voice);
  void loadVoiceToUI(int voice);
  void saveUIToVoice(int voice);
  void updateFiltersFromUI();
  void applyPreset(int presetId);

  // MidiKeyboardState::Listener
  void handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                    int midiNoteNumber, float velocity) override;
  void handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                     int midiNoteNumber, float velocity) override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinEditor)
};
