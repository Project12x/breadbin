# Architecture

## Overview

Breadbin is a JUCE 8 VST3/Standalone plugin. It uses a standard JUCE `AudioProcessor` +
`AudioProcessorEditor` split. The audio thread runs entirely in `BreadbinProcessor::processBlock`;
the UI thread lives in `BreadbinEditor` and its popup panels. All parameter communication between
the two goes through JUCE's `AudioProcessorValueTreeState` (APVTS); non-APVTS state (e.g. MIDI
learn mappings, SID player position) is serialized manually in `getStateInformation` /
`setStateInformation`.

## Components

### Audio Engine (`src/PluginProcessor.cpp/h`)

- **`BreadbinProcessor`** — top-level `AudioProcessor`. Owns two `SIDEngine` instances (left/right),
  handles all voice allocation (Mono / Paraphonic / Polyphonic / Poly+Para), MIDI event dispatch,
  modulation pipeline (LFOs, Filter Envelope, Mod Matrix, PWM Sweep), and the FX chain
  (Chorus → Stereo Delay → ReverbSC → safety chain).
- **Safety chain** (audio-thread output path): DC blocker (20 Hz 2nd-order Butterworth) →
  ultrasonic LP → limiter → envelope-following noise gate.
- **`SIDEngine` (`src/SIDEngine.cpp/h`)** — thin wrapper around reSIDfp. Exposes 8 chip model
  profiles (MOS 6581, 6581 R2/R3/R4, MOS 8580, 8580 R5, CSG 9580, 8580D) via reSIDfp's
  filter-curve, range, and combined-waveform tuning APIs. One instance per physical SID.
- **`SidFilePlayer` (`src/SidFilePlayer.cpp/h`)** — runs the full libsidplayfp engine (6502 CPU,
  CIA, VIC, MMU) on a background thread; produces audio into a lock-free ring buffer that
  `processBlock` mixes additively into the output.
- **`DigiSampler` (`src/DigiSampler.h`)** — WAV sample player. 4-bit mode routes through the
  $D418 SID volume register (authentic C64 digi); 8-bit mode mixes directly into the output bus.

### UI (`src/PluginEditor.cpp/h`)

- **`BreadbinEditor`** — subclass of `gm::ui::ScaledEditor` (from `ghostmoon-oss::core`), which
  applies a fixed 1000×800 logical layout scaled via an affine transform. Inherits
  `juce::MidiKeyboardState::Listener` and `juce::Timer` (30 Hz tick for live readouts and the SID
  register overlay). Owns all top-level controls and delegates popup sections to dedicated classes.
- **`BreadbinLookAndFeel`** — `juce::LookAndFeel_V4` subclass. Overrides rotary/linear slider
  drawing, button/toggle background drawing, and combo box rendering using the `gm::ui::synthwave`
  renderers (glowing knobs, CRT-style scope backgrounds, glass panels). Hover feedback
  (`drawButtonBackground` / `drawToggleButton`) is implemented locally via the JUCE `isHighlighted`
  flag — an accent edge plus faint accent wash — without touching the shared `gm::ui` renderers,
  so that knobs (which need a separate repaint hook) can be added later without architectural
  changes.
- **`C64Keyboard`** — `juce::MidiKeyboardComponent` subclass declared in `PluginEditor.h`,
  implemented in `PluginEditor.cpp`. Overrides `drawWhiteNote` and `drawBlackNote` to replace
  the stock JUCE keyboard with C64-styled keys: dark vertical-gradient caps on white keys,
  near-black sharps with a cyan neon top edge, cyan glow on press and a faint cyan wash on hover
  (using `gm::ui::theme::cyan`), and Press-Start pixel-font octave labels at each C note (C2–C6).
  Key width is set by `resizedContent()` to auto-fit the available window width.
- **Popup panels** (declared in `PluginEditor.h`, shown via `addAndMakeVisible`/`setVisible`):
  Modulation, Wavetable, Chord, SID Player, Digi. Each is styled with the Phase-C glass chrome:
  generated neon-grid backdrop, translucent glass, accent glow border, Press-Start glow title bar.
