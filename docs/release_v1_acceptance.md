# libaudition v1.0 release acceptance gate

This is a **release decision checklist**, not a declaration of readiness.
The library currently declares version `0.5.0` in the root CMake project.
Features in draft/stacked PRs, including the TMP-04 branch, must not be
described as released from `main` until integrated and retested together.

## Scope of the first stable release

The v1.0 contract is an application-agnostic **C++17 acoustic processing
library**. It includes composable core DSP, audio-domain observation models,
source/session lifecycle, acoustic spatial and identity interfaces, temporal
analysis, optional model adapters, and documented CMake consumption. ROS,
robot navigation, odometry/TF, camera/radar fusion, robot behavior and UI
orchestration remain outside the library.

Do **not** require every proposed research algorithm or optional hardware
accelerator for v1.0. Instead, accurately distinguish stable components,
experimental/preview features, and backend-specific limitations.

## Mandatory gates (P0)

| Gate | Verification | Current evidence / action |
| --- | --- | --- |
| **Integration** | Required stacked PRs have an explicit merge sequence; combined head is compiled/tested, with no unresolved conflicts or outdated bases | OPEN: speech/classification and temporal work still appear in multiple draft/stacked PRs. Green feature-branch CI does not close this gate |
| **Core behavior** | Default build and CTest on supported Linux and macOS configurations; deterministic numeric, invalid-input, lifecycle, overflow and multi-source tests; no ignored mandatory failures | Partial evidence: CI #1380 green on TMP-04 branch. Require a fresh integrated-head CI run after merges |
| **C++17 install consumption** | Build/install using documented CMake options, then compile and link an **independent** installed-package consumer of `audition::core`, `audition::dsp`, `audition::spatial`, `audition::memory` and `audition::pipeline`; exercise umbrella header and temporal headers | Installed-package CTest job exists. Explicit temporal umbrella coverage added on TMP-04 branch; rerun after integration |
| **Backend opt-in** | Every advertised enabled backend builds and executes appropriate contract/model-backed tests with pinned assets. Missing required model/WAV must fail rather than produce misleading PASS | Existing model-backed CI jobs and coverage matrix; audit the release's actually enabled backend matrix |
| **Accuracy claims** | Every accuracy/quality number in release notes includes model, version, dataset provenance, reference annotations, metric definition and measured result | Open for broad real-room DOA/SSS, model generalization, WER, DER/JER, and anti-spoofing. **No global quality claim** until measured |
| **Concurrency and bounded resources** | Verify no unbounded source/queue/label growth in documented core paths. Define caller-owned scratch and thread-safety contracts; reject malformed frames without corrupting committed source state | Unit/CTest work exists; TMP-04 includes bounded candidate/source histories and allocation probes. Review remaining adapters |
| **Failure recovery** | Reset, forget, source disappearance, invalid sample rates, timestamps, malformed files, empty audio, capacity and cancellation paths have deterministic tests | Existing map includes broad coverage; final gap review required |
| **Packaging and licensing** | Version consistency in CMake, headers, README, release notes; license and bundled model/third-party provenance documented; installation archive/exports independently verified | Version remains 0.5.0. Do not raise to 1.0 before other P0 gates |
| **Documentation** | Stable/preview API boundary, setup, optional dependencies, sample consumer, CTest map and known limitations published | Existing documentation is extensive but release notes/support table still needed |
| **Release reproducibility** | Exact tag/source commit, CI links, compiler/build option matrix, artifact provenance and supported-platform statement recorded in release notes | NOT VERIFIED until a candidate tag is selected |

**Decision rule:** v1.0 is ready only when every P0 item has dated,
verifiable evidence for the **same integrated release candidate**.
A green check on a feature branch or a synthetic benchmark alone is
insufficient. Never relabel an optional, unavailable hardware/model test
as passed. Do not merge to `main`, create a final release tag or end the
development task without the completed decision.

## Conditional, explicitly scoped evidence (P1)

- **Real acoustic quality:** holdout multichannel datasets with known array
  geometry and independent reference labels for model/scene metrics. Without
  such data, release these algorithms as uncalibrated components with an
  explicit limitation, not as validated real-world detectors.
- **Physical hardware:** ReSpeaker/other microphone arrays, ODAS device
  integration, ALSA disconnect/reconnect, real-time underruns and multi-hour
  soak on the actual embedded targets. Required if the release makes a
  device-level reliability or latency claim, otherwise mark NOT MEASURED.
- **Accelerator-specific backends:** CPU inference does not prove RKNN, QNN,
  GPU, DirectML or any specific provider. Gate only platforms/providers
  listed in the supported release matrix.
- **Runtime profiling:** C++ `operator new` probes count neither all `malloc`
  traffic nor bytes, fragmentation or peak RSS. Record target latency
  percentiles, queue loss, CPU and memory under 1/5/10+ simultaneous sources
  before asserting a real-time budget.

P1 items may become P0 for a particular target product. Their omission
must appear transparently under “Known limitations” and must never become
a fabricated pass.

## Release-candidate verification commands

```sh
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_TESTS=ON \
  -DLIBAUDITION_BUILD_EXAMPLES=ON \
  -DLIBAUDITION_BUILD_QT_DEMO=OFF
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure --no-tests=error
cmake --install build-release --prefix "$PWD/install-release"
```

These commands are a **local baseline only**. They do not replace the
installed-package external consumer, ASan/UBSan, CI optional-backend jobs,
genuine model/corpus tests, or a cross-platform release candidate matrix.
See [CTest suites](ctest_suites.md),
[test-family map](test_maintenance_map.md), and
[algorithm coverage](algorithm_test_matrix.md) for executable evidence.

## Development order

1. Finish and verify the current TMP-04 feature tests; resolve functional
   regressions before adding more microbenchmarks.
2. Review stacked speech, classification and temporal changes for
   compatibility; document merge order and integrate without losing test
   registration or optional-backend jobs.
3. Run unified core, installed-consumer, sanitizer and selected real-model
   CI jobs on the integrated release candidate.
4. Audit public headers, ABI/API changes, licensing, version and release
   notes; explicitly list unsupported platforms and uncalibrated models.
5. Make the release decision using the P0 table; only then change version,
   tag and publish after explicit authorization.

This document should be updated with concrete commit hashes and evidence
rather than a percentage-complete estimate.
