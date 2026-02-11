# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
- **APVTS dual-path conflicts**: Removed redundant `onChange`/`onClick` callbacks for
  `lfoWaveformSelector`, `lfoEnableButton`, and `masterVolSlider` that conflicted with
  APVTS attachments.
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
