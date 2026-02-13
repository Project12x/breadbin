#include "PluginEditor.h"
#include "BinaryData.h"

BreadbinEditor::BreadbinEditor(BreadbinProcessor &p)
    : juce::AudioProcessorEditor(&p), processor(p),
      keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {

  backgroundImage = juce::ImageFileFormat::loadFrom(
      BinaryData::background_jpg, BinaryData::background_jpgSize);

  // Load retro font (Press Start 2P) from binary assets
  auto retroTypeface = juce::Typeface::createSystemTypefaceFor(
      BinaryData::PressStart2PRegular_ttf,
      BinaryData::PressStart2PRegular_ttfSize);
  retroFont = juce::Font(juce::FontOptions(retroTypeface).withHeight(12.0f));

  // Load professional font (Roboto Bold) from binary assets
  auto proTypeface = juce::Typeface::createSystemTypefaceFor(
      BinaryData::RobotoBold_ttf, BinaryData::RobotoBold_ttfSize);
  proFont = juce::Font(juce::FontOptions(proTypeface).withHeight(14.0f));

  // Setup custom look and feel with Roboto Bold font for ComboBoxes
  customLookAndFeel.setProFont(proFont);
  setLookAndFeel(&customLookAndFeel);

  keyboardState.addListener(this);
  processor.getMidiMessageCollector().reset(p.getSampleRate());

  setupControls();
  setupLeftSID();
  setupRightSID();
  setupVoiceEditor();

  // On first-ever launch, apply Dual Lead. On restart, sync from saved APVTS state.
  if (!processor.wasStateRestored()) {
    applyGlobalPreset(1);
  } else {
    for (int v = 0; v < 6; ++v)
      processor.applyVoiceSettings(v);
  }
  selectVoice(processor.getSelectedVoice());
  setSize(1000, 800);
  setResizeLimits(900, 750, 1200, 1000);
  addAndMakeVisible(midiLearnOverlay);
  midiLearnOverlay.setAlwaysOnTop(true);

  startTimerHz(30);

  // Initialize Global Attachments
  masterVolAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "masterVol", masterVolSlider);
  dualModeAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "dualMode", dualModeSelector);
  chipLeftAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "chipLeft", leftChipSelector);
  chipRightAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "chipRight", rightChipSelector);
  agingAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "aging", agingSlider);
  leftDetuneAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "leftDetune", leftDetuneSlider);
  rightDetuneAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "rightDetune", rightDetuneSlider);
  leftPanAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "leftPan", leftPanSlider);
  rightPanAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "rightPan", rightPanSlider);
  glideAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "glide", glideTimeSlider);
  clockModeAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "clockMode", clockModeSelector);
  extInputEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "extInputEnable", extInputEnableButton);
  extInputLevelAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "extInputLevel", extInputLevelSlider);

  // FX: Chorus
  chorusEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "chorusEnable", chorusEnableButton);
  chorusRateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "chorusRate", chorusRateSlider);
  chorusDepthAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "chorusDepth", chorusDepthSlider);
  chorusMixAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "chorusMix", chorusMixSlider);

  // FX: Delay
  delayEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "delayEnable", delayEnableButton);
  delayTimeLAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "delayTimeL", delayTimeLSlider);
  delayTimeRAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "delayTimeR", delayTimeRSlider);
  delayFBAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "delayFeedback", delayFeedbackSlider);
  delayMixAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "delayMix", delayMixSlider);

  // Wavetable attachments are now in WavetablePanel popup

  // Filter Envelope
  filterEnvEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "filterEnvEnable", filterEnvEnableButton);
  filterEnvAttackAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvAttack", filterEnvAttackSlider);
  filterEnvDecayAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvDecay", filterEnvDecaySlider);
  filterEnvSustainAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvSustain", filterEnvSustainSlider);
  filterEnvReleaseAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvRelease", filterEnvReleaseSlider);
  filterEnvAmountAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvAmount", filterEnvAmountSlider);

  arpEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "arpEnable", arpEnableButton);
  arpPatternAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "arpPattern", arpPatternSelector);
  arpRateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "arpRate", arpRateSlider);
  arpOctaveAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "arpOctaves", arpOctaveSelector);

  refreshVoiceEditorAttachments();

  // Snapshot initial preset state for dirty detection
  processor.snapshotPresetState();
}

BreadbinEditor::~BreadbinEditor() {
  stopTimer();
  setLookAndFeel(nullptr); // Must reset before customLookAndFeel is destroyed
  keyboardState.removeListener(this);
}

void BreadbinEditor::handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                                  int midiNoteNumber, float velocity) {
  auto msg = juce::MidiMessage::noteOn(midiChannel, midiNoteNumber, velocity);
  processor.getMidiMessageCollector().addMessageToQueue(msg);
}

void BreadbinEditor::handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                                   int midiNoteNumber, float velocity) {
  auto msg = juce::MidiMessage::noteOff(midiChannel, midiNoteNumber, velocity);
  processor.getMidiMessageCollector().addMessageToQueue(msg);
}

void BreadbinEditor::timerCallback() {
  if (processor.isLearning()) {
    midiLearnOverlay.repaint();
    repaint();
  }

  // Update modulation meters
  cutoffMeterL.setValues(static_cast<float>(processor.getBaseFilterCutoff(true)),
                         static_cast<float>(processor.getLastAppliedCutoffLeft()));
  cutoffMeterL.repaint();

  cutoffMeterR.setValues(static_cast<float>(processor.getBaseFilterCutoff(false)),
                         static_cast<float>(processor.getLastAppliedCutoffRight()));
  cutoffMeterR.repaint();

  pwMeter.setValues(static_cast<float>(processor.getVoiceSettings(selectedVoice).pulseWidth),
                    static_cast<float>(processor.getLastAppliedPW()));
  pwMeter.repaint();

  pitchMeter.setValues(0.0f, processor.getLastAppliedPitchOffset());
  pitchMeter.repaint();

  resMeterL.setValues(static_cast<float>(processor.getBaseFilterResonance(true)),
                      static_cast<float>(processor.getLastAppliedResLeft()));
  resMeterL.repaint();

  resMeterR.setValues(static_cast<float>(processor.getBaseFilterResonance(false)),
                      static_cast<float>(processor.getLastAppliedResRight()));
  resMeterR.repaint();

  // Preset dirty indicator
  presetDirtyLabel.setText(processor.isPresetDirty() ? "*" : "",
                           juce::dontSendNotification);
}

// ========== ModMatrixPanel Implementation ==========

ModMatrixPanel::ModMatrixPanel(BreadbinProcessor &proc) : processor(proc) {
  // ========== LFO1 SETUP ==========
  lfoEnableButton.setTooltip("LFO 1: Low-frequency oscillator for modulation");
  lfoEnableButton.setColour(juce::ToggleButton::tickColourId,
                            juce::Colours::cyan);
  addAndMakeVisible(lfoEnableButton);

  lfoWaveformSelector.addItem("Tri", 1);
  lfoWaveformSelector.addItem("Saw", 2);
  lfoWaveformSelector.addItem("Sq", 3);
  lfoWaveformSelector.addItem("S&H", 4);
  lfoWaveformSelector.setTooltip("LFO1 waveform shape");
  addAndMakeVisible(lfoWaveformSelector);

  auto setupLfoSlider = [this](std::unique_ptr<MappableSlider> &slider,
                               juce::Label &label, const juce::String &name,
                               float minVal, float maxVal, float defaultVal,
                               juce::Slider::SliderStyle style,
                               juce::Slider::TextEntryBoxPosition textPos) {
    slider = std::make_unique<MappableSlider>(
        processor, BreadbinProcessor::ControlParam::None);
    slider->setRange(minVal, maxVal, 0.01);
    slider->setValue(defaultVal);
    slider->setSliderStyle(style);
    slider->setTextBoxStyle(textPos, false, 40, 14);
    slider->setColour(juce::Slider::textBoxTextColourId, juce::Colours::cyan);
    slider->setColour(juce::Slider::textBoxOutlineColourId,
                      juce::Colours::transparentBlack);
    addAndMakeVisible(*slider);
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(juce::Font(juce::FontOptions(9.0f)));
    addAndMakeVisible(label);
  };

  setupLfoSlider(lfoRateSlider, lfoRateLabel, "Rate", 0.1f, 20.0f, 2.0f,
                 juce::Slider::LinearHorizontal,
                 juce::Slider::TextBoxBelow);
  setupLfoSlider(lfoDepthFilterSlider, lfoDepthFilterLabel, "Flt", 0.0f, 1.0f,
                 0.0f, juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  setupLfoSlider(lfoDepthPWSlider, lfoDepthPWLabel, "PW", 0.0f, 1.0f, 0.0f,
                 juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  setupLfoSlider(lfoDepthPitchSlider, lfoDepthPitchLabel, "Vib", 0.0f, 1.0f,
                 0.0f, juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);

  // ========== LFO2 SETUP ==========
  lfo2EnableButton.setTooltip("LFO 2: Second LFO for additional modulation");
  lfo2EnableButton.setColour(juce::ToggleButton::tickColourId,
                             juce::Colours::orange);
  addAndMakeVisible(lfo2EnableButton);

  lfo2WaveformSelector.addItem("Tri", 1);
  lfo2WaveformSelector.addItem("Saw", 2);
  lfo2WaveformSelector.addItem("Sq", 3);
  lfo2WaveformSelector.addItem("S&H", 4);
  lfo2WaveformSelector.setTooltip("LFO2 waveform shape");
  addAndMakeVisible(lfo2WaveformSelector);

  setupLfoSlider(lfo2RateSlider, lfo2RateLabel, "Rate", 0.1f, 20.0f, 3.0f,
                 juce::Slider::LinearHorizontal,
                 juce::Slider::TextBoxBelow);
  setupLfoSlider(lfo2DepthFilterSlider, lfo2DepthFilterLabel, "Flt", 0.0f,
                 1.0f, 0.0f, juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  setupLfoSlider(lfo2DepthPWSlider, lfo2DepthPWLabel, "PW", 0.0f, 1.0f, 0.0f,
                 juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  setupLfoSlider(lfo2DepthPitchSlider, lfo2DepthPitchLabel, "Vib", 0.0f, 1.0f,
                 0.0f, juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);

  // ========== PITCH BEND RANGE ==========
  pitchBendRangeLabel.setText("PB Range", juce::dontSendNotification);
  pitchBendRangeLabel.setColour(juce::Label::textColourId,
                                juce::Colours::lightgrey);
  pitchBendRangeLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
  addAndMakeVisible(pitchBendRangeLabel);

  pitchBendRangeSelector.addItem("+/- 2", 1);
  pitchBendRangeSelector.addItem("+/- 3", 2);
  pitchBendRangeSelector.addItem("+/- 5", 3);
  pitchBendRangeSelector.addItem("+/- 7", 4);
  pitchBendRangeSelector.addItem("+/- 12", 5);
  pitchBendRangeSelector.setTooltip("Pitch bend range in semitones");
  addAndMakeVisible(pitchBendRangeSelector);

  // Sync PB ComboBox with APVTS
  auto *pbParam = processor.apvts.getParameter("pitchBendRange");
  if (pbParam) {
    int pbVal = static_cast<int>(pbParam->convertFrom0to1(pbParam->getValue()));
    int selId = 1;
    if (pbVal == 3)
      selId = 2;
    else if (pbVal == 5)
      selId = 3;
    else if (pbVal == 7)
      selId = 4;
    else if (pbVal >= 12)
      selId = 5;
    pitchBendRangeSelector.setSelectedId(selId, juce::dontSendNotification);
  }
  pitchBendRangeSelector.onChange = [this]() {
    static constexpr int vals[] = {2, 3, 5, 7, 12};
    int idx = pitchBendRangeSelector.getSelectedId() - 1;
    if (idx >= 0 && idx < 5) {
      auto *p = processor.apvts.getParameter("pitchBendRange");
      if (p)
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(vals[idx])));
    }
  };

  // ========== MOD MATRIX SLOTS ==========
  for (int i = 0; i < BreadbinProcessor::kModSlots; ++i) {
    auto &s = slots[i];
    auto prefix = "mod" + juce::String(i) + "_";

    s.slotLabel.setText("Slot " + juce::String(i + 1),
                        juce::dontSendNotification);
    s.slotLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(s.slotLabel);

    s.srcBox.addItem("None", 1);
    s.srcBox.addItem("LFO1", 2);
    s.srcBox.addItem("LFO2", 3);
    s.srcBox.addItem("FiltEnv", 4);
    s.srcBox.addItem("ModWheel", 5);
    s.srcBox.addItem("Velocity", 6);
    addAndMakeVisible(s.srcBox);

    s.dstBox.addItem("None", 1);
    s.dstBox.addItem("Filter", 2);
    s.dstBox.addItem("PW", 3);
    s.dstBox.addItem("Pitch", 4);
    s.dstBox.addItem("Resonance", 5);
    addAndMakeVisible(s.dstBox);

    s.amtSlider.setRange(-1.0, 1.0, 0.01);
    s.amtSlider.setValue(0.0);
    s.amtSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    s.amtSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    s.amtSlider.setColour(juce::Slider::textBoxTextColourId,
                          juce::Colours::cyan);
    s.amtSlider.setColour(juce::Slider::textBoxOutlineColourId,
                          juce::Colours::transparentBlack);
    addAndMakeVisible(s.amtSlider);

    s.sourceValueLabel.setColour(juce::Label::textColourId,
                                 juce::Colours::cyan);
    s.sourceValueLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    s.sourceValueLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(s.sourceValueLabel);

    s.contributionLabel.setColour(juce::Label::textColourId,
                                  juce::Colours::orange);
    s.contributionLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    s.contributionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(s.contributionLabel);

    s.srcAttach =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.apvts, prefix + "src", s.srcBox);
    s.dstAttach =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.apvts, prefix + "dst", s.dstBox);
    s.amtAttach =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, prefix + "amt", s.amtSlider);
  }

  // ========== PWM SWEEP SETUP ==========
  pwmSweepEnableButton.setColour(juce::ToggleButton::tickColourId,
                                  juce::Colours::greenyellow);
  pwmSweepEnableButton.setTooltip("Enable dedicated PWM sweep oscillator");
  addAndMakeVisible(pwmSweepEnableButton);

  pwmSweepRateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  pwmSweepRateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
  pwmSweepRateSlider.setRange(0.05, 10.0, 0.01);
  pwmSweepRateSlider.setColour(juce::Slider::textBoxTextColourId,
                                juce::Colours::greenyellow);
  pwmSweepRateSlider.setColour(juce::Slider::textBoxOutlineColourId,
                                juce::Colours::transparentBlack);
  addAndMakeVisible(pwmSweepRateSlider);

  pwmSweepRateLabel.setText("Rate", juce::dontSendNotification);
  pwmSweepRateLabel.setColour(juce::Label::textColourId,
                               juce::Colours::greenyellow);
  pwmSweepRateLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
  addAndMakeVisible(pwmSweepRateLabel);

  pwmSweepDepthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  pwmSweepDepthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
  pwmSweepDepthSlider.setRange(0.0, 1.0, 0.01);
  pwmSweepDepthSlider.setColour(juce::Slider::textBoxTextColourId,
                                 juce::Colours::greenyellow);
  pwmSweepDepthSlider.setColour(juce::Slider::textBoxOutlineColourId,
                                 juce::Colours::transparentBlack);
  addAndMakeVisible(pwmSweepDepthSlider);

  pwmSweepDepthLabel.setText("Depth", juce::dontSendNotification);
  pwmSweepDepthLabel.setColour(juce::Label::textColourId,
                                juce::Colours::greenyellow);
  pwmSweepDepthLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
  addAndMakeVisible(pwmSweepDepthLabel);

  // ========== APVTS ATTACHMENTS (LFO1/LFO2) ==========
  lfoEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "lfoEnable", lfoEnableButton);
  lfoWaveAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "lfoWave", lfoWaveformSelector);
  lfoRateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfoRate", *lfoRateSlider);
  lfoDepthFiltAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfoDepthFilt", *lfoDepthFilterSlider);
  lfoDepthPWAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfoDepthPW", *lfoDepthPWSlider);
  lfoDepthPitchAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfoDepthPitch", *lfoDepthPitchSlider);

  lfo2EnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "lfo2Enable", lfo2EnableButton);
  lfo2WaveAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "lfo2Wave", lfo2WaveformSelector);
  lfo2RateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfo2Rate", *lfo2RateSlider);
  lfo2DepthFiltAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfo2DepthFilt", *lfo2DepthFilterSlider);
  lfo2DepthPWAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfo2DepthPW", *lfo2DepthPWSlider);
  lfo2DepthPitchAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfo2DepthPitch", *lfo2DepthPitchSlider);

  // PWM Sweep attachments
  pwmSweepEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "pwmSweepEnable", pwmSweepEnableButton);
  pwmSweepRateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "pwmSweepRate", pwmSweepRateSlider);
  pwmSweepDepthAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "pwmSweepDepth", pwmSweepDepthSlider);

  startTimerHz(30);
  setSize(panelWidth, panelHeight);
}

