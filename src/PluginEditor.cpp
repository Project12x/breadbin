#include "PluginEditor.h"
#include "BinaryData.h"

BreadbinEditor::BreadbinEditor(BreadbinProcessor &p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {

  // Load background image
  backgroundImage = juce::ImageFileFormat::loadFrom(
      BinaryData::background_jpg, BinaryData::background_jpgSize);

  keyboardState.addListener(this);
  processor.getMidiMessageCollector().reset(p.getSampleRate());

  setupControls();
  setupVoiceSelector();
  setupVoiceControls();
  setupFilterControls();

  // Initialize all voices with default settings, select voice 1
  selectVoice(0);

  setSize(700, 600);
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
  titleLabel.setFont(juce::Font(28.0f, juce::Font::bold));
  titleLabel.setJustificationType(juce::Justification::centred);
  titleLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
  addAndMakeVisible(titleLabel);

  // Mode selector
  modeLabel.setText("Mode:", juce::dontSendNotification);
  modeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(modeLabel);

  dualModeSelector.addItem("Stereo Split", 1);
  dualModeSelector.addItem("Unison", 2);
  dualModeSelector.addItem("Multitimbral", 3);
  dualModeSelector.setSelectedId(1);
  dualModeSelector.onChange = [this]() {
    processor.setDualMode(static_cast<BreadbinProcessor::DualMode>(
        dualModeSelector.getSelectedId() - 1));
  };
  addAndMakeVisible(dualModeSelector);

  // Time Machine (aging)
  agingLabel.setText("Time Machine", juce::dontSendNotification);
  agingLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(agingLabel);

  agingStartLabel.setText("1982", juce::dontSendNotification);
  agingStartLabel.setColour(juce::Label::textColourId,
                            juce::Colours::lightgrey);
  agingStartLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(agingStartLabel);

  agingEndLabel.setText("NOW", juce::dontSendNotification);
  agingEndLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  agingEndLabel.setFont(juce::Font(10.0f));
  addAndMakeVisible(agingEndLabel);

  agingSlider.setRange(0.0, 1.0, 0.01);
  agingSlider.setValue(0.0);
  agingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  agingSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  agingSlider.onValueChange = [this]() {
    processor.setAgingFactor(static_cast<float>(agingSlider.getValue()));
  };
  addAndMakeVisible(agingSlider);

  // Chip selectors - per SID
  leftChipLabel.setText("L Chip:", juce::dontSendNotification);
  leftChipLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(leftChipLabel);

  leftChipSelector.addItem("6581", 1);
  leftChipSelector.addItem("8580", 2);
  leftChipSelector.setSelectedId(1);
  leftChipSelector.onChange = [this]() {
    processor.setLeftChipModel(leftChipSelector.getSelectedId() == 1
                                   ? SIDEngine::ChipModel::MOS6581
                                   : SIDEngine::ChipModel::MOS8580);
  };
  addAndMakeVisible(leftChipSelector);

  rightChipLabel.setText("R Chip:", juce::dontSendNotification);
  rightChipLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(rightChipLabel);

  rightChipSelector.addItem("6581", 1);
  rightChipSelector.addItem("8580", 2);
  rightChipSelector.setSelectedId(1);
  rightChipSelector.onChange = [this]() {
    processor.setRightChipModel(rightChipSelector.getSelectedId() == 1
                                    ? SIDEngine::ChipModel::MOS6581
                                    : SIDEngine::ChipModel::MOS8580);
  };
  addAndMakeVisible(rightChipSelector);

  // Preset selector
  presetLabel.setText("Preset:", juce::dontSendNotification);
  presetLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(presetLabel);

  presetSelector.addItem("Custom", 1);
  presetSelector.addItem("Classic Lead", 2);
  presetSelector.addItem("Fat Bass", 3);
  presetSelector.addItem("Arpeggio", 4);
  presetSelector.addItem("Warm Pad", 5);
  presetSelector.setSelectedId(1);
  presetSelector.onChange = [this]() {
    if (presetSelector.getSelectedId() > 1)
      applyPreset(presetSelector.getSelectedId());
  };
  addAndMakeVisible(presetSelector);

  // Virtual keyboard
  keyboard.setKeyWidth(18.0f);
  addAndMakeVisible(keyboard);
}

void BreadbinEditor::setupVoiceSelector() {
  voiceSelectorLabel.setText("Voice:", juce::dontSendNotification);
  voiceSelectorLabel.setColour(juce::Label::textColourId,
                               juce::Colours::lightgrey);
  addAndMakeVisible(voiceSelectorLabel);

  for (int i = 0; i < 6; ++i) {
    voiceButtons[i].setButtonText(juce::String(i + 1));
    voiceButtons[i].setClickingTogglesState(false);
    voiceButtons[i].onClick = [this, i]() { selectVoice(i); };
    addAndMakeVisible(voiceButtons[i]);
  }
}

void BreadbinEditor::setupVoiceControls() {
  // Waveform
  waveformLabel.setText("Wave:", juce::dontSendNotification);
  waveformLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(waveformLabel);

  waveformSelector.addItem("Triangle", 1);
  waveformSelector.addItem("Sawtooth", 2);
  waveformSelector.addItem("Pulse", 3);
  waveformSelector.addItem("Noise", 4);
  waveformSelector.setSelectedId(1);
  waveformSelector.onChange = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(waveformSelector);

  // Pulse Width
  pwLabel.setText("PW:", juce::dontSendNotification);
  pwLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(pwLabel);

  pulseWidthSlider.setRange(0, 4095, 1);
  pulseWidthSlider.setValue(2048);
  pulseWidthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  pulseWidthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  pulseWidthSlider.onValueChange = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(pulseWidthSlider);

  // ADSR
  auto setupADSR = [this](juce::Slider &slider, juce::Label &label,
                          const juce::String &text) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(juce::Font(10.0f));
    addAndMakeVisible(label);

    slider.setRange(0, 15, 1);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.onValueChange = [this]() { saveUIToVoice(selectedVoice); };
    addAndMakeVisible(slider);
  };

  attackSlider.setValue(0);
  decaySlider.setValue(0);
  sustainSlider.setValue(15);
  releaseSlider.setValue(0);
  setupADSR(attackSlider, attackLabel, "A");
  setupADSR(decaySlider, decayLabel, "D");
  setupADSR(sustainSlider, sustainLabel, "S");
  setupADSR(releaseSlider, releaseLabel, "R");

  // Pan
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

void BreadbinEditor::setupFilterControls() {
  auto setupFilter = [this](juce::Slider &cutoff, juce::Slider &res,
                            juce::ToggleButton &lp, juce::ToggleButton &bp,
                            juce::ToggleButton &hp, juce::Label &filterLabel,
                            juce::Label &cutLabel, juce::Label &resLabel,
                            SIDEngine &sid, const juce::String &sidName) {
    filterLabel.setText(sidName + " Filter", juce::dontSendNotification);
    filterLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    filterLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(filterLabel);

    cutLabel.setText("Cut", juce::dontSendNotification);
    cutLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    cutLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(cutLabel);

    resLabel.setText("Res", juce::dontSendNotification);
    resLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    resLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(resLabel);

    cutoff.setRange(0, 2047, 1);
    cutoff.setValue(1024);
    cutoff.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    cutoff.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    cutoff.onValueChange = [&sid, &cutoff]() {
      sid.setFilterCutoff(static_cast<int>(cutoff.getValue()));
    };
    addAndMakeVisible(cutoff);

    res.setRange(0, 15, 1);
    res.setValue(0);
    res.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    res.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    res.onValueChange = [&sid, &res]() {
      sid.setFilterResonance(static_cast<int>(res.getValue()));
    };
    addAndMakeVisible(res);

    auto setupFilterButton = [this, &sid](juce::ToggleButton &btn) {
      btn.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
      btn.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
      btn.onClick = [this]() { updateFiltersFromUI(); };
      addAndMakeVisible(btn);
    };

    setupFilterButton(lp);
    setupFilterButton(bp);
    setupFilterButton(hp);
    lp.setToggleState(true, juce::dontSendNotification);

    // Enable filter for all 3 voices on this SID
    sid.setFilterVoices(true, true, true);
    sid.setFilterMode(true, false, false);
    sid.setFilterCutoff(1024);
  };

  setupFilter(leftCutoffSlider, leftResonanceSlider, leftLPButton, leftBPButton,
              leftHPButton, leftFilterLabel, leftCutoffLabel,
              leftResonanceLabel, processor.getLeftSID(), "L");
  setupFilter(rightCutoffSlider, rightResonanceSlider, rightLPButton,
              rightBPButton, rightHPButton, rightFilterLabel, rightCutoffLabel,
              rightResonanceLabel, processor.getRightSID(), "R");
}

void BreadbinEditor::selectVoice(int voice) {
  if (voice < 0 || voice > 5)
    return;

  // Save current voice settings before switching
  if (selectedVoice != voice) {
    saveUIToVoice(selectedVoice);
  }

  selectedVoice = voice;

  // Update button appearance
  for (int i = 0; i < 6; ++i) {
    voiceButtons[i].setColour(juce::TextButton::buttonColourId,
                              i == selectedVoice ? juce::Colours::cyan
                                                 : juce::Colours::darkgrey);
  }

  loadVoiceToUI(voice);
}

void BreadbinEditor::loadVoiceToUI(int voice) {
  const auto &settings = processor.getVoiceSettings(voice);

  // Load waveform
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
}

void BreadbinEditor::saveUIToVoice(int voice) {
  auto &settings = processor.getVoiceSettings(voice);

  // Save waveform
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

  // Apply to SID engine
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
    // Dark overlay for readability
    g.setColour(juce::Colour(0, 0, 0).withAlpha(0.6f));
    g.fillRect(getLocalBounds());
  } else {
    g.fillAll(juce::Colour(30, 30, 35));
  }
}

