# Acoustix headless 7×7 m acoustic simulation

Status: **experimental offline integration**, not calibrated real-room accuracy.

## Architecture and boundaries

- A checked-in [room scenario](../tests/fixtures/acoustix/table_conversation_7x7.json)
  defines a 7×7×2.8 m rectangular room, RT60 = 0.36 s, seven people
  around a central table, refrigerator hum, air conditioner noise, and
  intermittent traffic near a window.
- A four-microphone ReSpeaker-like planar array travels around the table,
  with changing orientation. Each microphone's world coordinates are
  rotated/translated with the robot. No physical robot or sound hardware
  is required.
- [acoustix_scene.py](../tests/integration/acoustix_scene.py) independently
  synthesizes the dry sources, takes time-varying **Acoustix CPU
  PyRoomAcousticsRoom** impulse-response snapshots for every source/mic
  pair, and renders through overlap-add of smoothly weighted emission
  segments. This retains the tails of preceding reflections across motion
  steps. All microphones use the same scaling; there is no independent
  per-channel peak normalization.
- Output: six-channel 16 kHz/PCM16 `input.wav`: channel 0 = convenience
  **dry reference**, channels 1–4 = actual synthetic microphone signals,
  channel 5 = silent playback reference. This matches the existing
  offline ReSpeaker ODAS input mapping, **not** physical six-microphone
  geometry. `ground_truth.json` contains robot trajectory and source
  bearings in robot-local coordinates, sampled at 128-sample hops.
  With `--stems` (enabled in the dedicated CI workflow, or via
  `LIBAUDITION_ACOUSTIX_EXPORT_STEMS=ON` in CMake), each source is exported
  as an individual 4-channel microphone contribution.
- The existing `audition_offline_integration` executable processes the
  resulting PCM through real ODAS SSL/SST/SSS, emits recordings and
  observations, and produces a structural `metrics.json` report.

No Acoustix/torch/Qt/ROS headers or libraries are linked into or installed
with the C++ SDK. Acoustix is exclusively an optional development-time
Python fixture generator.

### What the physics does and does not cover

The room impulse responses include direct sound and **repeated specular
reflections** (image-source method) for a rectangular room. RT60 is a
simplified reverberation parameter rather than a calibrated frequency-dependent
material model. The moving room uses discrete RIRs with overlapping
emission-time windows; this protects audio tails and smooths transitions,
but is **not** a validated continuous-wave Doppler/phase propagation model.
The car is projected onto a moving virtual emitter *inside* the room by
the window. It does not simulate accurate glass transmission, occlusion,
diffraction, scattering, or outdoor road acoustics.

Default voices are deterministic **voiced-like excitation**, not understandable
human speech. They are suitable for spatial geometry and stress tests,
but not WER, language accuracy, or real speaker identification. Provide
seven **actual voice WAVs** to meaningfully test VAD/ASR/leakage.
Acoustic simulation is not a replacement for measured ReSpeaker recordings.

## Test layers

| Run | Requires Acoustix | Runs automatically | Purpose |
| --- | --- | --- | --- |
| `audio_acoustix_scene_contract` | No | Yes, via normal CTest/CI | Validate scenario schema, timing, geometry |
| `audio_acoustix_metrics_selftest` | No | Yes, via normal CTest/CI | Test one-to-one 3D track/source matching and adjacent ID switches |
| `audio_acoustix_signal_selftest` | No | Yes, via normal CTest/CI | Validate PCM16 stem summation, microphone diversity, and corrupt audio rejection |
| `audio_acoustix_dynamic_room` | Yes | No, optional CMake/CTest | Render moving RIR + replay genuine ODAS, fail on processing errors |
| `Acoustix acoustic room (headless)` Actions | Yes | Ready-for-review: full 12s; manual: 1.5/3/12s | Render, replay ODAS, export oracle, stems and tracking metrics |
| Full local scenario | Yes | No | All 12 seconds, optionally seven natural voices and per-source stems |

The optional ODAS replay currently gates **successful pipeline execution,
matching input frames, no recorder drops, and presence of SSL proposals**.
It reports track count/separation activity and diagnostic-only 3D angular
errors using **one-to-one** track/source association in the robot's body frame
within 45 degrees. A single track cannot count as seven simultaneously
tracked speakers. The report includes each speaker's matched source-hop
fraction, total matched voice-source coverage, the median/P90 angular error,
adjacent matched ID switches, and maximum simultaneous tracks. These figures
are **not calibrated real-world accuracy gates**: a synthetic voice is not
intelligible speech, and the algorithm may not resolve seven overlapping voices. Quantitative quality gates
must be calibrated after examining a real baseline; do not set arbitrary
large tolerance values merely to obtain green CI.

