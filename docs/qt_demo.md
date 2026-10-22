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

### Acoustic scene (3D ODAS Studio-style viewer)

#### Live ReSpeaker capture via ALSA (Linux)

The **Acoustic scene** tab also supports direct multichannel hardware capture,
without saving a WAV. ALSA belongs only to the Qt demo, never the library core.

CMake 4.x compatibility: the pinned ODAS upstream declares a legacy
`cmake_minimum_required(VERSION 2.4.6..3.16)`. The libaudition
`ExternalProject` automatically passes
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` into ODAS's own configuration. This
is required for modern Manjaro and does not change the pinned ODAS revision.
The ODAS CI job explicitly validates a CMake 4.1.2 build.

Install Linux ALSA development files (Ubuntu/Debian: `libasound2-dev`,
`libfftw3-dev`, `libconfig-dev`, `libpulse-dev`; Arch/Manjaro: `alsa-lib`
and ODAS build prerequisites). Configure with:

```bash
cmake -S . -B build-qt -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_QT_DEMO=ON -DLIBAUDITION_WITH_ODAS=ON
cmake --build build-qt -j4
./build-qt/demo/qt/libaudition_qt_demo
```

Connect the **ReSpeaker 4-Mic USB v2.0**, open **Acoustic scene**, then:

1. Click **Refresh ALSA devices**; the list shows real capture PCMs
   (`hw:card,device`), prioritizing names that match ReSpeaker/Seeed/4 Mic
   Array. If not found, check `arecord -l` and enter `hw:N,M` manually.
2. Verify **Channels = 6**, **Rate = 16000 Hz**, microphone mapping
   `1,2,3,4` (zero based), and accurate XYZ microphone positions (meters).
   The default geometry is demonstrative, not a verified hardware preset.
3. Press **Start live capture**. ALSA captures interleaved S16_LE; the
   worker feeds each complete ODAS hop and the sphere updates at ~20 Hz.
   Status reports hop count, tracks and capture overrun recovery count.
4. **Stop live capture** before starting offline WAV replay. Closing the
   window also joins/stops the capture worker.

**ODAS ReSpeaker angular profile:** The checkbox **Apply upstream
ReSpeaker USB 4-Mic 80°/100° angular profile** is enabled by default
for the demonstration setup. It populates ODAS's microphone directivity
and +Z spatial filter from the upstream ReSpeaker preset. These values
are polar-angle *all-pass/no-pass transition limits*, **not** a hard
±10° azimuth/elevation window and not microphone gain.
The generic libaudition defaults are 180°/180°, admitting a larger
angular region and possibly more mirror/ghost hypotheses for a planar
array. Compare both modes with the same speaker position; neither
guarantees true elevation resolution from coplanar microphones.
Switching while live requires Stop → toggle profile → Start.

Upstream ODAS config:
https://github.com/introlab/odas/blob/master/config/odaslive/respeaker_usb_4_mic_array.cfg
ALSA device parameters must support **exactly** the requested sample rate,
number of channels and PCM format; there is no hidden resampling or
downmixing. Inspect capabilities with
`arecord -D hw:N,M --dump-hw-params -d 1 /dev/null`.
USB device removal, busy PCMs and unsupported sample formats are surfaced
as UI errors, rather than silently switching to another capture device.

The worker accumulates short ALSA reads, resets ODAS on XRUN recovery, and
publishes only the latest tracked-source snapshot (bounded memory); the
capture thread never updates GUI widgets directly.

#### Live source sound classification (optional Sherpa backend)

The **Tracked sources** table now has an **Activity type** column.
**Classification** in a track's 3D hover tooltip and selected-source
details uses the same result, e.g. a model-provided sound class with its
probability. This is distinct from ODAS's numeric *Activity* (SST
tracking activity), and is not inferred from azimuth or signal gain.

For real sound-event labels (speech, music, machinery, environmental
sounds depending on the trained model), rebuild the Qt demo with the
Sherpa backend **in addition to ODAS**:

```bash
cmake -S . -B build-qt -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_QT_DEMO=ON \
  -DLIBAUDITION_WITH_ODAS=ON -DLIBAUDITION_WITH_SHERPA=ON
