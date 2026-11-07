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
  With `--stems`, each source is also exported as an individual 4-channel
  microphone contribution.
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
| `audio_acoustix_dynamic_room` | Yes | No, optional CMake/CTest | Render moving RIR + replay genuine ODAS, fail on processing errors |
| `Acoustix acoustic room (manual)` GitHub Actions | Yes | On manual dispatch | Install pinned dependencies, render 1.5/3/12s, archive WAV/ground truth/report |
| Full local scenario | Yes | No | All 12 seconds, optionally seven natural voices and per-source stems |

The optional ODAS replay currently gates **successful pipeline execution,
matching input frames, no recorder drops, and presence of SSL proposals**.
It reports track count/separation activity rather than pretending ODAS can
reliably separate all seven simultaneous voices. Quantitative quality gates
must be calibrated after examining a real baseline; do not set arbitrary
large tolerance values merely to obtain green CI.

## GitHub Actions

Go to **Actions → Acoustix acoustic room (manual) → Run workflow** and
select the feature branch, 1.5 s / 250 ms for a smoke. The full 12 s
case is intentionally opt-in because each robot pose requires a fresh
multi-source 3D room RIR computation. Download the uploaded acoustic
fixture artifacts even on failure to inspect logs.

The ordinary PR workflow stays lightweight: it runs
`audio_acoustix_scene_contract` as part of the normal CTest suite, but
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
  -DPython3_EXECUTABLE=/path/to/acoustix-venv/bin/python
cmake --build build-acoustix --target audition_offline_integration --parallel
ctest --no-tests=error --test-dir build-acoustix \
  -R '^audio_acoustix_(scene_contract|dynamic_room)$' --output-on-failure
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
