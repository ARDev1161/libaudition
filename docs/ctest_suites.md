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
