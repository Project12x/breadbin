# Project State

**Last Updated**: 2026-02-17

## Current Status: Alpha

### Working Features

- Dual SID emulation via reSIDfp (6 voices: 3 per chip)
- Three stereo modes (Split, Unison, Multitimbral)
- Per-channel chip selection (6581/8580)
- Waveform selection (Triangle, Saw, Pulse, Noise)
- ADSR envelope controls
- Filter (Cutoff, Resonance, LP/BP/HP) with per-voice routing
- Virtual MIDI keyboard (standalone)
- VST3 plugin builds and loads
- Safety limiter active
- MIDI Learn system (Global)
- Master Volume & Sustain Pedal
- State persistence (XML via APVTS)
- Full parameter automation via APVTS
- LFO modulation (Triangle, Sawtooth, Square, S&H) with filter/PW/pitch destinations
- Arpeggiator (integrated with UI)
- 14-bit pitch bend
- PAL/NTSC clock switching
- External Audio Input (sidechain bus)
- Per-SID pan (equal-power, UI sliders in each SID panel)
- Filter Envelope (ADSR with bipolar amount)
- Built-in FX: Chorus + Stereo Delay
- Second LFO (LFO2, independent from LFO1)
- Wavetable step sequencer (16 steps, per-step waveform/PW/pitch, popup step editor)
- 4-slot Mod Matrix (LFO1/LFO2/FilterEnv/ModWheel/Velocity -> Filter/PW/Pitch/Resonance)
- 69 global presets (categorized: Leads, Bass, Pads & Keys, Arps & Sequences, FX & Modulation, Classic C64)
- 37 voice presets (categorized: Leads, Bass, Pads & Keys, Percussion, FX & Utility)
- Pitch bend range selector (APVTS-exposed, ±2–12 semitones)
- Modulation popup (LFO1/LFO2, PWM sweep, pitch bend range, mod matrix in single dialog)
- PWM Sweep (dedicated triangle oscillator for pulse width automation)
- Chord Memory (4 slots x 5 intervals, trigger chords from single key, popup UI)
- SID File Player (load .SID/.PSID files, background-thread playback mixed into output, register overlay on main UI, snapshot SID registers to APVTS presets)

### Known Issues

- Minor: clangd lint errors related to JUCE includes (build succeeds)

## Build Status

- **Windows VST3**: Building
- **Windows Standalone**: Building
- **macOS**: Not tested
- **Linux**: Not tested

## Test Suites

| Suite | Tests | Status |
|-------|-------|--------|
| BreadbinLFOTests | 484 | Pass |
| BreadbinMutationTests | 18 mutations | 5.6% survival |
| BreadbinIntegrationTests | 382 | Pass |

## Directory Structure

```text
breadbin/
├── src/
│   ├── PluginProcessor.cpp/h    # Audio processing, dual SID
│   ├── PluginEditor.cpp/h       # UI editor
│   ├── SIDEngine.cpp/h          # reSIDfp wrapper
│   ├── SidFilePlayer.cpp/h      # .SID file playback (libsidplayfp)
│   └── SafetyLimiter.h          # Audio protection
├── tests/
│   ├── LFOTests.cpp             # LFO waveform verification
│   ├── MutationTests.cpp        # Mutation coverage
│   └── IntegrationTests.cpp     # Integration/regression tests
├── assets/
│   └── background.jpg           # Synthwave UI background
├── JUCE/                        # (fetched by CMake)
└── reSIDfp/                     # (fetched by CMake)
```
