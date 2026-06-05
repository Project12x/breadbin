# Project State

**Last Updated**: 2026-06-05

## Current Status: Beta

Feature-complete. In UI-polish and release-engineering phase toward 1.0.

### Working Features

#### Sound Engine
- Dual SID emulation via reSIDfp (6 voices: 3 per chip)
- 8 chip model variants (MOS 6581, 6581 R2, 6581 R3, 6581 R4, MOS 8580, 8580 R5, CSG 9580, 8580D)
- Dual-SID routing: Stereo Split, Unison (Multitimbral engine retained but hidden from UI)
- 4-mode voice system: Mono, Paraphonic (6 notes, shared filter), Polyphonic (per-note SID pair, up to 8), Poly+Para (paraphonic within each poly voice, up to 24)
- Voice stealing with configurable max notes
- Waveform selection (Triangle, Saw, Pulse, Noise)
- ADSR envelope controls
- Filter (Cutoff, Resonance, LP/BP/HP) with per-voice routing
- PAL/NTSC clock switching (985,248 / 1,022,727 Hz)
- Per-SID detune (±50 cents) and per-SID equal-power pan
- Master volume as output gain; 3-layer DC removal (idle-offset calibration + adaptive estimator + 20 Hz HP)
- Safety chain: DC blocker -> ultrasonic LP -> limiter -> envelope-following noise gate

#### Modulation
- Dual LFOs (Triangle/Saw/Square/S&H/Sine -> filter/PW/pitch); LFO1 tempo-syncable to DAW BPM
- Filter Envelope (dedicated ADSR, bipolar amount)
- 4-slot Mod Matrix (LFO1/LFO2/FilterEnv/ModWheel/Velocity -> Filter/PW/Pitch/Resonance, per-slot enable)
- PWM Sweep (dedicated triangle oscillator for pulse width automation)
- Ring mod + hard sync with per-voice mod offset
- 14-bit pitch bend (range ±2–12 semitones) + mod wheel -> filter

#### Performance / Composition
- Arpeggiator (Up/Down/UpDown/Random, octave expansion, PAL/NTSC frame timing)
- Chord Memory (4 slots × 5 intervals, trigger chords from a single key)
- Wavetable step sequencer (16 steps, per-step waveform/PW/pitch, popup editor)
- Portamento / Glide (monophonic legato, linear Hz interpolation)

#### Digi Sampler
- WAV loading; 4-bit ($D418 volume register) and 8-bit direct-mix modes; pitch tracking + loop

#### Built-in FX
- Chorus, Stereo Delay (independent L/R times), Reverb (ReverbSC, local MIT copy)

#### Extras
- SID File Player (.SID/.PSID via full libsidplayfp engine, background thread + ring buffer, live register overlay, snapshot registers to APVTS)
- 77 global presets + 37 voice presets (categorized submenus)
- Universal MIDI Learn (right-click any control)
- User preset saving (`%APPDATA%/GPLAudio/Breadbin/Presets/`)
- State persistence (XML via APVTS) + full parameter automation
- ASIO standalone support

### In Progress (branch `polish/2026-03-03`)
- DPI-aware UI scaling: `ScaledEditor` base applies `AffineTransform::scale()` so the window rescales while logical layout stays at 1000×800. Scale selector (75/100/125/150%) persisted per-machine via `ApplicationProperties`. Uncommitted; not yet host-verified.

### Known Issues
- MutationTests: 1/18 mutation survives (triangle boundary test) — pre-existing
- Deprecated `juce::Font` constructor warnings (JUCE 8)
- Minor: clangd lint errors related to JUCE includes (build succeeds)

## Build Status

- **Windows VST3**: Building
- **Windows Standalone**: Building (ASIO)
- **macOS**: Not tested
- **Linux**: Not tested

## Test Suites

| Suite | Tests | Status |
|-------|-------|--------|
| BreadbinLFOTests | 484 | Pass |
| BreadbinMutationTests | 18 mutations (17 killed) | 5.6% survival |
| BreadbinIntegrationTests | 405 | Pass |

Run: `ctest --test-dir build -C Release`

## Directory Structure

```text
breadbin/
├── src/
│   ├── PluginProcessor.cpp/h    # Audio processing, dual SID, voice modes, modulation, FX, safety
│   ├── PluginEditor.cpp/h       # UI editor, LookAndFeel, popup panels (~8k lines)
│   ├── ScaledEditor.h           # DPI-aware editor base (AffineTransform scale)
│   ├── SIDEngine.cpp/h          # reSIDfp wrapper, 8 chip model profiles
│   ├── SidFilePlayer.cpp/h      # .SID file playback (full libsidplayfp engine)
│   ├── DigiSampler.h            # WAV digi sample (4-bit / 8-bit storage)
│   ├── dsp/ReverbSC.h           # Reverb (Soundpipe port, MIT)
│   └── residfp/                 # Local config headers for reSIDfp build
├── tests/
│   ├── LFOTests.cpp             # LFO waveform math (standalone)
│   ├── MutationTests.cpp        # Mutation coverage (standalone)
│   └── IntegrationTests.cpp     # Full signal-path integration/regression (JUCE-linked)
├── assets/                      # background.jpg, fonts (Lato, JetBrains Mono, etc.)
├── releases/                    # preserved build artifacts (per build-preservation policy)
└── build/                       # CMake output; JUCE 8 + libsidplayfp fetched via CPM
```
