# Breadbin - ECO Poly SID Budget

**Date**: 2026-06-12
**Status**: Planning draft
**Branch**: `perf/breadbin-optimization-20260610`
**Builds on**: the S1-S6 CPU matrix, the poly release SID render gate candidate, and the current
4-mode voice system (`Mono`, `Para`, `Poly`, `Poly+Para`).

## Goal

Reduce musician-perceived latency pressure in polyphonic playing by cutting the number of reSIDfp
engines clocked per poly note, while keeping the current dual-SID sound available and unchanged
unless the user explicitly enables ECO behavior.

## Core Decision

ECO mode introduces an explicit **Poly SID Budget** policy:

| Policy | Meaning | SID cost for N poly notes | Intended use |
|---|---|---:|---|
| `Ultra` | Current behavior: every poly slot clocks a full L/R SID pair. | `2N` | Maximum stereo richness and complete backward compatibility. |
| `Hybrid` | One anchor note clocks the full L/R SID pair; additional notes clock one SID each, alternating L/R character. | `N + 1` | Live/default ECO compromise that preserves the dual-SID identity for the exposed note. |
| `Max ECO` | Every poly note clocks one SID engine. | `N` | Lowest CPU, largest tone/stereo change. |

Existing behavior remains the normal-mode default. Old sessions and presets load as they do today
unless ECO mode is explicitly enabled.

## ECO Mode Semantics

1. **Normal mode is unchanged.** `Poly` and `Poly+Para` keep spawning a stereo SID pair per poly
   slot. Existing presets, automation, A/B references, and session recall remain valid.
2. **ECO mode is explicit.** A new Settings popup exposes ECO controls. Turning ECO on is the user
   action that allows poly render behavior to change.
3. **Hybrid is the recommended ECO budget.** In ECO, Hybrid should be the first shipped and default
   budget because it preserves much of the dual-SID character while reducing dense poly cost.
4. **Ultra remains available inside ECO.** ECO can be on for future performance helpers while the
   budget remains Ultra; this keeps the Settings model extensible.
5. **Auto ECO is future work.** The first implementation is manual. Auto budget switching requires
   visible feedback and separate A/B policy because it can change behavior during performance.

## Stereo Anchor

Hybrid needs one note to receive the full stereo pair. The Settings popup exposes:

- `Oldest`: the first note in the current played group gets the stereo pair.
- `Newest`: the most recent note gets the stereo pair.

Initial implementation rule: **anchor assignment changes on note-on, not by silently promoting a
previously mono retained voice after note-off**. This avoids bringing in an unclocked dormant SID side
mid-tail with stale envelope/phase. In `Newest`, a new note can take the stereo pair and demote the
previous anchor to its assigned mono side because the newly allocated note starts both SID sides
fresh. In `Oldest`, the first active note keeps the pair until it releases or the voice is reused;
later notes alternate mono L/R.

If later listening tests show users strongly expect automatic promotion after anchor release, add it
as a second-stage behavior with an explicit fade-in/retrigger design and a separate A/B gate.

## Rendering Model

Each `PolyVoice` keeps owning both `sidLeft` and `sidRight`, so data layout, state serialization, and
existing voice-management code stay close to current behavior. ECO budget controls only which sides
are triggered/clocked/mixed for a given poly slot:

- `Pair`: trigger and clock both `sidLeft` and `sidRight`; mix as today.
- `LeftMono`: trigger and clock only `sidLeft`; pan/mix using the existing left SID pan law.
- `RightMono`: trigger and clock only `sidRight`; pan/mix using the existing right SID pan law.

Mono-role voices should not call `clock()` on the unused side. The largest win comes from eliminating
reSIDfp clocking; later polish can skip unused-side setters/register sync as a second target if the
profile shows meaningful overhead remains.

## UI Design

Add a **Settings** popup, initially focused on performance:

- `ECO Mode`: `Off` / `Manual`
- `Poly SID Budget`: `Hybrid` / `Ultra` / `Max ECO`
- `Stereo Anchor`: `Oldest` / `Newest`

Visibility and naming:

- ECO Off or ECO Ultra: existing SID I / SID II towers stay visible and unchanged.
- ECO Hybrid: keep both SID towers visible because both towers still define alternating mono color
  and the stereo anchor pair.
- ECO Max ECO: show one primary SID editing surface or clearly disable/hide the right-side per-note
  editing surface. The recommended label is `SID Engine`.

The UI must show current behavior plainly. No hidden automatic downgrade should occur without a
visible ECO state indicator.

## Poly+Para Handling

The same budget policy applies at the poly slot level. A `Pair` slot can play its internal para
voices on both SIDs. A mono slot plays its internal para voices only on its assigned left or right SID.
This preserves the cost model: the expensive unit is the poly slot's SID engine count, not the
headline MIDI-note count.

## Performance Target

The current final candidate profile has S3 worst case at:

- wall average: `9264.5 us`
- `SIDRender`: `9150.75 us`
- budget at 48 kHz / 512: `10666.7 us`

Hybrid should reduce dense-poly SID clocking from `2N` to `N + 1`. For an 8-note poly load, that is
16 SID engines down to 9. The practical target is:

- S3 worst-case wall average below `6500 us` at 48 kHz / 512.
- Typical playing remains below `4500 us`.
- No ECO-off CPU or WAV behavior change.

These are planning targets; final acceptance is based on measured profiles and WAV/listening gates.

## A/B and Behavior Policy

Because ECO deliberately changes samples, do not compare ECO-on renders against ECO-off as a
behavior-preserving change. Instead:

- ECO Off must render bit-identical or within existing tiny nondeterministic tolerance against the
  pre-ECO reference.
- ECO Hybrid and Max ECO get their own reference render sets and listening notes.
- The Settings popup must make it obvious when ECO is active.
- No existing preset should silently load into ECO unless a future versioned preset deliberately
  opts into it.

## Roadmap

1. Manual ECO settings and Hybrid budget.
2. Max ECO budget after Hybrid is validated.
3. reSIDfp fork/API work to reduce per-engine cost without reducing SID count.
4. Auto ECO budget as a later feature with visible status, thresholds, and hysteresis.

## Reference and Reuse Record

| Source | License | Files inspected | Reuse mode |
|---|---|---|---|
| `D:\Code\breadbin` | project source | `src/PluginProcessor.h`, `src/PluginProcessor.cpp`, `src/PluginEditor.cpp`, `tests/IntegrationTests.cpp` | local design over existing code |
| `C:\Users\estee\Desktop\My Stuff\Code\Antigravity\ghostmoon` | first-party | `gm::SilenceGate`, CPU profile harness patterns already used by Breadbin | existing first-party dependency |
| reSIDfp via libsidplayfp vendored source | GPL-2.0-or-later | current Breadbin integration only; no new source copied for this planning pass | inspection only |

## Non-Goals

- No JUCE or vendored dependency edits.
- No automatic ECO switching in the first implementation.
- No hidden polyphony reduction when ECO is off.
- No removal of Ultra/current behavior.
- No master-branch merge without user listening sign-off for ECO Hybrid examples.
