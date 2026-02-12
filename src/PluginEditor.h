#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

class MappableSlider; // Forward declaration

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
// Overlay displayed during MIDI Learn
class MidiLearnOverlay : public juce::Component {
public:
  MidiLearnOverlay(BreadbinProcessor &p) : processor(p) {
    setInterceptsMouseClicks(false, false); // Don't block UI
  }

  void paint(juce::Graphics &g) override {
    if (!processor.isLearning())
      return;

    auto bounds = getLocalBounds().reduced(20);
    auto popupH = 60;
    auto popupW = 300;
    auto popupRect = juce::Rectangle<int>(bounds.getCentreX() - popupW / 2,
                                          bounds.getY() + 20, popupW, popupH);

    // Semi-transparent background
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.fillRoundedRectangle(popupRect.toFloat(), 6.0f);

    // Gold border
    g.setColour(juce::Colours::gold);
    g.drawRoundedRectangle(popupRect.toFloat(), 6.0f, 2.0f);

    // Text
    g.setColour(juce::Colours::white);
    auto font = g.getCurrentFont();
    font.setHeight(16.0f);
    font.setBold(true);
    g.setFont(font);

    juce::String paramName =
        processor.getParamName(processor.getLearningParam());
    g.drawText("LEARNING: " + paramName,
               popupRect.removeFromTop(35).reduced(10, 0),
               juce::Justification::centred);

    font.setHeight(12.0f);
    font.setBold(false);
    g.setFont(font);
    g.drawText("Move any MIDI hardware control to map...",
               popupRect.reduced(10, 0), juce::Justification::centred);
  }

private:
  BreadbinProcessor &processor;
};

class MappableSlider : public juce::Slider {
public:
  MappableSlider(BreadbinProcessor &p, BreadbinProcessor::ControlParam param)
      : juce::Slider(), processor(p), controlParam(param) {}

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
  juce::Slider leftPanSlider;
  juce::Label leftPanLabel;

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
  juce::Slider rightPanSlider;
  juce::Label rightPanLabel;

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
  // panSlider removed (per-SID pan now)
  juce::Label waveformLabel, pwLabel;
  juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
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

  // ========== APVTS ATTACHMENTS ==========
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      masterVolAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      dualModeAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      chipLeftAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      chipRightAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      agingAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      leftDetuneAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      rightDetuneAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      leftPanAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      rightPanAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      glideAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      clockModeAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      extInputEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      extInputLevelAttach;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      lfoEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      lfoWaveAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfoRateAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfoDepthFiltAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfoDepthPWAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfoDepthPitchAttach;

  // LFO2 APVTS attachments
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      lfo2EnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      lfo2WaveAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfo2RateAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfo2DepthFiltAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfo2DepthPWAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfo2DepthPitchAttach;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      arpEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      arpPatternAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      arpRateAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      arpOctaveAttach;

  // Dynamic Voice Attachments (re-attached when selectedVoice changes)
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      voiceWaveformAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      voicePWAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      voiceAttackAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      voiceDecayAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      voiceSustainAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      voiceReleaseAttach;
  // voicePanAttach removed (per-SID pan now)
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      voiceRingModAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      voiceSyncAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      voiceFilterAttach;

  void refreshVoiceEditorAttachments();

  // Editor constants
  static constexpr int width = 1000;
  static constexpr int height = 743;

  // ========== FX: CHORUS ==========
  juce::ToggleButton chorusEnableButton{"Chorus"};
  juce::Slider chorusRateSlider, chorusDepthSlider, chorusMixSlider;
  juce::Label chorusRateLabel, chorusDepthLabel, chorusMixLabel;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      chorusEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      chorusRateAttach, chorusDepthAttach, chorusMixAttach;

  // ========== FX: DELAY ==========
  juce::ToggleButton delayEnableButton{"Delay"};
  juce::Slider delayTimeLSlider, delayTimeRSlider;
  juce::Slider delayFeedbackSlider, delayMixSlider;
  juce::Label delayTimeLLabel, delayTimeRLabel, delayFBLabel, delayMixLabel;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      delayEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      delayTimeLAttach, delayTimeRAttach, delayFBAttach, delayMixAttach;

  // ========== WAVETABLE STEP SEQUENCER ==========
  juce::ToggleButton wtEnableButton{"WaveTab"};
  juce::Slider wtNumStepsSlider;
  juce::Slider wtRateSlider;
  juce::ToggleButton wtLoopButton{"Loop"};
  juce::Label wtStepsLabel, wtRateLabel;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      wtEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      wtNumStepsAttach, wtRateAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      wtLoopAttach;

  // ========== FILTER ENVELOPE ==========
  juce::ToggleButton filterEnvEnableButton{"Filt Env"};
  juce::Slider filterEnvAttackSlider;
  juce::Slider filterEnvDecaySlider;
  juce::Slider filterEnvSustainSlider;
  juce::Slider filterEnvReleaseSlider;
  juce::Slider filterEnvAmountSlider;
  juce::Label filterEnvAttackLabel, filterEnvDecayLabel;
  juce::Label filterEnvSustainLabel, filterEnvReleaseLabel;
  juce::Label filterEnvAmountLabel;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      filterEnvEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      filterEnvAttackAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      filterEnvDecayAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      filterEnvSustainAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      filterEnvReleaseAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      filterEnvAmountAttach;

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

  // LFO2 UI components
  juce::ToggleButton lfo2EnableButton{"LFO2"};
  juce::ComboBox lfo2WaveformSelector;
  MappableSlider lfo2RateSlider{processor,
                                BreadbinProcessor::ControlParam::None};
  juce::Label lfo2RateLabel;
  MappableSlider lfo2DepthFilterSlider{
      processor, BreadbinProcessor::ControlParam::None};
  MappableSlider lfo2DepthPWSlider{processor,
                                   BreadbinProcessor::ControlParam::None};
  MappableSlider lfo2DepthPitchSlider{
      processor, BreadbinProcessor::ControlParam::None};
  juce::Label lfo2DepthFilterLabel, lfo2DepthPWLabel, lfo2DepthPitchLabel;

  // Virtual keyboard
  juce::MidiKeyboardState keyboardState;
  juce::MidiKeyboardComponent keyboard;

  MidiLearnOverlay midiLearnOverlay{processor};

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
