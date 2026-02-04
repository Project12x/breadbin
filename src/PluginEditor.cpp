#include "PluginEditor.h"
#include "BinaryData.h"

BreadbinEditor::BreadbinEditor(BreadbinProcessor &p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {
  setupControls();
  setupSynthControls();
  setupFilterControls();

  // Load background image from binary data
  backgroundImage = juce::ImageCache::getFromMemory(
      BinaryData::background_jpg, BinaryData::background_jpgSize);

  // Setup keyboard with listener (inject MIDI to processor)
  keyboardState.addListener(this);
  keyboard.setAvailableRange(36, 84); // C2 to C6
  addAndMakeVisible(keyboard);

  setSize(700, 500);

  // Initialize SID engines with UI control values
  updateSynthFromControls();
}

BreadbinEditor::~BreadbinEditor() { keyboardState.removeListener(this); }

void BreadbinEditor::handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                                  int midiNoteNumber, float velocity) {
  auto msg = juce::MidiMessage::noteOn(midiChannel, midiNoteNumber, velocity);
  msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
  processor.getMidiMessageCollector().addMessageToQueue(msg);
}

void BreadbinEditor::handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                                   int midiNoteNumber, float velocity) {
  juce::ignoreUnused(velocity);
  auto msg = juce::MidiMessage::noteOff(midiChannel, midiNoteNumber);
  msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
  processor.getMidiMessageCollector().addMessageToQueue(msg);
}

void BreadbinEditor::setupControls() {
  // Title label is no longer shown - background image provides branding

  // Dual Mode
  modeLabel.setText("Mode:", juce::dontSendNotification);
  addAndMakeVisible(modeLabel);

  dualModeSelector.addItem("Stereo Split", 1);
  dualModeSelector.addItem("Unison", 2);
  dualModeSelector.addItem("Multitimbral", 3);
  dualModeSelector.setSelectedId(static_cast<int>(processor.getDualMode()) + 1);
  dualModeSelector.onChange = [this]() {
    processor.setDualMode(static_cast<BreadbinProcessor::DualMode>(
        dualModeSelector.getSelectedId() - 1));
  };
  addAndMakeVisible(dualModeSelector);

  // Left Chip Model
  leftChipLabel.setText("L:", juce::dontSendNotification);
  addAndMakeVisible(leftChipLabel);

  leftChipSelector.addItem("6581", 1);
  leftChipSelector.addItem("8580", 2);
  leftChipSelector.setSelectedId(
      static_cast<int>(processor.getLeftChipModel()) + 1);
  leftChipSelector.onChange = [this]() {
    processor.setLeftChipModel(static_cast<SIDEngine::ChipModel>(
        leftChipSelector.getSelectedId() - 1));
  };
  addAndMakeVisible(leftChipSelector);

  // Right Chip Model
  rightChipLabel.setText("R:", juce::dontSendNotification);
  addAndMakeVisible(rightChipLabel);

  rightChipSelector.addItem("6581", 1);
  rightChipSelector.addItem("8580", 2);
  rightChipSelector.setSelectedId(
      static_cast<int>(processor.getRightChipModel()) + 1);
  rightChipSelector.onChange = [this]() {
    processor.setRightChipModel(static_cast<SIDEngine::ChipModel>(
        rightChipSelector.getSelectedId() - 1));
  };
  addAndMakeVisible(rightChipSelector);

  // Presets - Left SID
  leftPresetLabel.setText("L Preset:", juce::dontSendNotification);
  addAndMakeVisible(leftPresetLabel);

  leftPresetSelector.addItem("-- Select --", 1);
  leftPresetSelector.addItem("Classic Lead (Monty)", 2);
  leftPresetSelector.addItem("Fat Bass (Ocean)", 3);
  leftPresetSelector.addItem("PWM Pad (Hubbard)", 4);
  leftPresetSelector.addItem("Noise Snare", 5);
  leftPresetSelector.setSelectedId(1);
  leftPresetSelector.onChange = [this]() {
    applyPreset(leftPresetSelector.getSelectedId());
  };
  addAndMakeVisible(leftPresetSelector);

  // Presets - Right SID
  rightPresetLabel.setText("R Preset:", juce::dontSendNotification);
  addAndMakeVisible(rightPresetLabel);

  rightPresetSelector.addItem("-- Select --", 1);
  rightPresetSelector.addItem("Classic Lead (Monty)", 2);
  rightPresetSelector.addItem("Fat Bass (Ocean)", 3);
  rightPresetSelector.addItem("PWM Pad (Hubbard)", 4);
  rightPresetSelector.addItem("Noise Snare", 5);
  rightPresetSelector.setSelectedId(1);
  rightPresetSelector.onChange = [this]() {
    applyPreset(rightPresetSelector.getSelectedId());
  };
  addAndMakeVisible(rightPresetSelector);

  // Time Machine (aging slider) - 1982 to NOW labels at ends
  agingLabel.setText("Time Machine", juce::dontSendNotification);
  agingLabel.setJustificationType(juce::Justification::centred);
  agingLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(agingLabel);

  // "1982" label at left end
  agingStartLabel.setText("1982", juce::dontSendNotification);
  agingStartLabel.setJustificationType(juce::Justification::centredRight);
  agingStartLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  agingStartLabel.setFont(juce::FontOptions(11.0f));
  addAndMakeVisible(agingStartLabel);

  // "NOW" label at right end
  agingEndLabel.setText("NOW", juce::dontSendNotification);
  agingEndLabel.setJustificationType(juce::Justification::centredLeft);
  agingEndLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  agingEndLabel.setFont(juce::FontOptions(11.0f));
  addAndMakeVisible(agingEndLabel);

  // Simple 0-1 slider with no text box
  agingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  agingSlider.setRange(0.0, 1.0, 0.01);
  agingSlider.setValue(processor.getAgingFactor());
  agingSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  agingSlider.onValueChange = [this]() {
    processor.setAgingFactor(static_cast<float>(agingSlider.getValue()));
  };
  addAndMakeVisible(agingSlider);
}

