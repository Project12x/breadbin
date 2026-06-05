# Breadbin — C64 Reskin Phase B (OptionD Re-layout + Per-Section Colors)

**Date**: 2026-06-05
**Status**: Approved design
**Branch**: `polish/ui-2026-06-05`
**Rollback point**: tag `checkpoint/phase-a-foundation`
**Builds on**: Phase A foundation (`gm::ui` synthwave LookAndFeel, glass panels, CRT scopes, the `accentOf` hook) — see `2026-06-05-c64-theme-reskin-design.md`.
**Design source**: `C:\tmp\breadbin-design\breadbin1\project\optionD.jsx` (exact main-panel layout, 1000×748) + `parts.jsx` (component sizes). Read these for precise positions/sizes.

## Goal
Re-organize Breadbin's editor main panel to match OptionD's structure and assign the per-section accent colors. Breadbin's current layout is *already* roughly OptionD-shaped (OptionD was a re-org of this editor), so Phase B is a **refinement re-layout + colors** — reposition controls into OptionD's cleaner regions and color them. **Positioning + colors only — no controls added/removed, no behavior/DSP/parameter changes.**

## Fidelity decision
**Faithful structure, adapt for extras.** Match OptionD's regions, arrangement, sizes, and look as closely as practical. Where Breadbin carries controls OptionD's mockup doesn't depict (8 chip variants via the combo, per-SID detune, extra voice-editor controls), fit them into the nearest region (stack/resize). Hide nothing; move nothing to popups.

## Layout — OptionD regions mapped onto Breadbin
Top-to-bottom in the ~1000×800 `gm::ui::ScaledEditor` base (OptionD's artboard is 1000×748; keep Breadbin's height as needed to fit everything). Each region is a glass panel (Phase A's glass, aligned to these region bounds). See `optionD.jsx` for exact px.

**1 — Top bar** (glass, cyan). OptionD: `logo (~92×44) · │ · Engine seg [Split/Unison] · Voicing seg [Mono/Para/Poly/Poly+Para] · spacer · Master slider + value · │ · preset stepper (‹ LCD name ›) + Save · CPU %`.
Delta: consolidate today's scattered top row (Mode · Para · voice-count · SPREAD · Retrig · Patch nav · Master · CPU) into this bar. Add the logo (`BinaryData::logo_png`). Engine = dual-mode; Voicing = the 4-mode voice selector. Keep voice-count / spread / retrig accessible (fold into the bar near the voicing seg).

**2 — SID towers** (two glass panels: left **cyan**, right **orange**). OptionD per tower: `header (SID I/II + chip Combo) · 3 voice chips (LED + V#) · FilterGraph (~448×74 CRT) · Cutoff row · Reso row · bottom (filter-mode seg [LP/BP/HP] + Flt toggle + spacer + Pan)`.
Delta: **move each `FilterDisplay` INTO its tower** (above the cutoff row). Keep per-SID detune (bottom row, near Pan). Voice chips = the 3 voice enables per SID.

**3 — Voice editor** (magenta glass). OptionD: `header (Voice Editor + voice Combo + Ring/Sync/Flt) · body: [Pulse W, Glide, Mod sliders] │ [Amp ADSR] │ [Filter Env ADSR + Amount knob]`.
Delta: **relocate the Filter Envelope (ADSR + amount knob) up from the bottom into here**, grouped beside the Amp ADSR. Keep the wave selector, voice preset save/load, mod-offset.

**4 — FX chain** (cyan glass, magenta header). `"FX Chain" header · Chorus row (toggle + Rate/Depth/Mix) · Delay row (toggle + Time L/R + FB + Mix) · Reverb row (toggle + Decay/Damp/Mix)`. Breadbin already has these — arrange as clean labeled rows.

**5 — Dock** (row). OptionD: `Arp toggle + pattern Combo + Rate + octave Combo · spacer · [Modulation][Wavetable][Chord][SID][Digi] · │ · Clock Combo`.
Delta: include the Digi / WT / LFO1 / LFO2 enable toggles near the dock.

**6 — Keyboard** — unchanged at the bottom (~50px), standalone virtual keyboard.

## Per-section accent colors (via the `accentOf` `"accent"` property)
- **cyan** — SID I (left tower) controls
- **orange** — SID II (right tower) controls
- **magenta** — voice editor + mod indicators + FX-chain header
- **greenyellow** (`grn`) — aux toggles (Ring/Sync/Arp/Loop/Digi/WT), CPU readout, ADSR sliders

Set the property where each control is created (`setupControls` / the per-SID panel setup / the voice-editor setup). Anything unassigned stays cyan (the Phase-A default).

## Approach & sub-phasing
- **B1 — Re-layout**: rewrite `resizedContent()` + the `layout*` helpers (`layoutTopRow`, `layoutSidPanels`, `layoutVoiceEditor`, `layoutBottomControls`) to position controls into the 6 regions; align the glass panels (`BreadbinEditor::paint`) to the new region bounds; relocate the two `FilterDisplay`s into the towers. Bounds-only.
- **B2 — Colors**: set per-section accent properties on the controls.

Each sub-phase ends with a Release build + a **manual visual checkpoint** (user confirms) before it's marked done.

## Testing & verification
- `cmake --build build --config Release --target Breadbin_All` green.
- Suites unaffected (layout + colors only): **LFO 484 / Integration 409 / Mutation 17-of-18**.
- **Manual visual verification** (user): each region lands in the OptionD arrangement; per-section colors correct; nothing overlapping/clipped; the scale selector (75–150%) still rescales correctly.
- Rollback: tag `checkpoint/phase-a-foundation`.

## Risk
- The re-layout touches the most position-dense code (`resizedContent` + helpers, ~400 lines). Mitigate: it's mechanical (reposition `setBounds`, no new controls), sub-phased, with visual checkpoints + a rollback tag.
- OptionD's 748 height vs Breadbin's fuller control set: if everything doesn't fit at 800, grow region heights — do not shrink controls below usability.

## References
- `C:\tmp\breadbin-design\breadbin1\project\optionD.jsx` (exact positions/sizes), `parts.jsx` (component dims).
- Phase A spec: `docs/superpowers/specs/2026-06-05-c64-theme-reskin-design.md`.
- Current editor: `src/PluginEditor.cpp` (`resizedContent`, `layout*` helpers, `BreadbinEditor::paint`, `setupControls`), `src/PluginEditor.h`.
