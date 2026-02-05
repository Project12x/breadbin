#include "PluginEditor.h"
#include "BinaryData.h"

BreadbinEditor::BreadbinEditor(BreadbinProcessor &p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {

  backgroundImage = juce::ImageFileFormat::loadFrom(
      BinaryData::background_jpg, BinaryData::background_jpgSize);

  keyboardState.addListener(this);
  processor.getMidiMessageCollector().reset(p.getSampleRate());

  setupControls();
  setupLeftSID();
  setupCenterSID();
  setupRightSID();
  setupArpeggiator();
  setupVoiceEditor();

  selectVoice(0);
  setSize(700, 550);
}

BreadbinEditor::~BreadbinEditor() { keyboardState.removeListener(this); }

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

void BreadbinEditor::setupControls() {
  // Title
  titleLabel.setText("BREADBIN", juce::dontSendNotification);
  titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
  titleLabel.setJustificationType(juce::Justification::centred);
  titleLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
  addAndMakeVisible(titleLabel);

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

  // Preset
  presetLabel.setText("Preset:", juce::dontSendNotification);
  presetLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(presetLabel);

  presetSelector.addItem("-- Select --", 1);
  presetSelector.addItem("Classic Lead (Monty)", 2);
  presetSelector.addItem("Fat Bass (Ocean)", 3);
  presetSelector.addItem("PWM Pad (Hubbard)", 4);
  presetSelector.addItem("Noise Snare", 5);
  presetSelector.setSelectedId(1);
  presetSelector.setTooltip("Applies preset to currently selected voice");
  presetSelector.onChange = [this]() {
    if (presetSelector.getSelectedId() > 1)
      applyPreset(presetSelector.getSelectedId());
  };
  addAndMakeVisible(presetSelector);

  // Time Machine
  agingLabel.setText("Age:", juce::dontSendNotification);
  agingLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(agingLabel);

  agingStartLabel.setText("82", juce::dontSendNotification);
  agingStartLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
  agingStartLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(agingStartLabel);

  agingEndLabel.setText("NOW", juce::dontSendNotification);
  agingEndLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
  agingEndLabel.setFont(juce::Font(10.0f));
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

  // Keyboard
  keyboard.setKeyWidth(16.0f);
  addAndMakeVisible(keyboard);
}

void BreadbinEditor::setupLeftSID() {
  leftSIDLabel.setText("LEFT SID", juce::dontSendNotification);
  leftSIDLabel.setFont(juce::Font(14.0f, juce::Font::bold));
  leftSIDLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
  leftSIDLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(leftSIDLabel);

  leftChipSelector.addItem("6581", 1);
  leftChipSelector.addItem("8580", 2);
  leftChipSelector.setSelectedId(1);
  leftChipSelector.onChange = [this]() {
    processor.setLeftChipModel(leftChipSelector.getSelectedId() == 1
                                   ? SIDEngine::ChipModel::MOS6581
                                   : SIDEngine::ChipModel::MOS8580);
  };
  addAndMakeVisible(leftChipSelector);

  // Voice buttons and enables for L SID (voices 0-2)
  for (int i = 0; i < 3; ++i) {
    leftVoiceButtons[i].setButtonText(juce::String(i + 1));
    leftVoiceButtons[i].onClick = [this, i]() { selectVoice(i); };
    addAndMakeVisible(leftVoiceButtons[i]);

    leftVoiceEnables[i].setButtonText("");
    leftVoiceEnables[i].setToggleState(true, juce::dontSendNotification);
    leftVoiceEnables[i].onClick = [this, i]() {
      processor.getVoiceSettings(i).enabled =
          leftVoiceEnables[i].getToggleState();
    };
    addAndMakeVisible(leftVoiceEnables[i]);
  }

  // Filter
  leftCutoffLabel.setText("Cut", juce::dontSendNotification);
  leftCutoffLabel.setColour(juce::Label::textColourId,
                            juce::Colours::lightgrey);
  leftCutoffLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(leftCutoffLabel);

  leftCutoffSlider.setRange(0, 2047, 1);
  leftCutoffSlider.setValue(1024);
  leftCutoffSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  leftCutoffSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  leftCutoffSlider.onValueChange = [this]() {
    processor.getLeftSID().setFilterCutoff(
        static_cast<int>(leftCutoffSlider.getValue()));
  };
  addAndMakeVisible(leftCutoffSlider);

  leftResonanceLabel.setText("Res", juce::dontSendNotification);
  leftResonanceLabel.setColour(juce::Label::textColourId,
                               juce::Colours::lightgrey);
  leftResonanceLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(leftResonanceLabel);

  leftResonanceSlider.setRange(0, 15, 1);
  leftResonanceSlider.setValue(0);
  leftResonanceSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  leftResonanceSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  leftResonanceSlider.onValueChange = [this]() {
    processor.getLeftSID().setFilterResonance(
        static_cast<int>(leftResonanceSlider.getValue()));
  };
  addAndMakeVisible(leftResonanceSlider);

  auto setupButton = [this](juce::ToggleButton &btn) {
    btn.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    btn.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    btn.onClick = [this]() { updateFiltersFromUI(); };
    addAndMakeVisible(btn);
  };
  setupButton(leftLPButton);
  setupButton(leftBPButton);
  setupButton(leftHPButton);
  leftLPButton.setToggleState(true, juce::dontSendNotification);

  processor.getLeftSID().setFilterVoices(true, true, true);
  processor.getLeftSID().setFilterMode(true, false, false);
  processor.getLeftSID().setFilterCutoff(1024);
}

void BreadbinEditor::setupRightSID() {
  rightSIDLabel.setText("RIGHT SID", juce::dontSendNotification);
  rightSIDLabel.setFont(juce::Font(14.0f, juce::Font::bold));
  rightSIDLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
  rightSIDLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(rightSIDLabel);

  rightChipSelector.addItem("6581", 1);
  rightChipSelector.addItem("8580", 2);
  rightChipSelector.setSelectedId(1);
  rightChipSelector.onChange = [this]() {
    processor.setRightChipModel(rightChipSelector.getSelectedId() == 1
                                    ? SIDEngine::ChipModel::MOS6581
                                    : SIDEngine::ChipModel::MOS8580);
  };
  addAndMakeVisible(rightChipSelector);

  // Voice buttons and enables for R SID (voices 3-5)
  for (int i = 0; i < 3; ++i) {
    rightVoiceButtons[i].setButtonText(juce::String(i + 4));
    rightVoiceButtons[i].onClick = [this, i]() { selectVoice(i + 3); };
    addAndMakeVisible(rightVoiceButtons[i]);

    rightVoiceEnables[i].setButtonText("");
    rightVoiceEnables[i].setToggleState(true, juce::dontSendNotification);
    rightVoiceEnables[i].onClick = [this, i]() {
      processor.getVoiceSettings(i + 3).enabled =
          rightVoiceEnables[i].getToggleState();
    };
    addAndMakeVisible(rightVoiceEnables[i]);
  }

  // Filter
  rightCutoffLabel.setText("Cut", juce::dontSendNotification);
  rightCutoffLabel.setColour(juce::Label::textColourId,
                             juce::Colours::lightgrey);
  rightCutoffLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(rightCutoffLabel);

  rightCutoffSlider.setRange(0, 2047, 1);
  rightCutoffSlider.setValue(1024);
  rightCutoffSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  rightCutoffSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  rightCutoffSlider.onValueChange = [this]() {
    processor.getRightSID().setFilterCutoff(
        static_cast<int>(rightCutoffSlider.getValue()));
  };
  addAndMakeVisible(rightCutoffSlider);

  rightResonanceLabel.setText("Res", juce::dontSendNotification);
  rightResonanceLabel.setColour(juce::Label::textColourId,
                                juce::Colours::lightgrey);
  rightResonanceLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(rightResonanceLabel);

  rightResonanceSlider.setRange(0, 15, 1);
  rightResonanceSlider.setValue(0);
  rightResonanceSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  rightResonanceSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  rightResonanceSlider.onValueChange = [this]() {
    processor.getRightSID().setFilterResonance(
        static_cast<int>(rightResonanceSlider.getValue()));
  };
  addAndMakeVisible(rightResonanceSlider);

  auto setupButton = [this](juce::ToggleButton &btn) {
    btn.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    btn.setColour(juce::ToggleButton::tickColourId, juce::Colours::orange);
    btn.onClick = [this]() { updateFiltersFromUI(); };
    addAndMakeVisible(btn);
  };
  setupButton(rightLPButton);
  setupButton(rightBPButton);
  setupButton(rightHPButton);
  rightLPButton.setToggleState(true, juce::dontSendNotification);

  processor.getRightSID().setFilterVoices(true, true, true);
  processor.getRightSID().setFilterMode(true, false, false);
  processor.getRightSID().setFilterCutoff(1024);
}

void BreadbinEditor::setupCenterSID() {
  centerSIDLabel.setText("CENTER SID", juce::dontSendNotification);
  centerSIDLabel.setFont(juce::Font(14.0f, juce::Font::bold));
  centerSIDLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
  centerSIDLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(centerSIDLabel);

  centerChipSelector.addItem("6581", 1);
  centerChipSelector.addItem("8580", 2);
  centerChipSelector.setSelectedId(1);
  centerChipSelector.onChange = [this]() {
    processor.getCenterSID().setChipModel(centerChipSelector.getSelectedId() ==
                                                  1
                                              ? SIDEngine::ChipModel::MOS6581
                                              : SIDEngine::ChipModel::MOS8580);
  };
  addAndMakeVisible(centerChipSelector);

  // Voice buttons and enables for C SID (voices 3-5)
  for (int i = 0; i < 3; ++i) {
    centerVoiceButtons[i].setButtonText(juce::String(i + 4));
    centerVoiceButtons[i].onClick = [this, i]() { selectVoice(i + 3); };
    addAndMakeVisible(centerVoiceButtons[i]);

    centerVoiceEnables[i].setButtonText("");
    centerVoiceEnables[i].setToggleState(true, juce::dontSendNotification);
    centerVoiceEnables[i].onClick = [this, i]() {
      processor.getVoiceSettings(i + 3).enabled =
          centerVoiceEnables[i].getToggleState();
    };
    addAndMakeVisible(centerVoiceEnables[i]);
  }

  // Filter
  centerCutoffLabel.setText("Cut", juce::dontSendNotification);
  centerCutoffLabel.setColour(juce::Label::textColourId,
                              juce::Colours::lightgrey);
  centerCutoffLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(centerCutoffLabel);

  centerCutoffSlider.setRange(0, 2047, 1);
  centerCutoffSlider.setValue(1024);
  centerCutoffSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  centerCutoffSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  centerCutoffSlider.onValueChange = [this]() {
    processor.getCenterSID().setFilterCutoff(
        static_cast<int>(centerCutoffSlider.getValue()));
  };
  addAndMakeVisible(centerCutoffSlider);

  centerResonanceLabel.setText("Res", juce::dontSendNotification);
  centerResonanceLabel.setColour(juce::Label::textColourId,
                                 juce::Colours::lightgrey);
  centerResonanceLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(centerResonanceLabel);

  centerResonanceSlider.setRange(0, 15, 1);
  centerResonanceSlider.setValue(0);
  centerResonanceSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  centerResonanceSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  centerResonanceSlider.onValueChange = [this]() {
    processor.getCenterSID().setFilterResonance(
        static_cast<int>(centerResonanceSlider.getValue()));
  };
  addAndMakeVisible(centerResonanceSlider);

  auto setupButton = [this](juce::ToggleButton &btn) {
    btn.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    btn.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    btn.onClick = [this]() { updateFiltersFromUI(); };
    addAndMakeVisible(btn);
  };
  setupButton(centerLPButton);
  setupButton(centerBPButton);
  setupButton(centerHPButton);
  centerLPButton.setToggleState(true, juce::dontSendNotification);

  processor.getCenterSID().setFilterVoices(true, true, true);
  processor.getCenterSID().setFilterMode(true, false, false);
  processor.getCenterSID().setFilterCutoff(1024);
}

void BreadbinEditor::setupArpeggiator() {
  arpLabel.setText("ARPEGGIATOR", juce::dontSendNotification);
  arpLabel.setFont(juce::Font(12.0f, juce::Font::bold));
  arpLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);
  arpLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(arpLabel);

  arpEnableButton.setColour(juce::ToggleButton::textColourId,
                            juce::Colours::yellow);
  arpEnableButton.setColour(juce::ToggleButton::tickColourId,
                            juce::Colours::yellow);
  arpEnableButton.onClick = [this]() {
    processor.setArpEnabled(arpEnableButton.getToggleState());
  };
  addAndMakeVisible(arpEnableButton);

  arpPatternLabel.setText("Pattern", juce::dontSendNotification);
  arpPatternLabel.setColour(juce::Label::textColourId,
                            juce::Colours::lightgrey);
  arpPatternLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(arpPatternLabel);

  arpPatternSelector.addItem("Up", 1);
  arpPatternSelector.addItem("Down", 2);
  arpPatternSelector.addItem("Up/Down", 3);
  arpPatternSelector.addItem("Random", 4);
  arpPatternSelector.setSelectedId(1);
  arpPatternSelector.onChange = [this]() {
    using AP = BreadbinProcessor::ArpPattern;
    switch (arpPatternSelector.getSelectedId()) {
    case 1:
      processor.setArpPattern(AP::Up);
      break;
    case 2:
      processor.setArpPattern(AP::Down);
      break;
    case 3:
      processor.setArpPattern(AP::UpDown);
      break;
    case 4:
      processor.setArpPattern(AP::Random);
      break;
    }
  };
  addAndMakeVisible(arpPatternSelector);

  arpRateModeLabel.setText("Rate", juce::dontSendNotification);
  arpRateModeLabel.setColour(juce::Label::textColourId,
                             juce::Colours::lightgrey);
  arpRateModeLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(arpRateModeLabel);

  arpRateModeSelector.addItem("PAL 50Hz", 1);
  arpRateModeSelector.addItem("NTSC 60Hz", 2);
  arpRateModeSelector.addItem("Sync", 3);
  arpRateModeSelector.setSelectedId(1);
  arpRateModeSelector.onChange = [this]() {
    using ARM = BreadbinProcessor::ArpRateMode;
    switch (arpRateModeSelector.getSelectedId()) {
    case 1:
      processor.setArpRateMode(ARM::PAL50Hz);
      break;
    case 2:
      processor.setArpRateMode(ARM::NTSC60Hz);
      break;
    case 3:
      processor.setArpRateMode(ARM::Sync);
      break;
    }
  };
  addAndMakeVisible(arpRateModeSelector);

  arpOctaveLabel.setText("Octaves", juce::dontSendNotification);
  arpOctaveLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  arpOctaveLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(arpOctaveLabel);

  arpOctaveSlider.setRange(1, 3, 1);
  arpOctaveSlider.setValue(1);
  arpOctaveSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  arpOctaveSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 30, 15);
  arpOctaveSlider.onValueChange = [this]() {
    processor.setArpOctaveRange(static_cast<int>(arpOctaveSlider.getValue()));
  };
  addAndMakeVisible(arpOctaveSlider);
}

