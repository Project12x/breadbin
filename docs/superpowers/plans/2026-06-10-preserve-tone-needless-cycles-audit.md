# Preserve-Tone Needless-Cycles Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Release-safe audit counters that prove where Breadbin repeats no-op SID work before any preserve-tone optimization is implemented.

**Architecture:** Extend the shared CPU profile harness with optional per-scenario counter JSON callbacks. Add audit-only counters to `SIDEngine`, aggregate them through `BreadbinProcessor`, and emit them from `BreadbinIntegrationTests.exe --cpu-profile --json ...`. This first implementation slice must not skip writes or change audio behavior.

**Tech Stack:** C++20, JUCE 8, Breadbin `BreadbinIntegrationTests`, `gm::CpuSectionProfiler`, `ghostmoon_tools::CpuProfileHarness`, MSVC Release builds.

---

## File Map

- Modify: `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoongpl\dsp\include\ghostmoon_tools\CpuProfileHarness.h`
  - Adds optional reset/counter JSON callbacks to the shared GPL slice harness.
- Modify: `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoon-worktrees\breadbin-optimization-20260610\tools\include\ghostmoon_tools\CpuProfileHarness.h`
  - Mirrors the harness API change in the full first-party ghostmoon worktree.
- Modify: `D:\Code\breadbin\src\SIDEngine.h`
  - Defines `SIDEngine::PerfCounters` and counter controls.
- Modify: `D:\Code\breadbin\src\SIDEngine.cpp`
  - Increments counters and tracks same-value register writes without changing current write behavior.
- Modify: `D:\Code\breadbin\src\PluginProcessor.h`
  - Exposes audit reset/JSON methods and scenario-level aggregate counters.
- Modify: `D:\Code\breadbin\src\PluginProcessor.cpp`
  - Resets/aggregates SID counters and tracks active poly/note slot counts per profiled block.
- Modify: `D:\Code\breadbin\tests\IntegrationTests.cpp`
  - Wires counter callbacks into `cpuprofile::makeSpec()`.
- Modify: `D:\Code\breadbin\docs\OPTIMIZATION_TARGETS_2026-06-10.md`
  - Records audit evidence and promotes the first proven target.
- Modify: `D:\Code\breadbin\CHANGELOG.md`
  - Notes the diagnostic audit harness.
- Modify: `D:\Code\breadbin\STATE.md`
  - Notes the current audit artifact and next target.

---

### Task 1: Extend Shared CPU Profile Harness With Optional Counter JSON

**Files:**
- Modify: `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoongpl\dsp\include\ghostmoon_tools\CpuProfileHarness.h`
- Modify: `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoon-worktrees\breadbin-optimization-20260610\tools\include\ghostmoon_tools\CpuProfileHarness.h`

- [ ] **Step 1: Add counter callbacks to `CpuProfileSpec`**

In both harness headers, extend `CpuProfileSpec` with these members after `getProfiler`:

```cpp
  std::function<void(juce::AudioProcessor &)> resetCounters;
  std::function<std::string(juce::AudioProcessor &, int)> getCountersJson;
```

The second argument is `measureBlocks`, so plugin code can emit per-block averages.

- [ ] **Step 2: Reset counters after warmup and before measurement**

In `runCpuProfile`, after warmup and before `prof.reset()`, add:

```cpp
    if (spec.resetCounters)
      spec.resetCounters(*proc);
```

Expected local context:

```cpp
    for (int b = 0; b < spec.warmupBlocks; ++b)
      processProfileBlock(b);

    if (spec.resetCounters)
      spec.resetCounters(*proc);

    auto &prof = spec.getProfiler(*proc);
    prof.reset();
    prof.setEnabled(true);
```

- [ ] **Step 3: Append counter JSON per scenario**

In `runCpuProfile`, after writing the `sections` object for each scenario, append `counters` when the callback is provided:

```cpp
    json << " }";
    if (spec.getCountersJson) {
      const auto countersJson = spec.getCountersJson(*proc, spec.measureBlocks);
      if (!countersJson.empty())
        json << ", \"counters\": " << countersJson;
    }
    json << " }";
```

Replace the existing one-line close:

```cpp
    json << " } }";
```

- [ ] **Step 4: Commit shared harness changes**

Run:

```powershell
git -c safe.directory='C:/Users/estee/Desktop/My Stuff/Code/Antigravity/ghostmoongpl' add dsp/include/ghostmoon_tools/CpuProfileHarness.h
git -c safe.directory='C:/Users/estee/Desktop/My Stuff/Code/Antigravity/ghostmoongpl' commit -m "feat: allow CPU profile counter JSON"
git -c safe.directory='C:/Users/estee/Desktop/My Stuff/Code/Antigravity/ghostmoon-worktrees/breadbin-optimization-20260610' add tools/include/ghostmoon_tools/CpuProfileHarness.h
git -c safe.directory='C:/Users/estee/Desktop/My Stuff/Code/Antigravity/ghostmoon-worktrees/breadbin-optimization-20260610' commit -m "feat: allow CPU profile counter JSON"
```

Expected: both repos commit one harness header change and remain on `perf/breadbin-optimization-20260610`.

---

### Task 2: Add Audit-Only SIDEngine Counters

**Files:**
- Modify: `D:\Code\breadbin\src\SIDEngine.h`
- Modify: `D:\Code\breadbin\src\SIDEngine.cpp`

- [ ] **Step 1: Add `PerfCounters` and controls to `SIDEngine.h`**

In the public section of `SIDEngine`, after `float clock();`, add:

```cpp
  struct PerfCounters {
    uint64_t setFrequencyCalls = 0;
    uint64_t setFrequencySame = 0;
    uint64_t setPulseWidthCalls = 0;
    uint64_t setPulseWidthSame = 0;
    uint64_t setFilterCutoffCalls = 0;
    uint64_t setFilterCutoffSame = 0;
    uint64_t setFilterResonanceCalls = 0;
    uint64_t setFilterResonanceSame = 0;
    uint64_t setFilterModeCalls = 0;
    uint64_t setFilterModeSame = 0;
    uint64_t setFilterVoicesCalls = 0;
    uint64_t setFilterVoicesSame = 0;
    uint64_t writeRegisterCalls = 0;
    uint64_t writeRegisterSame = 0;
  };

  void setPerfCountersEnabled(bool enabled) noexcept {
    perfCountersEnabled = enabled;
  }
  void resetPerfCounters() noexcept { perfCounters = {}; }
  PerfCounters getPerfCounters() const noexcept { return perfCounters; }
```

In the private section, after `bool voicesMuted = false;`, add:

```cpp
  bool perfCountersEnabled = false;
  PerfCounters perfCounters;
  std::array<uint16_t, 3> frequencyRegs{};
  std::array<uint8_t, 32> registerCache{};
```

- [ ] **Step 2: Count frequency setter calls without changing writes**

In `SIDEngine::noteOn`, after computing `freq` and before writing registers, add:

```cpp
  frequencyRegs[voice] = freq;
```

In `SIDEngine::setFrequency`, after computing `freq` and before writing registers, add:

```cpp
  if (perfCountersEnabled) {
    ++perfCounters.setFrequencyCalls;
    if (frequencyRegs[voice] == freq)
      ++perfCounters.setFrequencySame;
  }
  frequencyRegs[voice] = freq;
```

Do not return early in this task. The audit commit must still write the registers exactly as before.

- [ ] **Step 3: Count pulse-width and filter setter no-ops**

At the top of `setPulseWidth`, after clamping the requested value into a local, use:

```cpp
  const auto next = static_cast<uint16_t>(std::clamp(pw, 0, 4095));
  if (perfCountersEnabled) {
    ++perfCounters.setPulseWidthCalls;
    if (voiceCache[voice].pulseWidth == next)
      ++perfCounters.setPulseWidthSame;
  }
  voiceCache[voice].pulseWidth = next;
```

Replace the existing direct assignment:

```cpp
  voiceCache[voice].pulseWidth = static_cast<uint16_t>(std::clamp(pw, 0, 4095));
```

For `setFilterCutoff`, use the same pattern:

```cpp
  const auto next = static_cast<uint16_t>(std::clamp(cutoff, 0, 2047));
  if (perfCountersEnabled) {
    ++perfCounters.setFilterCutoffCalls;
    if (filterCutoff == next)
      ++perfCounters.setFilterCutoffSame;
  }
  filterCutoff = next;
```

For `setFilterResonance`, use:

```cpp
  const auto next = static_cast<uint8_t>(std::clamp(resonance, 0, 15));
  if (perfCountersEnabled) {
    ++perfCounters.setFilterResonanceCalls;
    if (filterResonance == next)
      ++perfCounters.setFilterResonanceSame;
  }
  filterResonance = next;
```

- [ ] **Step 4: Count filter mode and voice-mask no-ops**

In `setFilterMode`, compute `nextMode` first:

```cpp
  uint8_t nextMode = 0;
  if (lowpass)
    nextMode |= 0x10;
  if (bandpass)
    nextMode |= 0x20;
  if (highpass)
    nextMode |= 0x40;
  if (perfCountersEnabled) {
    ++perfCounters.setFilterModeCalls;
    if (filterMode == nextMode)
      ++perfCounters.setFilterModeSame;
  }
  filterMode = nextMode;
```

In `setFilterVoices`, compute `nextMask` first:

```cpp
  uint8_t nextMask = 0;
  if (v1)
    nextMask |= 0x01;
  if (v2)
    nextMask |= 0x02;
  if (v3)
    nextMask |= 0x04;
  if (perfCountersEnabled) {
    ++perfCounters.setFilterVoicesCalls;
    if (filterVoiceMask == nextMask)
      ++perfCounters.setFilterVoicesSame;
  }
  filterVoiceMask = nextMask;
```

- [ ] **Step 5: Count low-level same-register writes**

Replace `SIDEngine::writeRegister` with:

```cpp
void SIDEngine::writeRegister(uint8_t reg, uint8_t value) {
  if (reg < registerCache.size()) {
    if (perfCountersEnabled) {
      ++perfCounters.writeRegisterCalls;
      if (registerCache[reg] == value)
        ++perfCounters.writeRegisterSame;
    }
    registerCache[reg] = value;
  } else if (perfCountersEnabled) {
    ++perfCounters.writeRegisterCalls;
  }
  sid->write(reg, value);
}
```

Expected behavior: no audio behavior changes. Every write still reaches `sid->write`.

---

### Task 3: Aggregate Counters in BreadbinProcessor

**Files:**
- Modify: `D:\Code\breadbin\src\PluginProcessor.h`
- Modify: `D:\Code\breadbin\src\PluginProcessor.cpp`

- [ ] **Step 1: Add public audit methods**

In `PluginProcessor.h`, near `getCpuProfiler()`, add:

```cpp
  void resetCpuAuditCounters();
  std::string getCpuAuditCountersJson(int measuredBlocks) const;
```

Ensure `PluginProcessor.h` includes `<string>` if it does not already.

- [ ] **Step 2: Add processor-level aggregate members**

In the private CPU/profiling member area of `PluginProcessor.h`, add:

```cpp
  struct CpuAuditCounters {
    uint64_t blocks = 0;
    uint64_t activePolyVoices = 0;
    uint64_t activePolyNoteSlots = 0;
  };
  CpuAuditCounters cpuAuditCounters;
```

- [ ] **Step 3: Enable and reset counters**

In `PluginProcessor.cpp`, implement:

```cpp
void BreadbinProcessor::resetCpuAuditCounters() {
  cpuAuditCounters = {};
  sidLeft.resetPerfCounters();
  sidRight.resetPerfCounters();
  sidLeft.setPerfCountersEnabled(true);
  sidRight.setPerfCountersEnabled(true);
  for (auto &pv : polyVoices) {
    pv.sidLeft->resetPerfCounters();
    pv.sidRight->resetPerfCounters();
    pv.sidLeft->setPerfCountersEnabled(true);
    pv.sidRight->setPerfCountersEnabled(true);
  }
}
```

- [ ] **Step 4: Count active poly state once per block**

In `processBlock`, after `polyMaxNotes` is synced and before MIDI handling mutates state, add:

```cpp
  if (cpuProfiler.isEnabled()) {
    ++cpuAuditCounters.blocks;
    for (int pi = 0; pi < polyMaxNotes; ++pi) {
      const auto &pv = polyVoices[pi];
      if (pv.active || pv.releasing) {
        ++cpuAuditCounters.activePolyVoices;
        cpuAuditCounters.activePolyNoteSlots +=
            (voiceMode == VoiceMode::PolyPara) ? static_cast<uint64_t>(pv.paraCount) : 1u;
      }
    }
  }
```

