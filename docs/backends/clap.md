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

`ClapOpenVocabularyClassifier` implements
`IOpenVocabularyAudioClassifier` by pairing the same audio embedding space with
the CLAP RoBERTa text encoder. The tokenizer and text encoder remain private
backend details; no tokenizer/ONNX Runtime types leak into the core API.

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

The matching open-vocabulary path additionally uses:

- `clap_text.onnx`, SHA-256
  `c07b27204836877d5b615c103685b66ea8f21bc6b5b70a572be356125423a8bf`;
- `tokenizer.json`, SHA-256
  `4fd1d86b4f5b53f40a609fcd11c1f34024b735f870a07439d70202b98493661a`;
- RoBERTa sequence length 77;
- 512-dimensional text embeddings in the same `model_id` space as the audio
  encoder.

The current tokenizer implementation intentionally accepts ASCII candidate
labels only. Non-ASCII labels are rejected rather than silently using a
different pre-tokenization rule from the pinned RoBERTa tokenizer.

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

## Open-vocabulary probabilities

For a supplied candidate set, the classifier computes cosine similarity between
one audio embedding and each candidate text embedding. Similarities are divided
by `ClapOpenVocabularyOptions::similarity_temperature` and normalized with a
stable softmax.

The resulting `Probability` values are **candidate-relative**: they sum to one
over the labels supplied to that call. They are not calibrated estimates of the
probability that an event exists in the real world. Adding/removing candidate
labels can therefore change every reported probability.

Candidate labels must be non-empty and unique. The result is sorted by
descending candidate-relative probability. A one-label candidate set returns
probability 1.0 by construction.

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

## Verification

The dedicated CLAP CI job verifies:

- pinned SHA-256 values for audio, text and tokenizer artifacts;
- the C++ tokenizer against reference token IDs/masks produced from the same
  `tokenizer.json` by Hugging Face `tokenizers`;
- real audio ONNX inference;
- real text ONNX inference through end-to-end open-vocabulary classification;
- candidate-relative probabilities sum to one and are returned in descending
  order;
- installed C++17 consumers can compile against the public CLAP targets without
  depending on tokenizer/ONNX types.

## Follow-up

Accelerator/provider-specific execution remains a later slice. Multilingual
candidate labels also remain a follow-up because exact RoBERTa Unicode
pre-tokenization should use a Unicode-aware implementation rather than an
approximation.