void ModMatrixPanel::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(30, 30, 35));

  // Section labels
  g.setColour(juce::Colours::cyan);
  g.setFont(11.0f);
  g.drawText("LFO 1", 4, 2, 50, 14, juce::Justification::centredLeft);

  g.setColour(juce::Colours::orange);
  g.drawText("LFO 2", 4, 57, 50, 14, juce::Justification::centredLeft);

  // PWM Sweep label
  g.setColour(juce::Colours::greenyellow);
  g.drawText("PWM Sweep", 4, 112, 70, 14, juce::Justification::centredLeft);

  // Separator after PWM row
  g.setColour(juce::Colours::grey.withAlpha(0.3f));
  g.drawHorizontalLine(140, 4.0f, static_cast<float>(panelWidth - 4));

  // PB Range label drawn via component

  // Separator after PB row
  g.drawHorizontalLine(168, 4.0f, static_cast<float>(panelWidth - 4));

  // Mod matrix column headers
  const int mmTop = 172;
  g.setColour(juce::Colours::cyan);
  g.setFont(12.0f);
  g.drawText("Source", 50, mmTop, 90, 16, juce::Justification::centred);
  g.drawText("Dest", 150, mmTop, 90, 16, juce::Justification::centred);
  g.drawText("Amount", 250, mmTop, 120, 16, juce::Justification::centred);
  g.drawText("Src", 380, mmTop, 45, 16, juce::Justification::centred);
  g.setColour(juce::Colours::orange);
  g.drawText("Out", 430, mmTop, 45, 16, juce::Justification::centred);
}

void ModMatrixPanel::resized() {
  // LFO row layout helper
  auto layoutLfoRow = [this](int y,
                             juce::ToggleButton &enableBtn,
                             juce::ComboBox &waveBox,
                             MappableSlider &rateSldr, juce::Label &rateLbl,
                             MappableSlider &fltSldr, juce::Label &fltLbl,
                             MappableSlider &pwSldr, juce::Label &pwLbl,
                             MappableSlider &vibSldr, juce::Label &vibLbl) {
    int x = 4;
    enableBtn.setBounds(x, y + 14, 50, 20);
    x += 54;
    waveBox.setBounds(x, y + 14, 68, 20);
    x += 76;
    rateLbl.setBounds(x, y + 2, 40, 12);
    rateSldr.setBounds(x, y + 14, 80, 20);
    x += 88;
    const int dW = 42;
    fltLbl.setBounds(x, y + 2, dW, 12);
    fltSldr.setBounds(x, y + 14, dW, 36);
    x += dW + 4;
    pwLbl.setBounds(x, y + 2, dW, 12);
    pwSldr.setBounds(x, y + 14, dW, 36);
    x += dW + 4;
    vibLbl.setBounds(x, y + 2, dW, 12);
    vibSldr.setBounds(x, y + 14, dW, 36);
  };

  // LFO1 row: y=0..54
  layoutLfoRow(0, lfoEnableButton, lfoWaveformSelector,
               *lfoRateSlider, lfoRateLabel,
               *lfoDepthFilterSlider, lfoDepthFilterLabel,
               *lfoDepthPWSlider, lfoDepthPWLabel,
               *lfoDepthPitchSlider, lfoDepthPitchLabel);

  // LFO2 row: y=55..109
  layoutLfoRow(55, lfo2EnableButton, lfo2WaveformSelector,
               *lfo2RateSlider, lfo2RateLabel,
               *lfo2DepthFilterSlider, lfo2DepthFilterLabel,
               *lfo2DepthPWSlider, lfo2DepthPWLabel,
               *lfo2DepthPitchSlider, lfo2DepthPitchLabel);

  // PWM Sweep row: y=112..139
  {
    int x = 4;
    pwmSweepEnableButton.setBounds(x, 126, 60, 20);
    x += 64;
    pwmSweepRateLabel.setBounds(x, 114, 35, 12);
    pwmSweepRateSlider.setBounds(x, 126, 170, 20);
    x += 178;
    pwmSweepDepthLabel.setBounds(x, 114, 40, 12);
    pwmSweepDepthSlider.setBounds(x, 126, 170, 20);
  }

  // PB Range row: y=144..166
  pitchBendRangeLabel.setBounds(4, 146, 60, 18);
  pitchBendRangeSelector.setBounds(66, 146, 90, 20);

  // Mod matrix slots: y=190 onward (header at 172)
  const int mmTop = 190;
  const int rowH = 35;
  for (int i = 0; i < BreadbinProcessor::kModSlots; ++i) {
    auto &s = slots[i];
    int y = mmTop + i * rowH;

    s.slotLabel.setBounds(4, y + 8, 44, 18);
    s.srcBox.setBounds(50, y + 6, 90, 22);
    s.dstBox.setBounds(150, y + 6, 90, 22);
    s.amtSlider.setBounds(250, y + 6, 120, 22);
    s.sourceValueLabel.setBounds(380, y + 6, 45, 22);
    s.contributionLabel.setBounds(430, y + 6, 45, 22);
  }
}

void ModMatrixPanel::timerCallback() {
  for (int i = 0; i < BreadbinProcessor::kModSlots; ++i) {
    float srcVal = processor.getModSlotSourceValue(i);
    float contrib = processor.getModSlotContribution(i);
    slots[i].sourceValueLabel.setText(juce::String(srcVal, 2),
                                      juce::dontSendNotification);
    slots[i].contributionLabel.setText(juce::String(contrib, 2),
                                       juce::dontSendNotification);
  }
}

// ========== CHORD MEMORY PANEL ==========

ChordMemoryPanel::ChordMemoryPanel(BreadbinProcessor &proc) : processor(proc) {
  // Enable toggle
  enableButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
  addAndMakeVisible(enableButton);
  enableAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processor.apvts, "chordEnable", enableButton);

  // 4 slot radio buttons
  int currentSlot = static_cast<int>(processor.apvts.getRawParameterValue("chordSlot")->load());
  for (int s = 0; s < 4; ++s) {
    slotButtons[s].setButtonText(juce::String(s + 1));
    slotButtons[s].setColour(juce::TextButton::buttonColourId,
                              s == currentSlot ? juce::Colours::cyan.withAlpha(0.3f)
                                               : juce::Colour(50, 50, 60));
    slotButtons[s].setColour(juce::TextButton::textColourOnId, juce::Colours::cyan);
    slotButtons[s].setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    slotButtons[s].onClick = [this, s]() {
      if (auto *param = processor.apvts.getParameter("chordSlot"))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(s)));
      for (int j = 0; j < 4; ++j)
        slotButtons[j].setColour(juce::TextButton::buttonColourId,
                                  j == s ? juce::Colours::cyan.withAlpha(0.3f)
                                         : juce::Colour(50, 50, 60));
    };
    addAndMakeVisible(slotButtons[s]);
  }

  // 4 rows x 5 interval sliders
  for (int s = 0; s < 4; ++s) {
    slots[s].label.setText("Slot " + juce::String(s + 1), juce::dontSendNotification);
    slots[s].label.setColour(juce::Label::textColourId, juce::Colours::cyan);
    slots[s].label.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(slots[s].label);

    for (int i = 0; i < 5; ++i) {
      auto &slider = slots[s].sliders[i];
      slider.setSliderStyle(juce::Slider::LinearVertical);
      slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 36, 14);
      slider.setRange(-24, 24, 1);
      slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
      slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
      addAndMakeVisible(slider);

      auto id = "chord_s" + juce::String(s) + "_i" + juce::String(i);
      slots[s].attachments[i] =
          std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
              processor.apvts, id, slider);
    }
  }
}

void ChordMemoryPanel::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(30, 30, 35));

  g.setColour(juce::Colours::cyan);
  g.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
  g.drawText("CHORD MEMORY", 10, 10, 120, 20, juce::Justification::centredLeft);

  // Column headers
  g.setColour(juce::Colours::lightgrey);
  g.setFont(juce::Font(juce::FontOptions(10.0f)));
  for (int i = 0; i < 5; ++i)
    g.drawText("Int " + juce::String(i + 1), 100 + i * 70, 35, 60, 14,
               juce::Justification::centred);
}

void ChordMemoryPanel::resized() {
  enableButton.setBounds(140, 8, 80, 24);
  for (int s = 0; s < 4; ++s)
    slotButtons[s].setBounds(230 + s * 50, 10, 40, 20);

  for (int s = 0; s < 4; ++s) {
    int y = 50 + s * 40;
    slots[s].label.setBounds(10, y + 5, 60, 20);
    for (int i = 0; i < 5; ++i)
      slots[s].sliders[i].setBounds(80 + i * 70, y, 60, 36);
  }
}

// ========== WAVETABLE PANEL ==========

WavetablePanel::WavetablePanel(BreadbinProcessor &proc) : processor(proc) {
  // Enable toggle
  enableButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
  addAndMakeVisible(enableButton);
  enableAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processor.apvts, "wtEnable", enableButton);

  // Steps slider
  stepsLabel.setText("Steps", juce::dontSendNotification);
  stepsLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  stepsLabel.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(stepsLabel);

  numStepsSlider.setRange(1.0, 16.0, 1.0);
  numStepsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  numStepsSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 30, 14);
  numStepsSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
  addAndMakeVisible(numStepsSlider);
  stepsAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor.apvts, "wtNumSteps", numStepsSlider);

  // Rate slider
  rateLabel.setText("Rate Hz", juce::dontSendNotification);
  rateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  rateLabel.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(rateLabel);

  rateSlider.setRange(1.0, 200.0, 1.0);
  rateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  rateSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 30, 14);
  rateSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
  addAndMakeVisible(rateSlider);
  rateAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor.apvts, "wtRate", rateSlider);

  // Loop toggle
  loopButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
  addAndMakeVisible(loopButton);
  loopAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processor.apvts, "wtLoop", loopButton);

  // Per-step controls
  for (int i = 0; i < 16; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    auto &step = steps[i];

    // Waveform ComboBox
    step.waveBox.addItem("T", 1);  // Triangle
    step.waveBox.addItem("S", 2);  // Sawtooth
    step.waveBox.addItem("P", 3);  // Pulse
    step.waveBox.addItem("N", 4);  // Noise
    addAndMakeVisible(step.waveBox);
    step.waveAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, prefix + "wave", step.waveBox);

    // Pitch slider
    step.pitchSlider.setRange(-24.0, 24.0, 1.0);
    step.pitchSlider.setSliderStyle(juce::Slider::LinearVertical);
    step.pitchSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 30, 12);
    step.pitchSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    step.pitchSlider.setColour(juce::Slider::thumbColourId, juce::Colours::cyan);
    addAndMakeVisible(step.pitchSlider);
    step.pitchAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, prefix + "pitch", step.pitchSlider);

    // PW slider
    step.pwSlider.setRange(0.0, 4095.0, 1.0);
    step.pwSlider.setSliderStyle(juce::Slider::LinearVertical);
    step.pwSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 30, 12);
    step.pwSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    step.pwSlider.setColour(juce::Slider::thumbColourId, juce::Colours::orange);
    addAndMakeVisible(step.pwSlider);
    step.pwAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, prefix + "pw", step.pwSlider);
  }

  startTimer(33);
  setSize(panelWidth, panelHeight);
}

