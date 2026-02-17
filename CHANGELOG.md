# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Editor State Restoration**: Filter cutoff/resonance, preset selection, and voice
  parameters now persist correctly when closing/reopening the editor and when
  saving/reloading DAW projects. Serialized non-APVTS filter state to ValueTree,
  removed conflicting `saveUIToVoice()` callbacks on APVTS-attached controls,
  and eliminated hardcoded preset selector default.
- **Chord/Arp exclusivity hardening**: Chord memory no longer populates arpeggiator
  held-note state, and arp stepping is bypassed while chord mode is active, avoiding
  stale arp behavior when both modes are enabled by automation/state.

### Changed

- **Chord voice allocation in Stereo/Unison**: Chord memory now allocates root + up
  to 5 intervals across both SIDs (up to 6 notes total), aligning playback with the
  5-interval chord editor.

### Added

- **19 Factory Presets**: 10 classic C64 sounds (Monty Lead, Sanxion Buzz, Last Ninja,
  Delta Run, Cobra Bass, IK Lead, Turbo Saw, Times of Lore, Hawkeye Pluck, Deflektor Bell),
  5 modern modulation patches (Drift Pad, Arp Machine, Wobble Bass, Sequence Morph, Poly Chord),
  and 4 bonus distinct sounds (Follin Complex, Noise Drums, Arp Bass, Filter Scream).
  Total factory presets: 46.

- **Preset Menu Submenus**: Converted flat preset dropdown to categorized submenus
  (Leads, Bass, Pads & Keys, Arps & Sequences, FX & Modulation, Classic C64).
  5 curated favorites (Dual Lead, Commando, Drift Pad, Growl Bass, Chip Sequence)
  appear at the top level for quick access.

- **CPU Meter**: Real-time DSP load percentage displayed in top-right corner.
  Color-coded: grey (<50%), orange (<80%), red (>=80%).

- **Preset Navigation**: Previous/next buttons flanking the preset selector
  for sequential browsing through all 46 factory presets with wrapping.

- **Tooltip Audit**: Added tooltips to mod matrix source/destination/amount
  controls and LFO depth sliders.

- **Popup Panel Styling**: Standardized SID File Player with title decoration
  (glow pill + divider). Wavetable title font matched to 14pt bold.

- **Universal MIDI Learn**: All interactive controls now support right-click MIDI Learn/Unlearn.
  Extended `ControlParam` enum with 37 new entries. Created `MappableToggle` and `MappableComboBox`
  widget classes. Converted all FX sliders, filter envelope ADSR+amount, pan sliders, PWM sweep
  controls, toggle buttons (arp/chorus/delay/filter env/ext in/LFO/wavetable/ring mod/sync/filter),
  and combo boxes (dual mode/clock mode/arp pattern/arp octaves/LFO waveforms) to their Mappable
  variants across both the main editor and ModMatrixPanel popup.

- **User Preset Menu Saving**: Save button now shows a popup menu with "Save to File" and
  "Save to Preset Menu" options. User presets are stored as `.breadbin` XML files in
  `%APPDATA%/GPLAudio/Breadbin/Presets/` and appear in a "User Presets" section of the global
  preset dropdown. Presets persist across sessions and are loaded on startup.

- **ASIO Standalone Support**: Added `JUCE_ASIO=1` compile definition and Steinberg ASIO SDK
  include path to CMakeLists.txt. ASIO devices now appear in the standalone audio settings.
  SDK path is configurable via `-DASIO_SDK_PATH` CMake cache variable.
- **Pitch bend range APVTS**: `pitchBendRange` AudioParameterInt (2-12, default 2) with
  processBlock sync, CC mapping writes APVTS, global preset reset. Replaces direct
  `setPitchBendRange()` calls with single source of truth.
- **Modulation popup**: Consolidated LFO1, LFO2, pitch bend range selector, and mod matrix
  into a single "Modulation" dialog (was "Mod Matrix"). Reclaimed 124px of main editor
  vertical space. All controls have APVTS attachments; values persist across popup open/close.
- **PWM Sweep**: Dedicated triangle-only pulse width sweep oscillator (enable/rate/depth),
  independent from LFOs, additive with all existing PW modulation. UI row in Modulation popup.
- **Chord Memory**: 4 chord slots with up to 5 semitone intervals each (-24 to +24),
  trigger chords from single keys. Mutually exclusive with arpeggiator. Separate popup
  UI with slot selection and interval sliders. 22 new APVTS parameters (enable, slot,
  4x5 intervals). Global preset reset included.
- **Wavetable step editor popup**: Full 16-step grid editor with per-step waveform ComboBox,
  pitch slider (-24..+24), and PW slider (0..4095). Global controls (enable, steps, rate, loop)
  moved from inline to popup. Current-step indicator with cyan highlight, inactive step dimming.
  Replaces ambiguous inline controls with proper editing UI for all 48 per-step APVTS params.
- **2 new presets**: "WT Arpeggio" (classic C64 wavetable chord arpeggio at 50Hz) and
  "WT Morph" (8-step timbral morphing sequence with delay + LFO filter sweep).
  Total global presets: 9.
- 4 new integration tests for pitchBendRange (default, sync, state persistence, full lifecycle).
- 3 new integration tests for PWM sweep (default off, modifies PW, state round-trip).
- 3 new integration tests for Chord Memory (default off, triggers audio, state round-trip).
- 2 new integration tests for Wavetable (step params editable, step sequencer produces variation).
- **Mod Matrix slot enable + totals**: Added per-slot enable/bypass control (new
  `mod*_enable` APVTS params) and popup readouts for destination totals (Filter/PW/Pitch/Res).