void BreadbinEditor::setupVoiceEditor() {
  voiceEditorLabel.setText("VOICE EDITOR", juce::dontSendNotification);
  voiceEditorLabel.setFont(juce::Font(12.0f, juce::Font::bold));
  voiceEditorLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(voiceEditorLabel);

  waveformLabel.setText("Wave:", juce::dontSendNotification);
  waveformLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(waveformLabel);

  waveformSelector.addItem("Tri", 1);
  waveformSelector.addItem("Saw", 2);
  waveformSelector.addItem("Pulse", 3);
  waveformSelector.addItem("Noise", 4);
  waveformSelector.setSelectedId(1);
  waveformSelector.onChange = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(waveformSelector);

  pwLabel.setText("PW:", juce::dontSendNotification);
  pwLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(pwLabel);

  pulseWidthSlider.setRange(0, 4095, 1);
  pulseWidthSlider.setValue(2048);
  pulseWidthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  pulseWidthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  pulseWidthSlider.onValueChange = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(pulseWidthSlider);

  auto setupADSR = [this](juce::Slider &slider, juce::Label &label,
                          const juce::String &text, int defaultVal) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(juce::Font(10.0f));
    addAndMakeVisible(label);

    slider.setRange(0, 15, 1);
    slider.setValue(defaultVal);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.onValueChange = [this]() { saveUIToVoice(selectedVoice); };
    addAndMakeVisible(slider);
  };

  setupADSR(attackSlider, attackLabel, "A", 0);
  setupADSR(decaySlider, decayLabel, "D", 0);
  setupADSR(sustainSlider, sustainLabel, "S", 15);
  setupADSR(releaseSlider, releaseLabel, "R", 0);

  panLabel.setText("Pan:", juce::dontSendNotification);
  panLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(panLabel);

  panSlider.setRange(-1.0, 1.0, 0.01);
  panSlider.setValue(0.0);
  panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  panSlider.onValueChange = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(panSlider);
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
  int voiceNum = (selectedVoice % 3) + 1;
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
  panSlider.setValue(settings.pan, juce::dontSendNotification);
  presetSelector.setSelectedId(settings.presetId, juce::dontSendNotification);
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
  settings.pan = static_cast<float>(panSlider.getValue());

  processor.applyVoiceSettings(voice);
}

