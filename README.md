# Breadbin

C64 Dual SID Synthesizer - GPL v3

A JUCE 8 VST3/Standalone synthesizer using reSIDfp for authentic SID chip emulation.

## Features

- **Dual SID Engine**: Two independent MOS 6581/8580 emulators
- **Three Modes**: Stereo Split, Unison (detuned fatness), Multitimbral (MIDI channel routing)
- **Chip Model Switching**: 6581 (darker, grittier) vs 8580 (cleaner, brighter)
- **Time Machine**: Simulates capacitor aging from factory fresh to vintage drift

## Building

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## License

GPL v3 - Designed for bundling with GPL audio software (Pedalboard3).

reSIDfp is GPL v2+ from the libsidplayfp project.