- **Wavetable step edit operations**: Added popup actions for active steps:
  shift left, shift right, randomize, and clear.
- **SID File Player**: Load and play .SID/.PSID files from the HVSC (50,000+ C64 tunes) using
  the full libsidplayfp engine (6502 CPU, CIA, VIC, MMU). Background thread produces audio via
  lock-free ring buffer, mixed additively into plugin output alongside synth voices.
  - Popup UI panel with file browser, transport (play/stop/pause), tune metadata (title, author,
    released), sub-tune selector, volume slider, and real-time hex register display
  - Register overlay on main editor: cyan labels on corresponding controls (waveform, PW, ADSR,
    cutoff, resonance) show live SID register values during playback at 30Hz
  - Snapshot button maps all 32 SID registers to APVTS parameters (waveform, PW, ADSR, filter
    cutoff/resonance/mode, master volume) for instant preset creation from any .SID tune
  - New files: `SidFilePlayer.h/cpp`, `src/residfp/config.h` (libsidplayfp stub),
    `src/residfp/sidplayfp/sidversion.h`
  - CMake: compiles ~30 additional libsidplayfp sources (C64 machine, CPU, CIA, VIC, tune loaders,
    player engine) from release tarball (includes pre-built .bin files)

## [0.9.2] - 2026-02-11

### Fixed

- **NTSC frequency**: `midiNoteToFrequency`, `noteOn`, `setFrequency` now use
  `getClockHz()` instead of hardcoded `SID_CLOCK_PAL`, giving correct pitch in NTSC mode.
- **Chip model cache churn**: `processBlock` now updates `chipModelLeft`/`chipModelRight`
  caches after applying new models, eliminating per-block re-initialization.
- **RT-unsafe logging**: Removed two `Logger::writeToLog` calls from the audio thread
  (sustain pedal and MIDI learn debug messages).

### Added

- **External input bus**: Added real input bus named "External Input" (`isBusesLayoutSupported`) so
  external audio routes through a proper host input, not a feedback loop from the output buffer.
- **Per-SID pan**: `leftPan` and `rightPan` APVTS params with standard semantics
  (-1=left, 0=center, +1=right). Left SID defaults -1, right SID defaults +1.
  Equal-power pan law (cos/sin). Pan sliders exposed in each SID panel's UI.
- **Dead code removal**: Removed legacy per-voice pan APVTS parameter, serialization,
  and preset references. Per-voice pan was a no-op superseded by per-SID pan.

### Added

- Proper control labels (Attack, Decay, Sustain, Release, PW, Cutoff, Reso)
- Semi-opaque text boxes for value display
- Time Machine slider with 1982/NOW end labels
- Synthwave background image integration
- Enlarged rotary controls for better usability (55px width, 70px height)
- Control layout now shows labels above sliders

## [0.9.1] - 2026-02-11

### Fixed

- **LFO waveform mapping**: Removed ghost "Sine" entry from APVTS `lfoWave` parameter.
  APVTS indices now map 1:1 to `LFOWaveform` enum {Triangle=0, Sawtooth=1, Square=2, S&H=3}.
  Old saved states are backward-safe (index 0 already mapped to Triangle in DSP).
  State restore now round-trips correctly through APVTS attachments.
- **APVTS dual-path conflicts**: Removed redundant `onChange`/`onClick` callbacks and
  manual preset-load refresh for APVTS-attached LFO controls. All LFO/volume state
  now flows exclusively through APVTS attachments.
- **Master volume SID sync**: `processBlock` now calls `setMasterVolume()` from APVTS
  value each block, fixing headless/automation scenarios where SID volume register
  was never updated.
- **getSampleRate()**: Added `setRateAndBufferSizeDetails()` in `prepareToPlay()` so
  `getSampleRate()` returns correct value in headless/test mode.

## [0.9.0] - 2026-02-07

### Added

- **MIDI Learn System**: Right-click context menu for all sliders with "MIDI Learn" and "Unlearn".
- **Visual Feedback**: Gold "LEARN" badge on sliders and global centered popup indicating mapping target.
- **Master Volume**: Global volume control (0.0 to 1.0) persisting in state.
- **Sustain Pedal**: CC 64 support with note queue management.
- **State Persistence**: All MIDI mappings automatically saved/loaded with plugin state.
- **Diagnostic Logging**: Integrated spdlog-style debug logging for MIDI and pedal activity.

## [0.2.0] - 2026-02-03

### Added

- Dual SID engine support with three stereo modes:
  - **Stereo Split**: Left SID → Left channel, Right SID → Right channel
  - **Unison**: Both SIDs summed to stereo with detune
  - **Multitimbral**: Voice 1 left, Voice 2 right, Voice 3 center
- Per-channel SID chip selection (6581/8580)
- Time Machine aging simulation (1982-NOW)
- Virtual MIDI keyboard for standalone testing
- Waveform selector (Triangle, Sawtooth, Pulse, Noise)
- ADSR envelope controls
- Filter controls (Cutoff, Resonance, LP/BP/HP modes)
- Preset system with C64 classics (Last Ninja, Monty)
- Safety limiter to prevent harsh audio artifacts

### Fixed

- Audio output from virtual keyboard
- Waveform selection affecting sound

## [0.1.0] - 2026-02-03

### Added

- Initial project scaffold
- JUCE 8 + reSIDfp integration
- Basic SIDEngine wrapper class
- VST3 and Standalone build targets
