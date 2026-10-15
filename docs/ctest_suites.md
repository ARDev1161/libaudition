# CTest suites: local machines and GitHub Actions

CTest is the test runner. GitHub Actions provisions compilers, optional
libraries, models and datasets, runs CMake, then invokes CTest. Model
exporters, downloads and installation are **preparation**, not tests.

CTest only lists tests built for the configured backends. A successful
minimal build does not imply that all optional backends were exercised.

## Inspect and run

```sh
cmake -S . -B build-test -G Ninja \
  -DLIBAUDITION_BUILD_TESTS=ON \
  -DLIBAUDITION_WITH_SPDLOG=OFF
cmake --build build-test --parallel
ctest --test-dir build-test -N
ctest --test-dir build-test --output-on-failure --parallel 2
```

Useful filters:

```sh
ctest --test-dir build-audio-integration -L audio-integration --output-on-failure
ctest --test-dir build-efficientat-real -L real-model --output-on-failure
ctest --test-dir build-efficientat-real -L esc10 --output-on-failure
ctest --test-dir build-efficientat-real -L stress --output-on-failure
ctest --test-dir build-sherpa -L speech-integration --output-on-failure
ctest --test-dir build-qt -L qt --output-on-failure
```

## Instrumented C++ core coverage

The `cpp-coverage` GitHub Actions job compiles a separate Debug configuration
with GCC gcov instrumentation (`--coverage`), executes its entire CTest
suite, and archives text, JSON, Cobertura XML, and HTML coverage reports.
Only `src/` library implementation sources count toward these results;
optional backends, third-party libraries and Qt are not included.

Locally (GCC, gcov and gcovr required):

```sh
cmake -S . -B build-coverage -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF \
  -DLIBAUDITION_WITH_SPDLOG=OFF -DLIBAUDITION_BUILD_TESTS=ON \
  -DLIBAUDITION_BUILD_EXAMPLES=ON -DLIBAUDITION_BUILD_QT_DEMO=OFF \
  -DCMAKE_CXX_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS=--coverage
cmake --build build-coverage --parallel 2
ctest --test-dir build-coverage --output-on-failure --parallel 2
gcovr build-coverage --root . --filter 'src/' --txt --json-summary \
  build-coverage/reports/coverage-summary.json
```

The core coverage job publishes the measured percentages and enforces minimum
thresholds of **88% line coverage** and **55% branch coverage**. The baseline
from CI #1124 was **2625 / 2889 lines (90.9%)** and **2142 / 3703 branches
(57.8%)**. CI fails on a regression below either threshold or on missing
coverage totals, but still uploads the generated reports. The thresholds
intentionally leave a small margin for instrumentation and test changes.

Coverage percentages reflect execution of instrumented core code paths, not
classification accuracy, branch behavior correctness, or optional backend and
hardware coverage. Consult the HTML report for per-file and per-branch gaps.

## Suites requiring external assets

| CMake option | Purpose | Required preparation |
| --- | --- | --- |
| `LIBAUDITION_REGISTER_MODEL_TOOL_TESTS=ON` | Model-tool Python tests, real EfficientAT/YAMNet, WAV fixtures, AudioSet and ESC-10 | Matching Python environment, pinned ONNX files, model labels, EfficientAT upstream source and network/ffmpeg for ESC-10 |
| `LIBAUDITION_REGISTER_REAL_QT_MODEL_TESTS=ON` | Qt tests with Silero, Whisper, speaker embeddings, Piper, CLAP and AASIST, plus real speech C++ replay when Sherpa is enabled | Relevant model files and paths, as documented in the backend CI jobs |
| `LIBAUDITION_REGISTER_EFFICIENTAT_REFERENCE_TESTS=ON` | C++ frontend vs PyTorch parity and synthetic ONNX adapter fixture | `numpy`, `onnx`, `torch`, `torchaudio` in the configured Python interpreter |
| `LIBAUDITION_REGISTER_INSTALLED_PACKAGE_SMOKE=ON` | Independent C++17 consumer of installed package | Install the library into `LIBAUDITION_PACKAGE_SMOKE_PREFIX` before running CTest |