- **Filter value readouts** — `juce::Label` instances stacked under the Cutoff and Resonance
  labels on each SID tower. Updated from base APVTS parameter values in `timerCallback`;
  `Label::setText` no-ops when unchanged, so no added per-frame repaint cost.

### Shared Library (`ghostmoon-oss`, external)

Consumed via `GHOSTMOON_OSS_DIR`. Three CMake targets under the `gm::` namespace:

| Target | Include root | License | Used for |
|---|---|---|---|
| `ghostmoon_oss::dsp` | `<ghostmoon/ReverbSC.h>` | LGPL-2.1-or-later | Algorithmic reverb in the FX chain |
| `ghostmoon_oss::core` | `<ghostmoon/ui/ScaledEditor.h>` etc. | MIT | `ScaledEditor` base class, geometry helpers |
| `ghostmoon_oss::ui_synthwave` | `<ghostmoon/ui/synthwave/…>` | MIT | Glowing knobs, CRT scopes, glass panels, synthwave theme tokens |

The `gm::ui::theme::cyan` (and other accent) tokens used throughout `BreadbinLookAndFeel` and
`C64Keyboard` come from `ghostmoon_oss::ui_synthwave`.

### Third-Party Dependencies

| Library | Version | Fetch method | License | Role |
|---|---|---|---|---|
| JUCE | 8.0.4 | CPM | GPL v3 / commercial | Plugin framework, audio I/O, UI primitives |
| reSIDfp / libsidplayfp | 2.16.0 | CPM (tarball) | GPL v2+ | Cycle-accurate SID emulation + .SID file playback |
| melatonin_blur | v1.4 | FetchContent | MIT | Frosted-glass blur for popup panels; fetched into `${CMAKE_BINARY_DIR}/melatonin_blur` so `juce_add_module` finds `melatonin_blur.h` at the expected path (CPM's `-src` suffix breaks the module discovery) |

## Data Flow

```
MIDI input
    |
    v
BreadbinProcessor::handleMidiEvent
    |-- voice allocation (Mono/Para/Poly/Poly+Para)
    |-- arpeggiator / chord memory
    |
    v
processBlock (per audio buffer)
    |-- LFO + Filter Envelope + Mod Matrix tick
    |-- PWM sweep
    |-- SIDEngine::process (x2, left + right)
    |-- DigiSampler mix (additive)
    |-- SidFilePlayer ring-buffer mix (additive, background thread produces)
    |-- FX chain: Chorus -> Stereo Delay -> ReverbSC
    |-- Safety chain: DC blocker -> ultrasonic LP -> limiter -> noise gate
    |
    v
output buffer -> DAW / ASIO

UI thread (30 Hz timer)
    |-- reads APVTS parameter values for live readouts (Cutoff, Res labels)
    |-- reads SID register snapshot for register overlay during SID file playback
    |-- conditional repaint (only on metered-value change)
```

## Parameter Bus

All real-time controllable parameters are APVTS `AudioParameter` objects. Non-APVTS state
(MIDI CC mappings, SID player file path/position, UI scale choice) is serialized as XML blobs
inside `getStateInformation` / `setStateInformation`. The two paths never mix: UI controls are
attached to APVTS via `SliderAttachment` / `ComboBoxAttachment` etc.; `processBlock` reads
APVTS atomics directly without locking.

## Known Structural Notes

- **Incremental build corruption (MSB8028)**: Shared intermediate directories between JUCE module
  targets and the plugin target can cause MSVC incremental linking to produce a stale binary.
  Use `--clean-first` for reliable Release builds:
  `cmake --build build --config Release --target Breadbin_All --clean-first`
- **melatonin_blur fetch path**: Must use `FetchContent` with an explicit `SOURCE_DIR` naming the
  folder `melatonin_blur` (not the CPM default which appends `-src`), because `juce_add_module`
  looks for `melatonin_blur/melatonin_blur.h` by exact path.
