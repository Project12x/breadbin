# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Headless performance profiling and A/B render harness**: `BreadbinIntegrationTests` now supports
  `--cpu-profile --json <path>` section attribution and `--render-ab --out-dir <dir>` deterministic
  WAV rendering for S1 idle, S2 typical playing, S3 worst case, S4 decay-to-silence, and S5 processed
  external-input sweep/pink-burst scenarios. Initial T1 artifacts are kept under
  `releases/cpu_baseline_2026-06-10.json` and `releases/ab/88fa9f6fbf6c/`; the full-matrix baseline is
  `releases/cpu_baseline_matrix_2026-06-10.json` with references under `releases/ab/4c07888/`.
- **Preserve-tone CPU audit counters**: the headless CPU profile JSON now includes SID wrapper
  setter and register-write counters so no-op work can be targeted before any quality or gating
  changes.
- **Digi performance A/B scenario**: the deterministic render/profile matrix now includes
  `s6-digi-4bit`, a generated 4-bit `$D418` playback case used to protect DigiSampler output
  while optimizing same-value SID register writes.
- **C64 "Neon Synthwave" Reskin — Phase A (foundation)**: `BreadbinLookAndFeel` rebuilt on
  `gm::ui` synthwave renderers from `ghostmoon-oss`: 270° glowing rotary knobs, inset linear
  sliders with accent fill and metallic thumbs, glass panels, CRT phosphor scope displays for
  filter and LFO, glow-header chrome, and a cached `background_clean.png` + light vignette
  backdrop. Applied to the existing layout (no controls moved).
- **C64 "Neon Synthwave" Reskin — Phase B (OptionD re-layout + per-section colors)**: the main
  panel was re-laid-out to OptionD's structure — two mirror-symmetric SID towers with in-tower
  CRT filter graphs (slimmed to align with the cutoff/res controls below), the filter envelope
  relocated into the voice editor, FX as Chorus/Delay/Reverb rows, and a 2-tier dock. Per-section
  accent colors applied via the `accentOf` property: SID I cyan, SID II orange, voice editor
  magenta, ADSR + Filter Env + aux toggles (Ring/Sync/Arp/LFO/WT/Digi) + CPU greenyellow.
- **C64 "Neon Synthwave" Reskin — Phase C (popup glass chrome + C64 nostalgia)**: the 5 popups
  (Modulation, Wavetable, Chord, SID Player, Digi) restyled with the synthwave glass treatment —
  a generated neon-grid backdrop (dimmed), translucent glass, accent glow border, and a Press-Start
  glow title bar in each popup's accent — plus per-role control accents inside each popup. Reworked
  the Chord (inset interval table, magenta) and SID Player (sectioned layout, Path-drawn icon
  transport, REG⟷BASIC register dump, `LOAD"…",8,1` line, big title typography) panels; Wavetable
  per-step cards with active-step glow; floppy/tape button icons. The NEON⟷C64 scheme switch was
  dropped at the user's direction (NEON palette only). DPI-aware window scaling — a 75/100/125/150% selector in the top row,
  persisted per machine. The editor keeps a fixed 1000×800 logical layout and scales the whole
  window via an affine transform (`gm::ui::ScaledEditor` base), so existing layout code is untouched.
- **UI polish — filter value readouts**: the Cutoff and Resonance knobs on both SID towers now show
  their live numeric value (accent-colored JetBrains Mono, stacked under each label), filling the
  previously silent headline filter controls. Updated from the base parameter values the editor
  timer already reads; `Label::setText` no-ops when unchanged, so no added per-frame repaint.
- **UI polish — label readability pass**: control labels that were thin Lato Regular are now Lato
  Bold across the editor and all popups (slider labels, LFO Rate/Filter/PW/Pitch rows, ADSR, Master
  Vol / Noise Gate / Glide / Mod Offset / PWM / pitch-bend, Digi root-note/bit-depth, SID register
  overlay, etc.), with anything below the design kit's 10px floor lifted to 10px. Checkbox/toggle
  labels are enlarged to 12px and stay at full-brightness `txt` in both states (state is still shown
  by the dot indicator) instead of dimming to `txt2` when off. The small Press-Start pixel-font slider/control
  labels (Cutoff, Res, Detune, Pan, Waveform, FX, Filter-Env, clock mode) keep the C64 pixel
  aesthetic and are bumped 7px → 8px for legibility (typeface unchanged).
