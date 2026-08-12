# Breadbin

C64 Dual SID Synthesizer - GPL v3

A JUCE 9 VST3/Standalone synthesizer using reSIDfp for cycle-accurate SID chip emulation.

## Features

### UI

- **C64 Neon Synthwave skin**: full LookAndFeel reskin — glowing 270° rotary knobs, CRT phosphor
  scope displays (filter/LFO), frosted-glass popup panels (melatonin_blur), per-section accent
  colors (SID I cyan, SID II orange, voice editor magenta), DPI-aware window scaling (75–150%).
- **C64 Neon Keyboard**: custom `MidiKeyboardComponent` subclass with dark gradient key caps,
  cyan neon-edged sharps, press/hover glow, and Press-Start pixel-font octave labels.
- **Live filter readouts**: Cutoff and Resonance knobs on each SID tower display their current
  value in real time.

### Sound Engine
- **Dual SID Engine**: Two independent SID emulators with 6 voices total
- **8 Chip Model Variants**: MOS 6581 (+ R2, R3, R4), MOS 8580 (+ R5, 8580D), CSG 9580
- **Dual-SID Routing**: Stereo Split and Unison (detuned fatness)
- **4-Mode Voice System**: Mono, Paraphonic, Polyphonic, and Poly+Para (up to 24 notes)
- **PAL/NTSC Clock**: Authentic PAL (985,248 Hz) and NTSC (1,022,727 Hz) timing

### Modulation
- **Dual LFOs**: Triangle/Saw/Square/S&H/Sine -> filter/PW/pitch
- **Filter Envelope**: Dedicated ADSR with bipolar amount
- **4-Slot Mod Matrix**: 6 sources x 5 destinations, bipolar amount
- **PWM Sweep**: Dedicated pulse width modulation oscillator
- **Ring Mod + Hard Sync**: Per-voice inter-oscillator modulation
- **14-bit Pitch Bend + Mod Wheel**

### Effects
- **Chorus**: JUCE DSP chorus
- **Stereo Delay**: Independent left/right delay times with feedback
- **Reverb**: Algorithmic reverb (ReverbSC)

### Performance
- **Arpeggiator**: Multiple patterns, octave expansion, tempo sync
- **Chord Memory**: 4 slots x 5 intervals, trigger chords from single key
- **Wavetable Sequencer**: 16 steps with per-step waveform/PW/pitch
- **Portamento / Glide**: Monophonic legato with linear Hz interpolation

### Digi Sampler
- **WAV Loading**: Load any WAV file as a digi sample
- **4-bit Mode**: Authentic C64 $D418 volume register playback (crunchy, lo-fi)
- **8-bit Mode**: Direct mix path for cleaner retro sound
- **Pitch Tracking + Loop**: MIDI note controls playback rate

### Extras
- **SID File Player**: Load .SID/.PSID files from the HVSC (50,000+ C64 tunes)
- **77 Factory Presets**: Categorized global presets + 37 voice presets
- **MIDI Learn**: Right-click any control for CC mapping
- **User Presets**: Save/load custom presets

## Building

```bash
cmake -B build
cmake --build build --config Release
```

### Test
```bash
ctest --test-dir build -C Release
```

## Dependencies

- JUCE 9.0.0 (pinned to `f8f8864172464b9adf9eba6101e1f784838d1597`)
- libsidplayfp 2.16.0
- melatonin_blur v1.4 (MIT, fetched automatically)
- CMake 3.22+

## License

GPL v3

JUCE 9 is available under AGPLv3 or a commercial licence. Before distributing
Breadbin binaries, ensure the distribution terms meet JUCE 9's licensing
requirements.

reSIDfp is GPL v2+ from the libsidplayfp project.