void BreadbinEditor::setupSynthControls() {
  // Helper to setup label
  auto setupLabel = [this](juce::Label &label, const juce::String &text) {
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(label);
  };

  // Helper to setup slider with styled text box
  auto setupSlider = [this](juce::Slider &slider, double min, double max,
                            double val, int textWidth) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setRange(min, max, 1.0);
    slider.setValue(val);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, textWidth, 16);
    slider.setColour(juce::Slider::textBoxBackgroundColourId,
                     juce::Colour(0x80000000));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour(juce::Slider::textBoxOutlineColourId,
                     juce::Colour(0x40FFFFFF));
    slider.onValueChange = [this]() { updateSynthFromControls(); };
    addAndMakeVisible(slider);
  };

  // Helper to setup waveform selector
  auto setupWaveformSelector = [this](juce::ComboBox &selector) {
    selector.addItem("Triangle", 1);
    selector.addItem("Sawtooth", 2);
    selector.addItem("Pulse", 3);
    selector.addItem("Noise", 4);
    selector.setSelectedId(1);
    selector.onChange = [this]() { updateSynthFromControls(); };
    addAndMakeVisible(selector);
  };

  // Left SID waveform controls
  setupLabel(leftWaveformLabel, "L Wave");
  setupWaveformSelector(leftWaveformSelector);
  setupLabel(leftPWLabel, "L PW");
  setupSlider(leftPulseWidthSlider, 0, 4095, 2048, 45);

  // Right SID waveform controls
  setupLabel(rightWaveformLabel, "R Wave");
  setupWaveformSelector(rightWaveformSelector);
  setupLabel(rightPWLabel, "R PW");
  setupSlider(rightPulseWidthSlider, 0, 4095, 2048, 45);

  // Left SID ADSR
  setupLabel(leftADSRLabel, "L ADSR");
  setupLabel(leftAttackLabel, "A");
  setupSlider(leftAttackSlider, 0, 15, 0, 30);
  setupLabel(leftDecayLabel, "D");
  setupSlider(leftDecaySlider, 0, 15, 8, 30);
  setupLabel(leftSustainLabel, "S");
  setupSlider(leftSustainSlider, 0, 15, 12, 30);
  setupLabel(leftReleaseLabel, "R");
  setupSlider(leftReleaseSlider, 0, 15, 4, 30);

  // Right SID ADSR
  setupLabel(rightADSRLabel, "R ADSR");
  setupLabel(rightAttackLabel, "A");
  setupSlider(rightAttackSlider, 0, 15, 0, 30);
  setupLabel(rightDecayLabel, "D");
  setupSlider(rightDecaySlider, 0, 15, 8, 30);
  setupLabel(rightSustainLabel, "S");
  setupSlider(rightSustainSlider, 0, 15, 12, 30);
  setupLabel(rightReleaseLabel, "R");
  setupSlider(rightReleaseSlider, 0, 15, 4, 30);
}

