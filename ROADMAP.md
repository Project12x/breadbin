# Roadmap

## Phase 1: Core Foundation ✅

- [x] Project scaffold with JUCE 8 + reSIDfp
- [x] SIDEngine wrapper class
- [x] Dual SID architecture
- [x] Basic audio processing pipeline

## Phase 2: Dual SID Modes ✅

- [x] Stereo Split mode
- [x] Unison mode with detune
- [x] Multitimbral mode

## Phase 3: Editor UI ✅

- [x] C64-inspired aesthetic with synthwave background
- [x] Mode and model selectors
- [x] ADSR envelope controls
- [x] Filter controls (cutoff, resonance, LP/BP/HP)
- [x] Time Machine aging slider
- [x] Virtual MIDI keyboard (standalone)

## Phase 4: Polish & Testing ✅

- [x] Headless tests for DSP components
- [x] Filter verification tests
- [x] Oscillator output tests
- [x] Safety limiter validation
- [x] State persistence (VST3 save/restore via APVTS)
- [x] Full parameter automation via APVTS

## Phase 5: Advanced Features ✅

- [x] Arpeggiator (rate, octave, pattern modes)
- [x] LFO modulation (Triangle/Saw/Square/S&H → filter/PW/pitch)
- [x] Ring modulation (Voice N × Voice N-1)
- [x] Hard sync (Voice N resets Voice N-1)
- [x] Portamento / Glide (monophonic legato, linear Hz interp)
- [x] Per-SID detune (±50 cents)
- [x] Per-voice filter routing (filter enable toggle per voice)
- [x] PAL/NTSC clock switching (985,248 / 1,022,727 Hz)
- [x] 14-bit pitch bend + mod wheel → filter
- [x] External audio input (sidechain bus through SID filter)
- [x] Per-SID pan (equal-power pan law, APVTS-exposed with UI sliders)
- [x] MIDI Learn system (global CC mapping with visual feedback)
- [x] Master Volume & Sustain Pedal
- [x] Preset system (per-voice and global presets with selector UI)

## Phase 6: Competitive Feature Sprint ✅

- [x] Filter Envelope (ADSR, bipolar amount, stacks with mod wheel + LFO)
- [x] Built-in FX: Chorus (JUCE DSP) + Stereo Delay (independent L/R times)
- [x] Second LFO (LFO2, identical structure, independent controls)
- [x] Wavetable step sequencer (16 steps, per-step waveform/PW/pitch, WT base -> LFO mod stacking)
- [x] 4-slot Mod Matrix (6 sources x 5 destinations, bipolar amount)
- [x] Global presets: full 130+ parameter reset before per-preset overrides
- [x] Fix: mod wheel + LFO filter modulation stacking (was overwrite)
- [x] Fix: wavetable PW/pitch now used as LFO base (was overwritten by voice defaults)

## Phase 7: Production Readiness

### Quick Wins

- [x] Pitch bend range APVTS + UI selector (±2–12 semitones, in Modulation popup)
- [x] LFO1/LFO2 + Pitch Bend Range moved to Modulation popup (was Mod Matrix)
- [x] PWM sweep (dedicated triangle oscillator, enable/rate/depth, UI in Modulation popup)

### Composition Tools

- [x] Chord Memory (4 slots x 5 intervals, trigger chords from single key, popup UI)

### Release Engineering

- [ ] DAW compatibility testing (Reaper, Ableton, FL Studio, Bitwig)
- [ ] macOS build (AU format)
- [ ] Linux build (LV2 format)
- [ ] Factory preset pack (curated patches showcasing all features)
- [ ] User documentation / manual
- [ ] Marketing assets (screenshots, demo audio)

### Deferred / High-Risk

- [ ] 3-SID expansion (9-voice architecture) — previous attempt caused oscillation; requires careful CPU/stability work

## Task Resolution Log

| Date | Feature | Resolution | Commit |
|------|---------|------------|--------|
| 2026-02-11 | Per-SID pan UI + dead code removal | Added leftPan/rightPan sliders to SID panels, removed all per-voice pan dead code (APVTS param, VoiceSettings, VoiceParamPtrs, serialization, preset lambdas) | `65113ae` |
| 2026-02-11 | NTSC frequency fix | `midiNoteToFrequency`/`noteOn`/`setFrequency` now use `getClockHz()` instead of hardcoded PAL | v0.9.2 |
| 2026-02-11 | Chip model cache churn | `processBlock` updates caches after applying models | v0.9.2 |
| 2026-02-11 | RT-unsafe logging | Removed `Logger::writeToLog` from audio thread | v0.9.2 |
| 2026-02-11 | External input bus | Real input bus via `isBusesLayoutSupported` | v0.9.2 |
| 2026-02-11 | Per-SID pan (engine) | `leftPan`/`rightPan` APVTS with equal-power pan law | v0.9.2 |
| 2026-02-11 | Competitive feature sprint | Filter env, chorus+delay, LFO2, wavetable, mod matrix, preset reset | Phase 6 |
| 2026-02-11 | WT PW/pitch overwrite fix | `applyLFOModulation` uses WT step as base; pipeline order-of-ops test | `c630455` |
| 2026-02-12 | Pitch bend range APVTS | `pitchBendRange` AudioParameterInt (2-12), processBlock sync, CC mapping writes APVTS, 4 integration tests | pending |
| 2026-02-12 | Modulation popup consolidation | LFO1/LFO2 + PB range moved from main editor to ModMatrixPanel popup, renamed "Modulation", reclaimed 124px vertical space | pending |
| 2026-02-12 | PWM Sweep | Dedicated triangle oscillator (enable/rate/depth), additive with LFO PW mod, UI in Modulation popup, 3 integration tests | pending |
| 2026-02-12 | Chord Memory | 4 slots x 5 intervals (-24..+24), mutually exclusive with arp, popup UI, global preset reset, 3 integration tests | pending |
| 2026-02-12 | Wavetable step editor | 16-step grid popup (waveform/pitch/PW per step), replaced inline controls, current-step indicator, 2 new presets (WT Arpeggio, WT Morph), 2 integration tests | pending |