void WavetablePanel::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(30, 30, 35));

  // Title
  g.setColour(juce::Colours::cyan);
  g.setFont(16.0f);
  g.drawText("WAVETABLE STEP SEQUENCER", 0, 4, panelWidth, 20,
             juce::Justification::centred);

  // Row labels
  g.setColour(juce::Colours::lightgrey);
  g.setFont(11.0f);
  g.drawText("Wave", 4, 62, 48, 16, juce::Justification::centredRight);
  g.drawText("Pitch", 4, 120, 48, 16, juce::Justification::centredRight);
  g.drawText("PW", 4, 225, 48, 16, juce::Justification::centredRight);

  // Step number headers
  int numActiveSteps = static_cast<int>(numStepsSlider.getValue());
  auto &wt = processor.getWavetable();
  int currentStep = wt.enabled ? wt.currentStep : -1;

  g.setFont(10.0f);
  for (int i = 0; i < 16; ++i) {
    int x = 55 + i * 40;
    bool isActive = i < numActiveSteps;
    bool isCurrent = (i == currentStep) && wt.enabled;

    if (isCurrent) {
      g.setColour(juce::Colours::cyan);
      g.fillRoundedRectangle(static_cast<float>(x), 44.0f, 38.0f, 14.0f, 3.0f);
      g.setColour(juce::Colours::black);
    } else if (isActive) {
      g.setColour(juce::Colours::white);
    } else {
      g.setColour(juce::Colour(70, 70, 80));
    }
    g.drawText(juce::String(i + 1), x, 44, 38, 14, juce::Justification::centred);
  }
}

void WavetablePanel::resized() {
  // Header row (y=24..42)
  enableButton.setBounds(10, 24, 65, 20);
  stepsLabel.setBounds(80, 24, 40, 18);
  numStepsSlider.setBounds(122, 22, 80, 34);
  rateLabel.setBounds(210, 24, 50, 18);
  rateSlider.setBounds(262, 22, 130, 34);
  loopButton.setBounds(400, 24, 55, 20);

  // Per-step columns
  const int leftMargin = 55;
  const int colW = 40;
  const int ctrlW = 38;

  for (int i = 0; i < 16; ++i) {
    int x = leftMargin + i * colW;
    auto &step = steps[i];

    step.waveBox.setBounds(x, 60, ctrlW, 22);
    step.pitchSlider.setBounds(x, 84, ctrlW, 110);
    step.pwSlider.setBounds(x, 196, ctrlW, 110);
  }
}

void WavetablePanel::timerCallback() {
  auto &wt = processor.getWavetable();
  int current = wt.enabled ? wt.currentStep : -1;
  if (current != lastHighlightedStep) {
    lastHighlightedStep = current;
    repaint();
  }

  // Dim inactive step columns
  int numActive = static_cast<int>(numStepsSlider.getValue());
  for (int i = 0; i < 16; ++i) {
    float alpha = (i < numActive) ? 1.0f : 0.3f;
    steps[i].waveBox.setAlpha(alpha);
    steps[i].pitchSlider.setAlpha(alpha);
    steps[i].pwSlider.setAlpha(alpha);
  }
}

void BreadbinEditor::showChordMemoryPopup() {
  if (chordMemoryWindow != nullptr) {
    chordMemoryWindow->toFront(true);
    return;
  }

  auto *panel = new ChordMemoryPanel(processor);

  juce::DialogWindow::LaunchOptions opts;
  opts.content.setOwned(panel);
  opts.dialogTitle = "Chord Memory";
  opts.dialogBackgroundColour = juce::Colour(30, 30, 35);
  opts.escapeKeyTriggersCloseButton = true;
  opts.useNativeTitleBar = true;
  opts.resizable = false;
  opts.componentToCentreAround = this;

  chordMemoryWindow = opts.launchAsync();
}

void BreadbinEditor::showModMatrixPopup() {
  if (modMatrixWindow != nullptr) {
    modMatrixWindow->toFront(true);
    return;
  }

  auto *panel = new ModMatrixPanel(processor);

  juce::DialogWindow::LaunchOptions opts;
  opts.content.setOwned(panel);
  opts.dialogTitle = "Modulation";
  opts.dialogBackgroundColour = juce::Colour(30, 30, 35);
  opts.escapeKeyTriggersCloseButton = true;
  opts.useNativeTitleBar = true;
  opts.resizable = false;
  opts.componentToCentreAround = this;

  modMatrixWindow = opts.launchAsync();
}

void BreadbinEditor::showWavetablePopup() {
  if (wavetableWindow != nullptr) {
    wavetableWindow->toFront(true);
    return;
  }

  auto *panel = new WavetablePanel(processor);

  juce::DialogWindow::LaunchOptions opts;
  opts.content.setOwned(panel);
  opts.dialogTitle = "Wavetable Step Sequencer";
  opts.dialogBackgroundColour = juce::Colour(30, 30, 35);
  opts.escapeKeyTriggersCloseButton = true;
  opts.useNativeTitleBar = true;
  opts.resizable = false;
  opts.componentToCentreAround = this;

  wavetableWindow = opts.launchAsync();
}

void BreadbinEditor::setupControls() {
  // Title label removed - logo is in lower left corner

  // Mode
  modeLabel.setText("Mode:", juce::dontSendNotification);
  modeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(modeLabel);

  dualModeSelector.addItem("Stereo Split", 1);
  dualModeSelector.addItem("Unison", 2);
  dualModeSelector.addItem("Multitimbral", 3);
  dualModeSelector.setSelectedId(1);
  dualModeSelector.setTooltip("Stereo: L/R SID split\nUnison: Both SIDs "
                              "together\nMultitimbral: Separate MIDI channels");
  dualModeSelector.onChange = [this]() {
    processor.setDualMode(static_cast<BreadbinProcessor::DualMode>(
        dualModeSelector.getSelectedId() - 1));
  };
  addAndMakeVisible(dualModeSelector);

  // Global Factory Presets
  globalPresetLabel.setText("Patch:", juce::dontSendNotification);
  globalPresetLabel.setColour(juce::Label::textColourId,
                              juce::Colours::lightgrey);
  addAndMakeVisible(globalPresetLabel);

  globalPresetSelector.addItem("Dual Lead", 1);
  globalPresetSelector.addItem("Pad Stack", 2);
  globalPresetSelector.addItem("Arpeggiated", 3);
  globalPresetSelector.addItem("Fat Unison", 4);
  globalPresetSelector.addItem("Retro Synth", 5);
  globalPresetSelector.addItem("Chord Stab", 6);
  globalPresetSelector.addItem("Mod Madness", 7);
  globalPresetSelector.addItem("WT Arpeggio", 8);
  globalPresetSelector.addItem("WT Morph", 9);
  globalPresetSelector.setSelectedId(1);
  globalPresetSelector.setTooltip("Factory presets - applies to entire plugin");
  globalPresetSelector.onChange = [this]() {
    applyGlobalPreset(globalPresetSelector.getSelectedId());
    processor.snapshotPresetState();
  };
  addAndMakeVisible(globalPresetSelector);

  // Preset dirty indicator
  presetDirtyLabel.setColour(juce::Label::textColourId, juce::Colours::gold);
  presetDirtyLabel.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
  presetDirtyLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(presetDirtyLabel);

  // Voice Preset (for selected voice)
  presetLabel.setText("Voice:", juce::dontSendNotification);
  presetLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(presetLabel);

  presetSelector.addItem("-- Select --", 1);
  presetSelector.addItem("Classic Lead (Monty)", 2);
  presetSelector.addItem("Fat Bass (Ocean)", 3);
  presetSelector.addItem("PWM Pad (Hubbard)", 4);
  presetSelector.addItem("Noise Snare", 5);
  presetSelector.addItem("Retro Triangle", 6);
  presetSelector.setSelectedId(1);
  presetSelector.setTooltip("Applies preset to currently selected voice");
  presetSelector.onChange = [this]() {
    if (presetSelector.getSelectedId() > 1) {
      applyPreset(presetSelector.getSelectedId());
      processor.snapshotPresetState();
    }
  };
  addAndMakeVisible(presetSelector);

  // Save/Load Patch buttons (full state) - Now Icon-based
  auto diskPath = makeDiskPath();
  auto folderPath = makeFolderPath();

  savePatchButton.setShape(diskPath, true, true, false);
  savePatchButton.setTooltip("Save all settings to a .breadbin patch file");
  savePatchButton.onClick = [this]() { savePresetToFile(); };
  addAndMakeVisible(savePatchButton);

  loadPatchButton.setShape(folderPath, true, true, false);
  loadPatchButton.setTooltip("Load settings from a .breadbin patch file");
  loadPatchButton.onClick = [this]() { loadPresetFromFile(); };
  addAndMakeVisible(loadPatchButton);

  // Save/Load Voice buttons (selected oscillator)
  saveVoiceButton.setShape(diskPath, true, true, false);
  saveVoiceButton.setTooltip("Save this voice's settings to a .voice file");
  saveVoiceButton.onClick = [this]() { saveVoicePresetToFile(); };
  addAndMakeVisible(saveVoiceButton);

  loadVoiceButton.setShape(folderPath, true, true, false);
  loadVoiceButton.setTooltip("Load settings for this voice from a .voice file");
  loadVoiceButton.onClick = [this]() { loadVoicePresetFromFile(); };
  addAndMakeVisible(loadVoiceButton);

  // Chip Age (Time Machine)
  agingLabel.setText("Chip Age:", juce::dontSendNotification);
  agingLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  agingLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(agingLabel);

  agingStartLabel.setText("Fresh", juce::dontSendNotification);
  agingStartLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
  agingStartLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
  addAndMakeVisible(agingStartLabel);

  agingEndLabel.setText("Vintage", juce::dontSendNotification);
  agingEndLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
  agingEndLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
  addAndMakeVisible(agingEndLabel);

  agingSlider.setRange(0.0, 1.0, 0.01);
  agingSlider.setValue(0.0);
  agingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  agingSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  agingSlider.setTooltip(
      "Time Machine: Simulates capacitor aging from 1982 to now");
  agingSlider.onValueChange = [this]() {
    processor.setAgingFactor(static_cast<float>(agingSlider.getValue()));
  };
  addAndMakeVisible(agingSlider);

  // ========== ARPEGGIATOR ==========
  arpEnableButton.setTooltip("Enable the arpeggiator");
  addAndMakeVisible(arpEnableButton);

  arpPatternSelector.addItem("Up", 1);
  arpPatternSelector.addItem("Down", 2);
  arpPatternSelector.addItem("Up/Down", 3);
  arpPatternSelector.addItem("Random", 4);
  arpPatternSelector.setSelectedId(static_cast<int>(processor.getArpPattern()) +
                                       1,
                                   juce::dontSendNotification);
  arpPatternSelector.onChange = [this]() {
    processor.setArpPattern(static_cast<BreadbinProcessor::ArpPattern>(
        arpPatternSelector.getSelectedId() - 1));
  };
  arpPatternSelector.setTooltip("Arp pattern: Up, Down, Up/Down, or Random");
  addAndMakeVisible(arpPatternSelector);

  arpRateSlider.setRange(1.0, 100.0, 1.0);
  arpRateSlider.setValue(processor.getArpRate());
  arpRateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  arpRateSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 35, 18);
  arpRateSlider.setTooltip("Arp Rate (Hz) - PAL=50, NTSC=60");
  arpRateSlider.onValueChange = [this]() {
    processor.setArpRate(static_cast<float>(arpRateSlider.getValue()));
  };
  addAndMakeVisible(arpRateSlider);

  arpRateLabel.setText("Hz", juce::dontSendNotification);
  arpRateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(arpRateLabel);

  arpOctaveSelector.addItem("1 Octave", 1);
  arpOctaveSelector.addItem("2 Octaves", 2);
  arpOctaveSelector.addItem("3 Octaves", 3);
  arpOctaveSelector.addItem("4 Octaves", 4);
  arpOctaveSelector.setSelectedId(processor.getArpOctaves(),
                                  juce::dontSendNotification);
  arpOctaveSelector.onChange = [this]() {
    processor.setArpOctaves(arpOctaveSelector.getSelectedId());
  };
  arpOctaveSelector.setTooltip("Arpeggiator range: 1-4 octaves");
  addAndMakeVisible(arpOctaveSelector);

  // Glide/Portamento
  glideTimeLabel.setText("Glide", juce::dontSendNotification);
  glideTimeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  glideTimeLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
  addAndMakeVisible(glideTimeLabel);

  glideTimeSlider.setRange(0.0, 2000.0, 1.0);
  glideTimeSlider.setValue(processor.getGlideTimeMs());
  glideTimeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  glideTimeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
  glideTimeSlider.setTooltip("Portamento time (0 = off, up to 2000ms)");
  glideTimeSlider.onValueChange = [this]() {
    processor.setGlideTimeMs(static_cast<float>(glideTimeSlider.getValue()));
  };
  addAndMakeVisible(glideTimeSlider);

  // Master Volume
  masterVolLabel.setText("Master", juce::dontSendNotification);
  masterVolLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  masterVolLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
  addAndMakeVisible(masterVolLabel);

  masterVolSlider.setRange(0.0, 1.0, 0.01);
  masterVolSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  masterVolSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
  masterVolSlider.setTooltip("Master output volume (affects both SID chips)");
  // No onValueChange needed — APVTS attachment + processBlock sync handles it
  addAndMakeVisible(masterVolSlider);

  // External Audio Input
  extInputEnableButton.setToggleState(processor.isExtInputEnabled(),
                                      juce::dontSendNotification);
  extInputEnableButton.setTooltip("Route external audio through SID filters");
  addAndMakeVisible(extInputEnableButton);

  extInputLabel.setText("Level", juce::dontSendNotification);
  extInputLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  extInputLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
  addAndMakeVisible(extInputLabel);

  extInputLevelSlider.setRange(0.0, 2.0, 0.01);
  extInputLevelSlider.setValue(processor.getExtInputLevel());
  extInputLevelSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  extInputLevelSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40,
                                      18);
  extInputLevelSlider.setTooltip("External input level (0-200%)");
  extInputLevelSlider.onValueChange = [this]() {
    processor.setExtInputLevel(
        static_cast<float>(extInputLevelSlider.getValue()));
  };
  addAndMakeVisible(extInputLevelSlider);

  // Clock mode (PAL/NTSC)
  clockModeLabel.setText("Clock:", juce::dontSendNotification);
  clockModeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  clockModeLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(clockModeLabel);

  clockModeSelector.addItem("PAL", 1);
  clockModeSelector.addItem("NTSC", 2);
  clockModeSelector.setSelectedId(
      processor.getClockMode() == SIDEngine::ClockMode::NTSC ? 2 : 1);
  clockModeSelector.setTooltip(
      "PAL (985 kHz) or NTSC (1023 kHz) clock frequency");
  clockModeSelector.onChange = [this]() {
    processor.setClockMode(clockModeSelector.getSelectedId() == 2
                               ? SIDEngine::ClockMode::NTSC
                               : SIDEngine::ClockMode::PAL);
  };
  addAndMakeVisible(clockModeSelector);

  // ===== FX: CHORUS =====
  chorusEnableButton.setTooltip("Chorus: Dimension D-style stereo widening");
  addAndMakeVisible(chorusEnableButton);

  auto setupFXSlider = [this](juce::Slider &slider, juce::Label &label,
                              const juce::String &name, float minVal,
                              float maxVal, float defaultVal, float step,
                              const juce::String &tooltip) {
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(retroFont.withHeight(7.0f));
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    slider.setRange(minVal, maxVal, step);
    slider.setValue(defaultVal);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setTooltip(tooltip);
    addAndMakeVisible(slider);
  };

  setupFXSlider(chorusRateSlider, chorusRateLabel, "Rate", 0.1f, 10.0f, 1.5f,
                0.1f, "Chorus Rate (Hz)");
  setupFXSlider(chorusDepthSlider, chorusDepthLabel, "Depth", 0.0f, 1.0f,
                0.3f, 0.01f, "Chorus Depth");
  setupFXSlider(chorusMixSlider, chorusMixLabel, "Mix", 0.0f, 1.0f, 0.5f,
                0.01f, "Chorus Wet/Dry Mix");

  // ===== FX: DELAY =====
  delayEnableButton.setTooltip("Stereo Delay with independent L/R times");
  addAndMakeVisible(delayEnableButton);

  setupFXSlider(delayTimeLSlider, delayTimeLLabel, "L ms", 1.0f, 1000.0f,
                375.0f, 1.0f, "Delay Time Left (ms)");
  setupFXSlider(delayTimeRSlider, delayTimeRLabel, "R ms", 1.0f, 1000.0f,
                500.0f, 1.0f, "Delay Time Right (ms)");
  setupFXSlider(delayFeedbackSlider, delayFBLabel, "FB", 0.0f, 0.95f, 0.3f,
                0.01f, "Delay Feedback");
  setupFXSlider(delayMixSlider, delayMixLabel, "Mix", 0.0f, 1.0f, 0.3f,
                0.01f, "Delay Wet/Dry Mix");

  // ===== WAVETABLE STEP SEQUENCER =====
  wavetableButton.setTooltip("Wavetable: C64-style step sequencer editor");
  wavetableButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(60, 60, 70));
  wavetableButton.setColour(juce::TextButton::textColourOnId,
                            juce::Colours::cyan);
  wavetableButton.setColour(juce::TextButton::textColourOffId,
                            juce::Colours::cyan);
  wavetableButton.onClick = [this]() { showWavetablePopup(); };
  addAndMakeVisible(wavetableButton);

  // ===== MOD MATRIX BUTTON =====
  modMatrixButton.setTooltip("Modulation: LFO, pitch bend range, mod matrix");
  modMatrixButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(60, 60, 70));
  modMatrixButton.setColour(juce::TextButton::textColourOnId,
                            juce::Colours::cyan);
  modMatrixButton.setColour(juce::TextButton::textColourOffId,
                            juce::Colours::cyan);
  modMatrixButton.onClick = [this]() { showModMatrixPopup(); };
  addAndMakeVisible(modMatrixButton);

  // ===== CHORD MEMORY BUTTON =====
  chordMemoryButton.setTooltip("Chord Memory: Trigger chords from single keys");
  chordMemoryButton.setColour(juce::TextButton::buttonColourId,
                              juce::Colour(60, 60, 70));
  chordMemoryButton.setColour(juce::TextButton::textColourOnId,
                              juce::Colours::cyan);
  chordMemoryButton.setColour(juce::TextButton::textColourOffId,
                              juce::Colours::cyan);
  chordMemoryButton.onClick = [this]() { showChordMemoryPopup(); };
  addAndMakeVisible(chordMemoryButton);

  // ===== FILTER ENVELOPE =====
  filterEnvEnableButton.setTooltip("Filter Envelope: Dedicated ADSR for filter cutoff");
  filterEnvEnableButton.setButtonText("Filt Env");
  addAndMakeVisible(filterEnvEnableButton);

  auto setupFilterEnvSlider = [this](juce::Slider &slider, juce::Label &label,
                                     const juce::String &name, float minVal,
                                     float maxVal, float defaultVal,
                                     const juce::String &tooltip) {
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(retroFont.withHeight(7.0f));
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    slider.setRange(minVal, maxVal, 0.001);
    slider.setValue(defaultVal);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setTooltip(tooltip);
    addAndMakeVisible(slider);
  };

  setupFilterEnvSlider(filterEnvAttackSlider, filterEnvAttackLabel, "A",
                       0.001f, 10.0f, 0.01f, "Filter Env Attack (seconds)");
  setupFilterEnvSlider(filterEnvDecaySlider, filterEnvDecayLabel, "D",
                       0.001f, 10.0f, 0.3f, "Filter Env Decay (seconds)");
  setupFilterEnvSlider(filterEnvSustainSlider, filterEnvSustainLabel, "S",
                       0.0f, 1.0f, 0.5f, "Filter Env Sustain level");
  setupFilterEnvSlider(filterEnvReleaseSlider, filterEnvReleaseLabel, "R",
                       0.001f, 10.0f, 0.5f, "Filter Env Release (seconds)");

  filterEnvAmountLabel.setText("Amt", juce::dontSendNotification);
  filterEnvAmountLabel.setColour(juce::Label::textColourId,
                                 juce::Colours::lightgrey);
  filterEnvAmountLabel.setFont(retroFont.withHeight(7.0f));
  filterEnvAmountLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(filterEnvAmountLabel);

  filterEnvAmountSlider.setRange(-1.0, 1.0, 0.01);
  filterEnvAmountSlider.setValue(0.5);
  filterEnvAmountSlider.setSliderStyle(
      juce::Slider::RotaryHorizontalVerticalDrag);
  filterEnvAmountSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40,
                                        12);
  filterEnvAmountSlider.setColour(juce::Slider::textBoxTextColourId,
                                  juce::Colours::green);
  filterEnvAmountSlider.setColour(juce::Slider::textBoxOutlineColourId,
                                  juce::Colours::transparentBlack);
  filterEnvAmountSlider.setTooltip(
      "Filter Env Amount: Bipolar (-1 to +1). Positive opens filter on attack.");
  addAndMakeVisible(filterEnvAmountSlider);

  // Keyboard
  keyboard.setKeyWidth(16.0f);
  keyboard.setAvailableRange(36, 84); // C2 to C6 - reasonable SID range
  addAndMakeVisible(keyboard);
}

