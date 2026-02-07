#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Custom LookAndFeel for professional ComboBox fonts
class BreadbinLookAndFeel : public juce::LookAndFeel_V4 {
public:
  void setProFont(const juce::Font &font) { proFont = font; }

  juce::Font getComboBoxFont(juce::ComboBox &) override {
    return proFont.withHeight(14.0f);
  }

  juce::Font getPopupMenuFont() override { return proFont.withHeight(14.0f); }

private:
  juce::Font proFont;
};

// Custom Slider class for MIDI Learning
class MappableSlider : public juce::Slider {
public:
  MappableSlider(BreadbinProcessor &p, BreadbinProcessor::ControlParam param)
      : processor(p), controlParam(param) {}

  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown()) {
      juce::PopupMenu m;
      m.addItem("MIDI Learn",
                [this] { processor.startLearning(controlParam); });
      m.addItem("Unlearn",
                [this] { processor.clearMIDIMappingForParam(controlParam); });
      m.showMenuAsync(juce::PopupMenu::Options{});
    } else {
      juce::Slider::mouseDown(e);
    }
  }

  void paint(juce::Graphics &g) override {
    juce::Slider::paint(g);
    if (processor.isLearning() &&
        processor.getLearningParam() == controlParam) {
      g.setColour(juce::Colours::gold.withAlpha(0.4f));
      g.drawRect(getLocalBounds(), 2);

      auto font = g.getCurrentFont();
      font.setHeight(10.0f);
      font.setBold(true);
      g.setFont(font);
      g.drawText("LEARN", getLocalBounds(), juce::Justification::centred);
    }
  }

private:
  BreadbinProcessor &processor;
  BreadbinProcessor::ControlParam controlParam;
};

