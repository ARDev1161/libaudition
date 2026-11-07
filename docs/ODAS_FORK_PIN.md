# Verified ODAS fork dependency pin (O2)

This PR changes only the ODAS CMake dependency from the historical upstream
`introlab/odas` SHA `bcb845434495e293df3d48f1203b7a86e1852449`
to **ARDev1161/odas** commit
`1d862592fa69c8e789743e71160e9e26ff38ebfc`.
This exact immutable SHA is a merged `dev` commit, not the moving `dev`
branch name. No public libaudition API, ROS code, hardware acceptance
criteria or acoustic quality thresholds change.

The fork contains the previous O0 SST factorized posterior and local
epsilon work, the O2 offline diagnostic recorder (compile-time OFF by
default), and the independently verified fix for loss of multiple births
during the same 128-sample hop. The offline recorder is **not** enabled
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
The previous ODAS PR #9 and native CTest as well as the paired
Acoustix run passed. No automatic libaudition main merge.

Rollback restores the old two-line `GIT_REPOSITORY` and `GIT_TAG`
definition in `cmake/Dependencies.cmake`; the exact original upstream
pin is recorded above. Note that rolling back reinstates the known
same-hop birth collision and original 7–8 SSL candidate suppression.

Next O2 work: diagnose T03 probation/association and physical
separation limitations independently, using real frozen WAV plus
clean/convolved stems and T00/T02 negative controls.
