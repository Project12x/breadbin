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
  void setFonts(const juce::Font &pro, const juce::Font &bold,
                const juce::Font &mono) {
    proFont = pro; boldFont = bold; monoFont = mono;
  }
  const juce::Font &getProFont()  const { return proFont; }
  const juce::Font &getBoldFont() const { return boldFont; }
  const juce::Font &getMonoFont() const { return monoFont; }

  juce::Font getComboBoxFont(juce::ComboBox &) override {
    return proFont.withHeight(14.0f);
  }
  juce::Font getPopupMenuFont() override { return proFont.withHeight(14.0f); }
  juce::Font getSliderPopupFont(juce::Slider &) override {
    return proFont.withHeight(12.0f);
  }

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

  void drawButtonText(juce::Graphics &, juce::TextButton &,
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

  void drawDocumentWindowTitleBar(juce::DocumentWindow &window,
                                  juce::Graphics &g, int w, int h,
                                  int titleSpaceX, int titleSpaceW,
                                  const juce::Image *icon,
                                  bool drawTitleTextOnLeft) override;

  juce::Label *createSliderTextBox(juce::Slider &slider) override {
    auto *l = juce::LookAndFeel_V4::createSliderTextBox(slider);
    l->setFont(proFont.withHeight(11.0f));
    return l;
  }

private:
  juce::Font proFont, boldFont, monoFont;
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

  void refreshFonts(const juce::Font &pro, const juce::Font &bold) {
    panelProFont = pro; panelBoldFont = bold;
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
      g.setFont(panelBoldFont.withHeight(16.0f));

      juce::String paramName =
          processor.getParamName(processor.getLearningParam());
      g.drawText("LEARNING: " + paramName,
                 popupRect.removeFromTop(35).reduced(10, 0),
                 juce::Justification::centred);

      g.setFont(panelProFont.withHeight(12.0f));
      g.drawText("Move any MIDI hardware control to map...",
                 popupRect.reduced(10, 0), juce::Justification::centred);
    } else {
      // Green border for success state
      float alpha = static_cast<float>(successFrames) / 30.0f;
      g.setColour(juce::Colours::limegreen.withAlpha(alpha));
      g.drawRoundedRectangle(popupRect.toFloat(), 6.0f, 2.0f);

      g.setColour(juce::Colours::limegreen.withAlpha(alpha));
      g.setFont(panelBoldFont.withHeight(16.0f));

      g.drawText("MAPPED: " + successParamName,
                 popupRect.removeFromTop(35).reduced(10, 0),
                 juce::Justification::centred);

      g.setFont(panelProFont.withHeight(12.0f));
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
  int successFrames = 0;
  juce::Font panelProFont, panelBoldFont;
};

class MappableSlider : public juce::Slider {
public:
  MappableSlider(BreadbinProcessor &p, BreadbinProcessor::ControlParam param)
      : juce::Slider(), processor(p), controlParam(param) {}

  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown()) {
      juce::PopupMenu m;
      m.addItem("Set to Default", [this] {
        setValue(getDoubleClickReturnValue(), juce::sendNotification);
      });
      m.addSeparator();
      m.addItem("Copy Value", [this] {
        s_clipboard     = static_cast<float>(getValue());
        s_hasClipboard  = true;
      });
      {
        juce::PopupMenu::Item pasteItem;
        pasteItem.text      = "Paste Value";
        pasteItem.isEnabled = s_hasClipboard;
        pasteItem.action    = [this] {
          setValue(static_cast<double>(s_clipboard), juce::sendNotification);
        };
        m.addItem(pasteItem);
      }
      m.addSeparator();
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

      if (auto *laf = dynamic_cast<BreadbinLookAndFeel *>(&getLookAndFeel()))
        g.setFont(laf->getBoldFont().withHeight(10.0f));
      else
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
      g.drawText("LEARN", getLocalBounds(), juce::Justification::centred);
    }
  }

