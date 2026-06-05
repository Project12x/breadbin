# C64 Reskin — Phase B (OptionD Re-layout + Per-Section Colors) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Re-arrange Breadbin's editor main panel into OptionD's structure (consolidated top bar, two SID towers with in-tower CRT filter graphs, voice editor with the filter envelope, FX-as-rows, dock) and apply the per-section accent colors.

**Architecture:** Bounds + colors only — no controls added/removed, no behavior/DSP/parameter changes. Rewrite `resizedContent()` + the `layout*` helpers to position existing controls into 6 regions; align the Phase-A glass panels to those regions; relocate the two `FilterDisplay`s into the towers; then set per-control `"accent"` properties (the Phase-A `accentOf` hook reads them). Verified by **build-green + manual visual checkpoint** (a re-layout can't be unit-tested; the user's UI-verification rule governs).

**Tech Stack:** C++20, JUCE 8 (`Component::resized`, `Rectangle::removeFromTop/removeFromLeft`, `setBounds`), the Phase-A `gm::ui` LookAndFeel + `accentOf`.

**Exact layout values:** `C:\tmp\breadbin-design\breadbin1\project\optionD.jsx` (region positions/sizes) + `parts.jsx` (component dims). Read them per region; this plan gives the structure + the control→region mapping, the implementer writes the `setBounds` against those values.

**Rollback:** tag `checkpoint/phase-a-foundation`. Branch `polish/ui-2026-06-05`, repo `D:\Code\breadbin`.

---

### Task B1: Re-layout the main panel to OptionD's 6 regions

**Files:**
- Modify: `src/PluginEditor.cpp` — `BreadbinEditor::resizedContent()` and the `layout*` helpers (`layoutTopRow`, `layoutSidPanels`, `layoutVoiceEditor`, `layoutBottomControls`); `BreadbinEditor::paint` (glass-panel region bounds); the `FilterDisplay` placement.
- Modify: `src/PluginEditor.h` — if region-bound members or helper signatures need adjusting.

- [ ] **Step 1: Read the current layout + the target.** Read `resizedContent()` + every `layout*` helper in `PluginEditor.cpp` to inventory which control each currently positions. Read `C:\tmp\breadbin-design\breadbin1\project\optionD.jsx` for the 6-region structure and exact px. Confirm the current control set (the screenshot/inventory): top row (Mode/dualMode, Para/voiceMode, voice-count, spread, retrig, preset prev/next + selector + save/load, master, CPU, scaleSelector); per-SID panel (chip combo, 3 voice buttons+enables, cutoff+meter, res+meter, LP/BP/HP, Flt, pan, detune, `FilterDisplay`); voice editor (voice selector, wave, PW, A/D/S/R, Ring/Sync/Flt, mod-offset, glide, voice preset save/load); bottom (Digi/WT/LFO1/LFO2 toggles, 5 popup buttons, Filter-Env toggle+ADSR+Amt knob, Chorus/Delay/Reverb rows, Arp toggle+pattern+rate+octave, Clock).

- [ ] **Step 2: Region 1 — Top bar.** Using a top `removeFromTop(<bar height from optionD>)`, lay out left→right: **logo** (draw `BinaryData::logo_png` in `paint`, reserve ~92×44), divider, Engine segmented (the dual-mode `[Split/Unison]`), Voicing segmented (the 4-mode voice selector), spacer, Master slider + value, divider, preset stepper (`‹` + selector LCD + `›` + Save), CPU. Fold voice-count/spread/retrig in near the Voicing seg. Keep `scaleSelector` in the bar (right side).