cmake --build build-qt -j4
```

On the **Acoustic scene** tab, check **Classify source audio with Sherpa**,
choose **Zipformer**, **CED**, or **YAMNet**, select a model variant where available,
then choose the matching **ONNX model** and **labels file** before starting capture.
The conditional **Model variant** selector offers CED Tiny/Mini/Small/Base and
Zipformer Small/Standard (FP32/INT8). It is a UI selection hint: model weights
for Sherpa are **not downloaded automatically** and the chosen variant does not
change the ONNX parser; the user must supply the corresponding files.
Changing family or variant clears manual model and label paths so an old model
is not silently reused. Returning to YAMNet restores the default demo asset paths.
The ONNX and labels paths must exist, and the capture rate must be
16 kHz. The demo does **not** install or silently download a model.
The existing standalone **Backend workbench → Audio tagging** panel can
be used to verify the model on a WAV first.

Enabling this option makes ODAS produce separate mono audio per tracked
source ID (SSS); a bounded background thread classifies source-specific
2-second windows, without blocking the ALSA/ODAS capture thread.
The table shows **Analyzing…** until a model result arrives. Predictions
are cached by track ID and expire after about five seconds to avoid
sticking to reused IDs. If the model cannot load or inference fails, the
GUI displays the tagging error while capture continues. Model scores
reflect that model's outputs, not a universal confidence calibration.

When Sherpa is absent, the checkbox is disabled and the column says
**Not configured**; no label is fabricated from track activity.
Classification in this release applies to **live ALSA** tracks, not
offline WAV replay. The click-to-ASR feature remains separate future work.

#### If the 3D sphere initially remains empty

An empty sphere does **not** imply ALSA has failed: the GUI used to display
only the confirmed ODAS SST tracks, which may take time to initialize or
disappear when acoustic activity is low. The live readout now exposes two
independent stages:

- **Raw ALSA input levels** for every hardware channel: RMS and peak in
  dBFS over the last display interval, before ODAS. Watch the four mapped
  microphone channels when speaking/clapping; near -180 dBFS means digital
  silence. Compare ch0/ch5 too, but they are not mapped as microphones.
- **ODAS SSL proposals**: outlined small diamonds on the unit sphere,
  separate from SST's larger filled/ID-labeled persistent tracks. A raw
  score >0 represents a positive potential-source candidate, **not a
  calibrated probability**. The readout also shows the proposal count
  and strongest raw score.

If mapped channels are active but SSL candidates remain zero, check
microphone order/geometry and the ODAS SSL thresholds. If there are SSL
candidates but SST stays empty or produces IDs with near-zero activity,
investigate tracker initiation/thresholds and room reverberation. The
published ODAS ReSpeaker USB 4-Mic configuration includes the
`1,2,3,4` (zero-based) mapping and a 32 mm microphone radius, but this
still needs orientation and real-hardware verification:
https://github.com/introlab/odas/blob/master/config/odaslive/respeaker_usb_4_mic_array.cfg

Because all four microphones are coplanar, elevation estimates can be
ambiguous and should be verified experimentally. Neither SSL proposals nor
SST tracks should be interpreted as metric position estimates.

**Not yet implemented:** raw `odas.cfg` import, ReSpeaker microphone geometry
auto-calibration, source classification, click-to-ASR, audio monitor/playback,
and source-separated live recording. Tracks indicate **directions**, not
metric distances.

Hardware-independent CI exercises ALSA discovery and an intentionally
invalid PCM error/restart path. This does not substitute for hands-on
capture and DOA validation with the physical ReSpeaker.

The new first tab renders a genuine **three-dimensional unit sphere**, not a
semicircle: latitude and longitude grids distinguish elevations above (blue)
and below (orange) the equatorial plane; X (forward), Y (left), and Z (up)
axes follow the library's sensor-frame convention. Mouse drag orbits the
camera, wheel zooms and double-click resets. Hover over a source for ID,
azimuth, **elevation**, activity and an explicit status for unavailable
classification. Click to select; numbered markers retain fading direction
trails. The markers represent *directions only*, not distances.

Use **Show 3D example** to demonstrate three explicitly synthetic directions,
including a source below the equator. This needs no third-party backend.

With `LIBAUDITION_WITH_ODAS=ON`, **Analyze WAV with ODAS** takes a multichannel
RIFF/WAV and a zero-based microphone channel map and XYZ geometry, processes
ODAS hops on a background worker, then replays captured tracker snapshots at
approximately 20 frames per second. Replay animates source **direction
metadata**, not sound output. The default `1,2,3,4` mapping corresponds to a
common ReSpeaker 4-Mic USB v2.0 six-channel recording but the example XYZ
coordinates **must be checked against the actual microphone geometry**.
The raw upstream `odas.cfg` format is not imported by this first viewer.

**Not connected yet:** real-time ALSA device capture/auto-discovery, optional
source classification, click-to-ASR from separated audio, and ODAS potential
energy heatmaps. The hover tooltip clearly says `Classification: not available`.
Existing diagnostic panels are unchanged.


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

## Asynchronous language ID, audio tagging and denoising

The remaining Sherpa model-backed Qt panels — Whisper language identification,
Zipformer/CED audio tagging, and offline/streaming GTCRN/DPDFNet denoising —
use the same Qt worker as other model runners. Model selection, sample-rate
settings, selected channel and output path are snapshotted before dispatch;
no worker closure reads a QWidget. Denoising WAV export is a background
side effect; discarding the result does **not** undo a written WAV.

Headless Sherpa smoke tests assert the asynchronous error path and GUI
recovery for these tabs. Runtime accuracy remains unverified without pinned
model-specific fixtures.

## Asynchronous speaker intelligence

Speaker embedding, speaker verification, ephemeral WAV-based enrollment and
identification, and diarization all execute WAV loading and Sherpa inference
on a Qt worker. Model selections, speaker enrollment text, confidence and
clustering parameters are snapshotted before dispatch; workers access no
Qt widgets. Each panel permits one operation in flight and offers the same
non-interrupting **Discard result** control.

Headless Sherpa GUI error-path checks cover all four panels and their
transition back to idle state. No heavyweight speaker inference fixtures are
pinned for this test, so the GUI error checks alone do **not** establish
speaker-recognition or diarization accuracy.

## Asynchronous streaming ASR and keyword spotting

Streaming ASR and keyword spotting now run WAV decoding, Sherpa session setup,
and frame-by-frame processing inside Qt worker tasks. Model family, paths,
tokenizer, sample-rate contract, chunk length and detection parameters are
snapshotted on the GUI thread. Busy state, indeterminate progress and
non-interrupting **Discard result** follow the existing async runner behavior.

The Sherpa headless `sherpa-errors` scenario also checks both panels' error
paths and action re-enabling. No pinned streaming ASR/KWS model is bundled in
this stage; successful transcript/keyword inference is **not** claimed.

## Asynchronous offline ASR and VITS/Piper TTS

Offline Whisper ASR (WAV decoding, model creation, transcription) and
VITS/Piper TTS (synthesis and WAV writing) use the same worker controller as
CLAP/AASIST/VAD. All options are snapshotted on the GUI thread before the
background invocation. The **Discard result** action only hides the result:
in particular, it **does not undo an output WAV** already written by TTS.

Without a pinned Whisper/VITS fixture, the headless `--model-self-test=sherpa-errors`
scenario verifies worker error delivery, action re-enabling, and retry on
Sherpa builds. It does **not** claim successful Whisper or TTS model inference.

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

### Sherpa VAD

The VAD subtab (Silero and TEN) uses the worker-based execution controller:
waveform loading and frame-level processing no longer block Qt event
dispatch. The `--model-self-test=silero` mode uses the already
checksum-pinned Silero VAD ONNX fixture in `sherpa-backend` CI to click
**Run VAD**, assert live event processing, and verify frame counts.
This validates runtime GUI wiring, not speech-detection accuracy.

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

## Asynchronous WORLD / ODAS runners

WORLD voice-trait and acoustic analysis and ODAS multichannel WAV hops now
run on worker threads. GUI settings are snapshotted first, and model
processing never touches Qt widgets. Both panels have indeterminate progress,
single-in-flight action guards, and result-discard semantics.

Headless `--model-self-test=world` processes an actual generated sine-wave WAV
through WORLD and checks output dimensions. `--model-self-test=odas-errors`
checks ODAS asynchronous error propagation and retry without pretending that a
synthetic microphone arrangement validates source localization performance.

## Asynchronous optional DSP and fusion runners

libsamplerate WAV resampling, WebRTC AEC3 playback/capture processing, and
acoustic-only GTSAM fusion now run on background Qt workers. All controls
are snapshotted on the GUI thread. Each panel offers a single in-flight
operation with honest indeterminate progress and non-interrupting result
discard. Resampler/AEC3 output WAV writes remain filesystem side effects
even if a result is discarded.

The headless model-fixture harness checks complete WAV resampling at 16 to
48 kHz, AEC3 processing of full 10 ms frames with WAV export, and a
position-observation GTSAM fusion run, all through the real Qt buttons.
These tests validate functionality and lifecycle, not acoustic cancellation
or spatial-estimation accuracy across real recordings.

## Real Sherpa ONNX model fixtures

Unlike the existing error-only smoke tests, these scenarios run **real
model inference via the Qt buttons**:

| Scenario | Model | Validated result |
| --- | --- | --- |
| `--model-self-test=whisper-real` | Whisper tiny.en INT8 | Encoder/decoder execute and return text/language/token fields |
| `--model-self-test=piper-real` | Piper VITS Amy Low INT8 | Generated PCM16 WAV is nonempty and valid |
| `--model-self-test=speaker-real` | NeMo TitaNet Small | Embedding, verification of identical WAV and identification |

The Whisper and speaker tests use synthetic 16-kHz tones. Therefore these
prove end-to-end execution and same-sample consistency, **not word-error
rate or speaker-discrimination accuracy**. A labeled real-speech evaluation
dataset is still necessary for accuracy claims.

Pinned assets:

- Whisper tiny.en at commit `d026532c022fa99fd789d6b32446a1df7b6bfc43`.
  INT8 encoder SHA-256: `0ce578b827c94a961aacb8fa14b02f096504b337e5c94be37c36238cbe3e8bc6`;
  decoder: `06c0e6ff6348d427e51839219d1c886c18cfdf411e629e33f5e1679bff9c1527`.
  The tokenizer comes from that same fixed git revision.
- NeMo TitaNet Small SHA-256:
  `ad4a1802485d8b34c722d2a9d04249662f2ece5d28a7a039063ca22f515a789e`.
- Sherpa release Piper Amy Low INT8 archive SHA-256:
  `93070ac9fadf512e56c46bdd0c5d2ce96b424fdc4e683d560167410bd2c4df7d`.

Set `QT_QPA_PLATFORM=offscreen` and the corresponding
`LIBAUDITION_TEST_WHISPER_ENCODER`, `LIBAUDITION_TEST_WHISPER_DECODER`,
`LIBAUDITION_TEST_WHISPER_TOKENS`, `LIBAUDITION_TEST_PIPER_DIR` or
`LIBAUDITION_TEST_SPEAKER_MODEL` environment variables. Missing assets
fail the test explicitly; no silent skip is allowed.