private:
  static inline float s_clipboard    = 0.0f;
  static inline bool  s_hasClipboard = false;
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
  bool setValues(float base, float modulated) {
    if (base == baseValue && modulated == modulatedValue)
      return false;
    baseValue = base;
    modulatedValue = modulated;
    return true;
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

// Waveform shape preview for LFO rows in the Modulation popup
class LFODisplay : public juce::Component {
public:
  void setWaveType(int type) {
    if (waveType != type) { waveType = type; repaint(); }
  }
  void setPhase(float p) {
    phase = p;
    repaint();
  }
  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(20, 20, 25));
    g.fillRoundedRectangle(b, 3.0f);
    g.setColour(juce::Colour(50, 50, 60));
    g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 0.5f);

    const float padX = 4.0f;
    float w   = b.getWidth() - padX * 2.0f;
    float cy  = b.getCentreY();
    float amp = (b.getHeight() - 6.0f) * 0.5f;
    float x0  = b.getX() + padX;
    auto yFor = [&](float n) { return cy - n * amp; };

    g.setColour(juce::Colour(55, 55, 68));
    g.drawHorizontalLine(static_cast<int>(cy), x0, x0 + w);

    // Waveform scrolls left as LFO phase advances — shows live motion
    float p = juce::jlimit(0.0f, 1.0f, phase);

    // Pre-generate S&H step values (fixed seed = deterministic, repeating pattern)
    float shVals[16];
    { juce::Random rng(42); for (auto &v : shVals) v = rng.nextFloat() * 2.0f - 1.0f; }

    auto waveAt = [&](float t) -> float {
      t -= std::floor(t); // wrap to [0, 1)
      if (waveType == 1) { // Triangle
        if (t < 0.25f) return t * 4.0f;
        if (t < 0.75f) return 1.0f - (t - 0.25f) * 4.0f;
        return -1.0f + (t - 0.75f) * 4.0f;
      } else if (waveType == 2) { // Sawtooth
        return t < 0.97f ? (-1.0f + (t / 0.97f) * 2.0f) : -1.0f;
      } else if (waveType == 3) { // Square
        return t < 0.5f ? 1.0f : -1.0f;
      } else if (waveType == 4) { // S&H: 8 fixed steps per cycle
        return shVals[static_cast<int>(t * 8) % 16];
      } else { // Sine (waveType == 5)
        return std::sin(t * juce::MathConstants<float>::twoPi);
      }
    };

    juce::Path path;
    const int numPts = static_cast<int>(w) + 1;
    for (int i = 0; i < numPts; ++i) {
      float t = static_cast<float>(i) / static_cast<float>(numPts - 1);
      float x = x0 + t * w;
      float y = yFor(waveAt(t + p));
      if (i == 0) path.startNewSubPath(x, y);
      else        path.lineTo(x, y);
    }
    g.setColour(juce::Colours::cyan.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(2.5f));
    g.setColour(juce::Colours::cyan);
    g.strokePath(path, juce::PathStrokeType(1.0f));
  }
private:
  int waveType = 1;
  float phase = 0.0f;
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
  void refreshFonts(const juce::Font &pro, const juce::Font &bold,
                    const juce::Font &mono);
  static constexpr int panelWidth = 520;
  static constexpr int panelHeight = 410;

private:
  BreadbinProcessor &processor;
  juce::Font panelProFont, panelBoldFont, panelMonoFont;

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
  juce::TextButton lfoSyncModeBtn;
  juce::ComboBox   lfoSyncDivCombo;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   lfoSyncAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoSyncDivAttach;

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
  juce::TextButton lfo2SyncModeBtn;
  juce::ComboBox   lfo2SyncDivCombo;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   lfo2SyncAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfo2SyncDivAttach;

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
  LFODisplay lfoDisplay1, lfoDisplay2;
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
  void refreshFonts(const juce::Font &pro, const juce::Font &bold);
  static constexpr int panelWidth = 520;
  static constexpr int panelHeight = 340;

