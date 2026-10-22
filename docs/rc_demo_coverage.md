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
| Scalar EMA/CUSUM change detection | Temporal patterns: editable scalar sequence | `libaudition_temporal_events_cli` | `cli-temporal-events-smoke` |
| Temporal hysteresis event lifecycle | Temporal patterns: editable score sequence / thresholds | `libaudition_temporal_events_cli` | `cli-temporal-events-smoke` |
| Persistent-source event episode association | Temporal patterns: episode identity timeline | `libaudition_temporal_events_cli` | `cli-temporal-events-smoke` |
| Allen interval relations, spatial-time/covariance gate and Hungarian batch assignment | Temporal patterns has editable Hungarian cost matrix; interval/covariance gates remain CLI | `libaudition_temporal_association_cli` | `cli-temporal-association-smoke`, `cli-temporal-association-{crossed,gated,penalty-*}` |
| Explicitly linked backend factory registry (mock classifier, no inference) | Feature catalog; API + CTest | `libaudition_factory_registry_cli` | `cli-factory-registry-{smoke,unknown,duplicate}` |
| Source/speaker/sound prototype registries and model metadata | Feature catalog points to Source identity / Backends | `libaudition_registries_cli` | `cli-registries-smoke` |
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
| Recording WAV/JSON timeline, source retirement and replay | Acoustic scene / recording popups | `libaudition_recording_replay_cli --export/--replay` | `cli-recording-export-replay-smoke`, recording timeline/session tests |

The optional backends are not built by the default dependency-light build.
The catalog identifies **API + CTest only** for features without a direct GUI runner;
it never shows a model-only capability as physically verified.

## Explicit gaps to close or mark before RC acceptance

1. The `libaudition_temporal_association_cli` now offers `--scenario crossed`,
   `--scenario gated` and `--scenario penalty --penalty 0.2`, with six
   additional CTest cases (including invalid input). These demonstrate
   one-to-one matching, forbidden edges and the unmatched-cost tradeoff.
   The Temporal patterns tab now runs Hungarian matching on editable
   observation-by-track matrices, with forbidden edges and the unmatched
   penalty. Interval relations and covariance gates remain CLI/API-only.
2. `libaudition_registries_cli` covers source/speaker/sound/model metadata; the
   standalone `libaudition_factory_registry_cli` demonstrates explicit factory
   registration, config forwarding, and duplicate/unknown-backend rejection.
   An interactive registry browser is still missing. Neither example loads
   weights, performs classification, or enrolls biometric identities.
3. The [recording export/replay tutorial](recording_export_replay.md) now writes
   actual WAV and JSON sidecar files, reopens them through public APIs and
   checks generation isolation, gap silence and rate consistency in CTest.
   Live ALSA/ODAS capture, asynchronous ASR timestamps and hardware loss
   accounting still require separate device-level verification.
4. The [model validation matrix](qt_model_validation_matrix.md) records exact
   real-model Qt test cases and successful CI #1608 evidence; pinned-model
   jobs prove a subset of GUI paths. Other Sherpa subpanels, true simultaneous
   physical device capture and deployment-specific quality remain untested.
   The new temporal scalar/event/episode GUI tests require **new exact-head
   CI**, not merely a previous green result.
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

## Parameterized matching quick start

```sh
./build/examples/libaudition_temporal_association_cli --scenario crossed
./build/examples/libaudition_temporal_association_cli --scenario gated
./build/examples/libaudition_temporal_association_cli --scenario penalty --penalty 0.2
./build/examples/libaudition_temporal_association_cli --scenario penalty --penalty 2
ctest --test-dir build -R '^cli-temporal-association-' --output-on-failure
```

These fixed synthetic cost matrices illustrate the public assignment API,
not verified acoustic tracking or source identity on real microphone data.
The `--penalty` argument applies only to the penalty scenario. Malformed
arguments and nonfinite/nonpositive penalties fail with a nonzero exit.
