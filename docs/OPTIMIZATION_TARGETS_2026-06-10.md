# Optimization Targets - pinned 2026-06-10

*Derived from Breadbin Release section profiling. The first pass used the
Moonglow-style three-scenario harness and landed T1. The current full
S1-S5 matrix baseline for the remaining targets is
`releases/cpu_baseline_matrix_2026-06-10.json`, with deterministic WAV
references under `releases/ab/4c07888/`. ETW drill-down was attempted, but
collection requires an elevated Administrator shell; no function-level
acceptance criterion is marked landed until that pass is captured.*

The sequence is in progress on branch `perf/breadbin-optimization-20260610`.
Breadbin consumes
the GPL-compatible `ghostmoon-oss` slice at
`C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoongpl`; fixes should
land there first when the primitive is shared. The current first-order hotspot is
plugin-level reSIDfp clocking, so the first behavioral target lands in Breadbin
around SID render activity gating rather than in JUCE or vendored reSIDfp.

Phase -1 branch setup:

- Breadbin: `perf/breadbin-optimization-20260610`
- `ghostmoongpl`: `perf/breadbin-optimization-20260610`
- full ghostmoon worktree:
  `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoon-worktrees\breadbin-optimization-20260610`
  on `perf/breadbin-optimization-20260610`

## Reference and Reuse Record

| Source | Commit | License | Files inspected | Reuse mode |
|---|---|---|---|---|
| `D:\Code\Moonglow` | `c31a2c38ea56d0f72a1d8bb79243d62c0ea41569` | Project-local first-party | `Source/PluginProcessor.cpp`, `Source/PluginProcessor.h`, `tests/HeadlessIntegrationTest.cpp`, `docs/OPTIMIZATION_TARGETS_2026-06.md`, `docs/superpowers/plans/2026-06-10-moonglow-cpu-optimization-sequence.md` | pattern-only, plus clean direct `gm::SilenceGate` API implementation from the plan |
| `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoon` | `f521679b8bd0946bc48b0941c45945e1c65d2369` | first-party MIT for copied utility headers | `dsp/include/ghostmoon/CpuSectionProfiler.h`, `tools/include/ghostmoon_tools/CpuProfileHarness.h` | direct-copy into `ghostmoongpl` with source-path notes |

No JUCE or vendored reSIDfp source was modified.

## Full S1-S5 Matrix Baseline

Reference render commit: `4c07888` (`test: expand performance scenario matrix`).
Artifacts: `releases/ab/4c07888/*.wav` and
`releases/cpu_baseline_matrix_2026-06-10.json`. T2 adds a deterministic
S6 digi `$D418` playback scenario; its pre-optimization S1-S6 reference set is
`releases/ab/1931533/`.

| Scenario | purpose | wall avg | wall max note | top section |
|---|---|---:|---:|---:|
| S1 `s1-idle-default` | idle/always-on defaults | 24.8 us | below budget | `Limiter` 12.8 us; `SIDRender` 0.10 us |
| S2 `s2-typical-playing` | moderate 4-voice polyphony | 5646.8 us | `SIDRender` max 35600 us | `SIDRender` 5589.98 us |
| S3 `s3-worst-case` | max poly+para, modulation, all FX | 13412.6 us | `SIDRender` max 58603.7 us | `SIDRender` 13285.25 us |
| S4 `s4-decay-to-silence` | dense burst then long release | 1886.2 us | below budget | `SIDRender` 1807.72 us |
| S5 `s5-input-sweep` | external input log sweep through FX | 1732.2 us | below budget | `SIDRender` 1633.08 us |
| S5 `s5-input-pink-burst` | external input pink burst + silence | 1543.5 us | below budget | `SIDRender` 1455.43 us |

S3 exceeds the 512-sample block budget at 48 kHz: 13.41 ms average versus a
10.67 ms budget. `SIDRender` is 99.1% of measured section time, so the next
target remains SID render activity rather than FX or safety-chain work.

## GPU Lane