void BreadbinEditor::resized() {
  auto bounds = getLocalBounds().reduced(10);
  const int rowHeight = 35;
  const int padding = 5;

  // Row 1: Title, Mode, Time Machine, Chips
  auto row1 = bounds.removeFromTop(rowHeight);
  titleLabel.setBounds(row1.removeFromLeft(100));
  row1.removeFromLeft(padding);
  modeLabel.setBounds(row1.removeFromLeft(40));
  dualModeSelector.setBounds(row1.removeFromLeft(100));
  row1.removeFromLeft(padding * 2);
  agingLabel.setBounds(row1.removeFromLeft(80));
  agingStartLabel.setBounds(row1.removeFromLeft(30));
  agingSlider.setBounds(row1.removeFromLeft(100));
  agingEndLabel.setBounds(row1.removeFromLeft(30));
  row1.removeFromLeft(padding * 2);
  leftChipLabel.setBounds(row1.removeFromLeft(45));
  leftChipSelector.setBounds(row1.removeFromLeft(60));
  row1.removeFromLeft(padding);
  rightChipLabel.setBounds(row1.removeFromLeft(50));
  rightChipSelector.setBounds(row1.removeFromLeft(60));

  bounds.removeFromTop(padding);

  // Row 2: Preset and Voice Selector
  auto row2 = bounds.removeFromTop(rowHeight);
  presetLabel.setBounds(row2.removeFromLeft(50));
  presetSelector.setBounds(row2.removeFromLeft(110));
  row2.removeFromLeft(padding * 3);
  voiceSelectorLabel.setBounds(row2.removeFromLeft(45));
  for (int i = 0; i < 6; ++i) {
    voiceButtons[i].setBounds(row2.removeFromLeft(45));
    row2.removeFromLeft(2);
  }

  bounds.removeFromTop(padding);

  // Row 3: Voice Controls (Wave, PW, ADSR, Pan)
  auto row3 = bounds.removeFromTop(80);
  waveformLabel.setBounds(row3.removeFromLeft(40));
  waveformSelector.setBounds(row3.removeFromLeft(90));
  row3.removeFromLeft(padding);
  pwLabel.setBounds(row3.removeFromLeft(30));
  pulseWidthSlider.setBounds(row3.removeFromLeft(100));
  row3.removeFromLeft(padding * 2);

  // ADSR - vertical sliders
  const int adsrWidth = 35;
  const int adsrHeight = 70;
  auto adsrSection = row3.removeFromLeft(adsrWidth * 4 + 10);
  auto adsrY = adsrSection.getY();
  attackLabel.setBounds(adsrSection.getX(), adsrY, adsrWidth, 15);
  attackSlider.setBounds(adsrSection.getX(), adsrY + 15, adsrWidth,
                         adsrHeight - 15);
  decayLabel.setBounds(adsrSection.getX() + adsrWidth, adsrY, adsrWidth, 15);
  decaySlider.setBounds(adsrSection.getX() + adsrWidth, adsrY + 15, adsrWidth,
                        adsrHeight - 15);
  sustainLabel.setBounds(adsrSection.getX() + adsrWidth * 2, adsrY, adsrWidth,
                         15);
  sustainSlider.setBounds(adsrSection.getX() + adsrWidth * 2, adsrY + 15,
                          adsrWidth, adsrHeight - 15);
  releaseLabel.setBounds(adsrSection.getX() + adsrWidth * 3, adsrY, adsrWidth,
                         15);
  releaseSlider.setBounds(adsrSection.getX() + adsrWidth * 3, adsrY + 15,
                          adsrWidth, adsrHeight - 15);

  row3.removeFromLeft(padding * 2);
  panLabel.setBounds(row3.removeFromLeft(35));
  panSlider.setBounds(row3.removeFromLeft(120));

  bounds.removeFromTop(padding * 2);

  // Row 4: Filters (L and R side by side)
  auto row4 = bounds.removeFromTop(100);
  const int filterWidth = (row4.getWidth() - padding) / 2;

  // Left filter
  auto leftFilterArea = row4.removeFromLeft(filterWidth);
  leftFilterLabel.setBounds(leftFilterArea.removeFromTop(18));
  auto leftKnobs = leftFilterArea.removeFromTop(50);
  leftCutoffLabel.setBounds(leftKnobs.removeFromLeft(25));
  leftCutoffSlider.setBounds(leftKnobs.removeFromLeft(50));
  leftResonanceLabel.setBounds(leftKnobs.removeFromLeft(25));
  leftResonanceSlider.setBounds(leftKnobs.removeFromLeft(50));
  auto leftModes = leftFilterArea.removeFromTop(25);
  leftLPButton.setBounds(leftModes.removeFromLeft(45));
  leftBPButton.setBounds(leftModes.removeFromLeft(45));
  leftHPButton.setBounds(leftModes.removeFromLeft(45));

  row4.removeFromLeft(padding);

  // Right filter
  auto rightFilterArea = row4;
  rightFilterLabel.setBounds(rightFilterArea.removeFromTop(18));
  auto rightKnobs = rightFilterArea.removeFromTop(50);
  rightCutoffLabel.setBounds(rightKnobs.removeFromLeft(25));
  rightCutoffSlider.setBounds(rightKnobs.removeFromLeft(50));
  rightResonanceLabel.setBounds(rightKnobs.removeFromLeft(25));
  rightResonanceSlider.setBounds(rightKnobs.removeFromLeft(50));
  auto rightModes = rightFilterArea.removeFromTop(25);
  rightLPButton.setBounds(rightModes.removeFromLeft(45));
  rightBPButton.setBounds(rightModes.removeFromLeft(45));
  rightHPButton.setBounds(rightModes.removeFromLeft(45));

  bounds.removeFromTop(padding);

  // Keyboard at bottom
  keyboard.setBounds(bounds.removeFromBottom(70));
}

