# YAMNet ONNX sound-event classification

The optional `audition::backend_yamnet` implements `IAudioClassifier` using
ONNX Runtime on CPU. It is independent of sherpa-onnx: CED/Zipformer and YAMNet
remain alternative classifiers with the same public output contract.

## Demo defaults

When the Qt demo is enabled, `LIBAUDITION_WITH_YAMNET` defaults to ON, and
`LIBAUDITION_DEMO_FETCH_YAMNET_MODEL` downloads the demo model and labels
at CMake configuration time from the pinned Hugging Face revision `f25b741`.
The Qt Acoustic Scene then selects YAMNet, prefills asset paths and checks
classification by default. Stop/restart live capture after changing selection.
The model cache is under the build tree (`demo-models/yamnet`).
Set `LIBAUDITION_DEMO_FETCH_YAMNET_MODEL=OFF` to build without downloading;
then supply compatible ONNX and class-map paths manually.

**Integrity caveat:** the current downloader pins an upstream revision but
has not yet independently verified and recorded SHA-256 hashes. Do not treat
this as a supply-chain-verified model distribution until hashes are added.

## Build

```sh
cmake -S . -B build-qt -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_QT_DEMO=ON \
  -DLIBAUDITION_WITH_ODAS=ON \
  -DLIBAUDITION_WITH_SHERPA=ON \
  -DLIBAUDITION_WITH_YAMNET=ON
cmake --build build-qt -j4
./build-qt/demo/qt/libaudition_qt_demo
```

Select **YAMNet** from **Acoustic scene → Model family**, enter the model ONNX
and its **521 class** AudioSet CSV, and enable classification. No model
artifacts are downloaded or redistributed automatically.

## ONNX model contract

- Tensor input: mono waveform samples **float32 at 16 kHz**, shape `[N]` or
  `[1,N]` (N dynamic or fixed).
- Tensor output: named `scores` preferred, shape `[frames,521]` or
  `[1,frames,521]`, float32 scores in `[0,1]`.
- Other YAMNet outputs (embeddings/spectrogram) are ignored. Graph exports
  with spectrogram/log-mel inputs are **not supported**.
- Full audio windows are passed through the ONNX graph; the graph itself
  must implement original YAMNet waveform frontend (STFT/mel). This backend
  deliberately does not approximate that frontend in C++.
- When the model specifies a fixed sample dimension, each `classify()` call
  must supply that exact number of samples. For the Qt demo, export a model
  accepting **16000** samples (one-second window) or use a dynamic waveform
  dimension.
- Frame scores are arithmetically averaged for each class, then top-K
  classes are returned, without renormalization. AudioSet classes are not
  mutually exclusive. They are model scores, not calibrated real-world
  event confidence.
- Class label list must have 521 entries. Supported: one label per line or
  the standard YAMNet `yamnet_class_map.csv` with
  `index,mid,display_name` columns.

## Runtime constraints

- Uses CPUExecutionProvider only. This integration does **not** provide RKNN
  NPU acceleration; that needs a separate backend and model conversion.
- Mono 16 kHz is enforced (no implicit resampling).
- The Qt demo derives the YAMNet input length from the model input tensor:
  15,600 samples for dynamic waveform exports (one 0.975-second patch),
  or the exact fixed length for fixed-shape exports. It uses **50% overlap**
  between consecutive classifications. Sherpa retains 2-second windows.
  Inference runs in a bounded background queue.
- The demo currently displays the highest-scoring class only, although the
  backend itself returns top-K.
- ODAS tracks are only sound directions. YAMNet classifies source-separated
  SSS audio; it does not supply spatial coordinates.
- No model weights are bundled. Before distributing any model, register its
  license, upstream revision and hash in `models/manifest.yaml`.

## C++ usage

```cpp
#include <audition/backends/yamnet.hpp>

// AudioView audio: mono 16 kHz, one-second (or supported model dimension).
audition::YamnetOnnxOptions options;
options.model = "/path/to/yamnet_waveform.onnx";
options.labels = "/path/to/yamnet_class_map.csv";
options.top_k = 3;
audition::YamnetAudioTagger classifier{options};
auto classes = classifier.classify(audio);
```

## Real-model adapter parity test

The `efficientat-real-weights` CI job additionally runs `yamnet_pcm16_probe`
and `tools/verify_yamnet_onnx_cpp.py` on identical **raw 16 kHz PCM16**
samples using the pinned waveform-input ONNX checkpoint. The independent
Python ONNX Runtime run computes per-patch 521-class scores and averages
them; the test fails if the C++ adapter returns different Top-10 labels
or values (absolute score tolerance 2e-5). The result is archived as
`yamnet-onnx-cpp-parity.json` in the CI real-event artifact.

This verifies **integration parity**, not that a particular real-world
sound will be recognized accurately. AudioSet label accuracy still
requires a larger event-aligned corpus and dedicated clip-level metrics.

## Behavioral verification of model weights

The CI job runs `tools/verify_yamnet_behavior.py` against the pinned ONNX
model on three official YAMNet reference stimuli, each 3 seconds at 16 kHz:
zero waveform (expected **Silence** in Top-10), reproducible uniform noise
(**White noise** in Top-10), and a 440 Hz sinusoid (**Sine wave** in Top-10).
These expectations come from the upstream
[`yamnet_test.py`](https://github.com/tensorflow/models/blob/master/research/audioset/yamnet/yamnet_test.py).
The CI archive includes `yamnet-behavior.json` with the complete outputs.
This detects gross regressions in the downloaded third-party model, but
does not replace event-aligned evaluation on microphone/ODAS recordings.
