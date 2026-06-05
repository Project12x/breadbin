# Breadbin — C64 "Neon Synthwave" Reskin (Option D)

**Date**: 2026-06-05
**Status**: Approved design — Phase A (foundation-first, phased)
**Branch**: `polish/ui-2026-06-05`
**Rollback point**: tag `checkpoint/pre-c64-redesign` + archive `releases/2026-06-05_0146_pre-c64-redesign/`
**Design source**: Claude Design handoff bundle `breadbin1` (extracted to `C:\tmp\breadbin-design\breadbin1\`). The file the user had open, `Breadbin C64 Theme.html`, renders **Option D** — described in the chat as *"neon synthwave, the ideal."*

## Goal

Reskin the Breadbin JUCE editor to match the approved **Option D — "Neon Synthwave"** design: the glass / neon / glow aesthetic of Option C reorganized with Option A's cleaner structure. **Reskin only — no behavior, DSP, or parameter changes.**

## Constraints & non-goals (from the design chat)

Firm rules:
- Keep the existing **background, logo, and retro fonts** (stated top priority).
- **CRT / scanlines only on scope displays** (filter graphs, LFO scopes) — never the whole window ("a serious tool, not a toy").
- **PETSCII font is an opt-in toggle, OFF by default** (even though the saved mockup had it on).
- **C64 VIC-II palette is a popup-only NEON⟷C64 switch** — the main panel stays NEON.

Explicit non-goals (do **not** build):
- No 6581/8580 character toggle ("too rare a use case").
- No boot splash.
- No hero oscilloscope baked into the main panel (possible future separate popup only).
- No drag-to-assign patch-cable mod matrix.
- No behavior / DSP / parameter changes — appearance and layout only.

## Approach: foundation-first, phased

| Phase | Scope |
|-------|-------|
| **A — Foundation** *(this spec)* | Theme tokens + new LookAndFeel + assets, applied to the **current layout**. No controls move. |
| **B — Main panel** | Re-lay-out to OptionD: consolidated top bar (logo · engine/voicing · master · preset · CPU), two SID "towers" with CRT filter graphs, voice editor, FX as Chorus/Delay/Reverb rows, dock row, keyboard. |
| **C — Popups** | The 5 popups (Modulation, Wavetable, Chord, SID Player, Digi) + NEON⟷C64 scheme switch + C64 nostalgia layer (blinking cursor, `LOAD"…",8,1`, REG⟷BASIC dump, floppy/tape icons). |
| **D — Motion / PETSCII** | Animated mod rings (default off), PETSCII label toggle, scope scanline tuning, blinking cursors. |

Each phase is its own review + commit. Phases B–D get their own brainstorm/plan when reached. **This spec details Phase A.**

## Phase A design

### A0. Component home — GhostmoonGPL (shared LGPL UI library)
Reusable synthwave primitives live in the user's **GhostmoonGPL** library
(LGPL-2.1-or-later; `…/Antigravity/ghostmoongpl`), not in Breadbin's `src/`. This
**supersedes the `src/` locations noted below** for the *reusable* pieces:
- **→ GhostmoonGPL** (`ui/include/ghostmoon/ui/`, namespace `gm::ui::`, header-only;
  new `ghostmoongpl_ui` INTERFACE target that assumes the consumer provides JUCE):
  theme token constants (`gm::ui::theme`), the 270° knob / linear-slider / CRT-scope /
  glass-panel renderers, and glow helpers.
- **→ Breadbin `src/`**: `BreadbinLookAndFeel` (wires the `gm::ui::` primitives to
  Breadbin's controls, accent assignments, and layout) and the project `DESIGN.md`.
- **Consumption**: default `add_subdirectory` via a `GHOSTMOONGPL_DIR` cache var
  (switchable to CPM/FetchContent if the repo gains a remote); finalized when Phase A
  wires it up.
- **Related cleanup (bonus)**: GhostmoonGPL's README documents that `ReverbSC` is
  **LGPL, not MIT** — so Breadbin's `src/dsp/ReverbSC.h` ("MIT copy") is mislabeled.
  Once consumption is wired, switch the reverb to `gm::ReverbSC` from GhostmoonGPL and
  delete the local copy (GPLv3 + LGPL = clean).

### A1. Theme tokens — single source of truth
New `src/BreadbinTheme.h` (`bb::theme` namespace):
- `juce::Colour` constants for every `synthwave.css` token:
  - Structure: `bg0 #0a0a0e`, `bg1 #13131a`, `panel #16161d`, `panel2 #1c1c25`, `panel3 #22222d`, `line #2c2c39`, `line2 #3a3a4a`, `inset #0e0e13`.
  - Text: `txt #e7e7f0`, `txt2 #a6a6b8`, `txt3 #6f6f82`.
  - Neon accents: `cyan #33eded` (+`cyan-d #1aa6a6`), `orange #ffae3b` (+`#c97f1e`), `grn #b6f23c` (+`#7fae23`), `mag #ff3df0`, `purple #9a6bff`, `gold #ffcb45`, `red #ff5468`, `lime #5dff7a`, `yellow #ffe14d`.
  - C64 VIC-II (popup scheme, Phase C): `cblue #8b80e8`, `cgrn #9ad284`, `cyel #d6dd7e`, `cred #d08a72`, `cpur #b98ae0`, `beige #d8c79f`.
- Font accessors for the four roles: **Press Start 2P** (headers), **JetBrains Mono** (numeric readouts), **Lato 400/700** (labels/body). (Roboto-Bold is shipped but unused — skip.)
- Helpers: glow color/alpha convention, panel-gradient endpoints.

Also a project-root **`DESIGN.md`** documenting palette, typography, and component specs (authored via the `design-md` skill). Durable reference for all UI work. **Rule: no ad-hoc hex anywhere after this — UI reads from `bb::theme` / DESIGN.md.**

### A2. Fonts & assets
- Fonts already embedded in `BreadbinAssets` (PressStart2P, JetBrainsMono, Lato R/B) — wire theme accessors to `BinaryData`.
- Add to `assets/` + CMake `BreadbinAssets`: `background_clean.png` (new main backdrop, logo removed), `logo.png` (transparent emblem). Popup grid textures (`refs/neon-bg.png`, `refs/c64-bg.png`) deferred to Phase C.

### A3. LookAndFeel
Rebuild `BreadbinLookAndFeel` (extends `LookAndFeel_V4`) to render the synthwave look:
- **Rotary knob** (`drawRotarySlider`): 270° arc — track arc `#26262f` w3; accent value arc w3 with glow; metallic cap (`radial-gradient #3a3a46→#15151c`, `#000` border, inner highlight); pointer bar in accent. Bipolar fills from center.
- **Linear sliders** h & v (`drawLinearSlider`): inset track `#0e0e13` (inner shadow, `#000` border); accent fill w6 + glow; metallic thumb (`gradient #d2d2dc→#5a5a66`, `#000` border, drop shadow). Bipolar fills from center with a center tick.
- **Combo** (`drawComboBox`): `gradient #202028→#17171e`, `line2` border, accent value text, `txt3` chevron.
- **Toggle / button**: `bb-tgl` (colored dot + glow when on) / `bb-btn` (gradient, `line2` border, inset + drop shadow).
- **Labels/fonts**: route to correct role — `.lbl` Lato 700 10px .14em uppercase `txt3`; `.val` JBMono 11px accent.
- **Per-control accent**: cyan = SID I, orange = SID II, magenta = voice/mod/FX, greenyellow = aux toggles — via a Colour-ID / component-property convention.
- **Panel chrome + glow-header** helper (replaces the current `drawHeaderGlow`): `NeonHD` = PressStart accent text with layered glow.

### A4. CRT scope displays
Restyle existing `FilterDisplay` + `LFODisplay`: `bb-scope` container (radial dark gradient `#0a1417→#05080a`, inset shadow, `#000` border); curve drawn **twice** (thick low-alpha bloom under a sharp stroke); **cached scanline overlay** image (repeating 0° lines, ~34% alpha). Sanctioned CRT use only.

### A5. Background & glass
- Draw `background_clean.png` cover + lightened vignette (radial, ~.14→.52 alpha).
- Panels = semi-transparent dark gradient `rgba(20,22,34,.44)→rgba(7,8,14,.60)` + 1px accent gradient border + glow-border. **No live blur** — glass is faked with translucency (still gives the "grid shows through" effect, zero perf cost).

### A6. Performance
- Cache background, vignette, scanline overlays (and any glass composite) as images; rebuild only on resize.
- `paint()` free of per-frame gradient/shadow churn; run the `juce-perf-troubleshooting` playbook.
- No live animation in Phase A (deferred to D).

### Scope boundary
Phase A restyles the **existing layout in place — no controls move.** New OptionD-only elements (e.g., in-tower CRT filter graphs) arrive in Phase B.

## Testing & verification
- `cmake --build build --config Release --target Breadbin_All` green.
- Existing suites unaffected (UI-only change): expect **LFO 484 / integration 405 / mutation 17-of-18**.
- **Manual visual verification** (UI bugs cannot be auto-verified): launch standalone; confirm rendering, colors, fonts, and glows match tokens, and paint stays smooth. **User confirms before Phase A is marked done.**
- Rollback: tag `checkpoint/pre-c64-redesign` + archive `releases/2026-06-05_0146_pre-c64-redesign/`.

## Risks
- **Glass is approximated** (no native JUCE backdrop blur): translucency + accent border + glow.
- **Some OptionD elements don't exist in the current layout yet** — they arrive with the Phase B re-layout; Phase A only restyles what's already there.
- The LookAndFeel rewrite touches many draw paths; mitigated by phasing, manual visual review, and the existing rollback point.

## Key references
- Design bundle `C:\tmp\breadbin-design\breadbin1\`: `synthwave.css` (tokens), `parts.jsx` (widget construction), `optionD.jsx` (main panel), `popups.jsx` (popups), `chats/chat1.md` (rationale).
- Existing editor: `src/PluginEditor.{h,cpp}` (`BreadbinLookAndFeel`, custom components), `src/ScaledEditor.h`.
