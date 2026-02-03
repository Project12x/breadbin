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

  // Per-channel chip selection
  juce::ComboBox leftChipSelector;
  juce::ComboBox rightChipSelector;
  juce::Label leftChipLabel;
  juce::Label rightChipLabel;

  // Presets
  juce::ComboBox presetSelector;
  juce::Label presetLabel;

  // Synth controls - Voice 1
  juce::ComboBox waveformSelector;
  juce::Slider pulseWidthSlider;
  juce::Slider attackSlider;
  juce::Slider decaySlider;
  juce::Slider sustainSlider;
  juce::Slider releaseSlider;
  juce::Label waveformLabel;
  juce::Label adsrLabel;

  // Filter controls
  juce::Slider filterCutoffSlider;
  juce::Slider filterResonanceSlider;
  juce::ToggleButton filterLPButton{"LP"};
  juce::ToggleButton filterBPButton{"BP"};
  juce::ToggleButton filterHPButton{"HP"};
  juce::Label filterLabel;

  // Virtual keyboard for standalone testing
  juce::MidiKeyboardState keyboardState;
  juce::MidiKeyboardComponent keyboard;

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
