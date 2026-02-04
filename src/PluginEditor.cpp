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

  // Presets
  presetLabel.setText("Preset:", juce::dontSendNotification);
  addAndMakeVisible(presetLabel);

  presetSelector.addItem("-- Select --", 1);
  presetSelector.addItem("Classic Lead (Monty)", 2);
  presetSelector.addItem("Fat Bass (Ocean)", 3);
  presetSelector.addItem("PWM Pad (Hubbard)", 4);
  presetSelector.addItem("Noise Snare", 5);
  presetSelector.setSelectedId(1);
  presetSelector.onChange = [this]() {
    applyPreset(presetSelector.getSelectedId());
  };
  addAndMakeVisible(presetSelector);

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
  // Waveform selector
  waveformLabel.setText("Waveform:", juce::dontSendNotification);
  addAndMakeVisible(waveformLabel);

  waveformSelector.addItem("Triangle", 1);
  waveformSelector.addItem("Sawtooth", 2);
  waveformSelector.addItem("Pulse", 3);
  waveformSelector.addItem("Noise", 4);
  waveformSelector.setSelectedId(1);
  waveformSelector.onChange = [this]() { updateSynthFromControls(); };
  addAndMakeVisible(waveformSelector);

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

  // ADSR with labels
  setupLabel(attackLabel, "Attack");
  setupSlider(attackSlider, 0, 15, 8, 36);

  setupLabel(decayLabel, "Decay");
  setupSlider(decaySlider, 0, 15, 8, 36);

  setupLabel(sustainLabel, "Sustain");
  setupSlider(sustainSlider, 0, 15, 12, 36);

  setupLabel(releaseLabel, "Release");
  setupSlider(releaseSlider, 0, 15, 8, 36);

  // Pulse width
  setupLabel(pulseWidthLabel, "PW");
  setupSlider(pulseWidthSlider, 0, 4095, 2048, 50);
}

void BreadbinEditor::setupFilterControls() {
  filterLabel.setText("Filter:", juce::dontSendNotification);
  addAndMakeVisible(filterLabel);

  // Helper to setup label (same as synth controls)
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

  setupLabel(cutoffLabel, "Cutoff");
  setupSlider(filterCutoffSlider, 0, 2047, 1024, 50);

  setupLabel(resonanceLabel, "Reso");
  setupSlider(filterResonanceSlider, 0, 15, 8, 36);

  filterLPButton.onClick = [this]() { updateSynthFromControls(); };
  filterBPButton.onClick = [this]() { updateSynthFromControls(); };
  filterHPButton.onClick = [this]() { updateSynthFromControls(); };
  filterLPButton.setToggleState(true, juce::dontSendNotification);
  addAndMakeVisible(filterLPButton);
  addAndMakeVisible(filterBPButton);
  addAndMakeVisible(filterHPButton);
}