void BreadbinEditor::setupFilterControls() {
  // Helper to setup label
  auto setupLabel = [this](juce::Label &label, const juce::String &text) {
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(label);
  };

  // Helper to setup slider with styled text box
  auto setupSlider = [this](juce::Slider &slider, double min, double max,
                            double val, int textWidth) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setRange(min, max, 1.0);
    slider.setValue(val);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, textWidth, 16);
    slider.setColour(juce::Slider::textBoxBackgroundColourId,
                     juce::Colour(0x80000000));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour(juce::Slider::textBoxOutlineColourId,
                     juce::Colour(0x40FFFFFF));
    slider.onValueChange = [this]() { updateSynthFromControls(); };
    addAndMakeVisible(slider);
  };

  // Left SID Filter
  leftFilterLabel.setText("L Filter", juce::dontSendNotification);
  leftFilterLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(leftFilterLabel);

  setupLabel(leftCutoffLabel, "Cut");
  setupSlider(leftCutoffSlider, 0, 2047, 1024, 45);

  setupLabel(leftResonanceLabel, "Res");
  setupSlider(leftResonanceSlider, 0, 15, 8, 30);

  leftLPButton.onClick = [this]() { updateSynthFromControls(); };
  leftBPButton.onClick = [this]() { updateSynthFromControls(); };
  leftHPButton.onClick = [this]() { updateSynthFromControls(); };
  leftLPButton.setToggleState(true, juce::dontSendNotification);
  addAndMakeVisible(leftLPButton);
  addAndMakeVisible(leftBPButton);
  addAndMakeVisible(leftHPButton);

  // Right SID Filter
  rightFilterLabel.setText("R Filter", juce::dontSendNotification);
  rightFilterLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(rightFilterLabel);

  setupLabel(rightCutoffLabel, "Cut");
  setupSlider(rightCutoffSlider, 0, 2047, 1024, 45);

  setupLabel(rightResonanceLabel, "Res");
  setupSlider(rightResonanceSlider, 0, 15, 8, 30);

  rightLPButton.onClick = [this]() { updateSynthFromControls(); };
  rightBPButton.onClick = [this]() { updateSynthFromControls(); };
  rightHPButton.onClick = [this]() { updateSynthFromControls(); };
  rightLPButton.setToggleState(true, juce::dontSendNotification);
  addAndMakeVisible(rightLPButton);
  addAndMakeVisible(rightBPButton);
  addAndMakeVisible(rightHPButton);
}

void BreadbinEditor::updateSynthFromControls() {
  auto &sid = processor.getLeftSID();
  auto &sidR = processor.getRightSID();

  // Helper to map waveform ID to enum
  auto getWaveform = [](int id) -> SIDEngine::Waveform {
    switch (id) {
    case 1:
      return SIDEngine::Waveform::Triangle;
    case 2:
      return SIDEngine::Waveform::Sawtooth;
    case 3:
      return SIDEngine::Waveform::Pulse;
    case 4:
      return SIDEngine::Waveform::Noise;
    default:
      return SIDEngine::Waveform::Triangle;
    }
  };

  // Left SID waveform and pulse width
  auto leftWaveform = getWaveform(leftWaveformSelector.getSelectedId());
  int leftPW = static_cast<int>(leftPulseWidthSlider.getValue());
  for (int v = 0; v < 3; ++v) {
    sid.setWaveform(v, leftWaveform);
    sid.setPulseWidth(v, leftPW);
  }

  // Right SID waveform and pulse width
  auto rightWaveform = getWaveform(rightWaveformSelector.getSelectedId());
  int rightPW = static_cast<int>(rightPulseWidthSlider.getValue());
  for (int v = 0; v < 3; ++v) {
    sidR.setWaveform(v, rightWaveform);
    sidR.setPulseWidth(v, rightPW);
  }

  // Left SID ADSR
  int leftAttack = static_cast<int>(leftAttackSlider.getValue());
  int leftDecay = static_cast<int>(leftDecaySlider.getValue());
  int leftSustain = static_cast<int>(leftSustainSlider.getValue());
  int leftRelease = static_cast<int>(leftReleaseSlider.getValue());
  for (int v = 0; v < 3; ++v) {
    sid.setAttack(v, leftAttack);
    sid.setDecay(v, leftDecay);
    sid.setSustain(v, leftSustain);
    sid.setRelease(v, leftRelease);
  }

  // Right SID ADSR
  int rightAttack = static_cast<int>(rightAttackSlider.getValue());
  int rightDecay = static_cast<int>(rightDecaySlider.getValue());
  int rightSustain = static_cast<int>(rightSustainSlider.getValue());
  int rightRelease = static_cast<int>(rightReleaseSlider.getValue());
  for (int v = 0; v < 3; ++v) {
    sidR.setAttack(v, rightAttack);
    sidR.setDecay(v, rightDecay);
    sidR.setSustain(v, rightSustain);
    sidR.setRelease(v, rightRelease);
  }

  // Left SID Filter
  sid.setFilterCutoff(static_cast<int>(leftCutoffSlider.getValue()));
  sid.setFilterResonance(static_cast<int>(leftResonanceSlider.getValue()));
  sid.setFilterMode(leftLPButton.getToggleState(),
                    leftBPButton.getToggleState(),
                    leftHPButton.getToggleState());
  sid.setFilterVoices(true, true, true);

  // Right SID Filter
  sidR.setFilterCutoff(static_cast<int>(rightCutoffSlider.getValue()));
  sidR.setFilterResonance(static_cast<int>(rightResonanceSlider.getValue()));
  sidR.setFilterMode(rightLPButton.getToggleState(),
                     rightBPButton.getToggleState(),
                     rightHPButton.getToggleState());
  sidR.setFilterVoices(true, true, true);
}

