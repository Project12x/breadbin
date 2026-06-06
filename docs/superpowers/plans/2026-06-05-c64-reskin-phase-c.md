# C64 Reskin — Phase C (Popup Glass Chrome + C64 Nostalgia) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Breadbin's 5 popups (Modulation, Wavetable, Chord, SID Player, Digi) the synthwave glass look — generated neon-grid backdrop + translucent glass + accent glow border + Press-Start glow title — plus per-role accent colors and a light C64 nostalgia layer in the SID Player (REG⟷BASIC register dump, `LOAD"…",8,1` line, floppy/tape icons).

**Architecture:** The popup windows (`NonModalPopup : juce::DialogWindow`) have **no window-level paint**, and each content `*Panel` fills its bounds opaquely with `Colour(30,30,35)` — so that fill occludes any window backdrop. Therefore the glass chrome is drawn **inside each panel's `paint()`** via a shared Breadbin-local helper, and the **shared title bar** (`BreadbinLookAndFeel::drawDocumentWindowTitleBar`) is made accent-aware. No `gm::ui` glass/grid primitive exists (only `drawPanel`); we compose grid-image + a glass-fill helper (extracted from the main panel) + `gm::ui::makeScanlineOverlay` + a manual accent glow ring + `gm::ui::drawGlowText`. NEON palette only — **no NEON⟷C64 scheme switch** (dropped). Bounds/paint/read-only-view changes only — no DSP/parameter/threading changes.

**Tech Stack:** C++20, JUCE 8 (`DialogWindow`, `Component::paint/resized`, `Image`/`ImageCache`, `Path`), `gm::ui` synthwave primitives (`drawGlowText`, `makeScanlineOverlay`, `theme::*`), the `accentOf` `"accent"` component property, Pollinations for the grid image.

**Design source:** `docs/superpowers/specs/2026-06-05-c64-reskin-phase-c-design.md` + `C:\tmp\breadbin-design\breadbin1\project\popups.jsx`.

**Rollback:** tag `checkpoint/phase-b-layout`. Branch `polish/ui-2026-06-05`, repo `D:\Code\breadbin`.

**Key facts (verified):**
- Title bar: `BreadbinLookAndFeel::drawDocumentWindowTitleBar` at `src/PluginEditor.cpp:240-258` (hardcodes `juce::Colours::cyan`, `boldFont 13px`, centred `window.getName()`).
- Panel paints (all in `src/PluginEditor.cpp`, declared in `.h`): ModMatrix `:1592` / Chord `:2009` / Wavetable `:2247` / SidPlayer `:436` / Digi `:629`. Each opens with `g.fillAll(juce::Colour(30,30,35))` (Digi: `fillRoundedRectangle`).
- Panel sizes: ModMatrix 520×410, Chord 520×340, Wavetable 820×380, SidPlayer 520×370, Digi 400×250.
- `background_clean` cache pattern to mirror: build in `resizedContent()` at `src/PluginEditor.cpp:3929-3959`, blit in `paint()` at `:3813-3819`. Glass-fill lambda lives in `BreadbinEditor::paint` (`drawGlassPanel`, ~`:3821`).
- CMake assets: `juce_add_binary_data(BreadbinAssets …)` at `CMakeLists.txt:112-122`.
- SID registers: `processor.getSidFilePlayer().getRegisterSnapshot()` → `struct RegisterSnapshot { uint8_t regs[32]; bool valid; }` (`src/SidFilePlayer.h:71`); `regs[i]` = `$D400+i`; lock-free, polled at 30 Hz. Loaded filename is **not** retained by the engine — capture it in the panel's load lambda (`src/PluginEditor.cpp:284`).
- `gm::ui` (namespace, header-only): `drawGlowText(g, text, font, boundsF, accent, justification)`; `makeScanlineOverlay(w, h, scopeFactor=1.48f) -> juce::Image`; `theme::{cyan,orange,grn,mag,…}`; `BreadbinLookAndFeel::accentOf(juce::Component&)` reads the `"accent"` ARGB-int property (default cyan).

---

### Task C1: Generate + embed the neon-grid popup backdrop

**Files:**
- Create: `assets/popup_grid.png`
- Modify: `CMakeLists.txt:121` (add to `BreadbinAssets` SOURCES)