private:
  BreadbinProcessor &processor;
  juce::Font panelProFont, panelBoldFont;
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
  juce::ComboBox presetSelector;
  juce::TextButton presetPrevButton, presetNextButton;
  void applyChordPresetByIndex(int presetIndex);
  juce::TextButton saveButton{"Save"}, loadButton{"Load"};
  void saveChordPreset();
  void loadChordPreset();
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
  void refreshFonts(const juce::Font &pro, const juce::Font &bold);
  static constexpr int panelWidth = 820;
  static constexpr int panelHeight = 380;

private:
  BreadbinProcessor &processor;
  juce::Font panelProFont, panelBoldFont;

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

  juce::ComboBox presetSelector;
  juce::TextButton presetPrevButton, presetNextButton;
  void applyWavetablePresetByIndex(int presetIndex);
  juce::TextButton saveButton{"Save"}, loadButton{"Load"};
  void saveWavetablePreset();
  void loadWavetablePreset();
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
  void refreshFonts(const juce::Font &mono);
  static constexpr int panelWidth = 520;
  static constexpr int panelHeight = 370;

private:
  BreadbinProcessor &processor;
  juce::Font panelMonoFont;

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

// 4-bit digi sample player popup panel
class DigiSamplerPanel : public juce::Component {
public:
  DigiSamplerPanel(BreadbinProcessor &proc);
  ~DigiSamplerPanel() override { setLookAndFeel(nullptr); }
  void resized() override;
  void paint(juce::Graphics &g) override;
  void refreshFonts(const juce::Font &pro, const juce::Font &bold,
                    const juce::Font &mono);
  static constexpr int panelWidth = 400;
  static constexpr int panelHeight = 250;

private:
  BreadbinProcessor &processor;
  juce::Font panelProFont, panelBoldFont, panelMonoFont;

  juce::TextButton loadButton{"Load WAV"};
  juce::Label fileNameLabel;
  juce::Label sampleInfoLabel;

  juce::ComboBox rootNoteSelector;
  juce::Label rootNoteLabel;

  juce::ComboBox bitDepthSelector;
  juce::Label bitDepthLabel;

  juce::ToggleButton loopButton{"Loop"};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      loopAttach;

  std::unique_ptr<juce::FileChooser> fileChooser;
  void updateInfoLabels();
};

// Interactive filter frequency response display for SID filter sections
class FilterDisplay : public juce::Component {
public:
  FilterDisplay(MappableSlider &cutoff, MappableSlider &res)
      : cutoffSlider(cutoff), resSlider(res) {}

  void setFont(const juce::Font &f) { displayFont = f; }
  void setMonoFont(const juce::Font &f) { monoFont = f; }

