#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

class MappableSlider; // Forward declaration

// Custom LookAndFeel: Synthwave/Neon + Retro Hardware aesthetic
class BreadbinLookAndFeel : public juce::LookAndFeel_V4 {
public:
  BreadbinLookAndFeel();
  void setProFont(const juce::Font &font) { proFont = font; }

  juce::Font getComboBoxFont(juce::ComboBox &) override {
    return proFont.withHeight(14.0f);
  }
  juce::Font getPopupMenuFont() override { return proFont.withHeight(14.0f); }

  void drawRotarySlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPosProportional, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &) override;

  void drawLinearSlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        juce::Slider::SliderStyle, juce::Slider &) override;

  void drawToggleButton(juce::Graphics &, juce::ToggleButton &,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

  void drawButtonBackground(juce::Graphics &, juce::Button &,
                            const juce::Colour &backgroundColour,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

  void drawComboBox(juce::Graphics &, int width, int height, bool isButtonDown,
                    int buttonX, int buttonY, int buttonW, int buttonH,
                    juce::ComboBox &) override;

  void drawPopupMenuBackground(juce::Graphics &, int width,
                               int height) override;

  void drawPopupMenuItem(juce::Graphics &, const juce::Rectangle<int> &area,
                         bool isSeparator, bool isActive, bool isHighlighted,
                         bool isTicked, bool hasSubMenu,
                         const juce::String &text,
                         const juce::String &shortcutKeyText,
                         const juce::Drawable *icon,
                         const juce::Colour *textColour) override;

  void drawPopupMenuSectionHeaderWithOptions(
      juce::Graphics &, const juce::Rectangle<int> &area,
      const juce::String &sectionName,
      const juce::PopupMenu::Options &) override;

private:
  juce::Font proFont;
};

// Non-modal popup window that hides on close (instead of staying allocated)
class NonModalPopup : public juce::DialogWindow {
public:
  using juce::DialogWindow::DialogWindow;
  void closeButtonPressed() override { setVisible(false); }
};

// Custom Slider class for MIDI Learning
// Overlay displayed during MIDI Learn
class MidiLearnOverlay : public juce::Component {
public:
  MidiLearnOverlay(BreadbinProcessor &p) : processor(p) {
    setInterceptsMouseClicks(false, false); // Don't block UI
  }

  // Call from editor timerCallback every frame
  void tick() {
    bool currentlyLearning = processor.isLearning();

    // Detect transition: was learning -> no longer learning = success
    if (wasLearning && !currentlyLearning) {
      successParamName = lastLearningParamName;
      successFrames = 30; // ~1 second at 30Hz
    }

    // Track current state for next tick
    wasLearning = currentlyLearning;
    if (currentlyLearning) {
      lastLearningParamName =
          processor.getParamName(processor.getLearningParam());
    }

    // Decrement success flash counter
    if (successFrames > 0)
      --successFrames;

    // Repaint if there's anything to show
    if (currentlyLearning || successFrames > 0)
      repaint();
  }

  bool isShowingAnything() const {
    return processor.isLearning() || successFrames > 0;
  }

  void paint(juce::Graphics &g) override {
    bool learning = processor.isLearning();
    bool showingSuccess = successFrames > 0 && !learning;

    if (!learning && !showingSuccess)
      return;

    auto bounds = getLocalBounds().reduced(20);
    auto popupH = 60;
    auto popupW = 300;
    auto popupRect = juce::Rectangle<int>(bounds.getCentreX() - popupW / 2,
                                          bounds.getY() + 20, popupW, popupH);

    // Semi-transparent background
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.fillRoundedRectangle(popupRect.toFloat(), 6.0f);

    if (learning) {
      // Gold border for learning state
      g.setColour(juce::Colours::gold);
      g.drawRoundedRectangle(popupRect.toFloat(), 6.0f, 2.0f);

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
    } else {
      // Green border for success state
      float alpha = static_cast<float>(successFrames) / 30.0f;
      g.setColour(juce::Colours::limegreen.withAlpha(alpha));
      g.drawRoundedRectangle(popupRect.toFloat(), 6.0f, 2.0f);

      g.setColour(juce::Colours::limegreen.withAlpha(alpha));
      auto font = g.getCurrentFont();
      font.setHeight(16.0f);
      font.setBold(true);
      g.setFont(font);

      g.drawText("MAPPED: " + successParamName,
                 popupRect.removeFromTop(35).reduced(10, 0),
                 juce::Justification::centred);

      font.setHeight(12.0f);
      font.setBold(false);
      g.setFont(font);
      g.setColour(juce::Colours::white.withAlpha(alpha));
      g.drawText("MIDI mapping saved successfully", popupRect.reduced(10, 0),
                 juce::Justification::centred);
    }
  }

private:
  BreadbinProcessor &processor;
  bool wasLearning = false;
  juce::String lastLearningParamName;
  juce::String successParamName;
  int successFrames = 0; // countdown for success flash
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

// Custom ToggleButton class for MIDI Learning
class MappableToggle : public juce::ToggleButton {
public:
  MappableToggle(const juce::String &name, BreadbinProcessor &p,
                 BreadbinProcessor::ControlParam param)
      : juce::ToggleButton(name), processor(p), controlParam(param) {}

  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown()) {
      juce::PopupMenu m;
      m.addItem("MIDI Learn",
                [this] { processor.startLearning(controlParam); });
      m.addItem("Unlearn",
                [this] { processor.clearMIDIMappingForParam(controlParam); });
      m.showMenuAsync(juce::PopupMenu::Options{});
    } else {
      juce::ToggleButton::mouseDown(e);
    }
  }

  void paint(juce::Graphics &g) override {
    juce::ToggleButton::paint(g);
    if (processor.isLearning() &&
        processor.getLearningParam() == controlParam) {
      g.setColour(juce::Colours::gold.withAlpha(0.4f));
      g.drawRect(getLocalBounds(), 2);
    }
  }

private:
  BreadbinProcessor &processor;
  BreadbinProcessor::ControlParam controlParam;
};

