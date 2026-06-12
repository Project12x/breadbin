# Project State

**Last Updated**: 2026-06-12

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
- Chorus, Stereo Delay (independent L/R times), Reverb (`gm::ReverbSC` from `ghostmoon-oss`, LGPL-2.1)

#### Extras
- SID File Player (.SID/.PSID via full libsidplayfp engine, background thread + ring buffer, live register overlay, snapshot registers to APVTS)
- 77 global presets + 37 voice presets (categorized submenus)
- Universal MIDI Learn (right-click any control)
- User preset saving (`%APPDATA%/GPLAudio/Breadbin/Presets/`)
- State persistence (XML via APVTS) + full parameter automation
- ASIO standalone support

### In Progress / Recent (branch `perf/breadbin-optimization-20260610`)
- **C64 "Neon Synthwave" Reskin Phase A — DONE**: `BreadbinLookAndFeel` rebuilt on `gm::ui`
  synthwave renderers (glowing knobs, CRT scopes, glass panels, glow headers, cached backdrop).
  `gm::ui::ScaledEditor` promoted from local `src/ScaledEditor.h`; `gm::ReverbSC` replaces
  deleted local `src/dsp/ReverbSC.h`. Tag: `checkpoint/phase-a-foundation`.
- **C64 "Neon Synthwave" Reskin Phase B — DONE**: OptionD re-layout — two mirror-symmetric SID
  towers with in-tower CRT filter graphs, filter envelope relocated into the voice editor, FX as
  Chorus/Delay/Reverb rows, 2-tier dock. Per-section accent colors (SID I cyan, SID II orange,
  voice editor magenta, ADSR/Filter Env/aux/CPU greenyellow) via the `accentOf` property.
  Tag: `checkpoint/phase-b-layout`.
- **C64 "Neon Synthwave" Reskin Phase C — DONE**: the 5 popups restyled with glass chrome (generated
  neon-grid backdrop, glass, accent glow border, glow title bar) + per-role accents; Chord inset
  interval table, SID Player sectioned layout with icon transport + REG⟷BASIC register dump + LOAD
  line, Wavetable per-step cards, floppy/tape button icons. NEON⟷C64 scheme switch dropped (NEON
  only). Tag: `checkpoint/phase-c-popups`.
- **UI Polish sub-phase (post-Phase-C) — DONE**:
  - Filter value readouts: Cutoff/Res knobs on both SID towers show live numeric values (JBMono, accent color, updated from editor timer).
  - Label readability pass: control labels promoted from Lato Regular to Lato Bold; sub-10px labels raised to 10px floor; toggle labels to 12px, full-brightness in both states; Press-Start pixel labels bumped 7px→8px.
  - Hover feedback: buttons and toggles show accent edge + faint wash on mouse-over via `BreadbinLookAndFeel` JUCE highlight flag (knobs pending).
  - C64 neon keyboard: `juce::MidiKeyboardComponent` replaced by `C64Keyboard` subclass — dark gradient key caps, near-black sharps with cyan neon top edge, cyan glow on press + hover, Press-Start octave labels (C2–C6), key width auto-fit to full window width.
- **Phase D motion design — SPEC WRITTEN, NOT IMPLEMENTED**: design spec at
  `docs/superpowers/specs/2026-06-07-c64-reskin-phase-d-motion-design.md` covers blinking C64
  cursor, opt-in animated mod rings (default OFF), and subtle scope scanline drift. Parked/pending.
- **CPU profiling pass T1 — LANDED**: headless `--cpu-profile` section attribution and
  `--render-ab` deterministic WAV references are wired into `BreadbinIntegrationTests`.
  Fully silent mono/paraphonic SID rendering now skips after output energy decays, using
  `gm::SilenceGate` from `ghostmoon-oss`. Release profile: idle-default 1818 us -> 24.5 us
  per 512-sample block; typical-playing 1822 us -> 1650 us; full-stack 4068 us -> 2987 us.
  WAV A/B passed automatically for idle, typical-playing, and full-stack.
- **CPU full matrix baseline — PINNED**: the performance harness now covers S1 idle,
  S2 typical polyphony, S3 honest worst case, S4 decay-to-silence, and S5 processed input
  sweep/pink burst. Matrix references are under `releases/ab/4c07888/`; baseline JSON is
  `releases/cpu_baseline_matrix_2026-06-10.json`. S3 remains over budget at 13412.6 us
  per 512-sample block, dominated by `SIDRender` at 13285.25 us.
- **Preserve-tone audit counters — PINNED**:
  `releases/cpu_audit_counters_2026-06-10.json` records SID setter/register no-op ratios for
  the full S1-S5 matrix. The next optimization target is same-value SID register/write guards,
  selected from this evidence before any quality or gating reduction. Its wall/section timings
  are run context only; `releases/cpu_baseline_matrix_2026-06-10.json` remains the pinned timing
  baseline until the next rebaseline.
