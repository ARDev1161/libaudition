# Acoustic scene: selectable speech backends

The live ODAS source pipeline accepts mono 16 kHz separated PCM and routes
speech through the existing bounded per-source ingress and segmentation runtime.

## Voice activity detection

- **Silero VAD** (sherpa-onnx): ONNX model, default.
- **TEN VAD** (sherpa-onnx): ONNX model.

Both currently require the sherpa-onnx backend. Their parameters use the
backend defaults. Selecting TEN VAD requires a compatible TEN VAD ONNX file;
the bundled Silero model is **not** interchangeable.

## Offline recognition

| Selection | Model files | Notes |
| --- | --- | --- |
| SenseVoice | ONNX model, tokens | Languages auto, zh, en, ja, ko, yue; **not Russian** |
| Whisper (sherpa-onnx) | encoder ONNX, decoder ONNX, tokens | Use multilingual weights for Russian |
| Paraformer | ONNX model, tokens | Language support depends on weights |
| NeMo CTC | ONNX model, tokens | Language support depends on weights |
| Zipformer CTC | ONNX model, tokens | Language support depends on weights |
| WeNet CTC | ONNX model, tokens | Language support depends on weights |
| Transducer | encoder ONNX, decoder ONNX, joiner ONNX, tokens | Language support depends on weights |
| whisper.cpp | GGML Whisper model (.bin) | Separate optional backend; multilingual weights for Russian |

These are **offline** engines, operating on completed speech segments.
They do not implement realtime partial ASR. The separate Sherpa online
ASR API/workbench remains a different path.

## Extended demo model provisioning

In addition to the default Silero + SenseVoice models, you can ask CMake to
download representative models for **every Sherpa offline family listed in
Acoustic scene**:

```sh
cmake -S . -B build/Desktop_Debug \
  -DLIBAUDITION_BUILD_QT_DEMO=ON \
  -DLIBAUDITION_WITH_SHERPA=ON \
  -DLIBAUDITION_DEMO_FETCH_EXTRA_SPEECH_MODELS=ON
```

The extended package downloads **TEN VAD**, multilingual **Whisper Tiny INT8**
(Russian included), English **Paraformer INT8**, Russian **NeMo CTC INT8**,
Chinese **Zipformer CTC INT8**, Chinese **WeNet CTC INT8**, and Russian
**Zipformer Transducer INT8**. Silero and SenseVoice remain part of the regular
model-fetch option. Models are cached under
`<build>/demo-models/speech` and picked up by **Scan models**.

These are substantial downloads, so extended provisioning is opt-in and
disabled in ordinary CI configurations. It requires network access during CMake
configuration and a significant amount of free disk space. CMake reuses
nonempty cached archives; no integrity hash is currently pinned for the extra
upstream model packs, so this path is for development/demo provisioning rather
than reproducible or security-critical builds. No model is embedded in the
installed library.

The `whisper.cpp` adapter requires a **separate GGML multilingual model**;
the Sherpa Whisper ONNX package is not compatible with `whisper.cpp` and does
not make that selection available automatically.

## Building whisper.cpp support

This adapter uses the native upstream `whisper.h` API. It is disabled by
default to keep core builds independent of GGML.

1. Build and install an upstream-compatible `whisper.cpp` library.
2. Configure `libaudition` with `-DLIBAUDITION_WITH_SHERPA=ON
   -DLIBAUDITION_WITH_WHISPERCPP=ON -DLIBAUDITION_BUILD_QT_DEMO=ON`.
3. Supply the installed `whisper.h` and `libwhisper` via the compiler's
   CMake search paths. If necessary set `WHISPERCPP_INCLUDE_DIR` and
   `WHISPERCPP_LIBRARY`.
4. Select `whisper.cpp (GGML)` in Acoustic scene and choose a GGML
   **multilingual** model such as `ggml-base.bin` (not `ggml-base.en.bin`).
5. Select Russian for the language.

The adapter requires mono interleaved 16 kHz float PCM, and serializes
inference on a given recognizer context. It does not download weights or
promise NPU acceleration. The VAD stage currently still requires sherpa-onnx.

## Known limitations

- Switching families does **not** convert model files; use files compatible
  with the selected backend.
- Model initialization and real-device capture should be tested on the target
  host. CI configurations without whisper.cpp do not exercise the new backend.
- Speech segmentation remains capped at 10 seconds per segment, and VAD
  pre-roll remains 200 ms. Delayed ODAS tracking and VAD activation can still
  omit the first seconds of speech.
- The tracked-source table shows the last part of accumulated **final**
  transcript text; realtime provisional tokens are not yet implemented.
