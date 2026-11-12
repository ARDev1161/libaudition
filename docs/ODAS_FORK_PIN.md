# Verified ODAS fork dependency pin (O2)

This PR changes only the ODAS CMake dependency from the historical upstream
`introlab/odas` SHA `bcb845434495e293df3d48f1203b7a86e1852449`
to **ARDev1161/odas** commit
`4c8e3bfcb56124ee340477a2443ea2ebe11ee712`.
This exact immutable SHA is a merged `dev` commit, not the moving `dev`
branch name. No public libaudition API, ROS code, hardware acceptance
criteria or acoustic quality thresholds change.

The fork contains the previous O0 SST factorized posterior and local
epsilon work, the O2 offline diagnostic recorder (compile-time OFF by
default), and the independently verified fix for loss of multiple births
during the same 128-sample hop, plus the independent positive-SSL-score precondition for dynamic SST births (ODAS PR #12). The offline recorder is **not** enabled
in production by this pin; enabling it manually adds synchronous per-hop
file I/O and must never be used in a realtime capture callback.

## Verified before/after evidence

Frozen real LibriSpeech Acoustix S2 WAVs in libaudition
[paired A/B run #42410846601](https://github.com/ARDev1161/libaudition/actions/runs/42410846601)
are SHA-identical before and after the fix. All case WAVs contain 240000
16 kHz input frames and have zero recorder drops; T03 contains 1875
validated 128-frame SST decision hops. A temporary pinned ODAS
instrumented branch contained the same private birth-slot fix tested
in the dev merge.

| Scene | Births before → after | Max active SST tracks before → after |
| --- | ---: | ---: |
| T02 (one human voice) | 18 → 18 | 1 → 1 |
| T03 (two human voices) | 12 → 13 | 1 → 1 |
| T07 (seven voices + fridge/AC/car) | 51 → 51 | 4 → 4 |

T03 hop 699: previously 3 above-threshold SSL proposals caused only
one retained pending SST birth ID 14; now two free slots retain IDs
12 and 13. The missing second **active** voice in T03 is **not fixed**.
During the two-voice overlapping speech interval, seven provisional
secondary IDs still expire without promotion after five hops.
No SI-SDRi, SIRi or WER was computed. This is not a claim that the
seven-person case is successfully separated or that ReSpeaker hardware
has been accepted.

## CI and rollback

The PR includes targeted CI building actual libaudition C++ tests and
offline consumer with `LIBAUDITION_WITH_ODAS=ON`, then running
`OdasOptions.*` and `OdasBackend.*` GoogleTests via CTest.
It must pass before this PR is eligible for review/merge to main.
ODAS PR #9 and PR #12 have both passed native CTest and actual
paired Acoustix experiments. The exact production pin must pass this
libaudition PR's own C++ CI. No automatic libaudition main merge.

Rollback restores the old two-line `GIT_REPOSITORY` and `GIT_TAG`
definition in `cmake/Dependencies.cmake`; the exact original upstream
pin is recorded above. Note that rolling back reinstates the known
same-hop birth collision and original 7–8 SSL candidate suppression.

Next O2 work: diagnose T03 probation/association and physical
separation limitations independently, using real frozen WAV plus
clean/convolved stems and T00/T02 negative controls.


## New no-evidence birth fix included in this immutable dev pin

After the slot-reservation repair, actual native Acoustix T03
experiments exposed that an *empty* SSL candidate slot may still
carry a pole direction and zero strength. With a deliberately
widened active GMM, ODAS assigned it a high new-source posterior
(~0.978), causing 1252 provisional births on 15 seconds of
expressly SYNTHETIC six-channel all-zero PCM.

ODAS PR #12 (merged to dev at exact SHA
`4c8e3bfcb56124ee340477a2443ea2ebe11ee712`) prevents
dynamic SST births unless the SSL strength is strictly positive
and finite. It leaves all normal positive-score candidates,
association/probation, SSS and GMM defaults unchanged.
The frozen real LibriSpeech Acoustix A/B experiment
[42445272845](https://github.com/ARDev1161/libaudition/actions/runs/42445272845)
PASS at identical WAV SHA and runtime options:

| Experimental scene | Provisional births before -> after |
| --- | ---: |
| Wide-GMM T00 synthetic digital-zero PCM | **1252 -> 0** |
| Wide-GMM T02 one voice | 77 -> 23 |
| Wide-GMM T03 two voices | 48 -> 22 |
| Wide-GMM T07 seven voices/noise | 100 -> 36 |

For both the normal GMM and the optional near-plane source prior,
**all original/default-GMM T02/T03/T07 structural tracking results
remained unchanged**. The wide-GMM phantom pair of active T03
tracks went away, but correct 3D two-human-source matches remained
**0/243**. Hence this fix is valid and tested, but DOES NOT repair
two-speaker voice localization/separation or ASR.

True human-source identity, SI-SDRi/SIRi, acoustic leakage,
WER and physical microphone quality require separate acceptance.
No experimental GMM widening or new spatial-prior default is
enabled by this pin.