void BreadbinEditor::applyPreset(int presetId) {
  // Apply preset to all 6 voices
  auto configureVoice = [this](int voice, SIDEngine::Waveform wave, int pw,
                               int a, int d, int s, int r, float pan) {
    auto &settings = processor.getVoiceSettings(voice);
    settings.waveform = wave;
    settings.pulseWidth = pw;
    settings.attack = a;
    settings.decay = d;
    settings.sustain = s;
    settings.release = r;
    settings.pan = pan;
    processor.applyVoiceSettings(voice);
  };

  // Default panning: L voices left, R voices right
  float lPan = -0.5f, rPan = 0.5f;

  switch (presetId) {
  case 2: // Classic Lead
    for (int v = 0; v < 3; ++v)
      configureVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, lPan);
    for (int v = 3; v < 6; ++v)
      configureVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, rPan);
    break;
  case 3: // Fat Bass
    for (int v = 0; v < 3; ++v)
      configureVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, lPan);
    for (int v = 3; v < 6; ++v)
      configureVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, rPan);
    break;
  case 4: // Arpeggio
    for (int v = 0; v < 3; ++v)
      configureVoice(v, SIDEngine::Waveform::Triangle, 2048, 0, 0, 15, 0, lPan);
    for (int v = 3; v < 6; ++v)
      configureVoice(v, SIDEngine::Waveform::Triangle, 2048, 0, 0, 15, 0, rPan);
    break;
  case 5: // Warm Pad
    for (int v = 0; v < 3; ++v)
      configureVoice(v, SIDEngine::Waveform::Triangle, 2048, 8, 8, 10, 10,
                     lPan);
    for (int v = 3; v < 6; ++v)
      configureVoice(v, SIDEngine::Waveform::Triangle, 2048, 8, 8, 10, 10,
                     rPan);
    break;
  }

  // Reload current voice into UI
  loadVoiceToUI(selectedVoice);
}
