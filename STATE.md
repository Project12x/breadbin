# Project State

**Last Updated**: 2026-02-11

## Current Status: Alpha

### Working Features

- Dual SID emulation via reSIDfp (6 voices: 3 per chip)
- Three stereo modes (Split, Unison, Multitimbral)
- Per-channel chip selection (6581/8580)
- Time Machine aging simulation
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
- Wavetable step sequencer (16 steps, per-step waveform/PW/pitch)
- 4-slot Mod Matrix (LFO1/LFO2/FilterEnv/ModWheel/Velocity -> Filter/PW/Pitch/Resonance)
- Global presets with full parameter reset

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
| BreadbinIntegrationTests | 146 | Pass |

## Directory Structure

```text
breadbin/
├── src/
│   ├── PluginProcessor.cpp/h    # Audio processing, dual SID
│   ├── PluginEditor.cpp/h       # UI editor
│   ├── SIDEngine.cpp/h          # reSIDfp wrapper
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
