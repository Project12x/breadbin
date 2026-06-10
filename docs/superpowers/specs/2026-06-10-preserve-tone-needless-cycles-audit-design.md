# Breadbin - Preserve-Tone Needless-Cycles Audit

**Date**: 2026-06-10
**Status**: Pending user review
**Branch**: `perf/breadbin-optimization-20260610`
**Builds on**: T1 idle SID render gate and the full S1-S5 performance matrix
(`releases/cpu_baseline_matrix_2026-06-10.json`, `releases/ab/4c07888/`).

## Goal

Find and remove needless CPU work in Breadbin's active DSP path while preserving the current
maximum sound: same reSIDfp emulation quality, same 8-voice Poly+Para ceiling, same active note
behavior, and no silent-tail truncation changes in this pass.

## Decisions

1. **Preserve tone first.** S3 worst case may remain expensive if the cost is truly active SID
   emulation. This pass must not reduce active voice count, lower resampling quality, bypass filters,
   or introduce a "lite" mode.
2. **Audit before optimizing.** The next work item is a needless-cycles audit with cheap counters
   and focused section timing. Do not assume the next win is gating or `SIDRender` subdivision.
3. **Fix only proven no-op work.** Acceptable fixes are behavior-preserving by construction:
   skipping redundant register writes, hoisting block-constant math, caching note/frequency
   conversions, and avoiding disabled-feature loops.
4. **Gating is deferred.** Tail/silent voice gating remains a later target for S4/S5 if active-path
   no-op work is not enough. It is not the first implementation target in this spec.
5. **No vendored edits.** reSIDfp, JUCE, and other third-party dependencies remain unchanged.

## Evidence

The current matrix baseline shows S3 over the 512-sample budget:

| Scenario | wall avg | top section |
|---|---:|---|
| S1 `s1-idle-default` | 24.8 us | `Limiter` 12.8 us; `SIDRender` 0.10 us |
| S2 `s2-typical-playing` | 5646.8 us | `SIDRender` 5589.98 us |
| S3 `s3-worst-case` | 13412.6 us | `SIDRender` 13285.25 us |
| S4 `s4-decay-to-silence` | 1886.2 us | `SIDRender` 1807.72 us |
| S5 `s5-input-sweep` | 1732.2 us | `SIDRender` 1633.08 us |
| S5 `s5-input-pink-burst` | 1543.5 us | `SIDRender` 1455.43 us |

The existing section table attributes nearly all S2/S3 cost to `SIDRender`, but that label currently
covers wrapper work, poly mixing, gain smoothing, reSIDfp clocking, DC tracking, external input
feeding, and output writes. It does not prove that all cycles are irreducible emulation.

Static inspection found plausible no-op or repeated-work candidates:

- `processBlock` applies poly settings to every active/releasing poly voice every block.
- Poly modulation writes filter cutoff, pulse width, and frequency to every active poly voice each
  block, even when values may be unchanged.
- Poly+Para pitch setup repeatedly computes MIDI note Hz with `std::pow`.
- Pitch-bend and modulation multipliers are recomputed inside loops where they are block-constant.
- `SIDEngine` setters always write SID registers, with no visible same-value fast path at the wrapper
  boundary.

These are candidates, not conclusions. The audit must measure call counts and no-op ratios before
implementing fixes.

## Architecture

### A1 - Counter-first diagnostic layer

Add a lightweight, Release-safe diagnostic counter path behind the existing CPU profile harness. The
counters are read only when `--cpu-profile` is active and should remain cheap enough to keep compiled
in Release:

- SID wrapper setter calls per block:
  - `setFrequency`
  - `setPulseWidth`
  - `setFilterCutoff`
  - `setFilterResonance`
  - `setFilterMode`
  - `setFilterVoices`
  - low-level `writeRegister`
- For setters that can compare against cached state, count both total calls and same-value calls.
- Active poly voice count and Poly+Para note slot count per block.
- Optional block-constant math counters for `std::pow`-based note/frequency conversions in the
  active path.

Counters should aggregate per profiling scenario and emit into the CPU JSON under a separate
`counters` object. They should not print during normal test runs.