- [ ] **Step 3: Region 2 — SID towers.** `removeFromTop(<tower height>)`, split into two equal columns (left/right) with a gap. In each column lay out top→bottom: header (SID label + chip combo), a row of 3 voice chips (enable toggle + V# label), the **`FilterDisplay` (move it here, ~full-width × ~74)**, Cutoff row (label + slider + value meter), Reso row, bottom row (LP/BP/HP seg + Flt toggle + spacer + Pan + Detune). Left = SID I, right = SID II.

- [ ] **Step 4: Region 3 — Voice editor.** `removeFromTop(<voice-editor height>)`. Header (Voice Editor label + voice selector combo + Ring/Sync/Flt). Body columns: [Pulse W, Glide, Mod-offset sliders] │ [Amp ADSR — the A/D/S/R sliders] │ **[Filter Env — move its toggle + ADSR + Amount knob up from the bottom into here]**. Keep wave selector + voice preset save/load in the header area.

- [ ] **Step 5: Region 4 — FX chain.** `removeFromTop(<fx height>)`. "FX Chain" header, then three labeled rows: Chorus (toggle + Rate/Depth/Mix), Delay (toggle + Time L/R + FB + Mix), Reverb (toggle + Decay/Damp/Mix) — reuse the existing FX controls.

- [ ] **Step 6: Region 5 — Dock.** `removeFromTop(<dock height>)`, a single row: Arp toggle + pattern combo + rate + octave combo, spacer, the 5 popup buttons (Modulation/Wavetable/Chord/SID/Digi) + the Digi/WT/LFO1/LFO2 enable toggles, divider, Clock combo.

- [ ] **Step 7: Region 6 — Keyboard.** The remaining bottom strip (~50px) → the virtual keyboard (unchanged).

- [ ] **Step 8: Align the glass panels.** In `BreadbinEditor::paint`, set the four (now six) glass-panel rectangles to the region bounds computed in `resizedContent` (store them as members if needed, as Phase A did). Each region = a glass panel; ensure the towers are two side-by-side panels.

- [ ] **Step 9: Build.**
```
cmake --build build --config Release --target Breadbin_All -- /m /verbosity:minimal
```
Expected: clean compile + link (it's repositioning existing controls — no new symbols). If a control reference broke during the move, fix it.

- [ ] **Step 10: Manual visual checkpoint.** Launch `build/Breadbin_artefacts/Release/Standalone/Breadbin.exe`. **Ask the user to confirm**: the 6 regions match OptionD's arrangement (consolidated top bar with logo, two SID towers with the filter graphs inside, voice editor with the filter envelope, FX rows, dock, keyboard); nothing overlaps/clips; the 75–150% scale selector still rescales correctly. Do NOT check this box without the user's confirmation.

- [ ] **Step 11: Commit.**
```bash
git add src/PluginEditor.cpp src/PluginEditor.h
git commit -m "feat(ui): re-layout main panel to OptionD structure (Phase B1)"
```
End with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

### Task B2: Per-section accent colors

**Files:**
- Modify: `src/PluginEditor.cpp` — where controls are created (`setupControls`, the per-SID panel setup, the voice-editor setup, the FX/dock setup).

- [ ] **Step 1: Define the accent map.** Per the spec: SID I (left tower) controls → `gm::ui::theme::cyan`; SID II (right tower) → `gm::ui::theme::orange`; voice editor + mod indicators + FX-chain header → `gm::ui::theme::mag`; aux toggles (Ring/Sync/Arp/Loop/Digi/WT) + CPU readout + ADSR sliders → `gm::ui::theme::grn`. Anything unassigned stays cyan (Phase-A default).

- [ ] **Step 2: Set the `"accent"` property.** For each control, where it's created/configured, call `comp.getProperties().set("accent", (int)gm::ui::theme::<colour>.getARGB());` per the map above. (The Phase-A `BreadbinLookAndFeel::accentOf` already reads this property; the renderers already honor the returned colour.) Apply to knobs, sliders, toggles, combos, and the section headers (`drawGlowText` accent) per region.

- [ ] **Step 3: Build.**
```
cmake --build build --config Release --target Breadbin_All -- /m /verbosity:minimal
```
Expected: clean compile + link.

- [ ] **Step 4: Manual visual checkpoint.** Launch the Standalone. **Ask the user to confirm**: left tower cyan, right tower orange, voice editor / FX header magenta, aux toggles + CPU + ADSR greenyellow; the colors read clearly against the backdrop. Do NOT check without the user's confirmation.

- [ ] **Step 5: Commit.**
```bash
git add src/PluginEditor.cpp
git commit -m "feat(ui): per-section accent colors (Phase B2)"
```
End with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

### Task B3: Phase B verification + milestone

- [ ] **Step 1: Full suite.**
```
ctest --test-dir build -C Release
```
Expected: **LFO 484 / Integration 409 / Mutation 17-of-18** unchanged (layout + colors don't touch tested behavior).

- [ ] **Step 2: Final visual sign-off + docs.** With the user's confirmation that the main panel now matches OptionD, update `STATE.md` (Phase B done; C/D pending) and `CHANGELOG.md` (`[Unreleased]`: the OptionD re-layout + per-section colors). Commit.

- [ ] **Step 3: Archive + tag.** Preserve the Release Standalone+VST3 to `releases/<ts>_phase-b/`, then:
```bash
git tag -a checkpoint/phase-b-layout -m "Reskin Phase B: OptionD re-layout + per-section colors"
```

---

## Notes for the executor
- **UI verification rule:** B1 Step 10, B2 Step 4, and B3 Step 2 require explicit user visual confirmation — do NOT mark them done on a green build alone. The build only proves it compiles, not that the layout/colors are right.
- **Exact px/positions:** read `optionD.jsx` per region rather than guessing; this plan gives structure + the control→region mapping, not transcribed `setBounds`.
- **No behavior changes:** if you find yourself changing a control's parameter, range, or callback, stop — Phase B is positioning + colors only.
- **Rollback:** `checkpoint/phase-a-foundation` predates all of this.