  void setCutoff(int val) {
    if (currentCutoff != val) { currentCutoff = val; repaint(); }
  }
  void setResonance(int val) {
    if (currentResonance != val) { currentResonance = val; repaint(); }
  }
  void setModes(bool lp, bool bp, bool hp) {
    if (lpOn != lp || bpOn != bp || hpOn != hp) {
      lpOn = lp; bpOn = bp; hpOn = hp; repaint();
    }
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(18, 18, 22));
    g.fillRoundedRectangle(b, 3.0f);
    g.setColour(juce::Colour(50, 50, 62));
    g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 0.5f);

    if (!lpOn && !bpOn && !hpOn) {
      g.setColour(juce::Colours::grey.withAlpha(0.4f));
      g.setFont(displayFont.withHeight(9.0f));
      g.drawText("filter off", b, juce::Justification::centred);
      return;
    }

    const float px = 4.0f, py = 4.0f;
    const float pw = b.getWidth() - px * 2.0f;
    const float ph = b.getHeight() - py * 2.0f;
    const float kMinHz = 30.0f, kMaxHz = 12000.0f;
    const float logRange = std::log(kMaxHz / kMinHz);

    float fcHz = kMinHz * std::pow(kMaxHz / kMinHz,
                                   static_cast<float>(currentCutoff) / 2047.0f);
    float Q = 0.5f + (static_cast<float>(currentResonance) / 15.0f) * 9.5f;

    // 0 dB reference line  (maps to norm = 30/42 in [-30..+12] range)
    float y0db = b.getY() + py + ph * (1.0f - 30.0f / 42.0f);
    g.setColour(juce::Colour(55, 55, 68));
    g.drawHorizontalLine(static_cast<int>(y0db), b.getX() + px,
                         b.getX() + px + pw);

    // Build response curve pixel-by-pixel
    juce::Path curve;
    const int N = static_cast<int>(pw);
    for (int i = 0; i < N; ++i) {
      float t   = static_cast<float>(i) / static_cast<float>(juce::jmax(1, N - 1));
      float fHz = kMinHz * std::exp(t * logRange);
      float xr  = fHz / fcHz;
      float xr2 = xr * xr;
      float d2  = (1.0f - xr2) * (1.0f - xr2) + (xr / Q) * (xr / Q);
      float d   = std::sqrt(juce::jmax(d2, 1e-12f));
      float mag = 0.0f;
      if (lpOn) mag += 1.0f / d;
      if (bpOn) mag += (xr / Q) / d;
      if (hpOn) mag += xr2 / d;
      mag       = juce::jlimit(0.001f, 20.0f, mag);
      float db  = 20.0f * std::log10(mag);
      float norm = (juce::jlimit(-30.0f, 12.0f, db) + 30.0f) / 42.0f;
      float cx  = b.getX() + px + static_cast<float>(i);
      float cy  = b.getY() + py + ph * (1.0f - norm);
      if (i == 0) curve.startNewSubPath(cx, cy);
      else        curve.lineTo(cx, cy);
    }
    g.setColour(juce::Colours::cyan.withAlpha(0.18f));
    g.strokePath(curve, juce::PathStrokeType(3.0f));
    g.setColour(juce::Colours::cyan.withAlpha(0.85f));
    g.strokePath(curve, juce::PathStrokeType(1.2f));

    // Cutoff frequency vertical marker
    float cxMark = b.getX() + px + pw * std::log(fcHz / kMinHz) / logRange;
    if (cxMark > b.getX() + px && cxMark < b.getX() + px + pw) {
      g.setColour(juce::Colours::cyan.withAlpha(0.35f));
      g.drawVerticalLine(static_cast<int>(cxMark), b.getY() + py,
                         b.getY() + py + ph);
    }
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    g.setFont(displayFont.withHeight(7.5f));
    g.drawText("drag", b.reduced(3).removeFromBottom(9.0f),
               juce::Justification::centredRight);

    // Frequency axis labels
    g.setFont(monoFont.withHeight(8.0f));
    g.setColour(juce::Colour(130, 130, 145).withAlpha(0.4f));
    for (auto [freq, label] : std::initializer_list<std::pair<float, const char*>>{
            {100.f, "100"}, {1000.f, "1k"}, {10000.f, "10k"}}) {
      float normX = std::log(freq / kMinHz) / logRange;
      int labelX = static_cast<int>(b.getX() + px + normX * pw);
      g.drawText(label, labelX - 10, static_cast<int>(b.getBottom()) - 10,
                 20, 10, juce::Justification::centred);
    }
  }

  void mouseDown(const juce::MouseEvent &e) override {
    dragStart         = e.getPosition();
    dragStartCutoff   = currentCutoff;
    dragStartRes      = currentResonance;
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
  }
  void mouseDrag(const juce::MouseEvent &e) override {
    int newCutoff = juce::jlimit(0, 2047,
        dragStartCutoff + (e.x - dragStart.x) * 2047 / juce::jmax(1, getWidth()));
    int newRes = juce::jlimit(0, 15,
        dragStartRes - (e.y - dragStart.y) * 15 / juce::jmax(1, getHeight()));
    cutoffSlider.setValue(static_cast<double>(newCutoff), juce::sendNotification);
    resSlider.setValue(static_cast<double>(newRes), juce::sendNotification);
  }
  void mouseUp(const juce::MouseEvent &) override {
    setMouseCursor(juce::MouseCursor::NormalCursor);
  }