Expected: counts are only updated while the profiler is enabled during measured blocks.

- [ ] **Step 5: Sum SID counters and emit JSON**

Add helper-local summing in `PluginProcessor.cpp`:

```cpp
namespace {
static void addSidCounters(SIDEngine::PerfCounters &dst,
                           const SIDEngine::PerfCounters &src) {
  dst.setFrequencyCalls += src.setFrequencyCalls;
  dst.setFrequencySame += src.setFrequencySame;
  dst.setPulseWidthCalls += src.setPulseWidthCalls;
  dst.setPulseWidthSame += src.setPulseWidthSame;
  dst.setFilterCutoffCalls += src.setFilterCutoffCalls;
  dst.setFilterCutoffSame += src.setFilterCutoffSame;
  dst.setFilterResonanceCalls += src.setFilterResonanceCalls;
  dst.setFilterResonanceSame += src.setFilterResonanceSame;
  dst.setFilterModeCalls += src.setFilterModeCalls;
  dst.setFilterModeSame += src.setFilterModeSame;
  dst.setFilterVoicesCalls += src.setFilterVoicesCalls;
  dst.setFilterVoicesSame += src.setFilterVoicesSame;
  dst.writeRegisterCalls += src.writeRegisterCalls;
  dst.writeRegisterSame += src.writeRegisterSame;
}
}
```

Then implement:

```cpp
std::string BreadbinProcessor::getCpuAuditCountersJson(int measuredBlocks) const {
  SIDEngine::PerfCounters total;
  addSidCounters(total, sidLeft.getPerfCounters());
  addSidCounters(total, sidRight.getPerfCounters());
  for (const auto &pv : polyVoices) {
    addSidCounters(total, pv.sidLeft->getPerfCounters());
    addSidCounters(total, pv.sidRight->getPerfCounters());
  }

  const auto denom = measuredBlocks > 0 ? static_cast<double>(measuredBlocks) : 1.0;
  std::ostringstream json;
  json << "{"
       << "\"blocks\":" << cpuAuditCounters.blocks
       << ",\"activePolyVoicesAvg\":" << (cpuAuditCounters.activePolyVoices / denom)
       << ",\"activePolyNoteSlotsAvg\":" << (cpuAuditCounters.activePolyNoteSlots / denom)
       << ",\"setFrequencyCalls\":" << total.setFrequencyCalls
       << ",\"setFrequencySame\":" << total.setFrequencySame
       << ",\"setPulseWidthCalls\":" << total.setPulseWidthCalls
       << ",\"setPulseWidthSame\":" << total.setPulseWidthSame
       << ",\"setFilterCutoffCalls\":" << total.setFilterCutoffCalls
       << ",\"setFilterCutoffSame\":" << total.setFilterCutoffSame
       << ",\"setFilterResonanceCalls\":" << total.setFilterResonanceCalls
       << ",\"setFilterResonanceSame\":" << total.setFilterResonanceSame
       << ",\"setFilterModeCalls\":" << total.setFilterModeCalls
       << ",\"setFilterModeSame\":" << total.setFilterModeSame
       << ",\"setFilterVoicesCalls\":" << total.setFilterVoicesCalls
       << ",\"setFilterVoicesSame\":" << total.setFilterVoicesSame
       << ",\"writeRegisterCalls\":" << total.writeRegisterCalls
       << ",\"writeRegisterSame\":" << total.writeRegisterSame
       << "}";
  return json.str();
}
```

Add `#include <sstream>` to `PluginProcessor.cpp` if missing.

---

### Task 4: Wire Counters Into Breadbin CPU Profile Spec

**Files:**
- Modify: `D:\Code\breadbin\tests\IntegrationTests.cpp`

- [ ] **Step 1: Add reset callback**

In `cpuprofile::makeSpec()`, after `spec.getProfiler = ...;`, add:

```cpp
  spec.resetCounters = [](juce::AudioProcessor &p) {
    static_cast<BreadbinProcessor &>(p).resetCpuAuditCounters();
  };
```

- [ ] **Step 2: Add JSON callback**

Immediately after the reset callback, add:

```cpp
  spec.getCountersJson = [](juce::AudioProcessor &p, int measuredBlocks) {
    return static_cast<BreadbinProcessor &>(p).getCpuAuditCountersJson(measuredBlocks);
  };
```

