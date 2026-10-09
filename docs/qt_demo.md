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
shows which external assets are required. Model-specific runner panels can be
added on top of this demo without changing libaudition itself.

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

The self-test executes real dependency-free DSP, SPL calibration, range-prior,
temporal-smoothing, source-identity, and AcousticEvent code.