void BreadbinEditor::setupLeftSID() {
  leftSIDLabel.setText("LEFT SID", juce::dontSendNotification);
  leftSIDLabel.setFont(retroFont.withHeight(10.0f));
  leftSIDLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
  leftSIDLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(leftSIDLabel);

  leftChipSelector.addItem("MOS 6581", 1);
  leftChipSelector.addItem("MOS 8580", 2);
  leftChipSelector.setSelectedId(1);
  leftChipSelector.onChange = [this]() {
    processor.setLeftChipModel(leftChipSelector.getSelectedId() == 1
                                   ? SIDEngine::ChipModel::MOS6581
                                   : SIDEngine::ChipModel::MOS8580);
  };
  leftChipSelector.setTooltip(
      "6581: Classic warm sound, 8580: Cleaner with better filters");
  addAndMakeVisible(leftChipSelector);

  // Voice buttons and enables for L SID (voices 0-2)
  for (int i = 0; i < 3; ++i) {
    leftVoiceButtons[i].setButtonText(juce::String(i + 1));
    leftVoiceButtons[i].onClick = [this, i]() { selectVoice(i); };
    leftVoiceButtons[i].setTooltip("Select Voice " + juce::String(i + 1) +
                                   " for editing");
    addAndMakeVisible(leftVoiceButtons[i]);

    leftVoiceEnables[i].setButtonText("");
    leftVoiceEnables[i].setToggleState(true, juce::dontSendNotification);
    leftVoiceEnables[i].onClick = [this, i]() {
      processor.getVoiceSettings(i).enabled =
          leftVoiceEnables[i].getToggleState();
    };
    leftVoiceEnables[i].setTooltip("Enable/disable Voice " +
                                   juce::String(i + 1));
    addAndMakeVisible(leftVoiceEnables[i]);
  }

  // Filter
  leftCutoffLabel.setText("Cutoff", juce::dontSendNotification);
  leftCutoffLabel.setColour(juce::Label::textColourId,
                            juce::Colours::lightgrey);
  leftCutoffLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(leftCutoffLabel);

  leftCutoffSlider.setRange(0, 2047, 1);
  leftCutoffSlider.setValue(1024);
  leftCutoffSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  leftCutoffSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  leftCutoffSlider.setTooltip("Filter Cutoff Frequency (0-2047)");
  leftCutoffSlider.onValueChange = [this]() {
    int val = static_cast<int>(leftCutoffSlider.getValue());
    processor.setBaseFilterCutoff(true, val);
    processor.getLeftSID().setFilterCutoff(val);
  };
  addAndMakeVisible(leftCutoffSlider);

  leftResonanceLabel.setText("Res", juce::dontSendNotification);
  leftResonanceLabel.setColour(juce::Label::textColourId,
                               juce::Colours::lightgrey);
  leftResonanceLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(leftResonanceLabel);

  leftResonanceSlider.setRange(0, 15, 1);
  leftResonanceSlider.setValue(0);
  leftResonanceSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  leftResonanceSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  leftResonanceSlider.setTooltip("Filter Resonance (0-15)");
  leftResonanceSlider.onValueChange = [this]() {
    int val = static_cast<int>(leftResonanceSlider.getValue());
    processor.setBaseFilterResonance(true, val);
    processor.getLeftSID().setFilterResonance(val);
  };
  addAndMakeVisible(leftResonanceSlider);

  // Modulation meters for left SID
  cutoffMeterL.setRange(0.0f, 2047.0f);
  addAndMakeVisible(cutoffMeterL);
  resMeterL.setRange(0.0f, 15.0f);
  addAndMakeVisible(resMeterL);

  auto setupButton = [this](juce::ToggleButton &btn) {
    btn.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    btn.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    btn.onClick = [this]() { updateFiltersFromUI(); };
    addAndMakeVisible(btn);
  };
  setupButton(leftLPButton);
  setupButton(leftBPButton);
  setupButton(leftHPButton);
  setupButton(leftFilterEnableButton);
  leftLPButton.setButtonText("LP");
  leftLPButton.setToggleState(true, juce::dontSendNotification);
  leftFilterEnableButton.setToggleState(true, juce::dontSendNotification);
  leftFilterEnableButton.setTooltip("Enable filter routing for all voices");
  leftLPButton.setTooltip("Low-pass filter - cuts high frequencies");
  leftBPButton.setTooltip("Band-pass filter - cuts lows and highs");
  leftHPButton.setTooltip("High-pass filter - cuts low frequencies");

  processor.getLeftSID().setFilterVoices(true, true, true);
  processor.getLeftSID().setFilterMode(true, false, false);
  processor.getLeftSID().setFilterCutoff(1024);

  // Detune slider
  leftDetuneLabel.setText("Detune", juce::dontSendNotification);
  leftDetuneLabel.setColour(juce::Label::textColourId,
                            juce::Colours::lightgrey);
  leftDetuneLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(leftDetuneLabel);

  leftDetuneSlider.setRange(-50.0, 50.0, 1.0);
  leftDetuneSlider.setValue(0.0);
  leftDetuneSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  leftDetuneSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 18);
  leftDetuneSlider.setTooltip("Detune: -50 to +50 cents");
  leftDetuneSlider.onValueChange = [this]() {
    processor.setLeftDetune(static_cast<float>(leftDetuneSlider.getValue()));
  };
  addAndMakeVisible(leftDetuneSlider);

  // Pan slider
  leftPanLabel.setText("Pan", juce::dontSendNotification);
  leftPanLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  leftPanLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(leftPanLabel);

  leftPanSlider.setRange(-1.0, 1.0, 0.01);
  leftPanSlider.setValue(-1.0);
  leftPanSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  leftPanSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 18);
  leftPanSlider.setTooltip("Left SID Pan: -1 (left) to +1 (right)");
  addAndMakeVisible(leftPanSlider);
}