// Custom ComboBox class for MIDI Learning
class MappableComboBox : public juce::ComboBox {
public:
  MappableComboBox(BreadbinProcessor &p, BreadbinProcessor::ControlParam param)
      : juce::ComboBox(), processor(p), controlParam(param) {}

  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown()) {
      juce::PopupMenu m;
      m.addItem("MIDI Learn",
                [this] { processor.startLearning(controlParam); });
      m.addItem("Unlearn",
                [this] { processor.clearMIDIMappingForParam(controlParam); });
      m.showMenuAsync(juce::PopupMenu::Options{});
    } else {
      juce::ComboBox::mouseDown(e);
    }
  }

  void paint(juce::Graphics &g) override {
    juce::ComboBox::paint(g);
    if (processor.isLearning() &&
        processor.getLearningParam() == controlParam) {
      g.setColour(juce::Colours::gold.withAlpha(0.4f));
      g.drawRect(getLocalBounds(), 2);
    }
  }

private:
  BreadbinProcessor &processor;
  BreadbinProcessor::ControlParam controlParam;
};

// Lightweight modulation indicator (thin vertical bar next to a slider)
class ModulationMeter : public juce::Component {
public:
  void setRange(float min, float max) {
    rangeMin = min;
    rangeMax = max;
  }
  void setValues(float base, float modulated) {
    baseValue = base;
    modulatedValue = modulated;
  }
  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(40, 40, 45));
    g.fillRoundedRectangle(bounds, 2.0f);
    float range = rangeMax - rangeMin;
    if (range <= 0.0f)
      return;
    float baseNorm = (baseValue - rangeMin) / range;
    float modNorm = (modulatedValue - rangeMin) / range;
    // Base position marker (thin grey line)
    float baseY = bounds.getBottom() - baseNorm * bounds.getHeight();
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine(static_cast<int>(baseY), bounds.getX(),
                         bounds.getRight());
    // Modulated fill (cyan = upward, orange = downward)
    float modY = bounds.getBottom() - modNorm * bounds.getHeight();
    float top = std::min(baseY, modY);
    float bottom = std::max(baseY, modY);
    g.setColour(modNorm > baseNorm ? juce::Colours::cyan.withAlpha(0.6f)
                                   : juce::Colours::orange.withAlpha(0.6f));
    g.fillRect(bounds.getX() + 1.0f, top, bounds.getWidth() - 2.0f,
               bottom - top);
  }

