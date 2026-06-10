# Optimization Targets - pinned 2026-06-10

*Derived from the first Breadbin profiling pass: section tables
(`BreadbinIntegrationTests.exe --cpu-profile`, Release, 3 scenarios). Baseline
artifacts: `releases/cpu_baseline_2026-06-10.json` and
`releases/ab/88fa9f6fbf6c/*.wav`. T1 artifact:
`releases/cpu_after_t1_2026-06-10.json`. ETW drill-down was attempted, but
collection requires an elevated Administrator shell; no function-level
acceptance criterion is marked landed until that pass is captured.*

The sequence is in progress on branch `polish/ui-2026-06-05`. Breadbin consumes
the GPL-compatible `ghostmoon-oss` slice at
`C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoongpl`; fixes should
land there first when the primitive is shared. The current first-order hotspot is
plugin-level reSIDfp clocking, so the first behavioral target lands in Breadbin
around SID render activity gating rather than in JUCE or vendored reSIDfp.

## Reference and Reuse Record

| Source | Commit | License | Files inspected | Reuse mode |
|---|---|---|---|---|
| `D:\Code\Moonglow` | `c31a2c38ea56d0f72a1d8bb79243d62c0ea41569` | Project-local first-party | `Source/PluginProcessor.cpp`, `Source/PluginProcessor.h`, `tests/HeadlessIntegrationTest.cpp`, `docs/OPTIMIZATION_TARGETS_2026-06.md`, `docs/superpowers/plans/2026-06-10-moonglow-cpu-optimization-sequence.md` | pattern-only, plus clean direct `gm::SilenceGate` API implementation from the plan |
| `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoon` | `f521679b8bd0946bc48b0941c45945e1c65d2369` | first-party MIT for copied utility headers | `dsp/include/ghostmoon/CpuSectionProfiler.h`, `tools/include/ghostmoon_tools/CpuProfileHarness.h` | direct-copy into `ghostmoongpl` with source-path notes |

No JUCE or vendored reSIDfp source was modified.

## Baseline Section Profile

| Scenario | wall avg | % of 10.67 ms budget | top section |
|---|---:|---:|---:|
| idle-default | 1818 us | 17.04% | `SIDRender` 1787 us |
| typical-playing | 1822 us | 17.08% | `SIDRender` 1782 us |
| full-stack | 4068 us | 38.14% | `SIDRender` 3946 us |

Top measured sections:

| Scenario | section | avg | share of measured section time |
|---|---|---:|---:|
| idle-default | `SIDRender` | 1787 us | 98.3% |
| idle-default | `Limiter` | 14.1 us | 0.8% |
| idle-default | `SafetyFilters` | 6.9 us | 0.4% |
| typical-playing | `SIDRender` | 1782 us | 97.8% |
| typical-playing | `Limiter` | 23.2 us | 1.3% |
| typical-playing | `SafetyFilters` | 6.9 us | 0.4% |
| full-stack | `SIDRender` | 3946 us | 97.0% |
| full-stack | `Reverb` | 39.9 us | 1.0% |
| full-stack | `Chorus` | 24.2 us | 0.6% |
| full-stack | `Limiter` | 14.6 us | 0.4% |
| full-stack | `Delay` | 13.5 us | 0.3% |

## T1 Result

| Scenario | before wall | after T1 wall | delta | before `SIDRender` | after T1 `SIDRender` |
|---|---:|---:|---:|---:|---:|
| idle-default | 1818 us | 24.5 us | -1793.5 us (-98.7%) | 1787 us | 0.09 us |
| typical-playing | 1822 us | 1650 us | -172 us (-9.4%) | 1782 us | 1614 us |
| full-stack | 4068 us | 2987 us | -1081 us (-26.6%) | 3946 us | 2897 us |

T1 passed the automatic WAV A/B gate:

| Scenario | diff peak | diff RMS | output RMS delta | centroid delta | result |
|---|---:|---:|---:|---:|---|
| idle-default | -72.25 dBFS | -103.38 dBFS | -0.00 dB | 0.90% | pass |
| typical-playing | -57.64 dBFS | -74.49 dBFS | 0.00 dB | 0.00% | pass |
| full-stack | -68.03 dBFS | -88.80 dBFS | 0.00 dB | 0.00% | pass |

Flagged pairs for listening: none.

## Function-Level Drill-Down

Status: blocked in this shell.

- RelWithDebInfo executable rebuilt at
  `build\RelWithDebInfo\BreadbinIntegrationTests.exe`.
- `ghostmoon/tools/perf/profile-hotspots.ps1` exited before capture because ETW
  CPU sampling requires an elevated Administrator shell.
- Required follow-up: rerun from elevated PowerShell and keep
  `releases/etw/first_drilldown.{etl,json}`. Use `D:\SymCache` for symbols.