- **UI polish — hover feedback**: buttons and toggles now show a subtle accent edge + faint wash on
  mouse-over, rendered in Breadbin's `LookAndFeel` via the JUCE highlight flag (the shared `gm::ui`
  renderers are untouched). Knobs are not yet included (JUCE sliders need an extra repaint hook).
- **UI polish — top-bar logo emblem**: a Breadbin logo emblem is rendered in the top bar of the
  main editor.
- **UI polish — C64 neon keyboard**: the on-screen keyboard is restyled and enlarged. Keys auto-fit to
  fill the full window width (previously a cramped ~half-width strip), with dark gradient key caps,
  cyan neon-edged sharps, a cyan glow on press + faint cyan on hover, and Press-Start pixel-font
  octave labels (C2–C6). Implemented as a `C64Keyboard : juce::MidiKeyboardComponent` subclass with
  custom `drawWhiteNote`/`drawBlackNote` (no `gm::ui` change).
- **Phase D motion design spec** written at
  `docs/superpowers/specs/2026-06-07-c64-reskin-phase-d-motion-design.md`. Covers blinking C64
  cursor, opt-in animated mod rings (default OFF), and subtle scope scanline drift. Design only —
  Phase D is not yet implemented.
- **4-Mode Voice System**: Mono, Paraphonic (up to 6 notes on 2 SID engines, shared filter),
  Polyphonic (per-note SID pair allocation, up to 8 notes), and Poly+Para (paraphonic within
  each poly voice, up to 24 notes). Replaces the old polyEnable toggle with a voiceMode selector.
- **True Polyphony**: Each polyphonic note gets its own dedicated SID engine pair with independent
  filter, ADSR, and waveform. Voice stealing with configurable max notes (1-8).
- **Preset Voice Mode Assignments**: 32 factory presets upgraded from mono — 12 to Para (pads,
  organs, strings), 15 to Poly (leads, keys, brass, plucks), 5 to Poly+Para (chord stabs,
  modern arps, showcase). 41 presets remain Mono (C64 era-accurate, glide, unison, SID arps).
- **8 Showcase Presets**: Kitchen Sink, Sync Sculptor, Ring Cathedral, Matrix Express,
  WT Kaleidoscope, Chord Cathedral, Dual Worlds, and Glide Machine — each exercising a different
  feature cluster. Total global presets: 77.
- **Mod Matrix Activity Indicators**: LED-style dots near the Modulation button on the main
  panel light up for each enabled mod slot.
- **Paraphonic Voice Editor Hints**: In Para mode the voice tabs are annotated/greyed to show
  that filter and ADSR are shared from voice 0.
- **FX Bypass Dimming**: Disabled chorus/delay/reverb sections dim for clear visual feedback.

### Changed

- **ghostmoon-oss restructure**: The reusable UI/DSP shared library was restructured into a
  mixed-license library exposing three CMake targets under namespace `gm::` / include root
  `ghostmoon/`:
  - `ghostmoon_oss::dsp` (`<ghostmoon/ReverbSC.h>` family) — LGPL-2.1-or-later
  - `ghostmoon_oss::core` (`<ghostmoon/ui/Geometry.h>`, `<ghostmoon/ui/ScaledEditor.h>`) — MIT
  - `ghostmoon_oss::ui_synthwave` (`<ghostmoon/ui/synthwave/{Controls,Chrome,Scope,Theme}.h>`) — MIT
  Per-file SPDX headers; `LICENSES/MIT.txt` + `LGPL-2.1.txt` present. Breadbin consumes all
  three via `GHOSTMOON_OSS_DIR`.
- **ScaledEditor promoted to `gm::ui::ScaledEditor`**: Local `src/ScaledEditor.h` deleted;
  now consumed from `ghostmoon_oss::core`.
- **ReverbSC source corrected**: Local `src/dsp/ReverbSC.h` (mislabeled MIT) deleted; reverb
  now uses `gm::ReverbSC` from `ghostmoon_oss::dsp` (correctly LGPL-2.1-or-later).
- **Dual Mode Selector**: Multitimbral option hidden from UI (redundant with per-voice settings).
  Engine support retained for potential future use.
- **Old Preset Migration**: Saved presets with `polyEnable` parameter auto-migrate to equivalent
  `voiceMode` value on load.