- [ ] **Step 1: Generate the grid image (Pollinations).** Use the `generative-art` skill / Pollinations `generateImage`. Prompt intent: *dark synthwave/C64 neon perspective grid receding to a low horizon, thin cyan + magenta grid lines on near-black, subtle, low overall brightness, no text, no sun, calm enough to sit behind UI controls.* Size ~1200×900 (covers the largest popup, 820×380, at any aspect via cover-fit). Save to `assets/popup_grid.png`.

- [ ] **Step 2: Add to CMake.** In `CMakeLists.txt`, append to the `juce_add_binary_data(BreadbinAssets SOURCES …)` list (after `assets/logo.png`):
```cmake
        assets/popup_grid.png
```

- [ ] **Step 3: Build the asset target.** Run: `cmake --build build --config Release --target Breadbin_All -- /m /verbosity:minimal`
Expected: clean build; `BinaryData::popup_grid_png` / `BinaryData::popup_grid_pngSize` now exist (a later task references them — this step only confirms embedding compiles).

- [ ] **Step 4: Manual visual checkpoint.** Open `assets/popup_grid.png` and **ask the user to confirm** the grid reads well as a popup backdrop (dark, calm, on-theme). If not, regenerate with an adjusted prompt before continuing. Do NOT check without the user's confirmation.

- [ ] **Step 5: Commit.**
```bash
git add assets/popup_grid.png CMakeLists.txt
git commit -m "feat(ui): embed neon-grid popup backdrop asset (Phase C)"
```
End with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

### Task C2: Shared glass helpers + accent-aware popup title bar

**Files:**
- Modify: `src/PluginEditor.cpp` — add helpers near the top (after includes, in an anonymous namespace); refactor `BreadbinEditor::paint`'s `drawGlassPanel` lambda to use the shared fill; rewrite `drawDocumentWindowTitleBar`.
- Modify: `src/PluginEditor.h` — add a `retroFont` member + setter to `BreadbinLookAndFeel` (the title bar needs Press Start 2P).

- [ ] **Step 1: Add the shared chrome helpers.** In `src/PluginEditor.cpp`, inside an anonymous namespace near the top, add:
```cpp
namespace {
// Translucent dark glass gradient + line border + top sheen (shared with the main panel).
inline void drawGlassFill(juce::Graphics& g, juce::Rectangle<float> fb, float radius) {
  juce::ColourGradient glass = juce::ColourGradient::vertical(
      juce::Colour(0x70141622), juce::Colour(0x9907080E), fb); // rgba(20,22,34,.44)->rgba(7,8,14,.60)
  g.setGradientFill(glass);
  g.fillRoundedRectangle(fb, radius);
  g.setColour(gm::ui::theme::line);
  g.drawRoundedRectangle(fb, radius, 1.0f);
  g.setColour(juce::Colour(0x0AFFFFFF));
  g.drawLine(fb.getX() + radius * 0.5f, fb.getY() + 1.0f,
             fb.getRight() - radius * 0.5f, fb.getY() + 1.0f, 1.0f);
}
// Popup chrome composed inside a panel's paint(): grid backdrop -> glass -> scanline -> accent glow edge.
inline void drawPopupGlass(juce::Graphics& g, juce::Rectangle<float> fb, juce::Colour accent,
                           const juce::Image& gridCache, const juce::Image& scanCache) {
  const float r = 8.0f;
  { juce::Graphics::ScopedSaveState ss(g);
    juce::Path clip; clip.addRoundedRectangle(fb, r); g.reduceClipRegion(clip);
    if (gridCache.isValid()) g.drawImageAt(gridCache, 0, 0);
    else { g.setColour(gm::ui::theme::bg0); g.fillAll(); }
    drawGlassFill(g, fb, r);
    if (scanCache.isValid()) g.drawImageAt(scanCache, 0, 0);
  }
  // Accent glow edge: bright 1px border + a couple of inner low-alpha strokes (tune in the visual pass).
  const float alpha[] = {0.85f, 0.22f, 0.10f};
  const float inset[] = {0.5f, 2.0f, 4.0f};
  for (int i = 0; i < 3; ++i) {
    g.setColour(accent.withAlpha(alpha[i]));
    g.drawRoundedRectangle(fb.reduced(inset[i]), r, i == 0 ? 1.5f : 1.0f);
  }
}
} // namespace
```

- [ ] **Step 2: DRY the main panel.** In `BreadbinEditor::paint` (`src/PluginEditor.cpp` ~`:3821`), replace the body of the `drawGlassPanel` lambda's gradient-fill + border + sheen with a call to `drawGlassFill(g, fb, radius)` (keep the lambda's `bounds.isEmpty()` guard and `fb = bounds.toFloat()`). Net behavior unchanged.

