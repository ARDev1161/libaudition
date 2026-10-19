# Algorithm test coverage contract

For test owners and new algorithm work, consult the
[test maintenance map](test_maintenance_map.md).

This matrix states which algorithms have executable CTest checks and which
claims **cannot** be made from those checks. The mandatory
`algorithm_coverage_contract` test verifies discovery against the configured
CMake backend flags and the compiled C++ translation-unit inventory.
The test writes `<build>/ctest-algorithm-coverage.json`.

## Existing automated evidence

| Algorithm / subsystem | CTest evidence | Scope of evidence |
|---|---|---|
| Core audio representation, timestamps and buffer layouts | `Time.*`, `AudioBuffer.*` | Unit and rejection paths, clock factories, signed time deltas |
| DSP, calibration, resampling and audio frontend | `Dsp.*`, `AudioQuality.*`, `AudioFrontend.*`, `SplCalibration.*`, `SoundLevelRangePrior.*`, `SamplerateBackend.*` | Unit, multichannel layouts, silence, clipping and invalid levels |
| AEC3 numeric safety (optional) | `WebRtcAec3Backend.SupportsThreeRatesAndPlanarMultichannelCapture`, `WebRtcAec3Backend.ReferenceWithoutCaptureDoesNotCreateAudibleNearEnd`, `WebRtcAec3Backend.NearEndWithoutPlaybackRemainsDetectable`, `WebRtcAec3Backend.RejectsInvalidRatesFramesAndDelaysThenProcessesValidAudio` | 10-ms 16/32/48-kHz frames, planar multichannel output, finite/bounded PCM, silence false-energy and near-end preservation, invalid input recovery; **not ERLE/echo-path accuracy**, CI #1206 PASS |
| Temporal scalar analysis, change points and segmentation (TMP-02) | `TemporalScalarAnalyzer.*`, `PageHinkleyDetector.*`, `TimedActivitySegmenter.*`, `TemporalNumericBoundaries.*` | Synthetic EMA/CUSUM/Page-Hinkley, duration thresholds, clocks, independent state, overflow recovery; **no empirical acoustic detection F1** |\n| Temporal association and spatial gating (TMP-03) | `TemporalIntervalRelations.*`, `EventEpisodeAssociator.*`, `SpatialTemporalGate.*`, `MahalanobisGate.*`, `HungarianEventMatcher.*`, `HungarianEventMatcherOracle.*`, `MahalanobisStability.*`, `TemporalAssociationScenario.*` | Exact interval relationships, bounded associations, spatial/covariance gating, independent exhaustive optimal-assignment oracle, synthetic multi-source scenarios; **no measured physical identity-switch rate** |\n| Temporal recurrence, DTW, motif discovery and source-local template matching (TMP-04) | `RecurrenceAnalyzer.*`, `DynamicTimeWarping.*`, `AcousticMotifFinder.*`, `StreamingPatternMatcher.*`, `temporal_streaming_allocation_probe`, `temporal_motif_allocation_probe`, `PublicHeaders.TemporalAlgorithmsAccessibleFromUmbrella`, `installed_package_cpp17_consumer` | Deterministic onset/clock and numeric validation, exhaustively checked binary motifs, independent streaming suffix oracles, multi-source isolation, cooldown, template lifecycle, bounded candidate storage and external C++17 installation; allocation/latency reports are diagnostic, **not real-audio precision/recall, memory peak or deadline guarantees** |
| Temporal acoustic event detection | `TemporalAcousticEventDetector.*` | Deterministic per-source/class hysteresis, timestamp and clock validation, capacity, gap closure, reset/retire, transactional rejection, caller-owned `AcousticEventAssembler` linkage; **model accuracy not measured** |
| Tracking, association, identity and acoustic events | `TemporalSpatialTrackSmoother.*`, `SpatialIdentityCoordinator.*`, `SourceIdentityResolver.*`, `AcousticEventAssembler.*` | Unit, lifecycle and conflicting state |
| End-to-end routed PCM to semantic acoustic event (controlled models) | `AcousticPipelineScenario.RoutedPcmClassificationAndSpeechPreserveSourceBinding`, `AcousticPipelineScenario.MalformedSourcePcmCannotPolluteOtherEventAndCanRecover` | Two synthetic audio sources through real frontend, classification worker, source-local VAD/ASR runtime, identity resolution and event assembly; failure isolation and recovery, with deterministic model fakes, **not real acoustic-model quality** |
| Per-source audio classification and overload | `SourceClassificationRuntime.*`, `ProgressiveSourceClassificationRuntime.*`, `progressive_stress_*_sources` | Multi-source ordering, queues, freshness, cancellation, and fragmented PCM window offsets |
| VoxConverse v0.3 full dev/test DER baseline | `speech_voxconverse_full_split_preflight_selftest`, `benchmark_voxconverse.py`, `.github/workflows/voxconverse-benchmark.yml` | Real ONNX diarization on complete independently labeled 216-dev/232-test recordings, separate weighted DER and per-clip RTTM, model SHA-256 pinned, Oxford archive hashes logged; **full-split DER pending benchmark workflow result**, not covered by short fixture |
| Independently annotated short recording: real DER | `speech_real_speaker_diarization_reference_der` | Real Pyannote+3D-Speaker inference on independently annotated 10-second FHS toolkit example, pinned Git blobs (WAV + human ground-truth RTTM); **CI #1060 passed**: DER **0.056604139** (5.66%), 0.440406 s missed speech, ~0.000031 s false alarm, 0 s confusion, 7.781 reference speaker-seconds; collar=0, overlap scored; one short fixture only, **no quality threshold** |
| Corpus DER aggregation (reference RTTMs) | `speech_diarization_corpus_scoring_selftest`, `score_diarization_corpus.py` | Rejects missing/extra/mismatched recordings; computes reference-speech-weighted corpus DER plus per-file miss/FA/confusion and diagnostic file-mean; synthetic 2-file self-test (10% micro vs 25% mean), no claimed real corpus accuracy |
| User-provisioned diarization DER benchmark | `speech_real_speaker_diarization_benchmark`, `score_diarization_rttm.py` | Unconstrained real ONNX inference exports RTTM for any mono 16-kHz WAV; separate independently annotated RTTM (e.g. VoxConverse v0.3) required to compute DER; benchmark smoke is **not** a quality threshold |
| Diarization RTTM predictions and DER evaluator | `speech_diarization_scoring_selftest`, `speech_real_speaker_diarization` | Exact-interval, permutation-invariant reference RTTM scoring tested on synthetic speaker turns (miss, false alarm, confusion, overlap and collars); real Pyannote hypothesis exported as RTTM with **no DER claim until independent ground-truth RTTM is provided** |
| Sherpa Pyannote 3.0 speaker diarization | `speech_real_speaker_diarization` | Real segmentation + 3D-Speaker embeddings and English two-speaker WAV; **CI #1032 passed:** 2 speakers, 4 segments, 8.49 s of cumulative segment durations, invalid sample rate rejected. Model/WAV SHA-256 pinned; no DER claim |
| Sherpa Whisper multilingual Language-ID | `speech_real_language_id_whisper` | Real multilingual Tiny INT8 model, official English/German labeled WAVs, repeatability, empty-input and invalid-sample-rate rejection; **CI #1025 passed**, observed `en`, `de`, `en` |
| Sherpa English streaming Zipformer ASR | `speech_real_streaming_asr_zipformer` | Real quantized online model, 100 ms WAV chunks, incremental hypothesis, finalization, reset and independent sessions; upstream speech fixture; **CI #1019 passed**, recognized `LIGHT UP` and produced 44 nonempty partial hypotheses |
| Sherpa GigaSpeech INT8 keyword spotting | `speech_real_kws_gigaspeech` | Official annotated positive WAV (`LIGHT UP`) and one-second negative silence after reset; **CI #1014 passed** |
| Sherpa GTCRN offline and streaming denoiser | `speech_real_gtcrn_denoising` | Genuine ONNX inference on deterministic noisy speech, output contract, session flush/reset; **not** a denoising quality claim |
| Sherpa auxiliary streaming ASR, KWS, language ID, denoising and diarization | `SherpaOnlineAsrContract.*`, `SherpaKwsContract.*`, `SherpaLanguageIdContract.*`, `SherpaDenoiserContract.*`, `SherpaDiarizationContract.*` | Supported model-family configuration, invalid thresholds, missing assets and runtime rejection; **not** real-weight inference quality |
| Per-source VAD/ASR pipeline | `PerSourceSpeechRuntime.*`, `speech_real_*`, `speech_report_scoring_selftest` | Contract tests, real Silero/Whisper/SenseVoice inference, reference-utterance diagnostics |
| ODAS SSL, SST, SSS | `OdasOptions.*`, `OdasBackend.*`, `audio_integration_*` | Deterministic six-channel synthetic ReSpeaker-layout streams and numeric planar DOA gates |
| Echo cancellation | `WebRtcAec3Backend.*`, `qt_aec3` | Backend contract and Qt smoke |
| YAMNet | `real_yamnet_*`, `real_esc10_yamnet_*` | Real ONNX, 1/5-second inference, Python/C++ parity, labels, temporal analysis and 5/10-source stress |
| EfficientAT (MN10/DyMN10) | `EfficientAtBackend.*`, `real_efficientat_*`, `efficientat_frontend_torch_parity` | ONNX, frontend numerical parity, real ESC-10 and AudioSet diagnostic |
| CLAP open-vocabulary embedding/classification | `ClapBackend.*`, `ClapOpenVocabulary.*`, `qt_real_clap` | API tests and model smoke |
| AASIST anti-spoofing | `AasistBackend.*`, `qt_real_aasist` | API and real-model smoke |
| WORLD traits and acoustic analysis | `WorldBackend.*`, `WorldAcousticBackend.*`, `qt_world` | Numeric and frontend/backend smoke |
| GTSAM spatial fusion | `GtsamSpatialFusion.*`, `qt_gtsam` | Spatial constraints/fusion and GUI smoke |
| WAV/JSON recording, lifecycle, timestamps | `WavIoTest.*`, `RecordingTimelineJson.*`, `source_wav_recorder_test`, `recording_audio_apis_test`, `source_recording_session_test` | PCM formats, strict parser rejection, Unicode, limits, file I/O and lifecycle |
| Logging sinks and default routing | `Logging.*` | Levels, sink replacement and null fallback |
| CI test registration integrity | `algorithm_coverage_contract`, `ci_ctest_entrypoints_selftest` | Discovery/source inventory and fail-closed CI CTest selectors |
| C++ core instrumented coverage | `cpp-coverage` GitHub Actions job, CTest | GCC branch/line coverage under `src/` only; gates >=88% lines / >=55% branches |
| Installed binary interface | `installed_package_cpp17_consumer` | External C++17 consumer of installed CMake package |