void BreadbinEditor::paint(juce::Graphics &g) {
  // Fill entire background with dark color first
  g.fillAll(juce::Colour(0xFF1A1A40));

  // Draw background image in the area above the keyboard
  // Keyboard takes bottom 80 pixels, so background fills the rest
  if (backgroundImage.isValid()) {
    const int keyboardHeight = 80;
    auto bgBounds = getLocalBounds().withTrimmedBottom(keyboardHeight);

    // Draw image scaled to fill the background area while maintaining aspect
    // ratio
    g.drawImage(backgroundImage, bgBounds.toFloat(),
                juce::RectanglePlacement::centred |
                    juce::RectanglePlacement::fillDestination);
  }
}

void BreadbinEditor::resized() {
  auto bounds = getLocalBounds().reduced(15);

  // Skip title area - background image provides branding
  bounds.removeFromTop(10);

  // Mode row
  auto modeRow = bounds.removeFromTop(32);
  modeLabel.setBounds(modeRow.removeFromLeft(50));
  dualModeSelector.setBounds(modeRow.removeFromLeft(110));
  modeRow.removeFromLeft(10);
  leftChipLabel.setBounds(modeRow.removeFromLeft(20));
  leftChipSelector.setBounds(modeRow.removeFromLeft(60));
  modeRow.removeFromLeft(5);
  rightChipLabel.setBounds(modeRow.removeFromLeft(20));
  rightChipSelector.setBounds(modeRow.removeFromLeft(60));
  modeRow.removeFromLeft(10);
  leftPresetLabel.setBounds(modeRow.removeFromLeft(55));
  leftPresetSelector.setBounds(modeRow.removeFromLeft(100));
  modeRow.removeFromLeft(5);
  rightPresetLabel.setBounds(modeRow.removeFromLeft(55));
  rightPresetSelector.setBounds(modeRow.removeFromLeft(100));

  bounds.removeFromTop(10);

  // Synth controls row - per-voice waveform and per-voice ADSR
  auto synthRow = bounds.removeFromTop(90);
  auto sliderHeight = 60;
  auto adsrWidth = 38;

  // Left Waveform
  auto leftWaveArea = synthRow.removeFromLeft(60);
  leftWaveformLabel.setBounds(leftWaveArea.removeFromTop(14));
  leftWaveformSelector.setBounds(leftWaveArea.removeFromTop(24));
  leftPWLabel.setBounds(leftWaveArea.removeFromTop(12));
  leftPulseWidthSlider.setBounds(leftWaveArea);

  synthRow.removeFromLeft(3);

  // Left ADSR
  leftADSRLabel.setBounds(synthRow.removeFromLeft(40).removeFromTop(14));
  synthRow.removeFromLeft(-40);
  auto lADSRArea = synthRow.removeFromLeft(adsrWidth * 4);
  auto lARow = lADSRArea.removeFromLeft(adsrWidth);
  leftAttackLabel.setBounds(lARow.removeFromTop(12));
  leftAttackSlider.setBounds(lARow);
  auto lDRow = lADSRArea.removeFromLeft(adsrWidth);
  leftDecayLabel.setBounds(lDRow.removeFromTop(12));
  leftDecaySlider.setBounds(lDRow);
  auto lSRow = lADSRArea.removeFromLeft(adsrWidth);
  leftSustainLabel.setBounds(lSRow.removeFromTop(12));
  leftSustainSlider.setBounds(lSRow);
  auto lRRow = lADSRArea.removeFromLeft(adsrWidth);
  leftReleaseLabel.setBounds(lRRow.removeFromTop(12));
  leftReleaseSlider.setBounds(lRRow);

  synthRow.removeFromLeft(10);

  // Right Waveform
  auto rightWaveArea = synthRow.removeFromLeft(60);
  rightWaveformLabel.setBounds(rightWaveArea.removeFromTop(14));
  rightWaveformSelector.setBounds(rightWaveArea.removeFromTop(24));
  rightPWLabel.setBounds(rightWaveArea.removeFromTop(12));
  rightPulseWidthSlider.setBounds(rightWaveArea);

  synthRow.removeFromLeft(3);

  // Right ADSR
  rightADSRLabel.setBounds(synthRow.removeFromLeft(40).removeFromTop(14));
  synthRow.removeFromLeft(-40);
  auto rADSRArea = synthRow.removeFromLeft(adsrWidth * 4);
  auto rARow = rADSRArea.removeFromLeft(adsrWidth);
  rightAttackLabel.setBounds(rARow.removeFromTop(12));
  rightAttackSlider.setBounds(rARow);
  auto rDRow = rADSRArea.removeFromLeft(adsrWidth);
  rightDecayLabel.setBounds(rDRow.removeFromTop(12));
  rightDecaySlider.setBounds(rDRow);
  auto rSRow = rADSRArea.removeFromLeft(adsrWidth);
  rightSustainLabel.setBounds(rSRow.removeFromTop(12));
  rightSustainSlider.setBounds(rSRow);
  auto rRRow = rADSRArea.removeFromLeft(adsrWidth);
  rightReleaseLabel.setBounds(rRRow.removeFromTop(12));
  rightReleaseSlider.setBounds(rRRow);

  bounds.removeFromTop(8);

  // Filter rows - Left and Right SID filter panels
  auto filterRow = bounds.removeFromTop(90);
  auto filterSliderHeight = 60;
  auto buttonHeight = 22;

  // Left Filter Panel
  leftFilterLabel.setBounds(filterRow.removeFromLeft(45).withHeight(18));
  filterRow.removeFromLeft(-45);
  auto leftPanel = filterRow.removeFromLeft(180);
  leftFilterLabel.setBounds(leftPanel.removeFromTop(18));

  auto leftControlRow = leftPanel.removeFromTop(sliderHeight);
  auto leftCutArea = leftControlRow.removeFromLeft(50);
  leftCutoffLabel.setBounds(leftCutArea.removeFromTop(14));
  leftCutoffSlider.setBounds(leftCutArea);

  auto leftResArea = leftControlRow.removeFromLeft(45);
  leftResonanceLabel.setBounds(leftResArea.removeFromTop(14));
  leftResonanceSlider.setBounds(leftResArea);

  auto leftButtonRow = leftControlRow.removeFromLeft(80);
  leftButtonRow.removeFromTop(10);
  leftLPButton.setBounds(
      leftButtonRow.removeFromLeft(26).removeFromTop(buttonHeight));
  leftBPButton.setBounds(
      leftButtonRow.removeFromLeft(26).removeFromTop(buttonHeight));
  leftHPButton.setBounds(
      leftButtonRow.removeFromLeft(26).removeFromTop(buttonHeight));

  filterRow.removeFromLeft(20);

  // Right Filter  Panel
  rightFilterLabel.setBounds(filterRow.removeFromLeft(45).withHeight(18));
  filterRow.removeFromLeft(-45);
  auto rightPanel = filterRow.removeFromLeft(180);
  rightFilterLabel.setBounds(rightPanel.removeFromTop(18));

  auto rightControlRow = rightPanel.removeFromTop(sliderHeight);
  auto rightCutArea = rightControlRow.removeFromLeft(50);
  rightCutoffLabel.setBounds(rightCutArea.removeFromTop(14));
  rightCutoffSlider.setBounds(rightCutArea);

  auto rightResArea = rightControlRow.removeFromLeft(45);
  rightResonanceLabel.setBounds(rightResArea.removeFromTop(14));
  rightResonanceSlider.setBounds(rightResArea);

  auto rightButtonRow = rightControlRow.removeFromLeft(80);
  rightButtonRow.removeFromTop(10);
  rightLPButton.setBounds(
      rightButtonRow.removeFromLeft(26).removeFromTop(buttonHeight));
  rightBPButton.setBounds(
      rightButtonRow.removeFromLeft(26).removeFromTop(buttonHeight));
  rightHPButton.setBounds(
      rightButtonRow.removeFromLeft(26).removeFromTop(buttonHeight));

  bounds.removeFromTop(5);

  // Time Machine slider with 1982/NOW labels
  agingLabel.setBounds(bounds.removeFromTop(18));
  auto agingRow = bounds.removeFromTop(25);
  agingStartLabel.setBounds(agingRow.removeFromLeft(35));
  agingEndLabel.setBounds(agingRow.removeFromRight(35));
  agingSlider.setBounds(agingRow.reduced(5, 0));

  bounds.removeFromTop(10);

  // Keyboard at bottom
  keyboard.setBounds(bounds.removeFromBottom(80));
}

