# Breadbin — C64 Reskin Phase D (Motion)

**Date**: 2026-06-07
**Status**: Approved design
**Branch**: `polish/ui-2026-06-05`
**Rollback point**: tag `checkpoint/phase-c-popups`
**Builds on**: Phase A foundation (`gm::ui` synthwave LookAndFeel, glass + scanline primitives,
`accentOf`), Phase B (OptionD re-layout + per-section colors), Phase C (popup glass chrome + C64
nostalgia layer). See the Phase A/B/C specs.

## Goal
Add the final **motion** layer to the reskin — the "finishing sparkle." Three tasteful, perf-bounded
animations: a blinking C64 cursor, opt-in animated modulation rings around the knobs, and a subtle
rolling-CRT scanline drift on the scopes. **Motion only — no DSP, parameter, or layout changes.** The
mod rings re-use modulation data the processor already exposes; the rest is pure rendering.

## Decisions (brainstormed)
1. **Scope = "Curated set."** Build exactly three motion elements: blinking cursor, animated mod rings
   (opt-in), scope scanline drift. **PETSCII label toggle is NOT in this set** — it remains a possible
   future "full nostalgia" item, explicitly out of scope here.
2. **Mod rings = swap, not additive.** Default OFF keeps today's rectangular bar-meters (no regression).
   Toggling ON **hides the bars** and draws an animated arc around each modulated knob. One clean mode
   swap — never both at once. Honors the Phase-A firm rule "animated mod rings default OFF."
3. **Cursor blink and scope drift are always on.** Both are inherently calm/tasteful and carry no
   meaningful perf cost, so neither gets a toggle. Only the rings (constant motion around every knob)
   are gated behind an opt-in.
4. **Rings toggle persists in APVTS state.** A UI-only boolean stored in the APVTS state `ValueTree`
   (a property on the state tree, **not** a host-automatable parameter), so it survives save/reload with
   the plugin instance without polluting automation lanes.

## Architecture
All motion hooks into the **30 Hz `juce::Timer`s that already exist** — `BreadbinEditor` (`startTimerHz(30)`,
`timerCallback` at PluginEditor.cpp ~L1237) and each of the 5 popup panels. **No new timers, nothing on
the audio thread.**

- **Shared phase counter.** Each driving timer increments a monotonic `int animFrame` per tick.
  Blink/drift phases derive from it deterministically (e.g. `blinkOn = (animFrame / 16) & 1`). No
  wall-clock / `juce::Time` needed — keeps behavior reproducible and frame-rate-defined.
- **Localized repaints only — the cardinal perf rule.** Each animated element repaints **only its own
  small sub-rectangle**, never the parent panel. A full-panel `repaint()` would re-blit the cached
  frosted-glass background (the melatonin-blurred image), which is the one expensive path we must avoid.
  This matches the existing pattern where `cutoffMeterL.repaint()` etc. repaint individual meters.
- **Cached images stay cached.** The scanline drift re-blits the *already-cached* `makeScanlineOverlay`
  image at a changing offset — no per-frame image regeneration.
- **Zero cost when off/idle.** Rings OFF = today's code path exactly. Cursor only repaints on its
  twice-per-second toggle. Drift is one extra offset on a blit that already happens.

## Design

### D1 — Blinking C64 cursor (always on)
- **Where:** `SidPlayerPanel` — trailing the `LOAD"<name>",8,1` line and the REG dump (the Phase-C
  `READY.` nostalgia block).
- **Behavior:** a solid block character that toggles visible/hidden ~every 0.5 s (C64-authentic; ≈16
  frames at 30 Hz via the shared `animFrame`). Lime-tinted to match the SID-player transport glyphs.
- **Repaint:** only the cursor's small rect on each toggle (2×/sec), not the panel. The Phase-C static
  cursor block becomes this blinking one.

### D2 — Animated mod rings (opt-in, default OFF)
- **Toggle control:** a small `MOD ◉` button in the bottom dock near the CPU readout (greenyellow aux
  accent, consistent with the Phase-B aux section). Click toggles rings on/off.
- **Persistence:** the on/off state is stored as a property on the APVTS state `ValueTree` (UI-only,
  non-automatable), read on editor construction and written on toggle.
- **OFF (default):** identical to today — the rectangular `ModulationMeter` bars on Cutoff L/R, PW,
  Pitch, Res L/R (cyan = upward mod, orange = downward).