void BreadbinEditor::setupRightSID() {
  rightSIDLabel.setText("RIGHT SID", juce::dontSendNotification);
  rightSIDLabel.setFont(retroFont.withHeight(10.0f));
  rightSIDLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
  rightSIDLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(rightSIDLabel);

  rightChipSelector.addItem("MOS 6581", 1);
  rightChipSelector.addItem("MOS 8580", 2);
  rightChipSelector.setSelectedId(1);
  rightChipSelector.onChange = [this]() {
    processor.setRightChipModel(rightChipSelector.getSelectedId() == 1
                                    ? SIDEngine::ChipModel::MOS6581
                                    : SIDEngine::ChipModel::MOS8580);
  };
  rightChipSelector.setTooltip(
      "6581: Classic warm sound, 8580: Cleaner with better filters");
  addAndMakeVisible(rightChipSelector);

  // Voice buttons and enables for R SID (voices 3-5)
  for (int i = 0; i < 3; ++i) {
    rightVoiceButtons[i].setButtonText(juce::String(i + 4));
    rightVoiceButtons[i].onClick = [this, i]() { selectVoice(i + 3); };
    rightVoiceButtons[i].setTooltip("Select Voice " + juce::String(i + 4) +
                                    " for editing");
    addAndMakeVisible(rightVoiceButtons[i]);

    rightVoiceEnables[i].setButtonText("");
    rightVoiceEnables[i].setToggleState(true, juce::dontSendNotification);
    rightVoiceEnables[i].onClick = [this, i]() {
      processor.getVoiceSettings(i + 3).enabled =
          rightVoiceEnables[i].getToggleState();
    };
    rightVoiceEnables[i].setTooltip("Enable/disable Voice " +
                                    juce::String(i + 4));
    addAndMakeVisible(rightVoiceEnables[i]);
  }

  // Filter
  rightCutoffLabel.setText("Cutoff", juce::dontSendNotification);
  rightCutoffLabel.setColour(juce::Label::textColourId,
                             juce::Colours::lightgrey);
  rightCutoffLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(rightCutoffLabel);

  rightCutoffSlider.setRange(0, 2047, 1);
  rightCutoffSlider.setValue(1024);
  rightCutoffSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  rightCutoffSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  rightCutoffSlider.setTooltip("Filter Cutoff Frequency (0-2047)");
  rightCutoffSlider.onValueChange = [this]() {
    int val = static_cast<int>(rightCutoffSlider.getValue());
    processor.setBaseFilterCutoff(false, val);
    processor.getRightSID().setFilterCutoff(val);
  };
  addAndMakeVisible(rightCutoffSlider);

  rightResonanceLabel.setText("Res", juce::dontSendNotification);
  rightResonanceLabel.setColour(juce::Label::textColourId,
                                juce::Colours::lightgrey);
  rightResonanceLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(rightResonanceLabel);

  rightResonanceSlider.setRange(0, 15, 1);
  rightResonanceSlider.setValue(0);
  rightResonanceSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  rightResonanceSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  rightResonanceSlider.setTooltip("Filter Resonance (0-15)");
  rightResonanceSlider.onValueChange = [this]() {
    int val = static_cast<int>(rightResonanceSlider.getValue());
    processor.setBaseFilterResonance(false, val);
    processor.getRightSID().setFilterResonance(val);
  };
  addAndMakeVisible(rightResonanceSlider);

  // Modulation meters for right SID
  cutoffMeterR.setRange(0.0f, 2047.0f);
  addAndMakeVisible(cutoffMeterR);
  resMeterR.setRange(0.0f, 15.0f);
  addAndMakeVisible(resMeterR);

  auto setupButton = [this](juce::ToggleButton &btn) {
    btn.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    btn.setColour(juce::ToggleButton::tickColourId, juce::Colours::orange);
    btn.onClick = [this]() { updateFiltersFromUI(); };
    addAndMakeVisible(btn);
  };
  setupButton(rightLPButton);
  setupButton(rightBPButton);
  setupButton(rightHPButton);
  setupButton(rightFilterEnableButton);
  rightLPButton.setToggleState(true, juce::dontSendNotification);
  rightFilterEnableButton.setToggleState(true, juce::dontSendNotification);
  rightFilterEnableButton.setTooltip("Enable filter routing for all voices");
  rightLPButton.setTooltip("Low-pass filter - cuts high frequencies");
  rightBPButton.setTooltip("Band-pass filter - cuts lows and highs");
  rightHPButton.setTooltip("High-pass filter - cuts low frequencies");

  processor.getRightSID().setFilterVoices(true, true, true);
  processor.getRightSID().setFilterMode(true, false, false);
  processor.getRightSID().setFilterCutoff(1024);

  // Detune slider
  rightDetuneLabel.setText("Detune", juce::dontSendNotification);
  rightDetuneLabel.setColour(juce::Label::textColourId,
                             juce::Colours::lightgrey);
  rightDetuneLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(rightDetuneLabel);

  rightDetuneSlider.setRange(-50.0, 50.0, 1.0);
  rightDetuneSlider.setValue(0.0);
  rightDetuneSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  rightDetuneSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 18);
  rightDetuneSlider.setTooltip("Detune: -50 to +50 cents");
  rightDetuneSlider.onValueChange = [this]() {
    processor.setRightDetune(static_cast<float>(rightDetuneSlider.getValue()));
  };
  addAndMakeVisible(rightDetuneSlider);

  // Pan slider
  rightPanLabel.setText("Pan", juce::dontSendNotification);
  rightPanLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  rightPanLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(rightPanLabel);

  rightPanSlider.setRange(-1.0, 1.0, 0.01);
  rightPanSlider.setValue(1.0);
  rightPanSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  rightPanSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 18);
  rightPanSlider.setTooltip("Right SID Pan: -1 (left) to +1 (right)");
  addAndMakeVisible(rightPanSlider);
}

void BreadbinEditor::setupVoiceEditor() {
  voiceEditorLabel.setText("VOICE EDITOR", juce::dontSendNotification);
  voiceEditorLabel.setFont(retroFont.withHeight(8.0f));
  voiceEditorLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(voiceEditorLabel);

  waveformLabel.setText("Wave:", juce::dontSendNotification);
  waveformLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  waveformLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(waveformLabel);

  waveformSelector.addItem("Triangle", 1);
  waveformSelector.addItem("Sawtooth", 2);
  waveformSelector.addItem("Pulse", 3);
  waveformSelector.addItem("Noise", 4);
  waveformSelector.setSelectedId(1);
  waveformSelector.setTooltip("Oscillator Waveform");
  waveformSelector.onChange = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(waveformSelector);

  pwLabel.setText("Pulse:", juce::dontSendNotification);
  pwLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(pwLabel);

  pulseWidthSlider.setRange(0, 4095, 1);
  pulseWidthSlider.setValue(2048);
  pulseWidthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  pulseWidthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
  pulseWidthSlider.setTooltip(
      "Pulse Width (0-4095): Controls the square wave duty cycle");
  pulseWidthSlider.onValueChange = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(pulseWidthSlider);

  // Modulation meters for voice editor
  pwMeter.setRange(0.0f, 4095.0f);
  addAndMakeVisible(pwMeter);
  pitchMeter.setRange(-24.0f, 24.0f);
  addAndMakeVisible(pitchMeter);

  auto setupADSR = [this](juce::Slider &slider, juce::Label &label,
                          const juce::String &text, const juce::String &tooltip,
                          int defaultVal) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(juce::Font(juce::FontOptions(10.0f)));
    addAndMakeVisible(label);

    slider.setRange(0, 15, 1);
    slider.setValue(defaultVal);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setTooltip(tooltip);
    slider.onValueChange = [this]() { saveUIToVoice(selectedVoice); };
    addAndMakeVisible(slider);
  };

  setupADSR(attackSlider, attackLabel, "Atk",
            "Attack: Time for volume to reach maximum (0-15)", 0);
  setupADSR(decaySlider, decayLabel, "Dec",
            "Decay: Time to fall to sustain level (0-15)", 0);
  setupADSR(sustainSlider, sustainLabel, "Sus",
            "Sustain: Volume level while key held (0-15)", 15);
  setupADSR(releaseSlider, releaseLabel, "Rel",
            "Release: Time to fade after key release (0-15)", 0);

  // Per-voice pan removed (now per-SID via leftPan/rightPan APVTS)

  // Ring Modulation button
  ringModButton.setTooltip(
      "Ring Modulation: Multiplies triangle wave with previous voice. "
      "Only works with Triangle waveform.");
  ringModButton.onClick = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(ringModButton);

  // Hard Sync button
  syncButton.setTooltip(
      "Hard Sync: Resets oscillator phase when previous voice completes a "
      "cycle. Creates harmonic overtones.");
  syncButton.onClick = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(syncButton);

  // Per-voice filter routing button
  voiceFilterButton.setTooltip("Route this voice through the SID filter.");
  voiceFilterButton.setToggleState(true, juce::dontSendNotification);
  voiceFilterButton.onClick = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(voiceFilterButton);

  // Update Ring Mod enable state when waveform changes
  waveformSelector.onChange = [this]() {
    saveUIToVoice(selectedVoice);
    // Ring mod only works with Triangle waveform
    bool isTriangle = (waveformSelector.getSelectedId() == 1);
    ringModButton.setEnabled(isTriangle);
    if (!isTriangle) {
      ringModButton.setToggleState(false, juce::dontSendNotification);
    }
  };
}

void BreadbinEditor::selectVoice(int voice) {
  if (voice < 0 || voice > 5)
    return;
  if (selectedVoice != voice) {
    saveUIToVoice(selectedVoice);
  }
  selectedVoice = voice;
  updateVoiceButtonStates();
  loadVoiceToUI(voice);
}

void BreadbinEditor::updateVoiceButtonStates() {
  for (int i = 0; i < 3; ++i) {
    leftVoiceButtons[i].setColour(juce::TextButton::buttonColourId,
                                  i == selectedVoice ? juce::Colours::cyan
                                                     : juce::Colours::darkgrey);
    rightVoiceButtons[i].setColour(juce::TextButton::buttonColourId,
                                   (i + 3) == selectedVoice
                                       ? juce::Colours::orange
                                       : juce::Colours::darkgrey);
  }

  // Update voice editor label to show which voice
  juce::String sidName = selectedVoice < 3 ? "L" : "R";

  voiceEditorLabel.setText("VOICE " + juce::String(selectedVoice + 1) + " (" +
                               sidName + ")",
                           juce::dontSendNotification);
}

void BreadbinEditor::loadVoiceToUI(int voice) {
  const auto &settings = processor.getVoiceSettings(voice);

  int waveformId = 1;
  switch (settings.waveform) {
  case SIDEngine::Waveform::Triangle:
    waveformId = 1;
    break;
  case SIDEngine::Waveform::Sawtooth:
    waveformId = 2;
    break;
  case SIDEngine::Waveform::Pulse:
    waveformId = 3;
    break;
  case SIDEngine::Waveform::Noise:
    waveformId = 4;
    break;
  }
  waveformSelector.setSelectedId(waveformId, juce::dontSendNotification);
  pulseWidthSlider.setValue(settings.pulseWidth, juce::dontSendNotification);
  attackSlider.setValue(settings.attack, juce::dontSendNotification);
  decaySlider.setValue(settings.decay, juce::dontSendNotification);
  sustainSlider.setValue(settings.sustain, juce::dontSendNotification);
  releaseSlider.setValue(settings.release, juce::dontSendNotification);
  // per-voice pan removed (now per-SID)
  presetSelector.setSelectedId(settings.presetId, juce::dontSendNotification);

  // Ring mod and sync
  ringModButton.setToggleState(settings.ringMod, juce::dontSendNotification);
  syncButton.setToggleState(settings.sync, juce::dontSendNotification);
  voiceFilterButton.setToggleState(settings.filterEnabled,
                                   juce::dontSendNotification);

  // Ring mod only works with Triangle waveform
  bool isTriangle = (waveformId == 1);
  ringModButton.setEnabled(isTriangle);
}

void BreadbinEditor::saveUIToVoice(int voice) {
  auto &settings = processor.getVoiceSettings(voice);

  switch (waveformSelector.getSelectedId()) {
  case 1:
    settings.waveform = SIDEngine::Waveform::Triangle;
    break;
  case 2:
    settings.waveform = SIDEngine::Waveform::Sawtooth;
    break;
  case 3:
    settings.waveform = SIDEngine::Waveform::Pulse;
    break;
  case 4:
    settings.waveform = SIDEngine::Waveform::Noise;
    break;
  }

  settings.pulseWidth = static_cast<int>(pulseWidthSlider.getValue());
  settings.attack = static_cast<int>(attackSlider.getValue());
  settings.decay = static_cast<int>(decaySlider.getValue());
  settings.sustain = static_cast<int>(sustainSlider.getValue());
  settings.release = static_cast<int>(releaseSlider.getValue());
  // per-voice pan removed (now per-SID)

  // Ring mod and sync
  settings.ringMod = ringModButton.getToggleState();
  settings.sync = syncButton.getToggleState();
  settings.filterEnabled = voiceFilterButton.getToggleState();

  processor.applyVoiceSettings(voice);
}

void BreadbinEditor::updateFiltersFromUI() {
  // Master filter enable (SID panel) combined with per-voice routing
  bool leftMaster = leftFilterEnableButton.getToggleState();
  processor.getLeftSID().setFilterVoices(
      leftMaster && processor.getVoiceSettings(0).filterEnabled,
      leftMaster && processor.getVoiceSettings(1).filterEnabled,
      leftMaster && processor.getVoiceSettings(2).filterEnabled);
  processor.getLeftSID().setFilterMode(leftLPButton.getToggleState(),
                                       leftBPButton.getToggleState(),
                                       leftHPButton.getToggleState());

  bool rightMaster = rightFilterEnableButton.getToggleState();
  processor.getRightSID().setFilterVoices(
      rightMaster && processor.getVoiceSettings(3).filterEnabled,
      rightMaster && processor.getVoiceSettings(4).filterEnabled,
      rightMaster && processor.getVoiceSettings(5).filterEnabled);
  processor.getRightSID().setFilterMode(rightLPButton.getToggleState(),
                                        rightBPButton.getToggleState(),
                                        rightHPButton.getToggleState());
}

void BreadbinEditor::paint(juce::Graphics &g) {
  if (backgroundImage.isValid()) {
    g.drawImage(backgroundImage, getLocalBounds().toFloat(),
                juce::RectanglePlacement::stretchToFit);
    g.setColour(juce::Colour(0, 0, 0).withAlpha(0.65f));
    g.fillRect(getLocalBounds());
  } else {
    g.fillAll(juce::Colour(30, 30, 35));
  }
}

