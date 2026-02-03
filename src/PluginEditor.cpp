#include "PluginEditor.h"

BreadbinEditor::BreadbinEditor(BreadbinProcessor &p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {
  setupControls();
  setupSynthControls();
  setupFilterControls();

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
  processor.getMidiMessageCollector().addMessageToQueue(msg);
}

void BreadbinEditor::handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                                   int midiNoteNumber, float velocity) {
  juce::ignoreUnused(velocity);
  auto msg = juce::MidiMessage::noteOff(midiChannel, midiNoteNumber);
  processor.getMidiMessageCollector().addMessageToQueue(msg);
}

void BreadbinEditor::setupControls() {
  // Title
  titleLabel.setText("BREADBIN", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
  titleLabel.setJustificationType(juce::Justification::centred);
  titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7B68EE));
  addAndMakeVisible(titleLabel);

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

  // Chip Model
  modelLabel.setText("Chip:", juce::dontSendNotification);
  addAndMakeVisible(modelLabel);

  chipModelSelector.addItem("MOS 6581 (1982)", 1);
  chipModelSelector.addItem("MOS 8580 (1986)", 2);
  chipModelSelector.setSelectedId(static_cast<int>(processor.getChipModel()) +
                                  1);
  chipModelSelector.onChange = [this]() {
    processor.setChipModel(static_cast<SIDEngine::ChipModel>(
        chipModelSelector.getSelectedId() - 1));
  };
  addAndMakeVisible(chipModelSelector);

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
                                const juce::String &tooltip) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setRange(0.0, 15.0, 1.0);
    slider.setValue(8.0);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setTooltip(tooltip);
    slider.onValueChange = [this]() { updateSynthFromControls(); };
    addAndMakeVisible(slider);
  };

  setupADSRSlider(attackSlider, "Attack (0-15)");
  setupADSRSlider(decaySlider, "Decay (0-15)");
  sustainSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  sustainSlider.setRange(0.0, 15.0, 1.0);
  sustainSlider.setValue(12.0);
  sustainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  sustainSlider.setTooltip("Sustain (0-15)");
  sustainSlider.onValueChange = [this]() { updateSynthFromControls(); };
  addAndMakeVisible(sustainSlider);

  setupADSRSlider(releaseSlider, "Release (0-15)");

  // Pulse width
  pulseWidthSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  pulseWidthSlider.setRange(0, 4095, 1);
  pulseWidthSlider.setValue(2048);
  pulseWidthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  pulseWidthSlider.setTooltip("Pulse Width");
  pulseWidthSlider.onValueChange = [this]() { updateSynthFromControls(); };
  addAndMakeVisible(pulseWidthSlider);
}

void BreadbinEditor::setupFilterControls() {
  filterLabel.setText("Filter:", juce::dontSendNotification);
  addAndMakeVisible(filterLabel);

  filterCutoffSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  filterCutoffSlider.setRange(0, 2047, 1);
  filterCutoffSlider.setValue(1024);
  filterCutoffSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  filterCutoffSlider.setTooltip("Cutoff");
  filterCutoffSlider.onValueChange = [this]() { updateSynthFromControls(); };
  addAndMakeVisible(filterCutoffSlider);

  filterResonanceSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  filterResonanceSlider.setRange(0, 15, 1);
  filterResonanceSlider.setValue(8);
  filterResonanceSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  filterResonanceSlider.setTooltip("Resonance");
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

  // Waveform for all voices
  auto waveform =
      static_cast<SIDEngine::Waveform>(waveformSelector.getSelectedId() - 1);
  for (int v = 0; v < 3; ++v)
    sid.setWaveform(v, waveform);

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
  }

  // Pulse width
  int pw = static_cast<int>(pulseWidthSlider.getValue());
  for (int v = 0; v < 3; ++v)
    sid.setPulseWidth(v, pw);

  // Filter
  sid.setFilterCutoff(static_cast<int>(filterCutoffSlider.getValue()));
  sid.setFilterResonance(static_cast<int>(filterResonanceSlider.getValue()));
  sid.setFilterMode(filterLPButton.getToggleState(),
                    filterBPButton.getToggleState(),
                    filterHPButton.getToggleState());
  sid.setFilterVoices(true, true, true);

  // Apply same to right SID
  auto &sidR = processor.getRightSID();
  for (int v = 0; v < 3; ++v) {
    sidR.setWaveform(v, waveform);
    sidR.setAttack(v, attack);
    sidR.setDecay(v, decay);
    sidR.setSustain(v, sustain);
    sidR.setRelease(v, release);
    sidR.setPulseWidth(v, pw);
  }
  sidR.setFilterCutoff(static_cast<int>(filterCutoffSlider.getValue()));
  sidR.setFilterResonance(static_cast<int>(filterResonanceSlider.getValue()));
  sidR.setFilterMode(filterLPButton.getToggleState(),
                     filterBPButton.getToggleState(),
                     filterHPButton.getToggleState());
  sidR.setFilterVoices(true, true, true);
}

