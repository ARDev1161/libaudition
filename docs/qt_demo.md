# Qt demo application

`libaudition_qt_demo` is an optional Qt Widgets playground for exploring the
public libaudition API without ROS 2 or audio_nav2.

It is deliberately isolated from the library: Qt is required only when
`LIBAUDITION_BUILD_QT_DEMO=ON`.

## Build

Qt 5 and Qt 6 Widgets are both supported through CMake's version-independent
Qt discovery.

On Ubuntu/Debian with Qt 5:

```bash
sudo apt install qtbase5-dev

cmake -S . -B build-qt \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_QT_DEMO=ON \
  -DLIBAUDITION_BUILD_EXAMPLES=OFF

cmake --build build-qt -j
./build-qt/demo/qt/libaudition_qt_demo
```

## Tabs

### Overview

A feature map of the library and the boundary between libaudition and consuming
applications such as audio_nav2.

### Audio / DSP

Interactive synthetic waveform generation and real libaudition calls for:

- `dsp::analyzeQuality()`,
- RMS and dBFS,
- peak,
- DC offset,
- crest factor,
- clipping ratio,
- explicit-noise-floor SNR,
- `dsp::AudioFrontend` gain/DC/fractional-delay processing.

### SPL / Range

Interactive:

```text
dBFS
  -> SoundPressureLevelCalibrator
  -> calibrated dB SPL
  -> SoundLevelRangePriorEstimator
  -> range mean / variance
```

The controls expose source-level and propagation uncertainty and the log-distance
path-loss exponent.

### Smoothing

Runs `TemporalSpatialTrackSmoother` on an editable comma-separated sequence of
range measurements and displays the evolving mean, variance, and range
provenance.

### Source identity

Simulates `SpatialTrackId` handoff and persistent `AcousticSourceId`
re-identification. Fingerprint vectors and association thresholds are editable.

### AcousticEvent

Exercises the explicit `AcousticEventAssembler` begin/update/finish lifecycle
with track geometry, classification, and transcript annotations.

### Pipeline

Runs a small synthetic end-to-end dependency-free chain:

```text
dBFS
  -> SPL calibration
  -> acoustic range prior
  -> SpatialTrack
  -> temporal smoothing
  -> persistent AcousticSourceId
  -> AcousticEvent
```

### Backends

Shows the build-time availability and role of optional adapters:

- ODAS,
- libsamplerate,
- WebRTC AEC3,
- sherpa-onnx speech/speaker/denoising/TTS/audio-tagging,
- CLAP,
- AASIST,
- WORLD,
- GTSAM.

Model-backed adapters are intentionally not given guessed model paths. The table
shows which external assets are required; model-specific runner panels are in
the Backend workbench tab.

## Build with optional backends

The status table reflects the same CMake switches used by the library. For
example:

```bash
cmake -S . -B build-qt \
  -DLIBAUDITION_BUILD_QT_DEMO=ON \
  -DLIBAUDITION_WITH_ODAS=ON \
  -DLIBAUDITION_WITH_GTSAM=ON \
  -DLIBAUDITION_WITH_WORLD=ON
```

The GUI itself does not make these dependencies mandatory.

## Headless smoke test

The demo has a small non-interactive mode for CI:

```bash
QT_QPA_PLATFORM=offscreen \
  ./build-qt/demo/qt/libaudition_qt_demo --self-test
```

Successful output:

```text
qt-demo-self-test=ok
```

### Real-model GUI smoke tests

With optional CLAP/AASIST model fixtures available, the same executable can
exercise the actual Qt buttons (not a parallel backend API path). It generates
a WAV at the model's exact required sample rate, fills the file selectors in
the workbench, clicks the button, validates the displayed output, and prints
elapsed milliseconds without imposing an unstable CI performance threshold.

```bash
QT_QPA_PLATFORM=offscreen \
LIBAUDITION_TEST_CLAP_AUDIO_MODEL=/path/to/clap_audio.onnx \
LIBAUDITION_TEST_CLAP_TEXT_MODEL=/path/to/clap_text.onnx \
LIBAUDITION_TEST_CLAP_TOKENIZER=/path/to/tokenizer.json \
./build-workbench/demo/qt/libaudition_qt_demo --model-self-test=clap

QT_QPA_PLATFORM=offscreen \
LIBAUDITION_TEST_AASIST_MODEL=/path/to/aasist.onnx \
./build-workbench/demo/qt/libaudition_qt_demo --model-self-test=aasist
```