Phase 1b GPU profiling is not applicable for this pass. Breadbin has no
WebGPU/Dawn-rendered visual path; the plugin UI is JUCE-rendered and this
optimization sequence is DSP CPU-bound.

## Initial Three-Scenario Baseline

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

## Preserve-Tone Audit Counters

Artifact: `releases/cpu_audit_counters_2026-06-10.json`, generated from the
Release `BreadbinIntegrationTests.exe --cpu-profile` harness after wiring
diagnostic-only SID setter/register counters. The counters do not skip writes;
they only measure repeated work. The artifact also includes wall/section timing
from that run, but `releases/cpu_baseline_matrix_2026-06-10.json` remains the
pinned timing baseline until the next full rebaseline; use the audit artifact for
counter ratios and provenance, not as a replacement timing baseline.

| Scenario | freq same/total | PW same/total | cutoff same/total | register same/total | active poly voices avg | active note slots avg |
|---|---:|---:|---:|---:|---:|---:|
| S1 `s1-idle-default` | 0/0 | 0/0 | 4000/4000 | 32000/32000 | 0.000 | 0.000 |
| S2 `s2-typical-playing` | 32556/32556 | 65112/65112 | 25704/25704 | 976700/977276 | 2.701 | 2.701 |
| S3 `s3-worst-case` | 4018/77428 | 1032/171792 | 368/65264 | 2003950/2537700 | 7.093 | 1.000 |
| S4 `s4-decay-to-silence` | 0/0 | 0/0 | 4000/4000 | 48000/48000 | 0.000 | 0.000 |
| S5 `s5-input-sweep` | 0/0 | 0/0 | 4000/4000 | 48000/48000 | 0.000 | 0.000 |
| S5 `s5-input-pink-burst` | 0/0 | 0/0 | 4000/4000 | 48000/48000 | 0.000 | 0.000 |

First proven no-op target: guard same-value SID register writes at the
`SIDEngine::writeRegister` boundary, with wrapper-level setter guards where the
same-value ratios are also high. This is a preserve-tone Breadbin SID-wrapper
optimization because the repeated work is plugin-specific reSIDfp register I/O;
if a reusable guarded-write primitive emerges during the implementation, it
should land in `ghostmoongpl`/full `ghostmoon` first and then be consumed here.

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

## T1b - Exact pitch-ratio hoists outside SID clocking - **landed**

**Evidence:** after T1, the dominant cost is still active/releasing SID clocking,
but S3 also spends measurable time in non-gating block-rate work:
`PolyMod` 4.24 us and `Modulation` 1.61 us in
`releases/cpu_pre_no_behavior_2026-06-11.json`. These sections are small, so
the target is limited to exact arithmetic cleanup outside the SID state machine.

**Result:** hoisted block-constant pitch-bend and pitch-modulation
`std::pow(2, semitones / 12)` calculations out of active-voice loops. The same
double formula feeds the same `setFrequency` calls in the same order. No
polyphony allocation, voice activity, gating, SID register sequencing, digi
`$D418` writes, or reset semantics changed.

**Landing area:** Breadbin `PluginProcessor.cpp`. Shared Ghostmoon was checked:
full `ghostmoon` has broader tuning helpers, but the GPL-compatible slice does
not currently expose a small exact helper. A shared approximation/LUT was not
used because that would be a tone-touching change.

**Effort:** low. The expected win is small and section-local; `SIDRender`
remains the real bottleneck.

**Before/after:** `releases/cpu_pre_no_behavior_2026-06-11.json` ->
`releases/cpu_after_exact_hoists_2026-06-11.json`.

| Scenario | wall before | wall after | `Modulation` before -> after | `PolyMod` before -> after |
|---|---:|---:|---:|---:|
| S2 `s2-typical-playing` | 3.76e+03 us | 3.81e+03 us | 0.68 -> 0.65 us | 1.07 -> 1.16 us |
| S3 `s3-worst-case` | 9.96e+03 us | 1.01e+04 us | 1.61 -> 1.49 us | 4.24 -> 3.92 us |

The S3 section-local win is measurable (`PolyMod` -7.5%, `Modulation` -7.5%),
but total wall time is dominated by run-to-run `SIDRender` variance. This does
not satisfy the main T2 optimization goal.