- **T2 digi A/B coverage — PINNED**:
  `releases/ab/1931533/` expands the pre-T2 reference set to S1-S6 with
  `s6-digi-4bit`, a deterministic 4-bit `$D418` playback case. The profile probe measured
  S6 at 1074.1 us wall avg, 1049.87 us `SIDRender`, and 1738066/1760554 same-value register
  writes.
- **Exact pitch-ratio hoists — LANDED**:
  block-constant pitch-bend/modulation `std::pow` ratios are hoisted out of active-voice loops
  without changing SID call order, polyphony behavior, register sequencing, or digi `$D418`
  writes. S3 section-local profile moved `PolyMod` 4.24 us -> 3.92 us and `Modulation`
  1.61 us -> 1.49 us; full S1-S6 WAV A/B passed automatically with no flagged pairs.
- **Poly release SID render gate — CANDIDATE, LISTEN FLAGGED**:
  released poly voices now use `gm::SilenceGate` on their own rendered output tail to skip retained
  reSIDfp `clock()` calls without freeing the voice slot or changing allocation/stealing state.
  Candidate profile recorded `polySidRenderSkipBlocks=2695` in S3; final S3 SIDRender moved
  9990 us -> 9150.75 us, while other scenarios were neutral/noisy. WAV A/B flags only
  `s3-worst-case` for listening (`diff RMS -30.66 dBFS`, centroid delta 1.131%):
  `releases/ab/exact_hoists_2026-06-11/s3-worst-case.wav` vs
  `releases/ab/poly_release_gate_2026-06-12/s3-worst-case.wav`.

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
| BreadbinIntegrationTests | 409 | Pass |

Run: `ctest --test-dir build -C Release`

## Performance Baseline

Release CPU artifacts:

| Scenario | matrix wall | top section |
|----------|------------:|-------------|
| s1-idle-default | 24.8 us | Limiter 12.8 us; SIDRender 0.10 us |
| s2-typical-playing | 5646.8 us | SIDRender 5589.98 us |
| s3-worst-case | 13412.6 us | SIDRender 13285.25 us |
| s4-decay-to-silence | 1886.2 us | SIDRender 1807.72 us |
| s5-input-sweep | 1732.2 us | SIDRender 1633.08 us |
| s5-input-pink-burst | 1543.5 us | SIDRender 1455.43 us |

Artifacts: `releases/cpu_baseline_2026-06-10.json`,
`releases/cpu_after_t1_2026-06-10.json`, `releases/ab/88fa9f6fbf6c/`, and
`releases/ab/t1_2026-06-10/` for the initial T1 pass; full matrix references
and baseline are `releases/ab/4c07888/` and
`releases/cpu_baseline_matrix_2026-06-10.json`. Preserve-tone audit counters
are pinned at `releases/cpu_audit_counters_2026-06-10.json`; use that artifact
for no-op ratios, not as a replacement timing baseline. Pre-T2 A/B references
including deterministic digi playback are under `releases/ab/1931533/`.
The exact-hoist pass is captured in
`releases/cpu_after_exact_hoists_2026-06-11.json` and
`releases/ab/exact_hoists_2026-06-11/`.

## Directory Structure

```text
breadbin/
├── src/
│   ├── PluginProcessor.cpp/h    # Audio processing, dual SID, voice modes, modulation, FX, safety
│   ├── PluginEditor.cpp/h       # UI editor, BreadbinLookAndFeel (uses gm::ui renderers), popups (~8k lines)
│   ├── SIDEngine.cpp/h          # reSIDfp wrapper, 8 chip model profiles
│   ├── SidFilePlayer.cpp/h      # .SID file playback (full libsidplayfp engine)
│   ├── DigiSampler.h            # WAV digi sample (4-bit / 8-bit storage)
│   └── residfp/                 # Local config headers for reSIDfp build
│   (ScaledEditor.h → gm::ui::ScaledEditor in ghostmoon-oss; dsp/ReverbSC.h → gm::ReverbSC)
├── tests/
│   ├── LFOTests.cpp             # LFO waveform math (standalone)
│   ├── MutationTests.cpp        # Mutation coverage (standalone)
│   └── IntegrationTests.cpp     # Full signal-path integration/regression (JUCE-linked)
├── assets/                      # background_clean.png, logo.png, fonts (Lato, JetBrains Mono, etc.)
├── releases/                    # preserved build artifacts (per build-preservation policy)
└── build/                       # CMake output; JUCE 8 + libsidplayfp + ghostmoon-oss fetched via CPM/GHOSTMOON_OSS_DIR
```
