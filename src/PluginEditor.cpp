#include "PluginEditor.h"
#include "BinaryData.h"

BreadbinEditor::BreadbinEditor(BreadbinProcessor &p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {

  backgroundImage = juce::ImageFileFormat::loadFrom(
      BinaryData::background_jpg, BinaryData::background_jpgSize);

  // Load retro font (Press Start 2P) from binary assets
  auto retroTypeface = juce::Typeface::createSystemTypefaceFor(
      BinaryData::PressStart2PRegular_ttf,
      BinaryData::PressStart2PRegular_ttfSize);
  retroFont = juce::Font(juce::FontOptions(retroTypeface).withHeight(10.0f));

  // Load professional font (Roboto Bold) from binary assets
  auto proTypeface = juce::Typeface::createSystemTypefaceFor(
      BinaryData::RobotoBold_ttf, BinaryData::RobotoBold_ttfSize);
  proFont = juce::Font(juce::FontOptions(proTypeface).withHeight(12.0f));

  // Setup custom look and feel with Roboto Bold font for ComboBoxes
  customLookAndFeel.setProFont(proFont);
  setLookAndFeel(&customLookAndFeel);

  keyboardState.addListener(this);
  processor.getMidiMessageCollector().reset(p.getSampleRate());

  setupControls();
  setupLeftSID();
  setupRightSID();
  setupVoiceEditor();

  // Apply Init preset to initialize all voices
  applyGlobalPreset(1);
  selectVoice(0);
  setSize(700, 500);
  setResizable(true, true);
  setResizeLimits(600, 400, 1200, 800);
}

BreadbinEditor::~BreadbinEditor() {
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

  globalPresetSelector.addItem("Init", 1);
  globalPresetSelector.addItem("Dual Lead", 2);
  globalPresetSelector.addItem("Pad Stack", 3);
  globalPresetSelector.addItem("Arpeggiated", 4);
  globalPresetSelector.addItem("Fat Unison", 5);
  globalPresetSelector.addItem("Retro Synth", 6);
  globalPresetSelector.setSelectedId(1);
  globalPresetSelector.setTooltip("Factory presets - applies to entire plugin");
  globalPresetSelector.onChange = [this]() {
    applyGlobalPreset(globalPresetSelector.getSelectedId());
  };
  addAndMakeVisible(globalPresetSelector);

  // Voice Preset (for selected voice)
  presetLabel.setText("Voice:", juce::dontSendNotification);
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

  // Save/Load preset buttons (save full state)
  savePresetButton.setTooltip("Save all settings to a .breadbin file");
  savePresetButton.onClick = [this]() { savePresetToFile(); };
  addAndMakeVisible(savePresetButton);

  loadPresetButton.setTooltip("Load settings from a .breadbin file");
  loadPresetButton.onClick = [this]() { loadPresetFromFile(); };
  addAndMakeVisible(loadPresetButton);

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
  arpEnableButton.setToggleState(processor.isArpEnabled(),
                                 juce::dontSendNotification);
  arpEnableButton.onClick = [this]() {
    processor.setArpEnabled(arpEnableButton.getToggleState());
  };
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

  // Pitch Bend Range
  pitchBendRangeLabel.setText("PB Range", juce::dontSendNotification);
  pitchBendRangeLabel.setColour(juce::Label::textColourId,
                                juce::Colours::lightgrey);
  pitchBendRangeLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
  addAndMakeVisible(pitchBendRangeLabel);

  pitchBendRangeSelector.addItem("+/-2", 2);
  pitchBendRangeSelector.addItem("+/-3", 3);
  pitchBendRangeSelector.addItem("+/-5", 5);
  pitchBendRangeSelector.addItem("+/-7", 7);
  pitchBendRangeSelector.addItem("+/-12", 12);
  pitchBendRangeSelector.setSelectedId(processor.getPitchBendRange(),
                                       juce::dontSendNotification);
  pitchBendRangeSelector.setTooltip("Pitch bend range in semitones");
  pitchBendRangeSelector.onChange = [this]() {
    processor.setPitchBendRange(pitchBendRangeSelector.getSelectedId());
  };
  addAndMakeVisible(pitchBendRangeSelector);

  // External Audio Input
  extInputEnableButton.setToggleState(processor.isExtInputEnabled(),
                                      juce::dontSendNotification);
  extInputEnableButton.onClick = [this]() {
    processor.setExtInputEnabled(extInputEnableButton.getToggleState());
  };
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

  // ===== LFO =====
  lfoEnableButton.setTooltip("Global LFO: Toggle all LFO modulations");
  lfoEnableButton.setButtonText("LFO");
  lfoEnableButton.setToggleState(processor.getLFO().enabled,
                                 juce::dontSendNotification);
  lfoEnableButton.onClick = [this]() {
    processor.getLFO().enabled = lfoEnableButton.getToggleState();
  };
  addAndMakeVisible(lfoEnableButton);

  lfoWaveformSelector.addItem("Tri", 1);
  lfoWaveformSelector.addItem("Saw", 2);
  lfoWaveformSelector.addItem("Sq", 3);
  lfoWaveformSelector.addItem("S&H", 4);
  lfoWaveformSelector.setSelectedId(
      static_cast<int>(processor.getLFO().waveform) + 1);
  lfoWaveformSelector.setTooltip(
      "LFO Shape: Tri (Smooth), Saw (Ramp), Sq (Hard), S&H (Random)");
  lfoWaveformSelector.onChange = [this]() {
    processor.getLFO().waveform = static_cast<BreadbinProcessor::LFOWaveform>(
        lfoWaveformSelector.getSelectedId() - 1);
  };
  addAndMakeVisible(lfoWaveformSelector);

  lfoRateLabel.setText("Hz", juce::dontSendNotification);
  lfoRateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  lfoRateLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(lfoRateLabel);

  lfoRateSlider.setRange(0.1, 20.0, 0.1);
  lfoRateSlider.setValue(processor.getLFO().rate);
  lfoRateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  lfoRateSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  lfoRateSlider.setTooltip("LFO Rate: Adjust modulation speed (0.1Hz - 20Hz)");
  lfoRateSlider.onValueChange = [this]() {
    processor.getLFO().rate = static_cast<float>(lfoRateSlider.getValue());
  };
  addAndMakeVisible(lfoRateSlider);

  lfoTargetLabel.setText("Mod Target", juce::dontSendNotification);
  lfoTargetLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  lfoTargetLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(lfoTargetLabel);

  lfoTargetSelector.addItem("Filter", 1);
  lfoTargetSelector.addItem("PWM", 2);
  lfoTargetSelector.addItem("Pitch", 3);
  lfoTargetSelector.setSelectedId(static_cast<int>(processor.getLFO().target) +
                                  1);
  lfoTargetSelector.onChange = [this]() {
    processor.getLFO().target = static_cast<BreadbinProcessor::LFOTarget>(
        lfoTargetSelector.getSelectedId() - 1);
  };
  lfoTargetSelector.setTooltip(
      "Mod Target: Select which parameter the LFO modulates");
  addAndMakeVisible(lfoTargetSelector);

  lfoDepthLabel.setText("Depth", juce::dontSendNotification);
  lfoDepthLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  lfoDepthLabel.setFont(retroFont.withHeight(7.0f));
  lfoDepthLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(lfoDepthLabel);

  lfoDepthSlider.setRange(0.0, 1.0, 0.01);
  lfoDepthSlider.setValue(processor.getLFO().depth);
  lfoDepthSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  lfoDepthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  lfoDepthSlider.setTooltip("LFO Depth: Adjust modulation intensity");
  lfoDepthSlider.onValueChange = [this]() {
    processor.getLFO().depth = static_cast<float>(lfoDepthSlider.getValue());
  };
  addAndMakeVisible(lfoDepthSlider);

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
    processor.getLeftSID().setFilterCutoff(
        static_cast<int>(leftCutoffSlider.getValue()));
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
    processor.getRightSID().setFilterCutoff(
        static_cast<int>(rightCutoffSlider.getValue()));
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

  panLabel.setText("Pan:", juce::dontSendNotification);
  panLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(panLabel);

  panSlider.setRange(-1.0, 1.0, 0.01);
  panSlider.setValue(0.0);
  panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  panSlider.setTooltip("Pan: Left (-1) to Right (+1)");
  panSlider.onValueChange = [this]() { saveUIToVoice(selectedVoice); };
  addAndMakeVisible(panSlider);

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
  panSlider.setValue(settings.pan, juce::dontSendNotification);
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
  settings.pan = static_cast<float>(panSlider.getValue());

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
  auto bounds = getLocalBounds().reduced(8);
  const int rowH = 28;
  const int pad = 4;

  // ===== TOP ROW: Mode, Global Preset, Voice Preset, Save/Load =====
  auto topRow = bounds.removeFromTop(rowH);
  modeLabel.setBounds(topRow.removeFromLeft(35));
  dualModeSelector.setBounds(topRow.removeFromLeft(100));
  topRow.removeFromLeft(pad * 2);
  globalPresetLabel.setBounds(topRow.removeFromLeft(40));
  globalPresetSelector.setBounds(topRow.removeFromLeft(100));
  topRow.removeFromLeft(pad * 2);
  presetLabel.setBounds(topRow.removeFromLeft(40));
  presetSelector.setBounds(topRow.removeFromLeft(145));
  topRow.removeFromLeft(pad);
  savePresetButton.setBounds(topRow.removeFromLeft(55));
  topRow.removeFromLeft(pad);
  loadPresetButton.setBounds(topRow.removeFromLeft(55));

  bounds.removeFromTop(pad * 2);

  // ===== SID PANELS: Left and Right side by side =====
  auto sidRow = bounds.removeFromTop(160);
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
  leftCutoffSlider.setBounds(leftFilterRow.removeFromLeft(45));
  leftResonanceLabel.setBounds(leftFilterRow.removeFromLeft(30));
  leftResonanceSlider.setBounds(leftFilterRow.removeFromLeft(45));

  auto leftModesRow = leftPanel.removeFromTop(22);
  leftFilterEnableButton.setBounds(leftModesRow.removeFromLeft(45));
  leftLPButton.setBounds(leftModesRow.removeFromLeft(45));
  leftBPButton.setBounds(leftModesRow.removeFromLeft(45));
  leftHPButton.setBounds(leftModesRow.removeFromLeft(45));

  // Detune
  auto leftDetuneRow = leftPanel.removeFromTop(20);
  leftDetuneLabel.setBounds(leftDetuneRow.removeFromLeft(45));
  leftDetuneSlider.setBounds(leftDetuneRow.removeFromLeft(130));

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
  rightResonanceSlider.setBounds(rightFilterRow.removeFromRight(45));
  rightResonanceLabel.setBounds(rightFilterRow.removeFromRight(30));
  rightCutoffSlider.setBounds(rightFilterRow.removeFromRight(45));
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

  bounds.removeFromTop(pad * 2);

  // ===== VOICE EDITOR =====
  auto editorArea = bounds.removeFromTop(120);
  voiceEditorLabel.setBounds(editorArea.removeFromTop(18));

  // Row 1: Waveform, Pulse Width, ADSR, GLOBAL LFO
  auto row1 = editorArea.removeFromTop(65);
  waveformLabel.setBounds(row1.removeFromLeft(40));
  waveformSelector.setBounds(row1.removeFromLeft(90));
  row1.removeFromLeft(pad);
  pwLabel.setBounds(row1.removeFromLeft(30));
  pulseWidthSlider.setBounds(row1.removeFromLeft(90));
  row1.removeFromLeft(pad * 2);

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

  // Global LFO section at the end of Row 1
  auto lfoArea = row1.removeFromLeft(260);
  const int rowCenterY = row1.getY() + (65 - 20) / 2;
  lfoEnableButton.setBounds(lfoArea.getX(), rowCenterY, 40, 20);
  lfoArea.removeFromLeft(42);
  lfoWaveformSelector.setBounds(lfoArea.getX(), rowCenterY, 55, 20);
  lfoArea.removeFromLeft(57);

  auto lfoRateArea = lfoArea.removeFromLeft(50);
  lfoRateSlider.setBounds(lfoRateArea.removeFromTop(40));
  lfoRateLabel.setBounds(lfoRateArea.getX(), lfoRateArea.getY() - 5, 30, 15);

  lfoArea.removeFromLeft(pad);

  lfoTargetSelector.setBounds(lfoArea.getX(), rowCenterY, 65, 20);
  lfoTargetLabel.setBounds(lfoArea.getX(), rowCenterY - 14, 65, 12);
  lfoArea.removeFromLeft(67 + pad);

  lfoDepthLabel.setBounds(lfoArea.getX(), row1.getY() + 4, 30, 12);
  lfoDepthSlider.setBounds(lfoArea.getX(), row1.getY() + 16, 25, 25);

  // Row 2: Pan, Glide, Ring Mod, Sync
  editorArea.removeFromTop(pad);
  auto row2 = editorArea.removeFromTop(28);
  panLabel.setBounds(row2.removeFromLeft(30));
  panSlider.setBounds(row2.removeFromLeft(120));
  row2.removeFromLeft(pad * 3);
  glideTimeLabel.setBounds(row2.removeFromLeft(35));
  glideTimeSlider.setBounds(row2.removeFromLeft(120));
  row2.removeFromLeft(pad * 3);
  ringModButton.setBounds(row2.removeFromLeft(55));
  row2.removeFromLeft(pad);
  syncButton.setBounds(row2.removeFromLeft(55));
  row2.removeFromLeft(pad);
  voiceFilterButton.setBounds(row2.removeFromLeft(45));
  row2.removeFromLeft(pad * 2);
  pitchBendRangeLabel.setBounds(row2.removeFromLeft(55));
  pitchBendRangeSelector.setBounds(row2.removeFromLeft(65));
  row2.removeFromLeft(pad * 2);
  extInputEnableButton.setBounds(row2.removeFromLeft(55));
  row2.removeFromLeft(pad);
  extInputLabel.setBounds(row2.removeFromLeft(35));
  extInputLevelSlider.setBounds(row2.removeFromLeft(100));

  bounds.removeFromTop(pad);

  // ===== KEYBOARD =====
  keyboard.setBounds(bounds.removeFromBottom(60));

  // Add some space above keyboard for the logo (lower-left)
  bounds.removeFromBottom(pad * 2);

  // ===== ARPEGGIATOR ROW (right-justified) =====
  auto arpRow = bounds.removeFromBottom(28);
  arpEnableButton.setBounds(arpRow.removeFromRight(50));
  arpRow.removeFromRight(pad);
  arpOctaveSelector.setBounds(arpRow.removeFromRight(50));
  arpOctaveLabel.setText("Oct", juce::dontSendNotification);
  arpOctaveLabel.setBounds(arpRow.removeFromRight(30));
  arpRow.removeFromRight(pad);
  arpRateSlider.setBounds(arpRow.removeFromRight(100));
  arpRateLabel.setBounds(arpRow.removeFromRight(35));
  arpRow.removeFromRight(pad);
  arpPatternSelector.setBounds(arpRow.removeFromRight(90));
  arpPatternLabel.setText("Arp", juce::dontSendNotification);
  arpPatternLabel.setBounds(arpRow.removeFromRight(30));

  bounds.removeFromBottom(pad);

  // ===== CHIP AGE ROW (right-justified) =====
  auto agingRow = bounds.removeFromBottom(26);
  agingEndLabel.setBounds(agingRow.removeFromRight(50));
  agingSlider.setBounds(agingRow.removeFromRight(120));
  agingStartLabel.setBounds(agingRow.removeFromRight(40));
  agingLabel.setBounds(agingRow.removeFromRight(60));
  agingRow.removeFromRight(pad * 3);
  clockModeSelector.setBounds(agingRow.removeFromRight(65));
  clockModeLabel.setBounds(agingRow.removeFromRight(40));
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

void BreadbinEditor::applyGlobalPreset(int presetId) {
  // Helper to configure a voice
  auto configVoice = [this](int v, SIDEngine::Waveform wave, int pw, int a,
                            int d, int s, int r, float pan) {
    auto &vs = processor.getVoiceSettings(v);
    vs.waveform = wave;
    vs.pulseWidth = pw;
    vs.attack = a;
    vs.decay = d;
    vs.sustain = s;
    vs.release = r;
    vs.pan = pan;
    vs.enabled = true;
    processor.applyVoiceSettings(v);
  };

  // Reset both SIDs to default filter state
  auto resetFilters = [this]() {
    processor.getLeftSID().setFilterCutoff(1024);
    processor.getLeftSID().setFilterResonance(0);
    processor.getRightSID().setFilterCutoff(1024);
    processor.getRightSID().setFilterResonance(0);
    leftCutoffSlider.setValue(1024.0);
    leftResonanceSlider.setValue(0.0);
    rightCutoffSlider.setValue(1024.0);
    rightResonanceSlider.setValue(0.0);
  };

  // Reset detune
  auto resetDetune = [this]() {
    processor.setLeftDetune(0.0f);
    processor.setRightDetune(0.0f);
    leftDetuneSlider.setValue(0.0);
    rightDetuneSlider.setValue(0.0);
  };

  switch (presetId) {
  case 1: // Init - Basic pulse on all voices
    for (int v = 0; v < 6; ++v) {
      float pan = (v < 3) ? -0.3f : 0.3f;
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15, 0, pan);
    }
    processor.setDualMode(BreadbinProcessor::DualMode::StereoSplit);
    dualModeSelector.setSelectedId(1, juce::dontSendNotification);
    processor.setArpEnabled(false);
    arpEnableButton.setToggleState(false, juce::dontSendNotification);
    resetFilters();
    resetDetune();
    break;

  case 2: // Dual Lead - Classic SID lead sound
    // Left SID voices - slightly detuned saw
    configVoice(0, SIDEngine::Waveform::Sawtooth, 2048, 0, 6, 10, 4, -0.7f);
    configVoice(1, SIDEngine::Waveform::Pulse, 1800, 0, 8, 8, 5, -0.3f);
    configVoice(2, SIDEngine::Waveform::Pulse, 2200, 0, 8, 8, 5, -0.5f);
    // Right SID voices - complement
    configVoice(3, SIDEngine::Waveform::Sawtooth, 2048, 0, 6, 10, 4, 0.7f);
    configVoice(4, SIDEngine::Waveform::Pulse, 1800, 0, 8, 8, 5, 0.3f);
    configVoice(5, SIDEngine::Waveform::Pulse, 2200, 0, 8, 8, 5, 0.5f);
    processor.setDualMode(BreadbinProcessor::DualMode::StereoSplit);
    dualModeSelector.setSelectedId(1, juce::dontSendNotification);
    processor.setLeftDetune(-5.0f);
    processor.setRightDetune(5.0f);
    leftDetuneSlider.setValue(-5.0);
    rightDetuneSlider.setValue(5.0);
    // Bright filter for lead
    processor.getLeftSID().setFilterCutoff(1800);
    processor.getLeftSID().setFilterResonance(4);
    processor.getRightSID().setFilterCutoff(1800);
    processor.getRightSID().setFilterResonance(4);
    leftCutoffSlider.setValue(1800.0);
    leftResonanceSlider.setValue(4.0);
    rightCutoffSlider.setValue(1800.0);
    rightResonanceSlider.setValue(4.0);
    break;

  case 3: // Pad Stack - Slow attack pad
    for (int v = 0; v < 6; ++v) {
      float pan = (v < 3) ? -0.5f : 0.5f;
      int pw = 1024 + (v % 3) * 300; // Varied pulse widths
      configVoice(v, SIDEngine::Waveform::Pulse, pw, 10, 4, 12, 10, pan);
    }
    processor.setDualMode(BreadbinProcessor::DualMode::StereoSplit);
    dualModeSelector.setSelectedId(1, juce::dontSendNotification);
    processor.setLeftDetune(-8.0f);
    processor.setRightDetune(8.0f);
    leftDetuneSlider.setValue(-8.0);
    rightDetuneSlider.setValue(8.0);
    // Mellow filter for pad
    processor.getLeftSID().setFilterCutoff(800);
    processor.getLeftSID().setFilterResonance(6);
    processor.getRightSID().setFilterCutoff(800);
    processor.getRightSID().setFilterResonance(6);
    leftCutoffSlider.setValue(800.0);
    leftResonanceSlider.setValue(6.0);
    rightCutoffSlider.setValue(800.0);
    rightResonanceSlider.setValue(6.0);
    break;

  case 4: // Arpeggiated - Fast arp with bright sound
    for (int v = 0; v < 6; ++v) {
      float pan = (v < 3) ? -0.4f : 0.4f;
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 4, 12, 3, pan);
    }
    processor.setDualMode(BreadbinProcessor::DualMode::StereoSplit);
    dualModeSelector.setSelectedId(1, juce::dontSendNotification);
    processor.setArpEnabled(true);
    processor.setArpRate(8.0f);
    processor.setArpOctaves(2);
    arpEnableButton.setToggleState(true, juce::dontSendNotification);
    arpRateSlider.setValue(8.0);
    arpOctaveSelector.setSelectedId(2, juce::dontSendNotification);
    resetFilters();
    resetDetune();
    break;

  case 5: // Fat Unison - Thick unison sound
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 6, 14, 5, 0.0f);
    }
    processor.setDualMode(BreadbinProcessor::DualMode::Unison);
    dualModeSelector.setSelectedId(2, juce::dontSendNotification);
    processor.setLeftDetune(-12.0f);
    processor.setRightDetune(12.0f);
    leftDetuneSlider.setValue(-12.0);
    rightDetuneSlider.setValue(12.0);
    // Rich filter for unison
    processor.getLeftSID().setFilterCutoff(1200);
    processor.getLeftSID().setFilterResonance(3);
    processor.getRightSID().setFilterCutoff(1200);
    processor.getRightSID().setFilterResonance(3);
    leftCutoffSlider.setValue(1200.0);
    leftResonanceSlider.setValue(3.0);
    rightCutoffSlider.setValue(1200.0);
    rightResonanceSlider.setValue(3.0);
    break;

  case 6: // Retro Synth - Mixed waveforms for classic vibe
    configVoice(0, SIDEngine::Waveform::Triangle, 0, 2, 4, 10, 6, -0.6f);
    configVoice(1, SIDEngine::Waveform::Pulse, 1536, 0, 6, 12, 4, -0.2f);
    configVoice(2, SIDEngine::Waveform::Sawtooth, 0, 0, 8, 8, 5, -0.4f);
    configVoice(3, SIDEngine::Waveform::Triangle, 0, 2, 4, 10, 6, 0.6f);
    configVoice(4, SIDEngine::Waveform::Pulse, 2560, 0, 6, 12, 4, 0.2f);
    configVoice(5, SIDEngine::Waveform::Sawtooth, 0, 0, 8, 8, 5, 0.4f);
    processor.setDualMode(BreadbinProcessor::DualMode::StereoSplit);
    dualModeSelector.setSelectedId(1, juce::dontSendNotification);
    resetFilters();
    resetDetune();
    break;
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

          // Update global controls
          dualModeSelector.setSelectedId(
              static_cast<int>(processor.getDualMode()) + 1,
              juce::dontSendNotification);
          leftChipSelector.setSelectedId(
              static_cast<int>(processor.getLeftChipModel()) + 1,
              juce::dontSendNotification);
          rightChipSelector.setSelectedId(
              static_cast<int>(processor.getRightChipModel()) + 1,
              juce::dontSendNotification);
          agingSlider.setValue(processor.getAgingFactor(),
                               juce::dontSendNotification);
        }
      }
    }
  });

  // Keep chooser alive
  static std::unique_ptr<juce::FileChooser> savedChooser;
  savedChooser = std::move(chooser);
}
