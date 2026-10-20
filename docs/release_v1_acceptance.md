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

## Verified stacked-PR integration order (2026-10-19)

GitHub PR metadata and branch commit comparisons establish the following
dependency chain. This is a **plan**, not authorization to merge:

1. [#52](https://github.com/ARDev1161/libaudition/pull/52)
   `feat/yamnet-audio-tagger` targets `main` (open, draft).
2. [#53](https://github.com/ARDev1161/libaudition/pull/53)
   `feat/classification-runtime-vocabulary` targets #52 (open, draft).
3. [#55](https://github.com/ARDev1161/libaudition/pull/55)
   `feat/per-source-speech-runtime` targets #53 (open, draft).
4. [#57](https://github.com/ARDev1161/libaudition/pull/57)
   `feat/temporal-acoustic-events` targets #55 (open).
5. [#58](https://github.com/ARDev1161/libaudition/pull/58)
   `feat/temporal-analysis-association` targets #57 (open, draft).
6. [#59](https://github.com/ARDev1161/libaudition/pull/59)
   `feat/temporal-acoustic-patterns` targets #58 (open, draft).

**Competing branch:** [#54](https://github.com/ARDev1161/libaudition/pull/54)
`feat/per-source-vad-asr` independently targets #53. Commit comparison
against #55 reports 18 commits reachable only from #54 and 520 reachable
only from #55 (as of this audit); these branches **diverge**. Do not
blindly merge or cherry-pick #54 after #55, and do not assume its behavior
has been superseded without a focused diff review. Reconcile the actual
`SourceSpeechRuntime` implementation, Qt integration and regression
coverage, then explicitly close/rebase the redundant PR if warranted.

[#56](https://github.com/ARDev1161/libaudition/pull/56) (CTest migration)
is already merged into the #55 feature branch, **not directly into main**.
PRs #57, #58 and #59 are descendants of #55. CI on #59 therefore tests
that descendant snapshot, not a merge of sibling #54 or a release from main.

### Speech runtime reconciliation: observed implementation differences

The #54 branch introduces `SourceSpeechRuntime`
(`audition/speech/source_speech_runtime.hpp`, 2 internal workers). Its
capture-facing `push()` queues audio for asynchronous VAD and ASR, returns a
backpressure/capacity `bool`, and exposes `status()` /
`drainTranscripts()`. The #55 branch instead implements
`PerSourceSpeechRuntime`
(`audition/speech/per_source_speech_runtime.hpp`, an ASR worker).
It processes VAD on the calling path, exposes `trackStatus()` /
`takeResults()`, and has generation-retirement APIs. They are **distinct
contracts**, not merely two spellings of one API.

The #54 tests emphasize asynchronous VAD queueing, a two-source
transcription path, in-flight cancellation after `forget()`, error
isolation and source capacity. #55 has a much larger suite covering
sample-exact pre/post-roll, chunk boundaries, segment caps, source errors
and generation retirement. Before deciding which public API survives,
review requirements for capture-thread blocking, queue backpressure,
timestamp/sample-clock origin and result lifecycle. If keeping #55 only,
record how #54's nonblocking ingestion requirement is satisfied (by a
caller-controlled bounded worker/queue or a separate explicit API) and
preserve equivalent behavioral regression tests. Do **not** claim #54
has been functionally subsumed merely because #55 has more tests.

Before integration, confirm branch heads and PR bases have not changed;
perform a conflict/source/CTest inventory review, merge in dependency
order only with user approval, retarget surviving PRs, and run full CI
on the final combined candidate. Preserve provenance of all optional
real-model tests; green workflow jobs do not imply calibrated quality.

## Feature-branch evidence snapshot (2026-10-20)

- [CI #1446](https://github.com/ARDev1161/libaudition/actions/runs/39408004979)
  on `000d20558088799297d338b06187106035a55566` completed with
  **25 successful jobs and one skipped** `real-spatial-corpus` job.
  This documentation-only update does not establish release readiness.


- [CI #1445](https://github.com/ARDev1161/libaudition/actions/runs/39401475193)
  on `0132531e3909309d5d567af53b4a94254af78b84` completed with
  **25 successful jobs, zero failures and one skipped** `real-spatial-corpus`.
  All four expanded independent installed-package C++17 consumers
  (Ubuntu/macOS, static/shared), sanitizer and optional backend jobs passed.
  This verifies the stacked #59 branch, **not** a merged v1.0 candidate;
  real spatial accuracy and hardware deadlines remain **NOT MEASURED**.


- [CI #1444](https://github.com/ARDev1161/libaudition/actions/runs/39394768807)
  on `d5818578a1e09bf7d384a3275b74e414765ae5f9` completed with
  **25 successful jobs and one skipped** `real-spatial-corpus` job.
  The atomic `retireWithEpoch()` change passed the stacked feature-branch
  matrix, including sanitizers and four independent installed-package builds.
  It does **not** verify an integrated v1.0 candidate or hardware metrics.

- [CI #1441](https://github.com/ARDev1161/libaudition/actions/runs/39375714540)
  on `4a829d312c6ac968b3f594b697762da96fa1168e` completed with
  **25 successful jobs and one skipped** `real-spatial-corpus` job.
  The async speech overflow/generation regression, installed C++17
  consumers and sanitizer/backend jobs passed on stacked PR #59.
  Real-room accuracy and hardware capture deadlines remain **NOT MEASURED**.
- [CI #1443](https://github.com/ARDev1161/libaudition/actions/runs/39387520234)
  on `62815dd9a49f7b8396ae302ed759d3d45b60f546` completed with
  **25 successful jobs and one skipped** `real-spatial-corpus` job.
  The VAD-error propagation and queued-PCM invalidation regressions,
  installed C++17 consumers and sanitizer/backend jobs passed on PR #59.
  This is still a stacked feature-branch result, **not** a v1.0 release gate.
- Concurrency audit: sourceEpoch() followed by retire() was a two-lock
  check/use sequence; a concurrent forget/reuse of the numeric source ID
  could change which epoch actually retired. The atomic retireWithEpoch()
  contract and its regression require fresh CI on their own commit.

- [CI #1440](https://github.com/ARDev1161/libaudition/actions/runs/39360106984)
  on `a1363aff570499c6c1a8540fd14b0fd6029b76a5` completed with
  **25 successful jobs and one skipped** `real-spatial-corpus` job.
  The installed C++17 consumers (Ubuntu/macOS, static/shared) and sanitizer
  jobs passed. This verifies the stacked #59 head, not a final release
  candidate. Skipped real-spatial-corpus remains **NOT MEASURED**.
- Deterministic async speech ingress overflow/generation regression is
  verified by #1441; error propagation and recording cancellation are pending.

- [CI #1427](https://github.com/ARDev1161/libaudition/actions/runs/39339464694)
  on `05e1667a9f2bce92d4b4742bd5d1b31f8f8b7f22` completed with
  **25 successful jobs** and one skipped `real-spatial-corpus` job.
  Four independent installed C++17 consumers passed (Ubuntu/macOS x
  static/shared), along with sanitizers, Qt and optional backend jobs.
- Subsequent source-aware PCM ingress, FIFO source-retirement and
  Qt/ALSA worker/sidecar integration are newer than #1427 and must pass
  their own CI. The app now has an explicit VAD worker and source-epoch
  acknowledgements. Hardware capture deadlines and PR #54/#55 contract
  reconciliation remain open; passing #1427 cannot verify these changes.
- These checks concern a stacked feature branch, **not** a final integrated
  release candidate. Neither physical hardware metrics nor independently
  annotated real-acoustic accuracy has been established by this evidence.

## Mandatory gates (P0)

| Gate | Verification | Current evidence / action |
| --- | --- | --- |
| **Integration** | Required stacked PRs have an explicit merge sequence; combined head is compiled/tested, with no unresolved conflicts or outdated bases | OPEN: speech/classification and temporal work still appear in multiple draft/stacked PRs. Green feature-branch CI does not close this gate |
| **Core behavior** | Default build and CTest on supported Linux and macOS configurations; deterministic numeric, invalid-input, lifecycle, overflow and multi-source tests; no ignored mandatory failures | CI #1445 green on stacked PR #59 **feature branch**, including sanitizers and model-backed jobs. Require a fresh unified-head CI run after integration |
| **C++17 install consumption** | Build/install using documented CMake options, then compile and link an **independent** installed-package consumer of all nine core targets (`core`, `interfaces`, `dsp`, `io`, `spatial`, `memory`, `model`, `logging`, `pipeline`); exercise umbrella/temporal/speech headers, package exports and installed legal notices | **FEATURE BRANCH VERIFIED (expanded scope):** #1445 passes all-target and legal-payload checks on Ubuntu/macOS static/shared. **STILL OPEN FOR v1.0:** rerun on final combined candidate |
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

## Signed audio-clock arithmetic (P0 audit)

`Duration` addition/subtraction and `Timestamp::since()` /
`Timestamp::advancedBy()` reject int64-nanosecond overflow with
`std::overflow_error`, rather than executing undefined signed arithmetic.
`Timestamp::since()` still rejects mismatched clock identities first.
`core_time_test` covers signed limits and valid boundary values.
These changes do **not** validate arbitrary `std::chrono` conversions,
physical clock continuity, or downstream timestamp arithmetic; they must
pass CTest and sanitizers on the final integrated release candidate.

## Installed-package integrity (P0 extension, feature-branch CI verified)

The external C++17 consumer now checks all nine core exported CMake targets,
including actual calls through the I/O, model-registry and logging libraries.
It requires the installed umbrella/speech/temporal headers, CMake export and
version files, Apache license, NOTICE, third-party inventory and retained
third-party license texts. It explicitly points CMake to the installed package
and disables package registries, so a build-tree/previous user package cannot
silently satisfy the test. This expanded test passed all four Ubuntu/macOS static/shared installed-package
CI jobs in #1445 on commit `0132531e3909309d5d567af53b4a94254af78b84`.
It must still run on the final integrated release candidate.

The next P0 check exercises a **relocated** core package (including a prefix
with spaces) and checks imported target locations and installed include paths
against that new prefix. It must pass the four Linux/macOS static/shared CI
jobs before it can be counted as evidence; the optional Sherpa consumer keeps
its existing non-relocated smoke to avoid copying large third-party payloads.

This verifies distribution completeness and ABI/link usability for the tested
platforms only. Windows, hardware realtime deadlines, model accuracy, license
compatibility of optional redistributed binaries, and a fully integrated
v1.0 candidate remain **NOT VERIFIED**.

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

1. Close P0 packaging, source-lifecycle and failure-recovery defects with
   focused regression tests; avoid further TMP-04 optimization without a
   release-blocking reason.
2. Reconcile #54/#55 semantics, review the stacked speech/classification and
   temporal changes, and plan conflict-free integration without dropping CTest
   registrations or optional backend jobs.
3. Run unified core, all-target installed-consumer, sanitizer and selected
   real-model CI jobs on one candidate commit.
4. Audit public headers, ABI/API changes, licenses, version, release notes,
   unsupported platforms and uncalibrated model claims.
5. Make the release decision using the P0 table; only then change version,
   tag and publish after explicit authorization.

This document should be updated with concrete commit hashes and evidence
rather than a percentage-complete estimate.