For locally downloaded real-model assets, set
`-DLIBAUDITION_REAL_MODEL_ASSET_DIR=/absolute/path`.
For the model-tool suite, use `-DPython3_EXECUTABLE=/path/to/venv/bin/python`.
Do not turn on real-model checks unless their assets are provisioned;
missing models must fail, not silently skip.

### EfficientAT frontend and generated ONNX reference

```sh
cmake -S . -B build-efficientat -G Ninja \
  -DLIBAUDITION_BUILD_TESTS=ON \
  -DLIBAUDITION_WITH_EFFICIENTAT=ON \
  -DLIBAUDITION_REGISTER_EFFICIENTAT_REFERENCE_TESTS=ON \
  -DPython3_EXECUTABLE=/path/to/venv/bin/python
cmake --build build-efficientat --parallel
LIBAUDITION_EFFICIENTAT_FIXTURE_DIR="$PWD/build-efficientat/fixture" \
  ctest --test-dir build-efficientat --output-on-failure --parallel 2
```

CTest Fixtures order the generated ONNX asset before discovered GTests,
and the frontend dump before the PyTorch comparison.

### Independent installed-package check

```sh
cmake -S . -B build-install -G Ninja \
  -DLIBAUDITION_BUILD_TESTS=OFF \
  -DLIBAUDITION_WITH_SPDLOG=OFF \
  -DLIBAUDITION_REGISTER_INSTALLED_PACKAGE_SMOKE=ON \
  -DLIBAUDITION_PACKAGE_SMOKE_PREFIX="$PWD/install-default"
cmake --build build-install --parallel
cmake --install build-install --prefix "$PWD/install-default"
ctest --test-dir build-install -R '^installed_package_cpp17_consumer$' --output-on-failure
```

### Speaker diarization: reference-based DER evaluation

The pinned sherpa-onnx two-speaker WAV verifies the C++ model pipeline, speaker
count and time-segment invariants. It has **no verified independent RTTM reference
in this repository**, so passing `speech_real_speaker_diarization` is **not**
a DER score or a diarization accuracy benchmark. The CI job exports the model
hypothesis as `build-sherpa/audio-fixtures/speaker-diarization-report.rttm`
and checks its syntax without comparing it to itself as ground truth.

Once an independent RTTM reference has been annotated and reviewed:

```sh
python3 tests/integration/score_diarization_rttm.py \
  --reference /path/to/independently-annotated.rttm \
  --hypothesis build-sherpa/audio-fixtures/speaker-diarization-report.rttm \
  --output build-sherpa/audio-fixtures/diarization-der.json
```

The tool scores exact non-quantized intervals, optimally remaps anonymous speaker
IDs, includes overlapping speakers by default, and reports missed, false-alarm,
and speaker-confusion durations. Optional flags `--collar SECONDS` (full
boundary exclusion width), `--skip-overlap` and `--max-der THRESHOLD` must be
chosen and documented before comparing results across runs. DER can exceed
100% if false alarms are high. Missing/malformed references cause a hard failure.

#### Independently annotated VoxConverse recordings (optional local benchmark)

