# How To Guide

## Building

### Prerequisites
- Visual Studio 2022 with C++ workload
- CMake 3.22+
- JUCE 8.0.4 (fetched automatically)

### Build Steps

```powershell
# Configure
cmake -B build -G "Visual Studio 17 2022"

# Build Release
cmake --build build --config Release

# Build Debug
cmake --build build --config Debug
```

### Output Locations
- **Standalone**: `build/Breadbin_artefacts/Release/Standalone/Breadbin.exe`
- **VST3**: `build/Breadbin_artefacts/Release/VST3/Breadbin.vst3/`

## Installation

### VST3 Plugin
Copy the `Breadbin.vst3` folder to your VST3 directory:
- Windows: `C:\Program Files\Common Files\VST3\`
- macOS: `/Library/Audio/Plug-Ins/VST3/`

### Standalone
Run `Breadbin.exe` directly - no installation required.

## Usage

### Dual SID Modes
1. **Stereo Split**: Classic dual-SID setup. Left SID plays on left, right SID on right.
2. **Unison**: Both SIDs play together with slight detune for thickness.
3. **Multitimbral**: Voice 1 pans left, Voice 2 right, Voice 3 center.

### Time Machine
Slide from 1982 (pristine chips) to NOW (aged, warmer sound).

### Chip Models
- **6581**: Original C64 chip. Warmer, grittier filter.
- **8580**: Later revision. Cleaner, tighter bass.

### Filter Modes
- **LP**: Low-pass (most common SID sound)
- **BP**: Band-pass (nasal, vocal quality)
- **HP**: High-pass (thin, cutting)

## Presets

Select from the Preset dropdown:
- **Init**: Default state
- **Last Ninja Lead**: Classic C64 game lead
- **Monty Bass**: Punchy bass sound
