# ECO Poly SID Budget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add explicit ECO poly SID budget modes so dense polyphony can clock fewer reSIDfp engines while normal mode preserves current behavior.

**Architecture:** Add APVTS-backed performance settings, store a per-poly-voice render role (`Pair`, `LeftMono`, `RightMono`), and route poly note allocation/rendering through that role only when ECO mode is enabled. Keep ECO-off behavior byte-compatible, then profile and A/B ECO Hybrid as a deliberate alternate render mode.

**Tech Stack:** C++20, JUCE 8, Breadbin `BreadbinProcessor`/`BreadbinEditor`, APVTS, `BreadbinIntegrationTests`, `gm::CpuSectionProfiler`, Release builds.

---

## File Map

- Modify: `D:\Code\breadbin\src\PluginProcessor.h`
  - Add ECO budget enums, raw parameter refs, current cached settings, and `PolyVoice::sidRenderRole`.
- Modify: `D:\Code\breadbin\src\PluginProcessor.cpp`
  - Add APVTS parameters, cache settings, assign poly render roles, and branch poly rendering by role.
- Modify: `D:\Code\breadbin\src\PluginEditor.h`
  - Add Settings popup/button members and ECO controls.
- Modify: `D:\Code\breadbin\src\PluginEditor.cpp`
  - Build the Settings popup, expose ECO mode/budget/anchor, and show behavior status.
- Modify: `D:\Code\breadbin\tests\IntegrationTests.cpp`
  - Add behavior-preservation tests for ECO off and role/counter tests for ECO Hybrid.
- Modify: `D:\Code\breadbin\docs\OPTIMIZATION_TARGETS_2026-06-10.md`
  - Add ECO Hybrid as the next major performance target with measured before/after numbers.
- Modify: `D:\Code\breadbin\ROADMAP.md`
  - Add manual ECO, Hybrid/Max ECO, reSIDfp fork/API, and future Auto ECO milestones.
- Modify: `D:\Code\breadbin\CHANGELOG.md`
  - Note the new Settings popup and ECO poly budget once implemented.
- Modify: `D:\Code\breadbin\STATE.md`
  - Record current implementation status and profile artifacts.

---

### Task 1: Add ECO Performance Parameters Without Behavior Change

**Files:**
- Modify: `D:\Code\breadbin\src\PluginProcessor.h`
- Modify: `D:\Code\breadbin\src\PluginProcessor.cpp`
- Test: `D:\Code\breadbin\tests\IntegrationTests.cpp`

- [ ] **Step 1: Add failing parameter existence/default test**

In `tests/IntegrationTests.cpp`, add:

```cpp
void testEcoPerformanceParamsExistAndDefaultSafe() {
  std::printf("--- ECO performance params exist and default safe ---\n");
  auto p = createTestProcessor();

  auto *eco = p->apvts.getParameter("ecoMode");
  auto *budget = p->apvts.getParameter("polySidBudget");
  auto *anchor = p->apvts.getParameter("polyStereoAnchor");

  ASSERT_TRUE(eco != nullptr, "ecoMode parameter exists");
  ASSERT_TRUE(budget != nullptr, "polySidBudget parameter exists");
  ASSERT_TRUE(anchor != nullptr, "polyStereoAnchor parameter exists");
  ASSERT_TRUE(eco->getValue() == 0.0f, "ECO mode defaults to Off");
  ASSERT_TRUE(static_cast<int>(budget->convertFrom0to1(budget->getValue())) == 0,
              "Poly SID budget defaults to Hybrid for ECO");
  ASSERT_TRUE(static_cast<int>(anchor->convertFrom0to1(anchor->getValue())) == 0,
              "Stereo anchor defaults to Oldest");
}
```

Call it near the APVTS wiring tests in `main()`.

- [ ] **Step 2: Run the failing test**

Run:

```powershell
cmake --build build --config Release --target BreadbinIntegrationTests -- /m:1 /v:minimal
.\build\Release\BreadbinIntegrationTests.exe
```

Expected: build or test fails because the new parameters do not exist yet.

- [ ] **Step 3: Add enums and raw refs**

In `PluginProcessor.h`, near `VoiceMode`, add:

```cpp
  enum class EcoMode { Off = 0, Manual = 1 };
  enum class PolySidBudget { Hybrid = 0, Ultra = 1, MaxEco = 2 };
  enum class PolyStereoAnchor { Oldest = 0, Newest = 1 };
  enum class PolySidRenderRole { Pair = 0, LeftMono = 1, RightMono = 2 };
```

