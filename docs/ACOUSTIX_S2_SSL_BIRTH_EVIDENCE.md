# O2 real T03: SSL directions vs SST birth posterior

This is a diagnostic-only stacked PR based on the original *real*
Acoustix LibriSpeech 7×7 m moving-robot sound simulation.
No rendering, DSP algorithm or production library changes.

## Frozen input provenance

- LibriSpeech Acoustix S2 job:
  https://github.com/ARDev1161/libaudition/actions/runs/42422756648
- Six-channel microphone PCM WAV T03 SHA256:
  c14fe750e47064de1b105c5fceab1ffe8e5330157b7519818a5c175d6c54f0ee
- 240000 frames at 16000 Hz; 1875 ODAS hops of 128 samples.
  Zero recorder drops. 243 oracle two-voice overlap hops.
- Two real speaker directions during overlap: median 31.48 degrees
  apart, range 8.38 to 45.17 degrees. This sample does NOT cover
  independent 60 or 120 degree angular-separation scenarios.

The oracle test uses 3-D unit-vector angular error. Two DIFFERENT SSL
candidates must each be within 25 degrees of a different oracle voice.
Pairing minimizes total angular error, ignoring birth probabilities and
strength, so it does not select favorable candidates based on the
outcome. This is angular coincidence, not isolated voice evidence.

## Genuine T03 measurements

| Property | Count or value |
| --- | ---: |
| Hops with two oracle voices | 243 |
| Hops with two distinct 3D candidate matches (25 deg) | 97 |
| Consecutive distinct-pair sequences | 32 |
| Median such sequence | 1 hop (8 ms) |
| Longest such sequence | 17 hops (136 ms) |
| Sequences at least 5 hops long (40 ms) | 5 |
| Hops with any two candidates above birth threshold 0.9 | 0 |
| Hops with two simultaneously active SST tracks | 0 |
| Second-oracle-direction candidate birth posterior median | 0.000003 |
| Second-oracle-direction candidate birth posterior maximum | 0.04101 |
| Second-oracle-direction candidates above birth threshold | 0 |
| First-oracle-direction candidate birth posterior maximum | 0.952221 |

At 25-degree matching, the first-oracle-direction candidate birth
posterior median was 0.004252. This is NOT the existence probability of
the existing active SST track ID 2; existing-track association posterior
is a separate quantity.

For source directions separated by <30 degrees, 69 of 108 overlap
hops had a distinct matched candidate pair. For separations from
30 through <60 degrees, 28 of 135 hops had a pair. There were no
overlap hops at >=60 degrees. Source energy, reverberation, movement
and elevation also vary, so do not attribute this correlation to
angular separation alone.

The result suggests that birth evidence for the second direction
is much weaker than the 0.9 birth threshold, before the later five-hop
0.8 track-confirmation threshold is applied. Decreasing only the
confirmation threshold cannot fix all of these birth failures.
But this diagnostic does not establish whether SSL localization,
reverberant echoes, association, source energy or mixture model is
the causal origin of weak independent birth evidence.

## Validation and reproduction

The Python audit uses genuine ODAS recorded per-hop JSONL:
tests/integration/acoustix_s2_ssl_birth_evidence.py

Its unit tests validate the geometry and fail-closed contracts, not
the quality of the acoustic simulation:
tests/integration/test_acoustix_s2_ssl_birth_evidence.py

To replay on the exact frozen job's extracted build-s2 artifact:

    python3 -m unittest discover -s tests/integration -p 'test_acoustix_s2_ssl_birth_evidence.py' -v
    python3 tests/integration/acoustix_s2_ssl_birth_evidence.py --artifact-root build-s2 --output build-s2/t03_real_ssl_birth_evidence.json --tolerance 25

The opt-in GitHub workflow runs the tests and the actual audit on
the real Acoustix WAV and archives the JSON report. Its numerical
checks pin the observed immutable baseline, not a passing quality gate.

## Next falsifiable work

Compare near-second-voice SSL candidate birth posterior, association
to existing active SST track and false-source evidence on the same
audio; stratify by oracle angular separation, SSL candidate strength,
elevation and persistence. Any ODAS DSP fix goes in an isolated fix/*
branch to dev with T00 silence/false-track control, T02 1-voice
regression, T03 paired actual replay and T07 7-voice negative controls.
Do not claim speaker identity or clean source separation from DOA.
True SI-SDRi, SIRi, WER, VAD onset, and physical ReSpeaker acceptance
are still NOT MEASURED by this analysis.
