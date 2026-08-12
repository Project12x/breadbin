# Roadmap

## Phase 1: Core Foundation ✅

- [x] Project scaffold with JUCE 9 + reSIDfp
- [x] SIDEngine wrapper class
- [x] Dual SID architecture
- [x] Basic audio processing pipeline

## Phase 2: Dual SID Modes ✅

- [x] Stereo Split mode
- [x] Unison mode with detune
- [x] Multitimbral mode (engine retained; hidden from UI — redundant with per-voice settings)

## Phase 3: Editor UI ✅

- [x] C64-inspired aesthetic with synthwave background
- [x] Mode and model selectors
- [x] ADSR envelope controls
- [x] Filter controls (cutoff, resonance, LP/BP/HP)
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
- [x] LFO modulation (Triangle/Saw/Square/S&H/Sine → filter/PW/pitch)
- [x] LFO tempo sync with DAW BPM tracking
- [x] Ring modulation (Voice N × Voice N-1)
- [x] Hard sync (Voice N resets Voice N-1)
- [x] Per-voice mod offset for sync/ring mod audibility
- [x] Portamento / Glide (monophonic legato, linear Hz interp)
- [x] Per-SID detune (±50 cents)
- [x] Per-voice filter routing (filter enable toggle per voice)
- [x] PAL/NTSC clock switching (985,248 / 1,022,727 Hz)
- [x] 14-bit pitch bend + mod wheel → filter
- [x] External audio input (sidechain bus through SID filter)
- [x] Per-SID pan (equal-power pan law, APVTS-exposed with UI sliders)
- [x] MIDI Learn system (global CC mapping with visual feedback, auto-dismiss overlay)
- [x] Master Volume & Sustain Pedal
- [x] Preset system (per-voice and global presets with selector UI)

## Phase 6: Competitive Feature Sprint ✅

- [x] Filter Envelope (ADSR, bipolar amount, stacks with mod wheel + LFO)
- [x] Built-in FX: Chorus (JUCE DSP) + Stereo Delay (independent L/R times)
- [x] Second LFO (LFO2, identical structure, independent controls)
- [x] Wavetable step sequencer (16 steps, per-step waveform/PW/pitch, WT base -> LFO mod stacking)
- [x] 4-slot Mod Matrix (6 sources x 5 destinations, bipolar amount, per-slot enable)
- [x] Global presets: full 130+ parameter reset before per-preset overrides
- [x] Fix: mod wheel + LFO filter modulation stacking (was overwrite)
- [x] Fix: wavetable PW/pitch now used as LFO base (was overwritten by voice defaults)

## Phase 7: Production Readiness

### Quick Wins ✅

- [x] Pitch bend range APVTS + UI selector (±2–12 semitones, in Modulation popup)
- [x] LFO1/LFO2 + Pitch Bend Range moved to Modulation popup (was Mod Matrix)
- [x] PWM sweep (dedicated triangle oscillator, enable/rate/depth, UI in Modulation popup)

### Composition Tools ✅

- [x] Chord Memory (4 slots x 5 intervals, trigger chords from single key, popup UI)
- [x] SID File Player (load .SID/.PSID from HVSC, playback with register overlay, snapshot to APVTS presets)
- [x] SID register hex display + live overlay on main editor controls

### Sound Engine ✅

- [x] Expanded chip variants (4 → 8 models: MOS 6581, 6581 R2, 6581 R3, 6581 R4, 8580, 8580 R5, CSG 9580, 8580D)
- [x] 8-bit digi mode (direct mix path bypassing 4-bit $D418 limitation)
- [x] Master volume reworked as output gain (affects voices + digi in both modes)
- [x] DC blocker improved (5Hz → 20Hz, eliminates SID idle DC offset)
- [x] Digi sampler (4-bit authentic $D418 playback with WAV loading, pitch tracking, loop)
- [x] Envelope-following noise gate (replaced hard-threshold gate, adjustable threshold)
- [x] RT safety audit (eliminated heap allocs and blocking on audio thread)

### Polyphony ✅

- [x] True polyphony with per-note SID pair allocation (up to 8 simultaneous notes)
- [x] 4-mode voice system (Mono / Paraphonic / Polyphonic / Poly+Para)
- [x] Paraphonic mode (up to 6 notes across 2 SID engines, shared filter)
- [x] Poly+Para mode (paraphonic within each poly voice, up to 24 notes)
- [x] Voice stealing with configurable max notes
- [x] Per-mode MIDI routing, arpeggiator, chord memory, and sustain pedal integration
- [x] Factory preset voice mode assignments (32 presets upgraded from mono)

