# Breadbin — C64 Reskin Phase C (Popup Glass Chrome + C64 Nostalgia Layer)

**Date**: 2026-06-05
**Status**: Approved design
**Branch**: `polish/ui-2026-06-05`
**Rollback point**: tag `checkpoint/phase-b-layout`
**Builds on**: Phase A foundation (`gm::ui` synthwave LookAndFeel, glass + scanline primitives, `accentOf`)
and Phase B (OptionD re-layout + per-section colors). See the Phase A/B specs.
**Design source**: `C:\tmp\breadbin-design\breadbin1\project\popups.jsx` (the OptionD `GlassDialog` popups) —
used **pattern-only** (JSX/React → ported as visual structure, not code).

## Goal
Restyle Breadbin's 5 popups (Modulation, Wavetable, Chord Memory, SID File Player, Digi Sampler) to the
synthwave **glass** look, and add a light **C64 nostalgia layer**. The controls inside the popups already
use the Phase-A `gm::ui` LookAndFeel; what's missing is the **popup chrome** (the windows are plain
`NonModalPopup`s with a solid `Colour(30,30,35)` background). **Chrome + colors + targeted nostalgia
content — no popup behavior/DSP/parameter changes** (except the SID-Player REG⟷BASIC view + `LOAD` line,
which are read-only displays).

## Decisions (brainstormed — these override the Phase-A spec's Phase-C sketch)
1. **Restyle depth = glass chrome + colors.** Wrap the 5 existing panels in the glass frame and assign
   per-role accents; keep each panel's current internal layout. Iterate any panel that reads poorly later
   (as B1 did), rather than re-laying-out all five up front.
2. **NEON⟷C64 scheme switch = DROPPED.** The Phase-A firm rule (a popup-only NEON⟷C64 palette switch) is
   **removed at the user's direction**. Popups are **NEON palette only** — no C64 VIC-II palette swap, no
   `pal()` role resolver, no second backdrop, no `SchemeSwitch` control.
3. **C64 nostalgia layer = useful + charming bits** (all rendered in NEON):
   - SID Player **REG⟷BASIC register dump** (live registers ↔ BASIC `POKE 54272…` listing).
   - SID Player **`LOAD"<name>",8,1`** line (static — no blink; cursor animation stays in Phase D).
   - **Floppy / tape icons** on Load/Save buttons.
4. **Popup backdrop = a generated neon grid image.** The mockup's `neon-bg.png` was not shipped in the
   bundle. Generate a custom C64-appropriate neon perspective-grid backdrop (Pollinations, during
   implementation; user reviews the result), embed it, and draw it inside each popup under the glass overlay.

## Approach
**Restyle `NonModalPopup` + a shared retro header** (chosen over per-panel glass or promoting a `gm::ui`
popup primitive). The glass treatment lives in the popup window itself, so all 5 popups inherit it from one
place; the 5 content panels stay as-is apart from per-role accents. Reuses the existing `gm::ui` glass /
scanline / glow-text primitives. Promotion of a reusable glass-popup frame to `gm::ui` (for Nessy /
Pedalboard3) is **deferred** — not required for Phase C.

## Design

### C1 — Glass popup chrome
Rewrite `NonModalPopup::paint()` (currently a solid semi-transparent fill) to render, in order:
1. the generated grid backdrop (cover-fit, **cached** per size),
2. a translucent glass gradient overlay (`rgba(20,22,34,.44)→rgba(7,8,14,.60)`, matching the main panel's
   `drawGlassPanel`),
3. a 1px accent glow border + soft outer glow,
4. a cached scanline sheen (`gm::ui::makeScanlineOverlay`).

Replace the plain title with a **Press-Start glow header** (`gm::ui::drawGlowText`, title in the popup's
accent) and keep a ✕ close affordance at the right. `NonModalPopup` takes an **accent `juce::Colour`**
(passed by each `show*Popup()`), used for the header text, border, and glow. Factor the main panel's
`drawGlassPanel` glass math into a small shared helper so the window and the main editor stay in sync.

### C2 — Backdrop asset
Generate one neon perspective-grid image (dark, calm enough to sit behind controls; synthwave/C64 horizon).
Add to `assets/` and the `BreadbinAssets` CMake target (alongside `background_clean.png`). Draw cover-fit,
cached as an image rebuilt only on size change.

### C3 — Per-popup accents
Assign via the Phase-B `"accent"` component-property mechanism (`accentOf`):
- **Modulation** — LFO 1 cyan, LFO 2 orange, PWM Sweep green, Mod-Matrix header magenta (slots tinted per source).
- **Wavetable** — cyan primary, green for the PW sliders.
- **Chord Memory** — magenta.
- **SID File Player** — cyan, with an orange "Snapshot to Synth" action.
- **Digi Sampler** — cyan, green for Loop.
The popup window accent (C1) matches each popup's primary accent.

### C4 — C64 nostalgia layer (`SidPlayerPanel` + shared icons)
- **REG⟷BASIC toggle** — a small segmented switch flipping a content area between the **live SID register
  table** (`V1/V2/V3/FLT` rows, read from the SID file player's live register state — the same source the
  existing on-panel register overlay already consumes) and the **same register state rendered as a C64
  BASIC `POKE 54272+n,v` listing**. Read-only view over already-exposed state; no new DSP or threading.
- **`LOAD"<name>",8,1` line** — static text derived from the loaded `.sid` filename: `LOAD"NAME",8,1` +
  `READY.` + a static cursor block + `DEVICE 8 · 1541`. Hidden until a file is loaded.
- **Floppy / tape icons** — small `juce::Path` glyphs (clean-room, drawn from the simple mockup concept) on
  Chord Save/Load, SID Player "Load SID", and Digi "Load WAV".

### Out of scope (→ Phase D)
Blinking-cursor animation, PETSCII label toggle, animated mod rings, and any NEON⟷C64 palette work.

## Testing & verification
- `cmake --build build --config Release --target Breadbin_All` green.
- Suites unaffected (UI-only): **LFO 484 / Integration 409 / Mutation 17-of-18**.
- **Manual visual verification** (user): each popup shows the glass frame + grid backdrop + glow header in
  its accent; controls read clearly; the SID-Player REG⟷BASIC toggle + `LOAD` line + icons render; nothing
  overlaps/clips. UI bugs cannot be auto-verified — user signs off per popup.
- Rollback: tag `checkpoint/phase-b-layout`.

## Risk
- `NonModalPopup` is a `juce::DialogWindow` on the desktop; its paint + non-native title bar must composite
  the backdrop image, glass, and glow without per-frame churn. Mitigate: cache the backdrop + scanline
  images per size (as Phase A did for the main panel), no live animation.
- The REG⟷BASIC dump adds read-only content to `SidPlayerPanel`; keep it a pure view over already-captured
  register state — do not touch the SID file-player engine or threading.
- Five popups × visual review is iterative; sub-phase by popup if needed, with the `checkpoint/phase-b-layout`
  rollback.

## References
- `C:\tmp\breadbin-design\breadbin1\project\popups.jsx` (GlassDialog, LfoBlock, WTStep, RegRows/BasicRows,
  IconFloppy/IconTape, Cursor), `parts.jsx` (component dims).
- Phase A spec: `docs/superpowers/specs/2026-06-05-c64-theme-reskin-design.md`;
  Phase B spec: `docs/superpowers/specs/2026-06-05-c64-reskin-phase-b-design.md`.
- Current popups: `src/PluginEditor.cpp` (`show*Popup`, the `*Panel` classes), `src/PluginEditor.h`
  (`NonModalPopup` at ~L97).
