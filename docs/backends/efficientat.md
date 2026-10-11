# EfficientAT audio tagging (experimental)

Enable with `-DLIBAUDITION_WITH_EFFICIENTAT=ON`. This builds
`audition::backend_efficientat` and installs the C++ header
`<audition/backends/efficientat/audio_tagger.hpp>`.

## C++ API

```cpp
#include <audition/backends/efficientat/audio_tagger.hpp>

audition::EfficientAtOnnxOptions options;
options.model = "/path/to/mn10_as.onnx";
options.labels = "/path/to/527-ordered-labels.txt";
options.top_k = 5;
audition::EfficientAtAudioTagger tagger{options};
audition::ClassificationResult result = tagger.classify(audio.view());
```

**The example is a contract, not a published working model package.**
Supply an exported ONNX artifact matching the exact IO below. No model
weights are bundled or downloaded for this backend.

- Input waveform: mono float32 at 32,000 Hz, provided through
  `IAudioClassifier::classify(AudioView)`. No implicit resampling.
- Preemphasis: `x[i + 1] - 0.97*x[i]`, reducing length by one.
- STFT: `n_fft=1024`, 800-sample symmetric Hann, hop 320,
  centered reflect padding, power spectrum.
- Mel: 128 Kaldi-style triangular banks, `fmin=0`, `fmax=15000`;
  last Nyquist column padded with zeros.
- Model input: contiguous float32 `[1,1,128,T]` normalized logmel,
  `(log(mel_power + 1e-5) + 4.5)/5`.
- Output: float32 `[1,527]` or `[527]` **logits**;
  sigmoid is applied in libaudition. Labels are ordered plain-text lines.
- `EfficientAtSpectrogramTagger::classifyLogMel()` accepts externally
  prepared, normalized log-mel directly if a reference frontend is preferred.

## Validation limits

The test suite generates a **synthetic** ONNX graph with fixed logits.
It verifies ONNX execution, sigmoid, Top-K, label mapping and rejects
non-finite / malformed inputs. A silence frontend regression test is included.

Qt acoustic scene can select `EfficientAT` when built with
`LIBAUDITION_WITH_EFFICIENTAT=ON`. The ODAS-separated 16 kHz source audio is
explicitly upsampled to 32 kHz by the backend's windowed-sinc 2x adapter;
this interpolation is not part of original EfficientAT evaluation and must
be benchmarked for classification quality. Select a matching ONNX and its
527 ordered labels manually; model files are not downloaded.

**Numerical parity against the original Python
`models/preprocess.py::AugmentMelSTFT` is not yet established.**
Do not publish accuracy or latency benchmark claims from this C++ path
until vectors computed by the original PyTorch + torchaudio Kaldi mel
implementation are checked at the tensor level. Do not assume arbitrary
public ONNX exports have the required tensor shapes and raw logits;
some may include their own frontend or output post-sigmoid scores.

Source of preprocessing contract:
https://github.com/fschmid56/EfficientAT/blob/main/models/preprocess.py

The model's original license and the export's license must be reviewed
independently before bundling artifacts.

## Frontend parity harness

CI's `efficientat-backend` job builds `efficientat_frontend_dump`, creates
a deterministic 32 kHz signal (440 Hz, 1730 Hz and swept-tone component),
then runs `compare_efficientat_frontend.py` using reference
`torch.stft` and `torchaudio.compliance.kaldi.get_mel_banks`.
The comparison reports max, mean and p99 absolute error, failing if
max error exceeds 3e-3. This parity check tests the frontend only;
it does not validate real EfficientAT model weights or accuracy.

To reproduce locally with the backend enabled:

```bash
cmake --build build-efficientat --target efficientat_frontend_dump
./build-efficientat/tests/efficientat_frontend_dump /tmp/efficientat_mel.f32
python3 tests/fixtures/compare_efficientat_frontend.py /tmp/efficientat_mel.f32
```

## Genuine checkpoint smoke testing

The `efficientat-real-weights` GitHub Actions job exports both upstream
`mn10_as` and `dymn10_as` with `tools/export_efficientat.py`. It verifies
PyTorch-vs-ONNX **raw logits** at fixed `[1,1,128,100]` input shape, and
then calls the C++ `EfficientAtAudioTagger` on a synthetic 440 Hz signal.
The exporter writes a per-model manifest with the ONNX artifact digest.

Verified in workflow run #540 (2026-10-11):
- `mn10_as`: max absolute logit difference `3.814697265625e-05`
- `dymn10_as`: max absolute logit difference `5.0067901611328125e-06`
- Both C++ model smoke tests: PASS

These results establish loading/shape/runtime compatibility and inference
parity on a **single random mel tensor**, not robustness, real-world accuracy,
or embedded-device performance. The exported graphs have a **fixed 100-frame
input dimension**; supply a one-second waveform at 32 kHz (or use
`EfficientAt16kAudioTagger` with one second at 16 kHz). Arbitrary frame lengths
should not be assumed to work with these exports.

Reference CI: https://github.com/ARDev1161/libaudition/actions/runs/38097431465

## Repeatable WAV latency benchmark

Build the optional backend with tests enabled. The WAV benchmark accepts mono,
32 kHz, 16-bit PCM RIFF/WAVE of up to 20 seconds and classifies its first
one-second window. It performs one warmup and 20 timed inferences, printing
median/p95 inference time, min/max, real-time factor (RTF), and Top-5 scores.

```bash
cmake --build build-efficientat --target efficientat_wav_benchmark
./build-efficientat/tests/efficientat_wav_benchmark \
    /path/to/mn10_as.onnx /path/to/mn10_as_labels.txt /path/to/32k_mono.wav
```

The CI benchmark uses **a generated 440 Hz WAV** for deterministic regression
and compares MN10 versus DyMN10 on the same runner. This is a performance
smoke test, not a real-world accuracy benchmark; never extrapolate its CPU
times to RK3588. A representative, licensed real-event WAV corpus, YAMNet
comparison and ARM/RK3588 measurements remain to be done.

## Evaluation with user-provided real acoustic events

No real WAV dataset is bundled or downloaded automatically. Prepare legally
usable mono PCM16 32000 Hz files with the target event present in the **first
second**; annotate a UTF-8 CSV manifest:

```csv
wav,label
audio/voice.wav,Speech
audio/bell.wav,Bell
audio/dog.wav,Bark|Dog
```

Run separately with each exported model:

```bash
python3 tools/evaluate_efficientat_wavs.py \
  --manifest /path/to/manifest.csv \
  --model /path/to/mn10_as.onnx \
  --labels /path/to/mn10_as_labels.txt \
  --benchmark ./build-efficientat/tests/efficientat_wav_benchmark
```

This reports **hit@5** and **macro_recall@5** for explicitly annotated
clips, without mislabeling these exploratory statistics as AudioSet mAP.
Use **the same exact WAV/manifest** for both models. The benchmark executes
20 iterations per clip, so the evaluation script is intentionally oriented
towards a small supervised regression corpus, not a full benchmarking suite.

GitHub Actions #547 (CPU runner, generated 440 Hz WAV, 1 s clip) measured:
- MN10: median 11.4436 ms, p95 11.5743 ms, RTF 0.0114.
- DyMN10: median 29.4378 ms, p95 30.0336 ms, RTF 0.0294.

These numbers are for one CI runner, **not RK3588** and not a real
acoustic-event detection quality assessment.