## GitHub Actions

Go to **Actions → Acoustix acoustic room (headless) → Run workflow** and
select the feature branch, 1.5 s / 250 ms for a smoke. Marking this PR
**Ready for review** triggers a full 12-second headless job. Ordinary pushes
only run the inexpensive scene/metric self-tests in main CI. The full room
simulation is expensive because each robot pose requires a fresh multi-source
3D RIR calculation. Download the uploaded acoustic
fixture artifacts even on failure to inspect logs.

The ordinary PR workflow stays lightweight: it runs
`audio_acoustix_scene_contract` and `audio_acoustix_metrics_selftest`
as part of the normal CTest suite, but
does not install torch/gpuRIR or render the room on every push.

## Run locally on Linux

Install ODAS build dependencies and follow the standard
[ODAS backend](backends/odas.md) guide. Set up a dedicated Python **3.12+**
virtual environment; use a CPU-only torch/torchaudio build if you do not
intend to test optional CUDA backends. A reproducible manual CI dependency
recipe is in [acoustix-room.yml](../.github/workflows/acoustix-room.yml).

The upstream Acoustix revision is pinned to
`32143bfd38c6f2196244e42e2385df48a71df7a4`.
Because the project depends on gpuRIR even when using its Pyroomacoustics
backend, install the pinned project using `pip install --no-deps` **after**
provisioning its needed CPU dependencies as in the workflow.

```sh
cmake -S . -B build-acoustix -G Ninja \
  -DLIBAUDITION_WITH_ODAS=ON \
  -DLIBAUDITION_WITH_SPDLOG=OFF \
  -DLIBAUDITION_BUILD_TESTS=ON \
  -DLIBAUDITION_BUILD_QT_DEMO=OFF \
  -DLIBAUDITION_BUILD_EXAMPLES=OFF \
  -DLIBAUDITION_ENABLE_ACOUSTIX_SCENE_TESTS=ON \
  -DLIBAUDITION_ACOUSTIX_RENDER_SECONDS=12 \
  -DLIBAUDITION_ACOUSTIX_HOP_MS=250 \
  -DLIBAUDITION_ACOUSTIX_EXPORT_STEMS=ON \
  -DLIBAUDITION_ACOUSTIX_COMPARE_CAPACITY=ON \
  -DPython3_EXECUTABLE=/path/to/acoustix-venv/bin/python
cmake --build build-acoustix --target audition_offline_integration --parallel
ctest --no-tests=error --test-dir build-acoustix \
  -R '^audio_acoustix_(scene_contract|metrics_selftest|signal_selftest|dynamic_room)$' --output-on-failure
```

To use natural speech, place `speaker_1.wav` through `speaker_7.wav`
in a directory, each mono 16 kHz PCM16, and add
`-DLIBAUDITION_ACOUSTIX_SPEECH_DIR=/absolute/path/to/voice-wavs`
to the CMake configuration. The source activity schedule in the JSON
determines which parts of those waveforms are audible.

For manually generating **individual reference stems** and reports:

```sh
/path/to/acoustix-venv/bin/python tests/integration/acoustix_scene.py \
  --scene tests/fixtures/acoustix/table_conversation_7x7.json \
  --output build-acoustix/acoustix-full \
  --hop-ms 250 --stems \
  --speech-dir /path/to/voice-wavs \
  --runner build-acoustix/tests/audition_offline_integration
```

To validate the scene without Python dependencies:

```sh
python3 tests/integration/acoustix_scene.py \
  --scene tests/fixtures/acoustix/table_conversation_7x7.json --validate-only
```

## Third-party licensing

Upstream Acoustix README advertises MIT, but its checked-in `LICENSE`
at the pinned revision is **GPL-3.0**. Treat it as GPL-3.0 until upstream
resolves the contradiction. It is **not** vendored, linked, bundled,
or installed with libaudition. The optional development environment
clones/installs Acoustix separately. Do not distribute combined code
or claim MIT licensing for Acoustix without a separate license review.