void BreadbinEditor::applyPreset(int presetId) {
  auto &sid = processor.getLeftSID();
  auto &sidR = processor.getRightSID();

  // Helper to apply filter settings to both L/R
  auto applyFilterPreset = [this](int cutoff, int resonance, bool lp, bool bp,
                                  bool hp) {
    leftCutoffSlider.setValue(cutoff);
    leftResonanceSlider.setValue(resonance);
    leftLPButton.setToggleState(lp, juce::dontSendNotification);
    leftBPButton.setToggleState(bp, juce::dontSendNotification);
    leftHPButton.setToggleState(hp, juce::dontSendNotification);
    rightCutoffSlider.setValue(cutoff);
    rightResonanceSlider.setValue(resonance);
    rightLPButton.setToggleState(lp, juce::dontSendNotification);
    rightBPButton.setToggleState(bp, juce::dontSendNotification);
    rightHPButton.setToggleState(hp, juce::dontSendNotification);
  };

  // Helper to apply waveform and PW to both L/R
  auto applyWaveformPreset = [this](int waveformId, int pw) {
    leftWaveformSelector.setSelectedId(waveformId);
    rightWaveformSelector.setSelectedId(waveformId);
    leftPulseWidthSlider.setValue(pw);
    rightPulseWidthSlider.setValue(pw);
  };

  // Helper to apply ADSR to both L/R
  auto applyADSRPreset = [this](int a, int d, int s, int r) {
    leftAttackSlider.setValue(a);
    leftDecaySlider.setValue(d);
    leftSustainSlider.setValue(s);
    leftReleaseSlider.setValue(r);
    rightAttackSlider.setValue(a);
    rightDecaySlider.setValue(d);
    rightSustainSlider.setValue(s);
    rightReleaseSlider.setValue(r);
  };

  switch (presetId) {
  case 2: // Classic Lead (Monty on the Run style)
    // Sawtooth, short attack, medium decay, high sustain
    applyWaveformPreset(2, 2048); // Sawtooth
    applyADSRPreset(0, 6, 12, 4);
    applyFilterPreset(1400, 6, true, false, false);
    break;

  case 3: // Fat Bass (Ocean Loader style)
    // Pulse wave, punchy envelope
    applyWaveformPreset(3, 1024); // Pulse, 25% duty cycle
    applyADSRPreset(0, 8, 6, 2);
    applyFilterPreset(600, 10, true, false, false);
    break;

  case 4: // PWM Pad (Hubbard style)
    // Triangle + Pulse combo, slow attack
    applyWaveformPreset(3, 2048); // Pulse for PWM
    applyADSRPreset(10, 4, 14, 8);
    applyFilterPreset(1800, 4, true, false, false);
    break;

  case 5:                         // Noise Snare
    applyWaveformPreset(4, 2048); // Noise
    applyADSRPreset(0, 6, 0, 4);
    applyFilterPreset(1200, 2, false, true, false);
    break;

  default:
    return; // "-- Select --" does nothing
  }

  updateSynthFromControls();
}
