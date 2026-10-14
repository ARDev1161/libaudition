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