void BreadbinEditor::updateFiltersFromUI() {
  processor.getLeftSID().setFilterMode(leftLPButton.getToggleState(),
                                       leftBPButton.getToggleState(),
                                       leftHPButton.getToggleState());
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
  auto bounds = getLocalBounds().reduced(8);
  const int rowH = 28;
  const int pad = 4;

  // ===== TOP ROW: Title, Mode, Preset =====
  auto topRow = bounds.removeFromTop(rowH);
  titleLabel.setBounds(topRow.removeFromLeft(85));
  topRow.removeFromLeft(pad);
  modeLabel.setBounds(topRow.removeFromLeft(35));
  dualModeSelector.setBounds(
      topRow.removeFromLeft(100)); // Wider for "Stereo Split"
  topRow.removeFromLeft(pad * 2);
  presetLabel.setBounds(topRow.removeFromLeft(45));
  presetSelector.setBounds(
      topRow.removeFromLeft(150)); // Wider for "Classic Lead (Monty)"

  bounds.removeFromTop(pad * 2);

  // ===== SID PANELS: Left, Center, Right side by side =====
  auto sidRow = bounds.removeFromTop(140);
  const int sidWidth = (sidRow.getWidth() - pad * 4) / 3;

  // ----- LEFT SID -----
  auto leftPanel = sidRow.removeFromLeft(sidWidth);
  leftSIDLabel.setBounds(leftPanel.removeFromTop(18));
  auto leftChipRow = leftPanel.removeFromTop(22);
  leftChipSelector.setBounds(leftChipRow.removeFromLeft(60));

  // Voice buttons with enable checkboxes
  auto leftVoicesRow = leftPanel.removeFromTop(26);
  for (int i = 0; i < 3; ++i) {
    leftVoiceEnables[i].setBounds(leftVoicesRow.removeFromLeft(18));
    leftVoiceButtons[i].setBounds(leftVoicesRow.removeFromLeft(35));
    leftVoicesRow.removeFromLeft(2);
  }

  // Filter
  leftPanel.removeFromTop(2);
  auto leftFilterRow = leftPanel.removeFromTop(40);
  leftCutoffLabel.setBounds(leftFilterRow.removeFromLeft(22));
  leftCutoffSlider.setBounds(leftFilterRow.removeFromLeft(38));
  leftResonanceLabel.setBounds(leftFilterRow.removeFromLeft(22));
  leftResonanceSlider.setBounds(leftFilterRow.removeFromLeft(38));

  auto leftModesRow = leftPanel.removeFromTop(20);
  leftLPButton.setBounds(leftModesRow.removeFromLeft(35));
  leftBPButton.setBounds(leftModesRow.removeFromLeft(35));
  leftHPButton.setBounds(leftModesRow.removeFromLeft(35));

  sidRow.removeFromLeft(pad * 2);

  // ----- CENTER SID -----
  auto centerPanel = sidRow.removeFromLeft(sidWidth);
  centerSIDLabel.setBounds(centerPanel.removeFromTop(18));
  auto centerChipRow = centerPanel.removeFromTop(22);
  centerChipSelector.setBounds(centerChipRow.removeFromLeft(60));

  auto centerVoicesRow = centerPanel.removeFromTop(26);
  for (int i = 0; i < 3; ++i) {
    centerVoiceEnables[i].setBounds(centerVoicesRow.removeFromLeft(18));
    centerVoiceButtons[i].setBounds(centerVoicesRow.removeFromLeft(35));
    centerVoicesRow.removeFromLeft(2);
  }

  centerPanel.removeFromTop(2);
  auto centerFilterRow = centerPanel.removeFromTop(40);
  centerCutoffLabel.setBounds(centerFilterRow.removeFromLeft(22));
  centerCutoffSlider.setBounds(centerFilterRow.removeFromLeft(38));
  centerResonanceLabel.setBounds(centerFilterRow.removeFromLeft(22));
  centerResonanceSlider.setBounds(centerFilterRow.removeFromLeft(38));

  auto centerModesRow = centerPanel.removeFromTop(20);
  centerLPButton.setBounds(centerModesRow.removeFromLeft(35));
  centerBPButton.setBounds(centerModesRow.removeFromLeft(35));
  centerHPButton.setBounds(centerModesRow.removeFromLeft(35));

  sidRow.removeFromLeft(pad * 2);

  // ----- RIGHT SID -----
  auto rightPanel = sidRow;
  rightSIDLabel.setBounds(rightPanel.removeFromTop(18));
  auto rightChipRow = rightPanel.removeFromTop(22);
  rightChipSelector.setBounds(rightChipRow.removeFromLeft(60));

  auto rightVoicesRow = rightPanel.removeFromTop(26);
  for (int i = 0; i < 3; ++i) {
    rightVoiceEnables[i].setBounds(rightVoicesRow.removeFromLeft(18));
    rightVoiceButtons[i].setBounds(rightVoicesRow.removeFromLeft(35));
    rightVoicesRow.removeFromLeft(2);
  }

  rightPanel.removeFromTop(2);
  auto rightFilterRow = rightPanel.removeFromTop(40);
  rightCutoffLabel.setBounds(rightFilterRow.removeFromLeft(22));
  rightCutoffSlider.setBounds(rightFilterRow.removeFromLeft(38));
  rightResonanceLabel.setBounds(rightFilterRow.removeFromLeft(22));
  rightResonanceSlider.setBounds(rightFilterRow.removeFromLeft(38));

  auto rightModesRow = rightPanel.removeFromTop(20);
  rightLPButton.setBounds(rightModesRow.removeFromLeft(35));
  rightBPButton.setBounds(rightModesRow.removeFromLeft(35));
  rightHPButton.setBounds(rightModesRow.removeFromLeft(35));

  bounds.removeFromTop(pad * 2);

  // ===== ARPEGGIATOR SECTION =====
  auto arpRow = bounds.removeFromTop(60);
  arpLabel.setBounds(arpRow.removeFromLeft(90));
  arpEnableButton.setBounds(arpRow.removeFromLeft(40));
  arpRow.removeFromLeft(pad);
  arpPatternLabel.setBounds(arpRow.removeFromLeft(50));
  arpPatternSelector.setBounds(arpRow.removeFromLeft(80));
  arpRow.removeFromLeft(pad);
  arpRateModeLabel.setBounds(arpRow.removeFromLeft(35));
  arpRateModeSelector.setBounds(arpRow.removeFromLeft(90));
  arpRow.removeFromLeft(pad);
  arpOctaveLabel.setBounds(arpRow.removeFromLeft(50));
  arpOctaveSlider.setBounds(arpRow.removeFromLeft(50));

  // ===== VOICE EDITOR =====
  auto editorRow = bounds.removeFromTop(80);
  voiceEditorLabel.setBounds(editorRow.removeFromTop(18));
  auto controlsRow = editorRow;

  waveformLabel.setBounds(controlsRow.removeFromLeft(35));
  waveformSelector.setBounds(controlsRow.removeFromLeft(65));
  controlsRow.removeFromLeft(pad);
  pwLabel.setBounds(controlsRow.removeFromLeft(25));
  pulseWidthSlider.setBounds(controlsRow.removeFromLeft(80));
  controlsRow.removeFromLeft(pad * 2);

  // ADSR
  const int adsrW = 30;
  const int adsrH = 55;
  auto adsrArea = controlsRow.removeFromLeft(adsrW * 4);
  int adsrY = adsrArea.getY();
  attackLabel.setBounds(adsrArea.getX(), adsrY, adsrW, 12);
  attackSlider.setBounds(adsrArea.getX(), adsrY + 12, adsrW, adsrH);
  decayLabel.setBounds(adsrArea.getX() + adsrW, adsrY, adsrW, 12);
  decaySlider.setBounds(adsrArea.getX() + adsrW, adsrY + 12, adsrW, adsrH);
  sustainLabel.setBounds(adsrArea.getX() + adsrW * 2, adsrY, adsrW, 12);
  sustainSlider.setBounds(adsrArea.getX() + adsrW * 2, adsrY + 12, adsrW,
                          adsrH);
  releaseLabel.setBounds(adsrArea.getX() + adsrW * 3, adsrY, adsrW, 12);
  releaseSlider.setBounds(adsrArea.getX() + adsrW * 3, adsrY + 12, adsrW,
                          adsrH);

  controlsRow.removeFromLeft(pad * 2);
  panLabel.setBounds(controlsRow.removeFromLeft(30));
  panSlider.setBounds(controlsRow.removeFromLeft(100));

  bounds.removeFromTop(pad);

  // ===== KEYBOARD =====
  keyboard.setBounds(bounds.removeFromBottom(60));

  // ===== TIME MACHINE (bottom, above keyboard) =====
  auto agingRow = bounds.removeFromBottom(24);
  agingLabel.setBounds(agingRow.removeFromLeft(30));
  agingStartLabel.setBounds(agingRow.removeFromLeft(22));
  agingSlider.setBounds(agingRow.removeFromLeft(200));
  agingEndLabel.setBounds(agingRow.removeFromLeft(30));
}