class BreadbinEditor : public juce::AudioProcessorEditor,
                       private juce::MidiKeyboardState::Listener,
                       private juce::Timer {
public:
  explicit BreadbinEditor(BreadbinProcessor &);
  ~BreadbinEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  void timerCallback() override;

private:
  BreadbinProcessor &processor;

  // Retro font for section headers (Press Start 2P)
  juce::Font retroFont;
  // Professional font for UI controls (Inter)
  juce::Font proFont;
  // Custom look and feel for ComboBox fonts
  BreadbinLookAndFeel customLookAndFeel;

  // Currently selected voice (0-5)
  int selectedVoice = 0;

  // Title and global controls
  juce::Label titleLabel;
  juce::ComboBox dualModeSelector;
  juce::Label modeLabel;
  juce::ComboBox globalPresetSelector; // Factory global presets
  juce::Label globalPresetLabel;
  juce::ComboBox presetSelector; // Voice presets
  juce::Label presetLabel;
  juce::ShapeButton savePatchButton{"Save Patch", juce::Colours::cyan,
                                    juce::Colours::cyan.withAlpha(0.7f),
                                    juce::Colours::white};
  juce::ShapeButton loadPatchButton{"Load Patch", juce::Colours::cyan,
                                    juce::Colours::cyan.withAlpha(0.7f),
                                    juce::Colours::white};
  juce::ShapeButton saveVoiceButton{"Save", juce::Colours::cyan,
                                    juce::Colours::cyan.withAlpha(0.7f),
                                    juce::Colours::white};
  juce::ShapeButton loadVoiceButton{"Load", juce::Colours::cyan,
                                    juce::Colours::cyan.withAlpha(0.7f),
                                    juce::Colours::white};

  // Time Machine (aging)
  MappableSlider agingSlider{processor, BreadbinProcessor::ControlParam::Aging};
  juce::Label agingLabel{"Aging", "AGING"};
  juce::Label agingStartLabel;
  juce::Label agingEndLabel;

  // Master Volume
  juce::Label masterVolLabel{"Master", "MASTER"};
  MappableSlider masterVolSlider{processor,
                                 BreadbinProcessor::ControlParam::MasterVolume};

  // ========== LEFT SID SECTION ==========
  juce::Label leftSIDLabel;
  juce::ComboBox leftChipSelector;
  // L SID Voices (0-2)
  std::array<juce::TextButton, 3> leftVoiceButtons;
  std::array<juce::ToggleButton, 3> leftVoiceEnables;
  // L SID Filter
  MappableSlider leftCutoffSlider{processor,
                                  BreadbinProcessor::ControlParam::LeftCutoff};
  MappableSlider leftResonanceSlider{
      processor, BreadbinProcessor::ControlParam::LeftResonance};
  juce::ToggleButton leftLPButton{"LP"};
  juce::ToggleButton leftBPButton{"BP"};
  juce::ToggleButton leftHPButton{"HP"};
  juce::ToggleButton leftFilterEnableButton{"Flt"};
  juce::Label leftCutoffLabel, leftResonanceLabel;
  MappableSlider leftDetuneSlider{processor,
                                  BreadbinProcessor::ControlParam::LeftDetune};
  juce::Label leftDetuneLabel;

  // ========== RIGHT SID SECTION ==========
  juce::Label rightSIDLabel;
  juce::ComboBox rightChipSelector;
  // R SID Voices (3-5)
  std::array<juce::TextButton, 3> rightVoiceButtons;
  std::array<juce::ToggleButton, 3> rightVoiceEnables;
  // R SID Filter
  MappableSlider rightCutoffSlider{
      processor, BreadbinProcessor::ControlParam::RightCutoff};
  MappableSlider rightResonanceSlider{
      processor, BreadbinProcessor::ControlParam::RightResonance};
  juce::ToggleButton rightLPButton{"LP"};
  juce::ToggleButton rightBPButton{"BP"};
  juce::ToggleButton rightHPButton{"HP"};
  juce::ToggleButton rightFilterEnableButton{"Flt"};
  juce::Label rightCutoffLabel, rightResonanceLabel;
  MappableSlider rightDetuneSlider{
      processor, BreadbinProcessor::ControlParam::RightDetune};
  juce::Label rightDetuneLabel;

  // ========== VOICE EDITOR (edits selected voice) ==========
  juce::Label voiceEditorLabel;
  juce::ComboBox waveformSelector;
  MappableSlider pulseWidthSlider{processor,
                                  BreadbinProcessor::ControlParam::VoicePW};
  MappableSlider attackSlider{processor,
                              BreadbinProcessor::ControlParam::VoiceAttack};
  MappableSlider decaySlider{processor,
                             BreadbinProcessor::ControlParam::VoiceDecay};
  MappableSlider sustainSlider{processor,
                               BreadbinProcessor::ControlParam::VoiceSustain};
  MappableSlider releaseSlider{processor,
                               BreadbinProcessor::ControlParam::VoiceRelease};
  MappableSlider panSlider{processor,
                           BreadbinProcessor::ControlParam::VoicePan};
  juce::Label waveformLabel, pwLabel;
  juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
  juce::Label panLabel;
  juce::ToggleButton ringModButton{"Ring"};
  juce::ToggleButton syncButton{"Sync"};
  juce::ToggleButton voiceFilterButton{"Flt"};

  // ========== ARPEGGIATOR ==========
  juce::ToggleButton arpEnableButton{"Arp"};
  juce::ComboBox arpPatternSelector;
  MappableSlider arpRateSlider{processor,
                               BreadbinProcessor::ControlParam::ArpRate};
  juce::ComboBox arpOctaveSelector;
  juce::Label arpRateLabel, arpPatternLabel, arpOctaveLabel;

  // ========== GLIDE/PORTAMENTO ==========
  MappableSlider glideTimeSlider{processor,
                                 BreadbinProcessor::ControlParam::GlobalGlide};
  juce::Label glideTimeLabel;

  // ========== PITCH BEND ==========
  juce::ComboBox pitchBendRangeSelector;
  juce::Label pitchBendRangeLabel;

  // ========== EXTERNAL AUDIO INPUT ==========
  juce::ToggleButton extInputEnableButton{"Ext In"};
  MappableSlider extInputLevelSlider{
      processor, BreadbinProcessor::ControlParam::ExtInputLevel};
  juce::Label extInputLabel;

  // ========== CLOCK MODE (PAL/NTSC) ==========
  juce::ComboBox clockModeSelector;
  juce::Label clockModeLabel;

  // Editor constants
  static constexpr int width = 1000;
  static constexpr int height = 550;

  // ========== LFO ==========
  juce::ToggleButton lfoEnableButton{"LFO"};
  juce::ComboBox lfoWaveformSelector;
  MappableSlider lfoRateSlider{processor,
                               BreadbinProcessor::ControlParam::LFORate};
  juce::Label lfoRateLabel;
  MappableSlider lfoDepthFilterSlider{
      processor, BreadbinProcessor::ControlParam::LFODepthFilter};
  MappableSlider lfoDepthPWSlider{processor,
                                  BreadbinProcessor::ControlParam::LFODepthPW};
  MappableSlider lfoDepthPitchSlider{
      processor, BreadbinProcessor::ControlParam::LFODepthPitch};
  juce::Label lfoDepthFilterLabel, lfoDepthPWLabel, lfoDepthPitchLabel;

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
  juce::Path makeDiskPath();
  juce::Path makeFolderPath();

  void updateVoiceButtonStates();
  void updateFiltersFromUI();
  void applyPreset(int presetId);
  void applyGlobalPreset(int presetId);
  void savePresetToFile();        // Overall Patch
  void loadPresetFromFile();      // Overall Patch
  void saveVoicePresetToFile();   // Selected Voice
  void loadVoicePresetFromFile(); // Selected Voice

  // MidiKeyboardState::Listener
  void handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                    int midiNoteNumber, float velocity) override;
  void handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                     int midiNoteNumber, float velocity) override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinEditor)
};
