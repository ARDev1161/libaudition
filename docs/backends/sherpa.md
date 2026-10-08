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

The upstream project supports additional families. They are deliberately added
to libaudition only when a typed public configuration and contract tests exist;
unknown native fields are not exposed through stringly typed escape hatches.

## Audio contract

The current speech adapters require mono input at their configured sample rate.
Use the explicit libaudition resampler/channel-routing blocks when input differs.

No backend performs implicit application-level buffering or thread scheduling.

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
ExternalProject with demos, TTS, speaker diarization, PortAudio, websocket and
upstream tests disabled for this speech-only slice.

The resulting sherpa runtime libraries are installed beside
`libaudition_backend_sherpa`; its install RPATH is relative to its own library
directory. A downstream C++17 consumer therefore links only:

```cmake
target_link_libraries(app PRIVATE audition::backend_sherpa)
```

## Future additions

Speaker embeddings, verification/identification, diarization, audio tagging,
speech enhancement and TTS remain separate capabilities and will be added as
separate classes instead of expanding these speech classes into a monolithic
`SherpaBackend`.