void BreadbinEditor::applyPreset(int presetId) {
  auto configureVoice = [this, presetId](int voice, SIDEngine::Waveform wave,
                                         int pw, int a, int d, int s, int r,
                                         float pan) {
    auto &settings = processor.getVoiceSettings(voice);
    settings.presetId = presetId;
    settings.waveform = wave;
    settings.pulseWidth = pw;
    settings.attack = a;
    settings.decay = d;
    settings.sustain = s;
    settings.release = r;
    settings.pan = pan;
    processor.applyVoiceSettings(voice);
  };

  // Apply preset to selected voice only
  float pan = (selectedVoice < 3) ? -0.5f : 0.5f;

  switch (presetId) {
  case 2: // Classic Lead (Monty) - Pulse with medium PWM
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4,
                   pan);
    break;
  case 3: // Fat Bass (Ocean) - Sawtooth with slow attack
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12,
                   3, pan);
    break;
  case 4: // PWM Pad (Hubbard) - Pulse with slow attack/release
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 1024, 8, 6, 12, 8,
                   pan);
    break;
  case 5: // Noise Snare - Noise with fast decay
    configureVoice(selectedVoice, SIDEngine::Waveform::Noise, 0, 0, 8, 0, 4,
                   pan);
    break;
  }

  loadVoiceToUI(selectedVoice);
}
