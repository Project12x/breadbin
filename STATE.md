# Project State

**Last Updated**: 2026-02-07

## Current Status: Alpha

### Working Features

- ✅ Dual SID emulation via reSIDfp
- ✅ Three stereo modes (Split, Unison, Multitimbral)
- ✅ Per-channel chip selection (6581/8580)
- ✅ Time Machine aging simulation
- ✅ Waveform selection (Triangle, Saw, Pulse, Noise)
- ✅ ADSR envelope controls
- ✅ Filter (Cutoff, Resonance, LP/BP/HP)
- ✅ Virtual MIDI keyboard (standalone)
- ✅ VST3 plugin builds and loads
- ✅ Safety limiter active
- ✅ MIDI Learn system (Global)
- ✅ Master Volume & Sustain Pedal
- ✅ State persistence (XML)

### Known Issues

- Minor: Linting errors related to JUCE includes (build succeeds)

### Not Yet Implemented

- Full parameter automation (APVTS Migration in progress)
- Arpeggiator (Integrated but needs UI refinement)
- LFO modulation (Integrated with UI)

## Build Status

- **Windows VST3**: ✅ Building
- **Windows Standalone**: ✅ Building
- **macOS**: 🔲 Not tested
- **Linux**: 🔲 Not tested

## Directory Structure

```text
breadbin/
├── src/
│   ├── PluginProcessor.cpp/h    # Audio processing, dual SID
│   ├── PluginEditor.cpp/h       # UI editor
│   ├── SIDEngine.cpp/h          # reSIDfp wrapper
│   └── SafetyLimiter.h          # Audio protection
├── assets/
│   └── background.jpg           # Synthwave UI background
├── JUCE/                        # (fetched by CMake)
└── reSIDfp/                     # (fetched by CMake)
```

## Recent Changes

- Added proper labels above all rotary controls
- Simplified Time Machine to 1982/NOW labels
- Added semi-opaque text boxes for values
- Enlarged controls for better usability
- Implemented MIDI Learn, Master Volume, and Sustain Pedal (v0.9.0)