- **DAW Tail Length**: `getTailLengthSeconds()` returns 10.0s (was 0.0) so DAWs render
  reverb/delay/SID release tails on bounce/freeze instead of cutting them off.
- **Internal Refactors**: `processBlock`, `handleMidiEvent`, and `timerCallback` split into named
  helpers; deduplicated LFO/filter-envelope/glide/pan code and the left/right SID panel setup;
  added conditional UI repaints (only on metered-value change) and a voice-settings dirty check to
  skip redundant per-block syncs. No behavior change.

### Fixed

- **Idle SID render CPU**: added a `gm::SilenceGate`-backed skip around fully silent
  mono/paraphonic SID rendering. Release-profile `idle-default` wall time dropped from 1818 us to
  24.5 us per 512-sample block; `SIDRender` dropped from 1787 us to 0.09 us. WAV A/B passed for
  idle, typical-playing, and full-stack references with diff RMS at or below -74.49 dBFS.
- **Exact pitch-ratio hoists**: moved block-constant pitch-bend/modulation `std::pow`
  calculations out of active-voice loops without changing polyphony, SID register ordering, or
  digi `$D418` writes. S3 `PolyMod` measured 4.24 us -> 3.92 us and `Modulation` 1.61 us ->
  1.49 us; full S1-S6 WAV A/B passed automatically against `releases/ab/1931533/`.
- **SID Player Snapshot**: `snapshotSidPlayerToAPVTS()` was writing to nonexistent APVTS parameter
  IDs (`leftCutoff`, etc.) and silently no-opping. Now writes filter cutoff/resonance/mode directly
  to both SID engines, so snapshotting a loaded `.SID` tune updates the filter controls.
- **Wavetable High-Rate Stepping**: Step advancement changed from `if` to `while` so wavetable
  rates above the block rate (~86 Hz) advance multiple steps per block instead of dropping them.
- **Voice-Steal Counter Wrap**: `polyNoteCounter` normalizes before `uint32_t` overflow, preventing
  the youngest voice from being mis-aged and stolen first after extreme note counts.

### Removed

- **Aging Factor ("Time Machine")**: Removed the per-block aging-cutoff offset — minimal sonic
  impact for real per-block CPU cost across all SID engines. Presets with a stale `aging` value
  ignore it on load.

## [0.9.6] - 2026-02-23

### Added

- **8 Chip Model Variants**: Expanded from 2 base SID models to 8 distinct chip profiles —
  MOS 6581 (standard), 6581 R2 (bright, weak CW), 6581 R3 (most common, slightly dark),
  6581 R4 (late revision, strong CW), MOS 8580 (standard clean), 8580 R5 (darker, strong CW),
  CSG 9580 (final production, bright), and 8580D (digiboost era, mellow). Each variant uses
  reSIDfp's filter curve, range, and combined waveform tuning APIs.
- **8-Bit Digi Mode**: Added bit depth selector for digi sampler (4-bit / 8-bit). 4-bit mode
  is authentic C64 $D418 volume register playback (crunchy, lo-fi). 8-bit mode stores samples
  at full resolution and mixes directly into the output bus, bypassing the 4-bit limitation
  for cleaner retro sound. Both modes support pitch tracking and looping.
- **Digi Sampler**: Authentic 4-bit $D418 volume register playback with WAV loading, MIDI pitch
  tracking, root note setting, and loop toggle. Gain compensation for digi-active voices.
- **Stereo Chip Defaults**: Left SID defaults to MOS 6581, right to MOS 8580 for immediate
  stereo width and tonal contrast out of the box.
- **LFO Tempo Sync**: LFO1 can lock to DAW BPM with selectable note divisions
  (1/1, 1/2, 1/4, 1/8, 1/16, 1/32). Free-running mode still available.
- **Sine LFO Waveform**: Added sine wave option to both LFO1 and LFO2 (Triangle/Saw/Square/S&H/Sine).

### Fixed

- **Master Volume**: Reworked from SID volume register to output gain. Master volume now
  controls all output (voices + digi in both 4-bit and 8-bit modes). Previously, digi
  playback overwrote the volume register and was unaffected by the master gain knob.
- **DC Offset / Drone**: Bumped DC blocker from 5Hz to 20Hz 2nd-order Butterworth. Faster
  at removing the MOS6581's significant idle DC offset while still below audible range.
  Eliminates persistent background drone that was previously masked by the noise gate.