void BreadbinEditor::resized() {
  midiLearnOverlay.setBounds(getLocalBounds());
  auto bounds = getLocalBounds().reduced(8);
  const int rowH = 28;
  const int pad = 4;

  // ===== TOP ROW: Mode, Patch (Global), Ext In =====
  auto topRow = bounds.removeFromTop(rowH);
  modeLabel.setBounds(topRow.removeFromLeft(35));
  dualModeSelector.setBounds(topRow.removeFromLeft(100));
  topRow.removeFromLeft(pad * 2);

  globalPresetLabel.setBounds(topRow.removeFromLeft(40));
  globalPresetSelector.setBounds(topRow.removeFromLeft(100));
  presetDirtyLabel.setBounds(topRow.removeFromLeft(14));
  topRow.removeFromLeft(pad);
  savePatchButton.setBounds(topRow.removeFromLeft(28).reduced(0, 2));
  topRow.removeFromLeft(pad);
  loadPatchButton.setBounds(topRow.removeFromLeft(28).reduced(0, 2));
  topRow.removeFromLeft(pad * 2);

  // Master Volume in header
  masterVolLabel.setBounds(topRow.removeFromLeft(45));
  masterVolSlider.setBounds(topRow.removeFromLeft(90));

  // Ext In on the right of header
  extInputLevelSlider.setBounds(topRow.removeFromRight(80));
  extInputLabel.setBounds(topRow.removeFromRight(35));
  topRow.removeFromRight(pad);
  extInputEnableButton.setBounds(topRow.removeFromRight(55));

  bounds.removeFromTop(pad * 2);

  // ===== SID PANELS: Left and Right side by side =====
  auto sidRow = bounds.removeFromTop(180);
  const int sidWidth = (sidRow.getWidth() - pad * 2) / 2;

  // ----- LEFT SID -----
  auto leftPanel = sidRow.removeFromLeft(sidWidth);
  leftSIDLabel.setBounds(leftPanel.removeFromTop(20));
  auto leftChipRow = leftPanel.removeFromTop(24);
  leftChipSelector.setBounds(leftChipRow.removeFromLeft(100));

  // Voice buttons with enable checkboxes
  auto leftVoicesRow = leftPanel.removeFromTop(30);
  for (int i = 0; i < 3; ++i) {
    leftVoiceEnables[i].setBounds(leftVoicesRow.removeFromLeft(20));
    leftVoiceButtons[i].setBounds(leftVoicesRow.removeFromLeft(40));
    leftVoicesRow.removeFromLeft(pad);
  }

  // Filter
  leftPanel.removeFromTop(pad);
  auto leftFilterRow = leftPanel.removeFromTop(50);
  leftCutoffLabel.setBounds(leftFilterRow.removeFromLeft(40));
  leftCutoffSlider.setBounds(leftFilterRow.removeFromLeft(40));
  cutoffMeterL.setBounds(leftFilterRow.removeFromLeft(6));
  leftResonanceLabel.setBounds(leftFilterRow.removeFromLeft(28));
  leftResonanceSlider.setBounds(leftFilterRow.removeFromLeft(40));
  resMeterL.setBounds(leftFilterRow.removeFromLeft(6));

  auto leftModesRow = leftPanel.removeFromTop(22);
  leftFilterEnableButton.setBounds(leftModesRow.removeFromLeft(45));
  leftLPButton.setBounds(leftModesRow.removeFromLeft(45));
  leftBPButton.setBounds(leftModesRow.removeFromLeft(45));
  leftHPButton.setBounds(leftModesRow.removeFromLeft(45));

  // Detune
  auto leftDetuneRow = leftPanel.removeFromTop(20);
  leftDetuneLabel.setBounds(leftDetuneRow.removeFromLeft(45));
  leftDetuneSlider.setBounds(leftDetuneRow.removeFromLeft(130));

  // Pan
  auto leftPanRow = leftPanel.removeFromTop(20);
  leftPanLabel.setBounds(leftPanRow.removeFromLeft(45));
  leftPanSlider.setBounds(leftPanRow.removeFromLeft(130));

  sidRow.removeFromLeft(pad * 2);

  // ----- RIGHT SID -----
  auto rightPanel = sidRow.removeFromRight(sidWidth);
  rightSIDLabel.setBounds(rightPanel.removeFromTop(20));
  auto rightChipRow = rightPanel.removeFromTop(24);
  rightChipSelector.setBounds(rightChipRow.removeFromRight(100));

  // Voice buttons with enable checkboxes (right-justified)
  auto rightVoicesRow = rightPanel.removeFromTop(30);
  for (int i = 2; i >= 0; --i) {
    rightVoicesRow.removeFromRight(pad);
    rightVoiceButtons[i].setBounds(rightVoicesRow.removeFromRight(40));
    rightVoiceEnables[i].setBounds(rightVoicesRow.removeFromRight(20));
  }

  // Filter (right-justified)
  rightPanel.removeFromTop(pad);
  auto rightFilterRow = rightPanel.removeFromTop(50);
  resMeterR.setBounds(rightFilterRow.removeFromRight(6));
  rightResonanceSlider.setBounds(rightFilterRow.removeFromRight(40));
  rightResonanceLabel.setBounds(rightFilterRow.removeFromRight(28));
  cutoffMeterR.setBounds(rightFilterRow.removeFromRight(6));
  rightCutoffSlider.setBounds(rightFilterRow.removeFromRight(40));
  rightCutoffLabel.setBounds(rightFilterRow.removeFromRight(40));

  auto rightModesRow = rightPanel.removeFromTop(22);
  rightHPButton.setBounds(rightModesRow.removeFromRight(45));
  rightBPButton.setBounds(rightModesRow.removeFromRight(45));
  rightLPButton.setBounds(rightModesRow.removeFromRight(45));
  rightFilterEnableButton.setBounds(rightModesRow.removeFromRight(45));

  // Detune (right-justified)
  auto rightDetuneRow = rightPanel.removeFromTop(20);
  rightDetuneSlider.setBounds(rightDetuneRow.removeFromRight(130));
  rightDetuneLabel.setBounds(rightDetuneRow.removeFromRight(45));

  // Pan (right-justified)
  auto rightPanRow = rightPanel.removeFromTop(20);
  rightPanSlider.setBounds(rightPanRow.removeFromRight(130));
  rightPanLabel.setBounds(rightPanRow.removeFromRight(45));

  bounds.removeFromTop(pad * 2);

  // ===== VOICE EDITOR =====
  auto editorArea = bounds.removeFromTop(120);
  voiceEditorLabel.setBounds(editorArea.removeFromTop(18));

  // Row 1: Voice Select, Save/Load Voice, Waveform, Pulse Width, ADSR, GLOBAL
  // LFO
  auto row1 = editorArea.removeFromTop(65);

  // Voice Presets selector and Save/Load moved here
  presetLabel.setBounds(row1.removeFromLeft(40));
  presetSelector.setBounds(
      row1.removeFromLeft(100).withHeight(20).translated(0, 22));
  row1.removeFromLeft(pad);
  saveVoiceButton.setBounds(row1.removeFromLeft(24).reduced(0, 20));
  row1.removeFromLeft(pad);
  loadVoiceButton.setBounds(row1.removeFromLeft(24).reduced(0, 20));
  row1.removeFromLeft(pad * 2);

  waveformLabel.setBounds(row1.removeFromLeft(40));
  waveformSelector.setBounds(
      row1.removeFromLeft(90).withHeight(20).translated(0, 22));
  row1.removeFromLeft(pad);
  pwLabel.setBounds(row1.removeFromLeft(30));
  pulseWidthSlider.setBounds(row1.removeFromLeft(82));
  pwMeter.setBounds(row1.removeFromLeft(6));
  row1.removeFromLeft(pad);
  pitchMeter.setBounds(row1.removeFromLeft(6));
  row1.removeFromLeft(pad);

  // ADSR sliders
  const int adsrW = 35;
  const int adsrH = 50;
  auto adsrArea = row1.removeFromLeft(adsrW * 4);
  int adsrY = adsrArea.getY();
  attackLabel.setBounds(adsrArea.getX(), adsrY, adsrW, 14);
  attackSlider.setBounds(adsrArea.getX(), adsrY + 14, adsrW, adsrH);
  decayLabel.setBounds(adsrArea.getX() + adsrW, adsrY, adsrW, 14);
  decaySlider.setBounds(adsrArea.getX() + adsrW, adsrY + 14, adsrW, adsrH);
  sustainLabel.setBounds(adsrArea.getX() + adsrW * 2, adsrY, adsrW, 14);
  sustainSlider.setBounds(adsrArea.getX() + adsrW * 2, adsrY + 14, adsrW,
                          adsrH);
  releaseLabel.setBounds(adsrArea.getX() + adsrW * 3, adsrY, adsrW, 14);
  releaseSlider.setBounds(adsrArea.getX() + adsrW * 3, adsrY + 14, adsrW,
                          adsrH);

  row1.removeFromLeft(pad * 2);

  // LFO moved to global stack below

  // Row 2: Glide, Ring Mod, Sync (per-voice pan removed)
  editorArea.removeFromTop(pad);
  auto row2 = editorArea.removeFromTop(28);
  glideTimeLabel.setBounds(row2.removeFromLeft(35));
  glideTimeSlider.setBounds(row2.removeFromLeft(120));
  row2.removeFromLeft(pad * 3);
  ringModButton.setBounds(row2.removeFromLeft(55));
  row2.removeFromLeft(pad);
  syncButton.setBounds(row2.removeFromLeft(55));
  row2.removeFromLeft(pad);
  voiceFilterButton.setBounds(row2.removeFromLeft(45));
  row2.removeFromLeft(pad * 2);
  row2.removeFromLeft(pad * 2);
  // Ext input removed from here (now in header)

  bounds.removeFromTop(pad);

  // ===== KEYBOARD =====
  keyboard.setBounds(bounds.removeFromBottom(60));
  bounds.removeFromBottom(pad);

  // ===== BOTTOM GLOBAL CONTROLS (Right-justified Stack) =====
  const int globalRowW = 500;
  const int globalRowH = 30; // Increased height

  // 1. Bottom Row: Chip Age & Clock (Right-justified)
  auto bottomRow = bounds.removeFromBottom(globalRowH);
  auto bottomStack = bottomRow.removeFromRight(globalRowW);

  clockModeSelector.setBounds(bottomStack.removeFromRight(65));
  clockModeLabel.setBounds(bottomStack.removeFromRight(40));
  bottomStack.removeFromRight(pad * 2);

  agingEndLabel.setBounds(bottomStack.removeFromRight(50));
  agingSlider.setBounds(bottomStack.removeFromRight(120));
  agingStartLabel.setBounds(bottomStack.removeFromRight(40));
  agingLabel.setBounds(bottomStack.removeFromRight(60));

  bounds.removeFromBottom(8); // Increased pad

  // 2. Middle Row: Arpeggiator (Right-justified, Toggle on Left)
  auto middleRow = bounds.removeFromBottom(globalRowH);
  auto arpStack = middleRow.removeFromRight(408); // Exact width for content

  arpEnableButton.setBounds(
      arpStack.removeFromLeft(50).withSize(50, 20).translated(0, 5));
  arpStack.removeFromLeft(15); // Clear gap

  arpPatternLabel.setText("Arp", juce::dontSendNotification);
  arpPatternLabel.setBounds(
      arpStack.removeFromLeft(30).withSize(30, 20).translated(0, 5));
  arpPatternSelector.setBounds(
      arpStack.removeFromLeft(90).withSize(90, 20).translated(0, 5));
  arpStack.removeFromLeft(pad);

  arpRateLabel.setBounds(
      arpStack.removeFromLeft(35).withSize(35, 20).translated(0, 5));
  arpRateSlider.setBounds(
      arpStack.removeFromLeft(100).withSize(100, 20).translated(0, 5));
  arpStack.removeFromLeft(pad);

  arpOctaveLabel.setText("Oct", juce::dontSendNotification);
  arpOctaveLabel.setBounds(
      arpStack.removeFromLeft(30).withSize(30, 20).translated(0, 5));
  arpOctaveSelector.setBounds(
      arpStack.removeFromLeft(50).withSize(50, 20).translated(0, 5));

  bounds.removeFromBottom(10); // Separation from LFO

  // 3. FX: Chorus + Delay (Left-justified, two rows)
  auto fxArea = bounds.removeFromBottom(50);
  auto chorusRow = fxArea.removeFromTop(25);
  auto delayRow = fxArea;

  // Chorus row
  auto chorusStack = chorusRow.removeFromLeft(420);
  chorusEnableButton.setBounds(
      chorusStack.removeFromLeft(60).withSize(60, 20).translated(0, 2));
  chorusStack.removeFromLeft(6);
  chorusRateLabel.setBounds(chorusStack.removeFromLeft(30).withHeight(12));
  chorusRateSlider.setBounds(
      chorusStack.removeFromLeft(90).withHeight(18).translated(0, 3));
  chorusStack.removeFromLeft(4);
  chorusDepthLabel.setBounds(chorusStack.removeFromLeft(35).withHeight(12));
  chorusDepthSlider.setBounds(
      chorusStack.removeFromLeft(80).withHeight(18).translated(0, 3));
  chorusStack.removeFromLeft(4);
  chorusMixLabel.setBounds(chorusStack.removeFromLeft(25).withHeight(12));
  chorusMixSlider.setBounds(
      chorusStack.removeFromLeft(80).withHeight(18).translated(0, 3));

  // Delay row
  auto delayStack = delayRow.removeFromLeft(420);
  delayEnableButton.setBounds(
      delayStack.removeFromLeft(60).withSize(60, 20).translated(0, 2));
  delayStack.removeFromLeft(6);
  delayTimeLLabel.setBounds(delayStack.removeFromLeft(30).withHeight(12));
  delayTimeLSlider.setBounds(
      delayStack.removeFromLeft(65).withHeight(18).translated(0, 3));
  delayStack.removeFromLeft(4);
  delayTimeRLabel.setBounds(delayStack.removeFromLeft(30).withHeight(12));
  delayTimeRSlider.setBounds(
      delayStack.removeFromLeft(65).withHeight(18).translated(0, 3));
  delayStack.removeFromLeft(4);
  delayFBLabel.setBounds(delayStack.removeFromLeft(20).withHeight(12));
  delayFeedbackSlider.setBounds(
      delayStack.removeFromLeft(50).withHeight(18).translated(0, 3));
  delayStack.removeFromLeft(4);
  delayMixLabel.setBounds(delayStack.removeFromLeft(25).withHeight(12));
  delayMixSlider.setBounds(
      delayStack.removeFromLeft(50).withHeight(18).translated(0, 3));

  bounds.removeFromBottom(4);

  // 3b. Popup buttons row (Wavetable, Chord, Modulation)
  auto wtRow = bounds.removeFromBottom(25);
  wavetableButton.setBounds(
      wtRow.removeFromLeft(80).withSize(80, 20).translated(0, 2));

  // Chord Memory button (right side)
  chordMemoryButton.setBounds(
      wtRow.removeFromRight(70).withSize(70, 20).translated(0, 2));
  wtRow.removeFromRight(4); // spacing

  // Modulation button (right side)
  modMatrixButton.setBounds(
      wtRow.removeFromRight(90).withSize(90, 20).translated(0, 2));

  bounds.removeFromBottom(4);

  // 4. Filter Envelope (Left-justified)
  auto filterEnvRow = bounds.removeFromBottom(60);
  auto filterEnvStack = filterEnvRow.removeFromLeft(310);

  filterEnvEnableButton.setBounds(
      filterEnvStack.removeFromLeft(60).withSize(60, 20).translated(0, 8));
  filterEnvStack.removeFromLeft(8);

  // ADSR mini-sliders (vertical)
  const int feW = 30;
  auto feAArea = filterEnvStack.removeFromLeft(feW);
  filterEnvAttackLabel.setBounds(feAArea.getX(), filterEnvRow.getY() + 2, feW, 12);
  filterEnvAttackSlider.setBounds(feAArea.getX(), filterEnvRow.getY() + 14, feW, 44);

  filterEnvStack.removeFromLeft(2);
  auto feDArea = filterEnvStack.removeFromLeft(feW);
  filterEnvDecayLabel.setBounds(feDArea.getX(), filterEnvRow.getY() + 2, feW, 12);
  filterEnvDecaySlider.setBounds(feDArea.getX(), filterEnvRow.getY() + 14, feW, 44);

  filterEnvStack.removeFromLeft(2);
  auto feSArea = filterEnvStack.removeFromLeft(feW);
  filterEnvSustainLabel.setBounds(feSArea.getX(), filterEnvRow.getY() + 2, feW, 12);
  filterEnvSustainSlider.setBounds(feSArea.getX(), filterEnvRow.getY() + 14, feW, 44);

  filterEnvStack.removeFromLeft(2);
  auto feRArea = filterEnvStack.removeFromLeft(feW);
  filterEnvReleaseLabel.setBounds(feRArea.getX(), filterEnvRow.getY() + 2, feW, 12);
  filterEnvReleaseSlider.setBounds(feRArea.getX(), filterEnvRow.getY() + 14, feW, 44);

  filterEnvStack.removeFromLeft(8);
  auto feAmtArea = filterEnvStack.removeFromLeft(50);
  filterEnvAmountLabel.setBounds(feAmtArea.getX(), filterEnvRow.getY() + 2, 50, 12);
  filterEnvAmountSlider.setBounds(feAmtArea.getX(), filterEnvRow.getY() + 14, 50, 44);

  // LFO1/LFO2 moved to Modulation popup (ModMatrixPanel)
}