### UI Polish ✅

- [x] Chord/Arp exclusivity hardening (event pipeline + UI auto-disable)
- [x] Chord Memory dual-SID note allocation (up to 6 notes in Stereo/Unison)
- [x] Mod Matrix slot enable toggles + destination total readouts
- [x] Wavetable step operations (shift left/right, randomize, clear)
- [x] Preset browser with categorized submenus + 5 curated favorites
- [x] Preset navigation buttons (prev/next with wrapping)
- [x] CPU meter (real-time DSP load, color-coded)
- [x] User preset saving (Save to File / Save to Preset Menu, persists in %APPDATA%)
- [x] Font overhaul (Lato + JetBrains Mono)
- [x] Visual polish pass (panel borders, vignette, rounded popups)
- [x] Comprehensive tooltip coverage
- [x] Scrolling LFO display with Hz suffix
- [x] Preset dirty detection indicator
- [x] ASIO standalone support

### Post-Phase-C UI Polish ✅

- [x] Filter value readouts (Cutoff/Res live numeric values, JBMono accent color, both SID towers)
- [x] Label readability pass (Lato Regular → Bold; 10px floor; toggle labels 12px, full-brightness in both states; Press-Start pixel labels 7px→8px)
- [x] Hover feedback on buttons/toggles (accent edge + faint wash via JUCE highlight flag in `BreadbinLookAndFeel`)
- [x] C64 neon keyboard (`C64Keyboard` subclass — dark gradient caps, cyan neon sharps, press/hover glow, pixel-font octave labels, auto-fit width)
- [x] Top-bar logo emblem

### C64 Neon Synthwave Reskin — Phase D (Motion Design) — PENDING

- [ ] Blinking C64 cursor animation
- [ ] Opt-in animated mod rings (default OFF)
- [ ] Subtle scope scanline drift
- Spec written: `docs/superpowers/specs/2026-06-07-c64-reskin-phase-d-motion-design.md`

### Release Engineering

- [ ] DAW compatibility testing (Reaper, Ableton, FL Studio, Bitwig)
- [ ] macOS build (AU + VST3 format)
- [x] Factory preset pack (77 global presets, 37 voice presets, categorized submenus)
- [ ] Hard sync audibility study (per-voice pitch offset / cross-SID sync for C64-accurate behavior)
- [ ] Ring mod audibility study (voice frequency relationships for proper inharmonic sidebands)
- [ ] User documentation / manual
- [ ] Marketing assets (screenshots, demo audio, demo video)
- [ ] Code signing (Microsoft Trusted Signing)
- [ ] Windows installer (Inno Setup)
- [ ] macOS installer + Apple notarization
- [ ] GitHub Actions CI/CD for cross-platform builds

### Cancelled

- [x] ~~3-SID expansion (9-voice architecture)~~ — Cancelled. Dual SID (6 voices) is the final architecture.

## Phase 8: Sound Engine Enhancements

### Effects Chain

- [x] Reverb (algorithmic — the biggest gap in the current FX chain)
- [ ] Bitcrusher / decimator (thematic fit for chiptune aesthetic)
- [ ] Phaser / flanger
- [ ] Simple EQ (tilt or high/low shelf)
- [ ] Compressor (tame poly mode dynamics)

### Filter & Voice

- [ ] Filter key tracking (cutoff follows pitch, essential for polyphonic playing)
- [ ] Velocity-to-amplitude (direct velocity sensitivity beyond mod matrix)
- [ ] Legato mode (distinct from glide — hold envelope vs retrigger on overlapping notes)
- [ ] Voice priority modes (last/first/highest/lowest note priority for mono/para)

### Modulation

- [ ] Aftertouch as mod source (channel pressure for expressive playing)
- [ ] LFO key sync (restart LFO phase on each note-on)
- [ ] Additional LFO shapes (ramp down, exponential, stepped)
- [ ] Envelope follower (sidechain-style modulation from input)
- [ ] Per-voice modulation in poly mode (each note gets its own LFO phase)

## Phase 9: UI/UX Modernization

- [~] Resizable GUI — *in progress*: transform-based DPI scaling (`ScaledEditor`, 75–150% selector persisted per machine). Free/proportional resize still pending.
- [ ] Visual voice allocation display (show active SID voices in real-time per mode)
- [ ] A/B comparison (toggle between two parameter states)
- [ ] Oscilloscope / waveform display
- [ ] Copy/paste voice settings between voices
- [ ] Randomize button (full or per-section)
- [ ] Preset search/filtering within category browser
- [ ] Dark/light theme options

