# Sound classification productionization

This document describes implementation status and acceptance criteria.
A model is not considered supported merely because an ONNX file exists.

## Current foundation (this branch)

- `SourceClassificationRuntime`: per-track mono buffering, bounded source map,
  bounded per-source FIFO queue, one background model inference worker, explicit states,
  errors and `forget()` for track retirement. In-flight work has a separate
  generation identity so retiring/reusing a track ID never attaches stale
  results to the new source. Cached model classifications expire after 3s
  (monotonic time); source ID 0 is accepted. Capture and scheduling are not
  coupled to Qt, ALSA, ODAS or ROS.
- Qt's `LiveAudioTaggingWorker` is a thin adapter around the production runtime;
  it only constructs the selected model and formats diagnostics.
- `VocabularySelection`: AllClasses / SelectedClasses, exact matching and
  explicit unsupported labels, with non-comparable score semantic tagging.
- CLAP open-vocabulary candidate labels are sent to its inference call.

### Known limitations to resolve before release

1. Qt now offers All classes (Top-5) or Selected vocabulary (exact, comma-separated labels). The selected mode currently uses a plain text field; a searchable class catalog / per-label status is still required.
2. The Qt adapter requests up to 521 YAMNet or 527 Sherpa scores before filtering. Validate each actual model's complete class count and label mapping; a partial result must never be treated as a complete catalog.
3. `SourceClassificationRuntime` now caps both source buffers and pending jobs,
   deduplicates queued windows per source and preserves FIFO ordering; worker shutdown
   deadlines, job cancellation, sample discontinuities,
   stale-result TTL and immutable result snapshots require further tests.
4. Model creation on the capture worker thread must become asynchronous
   with separately reported initialization state.
5. CLAP cosine/softmax scores are not absolute sound probabilities. Do not
   compare them numerically against model probabilities.
6. Any downloaded model must have pinned SHA-256 and license metadata.
7. Record 16 kHz per-track SSS and raw-microphone comparison fixtures.

## Candidates (explicit implementation required)

| Model | Why it may improve on YAMNet | Requirements |
| --- | --- | --- |
| EfficientAT MobileNet / DyMN variants | Compact models, published strong accuracy-efficiency tradeoff | 32 kHz waveform classifier and spectrogram ONNX adapter implemented, synthetic-model CI tests pass; PyTorch parity, authentic weight exports, and hardware benchmarks **not yet verified** |
| BEATs | Strong published audio representations / sound event accuracy | Checkpoint license, preprocessing and model-specific classification head |
| PaSST | Efficient transformer audio tagging via Patchout | Spectrogram frontend fidelity, input shape, runtime benchmark |
| HTS-AT | Hierarchical tagging transformer with competitive AudioSet scores | Frontend fidelity, model export tests |
| PANNs (CNN14 family) | Mature strong AudioSet baseline and embeddings | Add only if chosen variant beats YAMNet on an agreed axis (accuracy, latency, RAM or embeddings); no unconditional legacy inclusion |

Do not automatically add every paper/model called a classifier. A candidate must
have a compatible, legally distributable artifact, verified score semantics,
an independently tested input/output contract, and at least one documented
benefit relative to YAMNet.

## Acceptance tests for each additional model

- Model load with authentic pinned artifact and recorded hash/license
- Mono/sample-rate/layout rejection, tensor type/shape rejection
- Matched preprocessing reference outputs on fixed waveform
- Top-K and full class probabilities, id/label mapping, missing labels
- Silent/noisy/multi-source WAV fixtures on source-specific audio
- Measured latency, peak RSS, CPU load, and accuracy at reference hardware
- Demo UI model selection, progress/errors and runtime switch behavior
- `cmake --install` + an out-of-tree C++ consumer build
