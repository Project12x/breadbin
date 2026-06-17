# Breadbin Listening Gate - 2026-06-17

This note lists the performance A/B pairs that require human listening before
release/push acceptance. All files are 18 s stereo WAVs from the deterministic
headless render harness.

## 1. Poly Release Gate

Purpose: checks the release-tail gating change against the previous exact-hoist
baseline.

- A: `D:\Code\breadbin\releases\ab\exact_hoists_2026-06-11\s3-worst-case.wav`
- B: `D:\Code\breadbin\releases\ab\poly_release_gate_2026-06-12\s3-worst-case.wav`
- Metrics: diff RMS `-30.66 dBFS`, peak diff `-12.26 dBFS`, output RMS delta
  `-0.018 dB`, centroid delta `1.131%`.
- Listen for: earlier tail truncation, clicks, or a musically obvious loss of
  release texture in the dense worst-case patch.

## 2. ECO-Off Preservation

Purpose: checks the merged ECO stack with ECO off against the prior Ultra/current
arrangement. This should sound like the same SID arrangement.

- A: `D:\Code\breadbin\releases\ab\poly_release_gate_2026-06-12\s3-worst-case.wav`
- B: `D:\Code\breadbin\releases\ab\eco_hybrid_2026-06-12\s3-worst-case.wav`
- Metrics: diff RMS `-37.87 dBFS`, peak diff `-13.73 dBFS`, output RMS delta
  `-0.0253 dB`, centroid delta `0.020%`.
- Listen for: any audible tone shift despite ECO being off.

## 3. ECO Hybrid Character

Purpose: checks the deliberate Manual ECO Hybrid behavior against Ultra/current.
This pair is expected to sound different because Hybrid uses one stereo anchor
plus alternating mono SID notes instead of two SIDs per note.

- A: `D:\Code\breadbin\releases\ab\poly_release_gate_2026-06-12\s3-worst-case.wav`
- B: `D:\Code\breadbin\releases\ab\eco_hybrid_2026-06-12\s7-eco-hybrid-poly.wav`
- Metrics: diff RMS `-29.36 dBFS`, peak diff `-11.01 dBFS`, output RMS delta
  `-3.7634 dB`, centroid delta `4.839%`.
- Listen for: whether the compromise is musically acceptable for Manual ECO,
  especially stereo image, density, and patch identity.

## Decision

- Approve pair 1 only if the release-tail change is not audibly harmful.
- Approve pair 2 only if ECO-off still reads as the same Ultra/current behavior.
- Approve pair 3 if Manual ECO Hybrid is an acceptable alternate performance
  mode, not because it is identical.