In `PolyVoice`, add:

```cpp
    PolySidRenderRole sidRenderRole = PolySidRenderRole::Pair;
```

Near existing raw parameter refs, add:

```cpp
  std::atomic<float> *ecoModePtr = nullptr;
  std::atomic<float> *polySidBudgetPtr = nullptr;
  std::atomic<float> *polyStereoAnchorPtr = nullptr;
```

Near cached voice mode state, add:

```cpp
  EcoMode ecoMode = EcoMode::Off;
  PolySidBudget polySidBudget = PolySidBudget::Hybrid;
  PolyStereoAnchor polyStereoAnchor = PolyStereoAnchor::Oldest;
```

- [ ] **Step 4: Add APVTS parameters**

In `BreadbinProcessor::createParameterLayout()`, after `polyMaxNotes`, add:

```cpp
  params.push_back(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"ecoMode", 1}, "ECO Mode",
      juce::StringArray{"Off", "Manual"}, 0));
  params.push_back(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"polySidBudget", 1}, "Poly SID Budget",
      juce::StringArray{"Hybrid", "Ultra", "Max ECO"}, 0));
  params.push_back(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"polyStereoAnchor", 1}, "Poly Stereo Anchor",
      juce::StringArray{"Oldest", "Newest"}, 0));
```

In `cacheParameterRefs()`, add:

```cpp
  ecoModePtr = apvts.getRawParameterValue("ecoMode");
  polySidBudgetPtr = apvts.getRawParameterValue("polySidBudget");
  polyStereoAnchorPtr = apvts.getRawParameterValue("polyStereoAnchor");
```

In the per-block parameter sync near `voiceMode`, add:

```cpp
  ecoMode = static_cast<EcoMode>(
      juce::jlimit(0, 1, static_cast<int>(ecoModePtr->load())));
  polySidBudget = static_cast<PolySidBudget>(
      juce::jlimit(0, 2, static_cast<int>(polySidBudgetPtr->load())));
  polyStereoAnchor = static_cast<PolyStereoAnchor>(
      juce::jlimit(0, 1, static_cast<int>(polyStereoAnchorPtr->load())));
```

- [ ] **Step 5: Run test to verify parameter defaults**

Run:

```powershell
cmake --build build --config Release --target BreadbinIntegrationTests -- /m:1 /v:minimal
.\build\Release\BreadbinIntegrationTests.exe
```

Expected: `testEcoPerformanceParamsExistAndDefaultSafe` passes and no existing test fails.

- [ ] **Step 6: Commit parameter scaffold**

Run:

```powershell
git -c safe.directory=D:/Code/breadbin add src/PluginProcessor.h src/PluginProcessor.cpp tests/IntegrationTests.cpp
git -c safe.directory=D:/Code/breadbin commit -m "feat: add ECO poly budget parameters"
```

---

### Task 2: Add ECO-Off Render Preservation Test

**Files:**
- Modify: `D:\Code\breadbin\tests\IntegrationTests.cpp`

- [ ] **Step 1: Add a render comparison helper**

Add a helper near existing render/A-B utilities:

```cpp
static double rmsDiff(const juce::AudioBuffer<float> &a,
                      const juce::AudioBuffer<float> &b) {
  const int channels = std::min(a.getNumChannels(), b.getNumChannels());
  const int samples = std::min(a.getNumSamples(), b.getNumSamples());
  double sum = 0.0;
  int count = 0;
  for (int ch = 0; ch < channels; ++ch) {
    const auto *pa = a.getReadPointer(ch);
    const auto *pb = b.getReadPointer(ch);
    for (int i = 0; i < samples; ++i) {
      const double d = static_cast<double>(pa[i]) - static_cast<double>(pb[i]);
      sum += d * d;
      ++count;
    }
  }
  return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}
```

- [ ] **Step 2: Add ECO-off preservation test**

Add:

```cpp
void testEcoOffPreservesUltraPolyRender() {
  std::printf("--- ECO off preserves ultra poly render ---\n");

  auto render = [](bool explicitlyUltra) {
    auto p = createTestProcessor();
    warmUp(*p);
    setParamReal(*p, "voiceMode", 2.0f);
    setParamReal(*p, "polyMaxNotes", 4.0f);
    setParamReal(*p, "ecoMode", 0.0f);
    if (explicitlyUltra)
      setParamReal(*p, "polySidBudget", 1.0f);
    warmUp(*p);

    juce::AudioBuffer<float> out(2, 512 * 24);
    out.clear();
    for (int b = 0; b < 24; ++b) {
      juce::MidiBuffer midi;
      if (b == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
      if (b == 2) midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0);
      if (b == 4) midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 0);
      juce::AudioBuffer<float> block(2, 512);
      block.clear();
      p->processBlock(block, midi);
      for (int ch = 0; ch < 2; ++ch)
        out.copyFrom(ch, b * 512, block, ch, 0, 512);
    }
    return out;
  };

  const auto defaultRender = render(false);
  const auto explicitUltraIgnored = render(true);
  const double diff = rmsDiff(defaultRender, explicitUltraIgnored);
  std::printf("  ECO-off default vs explicit Ultra RMS diff: %.9g\n", diff);
  ASSERT_TRUE(defaultRender.getRMSLevel(0, 0, defaultRender.getNumSamples()) >
                  1.0e-8f,
              "ECO-off preservation render produces non-silent output");
  ASSERT_TRUE(diff < 5.0e-4,
              "ECO off ignores polySidBudget and preserves current Ultra render");
}
```

The tolerance is intentionally looser than sample-identical because separate Breadbin processor
instances already show tiny reSIDfp/runtime variance before ECO render behavior exists. The assertion
still catches meaningful ECO-off render changes while preserving a non-silent poly render.

- [ ] **Step 3: Run the test**

Run:

```powershell
cmake --build build --config Release --target BreadbinIntegrationTests -- /m:1 /v:minimal
.\build\Release\BreadbinIntegrationTests.exe
```

Expected: test passes because implementation still ignores ECO budget.

- [ ] **Step 4: Commit preservation test**

Run:

```powershell
git -c safe.directory=D:/Code/breadbin add tests/IntegrationTests.cpp
git -c safe.directory=D:/Code/breadbin commit -m "test: pin ECO-off poly render preservation"
```

---

### Task 3: Assign Poly SID Render Roles

**Files:**
- Modify: `D:\Code\breadbin\src\PluginProcessor.h`
- Modify: `D:\Code\breadbin\src\PluginProcessor.cpp`
- Test: `D:\Code\breadbin\tests\IntegrationTests.cpp`

- [ ] **Step 1: Add counters for role assignment**

Extend `CpuAuditCounters`:

```cpp
    uint64_t polyPairVoiceBlocks = 0;
    uint64_t polyLeftMonoVoiceBlocks = 0;
    uint64_t polyRightMonoVoiceBlocks = 0;
```

Emit them from `getCpuAuditCountersJson()`:

```cpp
       << ",\"polyPairVoiceBlocks\":" << cpuAuditCounters.polyPairVoiceBlocks
       << ",\"polyLeftMonoVoiceBlocks\":" << cpuAuditCounters.polyLeftMonoVoiceBlocks
       << ",\"polyRightMonoVoiceBlocks\":" << cpuAuditCounters.polyRightMonoVoiceBlocks
```

- [ ] **Step 2: Add failing Hybrid role test**

Add:

```cpp
void testEcoHybridAssignsOnePairAndAlternatingMonoRoles() {
  std::printf("--- ECO Hybrid assigns one pair and alternating mono roles ---\n");
  auto p = createTestProcessor();
  warmUp(*p);
  setParamReal(*p, "voiceMode", 2.0f);
  setParamReal(*p, "polyMaxNotes", 4.0f);
  setParamReal(*p, "ecoMode", 1.0f);
  setParamReal(*p, "polySidBudget", 0.0f);
  setParamReal(*p, "polyStereoAnchor", 0.0f);
  warmUp(*p);

  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 64);
  midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 128);
  processBlock(*p, 512, &midi);

  p->resetCpuAuditCounters();
  processBlock(*p);
  const auto json = p->getCpuAuditCountersJson(1);
  ASSERT_TRUE(extractJsonCounter(json, "polyPairVoiceBlocks") == 1,
              "Hybrid clocks one stereo pair voice block");
  ASSERT_TRUE(extractJsonCounter(json, "polyLeftMonoVoiceBlocks") == 1,
              "Hybrid assigns one added note to left mono");
  ASSERT_TRUE(extractJsonCounter(json, "polyRightMonoVoiceBlocks") == 1,
              "Hybrid assigns one added note to right mono");
}
```