CI reuses the pinned, checksum-verified model fixtures in the CLAP and AASIST
jobs to run both tests, in addition to the existing direct backend tests. These
smoke tests validate real inference and Qt wiring, not perceptual accuracy or
UI responsiveness under sustained load.

The self-test executes real dependency-free DSP, SPL calibration, range-prior,
temporal-smoothing, source-identity, and AcousticEvent code. It also verifies
the application and workbench tab tree, checks the actual enabled runner
actions for each configured backend, and tests stereo PCM16 WAV save/load
and explicit mono channel selection without any external model files.

CI runs this headless test in the standalone Qt job and in each
backend-enabled Qt build, so an optional runner is checked with its
actual compile-time feature flags. Model inference and audio quality still
require separate fixtures and manual/automated end-to-end tests.


## Real backend workbench

The **Backend workbench** tab runs concrete libaudition backend classes when
those adapters are enabled at build time.

### WAV loader

The demo owns a small explicit file adapter for uncompressed RIFF/WAV input:

- PCM 8/16/24/32-bit;
- IEEE float32;
- arbitrary channel count;
- interleaved conversion into `AudioBuffer`.

The loader does **not** resample, normalize, or automatically downmix. Model
panels expose an explicit channel selector when a mono backend is used. A
sample-rate mismatch is allowed to fail through the backend contract rather
than being silently corrected.

The demo can also write interleaved PCM16 WAV output for generated audio.

### WORLD runner

When built with:

```bash
-DLIBAUDITION_WITH_WORLD=ON
```

the WORLD panel loads a selected WAV channel and runs both:

- `WorldVoiceTraitsEstimator`;
- `WorldAcousticAnalyzer`.

The GUI exposes DIO+StoneMask/Harvest selection, F0 bounds, and frame period,
then reports pitch statistics, voiced-frame count, FFT size, feature dimensions,
and a preview of the F0 contour.

### AASIST runner

When built with:

```bash
-DLIBAUDITION_WITH_AASIST=ON
```

the panel accepts an AASIST ONNX path plus mono WAV input and runs
`AasistAuthenticityDetector`.

Raw bona-fide/spoof scores are shown separately from probabilities. Optional
Platt slope/intercept controls explicitly enable calibrated probabilities.

The backend's existing mono 16 kHz / 64600-frame contract remains authoritative.

### CLAP runner

When built with:

```bash
-DLIBAUDITION_WITH_CLAP=ON
```

the panel supports two real operations:

- audio embedding through `ClapAudioEmbedder`;
- open-vocabulary classification through `ClapOpenVocabularyClassifier`.

For open-vocabulary use, select the audio ONNX, text ONNX, tokenizer JSON, and
enter a comma-separated candidate set. Returned probabilities are explicitly
candidate-relative.

CLAP keeps its strict mono 48 kHz input contract; the demo does not hide a
resampler in front of it.

### Sherpa runner

When built with:

```bash
-DLIBAUDITION_WITH_SHERPA=ON
```

the current runner provides:

- Silero or TEN VAD over a WAV file in model-sized blocks;
- offline Whisper ASR with encoder/decoder/tokens;
- streaming ASR over explicitly chunked WAV input, including partial-result and
  endpoint status;
- keyword spotting with selectable online model family, inline keywords,
  score/threshold controls, and hit offsets;
- Whisper spoken-language identification;
- Zipformer or CED audio tagging with configurable top-k output;
- GTCRN or DPDFNet speech denoising in offline or streaming mode. Streaming
  mode uses the backend-reported `preferred_frame_count`, and denoised output
  is written as PCM16 WAV.

When TTS is also enabled:

```bash
-DLIBAUDITION_SHERPA_ENABLE_TTS=ON
```

a VITS/Piper synthesis panel appears. It accepts model/tokens/espeak-ng data,
text, language, speed, and speaker ID, then writes synthesized PCM16 WAV.

Dedicated speaker panels also cover:

- speaker embedding extraction with dimension, norm, quality, and vector preview;
- end-to-end speaker verification from a reference and candidate WAV;
- in-memory speaker identification from one or more enrollment WAVs per speaker;
- offline speaker diarization with editable clustering and segmentation controls.

The enrollment index used by the demo is deliberately temporary; it demonstrates
the computational `ISpeakerIdentifier` contract rather than pretending to be
application persistence.

### ODAS runner

When built with:

```bash
-DLIBAUDITION_WITH_ODAS=ON
```

the ODAS panel processes a multichannel WAV one configured hop at a time using
`OdasSpatialEngine`.

The microphone geometry and 0-based input-channel mapping are editable. The
pre-filled four-microphone cross is only an example and is not treated as a
hidden ReSpeaker calibration.

The output reports processed hops, hops containing tracks, maximum simultaneous
tracks, and the last azimuth/elevation/activity for each observed transient
track ID.

### libsamplerate runner

When built with:

```bash
-DLIBAUDITION_WITH_LIBSAMPLERATE=ON
```

the runner resamples a WAV through the public `IAudioResamplerSession`,
including `flush()`, preserves the input channel count, exposes all five
libsamplerate converter modes, and writes PCM16 WAV output.

### WebRTC AEC3 runner

When built with:

```bash
-DLIBAUDITION_WITH_WEBRTC_AEC3=ON
```

the runner accepts separate captured and playback/reference WAVs. It feeds
exact 10 ms frames to `IEchoCancellerSession`, exposes stream delay and the
initial-filter controls, reports ERL/ERLE/delay metrics when available, and
writes the processed capture to WAV. The backend contract remains visible:
capture/reference sample rates must match and must be 16, 32, or 48 kHz.

### GTSAM fusion runner

When built with:

```bash
-DLIBAUDITION_WITH_GTSAM=ON
```

the runner accepts acoustic bearing, scalar-range, and position observations,
then calls `GtsamSpatialFusion` and displays the resulting 3D mean and full
3x3 covariance. Huber robustification, iteration count, bearing sigma, and
fallback initialization range are editable. This panel intentionally remains
acoustic-only; camera, radar, robot-state, and other cross-modal fusion belong
in the application consuming libaudition.

## Asynchronous CLAP and AASIST inference

CLAP embedding, CLAP open-vocabulary classification, and AASIST authenticity
inference execute on a QtConcurrent worker, including WAV parsing and model
loading. UI controls are read and copied **before** starting; worker closures
never access widgets. A single panel allows only one active invocation.

While a job runs, the panel shows an **indeterminate** progress indicator
because the ONNX adapters do not expose a meaningful percentage. The action
buttons remain disabled until the backend returns, so requests cannot pile up.
**Discard result** suppresses the response but does **not** terminate a model
inference already executing. The panel explicitly states this limitation;
it remains busy until the backend has safely completed, then becomes usable
again. Closing the panel destroys GUI callbacks without a worker dereferencing
its widgets. No ROS 2 or sensor dependencies are introduced.

The pinned-model headless Qt tests additionally verify that a timer callback
runs while each backend executes and that actions are disabled until
completion. AASIST exercises the discard path and recovery. Qt's Concurrent
module is required only for the optional demo, not for libaudition itself.

## Building the backend workbench

Example:

```bash
cmake -S . -B build-workbench \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_QT_DEMO=ON \
  -DLIBAUDITION_WITH_ODAS=ON \
  -DLIBAUDITION_WITH_WORLD=ON \
  -DLIBAUDITION_WITH_AASIST=ON \
  -DLIBAUDITION_WITH_CLAP=ON \
  -DLIBAUDITION_WITH_SHERPA=ON \
  -DLIBAUDITION_SHERPA_ENABLE_TTS=ON \
  -DLIBAUDITION_WITH_LIBSAMPLERATE=ON \
  -DLIBAUDITION_WITH_WEBRTC_AEC3=ON \
  -DLIBAUDITION_WITH_GTSAM=ON

cmake --build build-workbench -j
./build-workbench/demo/qt/libaudition_qt_demo
```

Only adapters enabled by CMake are linked into the executable. Disabled runner
tabs remain visible and explain which option is required.
