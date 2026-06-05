# C64 Neon-Synthwave Reskin — Phase A (Foundation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the reusable "neon synthwave" UI foundation (theme tokens + LookAndFeel + CRT scopes + glass background) and apply it to Breadbin's *current* layout — no controls move.

**Architecture:** Reusable, header-only render primitives live in **GhostmoonGPL** (`gm::ui::`, LGPL); Breadbin's `BreadbinLookAndFeel` wires them to its controls. Rendering is verified by build + **manual visual check** (UI can't be auto-verified); only pure geometry/token logic gets unit tests.

**Tech Stack:** C++20, JUCE 8 (`juce::Graphics`, `LookAndFeel_V4`), CMake, GhostmoonGPL (LGPL header-only).

**Source of exact values:** the design bundle at `C:\tmp\breadbin-design\breadbin1\project\` — `synthwave.css` (tokens), `parts.jsx` (widget construction). Read the named file per task for per-pixel values; this plan cites the key ones.

**Cross-repo note:** Tasks 1, 2, 4, 5, 6 commit in the **GhostmoonGPL** repo (`C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoongpl`). Tasks 3, 7, 8, 9, 10 commit in **Breadbin** (`D:\Code\breadbin`, branch `polish/ui-2026-06-05`). Each commit step says which repo.

**Reference spec:** `docs/superpowers/specs/2026-06-05-c64-theme-reskin-design.md`.

---

### Task 1: GhostmoonGPL UI target + Breadbin consumption wiring

**Files:**
- Create: `ghostmoongpl/ui/include/ghostmoon/ui/Version.h` (smoke header)
- Modify: `ghostmoongpl/CMakeLists.txt` (add `ghostmoongpl_ui` INTERFACE target)
- Modify: `D:\Code\breadbin\CMakeLists.txt` (consume it)

- [ ] **Step 1: Add the UI INTERFACE target to GhostmoonGPL.** In `ghostmoongpl/CMakeLists.txt`, after the existing `ghostmoongpl` target, add:

```cmake
# Header-only JUCE UI primitives (consumer must provide JUCE on the include path).
add_library(ghostmoongpl_ui INTERFACE)
target_include_directories(ghostmoongpl_ui INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/include")
target_compile_features(ghostmoongpl_ui INTERFACE cxx_std_20)
add_library(ghostmoongpl::ui ALIAS ghostmoongpl_ui)
```

- [ ] **Step 2: Add a smoke header** `ghostmoongpl/ui/include/ghostmoon/ui/Version.h`:

```cpp
#pragma once
namespace gm::ui { inline constexpr int kVersion = 1; }
```

- [ ] **Step 3: Wire Breadbin to consume it.** In `D:\Code\breadbin\CMakeLists.txt`, after the `libsidplayfp` block, add (default path = sibling repo, overridable):

```cmake
set(GHOSTMOONGPL_DIR "C:/Users/estee/Desktop/My Stuff/Code/Antigravity/ghostmoongpl"
    CACHE PATH "Path to the GhostmoonGPL repo")
add_subdirectory("${GHOSTMOONGPL_DIR}" "${CMAKE_BINARY_DIR}/ghostmoongpl-build")
```
Then add `ghostmoongpl::ui` to `target_link_libraries(Breadbin PRIVATE ...)` and to `BreadbinIntegrationTests`.

- [ ] **Step 4: Smoke-test the link.** In `src/PluginEditor.cpp`, temporarily add near the top: `#include <ghostmoon/ui/Version.h>` and `static_assert(gm::ui::kVersion == 1);`. Run:

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --target Breadbin_All -- /m /verbosity:minimal
```
Expected: configures (prints the GhostmoonGPL add_subdirectory) and compiles. Then remove the temporary include/static_assert.

- [ ] **Step 5: Commit (both repos).**

```bash
# in ghostmoongpl repo:
git add CMakeLists.txt ui/include/ghostmoon/ui/Version.h
git commit -m "feat(ui): add header-only ghostmoongpl_ui INTERFACE target"
# in D:\Code\breadbin:
git add CMakeLists.txt
git commit -m "build: consume ghostmoongpl_ui via GHOSTMOONGPL_DIR"
```

---

### Task 2: Theme color tokens (`gm::ui::theme`) + DESIGN.md

**Files:**
- Create: `ghostmoongpl/ui/include/ghostmoon/ui/Theme.h`
- Create: `D:\Code\breadbin\DESIGN.md`
- Test: extend `D:\Code\breadbin\tests\IntegrationTests.cpp` (JUCE already linked there)

- [ ] **Step 1: Write the token sanity test** in `IntegrationTests.cpp` (follow the file's existing `check(...)` style):

```cpp
#include <ghostmoon/ui/Theme.h>
// in main():
check("theme.cyan",   gm::ui::theme::cyan.getARGB()   == 0xFF33EDED);
check("theme.orange", gm::ui::theme::orange.getARGB() == 0xFFFFAE3B);
check("theme.cblue",  gm::ui::theme::cblue.getARGB()  == 0xFF8B80E8);
```

- [ ] **Step 2: Run it to verify it fails.**
```
cmake --build build --config Release --target BreadbinIntegrationTests -- /m
./build/Release/BreadbinIntegrationTests.exe
```
Expected: FAIL to compile (`Theme.h` not found).

- [ ] **Step 3: Create `Theme.h`** — all tokens from `synthwave.css` lines 8–19 as `juce::Colour` (ARGB, `0xFF` alpha):

```cpp
#pragma once
#include <juce_graphics/juce_graphics.h>
namespace gm::ui::theme {
  // structure
  inline const juce::Colour bg0{0xFF0A0A0E}, bg1{0xFF13131A}, panel{0xFF16161D},
    panel2{0xFF1C1C25}, panel3{0xFF22222D}, line{0xFF2C2C39}, line2{0xFF3A3A4A},
    inset{0xFF0E0E13};
  // text
  inline const juce::Colour txt{0xFFE7E7F0}, txt2{0xFFA6A6B8}, txt3{0xFF6F6F82};
  // neon accents
  inline const juce::Colour cyan{0xFF33EDED}, cyanD{0xFF1AA6A6}, orange{0xFFFFAE3B},
    orangeD{0xFFC97F1E}, grn{0xFFB6F23C}, grnD{0xFF7FAE23}, mag{0xFFFF3DF0},
    purple{0xFF9A6BFF}, gold{0xFFFFCB45}, red{0xFFFF5468}, lime{0xFF5DFF7A}, yellow{0xFFFFE14D};
  // C64 VIC-II (popups, Phase C)
  inline const juce::Colour cblue{0xFF8B80E8}, cgrn{0xFF9AD284}, cyel{0xFFD6DD7E},
    cred{0xFFD08A72}, cpur{0xFFB98AE0}, beige{0xFFD8C79F};
  inline juce::Colour glow(juce::Colour c, float a) { return c.withAlpha(a); }
}
```

- [ ] **Step 4: Run the test to verify it passes.** Rebuild + run `BreadbinIntegrationTests.exe`. Expected: the three `theme.*` checks PASS (overall `=== Results: 408 passed, 0 failed ===`).

- [ ] **Step 5: Write `DESIGN.md`** at the Breadbin root documenting the palette table, the four font roles (Press Start 2P headers / JetBrains Mono readouts / Lato 400/700 labels-body), and the component specs from the spec's §B/§D. (Use the `design-md` skill to format.)

- [ ] **Step 6: Commit (both repos).**
```bash
# ghostmoongpl:
git add ui/include/ghostmoon/ui/Theme.h
git commit -m "feat(ui): add gm::ui::theme synthwave color tokens"
# breadbin:
git add DESIGN.md tests/IntegrationTests.cpp
git commit -m "docs: add DESIGN.md; test: assert theme token values"
```

---

### Task 3: Embed new background + logo assets

**Files:**
- Create: `D:\Code\breadbin\assets\background_clean.png` (copy from bundle)
- Create: `D:\Code\breadbin\assets\logo.png` (copy from bundle)
- Modify: `D:\Code\breadbin\CMakeLists.txt` (`juce_add_binary_data(BreadbinAssets ...)`)

- [ ] **Step 1: Copy the assets.**
```
copy "C:\tmp\breadbin-design\breadbin1\project\assets\background_clean.png" "D:\Code\breadbin\assets\"
copy "C:\tmp\breadbin-design\breadbin1\project\assets\logo.png" "D:\Code\breadbin\assets\"
```

- [ ] **Step 2: Add them to BinaryData.** In `CMakeLists.txt`, append to the `juce_add_binary_data(BreadbinAssets SOURCES ...)` list:
```cmake
        assets/background_clean.png
        assets/logo.png
```

- [ ] **Step 3: Verify the symbols build.** Add a temporary `static_assert(BinaryData::background_clean_pngSize > 0);` in `PluginEditor.cpp`, run `cmake -B build` then build `Breadbin_All`. Expected: configures + compiles (BinaryData symbols generated). Remove the temporary assert.

- [ ] **Step 4: Commit (breadbin).**
```bash
git add assets/background_clean.png assets/logo.png CMakeLists.txt
git commit -m "assets: embed clean background + logo for reskin"
```

---

### Task 4: `gm::ui` control renderers — knob + h/v sliders

**Files:**
- Create: `ghostmoongpl/ui/include/ghostmoon/ui/Geometry.h` (pure, testable)
- Create: `ghostmoongpl/ui/include/ghostmoon/ui/Controls.h` (rendering)
- Test: `ghostmoongpl/tests/test_ui_geometry.cpp` (GoogleTest, JUCE-free)

- [ ] **Step 1: Write the failing geometry test** `tests/test_ui_geometry.cpp`:

```cpp
#include <ghostmoon/ui/Geometry.h>
#include <gtest/gtest.h>
TEST(Geometry, KnobSweep270) {
  EXPECT_NEAR(gm::ui::knobSweepProportion(0.0f), 0.0f, 1e-5);
  EXPECT_NEAR(gm::ui::knobSweepProportion(1.0f), 1.0f, 1e-5);
}
TEST(Geometry, BipolarFillFromCenter) {
  EXPECT_NEAR(gm::ui::bipolarFill(0.5f).first,  0.5f, 1e-5); // start
  EXPECT_NEAR(gm::ui::bipolarFill(0.5f).second, 0.5f, 1e-5); // end == center -> zero width
  auto [a,b] = gm::ui::bipolarFill(0.75f);
  EXPECT_NEAR(a, 0.5f, 1e-5); EXPECT_NEAR(b, 0.75f, 1e-5);
}
```
Register it in `ghostmoongpl/CMakeLists.txt` under `GHOSTMOONGPL_BUILD_TESTS` (new `add_executable(ghostmoongpl_ui_tests tests/test_ui_geometry.cpp)` linked to `ghostmoongpl_ui` + `GTest::gtest_main`; `gtest_discover_tests`).

- [ ] **Step 2: Run it to verify it fails.**
```
cmake -S "<ghostmoongpl>" -B "<ghostmoongpl>/build" -DGHOSTMOONGPL_BUILD_TESTS=ON
cmake --build "<ghostmoongpl>/build"
ctest --test-dir "<ghostmoongpl>/build"
```
Expected: FAIL to compile (`Geometry.h` missing).

- [ ] **Step 3: Write `Geometry.h`** (JUCE-free pure math):
```cpp
#pragma once
#include <utility>
#include <algorithm>
namespace gm::ui {
  inline float knobSweepProportion(float v01) { return std::clamp(v01, 0.0f, 1.0f); }
  // returns {fillStart01, fillEnd01} for a bipolar control filling from center (0.5)
  inline std::pair<float,float> bipolarFill(float v01) {
    v01 = std::clamp(v01, 0.0f, 1.0f);
    return v01 >= 0.5f ? std::pair{0.5f, v01} : std::pair{v01, 0.5f};
  }
}
```

- [ ] **Step 4: Run tests to verify they pass.** Rebuild + `ctest`. Expected: 2 PASS.

- [ ] **Step 5: Write `Controls.h`** — knob + sliders. Signatures (header-only inline bodies). Use exact `parts.jsx` values: knob 270° sweep (start 135°, end 405° ≡ 2.356→7.069 rad), track arc `theme::line`-ish `#26262f` w3, value arc accent w3 + `glow(accent,.5)` bloom pass, cap `radial #3A3A46→#15151C` border `#000`, pointer bar accent + glow; HSlider track h6 `theme::inset` inner-shadow, fill accent + glow, thumb 13×18 `gradient #D2D2DC→#5A5A66`; VSlider mirror (18 wide, fill from bottom). Bipolar uses `bipolarFill()`.
```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <ghostmoon/ui/Theme.h>
#include <ghostmoon/ui/Geometry.h>
namespace gm::ui {
  void drawKnob (juce::Graphics&, juce::Rectangle<float>, float v01, juce::Colour accent, bool bipolar=false);
  void drawHSlider(juce::Graphics&, juce::Rectangle<float>, float v01, juce::Colour accent, bool bipolar=false);
  void drawVSlider(juce::Graphics&, juce::Rectangle<float>, float v01, juce::Colour accent, bool bipolar=false);
  // ... inline bodies: see parts.jsx Knob/HSlider/VSlider for exact arcs, gradients, glows.
}
```

- [ ] **Step 6: Commit (ghostmoongpl).**
```bash
git add ui/include/ghostmoon/ui/Geometry.h ui/include/ghostmoon/ui/Controls.h tests/test_ui_geometry.cpp CMakeLists.txt
git commit -m "feat(ui): knob + h/v slider renderers with geometry tests"
```

---

### Task 5: `gm::ui` chrome — panel, glow text, combo, toggle, button

**Files:**
- Create: `ghostmoongpl/ui/include/ghostmoon/ui/Chrome.h`

- [ ] **Step 1: Write `Chrome.h`** with these inline renderers (values from `synthwave.css` §29–80):
```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <ghostmoon/ui/Theme.h>
namespace gm::ui {
  // bb-panel: linear-gradient(panel2->panel), 1px line border, r8, inset top sheen + drop shadow
  void drawPanel(juce::Graphics&, juce::Rectangle<float>, juce::Colour accent, bool glowBorder=false);
  // PressStart accent text with text-shadow 0 0 12px + 0 0 4px (layered translucent passes)
  void drawGlowText(juce::Graphics&, const juce::String&, const juce::Font&,
                    juce::Rectangle<float>, juce::Colour accent, juce::Justification);
  // bb-combo background gradient #202028->#17171e, line2 border, r5 (caller draws value text in accent)
  void drawComboBackground(juce::Graphics&, juce::Rectangle<float>);
  // bb-tgl dot 10x10 r3: off #0C0C10/line2; on = accent fill+border + glow(accent,.8)
  void drawToggleDot(juce::Graphics&, juce::Rectangle<float>, juce::Colour accent, bool on);
  // bb-btn gradient #2A2A34->#1D1D25, line2 border, r5, inset+drop shadow
  void drawButtonBackground(juce::Graphics&, juce::Rectangle<float>, bool down);
}
```

- [ ] **Step 2: Compile check (in Breadbin).** `Chrome.h` pulls in JUCE, so it is first compiled when Breadbin includes it (Task 7) — the `ghostmoongpl_ui_tests` target stays JUCE-free (geometry only). No standalone build or unit assertions here; these renderers are verified visually in Tasks 7–9.

- [ ] **Step 3: Commit (ghostmoongpl).**
```bash
git add ui/include/ghostmoon/ui/Chrome.h
git commit -m "feat(ui): panel/glow-text/combo/toggle/button chrome renderers"
```

---

### Task 6: `gm::ui` CRT scope — background, scanline overlay, bloom trace

**Files:**
- Create: `ghostmoongpl/ui/include/ghostmoon/ui/Scope.h`
- Test: extend `ghostmoongpl/tests/test_ui_geometry.cpp` (overlay dimensions)

- [ ] **Step 1: Write the failing overlay test in Breadbin** (`tests/IntegrationTests.cpp` — JUCE is linked there; keep the ggpl `test_ui_geometry.cpp` JUCE-free):
```cpp
#include <ghostmoon/ui/Scope.h>
// in main():
{ auto img = gm::ui::makeScanlineOverlay(120, 58, 1.48f);
  check("scope.overlaySize", img.isValid() && img.getWidth()==120 && img.getHeight()==58); }
```

- [ ] **Step 2: Build BreadbinIntegrationTests to verify it fails** (compile error: `Scope.h` / `makeScanlineOverlay` missing).

- [ ] **Step 3: Write `Scope.h`** (values from `synthwave.css` §139–147):
```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <ghostmoon/ui/Theme.h>
namespace gm::ui {
  // radial #0A1417->#05080A, r5, #000 border, inner shadow
  void drawScopeBackground(juce::Graphics&, juce::Rectangle<float>);
  // repeating 0deg lines, alpha ~0.34*scopeFactor; cache the result, do not rebuild per paint
  juce::Image makeScanlineOverlay(int w, int h, float scopeFactor=1.48f);
  // phosphor bloom: thick low-alpha (w3.5, glow(accent,.5)) then sharp (w1.3, accent)
  void drawScopeTrace(juce::Graphics&, const juce::Path&, juce::Colour accent);
}
```

- [ ] **Step 4: Build + run `BreadbinIntegrationTests.exe` to verify it passes** (`scope.overlaySize` passes; integration now 409).

- [ ] **Step 5: Commit (both repos).**
```bash
# ghostmoongpl:
git add ui/include/ghostmoon/ui/Scope.h
git commit -m "feat(ui): CRT scope background/scanline/bloom renderers"
# breadbin:
git add tests/IntegrationTests.cpp
git commit -m "test: assert gm::ui scanline overlay dimensions"
```

---

### Task 7: `BreadbinLookAndFeel` — wire renderers + accent + fonts

**Files:**
- Modify: `D:\Code\breadbin\src\PluginEditor.h:14-88` (BreadbinLookAndFeel — add accent helper)
- Modify: `D:\Code\breadbin\src\PluginEditor.cpp` (rewrite the draw override bodies)

- [ ] **Step 1: Add a per-control accent convention.** In `BreadbinLookAndFeel`, add a helper that reads an accent from the component's `Colour` properties, defaulting to `theme::cyan`:
```cpp
static juce::Colour accentOf(juce::Component& c) {
  auto v = c.getProperties().getWithDefault("accent", (int)0xFF33EDED);
  return juce::Colour((juce::uint32)(int)v);
}
```
Controls opt into an accent via `comp.getProperties().set("accent", (int)gm::ui::theme::orange.getARGB());` (done where controls are created — left to Phase B for full role coverage; Phase A defaults to cyan + sets SID-II controls to orange where trivial).

- [ ] **Step 2: Rewrite `drawRotarySlider`** body to call `gm::ui::drawKnob(g, Rectangle<float>(x,y,width,height), sliderPosProportional, accentOf(slider), slider.isBipolar-ish)`. Map JUCE's start/end angles to the 270° design. Include `#include <ghostmoon/ui/Controls.h>` and `Theme.h` at the top of `PluginEditor.cpp`.

- [ ] **Step 3: Rewrite `drawLinearSlider`** to dispatch on `style`: horizontal → `drawHSlider`, vertical → `drawVSlider`, using `(sliderPos - x) / width` (or height) for `v01` and `accentOf(slider)`.

- [ ] **Step 4: Rewrite `drawComboBox`** → `gm::ui::drawComboBackground(...)` + value text in `accentOf(box)` using the Lato font; **rewrite `getComboBoxFont`** to return Lato ~11px (was `proFont` 14). Rewrite `drawToggleButton` → `gm::ui::drawToggleDot` + Lato label; `drawButtonBackground` → `gm::ui::drawButtonBackground`.

- [ ] **Step 5: Confirm font roles.** Ensure `setFonts` is called with pro = Press Start 2P, bold = Lato Bold, mono = JetBrains Mono (check the constructor call in `BreadbinEditor`); the design uses Lato for labels/combos/buttons, Press Start for eyebrow headers, JBMono for values.

- [ ] **Step 6: Build + manual visual check.**
```
cmake --build build --config Release --target Breadbin_All -- /m /verbosity:minimal
```
Then launch `build/Breadbin_artefacts/Release/Standalone/Breadbin.exe`. **Manual:** knobs render as 270° glowing arcs with metallic caps; sliders have inset tracks + accent fill + metallic thumbs; combos/toggles/buttons match the synthwave styling; fonts correct. **Ask the user to confirm** before checking this box (UI cannot be auto-verified).

- [ ] **Step 7: Commit (breadbin).**
```bash
git add src/PluginEditor.h src/PluginEditor.cpp
git commit -m "feat(ui): wire BreadbinLookAndFeel to gm::ui synthwave renderers"
```

---

### Task 8: Restyle `FilterDisplay` + `LFODisplay` as CRT scopes

**Files:**
- Modify: `D:\Code\breadbin\src\PluginEditor.cpp` (FilterDisplay::paint, LFODisplay::paint)
- Modify: `D:\Code\breadbin\src\PluginEditor.h` (cache an `Image scanlineOverlay` member in each)

- [ ] **Step 1: Cache the overlay.** In each display class, add a `juce::Image scanlineCache;` member; rebuild it in `resized()` via `gm::ui::makeScanlineOverlay(getWidth(), getHeight())` (NOT in paint).

- [ ] **Step 2: Rewrite `FilterDisplay::paint`** to: `gm::ui::drawScopeBackground`, build the existing biquad magnitude `Path`, `gm::ui::drawScopeTrace(g, path, accent)`, then `g.drawImageAt(scanlineCache, 0, 0)`. Same for `LFODisplay::paint` with its waveform path.

- [ ] **Step 3: Build + manual visual check.** Build `Breadbin_All`, launch standalone. **Manual:** filter/LFO displays show dark phosphor scopes with bloomed traces + subtle scanlines; no whole-window scanlines. **User confirms.**

- [ ] **Step 4: Commit (breadbin).**
```bash
git add src/PluginEditor.h src/PluginEditor.cpp
git commit -m "feat(ui): restyle filter/LFO displays as CRT phosphor scopes"
```

---

### Task 9: Editor background, vignette, glass panels + perf caching

**Files:**
- Modify: `D:\Code\breadbin\src\PluginEditor.cpp` (`BreadbinEditor::paint`, `drawHeaderGlow`)
- Modify: `D:\Code\breadbin\src\PluginEditor.h` (cached background `Image` members)

- [ ] **Step 1: Cache the background.** Add `juce::Image bgCache;` member; in `resized()`, render `background_clean.png` (cover-scaled) + the lightened vignette `radial .14→.52` once into `bgCache`.

- [ ] **Step 2: Rewrite `paint`** to `g.drawImageAt(bgCache, 0, 0)` instead of per-frame gradients. Panels: draw via `gm::ui::drawPanel(g, bounds, accent, glow)` with the translucent "glass" fill `rgba(20,22,34,.44)→rgba(7,8,14,.60)` so the background reads through. Rewrite `drawHeaderGlow` to call `gm::ui::drawGlowText`.

- [ ] **Step 3: Perf pass.** Confirm `paint()` contains no per-frame `ColourGradient`/`DropShadow` construction for static chrome (all cached). Cross-check against the `juce-perf-troubleshooting` skill checklist.

- [ ] **Step 4: Build + manual visual check.** Launch standalone. **Manual:** real background shows through translucent glass panels with accent-glow borders; lightened vignette; no paint hitching while dragging controls. **User confirms.**

- [ ] **Step 5: Commit (breadbin).**
```bash
git add src/PluginEditor.h src/PluginEditor.cpp
git commit -m "feat(ui): glass panels + cached background/vignette + glow headers"
```

---

### Task 10: ReverbSC → GhostmoonGPL (bonus) + Phase A verification

**Files:**
- Modify: `D:\Code\breadbin\src\PluginProcessor.h` (include path)
- Delete: `D:\Code\breadbin\src\dsp\ReverbSC.h`
- Modify: `D:\Code\breadbin\CMakeLists.txt` (link `ghostmoongpl::ghostmoongpl` for DSP)

- [ ] **Step 1: Switch the include.** In `PluginProcessor.h`, change `#include "dsp/ReverbSC.h"` → `#include <ghostmoon/ReverbSC.h>`; link `ghostmoongpl::ghostmoongpl` (the DSP target) in CMake. Delete `src/dsp/ReverbSC.h`.

- [ ] **Step 2: Build + run suites** (verify reverb still works identically — the integration suite has ReverbSC checks):
```
cmake --build build --config Release -- /m
ctest --test-dir build -C Release
```
Expected: **LFO 484, integration 409 (405 + 3 token + 1 overlay checks), mutation 17/18** — all pass, including `ReverbSC produces output` / `decay scales with feedback`.

- [ ] **Step 3: Full manual visual sign-off.** Launch standalone; the user does a full visual pass of the reskinned current layout (all control types, both scopes, glass background, fonts). **User confirms Phase A complete.**

- [ ] **Step 4: Preserve + tag.** Archive the build and tag the milestone:
```bash
# (discover artifact paths first, then) copy Standalone+VST3 to releases/<ts>_phase-a-foundation/
git add -A && git commit -m "chore: ReverbSC via GhostmoonGPL; complete Phase A foundation"
git tag -a checkpoint/phase-a-foundation -m "Reskin Phase A: synthwave LookAndFeel foundation on current layout"
```

---

## Notes for the executor
- **UI verification rule:** Tasks 7, 8, 9, 10 end in a manual visual check — do NOT mark those boxes without explicit user confirmation. Automated steps only confirm it builds/links, not that it looks right.
- **Exact pixel/hex values:** read the cited `synthwave.css` / `parts.jsx` lines per task rather than guessing.
- **Two repos:** keep GhostmoonGPL commits in its repo; Breadbin commits on `polish/ui-2026-06-05`.
- **Rollback:** `checkpoint/pre-c64-redesign` + `releases/2026-06-05_0146_pre-c64-redesign/` predate all of this.