Expected before implementation: role counters remain zero or all voices remain pair.

- [ ] **Step 3: Implement role assignment helpers**

In `PluginProcessor.h`, declare:

```cpp
  bool isEcoPolyBudgetActive() const noexcept;
  PolySidRenderRole chooseNewPolyRenderRole() const noexcept;
```

In `PluginProcessor.cpp`, implement:

```cpp
bool BreadbinProcessor::isEcoPolyBudgetActive() const noexcept {
  return ecoMode == EcoMode::Manual && isPolyActive();
}

BreadbinProcessor::PolySidRenderRole
BreadbinProcessor::chooseNewPolyRenderRole() const noexcept {
  if (!isEcoPolyBudgetActive() || polySidBudget == PolySidBudget::Ultra)
    return PolySidRenderRole::Pair;

  if (polySidBudget == PolySidBudget::MaxEco) {
    int monoCount = 0;
    for (int i = 0; i < polyMaxNotes; ++i)
      if (polyVoices[i].active || polyVoices[i].releasing)
        ++monoCount;
    return (monoCount % 2 == 0) ? PolySidRenderRole::LeftMono
                                : PolySidRenderRole::RightMono;
  }

  bool hasPair = false;
  int monoCount = 0;
  for (int i = 0; i < polyMaxNotes; ++i) {
    const auto &pv = polyVoices[i];
    if (!pv.active && !pv.releasing)
      continue;
    if (pv.sidRenderRole == PolySidRenderRole::Pair)
      hasPair = true;
    else
      ++monoCount;
  }
  if (!hasPair || polyStereoAnchor == PolyStereoAnchor::Newest)
    return PolySidRenderRole::Pair;
  return (monoCount % 2 == 0) ? PolySidRenderRole::LeftMono
                              : PolySidRenderRole::RightMono;
}
```

- [ ] **Step 4: Demote previous anchor for Newest**

Add helper:

```cpp
void BreadbinProcessor::demoteExistingPolyPairForNewestAnchor() noexcept {
  if (!isEcoPolyBudgetActive() || polySidBudget != PolySidBudget::Hybrid ||
      polyStereoAnchor != PolyStereoAnchor::Newest)
    return;

  int monoCount = 0;
  for (int i = 0; i < polyMaxNotes; ++i) {
    auto &pv = polyVoices[i];
    if (!pv.active && !pv.releasing)
      continue;
    if (pv.sidRenderRole == PolySidRenderRole::Pair) {
      pv.sidRenderRole = (monoCount % 2 == 0) ? PolySidRenderRole::LeftMono
                                              : PolySidRenderRole::RightMono;
      ++monoCount;
    } else {
      ++monoCount;
    }
  }
}
```

Call this at the start of `polyNoteOn()` and the new-allocation path of `polyParaNoteOn()` before
assigning the new voice role.

- [ ] **Step 5: Set/reset roles on allocation and free**

In `polyNoteOn()` after selecting `pv`, add:

```cpp
  const auto newRole = chooseNewPolyRenderRole();
```

After `pv.fadeGain = 0.0f;`, add:

```cpp
  pv.sidRenderRole = newRole;
```

In free/all-notes-off paths, reset:

```cpp
  pv.sidRenderRole = PolySidRenderRole::Pair;
```

Mirror the same in `polyParaNoteOn()`, `polyAllNotesOff()`, and `polyParaAllNotesOff()`.

- [ ] **Step 6: Count role blocks in `generateAudio()`**

Where active poly voices are collected, after the skip-gate decision and before the sample loop,
increment:

```cpp
      switch (pv.sidRenderRole) {
      case PolySidRenderRole::Pair:
        ++cpuAuditCounters.polyPairVoiceBlocks;
        break;
      case PolySidRenderRole::LeftMono:
        ++cpuAuditCounters.polyLeftMonoVoiceBlocks;
        break;
      case PolySidRenderRole::RightMono:
        ++cpuAuditCounters.polyRightMonoVoiceBlocks;
        break;
      }
```

- [ ] **Step 7: Run tests**

Run:

```powershell
cmake --build build --config Release --target BreadbinIntegrationTests -- /m:1 /v:minimal
.\build\Release\BreadbinIntegrationTests.exe
```