- [ ] **Step 3: Give the LookAndFeel a retro font.** In `src/PluginEditor.h`, add `juce::Font retroFont;` beside `proFont/boldFont/monoFont` (private, ~`:93`) and extend the existing font setter (the method at ~`:19-21` that does `proFont = pro; …`) to also set `retroFont`. At the call site (search `setFonts(` / where the editor passes fonts to `customLookAndFeel`), pass the editor's Press Start 2P font. If the setter signature changes, update both the standalone-editor and popup uses.

- [ ] **Step 4: Rewrite the title bar accent-aware.** Replace `BreadbinLookAndFeel::drawDocumentWindowTitleBar` body (`src/PluginEditor.cpp:240-258`) with:
```cpp
void BreadbinLookAndFeel::drawDocumentWindowTitleBar(
    juce::DocumentWindow &window, juce::Graphics &g, int w, int h,
    int titleSpaceX, int titleSpaceW, const juce::Image *, bool) {
  if (w * h == 0) return;
  const juce::Colour accent = accentOf(window);
  g.setColour(juce::Colour(0xFF101016));
  g.fillRect(0, 0, w, h);
  g.setColour(accent.withAlpha(0.55f));
  g.drawHorizontalLine(h - 1, 0.0f, static_cast<float>(w));
  gm::ui::drawGlowText(g, window.getName(), retroFont.withHeight(10.0f),
                       juce::Rectangle<int>(titleSpaceX, 0, titleSpaceW, h).toFloat(),
                       accent, juce::Justification::centred);
}
```

- [ ] **Step 5: Build.** Run: `cmake --build build --config Release --target Breadbin_All -- /m /verbosity:minimal`
Expected: clean compile + link. (No visual change yet — the panels still paint opaque; applied in C3.)

- [ ] **Step 6: Commit.**
```bash
git add src/PluginEditor.cpp src/PluginEditor.h
git commit -m "feat(ui): shared glass-fill helper + accent-aware popup title bar (Phase C)"
```
End with the co-author trailer.

---

### Task C3: Apply glass chrome to the 5 panels + set per-popup accent

**Files:**
- Modify: `src/PluginEditor.h` — add `juce::Image gridCache, scanCache;` members to `ModMatrixPanel`, `ChordMemoryPanel`, `WavetablePanel`, `SidPlayerPanel`, `DigiSamplerPanel`.
- Modify: `src/PluginEditor.cpp` — each panel's `resized()` builds the caches; each panel's `paint()` opens with `drawPopupGlass(...)` instead of `fillAll`; each `show*Popup()` sets the accent.

- [ ] **Step 1: Add cache members.** To each of the 5 panel classes in `src/PluginEditor.h`, add (private): `juce::Image gridCache, scanCache;`

- [ ] **Step 2: Build the caches in `resized()`.** At the **end** of each panel's `resized()` in `src/PluginEditor.cpp`, add (mirrors the `bgCache` build at `:3929`):
```cpp
  if (getWidth() > 0 && getHeight() > 0) {
    auto src = juce::ImageCache::getFromMemory(BinaryData::popup_grid_png,
                                               BinaryData::popup_grid_pngSize);
    gridCache = juce::Image(juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics gc(gridCache);
    if (src.isValid())
      gc.drawImage(src, 0, 0, getWidth(), getHeight(), 0, 0, src.getWidth(), src.getHeight());
    else { gc.setColour(gm::ui::theme::bg0); gc.fillAll(); }
    scanCache = gm::ui::makeScanlineOverlay(getWidth(), getHeight());
  }
```

- [ ] **Step 3: Swap each panel's opaque fill for glass.** In each panel's `paint()`, replace the leading `g.fillAll(juce::Colour(30,30,35));` **and** the immediately-following border `drawRoundedRectangle(...)` (the `Colour(50,50,60).withAlpha(0.5f)` ring — now redundant) with:
```cpp
  drawPopupGlass(g, getLocalBounds().toFloat(), BreadbinLookAndFeel::accentOf(*this),
                 gridCache, scanCache);
```
For `DigiSamplerPanel` (`:629`), replace the `fillRoundedRectangle` + cyan border similarly; keep its inner waveform-area fill (`Colour(18,18,22)`). Keep each panel's other content draws (title pills, section backgrounds) — though the cyan title pills can be removed later if redundant with the new glow title bar (note for the visual pass, not required now).