**WAV A/B:** rendered to `releases/ab/exact_hoists_2026-06-11/` and compared
against the S1-S6 pre-T2 references in `releases/ab/1931533/`.

| Scenario | diff peak | diff RMS | output RMS delta | centroid delta | result |
|---|---:|---:|---:|---:|---|
| S1 idle | -71.22 dBFS | -103.03 dBFS | -0.0012 dB | 0.033% | pass |
| S2 typical | -55.04 dBFS | -76.71 dBFS | +0.0001 dB | 0.004% | pass |
| S3 worst | -65.70 dBFS | -84.88 dBFS | +0.0011 dB | 0.005% | pass |
| S4 decay | -49.48 dBFS | -86.10 dBFS | +0.0004 dB | 0.012% | pass |
| S5 sweep | -69.48 dBFS | -85.22 dBFS | +0.0004 dB | 0.000% | pass |
| S5 pink burst | -67.39 dBFS | -88.22 dBFS | +0.0006 dB | 0.000% | pass |
| S6 digi 4-bit | -55.04 dBFS | -73.02 dBFS | +0.0056 dB | 0.010% | pass |

S5 input WAVs are bit-identical. Flagged pairs for listening: none.

## T2 - Same-value SID register/write guards - **investigated/rejected**

**Evidence:** the preserve-tone audit counters show repeated no-op SID writes in
active scenarios before any further gating. S2 typical playing has
976700/977276 same-value register writes, with all measured wrapper setter calls
same-value (`setFrequency`, `setPulseWidth`, cutoff, resonance, mode, and voice
mask). S3 worst case still has 2003950/2537700 same-value register writes while
averaging 7.093 active/releasing poly voices, so the target is not merely idle
silence. The pinned timing baseline remains `SIDRender` dominated: S2
5589.98 us and S3 13285.25 us.

**Planned fix:** add behavior-preserving same-value guards around SID wrapper
setter and register write paths so unchanged values do not re-enter reSIDfp.
Keep the first implementation narrow and measured: no voice-count reduction, no
quality mode, no silence gate expansion. This requires explicit approval before
implementation because reSIDfp updates internal bus TTL/sync side effects even
for same-value writes. If the guard helper can be expressed as a reusable
primitive, land it in `ghostmoongpl` and full `ghostmoon` first, then consume it
in Breadbin.

**Landing area:** likely Breadbin `SIDEngine` wrapper first; promote any generic
guard primitive to `ghostmoongpl`/full `ghostmoon` before plugin consumption.

**Effort:** medium. The risk is subtle behavior dependence on repeated
same-value register writes inside reSIDfp, so the change requires full matrix
profiling plus WAV A/B even though the intended output should be unchanged.

**Acceptance criteria:**

- S2 and S3 same-value register-write counts drop by at least 50% in the audit
  counters after the fix, with no missing non-same writes.
- S2 and S3 wall averages do not regress by more than 5%; target a measurable
  `SIDRender` drop in S2 and a smaller but positive S3 drop.
- S1, S4, and both S5 scenarios do not regress by more than 5% and do not gain
  reproducible max spikes.
- S6 `s6-digi-4bit` remains audible, does not lose `$D418` volume transitions,
  and passes WAV A/B against `releases/ab/1931533/s6-digi-4bit.wav`.
- Full Release tests remain green; mutation coverage remains adequate.
- Full Phase 4 WAV A/B passes automatically or flags only explainable pairs.

**2026-06-12 result:** rejected for now. Three variants were measured and none
met the acceptance criteria:

| Variant | Artifact | Key result |
|---|---|---|
| `SIDEngine::writeRegister` same-value elision plus setter guards | `releases/cpu_after_t2_register_guards_2026-06-12.json`, rerun `releases/cpu_t2_rerun_2026-06-12.json` | same-value write counts dropped, but active `SIDRender` regressed heavily (`S2` 3770 -> 9195 us on rerun, `S3` 9990 -> 22451 us) |
| setter-only guards preserving `SID::write()` when setters write | `releases/cpu_t2_setter_only_2026-06-12.json` | still regressed (`S2` 3770 -> 5097 us, `S3` 9990 -> 13553 us) |
| wrapper block render / inline-call experiments outside register writes | `releases/cpu_after_sid_block_render_2026-06-12.json`, `releases/cpu_after_inline_sid_clock_2026-06-12.json` | did not beat the pinned exact-hoist baseline |

Root cause evidence from upstream reSIDfp (`build/_deps/libsidplayfp-src/src/builders/residfp-builder/residfp/SID.cpp`):
`SID::write()` refreshes bus TTL and calls `voiceSync(false)` even when the
incoming register value is unchanged. Eliding those calls is therefore not a
pure no-op at the chip-emulation boundary. Diagnostic WAVs for the broad guard
were rendered to `releases/ab/t2_failed_diagnostic_2026-06-12/`; spot checks
against `releases/ab/exact_hoists_2026-06-11/` were effectively identical
(idle diff RMS -102.87 dBFS, decay -87.01 dBFS, sweep -85.25 dBFS), but CPU
acceptance failed, so no source change landed.

T2 remains closed unless we deliberately design a reSIDfp-aware fork/API that
preserves the required bus/voice-sync side effects while reducing redundant
internal register work. That would be a separate behavior-adjacent target and
needs approval before implementation.

## T3 - Inactive poly SID voice gating - **candidate / flagged for listen**

**Evidence:** after exact pitch-ratio hoists, S3 worst case still spends
9990 us in `SIDRender` with 10100 us wall average, while the block budget is
10666.7 us. S2 typical playing spends 3770 us in `SIDRender`. Preserve-tone
counters also show S3 averaging 7.093 retained poly voices but only 1 active
poly note slot, so release-retained poly SID pairs are the largest remaining
measurable waste before changing reSIDfp internals.

**Implemented candidate:** each poly voice owns a `gm::SilenceGate` that is
updated from that voice pair's rendered post-fade output peak while the voice is
`releasing`. When the gate is closed, the voice remains allocated and its
`fadeGain` continues to advance, but the paired reSIDfp `clock()` calls are
skipped until the slot is reused. Allocation, stealing, and active polyphony
counts are unchanged; note-on/all-notes-off/free paths reset the gate state.

**Landing area:** Breadbin plugin code using the existing shared
`gm::SilenceGate` primitive.

**Effort:** medium-high. This is behavior-adjacent because release-tail samples
can change after output energy falls below the gate threshold.

**CPU result:** final candidate profile
`releases/cpu_after_poly_release_gate_2026-06-12.json` recorded
`polySidRenderSkipBlocks=2695` in S3 and moved S3 wall/SIDRender
10100/9990 us -> 9264.5/9150.75 us, bringing the scenario from 94.7% to 86.85%
of the 512-sample block budget. S2/S4/S5/S6 were neutral to slower from run
noise and added gate bookkeeping. Treat this as an S3-targeted headroom
candidate, not a broad matrix win.

**WAV A/B:** rendered to
`releases/ab/poly_release_gate_2026-06-12/` against
`releases/ab/exact_hoists_2026-06-11/`.

| Scenario | Diff RMS | Peak diff | Output RMS delta | Centroid delta | Policy |
|---|---:|---:|---:|---:|---|
| S1 idle | -103.21 dBFS | -73.41 dBFS | +0.002 dB | 1.257% | pass |
| S2 typical | -77.44 dBFS | -54.89 dBFS | -0.000 dB | 0.001% | pass |
| S3 worst case | -30.66 dBFS | -12.26 dBFS | -0.018 dB | 1.131% | flagged |
| S4 decay | -86.45 dBFS | -55.04 dBFS | -0.000 dB | 0.028% | pass |
| S5 sweep | -85.36 dBFS | -69.48 dBFS | -0.000 dB | 0.075% | pass |
| S5 pink burst | -88.36 dBFS | -67.39 dBFS | -0.001 dB | 0.002% | pass |
| S6 digi | -74.02 dBFS | -57.05 dBFS | -0.001 dB | 0.024% | pass |