void BreadbinEditor::applyPreset(int presetId) {
  // Convert SIDEngine::Waveform to APVTS choice index
  auto waveToIndex = [](SIDEngine::Waveform w) -> float {
    switch (w) {
    case SIDEngine::Waveform::Triangle: return 0.0f;
    case SIDEngine::Waveform::Sawtooth: return 1.0f;
    case SIDEngine::Waveform::Pulse:    return 2.0f;
    case SIDEngine::Waveform::Noise:    return 3.0f;
    default: return 0.0f;
    }
  };

  auto configureVoice = [this, presetId, &waveToIndex](
                             int voice, SIDEngine::Waveform wave, int pw,
                             int a, int d, int s, int r) {
    juce::String prefix = "v" + juce::String(voice) + "_";
    auto setParam = [&](const juce::String &id, float val) {
      auto *p = processor.apvts.getParameter(id);
      p->setValueNotifyingHost(p->convertTo0to1(val));
    };
    setParam(prefix + "waveform", waveToIndex(wave));
    setParam(prefix + "pw", static_cast<float>(pw));
    setParam(prefix + "attack", static_cast<float>(a));
    setParam(prefix + "decay", static_cast<float>(d));
    setParam(prefix + "sustain", static_cast<float>(s));
    setParam(prefix + "release", static_cast<float>(r));
    processor.getVoiceSettings(voice).presetId = presetId;
    processor.applyVoiceSettings(voice);
  };

  // Apply preset to selected voice only

  switch (presetId) {
  case 2: // Classic Lead (Monty) - Pulse with medium PWM
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4);
    break;
  case 3: // Fat Bass (Ocean) - Sawtooth with slow attack
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12,
                   3);
    break;
  case 4: // PWM Pad (Hubbard) - Pulse with slow attack/release
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 1024, 8, 6, 12,
                   8);
    break;
  case 5: // Noise Snare - Noise with fast decay
    configureVoice(selectedVoice, SIDEngine::Waveform::Noise, 0, 0, 8, 0, 4);
    break;
  case 6: // Retro Triangle - Soft triangle with slow attack
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 2, 4, 10, 6);
    break;
  }

  loadVoiceToUI(selectedVoice);
}

void BreadbinEditor::applyGlobalPreset(int presetId) {
  // APVTS helper: set any parameter by ID and denormalized value
  auto setParam = [this](const juce::String &id, float val) {
    auto *p = processor.apvts.getParameter(id);
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(val));
  };

  // Convert SIDEngine::Waveform to APVTS choice index
  auto waveToIndex = [](SIDEngine::Waveform w) -> float {
    switch (w) {
    case SIDEngine::Waveform::Triangle: return 0.0f;
    case SIDEngine::Waveform::Sawtooth: return 1.0f;
    case SIDEngine::Waveform::Pulse:    return 2.0f;
    case SIDEngine::Waveform::Noise:    return 3.0f;
    default: return 0.0f;
    }
  };

  // ---- FULL STATE RESET ----
  // Every global preset defines the complete plugin state.
  // Reset all params to factory defaults first, then each preset overrides.

  // Per-voice defaults
  for (int v = 0; v < 6; ++v) {
    juce::String vp = "v" + juce::String(v) + "_";
    setParam(vp + "enable", 1.0f);
    setParam(vp + "waveform", 2.0f);   // Pulse
    setParam(vp + "pw", 2048.0f);
    setParam(vp + "attack", 0.0f);
    setParam(vp + "decay", 0.0f);
    setParam(vp + "sustain", 15.0f);
    setParam(vp + "release", 0.0f);
    setParam(vp + "ringMod", 0.0f);
    setParam(vp + "sync", 0.0f);
    setParam(vp + "filter", 1.0f);
    processor.getVoiceSettings(v).presetId = 1; // "-- Select --"
  }

  // Dual mode: StereoSplit (0)
  setParam("dualMode", 0.0f);

  // Detune: centered
  setParam("leftDetune", 0.0f);
  setParam("rightDetune", 0.0f);

  // Glide: off
  setParam("glide", 0.0f);

  // Clock: PAL
  setParam("clockMode", 0.0f);

  // Pan: hard left/right
  setParam("leftPan", -1.0f);
  setParam("rightPan", 1.0f);

  // Filter cutoff/resonance (non-APVTS, set on SID directly)
  processor.getLeftSID().setFilterCutoff(1024);
  processor.getLeftSID().setFilterResonance(0);
  processor.getRightSID().setFilterCutoff(1024);
  processor.getRightSID().setFilterResonance(0);
  processor.setBaseFilterCutoff(true, 1024);
  processor.setBaseFilterCutoff(false, 1024);
  processor.setBaseFilterResonance(true, 0);
  processor.setBaseFilterResonance(false, 0);
  leftCutoffSlider.setValue(1024.0, juce::dontSendNotification);
  leftResonanceSlider.setValue(0.0, juce::dontSendNotification);
  rightCutoffSlider.setValue(1024.0, juce::dontSendNotification);
  rightResonanceSlider.setValue(0.0, juce::dontSendNotification);

  // Arpeggiator: off, defaults
  setParam("arpEnable", 0.0f);
  setParam("arpPattern", 0.0f);   // Up
  setParam("arpRate", 50.0f);
  setParam("arpOctaves", 1.0f);

  // LFO1: off, defaults
  setParam("lfoEnable", 0.0f);
  setParam("lfoWave", 0.0f);      // Triangle
  setParam("lfoRate", 1.0f);
  setParam("lfoDepthFilt", 0.0f);
  setParam("lfoDepthPW", 0.0f);
  setParam("lfoDepthPitch", 0.0f);

  // LFO2: off, defaults
  setParam("lfo2Enable", 0.0f);
  setParam("lfo2Wave", 0.0f);     // Triangle
  setParam("lfo2Rate", 3.0f);
  setParam("lfo2DepthFilt", 0.0f);
  setParam("lfo2DepthPW", 0.0f);
  setParam("lfo2DepthPitch", 0.0f);

  // Filter Envelope: off, defaults
  setParam("filterEnvEnable", 0.0f);
  setParam("filterEnvAttack", 0.01f);
  setParam("filterEnvDecay", 0.3f);
  setParam("filterEnvSustain", 0.5f);
  setParam("filterEnvRelease", 0.5f);
  setParam("filterEnvAmount", 0.5f);

  // Chorus: off, defaults
  setParam("chorusEnable", 0.0f);
  setParam("chorusRate", 1.5f);
  setParam("chorusDepth", 0.3f);
  setParam("chorusMix", 0.5f);

  // Delay: off, defaults
  setParam("delayEnable", 0.0f);
  setParam("delayTimeL", 375.0f);
  setParam("delayTimeR", 500.0f);
  setParam("delayFeedback", 0.3f);
  setParam("delayMix", 0.3f);

  // Wavetable: off, defaults
  setParam("wtEnable", 0.0f);
  setParam("wtNumSteps", 4.0f);
  setParam("wtRate", 50.0f);
  setParam("wtLoop", 1.0f);
  for (int i = 0; i < 16; ++i) {
    auto wp = "wt_s" + juce::String(i) + "_";
    setParam(wp + "wave", 2.0f);   // Pulse
    setParam(wp + "pitch", 0.0f);
    setParam(wp + "pw", 2048.0f);
  }

  // Mod Matrix: all slots cleared
  for (int i = 0; i < 4; ++i) {
    auto mp = "mod" + juce::String(i) + "_";
    setParam(mp + "src", 0.0f);   // None
    setParam(mp + "dst", 0.0f);   // None
    setParam(mp + "amt", 0.0f);
  }

  // Pitch Bend Range: reset to default
  setParam("pitchBendRange", 2.0f);

  // PWM Sweep: reset to default
  setParam("pwmSweepEnable", 0.0f);
  setParam("pwmSweepRate", 0.5f);
  setParam("pwmSweepDepth", 0.0f);

  // Chord Memory: off, all intervals = 0
  setParam("chordEnable", 0.0f);
  setParam("chordSlot", 0.0f);
  for (int s = 0; s < 4; ++s)
    for (int i = 0; i < 5; ++i)
      setParam("chord_s" + juce::String(s) + "_i" + juce::String(i), 0.0f);

  // Note: masterVol, chipLeft/Right, aging, extInput left unchanged (user preference)

  // ---- VOICE CONFIGURATION HELPER ----
  // Sets voice waveform/ADSR via APVTS, assigns voice preset ID, syncs engine
  auto configVoice = [this, &setParam, &waveToIndex](
                         int v, SIDEngine::Waveform wave, int pw,
                         int a, int d, int s, int r, int vpId = 1) {
    juce::String vp = "v" + juce::String(v) + "_";
    setParam(vp + "enable", 1.0f);
    setParam(vp + "waveform", waveToIndex(wave));
    setParam(vp + "pw", static_cast<float>(pw));
    setParam(vp + "attack", static_cast<float>(a));
    setParam(vp + "decay", static_cast<float>(d));
    setParam(vp + "sustain", static_cast<float>(s));
    setParam(vp + "release", static_cast<float>(r));
    processor.getVoiceSettings(v).presetId = vpId;
    processor.applyVoiceSettings(v);
  };

  // Helper: set filter cutoff/resonance on both SIDs + base values + sliders
  auto setFilters = [this](int cutoff, int resonance) {
    processor.getLeftSID().setFilterCutoff(cutoff);
    processor.getLeftSID().setFilterResonance(resonance);
    processor.getRightSID().setFilterCutoff(cutoff);
    processor.getRightSID().setFilterResonance(resonance);
    processor.setBaseFilterCutoff(true, cutoff);
    processor.setBaseFilterCutoff(false, cutoff);
    processor.setBaseFilterResonance(true, resonance);
    processor.setBaseFilterResonance(false, resonance);
    leftCutoffSlider.setValue(static_cast<double>(cutoff), juce::dontSendNotification);
    leftResonanceSlider.setValue(static_cast<double>(resonance), juce::dontSendNotification);
    rightCutoffSlider.setValue(static_cast<double>(cutoff), juce::dontSendNotification);
    rightResonanceSlider.setValue(static_cast<double>(resonance), juce::dontSendNotification);
  };

  // ---- PER-PRESET OVERRIDES ----
  // Only set what differs from the defaults above.

  switch (presetId) {
  case 1: // Dual Lead - Fat Bass saw + Classic Lead pulse, detuned
    configVoice(0, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    configVoice(1, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(2, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(3, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    configVoice(4, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(5, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    setParam("leftDetune", -5.0f);
    setParam("rightDetune", 5.0f);
    setFilters(1800, 4);
    // Light chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.8f);
    setParam("chorusDepth", 0.15f);
    setParam("chorusMix", 0.25f);
    // Gentle LFO1 on filter for movement
    setParam("lfoEnable", 1.0f);
    setParam("lfoRate", 0.4f);
    setParam("lfoDepthFilt", 0.15f);
    break;

  case 2: // Pad Stack - PWM Pad on all voices, lush FX
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 1024, 8, 6, 12, 8, 4);
    setParam("leftDetune", -8.0f);
    setParam("rightDetune", 8.0f);
    setFilters(800, 6);
    // Chorus for lushness
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.2f);
    setParam("chorusDepth", 0.35f);
    setParam("chorusMix", 0.4f);
    // Slow PWM sweep for evolving texture
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.3f);
    setParam("pwmSweepDepth", 0.4f);
    // Filter envelope: slow swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 2.0f);
    setParam("filterEnvDecay", 1.5f);
    setParam("filterEnvSustain", 0.7f);
    setParam("filterEnvRelease", 3.0f);
    setParam("filterEnvAmount", 0.4f);
    break;

  case 3: // Arpeggiated - Classic Lead with arp + echo
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    setParam("arpEnable", 1.0f);
    setParam("arpRate", 8.0f);
    setParam("arpOctaves", 2.0f);
    // Stereo delay for rhythmic echo
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 250.0f);
    setParam("delayTimeR", 375.0f);
    setParam("delayFeedback", 0.35f);
    setParam("delayMix", 0.3f);
    // Filter envelope for plucky attack
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.15f);
    setParam("filterEnvSustain", 0.2f);
    setParam("filterEnvRelease", 0.3f);
    setParam("filterEnvAmount", 0.6f);
    setFilters(600, 4);
    break;

  case 4: // Fat Unison - Fat Bass, PWM sweep + mod matrix
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    setParam("dualMode", 1.0f); // Unison
    setParam("leftDetune", -12.0f);
    setParam("rightDetune", 12.0f);
    setFilters(1200, 3);
    // PWM sweep for subtle movement
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.6f);
    setParam("pwmSweepDepth", 0.25f);
    // Mod matrix: LFO1 -> Resonance for growl
    setParam("lfoEnable", 1.0f);
    setParam("lfoRate", 0.3f);
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src) s0src->setValueNotifyingHost(s0src->convertTo0to1(1.0f)); // LFO1
      if (s0dst) s0dst->setValueNotifyingHost(s0dst->convertTo0to1(4.0f)); // Resonance
      setParam("mod0_amt", 0.3f);
    }
    break;

  case 5: // Retro Synth - Mixed voices, vibrato + chorus
    configVoice(0, SIDEngine::Waveform::Triangle, 0, 2, 4, 10, 6, 6);
    configVoice(1, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(2, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    configVoice(3, SIDEngine::Waveform::Triangle, 0, 2, 4, 10, 6, 6);
    configVoice(4, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(5, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    // LFO2 for subtle pitch vibrato
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Rate", 5.0f);
    setParam("lfo2DepthPitch", 0.08f);
    // Light chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.3f);
    setParam("pitchBendRange", 7.0f);
    break;

  case 6: { // Chord Stab - Chord memory + delay + filter env stab
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 1800, 0, 3, 10, 2, 2);
    setFilters(1400, 5);
    // Chord memory: major 7th voicing (+4, +7, +11)
    setParam("chordEnable", 1.0f);
    setParam("chordSlot", 0.0f);
    setParam("chord_s0_i0", 4.0f);   // major 3rd
    setParam("chord_s0_i1", 7.0f);   // perfect 5th
    // Slot 1: minor chord (-3, -7)
    setParam("chord_s1_i0", 3.0f);   // minor 3rd
    setParam("chord_s1_i1", 7.0f);   // perfect 5th
    // Slot 2: sus4 (+5, +7)
    setParam("chord_s2_i0", 5.0f);   // perfect 4th
    setParam("chord_s2_i1", 7.0f);   // perfect 5th
    // Slot 3: power chord (+7, +12)
    setParam("chord_s3_i0", 7.0f);   // perfect 5th
    setParam("chord_s3_i1", 12.0f);  // octave
    // Punchy filter envelope
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.12f);
    setParam("filterEnvSustain", 0.1f);
    setParam("filterEnvRelease", 0.2f);
    setParam("filterEnvAmount", 0.7f);
    // Stereo delay for space
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 330.0f);
    setParam("delayTimeR", 500.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.35f);
    // Chorus for width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.5f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    break;
  }

  case 7: { // Mod Madness - Everything cranked: both LFOs, mod matrix, WT, FX
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 6, 10, 6, 2);
    setParam("leftDetune", -10.0f);
    setParam("rightDetune", 10.0f);
    setFilters(1000, 6);
    // LFO1: S&H on filter for glitchy movement
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 3.0f);       // S&H
    setParam("lfoRate", 6.0f);
    setParam("lfoDepthFilt", 0.5f);
    // LFO2: Triangle on pitch for wobble
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 0.0f);      // Triangle
    setParam("lfo2Rate", 1.5f);
    setParam("lfo2DepthPitch", 0.15f);
    // PWM sweep: fast and deep
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 2.5f);
    setParam("pwmSweepDepth", 0.7f);
    // Filter envelope: sharp attack, slow release
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.5f);
    setParam("filterEnvSustain", 0.3f);
    setParam("filterEnvRelease", 2.0f);
    setParam("filterEnvAmount", 0.6f);
    // Mod matrix: LFO2->PW, FilterEnv->Resonance
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src) s0src->setValueNotifyingHost(s0src->convertTo0to1(2.0f)); // LFO2
      if (s0dst) s0dst->setValueNotifyingHost(s0dst->convertTo0to1(2.0f)); // PW
      setParam("mod0_amt", 0.6f);
      auto *s1src = processor.apvts.getParameter("mod1_src");
      auto *s1dst = processor.apvts.getParameter("mod1_dst");
      if (s1src) s1src->setValueNotifyingHost(s1src->convertTo0to1(3.0f)); // FilterEnv
      if (s1dst) s1dst->setValueNotifyingHost(s1dst->convertTo0to1(4.0f)); // Resonance
      setParam("mod1_amt", 0.5f);
    }
    // Wavetable: 4-step cycling through waveforms
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 4.0f);
    setParam("wtRate", 12.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch, float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f);   // Pulse, root
      setWTStep(1, 1.0f, 0.0f, 2048.0f);   // Saw, root
      setWTStep(2, 2.0f, 12.0f, 1024.0f);  // Pulse, +octave, narrow PW
      setWTStep(3, 0.0f, 7.0f, 2048.0f);   // Triangle, +fifth
    }
    // Both FX
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 2.0f);
    setParam("chorusDepth", 0.4f);
    setParam("chorusMix", 0.35f);
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 200.0f);
    setParam("delayTimeR", 300.0f);
    setParam("delayFeedback", 0.5f);
    setParam("delayMix", 0.3f);
    setParam("pitchBendRange", 12.0f);
    break;
  }

  case 8: { // WT Arpeggio - Classic C64 wavetable arpeggio (Rob Hubbard technique)
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15, 4, 2);
    setFilters(1200, 4);
    // Wavetable: 3-step major chord arpeggio at 50Hz
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 3.0f);
    setParam("wtRate", 50.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch, float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f);   // Pulse, root
      setWTStep(1, 2.0f, 4.0f, 2048.0f);   // Pulse, major 3rd
      setWTStep(2, 2.0f, 7.0f, 2048.0f);   // Pulse, 5th
    }
    // Light chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.25f);
    // Subtle filter envelope pluck
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.2f);
    setParam("filterEnvSustain", 0.3f);
    setParam("filterEnvRelease", 0.4f);
    setParam("filterEnvAmount", 0.5f);
    break;
  }

  case 9: { // WT Morph - Timbral morphing through waveforms and PW
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 2, 4, 12, 6, 2);
    setFilters(1400, 3);
    // Wavetable: 8-step timbral sequence
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 8.0f);
    setParam("wtRate", 8.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch, float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 3500.0f);    // Pulse, thin
      setWTStep(1, 1.0f, 0.0f, 2048.0f);    // Saw
      setWTStep(2, 2.0f, 0.0f, 1024.0f);    // Pulse, narrow
      setWTStep(3, 0.0f, 12.0f, 2048.0f);   // Triangle, octave up
      setWTStep(4, 2.0f, 0.0f, 2800.0f);    // Pulse, medium-thin
      setWTStep(5, 1.0f, -12.0f, 2048.0f);  // Saw, octave down
      setWTStep(6, 3.0f, 0.0f, 2048.0f);    // Noise, percussive hit
      setWTStep(7, 2.0f, 7.0f, 1500.0f);    // Pulse, 5th, medium PW
    }
    // Stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 250.0f);
    setParam("delayTimeR", 375.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.35f);
    // Chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.3f);
    // LFO1: slow filter sweep
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);      // Triangle
    setParam("lfoRate", 0.3f);
    setParam("lfoDepthFilt", 0.3f);
    break;
  }
  }

  // Refresh UI for current voice
  loadVoiceToUI(selectedVoice);
}