private:
  float rangeMin = 0.0f, rangeMax = 1.0f;
  float baseValue = 0.0f, modulatedValue = 0.0f;
};

// Modulation popup panel (LFO1/LFO2, Pitch Bend Range, Mod Matrix)
class ModMatrixPanel : public juce::Component, private juce::Timer {
public:
  ModMatrixPanel(BreadbinProcessor &proc);
  ~ModMatrixPanel() override {
    stopTimer();
    setLookAndFeel(nullptr);
  }
  void resized() override;
  void paint(juce::Graphics &g) override;
  void timerCallback() override;
  static constexpr int panelWidth = 520;
  static constexpr int panelHeight = 410;

private:
  BreadbinProcessor &processor;

  // ========== LFO1 ==========
  MappableToggle lfoEnableButton{"LFO", processor,
                                 BreadbinProcessor::ControlParam::LfoEnable};
  MappableComboBox lfoWaveformSelector{
      processor, BreadbinProcessor::ControlParam::LfoWave};
  std::unique_ptr<MappableSlider> lfoRateSlider;
  juce::Label lfoRateLabel;
  std::unique_ptr<MappableSlider> lfoDepthFilterSlider, lfoDepthPWSlider,
      lfoDepthPitchSlider;
  juce::Label lfoDepthFilterLabel, lfoDepthPWLabel, lfoDepthPitchLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      lfoEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      lfoWaveAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfoRateAttach, lfoDepthFiltAttach, lfoDepthPWAttach, lfoDepthPitchAttach;

  // ========== LFO2 ==========
  MappableToggle lfo2EnableButton{"LFO2", processor,
                                  BreadbinProcessor::ControlParam::Lfo2Enable};
  MappableComboBox lfo2WaveformSelector{
      processor, BreadbinProcessor::ControlParam::Lfo2Wave};
  std::unique_ptr<MappableSlider> lfo2RateSlider;
  juce::Label lfo2RateLabel;
  std::unique_ptr<MappableSlider> lfo2DepthFilterSlider, lfo2DepthPWSlider,
      lfo2DepthPitchSlider;
  juce::Label lfo2DepthFilterLabel, lfo2DepthPWLabel, lfo2DepthPitchLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      lfo2EnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      lfo2WaveAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      lfo2RateAttach, lfo2DepthFiltAttach, lfo2DepthPWAttach,
      lfo2DepthPitchAttach;

  // ========== PWM SWEEP ==========
  MappableToggle pwmSweepEnableButton{
      "PWM", processor, BreadbinProcessor::ControlParam::PwmSweepEnable};
  MappableSlider pwmSweepRateSlider{
      processor, BreadbinProcessor::ControlParam::PwmSweepRate};
  MappableSlider pwmSweepDepthSlider{
      processor, BreadbinProcessor::ControlParam::PwmSweepDepth};
  juce::Label pwmSweepRateLabel, pwmSweepDepthLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      pwmSweepEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      pwmSweepRateAttach, pwmSweepDepthAttach;

  // ========== PITCH BEND RANGE ==========
  juce::ComboBox pitchBendRangeSelector;
  juce::Label pitchBendRangeLabel;

