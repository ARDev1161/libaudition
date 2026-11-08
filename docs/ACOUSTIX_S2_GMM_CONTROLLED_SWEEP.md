# O2: controlled native ODAS GMM sensitivity on frozen Acoustix S2

This is a diagnostic-only stacked libaudition test PR. It changes neither
the ODAS DSP source nor production libaudition, public API, ROS or ASR.
The point is to compare the same licensed real LibriSpeech six-channel
PCM through the actual native ODAS runner, not to simulate SST decisions
in Python.

## Input provenance and negative control

Real inputs: t02_dev_single, t03_eval_two and t07_eval_seven from the
immutable Acoustix S2 artifact. All WAV SHA256 checks must match the
baseline. 16 kHz, 240000 frames and exactly 1875 ODAS 128-frame hops
per scene, with zero recorded frame drops.

For each variant, also replay T00_ZERO_PCM: all six channels and all
240000 frames filled with zeros using the original T02 WAV format.
This is expressly SYNTHETIC silence, not measured room noise, a real
mic recording or a substitute for the roadmap T00/T08 room controls.

## One-factor test matrix

| Name | Active GMM mean | Active GMM variance | False-peak prior |
| --- | ---: | ---: | ---: |
| baseline | 0.4 | 0.0025 | 0.1 |
| active_mean_030 | 0.3 | 0.0025 | 0.1 |
| active_var_001 | 0.4 | 0.01 | 0.1 |
| false_prior_003 | 0.4 | 0.0025 | 0.03 |

Unchanged: inactive GMM (0.25, variance 0.0025), Pnew=0.1,
Ptrack=0.8, theta_new=0.9, N_prob=5 and theta_prob=0.8.
All overrides are test-runner-only, validated as bounded finite
values, and recorded in the actual native run summary.json.
The pinned ODAS fork's synchronous trace is enabled ONLY in
offline CI; never enable it in a realtime microphone callback.

## Experiment result semantics

The matrix measures actual ODAS provisional birth/retirement events,
unique confirmed SST IDs, maximum simultaneous active tracks,
two-active-track time during real T03 two-voice oracle overlap,
T02/T07 track-count trends, and the number of nonzero active-track
hops on explicitly synthetic T00 zero-PCM.

Every scenario records exact WAV SHA256, trace SHA256, frame/drop
integrity and runtime SST parameters. Every trace is checked by the
strict validator from the pinned ODAS checkout. The aggregated output
is build-s2/gmm-sweep/gmm_sweep_report.json. The test refuses existing
output paths to prevent accidental appended evidence.

These metrics do NOT establish speech separation, speaker identity,
SIRi, SI-SDRi, WER or stable multi-source acoustics. More active
tracks may be false positives, echoes or one-speaker fragmentation.
CI PASS certifies only execution and provenance, never production
fitness. Tests with synthetic JSONL fixtures validate contracts only.

To reproduce with the frozen Acoustix artifact and compiled offline
runner:

    python3 -m unittest discover -s tests/integration -p 'test_acoustix_s2_gmm_controlled_sweep.py' -v
    python3 tests/integration/acoustix_s2_gmm_controlled_sweep.py --artifact-root build-s2 --runner build-s2-odas/tests/audition_offline_integration --validator <pinned-ODAS>/tools/validate_sst_raw_trace.py --output build-s2/gmm-sweep

The opt-in GitHub workflow runs the same experiment after its ordinary
T02/T03/T07 evidence and uploads traces, reports and recording stems
for independent review. The experiment does not modify any original WAV.

## Acceptance criteria for future DSP changes

Before selecting a new default or touching ODAS dev: run held-out
T00 real-room noise, T02 one-voice, T03 angular sweeps at 30/60/120
degrees, T07 multi-voice and T08, and measure real source-ID
persistence, false tracks, leakage, SI-SDRi/SIRi and ASR WER.
Any actual ODAS DSP change belongs in a separate task PR into dev
with deterministic C tests and paired frozen audio. Do not merge
this diagnostic stack into libaudition main automatically.