## First complete 12-second CI observation (2026-11-07)

The first full CPU run passed the room-scene contract, the 3D-association
self-tests, and genuine ODAS replay on the entire 12 seconds.
[Workflow #42330822775](https://github.com/ARDev1161/libaudition/actions/runs/42330822775)
generated 10 individual source stems, a six-channel input WAV, moving
body-frame source oracle, and tracked-source diagnostics:

| Metric | Observed (diagnostic only) |
| --- | ---: |
| 3D RIR simulation duration | 12 s (plus padded reverberant tail) |
| Maximum simultaneously active synthetic voices | 7 |
| Maximum simultaneous ODAS tracks | 3 |
| Mean ODAS tracks during multi-voice activity | 1.4702 |
| Matched active voice source-hops (unique tracks, ≤45°) | 1,462 / 7,814 (18.71%) |
| Median / P90 3D angular error of *matched* observations | 29.22° / 35.85° |
| Median / P90 horizontal azimuth error of *matched* observations | 11.77° / 24.38° |
| Adjacent matched-hop track ID switches | 2 / 1,442 comparisons |
| ODAS SSL proposals / track observations | 5,953 / 2,448 |
| Saved source recordings / recorder drops | 3 / 0 |

The separate **azimuth error** is a navigation-relevant comparison of the
robot-local horizontal bearing, not a 3D position estimate. A planar array
has intrinsically ambiguous elevation; reporting a 3D angle alone can hide
useful horizontal information. Both figures still omit all missed sources.

The median/P90 are **conditional on matches** and therefore do not include
missed sources. The small number of ID switches is **not** evidence of stable
seven-way speaker tracking; the system acquires only a few of the concurrent
sources in the first place. Current source signals are voiced-like and not
natural speech. Seven real voice recordings are required to evaluate
speech separation/leakage, VAD, diarization or ASR accuracy.

When source stems are enabled, `signal_audit.json` checks that all ten
four-channel stems reconstruct the ODAS microphone mixture within
PCM16 roundoff (12 LSB maximum for ten stems). The audit checks that
microphone channels are distinct, reports source and microphone RMS,
and finds the last nonzero microphone PCM sample. ODAS tracks observed
after **actual digital silence** are counted separately: a tracker may
intentionally hold the previous track through a short silent interval,
so this diagnostic is not a fixed quality gate.

**Important capacity caveat:** the original offline ODAS runner uses the
library's default `ssl.potential_source_count = 4` and `sst.max_tracks = 4`.
Therefore a four-slot result cannot establish the maximum number of
independently recoverable voices in a seven-speaker scene. The optional
`LIBAUDITION_ACOUSTIX_COMPARE_CAPACITY=ON` setting replays **the exact
same rendered microphone WAV** across a 2×2 factorial sweep:
**4/4**, **4/8**, **8/4**, **8/8** (SSL candidate count / SST max tracks).
Reports are saved as `metrics.json`, `metrics_4x8.json`,
`metrics_8x4.json`, `metrics_8x8.json`,
`capacity_comparison.json`, and shown in the GitHub Actions summary.

The initial 4/4→8/8 experiment produced **5,953→9,192 SSL candidate
observations but 2,448→0 track observations** on the identical 12-second
WAV (workflow #42333482067). Thus increasing both limits alone is not
a valid fix; the separate 4/8 and 8/4 runs isolate which capacity
changes tracking behavior. The experiment does not change geometry,
beamforming, signals or the ODAS probability thresholds, and cannot
create spatial information that four microphones do not observe.
No specific number of tracked speakers is a pass/fail requirement.

The baseline should guide investigation, not hard-coded pass thresholds.
A green room test proves the offline pipeline completes without structural
failure, **not** that source separation quality is acceptable.

## Future acceptance gates

1. Measure dynamic DOA median/P90 angular error in robot frame with
   reflection settings and angular reference uncertainty stated.
2. Record ID switch rate and tracking continuity while circling the table.
3. With genuine speech, evaluate cross-source leakage/SI-SDR, VAD
   onset/offset, ASR WER and duplicated transcripts.
4. Calibrate simulation against measured ReSpeaker corpus
   (microphone responses, noise floor, directivity and room RT60).
5. Add configurable window transmission/occlusion and frequency-dependent
   materials via an engine supporting genuine multi-room or 3D geometry.