  // ========== MOD MATRIX ==========
  struct SlotRow {
    juce::ToggleButton enableButton;
    juce::ComboBox srcBox, dstBox;
    juce::Slider amtSlider;
    juce::Label slotLabel;
    juce::Label sourceValueLabel;  // current source value
    juce::Label contributionLabel; // effective contribution
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        enableAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        srcAttach, dstAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        amtAttach;
  };
  std::array<SlotRow, BreadbinProcessor::kModSlots> slots;
  juce::Label totalFilterLabel, totalPWLabel, totalPitchLabel, totalResLabel;
};

// Chord Memory popup panel
class ChordMemoryPanel : public juce::Component, private juce::Timer {
public:
  ChordMemoryPanel(BreadbinProcessor &proc);
  ~ChordMemoryPanel() override {
    stopTimer();
    setLookAndFeel(nullptr);
  }
  void resized() override;
  void paint(juce::Graphics &g) override;
  void timerCallback() override;
  static constexpr int panelWidth = 520;
  static constexpr int panelHeight = 340;

private:
  BreadbinProcessor &processor;
  juce::ToggleButton enableButton{"Enable"};
  std::array<juce::TextButton, 4> slotButtons;
  std::array<juce::TextButton, 4> learnButtons;
  struct SlotRow {
    juce::Label label;
    std::array<juce::Slider, 5> sliders;
    std::array<
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
        5>
        attachments;
  };
  std::array<SlotRow, 4> slots;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      enableAttach;
  void applyLearnedChord(int slot, const std::vector<int> &notes);
};

// Wavetable step editor popup
class WavetablePanel : public juce::Component, private juce::Timer {
public:
  WavetablePanel(BreadbinProcessor &proc);
  ~WavetablePanel() override {
    stopTimer();
    setLookAndFeel(nullptr);
  }
  void resized() override;
  void paint(juce::Graphics &g) override;
  void timerCallback() override;
  static constexpr int panelWidth = 820;
  static constexpr int panelHeight = 380;

private:
  BreadbinProcessor &processor;

  // Global controls (moved from main editor)
  juce::ToggleButton enableButton{"Enable"};
  juce::Slider numStepsSlider, rateSlider;
  juce::ToggleButton loopButton{"Loop"};
  juce::Label stepsLabel, rateLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      enableAttach, loopAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      stepsAttach, rateAttach;

  // Per-step controls (16 steps)
  struct StepColumn {
    juce::ComboBox waveBox;
    juce::Slider pitchSlider;
    juce::Slider pwSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        waveAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        pitchAttach, pwAttach;
  };
  std::array<StepColumn, 16> steps;
  juce::TextButton shiftLeftButton{"Shift <-"};
  juce::TextButton shiftRightButton{"Shift ->"};
  juce::TextButton randomizeButton{"Randomize"};
  juce::TextButton clearButton{"Clear"};

  int lastHighlightedStep = -1;
  void shiftActiveSteps(bool right);
  void randomizeActiveSteps();
  void clearActiveSteps();
};

// SID file player popup panel
class SidPlayerPanel : public juce::Component, private juce::Timer {
public:
  SidPlayerPanel(BreadbinProcessor &proc);
  ~SidPlayerPanel() override {
    stopTimer();
    setLookAndFeel(nullptr);
  }
  void resized() override;
  void paint(juce::Graphics &g) override;
  void timerCallback() override;
  static constexpr int panelWidth = 520;
  static constexpr int panelHeight = 370;

private:
  BreadbinProcessor &processor;

  juce::TextButton loadButton{"Load SID"};
  juce::TextButton playButton{"Play"};
  juce::TextButton stopButton{"Stop"};
  juce::TextButton pauseButton{"Pause"};
  juce::TextButton snapshotButton{"Snapshot to Synth"};

  juce::Label titleLabel, authorLabel, releasedLabel;
  juce::Label tuneInfoLabel;

  juce::ComboBox subtuneSelector;
  juce::Label subtuneLabel;

  juce::Slider volumeSlider;
  juce::Label volumeLabel;

  juce::TextEditor registerDisplay;

  std::unique_ptr<juce::FileChooser> fileChooser;