- **Chip Defaults**: Fixed right SID defaulting to MOS 6581 R4 instead of MOS 8580
  (bumped APVTS version to clear stale state).
- **Voice Enable Toggles**: Fixed voice enable toggles not applying correctly in global presets.

## [0.9.5] - 2026-02-17

### Added

- **19 Factory Presets (batch 2)**: 10 classic C64 sounds (Monty Lead, Sanxion Buzz, Last Ninja,
  Delta Run, Cobra Bass, IK Lead, Turbo Saw, Times of Lore, Hawkeye Pluck, Deflektor Bell),
  5 modern modulation patches (Drift Pad, Arp Machine, Wobble Bass, Sequence Morph, Poly Chord),
  and 4 bonus distinct sounds (Follin Complex, Noise Drums, Arp Bass, Filter Scream).
- **Preset Expansion (batch 3)**: 23 additional global presets (47-69), including velocity-sensitive
  sounds (Brass Section, Retro EP, Velocity Keys), chord memory presets (Chord Pad),
  ring mod presets (Ring Mod Pad), strings (String Machine), harpsichord, and percussion
  ensemble. Total global presets: 69.
- **Voice Preset Expansion**: 18 new voice presets (21-38), including 10 promoted from
  iconic C64 ADSR shapes (Commando Pluck, Punchy Saw, Buzz Saw, etc.) and 8 gap-filling
  presets (Brass Saw, String Ensemble, Electric Piano, Harpsichord, Snare Roll, Tom,
  Ambient Swell, Rising Noise). Total voice presets: 37.
- **Voice Preset Submenus**: Categorized voice presets into 5 submenus
  (Leads, Bass, Pads & Keys, Percussion, FX & Utility).
- **Preset Menu Submenus**: Converted flat preset dropdown to categorized submenus
  (Leads, Bass, Pads & Keys, Arps & Sequences, FX & Modulation, Classic C64).
  5 curated favorites (Dual Lead, Commando, Drift Pad, Growl Bass, Chip Sequence)
  appear at the top level for quick access.
- **CPU Meter**: Real-time DSP load percentage displayed in top-right corner.
  Color-coded: grey (<50%), orange (<80%), red (>=80%).
- **Preset Navigation**: Previous/next buttons flanking the preset selector
  for sequential browsing through all presets with wrapping.
- **Preset Dirty Detection**: Asterisk indicator when current state differs from loaded preset.

### Fixed

- **Editor State Restoration**: Filter cutoff/resonance, preset selection, and voice
  parameters now persist correctly when closing/reopening the editor and when
  saving/reloading DAW projects. Serialized non-APVTS filter state to ValueTree,
  removed conflicting `saveUIToVoice()` callbacks on APVTS-attached controls,
  and eliminated hardcoded preset selector default.
- **Chord/Arp Exclusivity**: Chord memory no longer populates arpeggiator
  held-note state, and arp stepping is bypassed while chord mode is active, avoiding
  stale arp behavior when both modes are enabled by automation/state.
- **Idle LFO Modulation**: Stopped idle LFO from writing PW/pitch modulation values
  when no voices are active, preventing artifacts on note-on.
- **Preset Voice Consistency**: Fixed vpIds, ring mod settings, and voice mixing
  across all factory presets.
- **SID Produces Sound Without Editor**: Fixed headless/no-editor scenarios where
  SID engines were not properly initialized until editor opened.

### Changed

- **Chord Voice Allocation**: Chord memory now allocates root + up to 5 intervals
  across both SIDs (up to 6 notes total in Stereo/Unison), aligning playback with
  the 5-interval chord editor.

## [0.9.4] - 2026-02-12

### Added

- **SID File Player**: Load and play .SID/.PSID files from the HVSC (50,000+ C64 tunes) using
  the full libsidplayfp engine (6502 CPU, CIA, VIC, MMU). Background thread produces audio via
  lock-free ring buffer, mixed additively into plugin output alongside synth voices.
  - Popup UI panel with file browser, transport (play/stop/pause), tune metadata (title, author,
    released), sub-tune selector, volume slider, and real-time hex register display
  - Register overlay on main editor: cyan labels on corresponding controls (waveform, PW, ADSR,
    cutoff, resonance) show live SID register values during playback at 30Hz
  - Snapshot button maps all 32 SID registers to APVTS parameters for instant preset creation
    from any .SID tune
