# Changelog

All notable changes to Breadbin SID VST are documented here.

## [0.3.0] - 2026-02-04
### Added
- C64-accurate arpeggiator with PAL 50Hz / NTSC 60Hz / Sync rate modes
- Arpeggiator patterns: Up, Down, Up/Down, Random
- Octave range expansion (1-3 octaves)
- Center SID GUI section with chip selector, voice buttons, filter controls
- Arpeggiator GUI section with enable, pattern, rate mode, octave controls

### Changed
- Reorganized SID layout to 3 columns (Left/Center/Right)

## [0.2.0] - 2026-02-04
### Added
- Triple SID architecture (sidLeft, sidCenter, sidRight)
- Expanded to 9 voices (0-2=left, 3-5=center, 6-8=right)
- Center note queue for center SID
- 3-way keyboard split in Multitimbral mode (C4-B4 = center)
- Per-SID panning (left, center, right)

### Changed
- Updated voice routing for 3 SIDs
- Updated Multitimbral split: below C4 → left, C4-B4 → center, C5+ → right

## [0.1.0] - 2026-02-03
### Added
- Initial dual SID implementation (Left/Right)
- reSIDfp cycle-accurate emulation
- 6 voices (3 per SID)
- Per-voice ADSR envelope
- Waveforms: Triangle, Sawtooth, Pulse, Noise
- Pulse width control
- Chip model selection (MOS6581 / MOS8580)
- Time Machine component aging simulation
- Stereo/Unison and Multitimbral dual modes
- Resonant multimode filter per SID (LP/BP/HP)
- Virtual MIDI keyboard for standalone
- Background image with C64 aesthetic
