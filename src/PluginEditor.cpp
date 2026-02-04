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

  // Aging slider
  agingLabel.setText("Time Machine", juce::dontSendNotification);
  agingLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(agingLabel);

  agingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  agingSlider.setRange(0.0, 1.0, 0.01);
  agingSlider.setValue(processor.getAgingFactor());
  agingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
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

  // ADSR controls
  adsrLabel.setText("ADSR:", juce::dontSendNotification);
  addAndMakeVisible(adsrLabel);

  auto setupADSRSlider = [this](juce::Slider &slider,
                                const juce::String &name) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setRange(0.0, 15.0, 1.0);
    slider.setValue(8.0);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 14);
    slider.setName(name);
    slider.onValueChange = [this]() { updateSynthFromControls(); };
    addAndMakeVisible(slider);
  };

  setupADSRSlider(attackSlider, "A");
  setupADSRSlider(decaySlider, "D");
  setupADSRSlider(sustainSlider, "S");
  sustainSlider.setValue(12.0); // Override default
  setupADSRSlider(releaseSlider, "R");

  // Pulse width
  pulseWidthSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  pulseWidthSlider.setRange(0, 4095, 1);
  pulseWidthSlider.setValue(2048);
  pulseWidthSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 14);
  pulseWidthSlider.setName("PW");
  pulseWidthSlider.onValueChange = [this]() { updateSynthFromControls(); };
  addAndMakeVisible(pulseWidthSlider);
}

void BreadbinEditor::setupFilterControls() {
  filterLabel.setText("Filter:", juce::dontSendNotification);
  addAndMakeVisible(filterLabel);

  filterCutoffSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  filterCutoffSlider.setRange(0, 2047, 1);
  filterCutoffSlider.setValue(1024);
  filterCutoffSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
  filterCutoffSlider.setName("Cutoff");
  filterCutoffSlider.onValueChange = [this]() { updateSynthFromControls(); };
  addAndMakeVisible(filterCutoffSlider);

  filterResonanceSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  filterResonanceSlider.setRange(0, 15, 1);
  filterResonanceSlider.setValue(8);
  filterResonanceSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50,
                                        14);
  filterResonanceSlider.setName("Reso");
  filterResonanceSlider.onValueChange = [this]() { updateSynthFromControls(); };
  addAndMakeVisible(filterResonanceSlider);

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

  // Synth controls row - larger controls
  auto synthRow = bounds.removeFromTop(90);
  waveformLabel.setBounds(synthRow.removeFromLeft(70));
  waveformSelector.setBounds(synthRow.removeFromLeft(100));
  synthRow.removeFromLeft(15);

  adsrLabel.setBounds(synthRow.removeFromLeft(45));
  attackSlider.setBounds(synthRow.removeFromLeft(60));
  decaySlider.setBounds(synthRow.removeFromLeft(60));
  sustainSlider.setBounds(synthRow.removeFromLeft(60));
  releaseSlider.setBounds(synthRow.removeFromLeft(60));
  synthRow.removeFromLeft(10);
  pulseWidthSlider.setBounds(synthRow.removeFromLeft(60));

  bounds.removeFromTop(8);

  // Filter row - larger controls
  auto filterRow = bounds.removeFromTop(90);
  filterLabel.setBounds(filterRow.removeFromLeft(50));
  filterCutoffSlider.setBounds(filterRow.removeFromLeft(70));
  filterResonanceSlider.setBounds(filterRow.removeFromLeft(70));
  filterRow.removeFromLeft(10);
  filterLPButton.setBounds(filterRow.removeFromLeft(45));
  filterBPButton.setBounds(filterRow.removeFromLeft(45));
  filterHPButton.setBounds(filterRow.removeFromLeft(45));

  bounds.removeFromTop(5);

  // Aging slider
  agingLabel.setBounds(bounds.removeFromTop(20));
  agingSlider.setBounds(bounds.removeFromTop(25).reduced(40, 0));

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