- [ ] **Step 4: Set the per-popup accent.** In each `show*Popup()` (`src/PluginEditor.cpp:2460-2593`), after `panel->setLookAndFeel(&customLookAndFeel);`, add the accent on **both** the panel and the window (the window drives the title bar):
```cpp
  const int acc = (int)<TOKEN>.getARGB();
  panel->getProperties().set("accent", acc);
  window->getProperties().set("accent", acc);   // place AFTER `window` is constructed
```
Token per popup: Modulation `gm::ui::theme::cyan`, Wavetable `gm::ui::theme::cyan`, Chord `gm::ui::theme::mag`, SID Player `gm::ui::theme::cyan`, Digi `gm::ui::theme::cyan`. (Set the panel accent right after `setLookAndFeel`; set the window accent right after the `new NonModalPopup(...)` line, before `setVisible`.)

- [ ] **Step 5: Build.** Run: `cmake --build build --config Release --target Breadbin_All -- /m /verbosity:minimal` — clean compile + link.

- [ ] **Step 6: Manual visual checkpoint.** Launch the Standalone; open all 5 popups (Modulation, Wavetable, Chord, SID Player, Digi). **Ask the user to confirm**: each shows the grid backdrop + translucent glass + accent glow edge + a Press-Start glow title in the popup's accent (Chord magenta, others cyan); controls read clearly; nothing clips. Tune the glow-edge alphas if requested. Do NOT check without the user's confirmation.

- [ ] **Step 7: Commit.**
```bash
git add src/PluginEditor.cpp src/PluginEditor.h
git commit -m "feat(ui): glass chrome + per-popup accent on all 5 popups (Phase C)"
```
End with the co-author trailer.

---

### Task C4: Per-role control accents inside the popups

**Files:**
- Modify: `src/PluginEditor.cpp` — where each panel's inner controls are constructed/configured (the `*Panel` constructors).

- [ ] **Step 1: Set inner-control accents via the `"accent"` property.** First read each `*Panel` class in `src/PluginEditor.h` (ModMatrixPanel ~456, WavetablePanel ~597, ChordMemoryPanel ~556, SidPlayerPanel ~654, DigiSamplerPanel ~695) to list its control members. Then, using `comp.getProperties().set("accent", (int)<token>.getARGB());` (the B2 mechanism), assign per the mockup, set where each control is added in the panel constructor:
  - **ModMatrixPanel** — LFO 1 block controls `cyan`; LFO 2 block controls `orange`; PWM Sweep controls `grn`; Mod-Matrix header + slot rows `mag`.
  - **WavetablePanel** — primary controls `cyan`; the per-step PW sliders `grn`.
  - **ChordMemoryPanel** — all controls `mag`.
  - **SidPlayerPanel** — controls `cyan`; `snapshotButton` `orange`.
  - **DigiSamplerPanel** — controls `cyan`; Loop toggle `grn`.
  Set each where the control is added (constructor/setup), matching how `setupSidPanel`/`setupVoiceEditor` set accents in Phase B.

- [ ] **Step 2: Build.** `cmake --build build --config Release --target Breadbin_All -- /m /verbosity:minimal` — clean.

- [ ] **Step 3: Manual visual checkpoint.** Launch; open Modulation (LFO1 cyan / LFO2 orange / PWM green / matrix magenta), Wavetable (cyan + green PW), Chord (magenta), SID Player (cyan + orange snapshot), Digi (cyan + green loop). **Ask the user to confirm** the per-role colors read correctly. Do NOT check without confirmation.

- [ ] **Step 4: Commit.**
```bash
git add src/PluginEditor.cpp
git commit -m "feat(ui): per-role accent colors inside popups (Phase C)"
```
End with the co-author trailer.

---

### Task C5: SID Player REG⟷BASIC register dump

**Files:**
- Modify: `src/PluginEditor.h` — `SidPlayerPanel`: add `bool basicView = false;` and `juce::TextButton regButton{"REG"}, basicButton{"BASIC"};`.
- Modify: `src/PluginEditor.cpp` — `SidPlayerPanel` ctor (set up the toggle), `resized()` (place it), `updateRegisterDisplay()` (branch REG/BASIC).

- [ ] **Step 1: Add the segmented toggle.** In the `SidPlayerPanel` constructor, configure `regButton`/`basicButton` as a 2-state segmented pair (set `setRadioGroupId`, `setClickingTogglesState(true)`, `setConnectedEdges`; `regButton.setToggleState(true, dontSendNotification)`). On click: `basicView = basicButton.getToggleState(); updateRegisterDisplay();`. Set their `"accent"` to `gm::ui::theme::cyan`. `addAndMakeVisible` both.