Expected: Hybrid role test passes, ECO-off preservation remains green.

- [ ] **Step 8: Commit role assignment**

Run:

```powershell
git -c safe.directory=D:/Code/breadbin add src/PluginProcessor.h src/PluginProcessor.cpp tests/IntegrationTests.cpp
git -c safe.directory=D:/Code/breadbin commit -m "feat: assign ECO poly SID render roles"
```

---

### Task 4: Render Poly Voices According To Role

**Files:**
- Modify: `D:\Code\breadbin\src\PluginProcessor.cpp`
- Test: `D:\Code\breadbin\tests\IntegrationTests.cpp`

- [ ] **Step 1: Add failing clock-budget test**

Use the existing CPU counters and add:

```cpp
void testEcoHybridReducesPolySidClockWork() {
  std::printf("--- ECO Hybrid reduces poly SID clock work ---\n");
  auto p = createTestProcessor();
  warmUp(*p);
  setParamReal(*p, "voiceMode", 2.0f);
  setParamReal(*p, "polyMaxNotes", 4.0f);
  setParamReal(*p, "ecoMode", 1.0f);
  setParamReal(*p, "polySidBudget", 0.0f);
  warmUp(*p);

  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 64);
  midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 128);
  processBlock(*p, 512, &midi);

  p->resetCpuAuditCounters();
  for (int i = 0; i < 8; ++i)
    processBlock(*p);
  const auto json = p->getCpuAuditCountersJson(8);
  ASSERT_TRUE(extractJsonCounter(json, "polyPairVoiceBlocks") == 8,
              "One pair role per block for three-note Hybrid");
  ASSERT_TRUE(extractJsonCounter(json, "polyLeftMonoVoiceBlocks") == 8,
              "One left mono role per block for three-note Hybrid");
  ASSERT_TRUE(extractJsonCounter(json, "polyRightMonoVoiceBlocks") == 8,
              "One right mono role per block for three-note Hybrid");
}
```

This verifies role accounting; CPU reduction is verified by profile, not unit timing.

- [ ] **Step 2: Branch clock/mix by role**

In the poly sample loop, replace unconditional `sL = pv.sidLeft->clock()` / `sR = pv.sidRight->clock()`
with:

```cpp
        float voiceL = 0.0f;
        float voiceR = 0.0f;
        switch (pv.sidRenderRole) {
        case PolySidRenderRole::Pair: {
          const float sL = pv.sidLeft->clock();
          const float sR = pv.sidRight->clock();
          voiceL = sL * pv.fadeGain;
          voiceR = sR * pv.fadeGain;
          break;
        }
        case PolySidRenderRole::LeftMono:
          voiceL = pv.sidLeft->clock() * pv.fadeGain;
          break;
        case PolySidRenderRole::RightMono:
          voiceR = pv.sidRight->clock() * pv.fadeGain;
          break;
        }
```

Keep the existing pan/mix lines:

```cpp
        outL += voiceL * leftGainL * smoothedLeftVoiceGain
              + voiceR * rightGainL * smoothedRightVoiceGain;
        outR += voiceL * leftGainR * smoothedLeftVoiceGain
              + voiceR * rightGainR * smoothedRightVoiceGain;
```

- [ ] **Step 3: Trigger only needed sides for new ECO mono voices**

In `polyNoteOn()`, wrap note-on calls:

```cpp
  if (pv.sidRenderRole == PolySidRenderRole::Pair ||
      pv.sidRenderRole == PolySidRenderRole::LeftMono) {
    for (int v = 0; v < 3; ++v)
      if (voiceSettings[v].enabled)
        pv.sidLeft->noteOn(v, midiNote, velocity, leftDetuneCents);
  }
  if (pv.sidRenderRole == PolySidRenderRole::Pair ||
      pv.sidRenderRole == PolySidRenderRole::RightMono) {
    for (int v = 0; v < 3; ++v)
      if (voiceSettings[v + 3].enabled)
        pv.sidRight->noteOn(v, midiNote, velocity, rightDetuneCents);
  }
```

Mirror this for the new-allocation path in `polyParaNoteOn()` using slot `0`.

- [ ] **Step 4: Apply modulation only to used sides**

In the poly modulation loop, guard setter work:

```cpp
      const bool useLeft = pv.sidRenderRole == PolySidRenderRole::Pair ||
                           pv.sidRenderRole == PolySidRenderRole::LeftMono;
      const bool useRight = pv.sidRenderRole == PolySidRenderRole::Pair ||
                            pv.sidRenderRole == PolySidRenderRole::RightMono;
```

Then only call left-side setters when `useLeft`, and right-side setters when `useRight`. ECO off and
Ultra roles still use both sides.

- [ ] **Step 5: Run tests**

Run:

```powershell
cmake --build build --config Release --target BreadbinIntegrationTests -- /m:1 /v:minimal
.\build\Release\BreadbinIntegrationTests.exe
```

Expected: all integration tests pass. ECO-off preservation remains under the documented `5.0e-4`
RMS diff tolerance.

- [ ] **Step 6: Commit render role implementation**

Run:

```powershell
git -c safe.directory=D:/Code/breadbin add src/PluginProcessor.cpp tests/IntegrationTests.cpp
git -c safe.directory=D:/Code/breadbin commit -m "perf: render ECO poly voices by SID budget"
```

---

### Task 5: Add Settings Popup UI

**Files:**
- Modify: `D:\Code\breadbin\src\PluginEditor.h`
- Modify: `D:\Code\breadbin\src\PluginEditor.cpp`

- [ ] **Step 1: Add Settings button and controls**

In `PluginEditor.h`, add members:

```cpp
  juce::TextButton settingsButton{"SETTINGS"};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ecoModeAttachment;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> polySidBudgetAttachment;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> polyStereoAnchorAttachment;
```

If popup panels are represented by local panel classes, add a `SettingsPanel` class following the
existing Chord/Wavetable/SID Player popup pattern with:

```cpp
  juce::ComboBox ecoModeSelector;
  juce::ComboBox polySidBudgetSelector;
  juce::ComboBox polyStereoAnchorSelector;
  juce::Label statusLabel;
```

- [ ] **Step 2: Wire popup controls**

In `setupPopupButtons()` or the existing popup setup block, add:

```cpp
  settingsButton.setTooltip("Performance and plugin settings");
  settingsButton.onClick = [this] { showSettingsPopup(); };
  addAndMakeVisible(settingsButton);
```

In `SettingsPanel` construction:

```cpp
  ecoModeSelector.addItem("Off", 1);
  ecoModeSelector.addItem("Manual", 2);
  polySidBudgetSelector.addItem("Hybrid", 1);
  polySidBudgetSelector.addItem("Ultra", 2);
  polySidBudgetSelector.addItem("Max ECO", 3);
  polyStereoAnchorSelector.addItem("Oldest", 1);
  polyStereoAnchorSelector.addItem("Newest", 2);
```

Attach each to `ecoMode`, `polySidBudget`, and `polyStereoAnchor`.

- [ ] **Step 3: Add clear status text**

The Settings popup must display:

```text
ECO Hybrid: one stereo anchor, added notes alternate SID engines.
```

When ECO is Off:

```text
ECO Off: current Ultra poly rendering is unchanged.
```

When Max ECO:

```text
Max ECO: each poly note uses one SID engine.
```

- [ ] **Step 4: Layout the button**

Place `settingsButton` in the top/dock row near the CPU readout. Keep it compact and consistent with
existing popup buttons. Do not move existing controls except for the minimum spacing needed.

- [ ] **Step 5: Build**

Run:

```powershell
cmake --build build --config Release --target Breadbin_Standalone BreadbinIntegrationTests -- /m:1 /v:minimal
```

Expected: build succeeds. Manual visual verification happens after implementation because this UI is
JUCE desktop, not covered by headless tests.

- [ ] **Step 6: Commit Settings popup**

Run:

```powershell
git -c safe.directory=D:/Code/breadbin add src/PluginEditor.h src/PluginEditor.cpp
git -c safe.directory=D:/Code/breadbin commit -m "feat: add performance settings popup"
```

---

### Task 6: Profile, WAV A/B, Docs, and Roadmap

**Files:**
- Modify: `D:\Code\breadbin\docs\OPTIMIZATION_TARGETS_2026-06-10.md`
- Modify: `D:\Code\breadbin\ROADMAP.md`
- Modify: `D:\Code\breadbin\CHANGELOG.md`
- Modify: `D:\Code\breadbin\STATE.md`
- Add: `D:\Code\breadbin\releases\cpu_after_eco_hybrid_2026-06-12.json`
- Add: `D:\Code\breadbin\releases\ab\eco_hybrid_2026-06-12\*.wav`

