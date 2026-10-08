# CLAP ONNX backend

The optional CLAP backend provides semantic audio embeddings without exposing
ONNX Runtime types through libaudition public APIs.

Build it with:

```bash
cmake -S . -B build-clap \
  -DLIBAUDITION_WITH_CLAP=ON \
  -DLIBAUDITION_WITH_SPDLOG=OFF
```

## Current slice

`ClapAudioEmbedder` implements `IAudioEmbedder`.

The first supported deployment contract is the LAION CLAP HTSAT-unfused audio
encoder. A known-compatible fixture is
`ConceptualMachines/magda-sample-tagger/clap_audio.onnx`:

- application input: mono 48 kHz float PCM;
- ONNX input: float32 `input_features` with shape
  `[batch, 1, 1001, 64]`;
- ONNX output: one 512-dimensional float32 semantic embedding;
- model artifact SHA-256
  `3f42f71e555b62709910b6efa66fa5879f00d9571874b12b0fa674f82dbfe332`.

The model is not distributed by libaudition. Applications provide the model
path explicitly. Model licensing/provenance remains separate from the backend
runtime license.

## Audio frontend contract

The exported HTSAT graph consumes CLAP log-mel features rather than PCM.
`ClapAudioEmbedder` therefore owns the deterministic model frontend while
keeping the public API in `AudioView` terms.

The current frontend contract is:

- mono 48 kHz PCM;
- at most 10 seconds per `embed()` call;
- shorter clips use the model-defined repeat-padding policy to the 10-second
  analysis window;
- longer clips are rejected and must be segmented explicitly by the caller;
- centered periodic-Hann STFT, FFT size 1024 and hop length 480;
- 64 Slaney-normalized mel filters over 50 Hz to 14 kHz;
- power spectrogram converted to decibels;
- final feature tensor `[1, 1, 1001, 64]`.

There is no hidden resampling or downmixing. The repeat-padding and feature
extraction are part of this model's declared frontend rather than generic audio
conversion. `EmbeddingCapabilities::audio.preferred_frame_count` reports the
10-second / 480000-frame analysis window.

At model load time the adapter verifies the ONNX input and output shapes and
float32 element types. Non-finite input samples and non-finite/zero-norm
embeddings are rejected.

The backend preserves the model output values. Prototype matching performs its
own cosine normalization, so the adapter does not silently turn similarity into
a calibrated probability.

## Execution

The initial backend intentionally supports ONNX Runtime CPU execution only.
Unsupported accelerator/provider/precision options are rejected instead of
silently ignored.

When no system ONNX Runtime is found, dependency fetching currently provides a
pinned Linux x86_64 CPU package for CI/development. Other platforms, including
ARM/RK3588, should supply an ONNX Runtime build explicitly through
`LIBAUDITION_ONNXRUNTIME_ROOT` or `ONNXRUNTIME_ROOT`.

Hardware-provider support is a later slice; the public API already remains
hardware-neutral through `ExecutionTarget`.

## Next slice

The next CLAP slice adds the matching text encoder and tokenizer, then
implements `IOpenVocabularyAudioClassifier` using candidate-relative
audio/text similarity. Text-relative scores will not be presented as calibrated
real-world event probabilities unless an explicit normalization/calibration
policy is configured.
