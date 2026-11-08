# O2: ODAS native GMM likelihood and absolute epsilon floor on real Acoustix WAV

This **diagnostic-only** stacked PR does not change the ODAS tracker,
its priors, or its default validation thresholds. It uses the same licensed
real LibriSpeech Acoustix S2 six-channel PCM, precise microphone geometry,
frozen oracle, and actual per-hop ODAS native trace as earlier PRs #97–101.

## Independently derived likelihood formula

In ARDev1161/odas:dev src/system/mixture2mixture.c, for each SSL candidate,
the unnormalised false-peak and new-source likelihood terms are:

    q_false(s) = Pfalse * G_inactive(s) * diffuse
    q_new(s)   = Pnew   * G_active(s)   * diffuse

In the frozen libaudition offline configuration (as verified by CI):
Pfalse=Pnew=0.1; G_active has mean 0.4, variance 0.0025;
G_inactive has mean 0.25, the same variance 0.0025. For 1D normal
likelihoods and a shared positive diffuse factor, the exact odds are:

    log(q_new/q_false) = 60*s - 19.5

where s is the **actual SSL potential strength**, not a voice identity.
The normalisation with existing-track assignment mass can change the
absolute posteriors but cannot change these two-label odds.

Therefore the new and false labels have equal odds at strength 0.325.
Even with no competing track or epsilon, crossing theta_new=0.9
requires strength above (19.5+log(9))/60 ~= 0.36162.
At the observed second-direction median strength 0.114744, the
new/false odds are approximately exp(-12.615) = 0.0000033.
A *posterior* new-source probability can be lower still because of
existing tracks and epsilon. Do not reinterpret this as isolated speech.

## Additional absolute-epsilon effect

The current ODAS fork uses a **per-candidate** denominator:

    mass = epsilon + sum(positive q_false, q_new, q_track...)

where epsilon=1e-20 is an absolute probability-mass floor.
Both Gaussians are narrow (stddev 0.05). SSL candidate strengths
near 0.9–1.0 lie far above their means: both GMM likelihoods can
be << epsilon. For such out-of-distribution high-energy peaks,
epsilon can dominate the posterior **even if q_new >> q_false**.

This is not ordinary floating-point underflow, nor by itself a bug
in source identification. The epsilon class represents unmodelled
evidence, so suppressing very large energy peaks may be an intentional
OOD safeguard. It becomes a potential numerical design problem if
valid high-energy real sources are systematically rejected. It MUST be
tested against silence controls and held-out voice-quality objectives
before modifying DSP.

The native JSONL records P_new and every P_existing_track, not P_false
or P_epsilon separately. The new audit therefore reconstructs an
**estimated** P_false = P_new*exp(-logodds), then estimates epsilon
as 1-P_new-sum(P_existing)-P_false. All such quantities are subject
to float32 rounding. Classification is accepted only at a large
dominance margin (inferred P_epsilon >0.5), not a near-zero floor.
The validator rejects impossible posterior masses and inconsistent
native likelihood ratios rather than silently normalising them.

## Frozen real observations, not modeled output

Same 128-sample hop clock, source schedule and PCM SHA as the successful
T02/T03/T07 real Acoustix S2 replay. Each case contains 1875 hops,
7500 SSL candidate assessments (4 candidates/hop) and zero recorder
drops. Independently reconstructed inferred epsilon dominates (>0.5)
on:

| Case | Candidate assessments | Epsilon-dominant |
| --- | ---: | ---: |
| T02 single voice | 7500 | **7** |
| T03 overlapping 2 voices | 7500 | **14** |
| T07 seven voices + noise | 7500 | **0** |

For additional context, candidates with strength >0.8:
T02: 47, all 47 have P_new <0.01;
T03: 22, 13 with P_new <0.01;
T07: 0.
These are **per-candidate** counts, not distinct speech sources,
false-positive counts, or ASR decisions. Strong source vectors may
belong to an existing track rather than the new-source label.

## Test and replay

- Native GMM defaults, priors and exact instrumented ODAS dependency
  SHA are verified in the GitHub workflow before evaluating results.
- The Python audit reads actual JSONL with WAV hash validation, zero
  recorder drops, exact frame count, contiguous hop timestamps,
  bounded per-candidate probabilities and unique existing-track IDs.
- Sixteen separate synthetic adversarial **contract tests** cover
  the analytic likelihood identity, false/track/epsilon decomposition,
  impossible probability mass, NaNs, booleans, duplicate IDs,
  broken clocks, empty traces and threshold arithmetic.
- Opt-in Acoustix workflow archives an immutable report at
  build-s2/real_speech_gmm_epsilon_audit.json.

Run from a checkout of this test branch on the exact frozen Acoustix
artifact's extracted build-s2 directory:

    python3 -m unittest discover -s tests/integration -p 'test_acoustix_s2_gmm_epsilon_audit.py' -v
    python3 tests/integration/acoustix_s2_gmm_epsilon_audit.py --artifact-root build-s2 --output build-s2/real_speech_gmm_epsilon_audit.json

## Future fix acceptance (NOT claimed here)

In a separate ODAS fix/experiment branch, test **both** hypotheses:
(a) active/inactive Gaussian calibration for actual SSL scores, and
(b) absolute versus relative/fallback epsilon mass. Hold the original
WAV and oracle fixed, and compare with T00 silence plus T02, T03, T07
and wider-angle held-out tests.

Do not increase false tracks merely to gain T03 provisional births.
Require independent quality metrics (source-ID persistence, false
tracks, DOA, SI-SDRi, SIRi, voice leakage and ASR WER). None of the
separation/ASR metrics is measured or improved by this diagnostic.
Production libaudition/main, ODAS/master and audition_ros2 remain
outside this task.
