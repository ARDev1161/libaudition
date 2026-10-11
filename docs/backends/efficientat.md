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

## Paired MN10 / DyMN10 quality comparison

`tools/compare_efficientat_wavs.py` executes the same annotated manifest
for both exported models. It writes a JSON artifact containing per-clip
Top-5 scores, hit@5, macro recall@5 and mean median inference latency.

```bash
python3 tools/compare_efficientat_wavs.py \
  --manifest /path/to/manifest.csv \
  --benchmark ./build-efficientat/tests/efficientat_wav_benchmark \
  --mn-model /path/to/mn10_as.onnx \
  --mn-labels /path/to/mn10_as_labels.txt \
  --dymn-model /path/to/dymn10_as.onnx \
  --dymn-labels /path/to/dymn10_as_labels.txt \
  --output comparison.json
```

The comparison rejects ground-truth labels missing from either model's
527-label vocabulary. YAMNet is **not** treated as sharing these class
indices: its 521-class vocabulary and 16 kHz waveform contract require an
explicit label-name/MID mapping and input-rate adapter before comparable
multi-model metrics are meaningful.

## YAMNet ontology compatibility audit

YAMNet's 521-class vocabulary differs from EfficientAT's 527-class AudioSet
vocabulary. Use stable **AudioSet MID identifiers** (not numerical positions,
nor display names) to reconcile them:

```bash
python3 tools/audit_audioset_vocabularies.py \
  --efficientat-csv /path/to/EfficientAT/metadata/class_labels_indices.csv \
  --yamnet-csv /path/to/yamnet_class_map.csv \
  --output /tmp/audioset_vocab_audit.json
```

The audit records shared MIDs, model-specific labels and shared MIDs with
different display names. The original complete CSV maps are necessary;
an exported EfficientAT `*_labels.txt` loses MIDs and is insufficient for
this check. Auditing the ontologies does **not** constitute a three-model
quality comparison: the same precisely annotated clips, preprocessing,
score thresholds and unmapped-label treatment must still be established.

## Three-backend PCM comparison (integration smoke test)

`yamnet_wav_benchmark` uses the same mono PCM16 32 kHz WAV as the
EfficientAT benchmarks, applying a 32 -> 16 kHz windowed-sinc low-pass
decimator for the YAMNet waveform input. The `efficientat-real-weights`
CI job now builds all three benchmark binaries and runs YAMNet, MN10 and
DyMN10 against the **same** generated waveform.

This tests the rate conversion and inference path only. Since the YAMNet
ONNX artifact has a separate 521-class ontology, output labels must be
reconciled using `tools/audit_audioset_vocabularies.py` before computing
three-model metrics. End-to-end real-event accuracy still needs a licensed
annotated dataset; scores from the generated sine wave are not accuracy
evidence.

## Unified three-model AudioSet benchmark

`tools/compare_audio_taggers.py` compares YAMNet, MN10 and DyMN10 on
**the same annotated 32 kHz mono PCM16 WAVs**. The manifest uses AudioSet
MID identifiers, not display names, and may annotate multiple labels:

```csv
wav,mid
audio/voice.wav,/m/09x0r
audio/mixed.wav,/m/09x0r|/m/05zppz
```

The command requires model/labels paths and BOTH upstream ontology CSV maps:

```bash
python3 tools/compare_audio_taggers.py \
 --manifest /path/to/manifest.csv \
 --mn-model /models/mn10_as.onnx --mn-labels /models/mn10_as_labels.txt \
 --dymn-model /models/dymn10_as.onnx --dymn-labels /models/dymn10_as_labels.txt \
 --yamnet-model /models/yamnet.onnx --yamnet-labels /models/yamnet_class_map.csv \
 --efficientat-csv /path/to/EfficientAT/metadata/class_labels_indices.csv \
 --yamnet-csv /models/yamnet_class_map.csv \
 --efficientat-benchmark ./build-efficientat/tests/efficientat_wav_benchmark \
 --yamnet-benchmark ./build-efficientat/tests/yamnet_wav_benchmark \
 --output /tmp/audio_taggers_comparison.json
```

Each model reports hit@5, macro_recall@5, mean median/p95 latency and
mean RTF, with per-clip Top-5 and scores. Clips annotated with IDs outside
the **shared intersection** of MID vocabularies are reported as excluded;
out-of-intersection predictions still occupy Top-5 slots. This avoids
claiming one model is wrong for a label it cannot represent.

These scores are **not AudioSet mAP**. CPU benchmark timings omit model
loading, WAV decoding, and the YAMNet 32->16 kHz preprocessing step.
Use identical clips and hardware; do not interpret runner timings as
RK3588 results.