## Phase 10: Performance & Sequencer

- [~] Manual ECO performance mode with Settings popup — implemented and measured; blocked on S3 ECO-off listening/baseline acceptance
- [~] Hybrid Poly SID Budget: one stereo anchor note, added notes alternate L/R SID engines — S7 measured at 2842.3 us wall / 2743.91 us `SIDRender`
- [ ] Max ECO Poly SID Budget: one SID engine per poly note
- [ ] reSIDfp fork/API optimization path for lower per-engine SIDRender cost
- [ ] Auto ECO Budget with visible status, thresholds, and hysteresis — future work after Manual ECO listening validation
- [ ] More arp patterns (programmable user patterns)
- [ ] Arp MIDI output (DAW records arpeggiated notes)
- [ ] Arp hold/latch mode
- [ ] Built-in step sequencer (note sequencing beyond wavetable timbral steps)

## Phase 11: Format & Platform Expansion

- [ ] CLAP format (Bitwig, growing ecosystem)
- [ ] AAX format (Pro Tools)
- [ ] Linux VST3

## Phase 12: Advanced / Niche

- [ ] MPE support (per-note pitch/pressure/slide from Seaboard/Linnstrument)
- [ ] Microtuning (scale/tuning file support — .scl/.tun)
- [ ] Oversampling option (reduce aliasing at higher CPU cost)

## Task Resolution Log

| Date | Feature | Resolution | Commit |
|------|---------|------------|--------|
| 2026-06-12 | ECO Poly SID Budget | Manual ECO Settings popup and Hybrid budget implemented; S7 dense-poly profile passes 6500 us target, but S3 ECO-off A/B against `poly_release_gate_2026-06-12` is flagged for listening/baseline acceptance | pending |
| 2026-06-05 | UI scaling (in progress) | `ScaledEditor` transform-based DPI scaling + 75–150% selector persisted per machine; docs synced to current state | pending |
| 2026-04-21 | ReverbSC MIT swap | Replaced proprietary ghostmoon ReverbSC link with bundled local MIT copy, removed proprietary dependency | `23f76f3` |
| 2026-03-03 | UI polish pass | Conditional repaints, paraphonic voice hints, FX bypass dimming, mod matrix activity indicators | `8b5e1f0` |
| 2026-03-03 | Polish refactors (Phases 3–6) | Extracted processBlock/handleMidiEvent/timerCallback helpers; deduped LFO/filter-env/glide/pan; merged SID panels; voice-settings dirty check | `30679c8`..`85580f7` |
| 2026-03-03 | Polish fixes (Phases 1–2) | Removed aging factor; fixed SID-player snapshot param IDs; polyNoteCounter wrap safety; tail length 10s; wavetable high-rate stepping | `52231d9`..`331cf04` |
| 2026-02-17 | Preset expansion | 69 global presets (velocity, chord, ring mod), 37 voice presets (categorized submenus), headless sound fix | `83a830b` |
| 2026-02-17 | Composition UI polish pass | Hardened chord/arp exclusivity; chord dual-SID note allocation; added mod-slot enable + destination totals; added wavetable step ops (shift/randomize/clear) | pending |
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
| 2026-02-12 | SID File Player | Load .SID files via libsidplayfp full engine, background thread + ring buffer playback, register overlay on main UI, snapshot registers to APVTS presets, popup UI with transport/info/hex display | pending |
| 2026-02-23 | Chip variant expansion | 4 → 8 chip models (6581 R2/R3, 8580 R5, CSG 9580) using reSIDfp filter curve/range/CW tuning | `aed8cef` |
| 2026-02-23 | 8-bit digi mode | Dual 4-bit/8-bit storage, direct mix path bypassing $D418, bit depth selector UI | `aed8cef` |
| 2026-02-23 | Master volume fix | Reworked as output gain (was SID volume register), now affects voices + digi in all modes | `aed8cef` |
| 2026-02-23 | DC blocker improvement | 5Hz → 20Hz 2nd-order Butterworth, eliminates SID idle DC offset/drone | `aed8cef` |
| 2026-02-23 | True polyphony | 4-mode voice system (Mono/Para/Poly/Poly+Para), per-note SID pair allocation, voice stealing, 406 integration tests | `1f24bce` |
| 2026-02-23 | Preset voice modes | 32 factory presets assigned to Para/Poly/Poly+Para modes | `a383718` |
| 2026-02-23 | Multitimbral UI hide | Removed from dual mode selector, engine retained | `dcfb3cd` |
