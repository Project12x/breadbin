# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