- **Chord Memory**: 4 chord slots with up to 5 semitone intervals each (-24 to +24),
  trigger chords from single keys. Mutually exclusive with arpeggiator. Separate popup
  UI with slot selection and interval sliders. 22 new APVTS parameters.
- **PWM Sweep**: Dedicated triangle-only pulse width sweep oscillator (enable/rate/depth),
  independent from LFOs, additive with all existing PW modulation. UI row in Modulation popup.
- **Pitch Bend Range APVTS**: `pitchBendRange` AudioParameterInt (2-12, default 2) with
  processBlock sync, replacing direct `setPitchBendRange()` calls with single source of truth.
- **Wavetable Step Editor Popup**: Full 16-step grid editor with per-step waveform ComboBox,
  pitch slider (-24..+24), and PW slider (0..4095). Current-step indicator with cyan highlight.
  Replaces ambiguous inline controls.
- **Mod Matrix Slot Enable**: Per-slot enable/bypass control with destination total readouts
  (Filter/PW/Pitch/Res) in popup.
- **Wavetable Step Edit Operations**: Shift left, shift right, randomize, and clear for
  active steps in wavetable popup.
- **Modulation Popup**: Consolidated LFO1, LFO2, pitch bend range, and mod matrix
  into a single "Modulation" dialog. Reclaimed 124px of main editor vertical space.
- **Chord Memory Presets**: Save/Load buttons with .chords XML file format.
  6 factory presets (Major Triad, Minor Triad, 7th, Sus4, Power Chord, Octaves).
- **Wavetable Presets**: Save/Load buttons with .wtsteps XML file format.
  6 factory presets (Classic Sweep, Arp Up, Random S&H, PWM Cycle, Noise Burst, Waveform Morph).
- **2 New Presets**: "WT Arpeggio" (classic C64 wavetable chord arpeggio at 50Hz) and
  "WT Morph" (8-step timbral morphing sequence).
- 13 new integration tests (pitch bend range, PWM sweep, chord memory, wavetable).

### Fixed

- **PWM Cycle Preset**: Fixed triangle sweep direction.
- **Wavetable Preset Indices**: Fixed waveform indices (Choice is 0-indexed, not 1-indexed).

## [0.9.3] - 2026-02-11

### Added

- **Universal MIDI Learn**: All interactive controls now support right-click MIDI Learn/Unlearn.
  Extended `ControlParam` enum with 37 new entries. Created `MappableToggle` and `MappableComboBox`
  widget classes. Converted all FX sliders, filter envelope controls, pan sliders, PWM sweep
  controls, toggle buttons, and combo boxes to their Mappable variants.
- **MIDI Learn Overlay**: Auto-dismiss with green "MAPPED" success flash and 1-second fade-out.
- **User Preset Saving**: Save button shows popup with "Save to File" and "Save to Preset Menu".
  User presets stored as `.breadbin` XML files in `%APPDATA%/GPLAudio/Breadbin/Presets/`.
  Appear in "User Presets" section of global preset dropdown. Persist across sessions.
- **ASIO Standalone Support**: Added `JUCE_ASIO=1` compile definition and Steinberg ASIO SDK
  include path. ASIO devices now appear in standalone audio settings.
- **Adjustable Noise Gate**: Threshold controllable via slider (0-0.1, default 0.01 / ~-40dB).
  Set to 0 to disable gating.
- **Tooltip Audit**: Added tooltips to mod matrix source/destination/amount controls,
  LFO depth sliders, and all interactive controls.
- **Popup Panel Styling**: Standardized title decorations (glow pill + divider) across all popups.

### Changed

- **Manufacturer**: Updated from "GPL Audio" to "Eric Steenwerth" in CMakeLists.txt.

## [0.9.2] - 2026-02-11

### Added

- **External Input Bus**: Added real input bus named "External Input" (`isBusesLayoutSupported`) so
  external audio routes through a proper host input, not a feedback loop from the output buffer.
- **Per-SID Pan**: `leftPan` and `rightPan` APVTS params with standard semantics
  (-1=left, 0=center, +1=right). Left SID defaults -1, right SID defaults +1.
  Equal-power pan law (cos/sin). Pan sliders exposed in each SID panel's UI.
