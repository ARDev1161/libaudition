# sherpa-onnx speech backend

The sherpa backend adapts `k2-fsa/sherpa-onnx` to libaudition's narrow speech
interfaces. It is optional and does not expose sherpa-onnx types from public
headers.

Pinned upstream revision:

```text
99ddefaa92129858b80a71a426903dd4215c83fa
```

## Components

| libaudition class | Interface | Native sherpa primitive |
|---|---|---|
| `SherpaVad` | `IVoiceActivityDetector` | `VoiceActivityDetector` |
| `SherpaOfflineAsr` | `IAsrEngine` | `OfflineRecognizer` |
| `SherpaStreamingAsr` | `IStreamingAsrEngine` | `OnlineRecognizer` |
| `SherpaKeywordSpotter` | `IKeywordSpotter` | `KeywordSpotter` |
| `SherpaLanguageIdentifier` | `ILanguageIdentifier` | `SpokenLanguageIdentification` |
| `SherpaAudioTagger` | `IAudioClassifier` | `AudioTagging` |
| `SherpaOfflineSpeechDenoiser` | `INoiseSuppressor` | `OfflineSpeechDenoiser` |
| `SherpaStreamingSpeechDenoiser` | `INoiseSuppressor` | `OnlineSpeechDenoiser` |
| `SherpaSpeakerEmbedder` | `ISpeakerEmbedder` | `SpeakerEmbeddingExtractor` |
| `SherpaSpeakerVerifier` | `ISpeakerVerifier` | `SpeakerEmbeddingManager` |
| `SherpaSpeakerIdentifier` | `ISpeakerIdentifier` | `SpeakerEmbeddingManager` |
| `SherpaSpeakerDiarizer` | `ISpeakerDiarizer` | `OfflineSpeakerDiarization` |

All algorithm scheduling remains application-owned.

## Supported model families in this slice

Offline ASR:
- transducer;
- Paraformer;
- NeMo EncDec CTC;
- Whisper;
- SenseVoice;
- Zipformer CTC;
- WeNet CTC.

Streaming ASR / KWS model configuration:
- transducer;
- Paraformer;
- Zipformer2 CTC;
- NeMo CTC;
- T-One CTC.

VAD:
- Silero VAD;
- TEN VAD.

Spoken-language identification:
- Whisper encoder/decoder pair.

Audio tagging:
- Zipformer audio-tagging models;
- CED audio-tagging models;
- explicit label CSV and top-K selection;
- mono 16 kHz input for the pinned Sherpa audio-tagging families.

Speech enhancement:
- GTCRN;
- DPDFNet;
- offline complete-buffer denoising;
- stateful streaming denoising with explicit `flush()` and `reset()`;
- DPDFNet attenuation limiting in offline mode;
- model-derived sample rate and streaming preferred frame count.

The denoiser adapters reject input at a different sample rate instead of allowing
Sherpa's internal resampler to run implicitly. Convert explicitly with an
`IAudioResampler` when required. Streaming sessions preserve a continuous
output timeline across buffered chunks and return the remaining tail from
`flush()`.

Speaker intelligence:
- speaker embedding models supported by Sherpa's `SpeakerEmbeddingExtractor`;
- cosine-style verification and transient identification through `SpeakerEmbeddingManager`;
- offline diarization through pyannote segmentation + speaker embedding + fast clustering.

The upstream project supports additional families. They are deliberately added
to libaudition only when a typed public configuration and contract tests exist;
unknown native fields are not exposed through stringly typed escape hatches.

## Audio contract

The current speech adapters require mono input at their configured sample rate.
Use the explicit libaudition resampler/channel-routing blocks when input differs.

No backend performs implicit application-level buffering or thread scheduling.

## Audio-tagging probability semantics

Sherpa audio tagging returns ranked events with a label and `prob`. The upstream
API represents this value as a probability in `[0, 1]`; libaudition validates
the range and maps it directly to `Probability`. The top-K values are not
renormalized and are not required to sum to one.

The pinned Sherpa audio-tagging stream is internally fixed to 16 kHz and would
otherwise resample mismatched input inside Sherpa. To preserve libaudition's
explicit-conversion contract, the adapter accepts mono 16 kHz only and rejects
other sample rates before the native call. Use libaudition's frontend/resampler
blocks explicitly when the source format differs.

## Missing scores are represented honestly

The current sherpa C++ VAD wrapper exposes speech state/segments, not a calibrated
per-call speech probability. Its spoken-language identifier returns a language
label without a probability, and KWS results expose the keyword/timestamps but
not a calibrated trigger probability.

Therefore:

- `VadResult::speech_probability` may be empty;
- `LanguageScore::probability` may be empty;
- `KeywordHit::probability` may be empty.

The adapter never substitutes 0 or 1 for unavailable confidence.

## Speaker identity semantics

