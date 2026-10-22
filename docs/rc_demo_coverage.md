# Release-candidate executable demonstration coverage

This matrix tracks **demonstrations**, which are different from algorithm
unit/CTest coverage or measured physical-model accuracy. The RC must expose
each public feature family through a runnable example or an honest, navigable
capability entry; backend-dependent runners must fail visibly on missing
weights/devices instead of fabricating results.

## Runnable coverage (2026-10-22)

| Public feature family | Interactive Qt | CLI example / runnable entry | Evidence |
|---|---|---|---|
| AudioBuffer, formats, levels, RMS/peak/clipping, DSP | Audio / DSP | `libaudition_example_core` | `qt_self_test`, `core_audio.cpp` |
| WAV inspection and channel selection | Backend workbench / WAV | Workbench runner | `qt_self_test`, WAV round-trip |
| SPL calibration and sound-level distance priors | SPL / Range | `libaudition_spl_cli` | `cli-spl-range-smoke` |
| Temporal spatial smoothing | Smoothing | `libaudition_smoothing_cli` | `cli-smoothing-smoke` |
| Persistent acoustic-source resolution | Source identity | `libaudition_identity_cli` | `cli-identity-smoke` |
| AcousticEvent lifecycle and assembling | AcousticEvent | `libaudition_event_cli` | `cli-event-smoke` |
| Synthetic multi-stage acoustic routing | Pipeline | `libaudition_pipeline_cli` | `cli-pipeline-smoke` |
| Scalar EMA/CUSUM change detection | Feature catalog points to temporal demos | `libaudition_temporal_events_cli` | `cli-temporal-events-smoke` |
| Temporal hysteresis event lifecycle | Feature catalog; no dedicated Qt controls | `libaudition_temporal_events_cli` | `cli-temporal-events-smoke` |
| Persistent-source event episode association | Feature catalog; no dedicated Qt controls | `libaudition_temporal_events_cli` | `cli-temporal-events-smoke` |
| Allen interval relations, spatial-time/covariance gate and Hungarian batch assignment | Feature catalog; no dedicated Qt controls | `libaudition_temporal_association_cli` | `cli-temporal-association-smoke` |
| Recurrence / periodic event onset analysis | Temporal patterns | `libaudition_temporal_cli` | `qt_self_test`, `cli-temporal-patterns-smoke` |
| DTW distance and warping | Temporal patterns | `libaudition_temporal_cli` | `qt_self_test`, `cli-temporal-patterns-smoke` |
| Offline motif discovery | Temporal patterns | `libaudition_temporal_cli` | `qt_self_test`, `cli-temporal-patterns-smoke` |
| Source-local streaming template matching | Temporal patterns | `libaudition_temporal_cli` | `qt_self_test`, `cli-temporal-patterns-smoke` |
| Live multichannel SSL/SST/SSS ODAS with ALSA | Acoustic scene | Headless error and optional physical-device runners | `qt_odas_errors`, `qt_alsa_errors` where enabled |
| Acoustic source classification (Sherpa, YAMNet) | Acoustic scene / Backend workbench | Real-model self-tests when configured | Optional model fixture CI |
| EfficientAT, CLAP, AASIST, WORLD | Backend workbench where supported | Backend model tests / workbench runners | Optional backend CTest |
| Offline/streaming speech VAD/ASR, KWS, language ID, TTS | Acoustic scene / Sherpa workbench | Real-model self-tests when configured | Optional model fixture CI |
| Speaker identity, embedding, diarization | Sherpa workbench | Real-model self-tests when configured | Optional speech tests |
| Audio resampling, AEC3, GTSAM fusion | Backend workbench | Real backend operation when enabled | Optional Qt model-self-test cases |
| Per-source async PCM ingress, cancellation, generation | Acoustic scene uses adapter internally | Integrated scenario and runtime tests | CTest ingress/worker/recording tests |
| Recording WAV/JSON timeline, source retirement | Acoustic scene / recording popups | WAV/sidecar tests and live capture | `recording_timeline_json_test`, `source_recording_session_test` |

The optional backends are not built by the default dependency-light build.
The catalog says **API + CTest only** for features without a direct GUI runner;
it never shows a model-only capability as physically verified.

## Explicit gaps to close or mark before RC acceptance

1. The deterministic `libaudition_temporal_association_cli` now covers
   Hungarian batch matching, diagonal covariance gating and Allen interval
   relations; a **parameterized or graphical** matching scenario remains open.
   The Temporal patterns tab currently runs TMP-04, not Hungarian matching.
2. A self-contained CLI or GUI example for **source/speaker/prototype registry,
   model metadata and factory abstraction** is still missing. The public APIs
   and tests exist; classification and source-identity demonstrations only
   cover a subset.
3. A publicly documented end-to-end **recording export/replay** tutorial
   should show how WAV, source generations, annotation JSON and discontinuity
   gaps relate, using actual outputs, not only a synthetic assertion.
4. For optional real-model/backend panels, run the GUI on one fully provisioned
   configuration and explicitly record which panels were checked. The
   default core-only `qt_self_test` cannot prove model UI behavior.
5. Hardware microphone, model-accuracy and target embedded latency tests are
   separate evidence. They remain NOT MEASURED until independently verified.

## Maintenance rules

- Run `ctest --test-dir build -R '^cli-' --output-on-failure` for CLI examples.
- In a Qt-enabled build, run `ctest --test-dir build-qt -R '^qt_self_test$'
  --output-on-failure`; it asserts nonempty temporal results and working
  catalog search.
- Every new public subsystem should gain a catalog entry and an executable
  demonstration or an explicitly recorded gap. Keep the demos lightweight;
  core-only examples cannot silently download models.
- Do not treat a demonstration on canned input as general acoustic accuracy.