  void updateRegisterDisplay();
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
  MappableComboBox dualModeSelector{processor,
                                    BreadbinProcessor::ControlParam::DualMode};
  juce::Label modeLabel;
  juce::ComboBox globalPresetSelector; // Factory + user presets
  juce::Label globalPresetLabel;
  std::vector<juce::File> userPresetFiles; // Tracked user presets (ID 1000+)
  juce::ComboBox presetSelector;           // Voice presets
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
  MappableSlider leftPanSlider{processor,
                               BreadbinProcessor::ControlParam::LeftPan};
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
  MappableSlider rightPanSlider{processor,
                                BreadbinProcessor::ControlParam::RightPan};
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
  MappableToggle ringModButton{"Ring", processor,
                               BreadbinProcessor::ControlParam::VoiceRingMod};
  MappableToggle syncButton{"Sync", processor,
                            BreadbinProcessor::ControlParam::VoiceSync};
  MappableToggle voiceFilterButton{
      "Flt", processor, BreadbinProcessor::ControlParam::VoiceFilterEnable};

  // ========== ARPEGGIATOR ==========
  MappableToggle arpEnableButton{"Arp", processor,
                                 BreadbinProcessor::ControlParam::ArpEnable};
  MappableComboBox arpPatternSelector{
      processor, BreadbinProcessor::ControlParam::ArpPattern};
  MappableSlider arpRateSlider{processor,
                               BreadbinProcessor::ControlParam::ArpRate};
  MappableComboBox arpOctaveSelector{
      processor, BreadbinProcessor::ControlParam::ArpOctaves};
  juce::Label arpRateLabel, arpPatternLabel, arpOctaveLabel;

  // ========== GLIDE/PORTAMENTO ==========
  MappableSlider glideTimeSlider{processor,
                                 BreadbinProcessor::ControlParam::GlobalGlide};
  juce::Label glideTimeLabel;

  // ========== EXTERNAL AUDIO INPUT ==========
  MappableToggle extInputEnableButton{
      "Ext In", processor, BreadbinProcessor::ControlParam::ExtInputEnable};
  MappableSlider extInputLevelSlider{
      processor, BreadbinProcessor::ControlParam::ExtInputLevel};
  juce::Label extInputLabel;

  // ========== CLOCK MODE (PAL/NTSC) ==========
  MappableComboBox clockModeSelector{
      processor, BreadbinProcessor::ControlParam::ClockMode};
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
  MappableToggle chorusEnableButton{
      "Chorus", processor, BreadbinProcessor::ControlParam::ChorusEnable};
  MappableSlider chorusRateSlider{processor,
                                  BreadbinProcessor::ControlParam::ChorusRate};
  MappableSlider chorusDepthSlider{
      processor, BreadbinProcessor::ControlParam::ChorusDepth};
  MappableSlider chorusMixSlider{processor,
                                 BreadbinProcessor::ControlParam::ChorusMix};
  juce::Label chorusRateLabel, chorusDepthLabel, chorusMixLabel;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      chorusEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      chorusRateAttach, chorusDepthAttach, chorusMixAttach;

  // ========== FX: DELAY ==========
  MappableToggle delayEnableButton{
      "Delay", processor, BreadbinProcessor::ControlParam::DelayEnable};
  MappableSlider delayTimeLSlider{processor,
                                  BreadbinProcessor::ControlParam::DelayTimeL};
  MappableSlider delayTimeRSlider{processor,
                                  BreadbinProcessor::ControlParam::DelayTimeR};
  MappableSlider delayFeedbackSlider{
      processor, BreadbinProcessor::ControlParam::DelayFeedback};
  MappableSlider delayMixSlider{processor,
                                BreadbinProcessor::ControlParam::DelayMix};
  juce::Label delayTimeLLabel, delayTimeRLabel, delayFBLabel, delayMixLabel;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      delayEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      delayTimeLAttach, delayTimeRAttach, delayFBAttach, delayMixAttach;