- **ON:** the bars are hidden; an animated arc wraps each modulated knob, fed by the live atomic values
  the processor already exposes (`getModTotalFilterCutoff/PulseWidth/Pitch/Resonance()`, and the
  per-slot `getModSlotSourceValue/Contribution()` where finer detail helps). The arc sweeps from the
  knob's base value to its modulated value, **cyan = upward / orange = downward** — the same color
  language as the bars, wrapped around the knob (Vital/Serum-style).
- **Implementation shape:** rings are drawn by a lightweight per-knob overlay (consistent with the
  existing per-knob `ModulationMeter` components), so each repaints only its own rect at 30 Hz when ON
  and never triggers a background re-blit. The bar-meters and the ring overlays are mutually exclusive:
  the toggle flips visibility of one set vs. the other.

### D3 — Subtle scope scanline drift (always on)
- **Where:** the CRT scopes **only** — filter graphs L/R and the LFO display(s). Honors the Phase-A
  firm rule "scanlines only on scopes." The main panel and popups do **not** get drift.
- **Behavior:** the existing cached scanline overlay (`gm::ui::makeScanlineOverlay`) is blitted at a
  slowly drifting vertical offset that wraps around, giving a faint rolling-CRT shimmer. Speed ~1px
  every few frames (derived from `animFrame`) — perceptible as life, not motion sickness.
- **Cost:** one offset on a blit that already happens each scope repaint — negligible.

## Defaults summary
| Element | Default | Toggle? |
|---|---|---|
| Blinking cursor | ON | no |
| Scope scanline drift | ON | no |
| Animated mod rings | OFF | yes (`MOD ◉` dock button, persisted in APVTS state) |
| PETSCII labels | — | out of scope (future "full nostalgia") |

## Testing & verification
- `cmake --build build --config Release --target Breadbin_All` green (**clean rebuild, `--clean-first`**,
  per the incremental-corruption discipline established earlier in the reskin).
- Suites unaffected (UI-only): **LFO 484 / Integration 409 / Mutation 17-of-18** (the 1 surviving
  triangle-boundary mutation is pre-existing).
- **Manual visual verification (user):** (1) the SID-player cursor blinks at a C64-ish rate on the LOAD
  line + REG dump; (2) the `MOD ◉` toggle cleanly swaps bars↔rings, rings track live modulation in the
  right colors, and the state survives close/reopen; (3) the scopes show a faint rolling scanline drift
  with no flicker or perceptible CPU cost. UI motion cannot be auto-verified — user signs off per element.
- Rollback: tag `checkpoint/phase-c-popups`.

## Risk
- **Frosted-glass re-blit on repaint.** The single biggest risk: a stray full-panel `repaint()` would
  re-composite the cached blurred background every frame. Mitigate by repainting only each element's
  sub-rect (overlay components / `repaint(rect)`), never the parent.
- **Ring overlay vs. knob compositing.** Rings must sit visually around the rotary without fighting the
  `gm::ui` knob renderer or capturing mouse events. Mitigate: overlays are non-interactive
  (`setInterceptsMouseClicks(false, false)`) and positioned to frame, not cover, the knob.
- **Drift cadence.** Too fast reads as flicker/seasickness. Keep ~1px / few-frames and tune during
  manual review; it is trivially adjustable (a single divisor on `animFrame`).
- Each element is independently visible — sub-phase by element (D1 → D2 → D3) with the
  `checkpoint/phase-c-popups` rollback if any one reads poorly.

## References
- Live mod data: `src/PluginProcessor.h` — `getModTotalFilterCutoff/PulseWidth/Pitch/Resonance()` (atomic
  `.load()`), `getModSlotSourceValue/Contribution(int)`.
- Existing motion infra: `src/PluginEditor.cpp` — `BreadbinEditor::timerCallback` (~L1237),
  `SidPlayerPanel` timer (~L580/L634); `src/PluginEditor.h` — `ModulationMeter` (~L334), the per-knob
  meter members (`cutoffMeterL/R`, `pwMeter`, `pitchMeter`, `resMeterL/R` ~L1271), `cpuLoadLabel` (~L936).
- Scanline primitive: `gm::ui::makeScanlineOverlay`.
- Phase A spec: `docs/superpowers/specs/2026-06-05-c64-theme-reskin-design.md`;
  Phase B spec: `docs/superpowers/specs/2026-06-05-c64-reskin-phase-b-design.md`;
  Phase C spec: `docs/superpowers/specs/2026-06-05-c64-reskin-phase-c-design.md`.