S3 is explainable: the optimization intentionally stops clocking released poly
SID pairs once their own output tail has fallen below the gate threshold, so
the difference is concentrated in the worst-case release-heavy scenario. It is
inside the policy's `-45` to `-25 dBFS` flagged band, not an automatic pass.
Flagged pair for user listening before merge:
`releases/ab/exact_hoists_2026-06-11/s3-worst-case.wav` vs
`releases/ab/poly_release_gate_2026-06-12/s3-worst-case.wav`.

**Acceptance criteria:** keep only if full Release verification remains green
and the flagged S3 pair is audibly acceptable to the user. Revert or retune if
the listen check finds tail truncation, clicks, or unacceptable tone loss.

## T3b - Manual ECO Hybrid poly SID budget - **planned**

**Evidence:** after the T3 candidate, S3 worst case is technically under the
512-sample block budget but still close: wall average 9264.5 us, `SIDRender`
9150.75 us, 86.85% of budget at 48 kHz / 512. Musician-feel discussion also
showed that the typical path is comfortable, but worst case is not a fast-feel
latency target. The largest remaining musical lever is reducing dense-poly SID
engine count without hiding the dual-SID identity of the patch.

**Planned fix:** add explicit Manual ECO settings with a `Poly SID Budget`.
Hybrid keeps one stereo anchor note as a full L/R SID pair, while added poly
notes clock one SID engine each and alternate L/R character. Current behavior
becomes `Ultra` and remains the ECO-off/default path. `Max ECO` is planned as a
later one-SID-per-note option. The full design is
`docs/superpowers/specs/2026-06-12-eco-poly-sid-budget-design.md`; the
implementation plan is
`docs/superpowers/plans/2026-06-12-eco-poly-sid-budget.md`.

**Landing area:** Breadbin plugin code and UI. No shared-code requirement for
the first slice because this is product-specific voice allocation/render policy.
The later reSIDfp fork/API path remains a separate shared or vendored-fork
decision.

**Effort:** high. This intentionally changes ECO-on poly render behavior and
touches allocation, modulation sync, poly render mixing, UI, presets/state, and
profiling scenarios.

**Acceptance criteria:**
- ECO Off preserves current Ultra behavior in tests and WAV A/B.
- ECO Hybrid has a visible Settings control and status text; no hidden automatic
  behavior switching.
- S3 or an equivalent dense-poly ECO scenario targets wall average below
  6500 us at 48 kHz / 512; actual results must be documented even if the target
  is missed.
- ECO Hybrid render gets its own WAV references and listening flags; it is not
  treated as a behavior-preserving replacement for Ultra.
- Existing Ultra/current behavior remains available for patches that rely on
  full dual-SID-per-note stereo spread.

## T4 - Safety-chain micro-costs - **defer**

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
- Further per-block parameter sync, LFO, modulation, arpeggiator, and wavetable
  work after T1b. Each remaining section is small; revisit only if ETW or a new
  section table shows a concrete issue.
- JUCE, libsidplayfp, and reSIDfp source modifications.

## Verification Plan

1. Keep Phase 0 WAVs under `releases/ab/4c07888/` as the full S1-S5 matrix
   ground truth. For T2, use the expanded S1-S6 reference set under
   `releases/ab/1931533/`, which adds deterministic 4-bit digi `$D418`
   playback. The earlier 3-scenario T1 references remain under
   `releases/ab/88fa9f6fbf6c/`.
2. Build Release before every profile; never use Debug profile numbers.
3. Run `BreadbinIntegrationTests.exe --cpu-profile --json releases\cpu_after_<date>.json`.
4. Re-render the same WAV scenario set at HEAD with identical settings.
5. Compute peak/RMS diff, output RMS delta, and rough spectral-centroid delta for
   each pair. Stop on silence, NaN/Inf, clicks, sustained >1.5 dB level shifts,
   or `diff RMS > -25 dBFS`.
6. Capture ETW from an elevated shell before marking function-level criteria
   landed.
