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

### Changed
- Enlarged rotary controls for better usability (55px width, 70px height)
- Control layout now shows labels above sliders

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