- [ ] **Step 3: Build Release integration harness**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' 'build\BreadbinIntegrationTests.vcxproj' /p:Configuration=Release /p:BuildProjectReferences=false /m:1 /v:minimal
```

Expected: build succeeds. The existing MSB8028 intermediate-directory warning is acceptable.

- [ ] **Step 4: Run CPU profile and confirm `counters` JSON exists**

Run:

```powershell
.\build\Release\BreadbinIntegrationTests.exe --cpu-profile --json releases\cpu_audit_counters_2026-06-10.json
```

Expected: JSON contains `"counters"` for every S1-S5 scenario. Inspect with:

```powershell
Select-String -Path releases\cpu_audit_counters_2026-06-10.json -Pattern '"counters"'
```

---

### Task 5: Verify, Document, and Commit Audit Evidence

**Files:**
- Modify: `D:\Code\breadbin\docs\OPTIMIZATION_TARGETS_2026-06-10.md`
- Modify: `D:\Code\breadbin\CHANGELOG.md`
- Modify: `D:\Code\breadbin\STATE.md`
- Add: `D:\Code\breadbin\releases\cpu_audit_counters_2026-06-10.json`

- [ ] **Step 1: Run the Release test suite**

Run:

```powershell
.\build\Release\BreadbinLFOTests.exe
.\build\Release\BreadbinIntegrationTests.exe
.\build\Release\BreadbinMutationTests.exe
```

Expected:

- LFO: `484 passed, 0 failed`
- Integration: `409 passed, 0 failed`
- Mutation: `17/18 killed`, `5.6% survival`, `OK: Mutation coverage is adequate`

- [ ] **Step 2: Update the targets doc with audit findings**

In `docs/OPTIMIZATION_TARGETS_2026-06-10.md`, add a section named:

```markdown
## Preserve-Tone Audit Counters
```

Include a table with at least these columns:

```markdown
| Scenario | freq same/total | PW same/total | cutoff same/total | register same/total | active poly voices avg | active note slots avg |
```

Use values from `releases/cpu_audit_counters_2026-06-10.json`. After the table, add one short paragraph naming the first proven no-op target or stating that no behavior-preserving target is proven yet.

- [ ] **Step 3: Update changelog and state**

In `CHANGELOG.md` under `[Unreleased] / Added`, add:

```markdown
- **Preserve-tone CPU audit counters**: the headless CPU profile JSON now includes SID wrapper setter and register-write counters so no-op work can be targeted before any quality or gating changes.
```

In `STATE.md` under the CPU profiling section, add:

```markdown
- **Preserve-tone audit counters**: `releases/cpu_audit_counters_2026-06-10.json` records SID setter/register no-op ratios for the full S1-S5 matrix. The next optimization target is selected from this evidence.
```

- [ ] **Step 4: Commit the audit**

Run:

```powershell
git -c safe.directory=D:/Code/breadbin add src/SIDEngine.h src/SIDEngine.cpp src/PluginProcessor.h src/PluginProcessor.cpp tests/IntegrationTests.cpp CHANGELOG.md STATE.md docs/OPTIMIZATION_TARGETS_2026-06-10.md
git -c safe.directory=D:/Code/breadbin add -f releases\cpu_audit_counters_2026-06-10.json
git -c safe.directory=D:/Code/breadbin commit -m "perf: add preserve-tone SID audit counters" -m "Release verification: LFO 484 passed; Integration 409 passed; Mutation 17/18 killed with one documented survivor. CPU audit artifact: releases/cpu_audit_counters_2026-06-10.json."
```

Expected: one Breadbin commit with code, docs, and the audit JSON. No WAV A/B is required for this audit-only commit because it must not change audio behavior.

---

## Post-Plan Decision Gate

After Task 5, stop and inspect the counter ratios before choosing the first optimization commit.

Promote a target only if the audit shows a high no-op ratio in an active scenario:

- same-value `writeRegister` or setter calls above 50% in S2/S3: prioritize same-value guards.
- high `setFrequencySame` in S3: prioritize frequency register fast paths and note-frequency cache.
- low no-op ratios but high `SIDClock` section after later subdivision: document active reSIDfp emulation as sound cost or propose a user-approved quality mode in a separate spec.

Do not implement a fix in the audit commit.