All `real-*` tests require weights/fixtures and the CMake options documented
in [ctest_suites.md](ctest_suites.md). A minimal configuration does not silently
claim optional-backend coverage.

## Known gaps; do not claim complete algorithm quality

1. **ODAS owns spatial localization and separation quality**, not libaudition.
   Existing CI covers ODAS integration with deterministic multichannel audio,
   numeric DOA smoke checks, generic array geometry, source identity and
   adapter failure paths. Comprehensive DOA/SSS accuracy and device-specific
   geometry calibration belong to separate ODAS evaluations; they do **not**
   block libaudition's core CI. External audio-scene generation, if ever
   used, is explicitly outside the libaudition repository.
2. **Sherpa auxiliary models**: genuine GigaSpeech KWS, Streaming Zipformer ASR,
   multilingual Whisper language ID, GTCRN denoising and Pyannote diarization
   all have model-backed smoke tests. However, alternative online model
   families, DPDFNet, representative language-ID accuracy, and robust
   diarization metrics (DER/JER) still lack quantitative holdout datasets.
   The two-speaker fixture checks the speaker count and segment invariants,
   not the correctness of every timestamp or speaker assignment.
3. **Statistical accuracy**: the ESC-10 subsets and a reference speech phrase
   are regression diagnostics, not unbiased population WER, AudioSet mAP,
   false-alarm rates, or generalization benchmarks. Model metrics need
   versioned representative holdout data.
4. **CPU/GPU/NPU variants**: a passing CPU backend does not validate every
   optional accelerator (RKNN/QNN/DirectML/etc.). Those require their own
   runners and runtime/provider checks.
5. **Optional backend code coverage**: the GCC-instrumented `cpp-coverage`
   job measures only the library's `src/` implementation (CI #1124 baseline:
   90.9% lines, 57.8% branches). Its minimum gates are 88% lines and 55%
   branches. These numbers exclude optional backends and do not establish
   functional correctness or model accuracy. Backend instrumentation and
   branch-level failure-path tests remain follow-up work.
6. **Long-running hardware reliability**: device disconnect/reconnect, ALSA
   capture under load and multi-hour soak tests need physical hardware and
   controlled collection of diagnostics.

The machine-readable contract fails on missing registered test families and
new compiled source units not accounted for in its inventory. This makes
omissions visible without inventing passing results for tests not yet written.