void BreadbinEditor::updateSynthFromControls() {
  auto &sid = processor.getLeftSID();
  auto &sidR = processor.getRightSID();

  // Map combobox ID (1-4) to actual SID waveform values
  SIDEngine::Waveform waveform;
  switch (waveformSelector.getSelectedId()) {
  case 1:
    waveform = SIDEngine::Waveform::Triangle;
    break;
  case 2:
    waveform = SIDEngine::Waveform::Sawtooth;
    break;
  case 3:
    waveform = SIDEngine::Waveform::Pulse;
    break;
  case 4:
    waveform = SIDEngine::Waveform::Noise;
    break;
  default:
    waveform = SIDEngine::Waveform::Triangle;
    break;
  }
  for (int v = 0; v < 3; ++v) {
    sid.setWaveform(v, waveform);
    sidR.setWaveform(v, waveform);
  }

  // ADSR
  int attack = static_cast<int>(attackSlider.getValue());
  int decay = static_cast<int>(decaySlider.getValue());
  int sustain = static_cast<int>(sustainSlider.getValue());
  int release = static_cast<int>(releaseSlider.getValue());
  for (int v = 0; v < 3; ++v) {
    sid.setAttack(v, attack);
    sid.setDecay(v, decay);
    sid.setSustain(v, sustain);
    sid.setRelease(v, release);
    sidR.setAttack(v, attack);
    sidR.setDecay(v, decay);
    sidR.setSustain(v, sustain);
    sidR.setRelease(v, release);
  }

  // Pulse width
  int pw = static_cast<int>(pulseWidthSlider.getValue());
  for (int v = 0; v < 3; ++v) {
    sid.setPulseWidth(v, pw);
    sidR.setPulseWidth(v, pw);
  }

  // Filter
  sid.setFilterCutoff(static_cast<int>(filterCutoffSlider.getValue()));
  sid.setFilterResonance(static_cast<int>(filterResonanceSlider.getValue()));
  sid.setFilterMode(filterLPButton.getToggleState(),
                    filterBPButton.getToggleState(),
                    filterHPButton.getToggleState());
  sid.setFilterVoices(true, true, true);

  sidR.setFilterCutoff(static_cast<int>(filterCutoffSlider.getValue()));
  sidR.setFilterResonance(static_cast<int>(filterResonanceSlider.getValue()));
  sidR.setFilterMode(filterLPButton.getToggleState(),
                     filterBPButton.getToggleState(),
                     filterHPButton.getToggleState());
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
  presetLabel.setBounds(modeRow.removeFromLeft(50));
  presetSelector.setBounds(modeRow.removeFromLeft(140));

  bounds.removeFromTop(10);

  // Synth controls row - labels above sliders
  auto synthRow = bounds.removeFromTop(100);
  waveformLabel.setBounds(synthRow.removeFromLeft(70).removeFromTop(20));
  synthRow.removeFromLeft(-70); // Reset position
  auto waveformArea = synthRow.removeFromLeft(70);
  waveformLabel.setBounds(waveformArea.removeFromTop(20));
  waveformSelector.setBounds(waveformArea.removeFromTop(30));

  synthRow.removeFromLeft(15);

  // ADSR sliders with labels above
  auto adsrWidth = 55;
  auto sliderHeight = 70;

  auto attackArea = synthRow.removeFromLeft(adsrWidth);
  attackLabel.setBounds(attackArea.removeFromTop(16));
  attackSlider.setBounds(attackArea.removeFromTop(sliderHeight));

  auto decayArea = synthRow.removeFromLeft(adsrWidth);
  decayLabel.setBounds(decayArea.removeFromTop(16));
  decaySlider.setBounds(decayArea.removeFromTop(sliderHeight));

  auto sustainArea = synthRow.removeFromLeft(adsrWidth);
  sustainLabel.setBounds(sustainArea.removeFromTop(16));
  sustainSlider.setBounds(sustainArea.removeFromTop(sliderHeight));

  auto releaseArea = synthRow.removeFromLeft(adsrWidth);
  releaseLabel.setBounds(releaseArea.removeFromTop(16));
  releaseSlider.setBounds(releaseArea.removeFromTop(sliderHeight));

  synthRow.removeFromLeft(10);

  auto pwArea = synthRow.removeFromLeft(adsrWidth);
  pulseWidthLabel.setBounds(pwArea.removeFromTop(16));
  pulseWidthSlider.setBounds(pwArea.removeFromTop(sliderHeight));

  bounds.removeFromTop(8);

  // Filter row - labels above sliders
  auto filterRow = bounds.removeFromTop(100);
  filterLabel.setBounds(filterRow.removeFromLeft(50).removeFromTop(20));
  filterRow.removeFromLeft(-50);
  auto filterLabelArea = filterRow.removeFromLeft(50);
  filterLabel.setBounds(filterLabelArea.removeFromTop(20));
  filterRow.removeFromLeft(10);

  auto cutoffArea = filterRow.removeFromLeft(65);
  cutoffLabel.setBounds(cutoffArea.removeFromTop(16));
  filterCutoffSlider.setBounds(cutoffArea.removeFromTop(sliderHeight));

  auto resoArea = filterRow.removeFromLeft(55);
  resonanceLabel.setBounds(resoArea.removeFromTop(16));
  filterResonanceSlider.setBounds(resoArea.removeFromTop(sliderHeight));

  filterRow.removeFromLeft(10);
  auto buttonHeight = 25;
  auto buttonArea = filterRow.removeFromLeft(120);
  buttonArea.removeFromTop(30);
  filterLPButton.setBounds(
      buttonArea.removeFromLeft(40).removeFromTop(buttonHeight));
  filterBPButton.setBounds(
      buttonArea.removeFromLeft(40).removeFromTop(buttonHeight));
  filterHPButton.setBounds(
      buttonArea.removeFromLeft(40).removeFromTop(buttonHeight));

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

  switch (presetId) {
  case 2: // Classic Lead (Monty on the Run style)
    // Sawtooth, short attack, medium decay, high sustain
    waveformSelector.setSelectedId(2); // Sawtooth
    attackSlider.setValue(0);
    decaySlider.setValue(6);
    sustainSlider.setValue(12);
    releaseSlider.setValue(4);
    pulseWidthSlider.setValue(2048);
    filterCutoffSlider.setValue(1400);
    filterResonanceSlider.setValue(6);
    filterLPButton.setToggleState(true, juce::dontSendNotification);
    filterBPButton.setToggleState(false, juce::dontSendNotification);
    filterHPButton.setToggleState(false, juce::dontSendNotification);
    break;

  case 3: // Fat Bass (Ocean Loader style)
    // Pulse wave, punchy envelope
    waveformSelector.setSelectedId(3); // Pulse
    attackSlider.setValue(0);
    decaySlider.setValue(8);
    sustainSlider.setValue(6);
    releaseSlider.setValue(2);
    pulseWidthSlider.setValue(1024); // 25% duty cycle
    filterCutoffSlider.setValue(600);
    filterResonanceSlider.setValue(10);
    filterLPButton.setToggleState(true, juce::dontSendNotification);
    filterBPButton.setToggleState(false, juce::dontSendNotification);
    filterHPButton.setToggleState(false, juce::dontSendNotification);
    break;

  case 4: // PWM Pad (Hubbard style)
    // Triangle + Pulse combo, slow attack
    waveformSelector.setSelectedId(3); // Pulse for PWM
    attackSlider.setValue(10);
    decaySlider.setValue(4);
    sustainSlider.setValue(14);
    releaseSlider.setValue(8);
    pulseWidthSlider.setValue(2048);
    filterCutoffSlider.setValue(1800);
    filterResonanceSlider.setValue(4);
    filterLPButton.setToggleState(true, juce::dontSendNotification);
    filterBPButton.setToggleState(false, juce::dontSendNotification);
    filterHPButton.setToggleState(false, juce::dontSendNotification);
    break;

  case 5:                              // Noise Snare
    waveformSelector.setSelectedId(4); // Noise
    attackSlider.setValue(0);
    decaySlider.setValue(6);
    sustainSlider.setValue(0);
    releaseSlider.setValue(4);
    filterCutoffSlider.setValue(1200);
    filterResonanceSlider.setValue(2);
    filterLPButton.setToggleState(false, juce::dontSendNotification);
    filterBPButton.setToggleState(true, juce::dontSendNotification);
    filterHPButton.setToggleState(false, juce::dontSendNotification);
    break;

  default:
    return; // "-- Select --" does nothing
  }

  updateSynthFromControls();
}