## CI three-model smoke report artifact

The `efficientat-real-weights` CI job also calls
`tools/compare_audio_taggers.py` with one generated 440 Hz PCM clip and
the shared AudioSet speech MID. The resulting
`efficientat-yamnet-smoke-report` JSON artifact contains per-model timing,
Top-5 predictions, ontology-intersection information and an explicit
record of excluded clips.

**The synthetic tone is NOT evidence of speech classification quality.**
Its CSV target only exercises the metric calculation pipeline. The
`hit@5` values in that artifact must not be reported as real-world
accuracy. Replace the manifest with a licensed, independently annotated
corpus before drawing conclusions about classifier quality.

## Real recordings: ESC-10 exploratory CI comparison

The real-weight CI job now additionally downloads **four ESC-10** recordings
(dog, rain, helicopter, chainsaw) from the upstream
[ESC-50](https://github.com/karolpiczak/ESC-50) repository. Only recordings
marked `esc10` in official metadata are selected (CC BY rather than the
larger ESC-50 CC BY-NC set). Source files are 5 seconds at 44.1 kHz;
ffmpeg converts a **1.0–2.0 s excerpt** to mono PCM16 at 32 kHz for all
three models. WAVs are never committed to the repository.

```bash
python3 tools/prepare_esc10_evaluation.py \
 --output /tmp/esc10-eval \
 --efficientat-csv /path/to/EfficientAT/metadata/class_labels_indices.csv \
 --yamnet-csv /path/to/yamnet_class_map.csv
```

Artifact `efficientat-yamnet-esc10-real-event-report` includes MID
annotations, per-model JSON results, original Freesound identifiers and
upstream attribution/license. It intentionally omits redistributing audio.

**Limitations:** source labels apply to 5-second clips, not verified
annotations of the extracted 1-second interval. This is an exploratory
real-waveform smoke/relative-comparison test, not a validated accuracy
benchmark or an official ESC-10 evaluation. Expand and manually verify
clip/window annotations before selecting a production classifier.

## ESC-10 first real-waveform results (CI #577)

On **four** one-second ESC-10 excerpts selected from five-second labeled
files, the exploratory top-5 exact-MID hit counts were MN10 **0/4**,
DyMN10 **1/4**, YAMNet **1/4**. Mean of per-clip median inference times
on the GitHub Actions x86 runner: **13.31 ms**, **36.62 ms** and **3.57 ms**,
respectively. The raw per-clip scores and provenance are in the CI
`efficientat-yamnet-esc10-real-event-report` artifact.

Notable diagnostics: MN10 emitted multiple **1.0** speech-class scores
on all four unrelated event clips, as well as the earlier generated sine
wave. This is a serious frontend/model-parity issue to investigate, not
evidence that speech was present. The one-second dog excerpt was tagged
`Silence` at 1.0 by YAMNet, making its clip-level dog label unsuitable
as unverified one-second ground truth. On rain, YAMNet output `Rain`
within Top-5 (DyMN10 also did). On helicopter both primarily predicted
engine-related labels, rather than the exact annotated class.

`tools/review_audio_taggers_report.py` flags saturated Top-5, silence-
dominant excerpts and very small evaluation sets in CI and stores
`review-findings.json` beside the comparison report.

**Do not rank these classifiers by detection accuracy yet.** The immediate
next gating experiments are MN10 PyTorch-versus-C++ inference parity on
**identical decoded WAV/mel inputs**, verifying labels inside each selected
one-second excerpt, and increasing the licensed annotated corpus size.

## MN10 saturation root-cause diagnostic

The `efficientat-real-weights` CI now checks a genuine ESC-10 chainsaw
excerpt using `tools/diagnose_efficientat_wav.py`. It computes upstream
PyTorch-compatible mel features for the same 32 kHz PCM16 samples, runs
both original PyTorch checkpoint and exported ONNX on this tensor, and
compares the resulting class probabilities with the C++ WAV benchmark.

The report `mn10-chainsaw-parity.json` records raw logit discrepancies,
mel range, and PyTorch/ONNX/C++ Top-5 labels/scores. This isolates:
- Torch vs ONNX divergence (export or inference mismatch)
- C++ vs Python score divergence (waveform frontend or adapter mismatch)
- All three agreeing but emitting 1.0 on speech (model/input mismatch,
  or upstream model behavior; not automatically an ONNX/C++ defect)

This diagnostic needs passing CI and human review before calling the
saturation root cause resolved.