  // ========== WAVETABLE STEP SEQUENCER ==========
  juce::TextButton wavetableButton{"Wavetable"};
  juce::Component::SafePointer<juce::DialogWindow> wavetableWindow;
  void showWavetablePopup();

  // ========== INLINE ENABLE TOGGLES (above popup buttons) ==========
  MappableToggle wtEnableToggle{"WT", processor,
                                BreadbinProcessor::ControlParam::WtEnable};
  MappableToggle lfo1EnableToggle{"LFO1", processor,
                                  BreadbinProcessor::ControlParam::LfoEnable};
  MappableToggle lfo2EnableToggle{"LFO2", processor,
                                  BreadbinProcessor::ControlParam::Lfo2Enable};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      wtEnableToggleAttach, lfo1EnableToggleAttach, lfo2EnableToggleAttach;

  // ========== FILTER ENVELOPE ==========
  MappableToggle filterEnvEnableButton{
      "Filt Env", processor, BreadbinProcessor::ControlParam::FilterEnvEnable};
  MappableSlider filterEnvAttackSlider{
      processor, BreadbinProcessor::ControlParam::FilterEnvAttack};
  MappableSlider filterEnvDecaySlider{
      processor, BreadbinProcessor::ControlParam::FilterEnvDecay};
  MappableSlider filterEnvSustainSlider{
      processor, BreadbinProcessor::ControlParam::FilterEnvSustain};
  MappableSlider filterEnvReleaseSlider{
      processor, BreadbinProcessor::ControlParam::FilterEnvRelease};
  MappableSlider filterEnvAmountSlider{
      processor, BreadbinProcessor::ControlParam::FilterEnvAmount};
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

  // ========== MODULATION ==========
  juce::TextButton modMatrixButton{"Modulation"};
  juce::Component::SafePointer<juce::DialogWindow> modMatrixWindow;
  void showModMatrixPopup();

  // ========== CHORD MEMORY ==========
  juce::TextButton chordMemoryButton{"Chord"};
  juce::Component::SafePointer<juce::DialogWindow> chordMemoryWindow;
  void showChordMemoryPopup();

  // ========== SID FILE PLAYER ==========
  juce::TextButton sidPlayerButton{"SID Player"};
  juce::Component::SafePointer<juce::DialogWindow> sidPlayerWindow;
  void showSidPlayerPopup();

  // ========== MODULATION METERS ==========
  ModulationMeter cutoffMeterL, cutoffMeterR;
  ModulationMeter pwMeter;
  ModulationMeter resMeterL, resMeterR;
  ModulationMeter pitchMeter;

  // ========== SID PLAYER REGISTER OVERLAY ==========
  juce::Label sidOverlayWave, sidOverlayPW;
  juce::Label sidOverlayAttack, sidOverlayDecay, sidOverlaySustain,
      sidOverlayRelease;
  juce::Label sidOverlayCutoff, sidOverlayRes;

  // ========== PRESET DIRTY INDICATOR ==========
  juce::Label presetDirtyLabel;

  // Virtual keyboard
  juce::MidiKeyboardState keyboardState;
  juce::MidiKeyboardComponent keyboard;

  MidiLearnOverlay midiLearnOverlay{processor};

  // Background image
  juce::Image backgroundImage;
  juce::TooltipWindow tooltipWindow{this, 500}; // 500ms delay before showing

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
  void savePresetToFile();               // Overall Patch
  void loadPresetFromFile();             // Overall Patch
  void saveVoicePresetToFile();          // Selected Voice
  void loadVoicePresetFromFile();        // Selected Voice
  void savePresetToMenu();               // Save into preset dropdown
  void refreshUserPresets();             // Scan user preset directory
  static juce::File getUserPresetsDir(); // %APPDATA%/GPLAudio/Breadbin/Presets

  // MidiKeyboardState::Listener
  void handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                    int midiNoteNumber, float velocity) override;
  void handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                     int midiNoteNumber, float velocity) override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreadbinEditor)
};