- [ ] **Step 2: Place the toggle in `resized()`.** Before the `registerDisplay.setBounds(bounds);` line (`src/PluginEditor.cpp:433`), carve a small header row off `bounds` for the REG/BASIC toggle (e.g. `auto regHdr = bounds.removeFromTop(20); regButton.setBounds(regHdr.removeFromLeft(54)); basicButton.setBounds(regHdr.removeFromLeft(60)); bounds.removeFromTop(4);`), then `registerDisplay.setBounds(bounds);`.

- [ ] **Step 3: Branch `updateRegisterDisplay()`.** Keep the early-return gating (`isPlaying()||isPaused()`, `snapshot.valid`). When `!basicView`, build the existing register table. When `basicView`, build a C64 BASIC POKE listing from the **same** `regs[]`, e.g.:
```cpp
  // BASIC view: POKE listing against SID base 54272 ($D400)
  juce::String t;
  t << "10 SID=54272 : REM $D400\n";
  t << "20 POKE SID+24," << (int)(regs[0x18] & 0x0F) << " : REM VOLUME\n";
  for (int v = 0; v < 3; ++v) {
    int b = v * 7;
    t << (30 + v * 20) << " POKE SID+" << (b + 0) << "," << (int)regs[b + 0]
      << " : POKE SID+" << (b + 1) << "," << (int)regs[b + 1] << " : REM V" << (v + 1) << " FREQ\n";
    t << (40 + v * 20) << " POKE SID+" << (b + 5) << "," << (int)regs[b + 5]
      << " : POKE SID+" << (b + 6) << "," << (int)regs[b + 6] << " : REM V" << (v + 1) << " ADSR\n";
  }
  t << "90 POKE SID+4," << (int)regs[0x04] << " : REM V1 CTRL/GATE\n";
  registerDisplay.setText(t, juce::dontSendNotification);
```
(REG branch keeps the current formatting. Read-only view over the snapshot — no DSP changes.)

- [ ] **Step 4: Build + visual checkpoint.** Build; launch; load a `.sid`, play; toggle REG⟷BASIC. **Ask the user to confirm** both views render and update live. Do NOT check without confirmation.

- [ ] **Step 5: Commit.**
```bash
git add src/PluginEditor.cpp src/PluginEditor.h
git commit -m "feat(ui): SID Player REG/BASIC register dump (Phase C)"
```
End with the co-author trailer.

---

### Task C6: SID Player `LOAD"…",8,1` nostalgia line

**Files:**
- Modify: `src/PluginEditor.h` — `SidPlayerPanel`: add `juce::String loadedFileName;` and `juce::Label loadLineLabel;`; bump `panelHeight` 370 → 396.
- Modify: `src/PluginEditor.cpp` — capture the filename in the load lambda; configure + place + show `loadLineLabel`.

- [ ] **Step 1: Capture the filename.** In the load lambda (`src/PluginEditor.cpp:284`, near `tuneInfoLabel.setText("Loaded: " + file.getFileName() …)`), add: `loadedFileName = file.getFileNameWithoutExtension().toUpperCase();` then refresh the load line (Step 3 helper).

- [ ] **Step 2: Configure the label.** In the ctor: `loadLineLabel.setFont(monoFont.withHeight(11.0f)); loadLineLabel.setColour(juce::Label::textColourId, gm::ui::theme::cyan); loadLineLabel.setJustificationType(juce::Justification::centredLeft); loadLineLabel.setVisible(false); addAndMakeVisible(loadLineLabel);` Set its `"accent"` cyan.

- [ ] **Step 3: Set the text on load.** After capturing the name, set:
```cpp
  loadLineLabel.setText("LOAD\"" + loadedFileName + "\",8,1   READY.   █   DEVICE 8 · 1541",
                        juce::dontSendNotification);
  loadLineLabel.setVisible(true);
```
(The `█` block is a static cursor — no blink; animation is Phase D.)

- [ ] **Step 4: Place it in `resized()`.** Insert a row for `loadLineLabel` (e.g. directly under Row 1 / the load button row): `auto loadRow = bounds.removeFromTop(18); loadLineLabel.setBounds(loadRow); bounds.removeFromTop(4);` Adjust the `panelHeight` constant (370 → 396) so nothing is squeezed; verify the register area still has room.