## T1 - Silence-aware SID render skip for fully idle output - **landed**

**Evidence:** `SIDRender` is 1787 us at idle and 1782 us in the typical-playing
profile, dominating both near-silent and active scenarios. The idle scenario has
no MIDI, no SID file, no digi playback, no external input, and no active voice
source, yet both SID chips are still clocked for every sample. This is the only
target with enough section-level evidence to justify the first behavioral change
without ETW.

**Result:** `ghostmoongpl` now exposes a small first-party `gm::SilenceGate`
primitive. Breadbin wires it around the mono/paraphonic SID render path: the gate
uses explicit source activity plus the previously rendered SID output peak, waits
for eight silent blocks, then clears the output block and resets wrapper-side SID
runtime buffers. It does not reset reSIDfp oscillator/filter internals, preserving
chip state for the next note-on.

**Landing area:** generic gate math in `ghostmoongpl`; SID source/tail policy in
Breadbin plugin code.

**Effort:** medium. The risk was audible tail truncation or changed oscillator
phase after long idle.

**Acceptance criteria:**

- `idle-default` wall average drops by at least 60% and `SIDRender` drops by at
  least 70%. Met: wall -98.7%, `SIDRender` 1787 us -> 0.09 us.
- `typical-playing` and `full-stack` do not regress by more than 5%. Met in the
  captured T1 profile: typical -9.4%, full-stack -26.6%.
- Phase 4 WAV A/B passes automatically where `diff RMS <= -45 dBFS` and
  spectral-centroid delta is below 2%. Met for all three reference pairs.
- No output goes silent while a source is active; no NaN/Inf/click evidence. Met
  by integration coverage plus A/B metrics.

## T2 - Inactive poly SID voice gating - **planned after T1**

**Evidence:** `full-stack` spends 3946 us in `SIDRender` while exercising
polyphonic and paraphonic voice modes. This is roughly 2.2x the mono scenarios,
consistent with per-poly-voice SID pairs being clocked even after voices have
finished audibly contributing.

**Planned fix:** after T1 proves the output-energy gate policy, extend the same
principle to inactive poly voices: gate only on each voice pair's rendered output
energy after note-off, preserve long tails, and reactivate on allocation or
trigger. The established `gm::KsVoice` activity-gating pattern is the reference
behavioral shape, but implementation should remain SID-specific unless a clean
shared primitive emerges.

**Landing area:** Breadbin plugin code, with any generic helper considered for
`ghostmoongpl`.

**Effort:** medium-high. More A/B risk than T1 because oscillator phase and
voice stealing interact with allocation state.

**Acceptance criteria:**

- `full-stack` wall average drops by at least 20% after T1's baseline.
- Mono `typical-playing` remains within 5%.
- WAV A/B policy from Phase 4 passes or flags only explainable note-tail pairs.

## T3 - Safety-chain micro-costs - **defer**

**Evidence:** the whole safety chain is small compared with SID render cost:
idle `SafetyFilters` 6.9 us, `Limiter` 14.1 us, `NoiseGate` 5.3 us; full-stack
`SafetyFilters` 7.7 us, `Limiter` 14.6 us, `NoiseGate` 5.5 us. These sections
are useful protection and are below 2% combined in every scenario.

**Planned fix:** none in this sequence unless ETW shows a surprising
function-level issue after T1/T2. The existing output noise gate is part of
Breadbin's sound and release safety story.

**Landing area:** not targeted.

**Effort:** low if revisited, but low value.

**Acceptance criteria:** not applicable until promoted to an active target.

## Non-Targets

- Active reSIDfp SID emulation while audible voices are sounding. This is the
  instrument's core sound, not waste.
- `Reverb`, `Chorus`, and `Delay` in `full-stack`. Together they are under 2%
  of measured section time and are only meaningful when enabled.
- Per-block parameter sync, LFO, modulation, arpeggiator, and wavetable sections.
  Each is below 8 us in the worst scenario, and most are below 3 us.
- JUCE, libsidplayfp, and reSIDfp source modifications.

## Verification Plan

1. Keep Phase 0 WAVs under `releases/ab/88fa9f6fbf6c/` as the ground truth.
2. Build Release before every profile; never use Debug profile numbers.
3. Run `BreadbinIntegrationTests.exe --cpu-profile --json releases\cpu_after_<date>.json`.
4. Re-render the same WAV scenario set at HEAD with identical settings.
5. Compute peak/RMS diff, output RMS delta, and rough spectral-centroid delta for
   each pair. Stop on silence, NaN/Inf, clicks, sustained >1.5 dB level shifts,
   or `diff RMS > -25 dBFS`.
6. Capture ETW from an elevated shell before marking function-level criteria
   landed.
