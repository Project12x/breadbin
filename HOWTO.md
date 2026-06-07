# How To Guide

## Building

### Prerequisites
- Visual Studio 2022 with C++ workload
- CMake 3.22+
- JUCE 8.0.4 and libsidplayfp 2.16.0 (fetched automatically via CPM)
- Optional: Steinberg ASIO SDK at `C:/SDKs/asiosdk/ASIOSDK` for ASIO standalone support

### Build Steps

```powershell
# Configure
cmake -B build -G "Visual Studio 17 2022"

# Build Release
cmake --build build --config Release

# Build Release (clean — required when you see MSB8028 or stale-artifact symptoms;
# shared intermediate dirs between JUCE module targets can corrupt incremental links)
cmake --build build --config Release --target Breadbin_All --clean-first

# Build Debug
cmake --build build --config Debug
```

### Test

```powershell
ctest --test-dir build -C Release
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
- **Stereo Split**: Left SID -> left channel, right SID -> right channel (per-SID pan adjustable).
- **Unison**: Both SIDs play together with detune for thickness.

(A Multitimbral engine mode exists internally but is hidden from the UI — per-voice settings cover the same ground.)

### Voice Modes
- **Mono**: Single note, last-note priority (C64-authentic; pairs with glide).
- **Paraphonic**: Up to 6 notes across the two SID engines, sharing one filter/ADSR.
- **Polyphonic**: Each note gets its own SID pair (independent filter/ADSR), up to the max-notes limit.
- **Poly+Para**: Paraphonic stacking within each polyphonic voice (up to 24 notes).

### Chip Models
8 variants per SID, selectable independently:
- **6581 family** (warmer, grittier filter): MOS 6581, 6581 R2 (bright), 6581 R3 (most common), 6581 R4 (strong combined waveforms).
- **8580 family** (cleaner, tighter bass): MOS 8580, 8580 R5 (darker), CSG 9580 (bright final run), 8580D (digiboost, mellow).

Defaults: left SID = MOS 6581, right SID = MOS 8580.

### Filter Modes
- **LP**: Low-pass (most common SID sound)
- **BP**: Band-pass (nasal, vocal quality)
- **HP**: High-pass (thin, cutting)

### UI Scale
Use the scale selector (top row, 75–150%) to rescale the whole window for high- or low-DPI displays. The choice is saved per machine.

## Presets

- **77 global presets** in the Preset dropdown, grouped into submenus (Leads, Bass, Pads & Keys, Arps & Sequences, FX & Modulation, Classic C64), with curated favorites at the top level.
- **37 voice presets** (Leads, Bass, Pads & Keys, Percussion, FX & Utility).
- Save custom presets via the **Save** button (to file, or into the User Presets menu). User presets live in `%APPDATA%/GPLAudio/Breadbin/Presets/`.
- Right-click any control for **MIDI Learn / Unlearn**.