void BreadbinEditor::savePresetToFile() {
  // Save all current UI state to selected voice first
  saveUIToVoice(selectedVoice);

  // Get state from processor
  juce::MemoryBlock data;
  processor.getStateInformation(data);

  // Show file dialog
  auto chooser = std::make_unique<juce::FileChooser>(
      "Save Preset",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.breadbin", true);

  auto chooserFlags = juce::FileBrowserComponent::saveMode |
                      juce::FileBrowserComponent::canSelectFiles |
                      juce::FileBrowserComponent::warnAboutOverwriting;

  chooser->launchAsync(chooserFlags, [this, data](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file != juce::File{}) {
      // Ensure correct extension
      if (!file.hasFileExtension(".breadbin"))
        file = file.withFileExtension(".breadbin");

      // Write state as XML for human readability
      auto state =
          juce::ValueTree::readFromData(data.getData(), data.getSize());
      if (state.isValid()) {
        auto xml = state.createXml();
        if (xml != nullptr) {
          xml->writeTo(file);
        }
      }
    }
  });

  // Keep chooser alive
  static std::unique_ptr<juce::FileChooser> savedChooser;
  savedChooser = std::move(chooser);
}

void BreadbinEditor::loadPresetFromFile() {
  auto chooser = std::make_unique<juce::FileChooser>(
      "Load Preset",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.breadbin", true);

  auto chooserFlags = juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles;

  chooser->launchAsync(chooserFlags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file.existsAsFile()) {
      auto xml = juce::XmlDocument::parse(file);
      if (xml != nullptr) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
          juce::MemoryBlock data;
          juce::MemoryOutputStream stream(data, false);
          state.writeToStream(stream);
          processor.setStateInformation(data.getData(),
                                        static_cast<int>(data.getSize()));

          // Refresh UI to show loaded state
          loadVoiceToUI(selectedVoice);
          updateVoiceButtonStates();

          // Update global controls
          dualModeSelector.setSelectedId(
              static_cast<int>(processor.getDualMode()) + 1,
              juce::dontSendNotification);
          agingSlider.setValue(processor.getAgingFactor(),
                               juce::dontSendNotification);
          // pitchBendRange now APVTS-managed, auto-restored
          clockModeSelector.setSelectedId(
              static_cast<int>(processor.getClockMode()) + 1,
              juce::dontSendNotification);
          extInputEnableButton.setToggleState(processor.isExtInputEnabled(),
                                              juce::dontSendNotification);
          extInputLevelSlider.setValue(processor.getExtInputLevel(),
                                       juce::dontSendNotification);

          // LFO controls (lfoEnable, lfoWaveform, lfoRate, depths) are
          // APVTS-attached — setStateInformation auto-syncs them.

          processor.snapshotPresetState();
        }
      }
    }
  });

  static std::unique_ptr<juce::FileChooser> savedChooser;
  savedChooser = std::move(chooser);
}

void BreadbinEditor::saveVoicePresetToFile() {
  saveUIToVoice(selectedVoice);
  auto state = processor.getVoiceState(selectedVoice);

  auto chooser = std::make_unique<juce::FileChooser>(
      "Save Voice Settings",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.voice", true);

  auto chooserFlags = juce::FileBrowserComponent::saveMode |
                      juce::FileBrowserComponent::canSelectFiles |
                      juce::FileBrowserComponent::warnAboutOverwriting;

  chooser->launchAsync(chooserFlags, [state](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file != juce::File{}) {
      if (!file.hasFileExtension(".voice"))
        file = file.withFileExtension(".voice");

      auto xml = state.createXml();
      if (xml != nullptr)
        xml->writeTo(file);
    }
  });

  static std::unique_ptr<juce::FileChooser> savedVoiceChooser;
  savedVoiceChooser = std::move(chooser);
}

void BreadbinEditor::loadVoicePresetFromFile() {
  auto chooser = std::make_unique<juce::FileChooser>(
      "Load Voice Settings",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.voice", true);

  auto chooserFlags = juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles;

  chooser->launchAsync(chooserFlags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file.existsAsFile()) {
      auto xml = juce::XmlDocument::parse(file);
      if (xml != nullptr) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
          processor.setVoiceState(selectedVoice, state);
          loadVoiceToUI(selectedVoice);
        }
      }
    }
  });

  static std::unique_ptr<juce::FileChooser> savedVoiceChooser;
  savedVoiceChooser = std::move(chooser);
}

juce::Path BreadbinEditor::makeDiskPath() {
  return juce::Drawable::parseSVGPath(
      "M3 3h14l4 4v14H3V3zm16 16V7.83L16.17 5H5v14h14zM7 7h7v4H7V7zm0 "
      "10h10v-4H7v4z");
}

juce::Path BreadbinEditor::makeFolderPath() {
  return juce::Drawable::parseSVGPath(
      "M3 3h7l2 2h9v16H3V3zm16 16V7h-8l-2-2H5v14h14z");
}

void BreadbinEditor::refreshVoiceEditorAttachments() {
  // Release existing attachments
  voiceWaveformAttach.reset();
  voicePWAttach.reset();
  voiceAttackAttach.reset();
  voiceDecayAttach.reset();
  voiceSustainAttach.reset();
  voiceReleaseAttach.reset();
  // voicePanAttach removed (per-SID pan now)
  voiceRingModAttach.reset();
  voiceSyncAttach.reset();
  voiceFilterAttach.reset();

  // Re-attach to selected voice
  juce::String prefix = "v" + juce::String(processor.getSelectedVoice()) + "_";

  voiceWaveformAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, prefix + "waveform", waveformSelector);
  voicePWAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "pw", pulseWidthSlider);
  voiceAttackAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "attack", attackSlider);
  voiceDecayAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "decay", decaySlider);
  voiceSustainAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "sustain", sustainSlider);
  voiceReleaseAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "release", releaseSlider);
  // voicePanAttach removed (per-SID pan now)
  voiceRingModAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, prefix + "ringMod", ringModButton);
  voiceSyncAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, prefix + "sync", syncButton);
  voiceFilterAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, prefix + "filter", voiceFilterButton);
}