### A2 - Focused section timing after counters

Only after the counter pass shows where repeated work clusters, split timing sections narrowly:

- `SIDRender/PolyMix` for Breadbin's active poly sample loop and summing.
- `SIDRender/SIDClock` around calls into `SIDEngine::clock`.
- `SIDRender/MonoMix` for mono/paraphonic mixing.
- `PolyMod/RegisterSync` around poly filter/PW/frequency setter loops.

These sections are diagnostic, not a rewrite. They should follow the existing `gm::CpuSectionProfiler`
pattern.

### A3 - Behavior-preserving fixes

Implement one proven target per commit. Candidate fixes, gated by audit evidence:

- Add same-value guards to `SIDEngine` setters so unchanged register values do not call
  `sid->write`.
- Cache MIDI note Hz or SID frequency register values for integer MIDI notes; keep detune and
  pitch-bend math exact enough to preserve existing rendered output within the WAV A/B policy.
- Hoist block-constant pitch-bend multiplier and modulation multipliers outside per-poly-voice loops.
- Skip disabled modulation/write paths before iterating all poly voices.
- Avoid reapplying full voice settings to a poly voice unless a dirty flag or newly allocated voice
  requires it.

If a candidate changes samples more than the A/B policy allows and the change is not obviously a
bug fix, revert that candidate and leave the measurement in the targets doc as a non-landed target.

## Non-Goals

- No active voice-count reduction.
- No reSIDfp sampling quality reduction.
- No switch to `clockSilent` in active audio paths.
- No changes to JUCE, reSIDfp, libsidplayfp, or other vendored dependencies.
- No tail gating in this pass unless the user explicitly promotes it after the audit.

## Acceptance Criteria

- The audit commit produces per-scenario counter evidence in the CPU JSON.
- Each optimization commit names the measured no-op source it removes and includes before/after
  matrix numbers.
- S2 and S3 active playing do not lose voices, change voice allocation semantics, or silence active
  sources.
- Full S1-S5 WAV A/B is run for every tone-path optimization:
  - automatic pass: diff RMS <= -45 dBFS and centroid delta < 2%;
  - explainable flag: diff RMS between -45 and -25 dBFS;
  - stop and fix/revert: diff RMS > -25 dBFS, silence, sustained level shift > 1.5 dB, NaN/Inf,
    click evidence, or audibly shorter tails.
- S3 remains allowed to be over budget if the audit shows the remaining cost is active reSIDfp
  emulation rather than no-op work. In that case, update the targets doc to mark that portion as
  sound cost, not waste.

## Testing and Verification

- Build Release before every profile. Do not use Debug profiling numbers.
- Run:
  - `BreadbinLFOTests.exe`
  - `BreadbinIntegrationTests.exe`
  - `BreadbinMutationTests.exe` (known 17/18 killed, one documented survivor, coverage adequate)
- Run matrix CPU profiles before and after each target:
  - `BreadbinIntegrationTests.exe --cpu-profile --json releases/cpu_after_<target>_2026-06-10.json`
- Re-render full S1-S5 WAVs at HEAD and compare against `releases/ab/4c07888/`.
- ETW/function-level drill-down remains required before marking function-level acceptance criteria
  landed; it needs an elevated Administrator shell and `D:\SymCache`.

## Reference and Reuse Record

| Source | License | Files inspected | Reuse mode |
|---|---|---|---|
| `D:\Code\Moonglow` | first-party | CPU section instrumentation, `--cpu-profile` harness wiring, targets doc template | pattern-only |
| `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoon` | first-party/MIT utility headers | `CpuSectionProfiler`, `CpuProfileHarness`, `SilenceGate` | direct first-party reuse already landed |
| `D:\Code\breadbin\build\_deps\libsidplayfp-src\src\builders\residfp-builder\residfp\SID.h` | GPL-2.0-or-later | `SID::clock(unsigned int cycles, short* buf)` implementation and API notes | inspection only; no source copied |

## Rollback

Each optimization lands as a separate commit after the audit commit. If a target fails tests,
profile acceptance, or WAV A/B policy, revert only that target commit and keep the audit evidence.