`SherpaSpeakerIdentifier` is an in-memory computational search index, not a
persistent profile database. Applications can populate it from
`ISpeakerRegistry` or another durable store.

The index uses stable `SpeakerId` keys supplied by the application, but Sherpa's
native manager stores only its normalized enrollment vector and search metadata.

Diarization is deliberately different: `SpeakerDiarizationSegment::speaker_index`
is a local cluster label valid only within that diarization result. It must not be
promoted directly to `SpeakerId`.

Sherpa native speaker-manager similarity is preserved as a generic `Score`.
It is not relabeled as a probability. Likewise speaker embedding quality remains
unset because the native extractor does not expose a calibrated quality measure.

If an input is too short for the embedding extractor to become ready,
`SherpaSpeakerEmbedder::embed()` returns `std::nullopt`.

## Token timestamps

Sherpa recognition results expose token sequences and token start timestamps.
libaudition maps these to `Transcript::tokens` / `TimedToken`.

A token end offset is derived from the next token start when available. For
offline TDT-style outputs, native durations are used when available. These are
not promoted to `WordTimestamp`: tokens and words are distinct semantic units.

Offline Whisper advertises token timestamp capability only when
`enable_token_timestamps` is set. The streaming adapter advertises the native
token-timestamp result surface; a particular model may still return no tokens or
timestamps.

Whisper's native `enable_segment_timestamps` option is intentionally not exposed
in this slice because the current structured C++ result does not carry segment
timestamps; they are available only through backend JSON. libaudition does not
leak that raw JSON into its domain model. Word timestamps remain false.

## Execution providers

`SherpaRuntimeOptions::execution.provider` is passed to sherpa-onnx as its
provider string. The adapter does not hard-code RKNN, CUDA, QNN, CoreML or other
accelerators.

When provider is empty, Auto/CPU resolves to `cpu`. Generic GPU/NPU/Accelerator
targets require an explicit provider.

For a fetched sherpa-onnx runtime, provider support is a build-time concern and
libaudition exposes pass-through CMake options rather than choosing hardware:

```text
LIBAUDITION_SHERPA_ENABLE_GPU
LIBAUDITION_SHERPA_ENABLE_DIRECTML
LIBAUDITION_SHERPA_ENABLE_RKNN
LIBAUDITION_SHERPA_ENABLE_AXERA
LIBAUDITION_SHERPA_ENABLE_AXCL
LIBAUDITION_SHERPA_ENABLE_ASCEND_NPU
LIBAUDITION_SHERPA_ENABLE_QNN
LIBAUDITION_SHERPA_ENABLE_SPACEMIT
```

All are OFF by default. Required vendor SDK/toolchain environment remains the
responsibility of the selected sherpa provider. A system-provided sherpa-onnx
installation may of course be built with a different provider set.

The first adapter version intentionally rejects execution fields that sherpa's
current C++ configuration cannot represent directly:

- `device_index`;
- explicit `PrecisionPreference`;
- arbitrary `provider_options`;
- `allow_fallback=false`.

This is preferable to silently ignoring caller intent.

## ModelDescriptor integration

Every Sherpa option structure may carry a `ModelDescriptor`.

If explicit model paths are relative and `descriptor.artifact_path` is set,
they are resolved relative to that path. The descriptor is also the place for
source revision, SHA-256 and model license metadata.

No model files are shipped by enabling `LIBAUDITION_WITH_SHERPA`. Runtime code
license and model artifact license are separate concerns.

## State and concurrency

`SherpaOfflineAsr` and `SherpaLanguageIdentifier` serialize native recognizer
access internally.

Streaming ASR and keyword-spotting sessions share one loaded recognizer with a
mutex protecting recognizer operations. Stream waveform state remains per
session.

A VAD session owns its native VAD state. Creating a VAD session therefore creates
the corresponding native detector state.

No class starts an application scheduler thread.

## Build

```bash
cmake -S . -B build \
  -DLIBAUDITION_WITH_SHERPA=ON \
  -DLIBAUDITION_WITH_SPDLOG=OFF
cmake --build build --parallel
```

When dependency fetching is enabled, sherpa-onnx is built as an isolated shared
ExternalProject with demos, TTS, PortAudio, websocket and upstream tests
disabled. Speaker diarization is enabled because the speaker slice uses the
native offline diarization pipeline.

The resulting sherpa runtime libraries are installed beside
`libaudition_backend_sherpa`; its install RPATH is relative to its own library
directory. A downstream C++17 consumer therefore links only:

```cmake
target_link_libraries(app PRIVATE audition::backend_sherpa)
```

## Future additions

TTS remains a separate capability and will be added as its own adapter instead of
expanding these classes into a monolithic `SherpaBackend`.

Persistent speaker memory is intentionally implemented outside the Sherpa
backend so another embedder/index can replace Sherpa without changing stored
identity semantics.