void BreadbinEditor::paint(juce::Graphics &g) {
  // C64-inspired dark blue gradient
  g.setGradientFill(juce::ColourGradient(
      juce::Colour(0xFF1A1A40), 0, 0, juce::Colour(0xFF2D2D6A), 0,
      static_cast<float>(getHeight()), false));
  g.fillAll();

  // Subtitle
  g.setColour(juce::Colour(0xFF8888CC));
  g.setFont(12.0f);
  g.drawText("C64 Dual SID Synthesizer",
             getLocalBounds().removeFromTop(70).removeFromBottom(20),
             juce::Justification::centred);
}

void BreadbinEditor::resized() {
  auto bounds = getLocalBounds().reduced(15);

  // Title area
  titleLabel.setBounds(bounds.removeFromTop(40));
  bounds.removeFromTop(10);

  // Mode row
  auto modeRow = bounds.removeFromTop(25);
  modeLabel.setBounds(modeRow.removeFromLeft(50));
  dualModeSelector.setBounds(modeRow.removeFromLeft(120));
  modeRow.removeFromLeft(20);
  modelLabel.setBounds(modeRow.removeFromLeft(40));
  chipModelSelector.setBounds(modeRow.removeFromLeft(130));

  bounds.removeFromTop(10);

  // Synth controls row
  auto synthRow = bounds.removeFromTop(70);
  waveformLabel.setBounds(synthRow.removeFromLeft(70));
  waveformSelector.setBounds(synthRow.removeFromLeft(100));
  synthRow.removeFromLeft(10);

  adsrLabel.setBounds(synthRow.removeFromLeft(40));
  attackSlider.setBounds(synthRow.removeFromLeft(50));
  decaySlider.setBounds(synthRow.removeFromLeft(50));
  sustainSlider.setBounds(synthRow.removeFromLeft(50));
  releaseSlider.setBounds(synthRow.removeFromLeft(50));
  pulseWidthSlider.setBounds(synthRow.removeFromLeft(50));

  bounds.removeFromTop(5);

  // Filter row
  auto filterRow = bounds.removeFromTop(70);
  filterLabel.setBounds(filterRow.removeFromLeft(50));
  filterCutoffSlider.setBounds(filterRow.removeFromLeft(60));
  filterResonanceSlider.setBounds(filterRow.removeFromLeft(60));
  filterLPButton.setBounds(filterRow.removeFromLeft(40));
  filterBPButton.setBounds(filterRow.removeFromLeft(40));
  filterHPButton.setBounds(filterRow.removeFromLeft(40));

  bounds.removeFromTop(10);

  // Aging slider
  agingLabel.setBounds(bounds.removeFromTop(20));
  agingSlider.setBounds(bounds.removeFromTop(25).reduced(40, 0));

  bounds.removeFromTop(15);

  // Keyboard at bottom
  keyboard.setBounds(bounds.removeFromBottom(80));
}
