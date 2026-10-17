# Test maintenance map (libaudition)

Entry point for maintainers and coding agents extending algorithms or fixing
regressions. For complete instructions see [CTest suites](ctest_suites.md),
[algorithm evidence](algorithm_test_matrix.md), and
[measured core coverage audit](TEST_COVERAGE_AUDIT.md).

## What a green check establishes

- **Discovery**: a configured CTest build registers the expected test cases.
  `algorithm_coverage_contract` uses CTest JSON and compiled-source inventory.
  Registration is NOT proof of correct numerical output.
- **Behavior**: a deterministic test asserts an output, metric, invariant,
  expected error or transactional state; construction-only smoke is weaker.
- **Accuracy**: inference or spatial output is compared to **independently
  annotated** data with named metrics and calibrated acceptance thresholds.
  Synthetic labels and a model's own output are not real ground truth.
- **Operation**: state lifecycle, chunk boundaries, multiple concurrent
  sources, cancellation, throughput and bounded memory are exercised.
- **Environment**: CI actually enabled the adapter and provisioned its
  runtime, model weights, audio and geometry.

The standard GCC coverage job instruments **src/** only; it does not measure
optional adapter code, Qt or external dependencies. On successful CI #1202
(2026-10-17), the core covered 2840/2909 lines (**97.6%**) and
2621/3731 branches (**70.2%**). The 70% planning milestone is met, not a
claim of complete model/hardware validation.

## Test-family index

Patterns below denote CTest case names or GoogleTest suite names, *not
necessarily labels*. A build registers only its configured backends. Use
`ctest --test-dir <build> --show-only=json-v1` to inspect actual names.

| Test family | Names / owning files | Evidence and limitation |
| --- | --- | --- |
| Core contracts, types, time | `Time.*`, `AudioBuffer.*`, `Probability.*`, `Direction.*`, `FactoryRegistry.*`, `ModelRegistry.*`, `PublicHeaders.*`; `tests/core_time_test.cpp` etc. | Time/clock math, IDs, layout, factories, public API; no acoustic quality |
| DSP, frontend, SPL | `Dsp.*`, `AudioQuality.*`, `AudioFrontend.*`, `SplCalibration.*`, `SoundLevelRangePrior.*` | Channel routing, gain, clipping, metrics, delays, calibrated inputs; not microphone measurements |
| Memory and identification | `MemoryRegistry.*`, `SoundPrototypeMatcher.*`, `SourceIdentityResolver.*`, `SpatialIdentityCoordinator.*` | Registries, stable identity, track handoff, eviction and failure consistency; synthetic observations |
| Spatial estimation and events | `TemporalSpatialTrackSmoother.*`, `AcousticEventAssembler.*`, `AcousticPipelineScenario.*` | Uncertainty, covariance, lifecycle and multi-source events; not measured ODAS DOA accuracy |
| Per-source classification | `Vocabulary.*`, `SourceClassificationRuntime.*`, `ProgressiveSourceClassificationRuntime.*`, `progressive_stress_5_sources`, `progressive_stress_10_sources` | Windows, queue overload, stale result invalidation, prioritization, multi-source stress; scheduler correctness only |
| Speech and speakers | `PerSourceSpeechRuntime.*`, `SpeechContracts.*`, `SpeakerContracts.*`, `Sherpa*` | VAD/ASR session/ID lifecycle and backend contracts; quality requires model-backed tests |
| Recording and parsing | `WavIoTest.*`, `RecordingTimelineJson.*`, `source_wav_recorder_test`, `source_recording_session_test`, `recording_audio_apis_test` | PCM/JSON validation, stream and file lifecycle, malformed input; not device uptime |
| ODAS and GTSAM integration | `OdasOptions.*`, `OdasBackend.*`, `audio_integration_*`, `GtsamSpatialFusion.*` | Adapter, synthetic SSL/SST/SSS metrics and spatial fusion; genuine room accuracy needs independent corpus |
| AEC and resampling | `WebRtcAec3Backend.*`, `SamplerateBackend.*` | Frame contracts, reset, flush and layout; no broad ERLE/double-talk acceptance gate yet |
| Inference adapters | `EfficientAt*`, `Clap*`, `Aasist*`, `World*`, `Sherpa*` and real YAMNet tests | Config validation, numerical/parity and real ONNX smoke when configured; not unbiased mAP/WER |
| Corpora and scoring | `audio_respeaker_manifest_selftest`, `audio_locata_converter_selftest`, `audio_real_spatial_bundle_selftest`, `speech_*scoring_selftest`, `speech_real_speaker_diarization_reference_der` | Archive checks, provenance, independent reference scoring; real acceptance only with reference audio/labels |
| CI integrity and packaging | `algorithm_coverage_contract`, `ci_ctest_entrypoints_selftest`, `branch_coverage_audit_selftest`, `installed_package_cpp17_consumer` | CTest-only registration, source inventory, branch report integrity, installed C++17 usage |

**Source-of-truth mapping**: `tests/CMakeLists.txt` owns most registrations;
`tests/integration/audit_ctest_algorithms.py` owns the required-case and
compiled-source inventory. Backend test files are under `tests/`; backend
implementation is under `backends/<name>/src/`. See
[algorithm_test_matrix.md](algorithm_test_matrix.md) for full evidence and
known gaps. No test may claim accuracy merely because its suite is registered.

## Representative streaming regressions

The streaming test batch after CI #1203 specifically asserts **observable
behavior**, not merely execution:

- `AudioFrontend.ChunkPartitioningPreservesCalibratedMultichannelSamples`:
  3-input-channel routing with delays and gains, interleaved/planar inputs
  and outputs, short and empty chunks; canonical PCM equals a monolithic pass.
- `AudioFrontend.ResetMatchesFreshInstanceAfterFragmentedHistory`:
  resetting after partial delay history must behave like a fresh instance.
- `SourceClassificationRuntime.InterleavedTrackChunksPreserveWindowSamplesAndOffsets`:
  two interleaved sources with different push partitions produce exact
  overlapping sample windows and independent PCM offsets.
- `PerSourceSpeechRuntime.HomogeneousVadChunksPreservePreAndPostRollSegmentation`:
  speech/pre-roll/post-roll lengths agree for different partitions of
  **homogeneous** VAD decisions. Do not claim arbitrary VAD chunk invariance:
  the VAD model itself is explicitly chunk-based.
- `PerSourceSpeechRuntime.PartialPostRollDiscardedOnResetWithoutGhostSegment`:
  reset during post-roll cancels the unfinished old segment and creates
  a new source generation.
- `SamplerateBackend.RepeatedFlushNeverReplaysTailAndResetStartsNewClock`
  and `SamplerateBackend.StreamingOutputClockIgnoresLaterInputTimestampJitter`:
  no repeated flush tail, proper reset of stream time/layout, output continuity
  across capture timestamp jitter.

These are deterministic contracts. They do not establish WER, ERLE,
room acoustics or real-device reliability.

## Cross-component synthetic acoustic scenarios

`AcousticPipelineScenario.RoutedPcmClassificationAndSpeechPreserveSourceBinding`
connects the actual multi-channel frontend, independent classification worker,
per-source VAD/ASR runtime, persistent spatial identity resolver and event
assembler. Two synthetic PCM sources are intentionally distinct: speech on one
channel and machinery on the other. A subsequent tracker handoff must preserve
the first source's classification and transcript without contaminating the
second event. VAD, ASR and classification **models are controlled fakes**,
not genuine acoustic-accuracy evidence.

`AcousticPipelineScenario.MalformedSourcePcmCannotPolluteOtherEventAndCanRecover`
checks that nonfinite source PCM quarantines only one classifier source; the
healthy source still produces a labelled event. Conflicting event source
annotations must be rejected transactionally. Forgetting the bad source epoch
must permit fresh classification without resurrecting stale output.

The test file is `tests/acoustic_pipeline_scenario_test.cpp`. Both cases are
ordinary GoogleTest-discovered CTest tests and need no model download.

## Fast local development commands

~~~sh
cmake -S . -B build-test -G Ninja \
  -DLIBAUDITION_BUILD_TESTS=ON -DLIBAUDITION_WITH_SPDLOG=OFF
cmake --build build-test --parallel
ctest --test-dir build-test -N
ctest --test-dir build-test --show-only=json-v1
ctest --test-dir build-test --no-tests=error \
  -R '^(AudioFrontend|AudioQuality|Dsp)\.' --output-on-failure
ctest --test-dir build-test --no-tests=error --output-on-failure --parallel 2
~~~

Optional components need their **own configured build** and pre-provisioned
assets. Relevant CTest labels include `audio-integration`, `speech-integration`,
`real-model` and `stress`, but are **not** promised in a minimal configuration.
An empty selector must FAIL (`--no-tests=error`).

## CI jobs by responsibility

| Job/group in `.github/workflows/ci.yml` | Responsibility |
| --- | --- |
| `build-test`, `minimal-core`, `sanitizers` | Core builds, default tests, ASan/UBSan where enabled |
| `cpp-coverage` | Core GCC gcovr and branch-audit JSON/HTML artifacts |
| `audio-integration`, `odas-backend`, `gtsam-backend` | Synthetic spatial fixtures, ODAS wrapper and spatial fusion |
| `samplerate-backend`, `aec3-backend` | Streaming resampling and echo backend contracts |
| `sherpa-backend`, `sherpa-tts-backend`, `efficientat-backend`, `efficientat-real-weights`, `clap-backend`, `aasist-backend`, `world-backend` | Isolated model/adaptor configurations |
| `installed-package`, `qt-*` | C++ package consumer and GUI/integration configurations |
| `real-spatial-corpus` | Optional *independently annotated* spatial quality gate; SKIPPED if not configured |

Full VoxConverse evaluation is a separate
`.github/workflows/voxconverse-benchmark.yml` workflow.
Check the precise build flags, fixtures and semantics in
[ctest_suites.md](ctest_suites.md); **optional backend success in one job
does not establish that every backend or accelerator was exercised.**

## Adding or changing an algorithm: required checklist

1. Search its public contract (`include/audition/`), owning `src/` or `backends/`
   implementation, existing `tests/` case names, then this index and the matrix.
   Avoid tests which merely duplicate a previously asserted branch.
2. Add a deterministic output/numeric invariant, invalid-input/failure
   regression, and reset/lifecycle test for stateful algorithms. For streaming
   code cover 10/20 ms and irregular chunks (where applicable), zero input,
   discontinuities, source isolation and bounded queues. **Do not require
   exact chunk equivalence when the algorithm is intrinsically chunk-sensitive.**
3. If an ML or acoustic benchmark is added, pin model/data versions, hashes
   and provenance; define a reproducible metric and acceptance rule. Distinguish
   independent real annotations from synthetic targets and diagnostics.
4. Register every test through `gtest_discover_tests` or `add_test` in CMake.
   Add truthful CTest labels, timeouts, and producer/consumer `FIXTURES_*`.
   All workflow test execution must go through CTest, never hidden shell smoke.
5. Update `tests/integration/audit_ctest_algorithms.py` for new compiled
   translation units, backend options and mandatory algorithm test families.
   Keep self-test coverage for changes to the audit itself.
6. Update [algorithm_test_matrix.md](algorithm_test_matrix.md) with test names,
   evidence level, metric (if measured), missing quality claims, and optional
   asset requirements. Update this map for new groups; extend
   [ctest_suites.md](ctest_suites.md) for changed setup or CLI.
7. Run matching narrow CTest locally, then entire configured local suite.
   Push **one coherent CI batch** (several useful cases together). On red CI,
   diagnose and fix before adding unrelated tests. Never lower a validated
   threshold merely to obtain green.
8. When a fixture or physical device is unavailable, mark the corresponding
   evidence NOT MEASURED; a skipped optional quality job is not PASS.

## Limited transition budget before expanding the algorithm base

With 70.2% core branches already reached, cap additional standalone
testing work to **approximately 3–5 CI runs**. At 10 minutes each that is
**30–50 minutes of CI execution**, excluding implementation/debugging time.

1. This maintenance documentation and discovery contracts: one CI run.
2. Batch streaming/chunking invariants and recovery tests: one to two runs.
3. One richer cross-component multi-source PCM-to-event test: one run.
4. If reproducible evidence is available, targeted AEC3 numeric tests or a
   small independent real-corpus quality gate: zero to one run.

These are planning estimates, not guarantees. Move heavy data collection,
real-model statistics, accelerator runners, and soak tests into ongoing tracks
alongside **new algorithm development**, not as blockers. New algorithms
follow the above checklist from their first implementation.
