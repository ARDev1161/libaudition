# O2: paired native Acoustix acceptance for zero-strength SSL birth guard

This experimental stacked PR evaluates **ODAS PR #12** on the
original, unchanged genuine six-channel LibriSpeech Acoustix
T02/T03/T07 WAVs and the explicitly SYNTHETIC six-channel
zero-PCM T00. It does not switch production libaudition, modify
ODAS master or touch audition_ros2.

## Precise before/after provenance

Before: successfully verified native Acoustix run 42444370597,
artifact "acoustix-s2-real-odas-diagnostics", with instrumented
ODAS commit 76da9ec6? **NO** — this is the unchanged pre-fix
ODAS commit e5d10d5f9a6f70c84ec563c57dd7dccb1699aae8.
Its artifact includes baseline and active-GMM-variance=0.01
replays, both with and without horizontal source prior.

After: native ODAS PR #12 HEAD
76da9ec6be6dce89cbfbdb5fb241e7180297b53c, compiled
in the exact same libaudition offline runner, with only the
dynamic SST zero-score birth predicate changed. The prior
variants, microphone geometry, T02/T03/T07 PCM SHA values,
T00 synthetic-zero PCM hash, candidate capacity and all
existing GMM probabilities/thresholds remain fixed.

The paired analyzer
tests/integration/acoustix_s2_zero_evidence_birth_compare.py
does all of the following:

- Reject absent/malformed original and candidate JSON artifacts;
- require bitwise identical source WAV SHA for every case/mode;
- require identical explicit SST parameter maps and horizontal
  source prior status across before/after replays;
- verify 1875 sequential 128-sample native ODAS records for
  each result, with exact hop index and sample offset;
- ensure every observed *newly born SST track* is backed by at
  least one **positive SSL strength** candidate crossing the
  original theta_new in that actual hop (including T02/T03/T07);
- require zero births and zero active SST hops on T00 synthetic
  digital-zero microphone PCM, in every after variant;
- require the two historical GMM-wide before variants each
  report the documented **1252** provisional T00 births;
- require that the default-GMM native T02/T03/T07 structural
  tracking metrics remain unchanged;
- report true T03 3D one-to-one oracle matching independently
  from the number of SST active tracks.

## Why it matters

At zero energy, native SSL still emits 4 candidates per hop
because its POT array is fixed-size, often giving them the
first scan-sphere point +Z. The GMM-wide hypothesis assigns
these empty placeholders a false birth posterior of about
0.978264868, which exceeds the frozen theta_new=0.9. That
produced 1252 new provisional SST IDs in 15 s of synthetic
digital zero PCM. A near-plane search prior did not prevent
those placeholder births because it affects which *positive*
directional scan peaks can be found; it does not change the
meaning of zero-strength placeholders.

ODAS PR #12 makes a minimal, testable invariant: dynamic
source birth requires a positive finite native SSL strength
and the original posterior threshold. The predicate does
not change any positive-score candidate threshold or the
tracking/SSS algorithm, and introduces no global acoustic
strength floor or public ABI change.

## Interpreting the result

Even zero T00 births would not justify increasing GMM variance
to 0.01 in production. Earlier real T03 replay under that
variance saw up to 227/243 two-active-track overlap hops
but zero spatial matches for both human source directions
within 45 degrees; the T02 single human also fragmented.
This remains an **unresolved** localization/association
failure. True SIRi/SI-SDRi, leakage and ASR WER are NOT
measured by this PR.

The derived A/B report is uploaded at
build-s2/zero_ssl_birth_guard_before_after.json in the
normal GitHub Actions artifact. Every acceptance claim must
refer to the exact last CI run and SHA, not a prior success.
Do not merge this diagnostic stack into main automatically.

To consider follow-up production changes, first run genuine
T00 real-room noise, 30/60/120-degree real/held-out
two-speaker geometry, T07 and T08, and check ID persistence,
false track precision, actual separated audio metrics and
ASR. ODAS PR #12 itself targets dev, never master.