- [ ] **Step 1: Run full Release tests**

Run:

```powershell
cmake --build build --config Release --target BreadbinLFOTests BreadbinIntegrationTests BreadbinMutationTests -- /m:1 /v:minimal
.\build\Release\BreadbinLFOTests.exe
.\build\Release\BreadbinIntegrationTests.exe
.\build\Release\BreadbinMutationTests.exe
```

Expected:
- LFO: `484 passed, 0 failed`
- Integration: all tests pass
- Mutation: known adequate result, currently `17/18 killed`, process may exit `1`

- [ ] **Step 2: Run ECO-off CPU profile**

Run:

```powershell
.\build\Release\BreadbinIntegrationTests.exe --cpu-profile --json D:\Code\breadbin\releases\cpu_after_eco_params_off_2026-06-12.json
```

Expected: ECO-off profile is within normal run noise of `releases/cpu_after_poly_release_gate_2026-06-12.json`.

- [ ] **Step 3: Add ECO Hybrid profile scenario**

In `tests/IntegrationTests.cpp`, find the CPU profile scenario list in the `cpuprofile` namespace and
duplicate the existing S3 dense-poly scenario entry. Name the duplicate `s7-eco-hybrid-poly`, keep the
same MIDI/input pattern as S3, and add these setup lines after the copied S3 parameter setup:

```cpp
setParamReal(p, "ecoMode", 1.0f);
setParamReal(p, "polySidBudget", 0.0f);
setParamReal(p, "polyStereoAnchor", 0.0f);
```

The S7 description string should be:

```text
S7 ECO Hybrid: S3 dense poly with one stereo anchor and alternating mono SID notes
```

Expected: S3 remains the Ultra/current dense-poly scenario; S7 is the ECO Hybrid comparison using the
same musical workload.

- [ ] **Step 4: Run ECO Hybrid CPU profile**

Run:

```powershell
.\build\Release\BreadbinIntegrationTests.exe --cpu-profile --json D:\Code\breadbin\releases\cpu_after_eco_hybrid_2026-06-12.json
```

Target: S3/S7 dense-poly wall average below `6500 us`; document actual numbers even if target is
missed.

- [ ] **Step 5: Render and compare WAVs**

Render:

```powershell
.\build\Release\BreadbinIntegrationTests.exe --render-ab --out-dir D:\Code\breadbin\releases\ab\eco_hybrid_2026-06-12
```

Compare:
- ECO Off against `releases/ab/poly_release_gate_2026-06-12/` must pass as behavior-preserving.
- ECO Hybrid gets its own flagged listening summary, not a behavior-preserving pass/fail against
  Ultra.

- [ ] **Step 6: Update docs**

Update:
- `docs/OPTIMIZATION_TARGETS_2026-06-10.md` with ECO Hybrid before/after CPU numbers and listening
  flags.
- `ROADMAP.md` Phase 10 with Manual ECO Hybrid as active/in-progress and Auto ECO as future.
- `CHANGELOG.md` under Unreleased with the Settings popup and ECO Hybrid behavior.
- `STATE.md` with current artifacts and any residual risk.

- [ ] **Step 7: Commit measured ECO Hybrid result**

Run:

```powershell
git -c safe.directory=D:/Code/breadbin add docs/OPTIMIZATION_TARGETS_2026-06-10.md ROADMAP.md CHANGELOG.md STATE.md
git -c safe.directory=D:/Code/breadbin add -f releases/cpu_after_eco_hybrid_2026-06-12.json releases/ab/eco_hybrid_2026-06-12
git -c safe.directory=D:/Code/breadbin commit -m "docs: record ECO hybrid poly SID budget results"
```

---

## Self-Review

- Spec coverage: manual ECO, Hybrid, Ultra, Max ECO, Settings popup, oldest/newest anchor, ECO-off
  preservation, A/B policy, and roadmap integration each have a task.
- Placeholder scan: no `TBD`/`TODO` markers are present.
- Type consistency: parameter IDs are `ecoMode`, `polySidBudget`, `polyStereoAnchor`; enums are
  `EcoMode`, `PolySidBudget`, `PolyStereoAnchor`, `PolySidRenderRole`.

## Execution Handoff

Plan complete. Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` before
implementation. Do not implement until the user approves this plan and the anchor-promotion rule in
the design spec.