- [ ] **Step 5: Build + visual checkpoint.** Build; launch; load a `.sid`. **Ask the user to confirm** the `LOAD"NAME",8,1 … DEVICE 8 · 1541` line shows in cyan mono and the panel isn't cramped. Do NOT check without confirmation.

- [ ] **Step 6: Commit.**
```bash
git add src/PluginEditor.cpp src/PluginEditor.h
git commit -m "feat(ui): SID Player LOAD\",8,1 nostalgia line (Phase C)"
```
End with the co-author trailer.

---

### Task C7: Floppy / tape icons on Load/Save buttons

**Files:**
- Modify: `src/PluginEditor.cpp` — add `juce::Path` icon helpers (anonymous namespace) + draw them in `SidPlayerPanel`, `ChordMemoryPanel`, `DigiSamplerPanel` paints.

- [ ] **Step 1: Add icon helpers.** In the anonymous namespace, add `drawFloppyIcon(juce::Graphics&, juce::Rectangle<float>, juce::Colour)` and `drawTapeIcon(...)` using simple `juce::Path`s (floppy: rounded square + notched top-right corner + a small label rect; tape: rounded rect + two hub circles). Stroke in the accent colour, ~1px.

- [ ] **Step 2: Draw next to the buttons.** In the panels' `paint()`, draw the icon in the gap to the left of the relevant button bounds (use the same `Rectangle` math as `resized()` so it tracks): SID Player → floppy left of `loadButton`; Chord → floppy left of its Save (and Load) button(s); Digi → tape left of `loadButton`. Keep icons ~12px, vertically centred on the button.

- [ ] **Step 3: Build + visual checkpoint.** Build; launch; open SID Player, Chord, Digi. **Ask the user to confirm** the floppy/tape glyphs render cleanly next to the Load/Save buttons. Do NOT check without confirmation.

- [ ] **Step 4: Commit.**
```bash
git add src/PluginEditor.cpp
git commit -m "feat(ui): floppy/tape icons on popup Load/Save buttons (Phase C)"
```
End with the co-author trailer.

---

### Task C8: Phase C verification + docs + milestone

- [ ] **Step 1: Full suite.** Run: `ctest --test-dir build -C Release`
Expected: **LFO Passed / Integration Passed / Mutation 17-of-18** (the single triangle-boundary survivor is the pre-existing known issue; ctest marks MutationTests "Failed" on that non-zero exit — confirm the count is still 17/18, unchanged, since Phase C is UI-only).

- [ ] **Step 2: Docs.** With the user's confirmation that all popups read correctly, update `STATE.md` (Phase C done; D pending) and `CHANGELOG.md` (`[Unreleased]` → the popup glass chrome + per-role accents + SID-Player REG/BASIC + LOAD line + icons; note the NEON⟷C64 switch was dropped). Commit:
```bash
git add STATE.md CHANGELOG.md
git commit -m "docs: mark Phase C (popup glass chrome + C64 nostalgia) done"
```

- [ ] **Step 3: Archive + tag.** Preserve the Release Standalone + VST3 to `releases/<yyyy-mm-dd_HHmm>_phase-c/` (discover artifact paths dynamically, copy both, verify), then:
```bash
git tag -a checkpoint/phase-c-popups -m "Reskin Phase C: popup glass chrome + C64 nostalgia layer"
```

---

## Notes for the executor
- **UI verification rule:** C1 Step 4, C3 Step 6, C4 Step 3, C5 Step 4, C6 Step 5, C7 Step 3 require explicit user visual confirmation — a green build is not sufficient. Sub-phase by popup if a panel needs iteration (like B1).
- **No behavior changes:** the REG⟷BASIC dump + LOAD line are read-only views over already-exposed `getRegisterSnapshot()` state and the captured filename. Do not touch `SidFilePlayer` / its threading. If you find yourself changing a parameter, callback, or DSP path, stop.
- **Occlusion gotcha:** the popup window's `Colour(30,30,35)` background is never visible — the glass MUST be drawn inside each panel's `paint()` (Task C3). Drawing it at the window level would be hidden.
- **No `gm::ui` glass primitive:** compose from the local `drawPopupGlass` helper (grid image + `drawGlassFill` + `makeScanlineOverlay` + manual accent glow). Promoting a reusable glass-popup frame to `gm::ui` is deferred (per the spec).
- **Rollback:** `checkpoint/phase-b-layout` predates all of this.
