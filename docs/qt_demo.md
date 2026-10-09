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
  -DLIBAUDITION_SHERPA_ENABLE_TTS=ON

cmake --build build-workbench -j
./build-workbench/demo/qt/libaudition_qt_demo
```

Only adapters enabled by CMake are linked into the executable. Disabled runner
tabs remain visible and explain which option is required.