- **Per-Voice Mod Offset**: New `modOffset` parameter per voice (0-12 semitones) controlling
  the frequency relationship for hard sync and ring mod. UI slider in voice editor.
- **Envelope-Following Gate**: Replaced hard-threshold noise gate with envelope-following gate
  for smoother gating behavior. Fixed ring mod presets that were incorrectly gated.
- **Font Overhaul**: Switched to Lato (UI text) + JetBrains Mono (values/readouts).
- **Scrolling LFO Display**: Animated waveform display with Hz suffix, capped rate at 10 Hz.
- **Visual Polish**: Panel borders, synthwave vignette, rounded popup corners.

### Fixed

- **NTSC Frequency**: `midiNoteToFrequency`, `noteOn`, `setFrequency` now use
  `getClockHz()` instead of hardcoded `SID_CLOCK_PAL`, giving correct pitch in NTSC mode.
- **Chip Model Cache Churn**: `processBlock` now updates `chipModelLeft`/`chipModelRight`
  caches after applying new models, eliminating per-block re-initialization.
- **RT-Unsafe Logging**: Removed `Logger::writeToLog` calls from the audio thread
  (sustain pedal and MIDI learn debug messages).
- **RT Safety Audit**: Eliminated heap allocations and blocking calls on audio thread.
- **Hard Sync Audibility**: Fixed carrier voice offset direction — was offsetting modulator
  voice down (inaudible), now offsets carrier voice up for proper harmonic content.
- **Sync/Ring Mod in Presets**: Fixed sync and ring mod flags not being applied to SID engine
  when loading global presets.
- **LFO Rate Slider**: Fixed sensitivity and value clipping, switched to TextBoxRight layout.
- **Dead Code Removal**: Removed legacy per-voice pan APVTS parameter, serialization,
  and preset references (superseded by per-SID pan).
- **UI Rendering**: Fixed preset navigation buttons showing "..." truncation.

## [0.9.1] - 2026-02-11

### Fixed

- **LFO Waveform Mapping**: Removed ghost "Sine" entry from APVTS `lfoWave` parameter.
  APVTS indices now map 1:1 to `LFOWaveform` enum {Triangle=0, Sawtooth=1, Square=2, S&H=3}.
  Old saved states are backward-safe. State restore now round-trips correctly.
- **APVTS Dual-Path Conflicts**: Removed redundant `onChange`/`onClick` callbacks and
  manual preset-load refresh for APVTS-attached LFO controls.
- **Master Volume SID Sync**: `processBlock` now calls `setMasterVolume()` from APVTS
  value each block, fixing headless/automation scenarios.
- **getSampleRate()**: Added `setRateAndBufferSizeDetails()` in `prepareToPlay()` so
  `getSampleRate()` returns correct value in headless/test mode.

## [0.9.0] - 2026-02-07

### Added

- **MIDI Learn System**: Right-click context menu for all sliders with "MIDI Learn" and "Unlearn".
- **Visual Feedback**: Gold "LEARN" badge on sliders and global centered popup indicating mapping target.
- **Master Volume**: Global volume control (0.0 to 1.0) persisting in state.
- **Sustain Pedal**: CC 64 support with note queue management.
- **State Persistence**: All MIDI mappings automatically saved/loaded with plugin state.
- **Diagnostic Logging**: Integrated spdlog-style debug logging for MIDI and pedal activity.

## [0.2.0] - 2026-02-03

### Added

- Dual SID engine support with three stereo modes:
  - **Stereo Split**: Left SID -> Left channel, Right SID -> Right channel
  - **Unison**: Both SIDs summed to stereo with detune
  - **Multitimbral**: Voice 1 left, Voice 2 right, Voice 3 center
- Per-channel SID chip selection (6581/8580)
- Virtual MIDI keyboard for standalone testing
- Waveform selector (Triangle, Sawtooth, Pulse, Noise)
- ADSR envelope controls
- Filter controls (Cutoff, Resonance, LP/BP/HP modes)
- Preset system with C64 classics (Last Ninja, Monty)
- Safety limiter to prevent harsh audio artifacts

### Fixed

- Audio output from virtual keyboard
- Waveform selection affecting sound

## [0.1.0] - 2026-02-03

### Added

- Initial project scaffold
- JUCE 8 + reSIDfp integration
- Basic SIDEngine wrapper class
- VST3 and Standalone build targets