private:
  MappableSlider &cutoffSlider;
  MappableSlider &resSlider;
  int  currentCutoff    = 1024;
  int  currentResonance = 0;
  bool lpOn = true, bpOn = false, hpOn = false;
  juce::Point<int> dragStart;
  int  dragStartCutoff  = 0;
  int  dragStartRes     = 0;
  juce::Font displayFont;
  juce::Font monoFont;
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

  // timerCallback subsections
  void updateModulationMeters();
  void updateVoiceCountDisplay();
  void updateFxBypassVisuals();
  void updateSidPlayerOverlay();

private:
  BreadbinProcessor &processor;

  // Retro font for section headers (Press Start 2P)
  juce::Font retroFont;
  // Professional font for UI controls (Lato Regular)
  juce::Font proFont;
  // Bold font for panel titles (Lato Bold)
  juce::Font boldFont;
  // Monospaced font for numeric displays (JetBrains Mono)
  juce::Font monoFont;
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
  juce::Label cpuLoadLabel;
  juce::TextButton presetPrevButton, presetNextButton;
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

  // Voice mode controls
  juce::ComboBox voiceModeSelector;
  juce::ComboBox polyMaxNotesSelector;
  juce::Label polyVoiceCountLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      voiceModeAttachment;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      polyMaxNotesAttachment;

  // Paraphonic stacking controls
  MappableSlider paraSpreadSlider{processor,
                                  BreadbinProcessor::ControlParam::ParaSpread};
  juce::Label paraSpreadLabel{"ParaSpread", "SPREAD"};
  MappableToggle paraRetrigButton{"Retrig", processor,
                                  BreadbinProcessor::ControlParam::ParaFilterRetrig};
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      paraSpreadAttachment;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      paraRetrigAttachment;

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

  // Filter response displays (interactive, one per SID)
  FilterDisplay filterDisplay_L{leftCutoffSlider, leftResonanceSlider};
  FilterDisplay filterDisplay_R{rightCutoffSlider, rightResonanceSlider};

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
  juce::Slider modOffsetSlider;
  juce::Label modOffsetLabel{"", "MOD"};

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
  juce::Label noiseGateLabel{"", "GATE"};
  juce::Slider noiseGateSlider;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      noiseGateAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      dualModeAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      chipLeftAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
      chipRightAttach;
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
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      voiceModOffsetAttach;

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

  // ========== FX: REVERB ==========
  MappableToggle reverbEnableButton{
      "Reverb", processor, BreadbinProcessor::ControlParam::ReverbEnable};
  MappableSlider reverbDecaySlider{
      processor, BreadbinProcessor::ControlParam::ReverbDecay};
  MappableSlider reverbDampingSlider{
      processor, BreadbinProcessor::ControlParam::ReverbDamping};
  MappableSlider reverbMixSlider{processor,
                                 BreadbinProcessor::ControlParam::ReverbMix};
  juce::Label reverbDecayLabel, reverbDampingLabel, reverbMixLabel;

  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      reverbEnableAttach;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
      reverbDecayAttach, reverbDampingAttach, reverbMixAttach;

  // ========== WAVETABLE STEP SEQUENCER ==========
  juce::TextButton wavetableButton{"Wavetable"};
  juce::Component::SafePointer<juce::DialogWindow> wavetableWindow;
  void showWavetablePopup();

  // ========== DIGI SAMPLER ==========
  juce::TextButton digiButton{"Digi"};
  juce::Component::SafePointer<juce::DialogWindow> digiWindow;
  MappableToggle digiEnableToggle{
      "Digi", processor, BreadbinProcessor::ControlParam::DigiEnable};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
      digiEnableAttach;
  void showDigiPopup();

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