For a real DER number, use a versioned, independently annotated corpus
such as [VoxConverse v0.3](https://github.com/joonson/voxconverse).
Its `dev/` and `test/` directories provide source-of-truth RTTM
annotations; the corresponding WAV archives are linked from its README.
The corpus is **not downloaded in hosted CI**: its large clips are unsuitable
for the mandatory smoke run. Cite Chung et al., *Interspeech 2020*,
when reporting metrics. Follow the dataset's licensing and attribution terms.

With the Pyannote segmentation and 3D-Speaker embedding ONNX files already
provisioned for the Sherpa build, choose one VoxConverse WAV and its matching
RTTM file (both must use the same recording basename, e.g. `abjxc`).
If necessary, convert the audio to the backend-required 16-kHz mono WAV,
preserving the original duration:

```sh
ffmpeg -hide_banner -loglevel error -i /data/voxconverse/dev/audio/abjxc.wav \
  -ac 1 -ar 16000 -c:a pcm_s16le /tmp/abjxc.wav

build-sherpa/tests/sherpa_real_speaker_diarization \
  build-sherpa/diarization-segmentation/model.onnx \
  build-sherpa/diarization-embedding.onnx \
  /tmp/abjxc.wav \
  build-sherpa/audio-fixtures/abjxc-report.txt --benchmark

python3 tests/integration/score_diarization_rttm.py \
  --reference /data/voxconverse/dev/abjxc.rttm \
  --hypothesis build-sherpa/audio-fixtures/abjxc-report.rttm \
  --output build-sherpa/audio-fixtures/abjxc-der.json
```

`--benchmark` disables only the **fixture-specific** assertion that exactly
two speakers must be found. It still verifies segment validity, the audio
format, and the output RTTM schema. It does not assign or replace ground
truth. For reproducible corpus-level research, record exact split,
reference version, model hashes, resampling, scoring collar, overlap policy,
and per-recording results; **do not** infer corpus-wide DER from one sample.

#### Real reference-backed DER smoke (independent 10-second example)

The hosted Sherpa CI adds `speech_real_speaker_diarization_reference_der`: it
downloads a short public audio clip and its separate, independently labeled
`ground_truth_files` RTTM from the
[Digital Working Group speaker-diarization toolkit](https://github.com/Digital-Working-Group/speaker-diarization),
derived from the FHS Brain Aging Program examples. Both assets are fetched
from the exact upstream commit
`9d9ebfcdc08cde1831be697ff5fb22269737a28d` and checked against their
Git blob hashes before use. The ground-truth file's original recording ID
`waveform` is renamed to the matching WAV stem for the scorer; speaker labels
and time boundaries remain unchanged. Audio is resampled to mono 16 kHz by
FFmpeg for the real Sherpa ONNX backend.

This provides a **genuine observed DER with independently supplied labels**,
not a self-comparison of model RTTMs. The CI uploads
`audio-fixtures/diarization-reference/der.json`, the reference annotation,
the model hypothesis RTTM and pinned source hashes. In [CI #1060](https://github.com/ARDev1161/libaudition/actions/runs/38630951026),
this exact input produced **DER = 0.056604139 (5.66%)** under a zero-second
collar with reference overlap included: 0.440405968 seconds missed speech,
0.000030838 seconds false alarm, zero speaker-confusion seconds, and
7.781 reference speaker-seconds. Two reference speakers were matched to two
hypothesized speakers. This ten-second fixture is only an integration and
scoring **diagnostic**: it has no calibrated `--max-der` quality gate and
cannot establish population accuracy or generalization to VoxConverse. Keep the original toolkit attribution when
reusing the diagnostic.

#### Corpus-level DER: fail-closed multi-recording evaluation

The corpus scorer matches each independent reference RTTM to its hypothesis
by recording ID and refuses missing, extra or mismatched files. **Headline
corpus DER is the reference-speech-weighted micro-average**:
`Σ(missed + false_alarm + confusion) / Σ(reference_speaker_seconds)`.
A simple average of per-recording DERs is also saved, explicitly labeled as
diagnostic only. Scores and component durations are written per recording.

When WAVs have already been evaluated, score the complete RTTM collection:

```sh
python3 tests/integration/score_diarization_corpus.py \
  --reference-dir /data/voxconverse/dev \
  --hypothesis-dir /data/predicted-rttm \
  --output /data/corpus-der.json
```

For a complete local model-backed run on an annotated collection of
**already converted mono 16-kHz WAVs**, provide the binary and model paths.
Use a **fresh, empty** prediction directory (existing outputs cannot
silently contaminate the benchmark):

```sh
python3 tests/integration/score_diarization_corpus.py \
  --reference-dir /data/voxconverse/dev \
  --wav-dir /data/voxconverse/mono16k \
  --hypothesis-dir /data/voxconverse/predicted \
  --diarizer build-sherpa/tests/sherpa_real_speaker_diarization \
  --segmentation-model build-sherpa/diarization-segmentation/model.onnx \
  --embedding-model build-sherpa/diarization-embedding.onnx \
  --collar 0.25 \
  --output /data/voxconverse/results.json
```

The reference RTTMs must use the same IDs as the WAV basenames. For
compatibility with published results, document the corpus split, exact model
hashes, collar, overlap policy and resampling. The scorer fails on missing
ground truth or unmatched predictions; it never substitutes inferred
speaker turns as reference labels. A mandatory CTest
`speech_diarization_corpus_scoring_selftest` verifies that a 2s clip with
50% DER and an 8s perfect clip aggregate to **10% corpus DER**, not the
25% unweighted recording mean.

The reference-independent algorithm self-test runs through:
`ctest --test-dir build-sherpa -R '^speech_diarization_scoring_selftest$' --output-on-failure`.
It checks speaker permutation, false alarms, misses, speaker confusion,
overlapping speech, collar exclusion, and malformed reference records.

## Generic ODAS array geometry (opt-in)

The same real-corpus evaluator can now accept an `array` object in
`corpus.json`; without it, the existing six-channel ReSpeaker fixture
profile remains the backward-compatible default:

```json
"array": {
  "microphones": [
    {"channel":0, "xyz_m":[-0.05, 0.0, 0.0]},
    {"channel":1, "xyz_m":[ 0.05, 0.0, 0.0]},
    {"channel":2, "xyz_m":[0.0, -0.05, 0.0]},
    {"channel":3, "xyz_m":[0.0,  0.05, 0.0]}
  ]
}
```

This example is illustrative, **not LOCATA geometry**. Use measured
coordinates and recording channel order from the actual array. Explicit
geometry bypasses the ReSpeaker angular mask. The offline runner accepts
repeated `--mic CHANNEL:X:Y:Z` arguments (metres) and checks that each
selected channel exists. The current ODAS integration path still requires
16-kHz PCM16 and 128-frame-aligned WAV; other sampling rates and dataset
coordinate transforms need explicit conversion with documented provenance.
LOCATA is not yet imported or benchmarked by the default GitHub CI.

## Real ReSpeaker recordings (opt-in)

The `audio_real_respeaker_corpus` CTest uses **measured, independently
annotated microphone captures**, not the synthetic spatial generator.
The dataset is never downloaded implicitly. Each clip must be six-channel
16 kHz PCM16 WAV, channels 1–4 carrying the microphone signals in the
existing ReSpeaker USB 4-Mic profile ordering (channels 0 and 5 remain
the reference channels). Frame count must be divisible by 128.

### Capture and import protocol

Place the array on a fixed mount and record the microphone orientation:
+X is the zero-angle direction and +Y is positive rotation. Measure the
speaker bearing independently of ODAS (for example, from measured XY
coordinates in the array frame); include distance, room description,
microphone height, orientation, gain and source type in your experiment
notes. These fields are not treated as ground truth unless explicitly
measured. Preserve the raw six-channel ReSpeaker PCM16 capture rather
than downmixing, remapping, or changing the channel order. For typical
ALSA devices an initial capture command is:

```bash
arecord -l
arecord -D hw:CARD,DEVICE -f S16_LE -r 16000 -c 6 -t wav capture.wav
```

Replace `CARD,DEVICE` with the actual ALSA device numbers and verify the
channel layout of your firmware. Stop recording with Ctrl+C. The importer
requires an integer multiple of 128 frames; use a capture tool that preserves
that alignment or trim excess complete tail frames *losslessly* while
maintaining the reference time origin. Never resample or silently convert.

For each take, create an independently measured annotation JSON:

```json
{
  "id": "room01_bearing030_take01",
  "intervals": [
    {"source_id":"speaker_a", "start_s":1.0, "end_s":3.0,
     "azimuth_deg":30.0}
  ]
}
```

Import the original WAV and validate annotations (duplicate IDs and
overwriting existing WAVs are rejected):

```bash
python3 tests/integration/import_respeaker_recording.py \
  --wav capture.wav --annotation take01.json --corpus /data/respeaker
```

The importer copies bytes without changing the signal, computes SHA-256,
validates annotations and appends the recording to `corpus.json`. A useful
capture matrix is silence, several fixed bearings around 360 degrees,
near/far distances, moving azimuths, two simultaneous emitters, varying
reverberation and background noise. Record *actual* start/end times and
positions; do not substitute nominal intended angles for measured labels.
For moving sources, linear interpolation is an approximation and should
only be used with a controlled, approximately constant-angular-speed path.
A future timestamped trajectory format would be preferable for arbitrary
motions.

Create a corpus folder containing `corpus.json` and WAV files. Example
(reference azimuths are illustrative placeholders, **not** measurements):

```json
{
  "schema": "libaudition.respeaker-corpus.v1",
  "recordings": [
    {
      "id": "room1_static_01",
      "wav": "room1_static_01.wav",
      "sha256": "REPLACE_WITH_64_LOWERCASE_HEX_SHA256",
      "intervals": [
        {"start_s": 1.0, "end_s": 4.0, "azimuth_deg": 30.0}
      ]
    }
  ]
}
```

The azimuth is measured in the microphone-array frame, with zero pointing
along +X and positive angles toward +Y (atan2(y, x)); ensure microphone
orientation, source position and their measurement uncertainty are logged.
Intervals for the **same source_id** must be ordered and non-overlapping;
intervals with different source IDs may overlap and must remain within WAV
duration. Static intervals use `azimuth_deg`. Moving sources use both
`azimuth_start_deg` and `azimuth_end_deg` and are interpolated via the
shortest circular angular arc. Example of two simultaneous sources:

```json
"intervals": [
  {"source_id":"speaker_a", "start_s":1.0, "end_s":4.0,
   "azimuth_start_deg":-40, "azimuth_end_deg":40},
  {"source_id":"speaker_b", "start_s":2.0, "end_s":4.0,
   "azimuth_deg":100}
]
```

For each 128-sample hop, the evaluator assigns distinct ODAS observations
to simultaneously active reference sources, minimizing summed angular error
among maximum-cardinality matches; one track cannot count toward two
sources at the same time. Reported coverage is positional detection
coverage. The evaluator now also reports `adjacent_hop_id_switches` when
the assigned ODAS track ID changes between consecutive observed hops and
`id_changes_after_gap` when an ID changes after one or more missed hops.
These are operational continuity diagnostics, not standardized CLEAR MOT
IDSW/MOTA or identity-precision scores. The evaluator still does not measure
elevation, reverberation level or source separation quality. Track assignment
uses directions only, so close or crossing sources can make identity scores
ambiguous. Keep those uncertainties explicit when interpreting results.

```bash
cmake -S . -B build-odas -G Ninja \
  -DLIBAUDITION_WITH_ODAS=ON \
  -DLIBAUDITION_BUILD_TESTS=ON \
  -DLIBAUDITION_RESPEAKER_CORPUS_MANIFEST=/data/respeaker/corpus.json
cmake --build build-odas --target audition_offline_integration
ctest --test-dir build-odas -R '^audio_real_respeaker_corpus$' \
  --output-on-failure --no-tests=error
```

The evaluator checks the SHA-256 of every recording, WAV format, annotation
ranges, ODAS runner execution, output consistency and zero recorder drops.
It writes each clip's raw observations, outputs and `metrics.json`, plus
a corpus `report.json`, under `build-odas/tests/real_respeaker_results/`.
Report fields include per-interval track coverage, median nearest-track
3D angular error and median nearest SSL-proposal error. Per-hop one-to-one matched direction metrics are diagnostics; they do\n**not** score persistent track identities across hops. There are no hardcoded DOA acceptance thresholds for real captures.
Optional manually calibrated thresholds are supported by invoking
`tests/integration/validate_respeaker_corpus.py` directly with
`--max-median-error-deg` and `--min-track-coverage`; do not set them
from synthetic CI performance.

## Coverage and caveats

- GTest and deterministic regression checks run without downloaded models.
- Backend-specific tests require their CMake backend enabled. The CI matrix
  builds those configurations separately.
- Model-backed tests are genuine inference on ONNX weights. Synthetic and
  generated speech fixtures do **not** prove performance on natural speech.
- ESC-10 checks operate on attributed recordings; a four-class diagnostic is
  not an AudioSet mAP benchmark.
- Real six-channel ReSpeaker DOA accuracy still requires a capture dataset
  with independently measured source directions and device orientation.
  No fabricated hardware thresholds should be added to CI.
- Timing gates should run on a stable dedicated runner rather than assuming
  that shared GitHub-hosted runners have deterministic latency.

## Debugging

Use `ctest --test-dir <build> -N` to see the registered tests for that
configuration. CTest automatically includes required fixture setup tests when
filtering by regex/label. Keep the JSON/audio outputs from failing runs rather
than rerunning on different inputs.
