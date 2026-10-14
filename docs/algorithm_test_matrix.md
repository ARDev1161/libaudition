# Algorithm test coverage contract

This matrix states which algorithms have executable CTest checks and which
claims **cannot** be made from those checks. The mandatory
`algorithm_coverage_contract` test verifies discovery against the configured
CMake backend flags and the compiled C++ translation-unit inventory.
The test writes `<build>/ctest-algorithm-coverage.json`.

## Existing automated evidence

| Algorithm / subsystem | CTest evidence | Scope of evidence |
|---|---|---|
| Core audio representation, timestamps and buffer layouts | `Time.*`, `AudioBuffer.*` | Unit and rejection paths |
| DSP, calibration, resampling and audio frontend | `Dsp.*`, `AudioFrontend.*`, `SplCalibration.*`, `SoundLevelRangePrior.*`, `SamplerateBackend.*` | Unit and deterministic DSP |
| Tracking, association, identity and acoustic events | `TemporalSpatialTrackSmoother.*`, `SpatialIdentityCoordinator.*`, `SourceIdentityResolver.*`, `AcousticEventAssembler.*` | Unit, lifecycle and conflicting state |
| Per-source audio classification and overload | `SourceClassificationRuntime.*`, `ProgressiveSourceClassificationRuntime.*`, `progressive_stress_*_sources` | Multi-source ordering, queues, freshness, cancellation |
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
| WAV/JSON recording, lifecycle, timestamps | `source_wav_recorder_test`, `recording_audio_apis_test`, `source_recording_session_test` | File I/O and lifecycle regression |
| Installed binary interface | `installed_package_cpp17_consumer` | External C++17 consumer of installed CMake package |

All `real-*` tests require weights/fixtures and the CMake options documented
in [ctest_suites.md](ctest_suites.md). A minimal configuration does not silently
claim optional-backend coverage.

## Known gaps; do not claim complete algorithm quality

1. **Real ReSpeaker microphone-array localization and separation**: existing
   gates use deterministic simulated six-channel recordings. Actual DOA
   accuracy, source-association rate and SSS quality must be measured on
   annotated physical ReSpeaker recordings with measured geometry,
   directions and synchronization. A hardware-in-the-loop runner is separate
   from hosted CI.
2. **Sherpa auxiliary models**: streaming ASR, keyword spotting, language
   identification, DPDFNet inference and speaker diarization are
   implemented, but the registration contract does not establish that each
   has a quantitative real-weight acceptance dataset and accuracy thresholds.
   Configuration tests are not equivalent to functional inference.
3. **Statistical accuracy**: the ESC-10 subsets and a reference speech phrase
   are regression diagnostics, not unbiased population WER, AudioSet mAP,
   false-alarm rates, or generalization benchmarks. Model metrics need
   versioned representative holdout data.
4. **CPU/GPU/NPU variants**: a passing CPU backend does not validate every
   optional accelerator (RKNN/QNN/DirectML/etc.). Those require their own
   runners and runtime/provider checks.
5. **Code and branch coverage**: CTest discovery checks that tests are
   registered, not that all statements, branches or malformed inputs are
   exercised. A coverage-instrumented compiler/build with reports is required
   before making numerical code-coverage claims.
6. **Long-running hardware reliability**: device disconnect/reconnect, ALSA
   capture under load and multi-hour soak tests need physical hardware and
   controlled collection of diagnostics.

The machine-readable contract fails on missing registered test families and
new compiled source units not accounted for in its inventory. This makes
omissions visible without inventing passing results for tests not yet written.
