# Project State

**Last Updated**: 2026-02-03

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

### Known Issues
- Minor: Unused variable warnings in updateSynthFromControls
- Linting errors related to JUCE includes (build succeeds)

### Not Yet Implemented
- Full parameter automation
- State save/restore in DAW
- Preset save to disk
- Arpeggiator
- LFO modulation

## Build Status
- **Windows VST3**: ✅ Building
- **Windows Standalone**: ✅ Building
- **macOS**: 🔲 Not tested
- **Linux**: 🔲 Not tested

## Directory Structure
```
primordial-shuttle/
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
