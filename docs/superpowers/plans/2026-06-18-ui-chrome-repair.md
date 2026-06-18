# UI Chrome Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore visible preset selection, logo branding, and a slim footer that hosts status text plus the poly/voicing controls.

**Architecture:** Keep the existing `BreadbinEditor` component tree and preset backend. The change is layout-only plus two footer labels, so implementation stays in `PluginEditor.h` and `PluginEditor.cpp`.

**Tech Stack:** JUCE C++ plugin editor, existing Breadbin binary assets, existing explicit `setBounds` layout helpers.

---

## File Structure

- Modify `D:/Code/breadbin/src/PluginEditor.h`: add footer labels and a footer panel bounds rectangle.
- Modify `D:/Code/breadbin/src/PluginEditor.cpp`: add footer label setup, draw the footer glass panel, rebalance region heights, move poly controls into a new footer layout helper, and protect preset selector width in the top bar.

Automated red/green UI layout tests are not practical in this codebase because the affected behavior is JUCE pixel placement and asset visibility inside the standalone/plugin editor. Verification is build plus manual visual check against the spec.

### Task 1: Footer Components

**Files:**
- Modify: `D:/Code/breadbin/src/PluginEditor.h`
- Modify: `D:/Code/breadbin/src/PluginEditor.cpp`

- [ ] **Step 1: Add footer member declarations**

Add two labels and a footer layout helper to `BreadbinEditor`:

```cpp
juce::Label footerBrandLabel;
juce::Label footerStatusLabel;
juce::Rectangle<int> footerPanelBounds;
void layoutFooter(juce::Rectangle<int> &bounds);
```

- [ ] **Step 2: Initialize labels in `setupGlobalControls()`**

Configure text, fonts, colors, justification, and visibility:

```cpp
footerBrandLabel.setText("ANTIGRAVITY · BREADBIN", juce::dontSendNotification);
footerBrandLabel.setFont(boldFont.withHeight(10.0f));
footerBrandLabel.setColour(juce::Label::textColourId, gm::ui::theme::cyan);
footerBrandLabel.setJustificationType(juce::Justification::centredLeft);
addAndMakeVisible(footerBrandLabel);

footerStatusLabel.setText("PATCH READY", juce::dontSendNotification);
footerStatusLabel.setFont(monoFont.withHeight(10.0f));
footerStatusLabel.setColour(juce::Label::textColourId, gm::ui::theme::txt2);
footerStatusLabel.setJustificationType(juce::Justification::centred);
addAndMakeVisible(footerStatusLabel);
```

- [ ] **Step 3: Build check**

Run:

```powershell
cmake --build build --config Release --target Breadbin_All
```

Expected: build exits 0.

### Task 2: Layout Rebalance

**Files:**
- Modify: `D:/Code/breadbin/src/PluginEditor.cpp`

- [ ] **Step 1: Add a footer region in `resizedContent()`**

Keep the default editor size, reduce tower/voice/fx/dock heights enough to add a footer below the keyboard:

```cpp
static constexpr int kFooterH = 28;
```

Assign:

```cpp
footerPanelBounds = bounds.removeFromTop(kFooterH);
layoutFooter(footerPanelBounds);
```

- [ ] **Step 2: Draw the footer glass panel**

In `paint()`, include:

```cpp
drawGlassPanel(footerPanelBounds);
```

- [ ] **Step 3: Remove poly controls from top-bar layout**

Delete these top-bar bounds assignments from `layoutTopRow()`:

```cpp
voiceModeSelector.setBounds(...);
polyMaxNotesSelector.setBounds(...);
polyVoiceCountLabel.setBounds(...);
paraSpreadLabel.setBounds(...);
paraSpreadSlider.setBounds(...);
paraRetrigButton.setBounds(...);
```

- [ ] **Step 4: Build check**

Run:

```powershell
cmake --build build --config Release --target Breadbin_All
```

Expected: build exits 0.

### Task 3: Footer Poly Cluster

**Files:**
- Modify: `D:/Code/breadbin/src/PluginEditor.cpp`

- [ ] **Step 1: Implement `layoutFooter()`**

Footer left-to-right:

```cpp
void BreadbinEditor::layoutFooter(juce::Rectangle<int> &bounds) {
  auto row = bounds.reduced(8, 3);
  auto centreV = [](juce::Rectangle<int> r, int h) {
    return r.withHeight(h).withY(r.getCentreY() - h / 2);
  };

  footerBrandLabel.setBounds(row.removeFromLeft(170).withHeight(16).withY(row.getCentreY() - 8));
  row.removeFromLeft(8);
  footerStatusLabel.setBounds(row.removeFromLeft(160).withHeight(16).withY(row.getCentreY() - 8));
  row.removeFromLeft(8);

  paraSpreadSlider.setBounds(centreV(row.removeFromRight(92), 20));
  paraSpreadLabel.setBounds(row.removeFromRight(42).withHeight(14).withY(row.getCentreY() - 7));
  paraRetrigButton.setBounds(centreV(row.removeFromRight(58), 20));
  polyVoiceCountLabel.setBounds(row.removeFromRight(36).withHeight(16).withY(row.getCentreY() - 8));
  polyMaxNotesSelector.setBounds(centreV(row.removeFromRight(46), 20));
  voiceModeSelector.setBounds(centreV(row.removeFromRight(92), 20));
  row.removeFromRight(8);
}
```

- [ ] **Step 2: Protect global preset width in `layoutTopRow()`**

Use a wider selector than the current 110 px:

```cpp
globalPresetSelector.setBounds(row.removeFromLeft(190).withHeight(22).withY(row.getCentreY() - 11));
```

- [ ] **Step 3: Ensure logo remains visible**

Keep `titleLabel` as a reserved bounds anchor but make its own text transparent/non-interactive if needed:

```cpp
titleLabel.setText("", juce::dontSendNotification);
titleLabel.setInterceptsMouseClicks(false, false);
```

- [ ] **Step 4: Final build and visual launch**

Run:

```powershell
cmake --build build --config Release --target Breadbin_All
Start-Process -FilePath "D:/Code/breadbin/build/Breadbin_artefacts/Release/Standalone/Breadbin.exe"
```

Expected: build exits 0 and the standalone opens.

Manual checks:

- Logo visible in the top-left top bar.
- Preset selector visible and wider than before.
- Footer visible below keyboard.
- Poly/voicing cluster appears only in footer.
- No obvious overlap at 100% scale.
