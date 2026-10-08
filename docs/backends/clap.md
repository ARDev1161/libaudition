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
encoder exported to ONNX as a raw-waveform model. A known-compatible fixture is
`ConceptualMachines/magda-sample-tagger/clap_audio.onnx`:

- mono 48 kHz waveform input;
- one float32 input tensor;
- one float32 output tensor;
- 512-dimensional semantic embedding;
- model artifact SHA-256
  `3f42f71e555b62709910b6efa66fa5879f00d9571874b12b0fa674f82dbfe332`.

The model is not distributed by libaudition. Applications provide the model
path explicitly. Model licensing/provenance remains separate from the backend
runtime license.

## Audio and shape contract

The adapter never resamples, downmixes, pads, truncates or windows audio
implicitly. Input must already be mono at the configured sample rate.

At model load time the adapter inspects the ONNX input shape:

- rank-1 waveform tensors are supported;
- rank-2 tensors are supported when the batch dimension is one or dynamic;
- if the waveform length is fixed by the graph, it is exposed through
  `EmbeddingCapabilities::audio.preferred_frame_count` and callers must provide
  that exact frame count;
- dynamic waveform lengths accept any non-empty input.

The output must be a rank-1 embedding or a batch-of-one rank-2 embedding with
the configured dimension. Non-finite and zero-norm results are rejected.

The backend preserves the model output values. Prototype matching performs its
own cosine normalization, so the adapter does not silently renormalize the
embedding.

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
