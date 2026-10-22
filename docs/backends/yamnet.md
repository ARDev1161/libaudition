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

## Full-clip verification versus low-latency operation

The real-event comparison now reports two independent YAMNet modes on the
same ESC-10 source recordings: `yamnet` uses the first 1 second; `yamnet_5s`
processes the complete **five-second** recording with the same model and
mean-per-class pooling across its temporal patches. These are distinct
measurement regimes, not competing model weights or alternative label maps.
An ESC-10 clip-level label does not imply that the first second contains
its event. A first-second `Silence` prediction can therefore be correct
for the input while still missing a sound that occurs later.

`yamnet_wav_benchmark` accepts an optional `--seconds 5` argument, with
the default remaining 1 second. Its RTF is normalized by the requested audio
duration. For real-time ODAS classification the Qt adapter retains an
approximately 0.975-second model patch and 50% hop; do **not** force a
five-second startup delay based on offline clip metrics.

The real-weights CI also verifies **C++ vs independently executed ONNX**
on 80,000 samples (five seconds) in addition to the original 15,600-sample
single-patch fixture. This checks temporal patch aggregation as well as
the reported class ordering.

## Full-event timeline validation

The YAMNet adapter averages all native patch scores for a given waveform.
This is correct for **clip-level aggregation**, but a short transient
embedded in silence can have a low five-second mean even when a particular
patch classifies it correctly. Do not apply a threshold to the five-second
mean and infer that the event never occurred.

The `tools/analyze_yamnet_event_timeline.py` CI diagnostic runs the full
five-second ESC-10 examples through the waveform ONNX model, preserving
scores for each native YAMNet patch (approximately 0.96 seconds of context,
0.48-second stride). Its artifact `yamnet-event-timeline.json` includes
per-frame Top-3 predictions, ground-truth class mean and maximum, time of
peak, and the Top-5 hit under **mean versus temporal maximum**. The latter
is an exploratory event-presence diagnostic and is not calibrated; it can
increase false positives if used as a detector without threshold validation.

For live ODAS separated audio, the Qt adapter uses a short patch-length
window with 50% overlap. After each hop, an inference result can be
published without waiting for an entire 5-second recording. This is
preferable for *when did an event happen?*; longer clip-level aggregates
are appropriate for *what happened during the recording?*.

## Separated-source fault isolation

`SourceClassificationRuntime` isolates malformed audio and classifier
exceptions by ODAS `source_id`. A NaN sample or an inference failure
marks that source as `Error` and discards its buffered/queued windows,
without terminating the inference worker or blocking other tracks.
The affected track remains quarantined until the caller explicitly
`forget(source_id)` (for example, after ODAS retires the track).
The next generation of the same numeric source ID can then be
classified normally. A completed inference must not overwrite a
source error if it was reported while that inference was running.

This is an operational safety property, not a guarantee of successful
recognition on poor-quality separated audio. The C++ regression suite
covers malformed samples, a throwing backend, healthy parallel
sources and numeric track-ID reuse after `forget()`.

## Extended ESC-10 evaluation (80 clips, four classes)

The real-weight CI job now prepares **80 five-second ESC-10 clips**: 20 each
for Dog, Rain, Helicopter and Chainsaw, selecting four examples from
each original ESC-50 fold. The upstream sources are pinned to
`33c8ce9eb2cf0b1c2f8bcf322eb349b6be34dbb6`, and the generated
`provenance.csv` retains the source filenames, folds and CC BY attribution.
Only the reports, manifest and licensing information are archived; the
downloaded WAVs are not committed.

The `tools/evaluate_yamnet_esc10.py` runner uses one YAMNet ONNX session and
the same audio content to compare:

- **First second:** a low-latency approximately one-second classifier window,
  which might not contain the event labelled at clip level.
- **Whole-clip mean:** normal YAMNet aggregation over all five seconds.
- **Whole-clip peak:** the highest per-class native patch score across five
  seconds, an *uncalibrated* diagnostic sensitive to brief events and noise.

The separate `yamnet-esc10-80-evaluation` artifact includes clip-level
Top-5, each true class's rank, per-class Top-1/Top-5 recall, macro/micro
averages, and inference-only timing. **There is no hard accuracy threshold
in CI**: a failure of the pipeline means broken evaluation or integration,
not that a model must meet an arbitrarily chosen accuracy level. Results
cannot establish AudioSet mAP, event-level onset accuracy, or false-positive
rates on ReSpeaker/ODAS hardware. On-device capture and streaming validation
remain separate release checks.

To use a different bounded corpus size, run
`tools/prepare_esc10_evaluation.py --clips-per-category N` where
`1 <= N <= 40`, then use its manifest with
`tools/evaluate_yamnet_esc10.py`.

## Progressive per-source classification (~1 s, then 5 s)

`ProgressiveSourceClassificationRuntime` (in
`<audition/classify/progressive_source_classification_runtime.hpp>`) composes
two or more independent classifiers. Each stage accepts **any**
`IAudioClassifier`, with its own `window_samples` and `hop_samples`.
The order in the configuration defines result priority. As soon as the
short-window result is available, `status(source_id).result` returns it;
after the higher-priority 5-second stage completes, its prediction is
preferred. `status(source_id).stages` still exposes **every** model result
and its error separately; no scores from different windows are fabricated
or fused.

The Qt live demo enables this automatically for **dynamic-input YAMNet**:

| Stage | Audio window | Hop | When available |
| --- | --- | --- | --- |
| `quick` | Model-preferred 15,600 samples (~0.975 s) | Half window (~0.488 s) | First ~1 s |
| `refined` | 80,000 samples (5 s) | 16,000 samples (1 s) | First 5 s |

Both use separate waveform ONNX sessions. The refinement stage adds CPU
and model memory cost. With a **fixed-length** YAMNet ONNX graph that cannot
accept 80,000 samples, Qt keeps the original short-only behavior. Optional
`LiveTaggingOptions` window and hop overrides adjust its scheduling;
an incompatible explicit fixed ONNX window is rejected instead of silently
passing the wrong tensor shape.

Example using an entirely different pair of classifiers (no dependency
on ONNX or YAMNet in the progressive runtime):

```cpp
#include <audition/classify/progressive_source_classification_runtime.hpp>

std::vector<audition::ProgressiveClassificationStage> stages;
stages.push_back({"quick", std::move(fast_classifier), 16000U, 8000U});
stages.push_back({"context", std::move(long_classifier), 80000U, 16000U});
audition::ProgressiveSourceClassificationRuntime progressive{
    16000U, std::move(stages), 16U};
progressive.push(source_id, pcm16k.data(), pcm16k.size());
const auto state = progressive.status(source_id);
if (state.result && state.selected_stage) {
    const auto& stage = state.stages[*state.selected_stage];
    // Stage name and scores are available separately.
}
progressive.forget(source_id); // ODAS retires or reuses its track ID.
```

For arbitrary window lengths or a single model, continue using
`SourceClassificationRuntime` directly. The progressive runtime does not
change its API. Stages have independent bounded queues and worker threads;
a failing refinement cannot disable the short-window classifier. A
five-second window is a **context refinement, not guaranteed higher
accuracy**, particularly immediately after an acoustic event changes.
The highest-priority valid result is selected while its existing runtime
TTL remains active. Applications can instead choose a particular stage
explicitly using `state.stages`.

### Freshness and overload policy for progressive stages

Every `SourceClassificationStatus` exposes `received_samples` and
`result_end_sample` (end-exclusive position of the **audio that produced
the result**, not when the model happened to finish). This lets callers
distinguish a recently returned result about old audio from a result
about recent audio. `result_end_sample` is empty when the result is
missing, expired, or the track failed.

`ProgressiveClassificationStage::max_result_lag_samples` optionally
limits the difference between the latest received position and that
stage's last completed prediction. A value of `0` chooses the default
of two effective hops. Results remain visible in `status().stages`
even after they become too old to be selected. The default selection
still prefers later stages, **but only while their audio is recent**.
For a five-second refiner with a one-second hop, a result more than
two seconds behind the stream yields priority back to the quick model.
The limit can be overridden independently for any classifier or window.

Each underlying single-stage runtime still bounds its pending queue
and accepts arbitrary windows. If several completed windows accumulate
before a source's queued job can be processed, newer audio replaces the
older **pending** job for that same source, without interrupting a
currently running inference or evicting a different source. The
`dropped_windows` counter includes replaced intermediate windows.
This is deliberate best-effort real-time behavior, not lossless
recording: under severe overload the intermediate acoustic events may
still go undetected.

### Continuous 5–10 source stress and runtime diagnostics

The generic `progressive_multisource_stress` CTest executable simulates
five or ten independently tracked 16-kHz sources in 10-ms PCM chunks.
It uses two *synthetic* classifiers with configurable inference delays
and validates that every stage eventually processes the most recent
eligible window despite overload. Run manually:

```bash
./build/tests/progressive_multisource_stress 5 8 4 12
./build/tests/progressive_multisource_stress 10 8 8 24
```

Each JSON line reports the number of sources, simulated audio duration
per source, audio-feed wall time, worst synchronous `push()` duration
and per-stage drops, completed inference windows, maximum observed
inference time and queue wait time. These are **diagnostic measurements
on the host executing the tool**, not real-world accuracy or latency
guarantees for the RK3588.

For real model throughput, `yamnet_progressive_multisource` runs the
original ONNX waveform classifier with 5 or 10 independent sources,
16-kHz PCM, and 10-ms batches delivered at their nominal capture rate.
It tests both the ~0.975-second and 5-second windows with real model
weights and reports the same timing/drop statistics:

```bash
./build/tests/yamnet_progressive_multisource yamnet.onnx \
  yamnet_class_map.csv 10 8 real-time
```

`burst` can replace `real-time` to test queue coalescing at faster-than-
capture input speed. CI archives the real-weight 5- and 10-source JSON
results in `yamnet-progressive-multisource-stress`. The actual sampling
hardware and ODAS separation quality cannot be certified from this
synthetic audio: test on ReSpeaker and the target SBC separately.

`SourceClassificationStatus::dropped_windows` now counts only **that
source's** superseded or rejected windows (not all ODAS tracks).
`completed_windows`, `last_queue_wait_ms` and `last_inference_ms`
provide per-source/per-stage performance observability. Timing fields
describe the latest successful window and are absent until the first
successful inference. Status `result_end_sample` is an **audio clock**
rather than a time of inference completion. Loss counters reset when the
source is explicitly retired with `forget()`. Under sustained overload,
old queued windows are intentionally replaced to prioritize freshness.
