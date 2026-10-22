# libaudition v1.0.0-rc.1 — release candidate gate

**Status (2026-10-22): PREPARATION — NOT TAGGED / NOT PUBLISHED.**

This is the single current checklist for release-candidate readiness. The older
[acceptance audit](release_v1_acceptance.md) contains dated historical snapshots
and should not be interpreted as an inventory of still-open feature PRs.

## Integration completed

The final `main` integration commit is
[`8371a1f`](https://github.com/ARDev1161/libaudition/commit/8371a1f0335049032bab62ae81f48d2c641bc03d).
GitHub reports **merged** PRs #52 (YAMNet), #53 (classification runtime),
#55 (speech runtime and CTest foundation), #57 (temporal events),
#58 (change detection/association), and #59 (patterns, hardening and
release-validation work). The competing #54 speech runtime and the duplicate
#60 epoch-cancellation patch are **closed without merge**, after verifying
the respective behaviors and regressions are represented in #59.

The complete pre-merge stack passed [CI #1560](https://github.com/ARDev1161/libaudition/actions/runs/39698811504).
That is not evidence of CI on the new merged `main` commit or this
versioned release branch. The release-branch code HEAD
`a95e46562d711bcb3a5e49e7a674b42682203f56` passed
[CI #1616](https://github.com/ARDev1161/libaudition/actions/runs/39768223220):
**25 successful jobs and one `real-spatial-corpus` skipped**,
including Linux/macOS core, ASan/UBSan, Qt5/Qt6 self-tests and four
installed-package C++17 consumers. This includes the interactive
scalar/event/episode Qt panel and multiple
[positive model-backed Qt tests](qt_model_validation_matrix.md).
**Later documentation or code changes still require a final exact-HEAD
CI before release approval.** The skipped real spatial corpus is NOT
evidence of measured DOA/SSS performance.

## Version contract

- CMake `project(libaudition VERSION 1.0.0)` advertises the numeric CMake
  package version `1.0.0` (CMake's standard `VERSION` is numeric).
- `audition::kVersion` is `1.0.0-rc.1`; public major/minor/patch are 1/0/0.
- The proposed Git tag is `v1.0.0-rc.1`, **only after the gates below pass**.
- `find_package(libaudition 1.0.0 EXACT CONFIG REQUIRED)` checks the numeric
  CMake version and **cannot distinguish RC artifacts from a later stable
  1.0.0 build**. Consumers must select the intended package prefix/artifact
  and inspect `audition::kVersion`; do not advertise the RC as final 1.0.0.

## Required before tagging

- [x] Merge the primary stacked feature PRs into `main` in dependency order.
- [x] Reconcile and close sibling PRs #54 and #60 without importing duplicate APIs.
- [x] Preserve optional backend provenance/CI integrity and all CTest suites.
- [x] Verify complete core Linux/macOS, ASan/UBSan, optional model-backed,
      Qt, and independent installed-package C++17 consumers on code commit
      `a95e465` ([CI #1616](https://github.com/ARDev1161/libaudition/actions/runs/39768223220)).
- [ ] Reconfirm a single successful full CI on the **final exact release PR HEAD**
      after all release documentation changes; record SHA and run URL.
- [x] Verify public headers, version constants, CMake export paths,
      downstream `find_package` and Release build/install against CI #1608
      (four successful independent installed-package C++17 consumers).
      Reconfirm this gate on the final immutable candidate commit.
- [x] **RC demo coverage:** Qt Feature catalog, Temporal patterns
      (including editable Hungarian and scalar/event lifecycle), and all new
      CLI examples pass CTest on CI #1616. The
      [demo inventory](rc_demo_coverage.md) explicitly records API-only
      and device/model verification limits.
- [x] Review `CHANGELOG.md`, `NOTICE`, `THIRD_PARTY.md`,
      `models/manifest.yaml` and supported-platform claims. Default
      source/install packages keep model weights unbundled; all 14 manifest
      models explicitly deny redistribution approval pending review.
      Optional WebRTC AEC3 extraction/patent-file provenance and original
      SenseVoice weight terms remain **unresolved** and are not represented
      as cleared rights. This is a scoped disclosure, not a legal opinion.
- [ ] Make an explicit release decision and only then create `v1.0.0-rc.1`
      and any downloadable GitHub release artifacts.

## Release scope and explicitly excluded claims

The deliverable is an application-agnostic C++17 library with acoustic DSP,
source/audio observations, spatial interfaces and identity, per-source
classification/speech lifecycles, temporal events, change analysis, association,
recurrence, DTW, motifs, and optional backends. The Qt playground is a demo;
ROS2 navigation, robot perception fusion, ODAS geometry calibration and
hardware device support are not part of its core contract.

Third-party model files are **not bundled**. Several CI-only fixture licenses
(in particular SenseVoice original-weight provenance) remain pending. Do not
claim downstream commercial model redistribution is cleared by CI execution.
Likewise, synthetic and pinned-model contract tests do not establish
representative real-room DOA/SSS accuracy, WER, DER/JER, audio-source identity
metrics, RK3588 latency or multi-hour device reliability. Those require
independent reference data and hardware-specific acceptance tests.

The real spatial-corpus CI job can be skipped when an externally hosted,
independently annotated corpus is not configured. **SKIP is not PASS**.
This limitation may be accepted for a library-level RC only if no numerical
real-world spatial quality or device realtime guarantee is claimed.

## Pre-tag reproducibility and human decision

The RC candidate is reproducible from a specific Git commit, not from
`release/v1.0.0-rc1` as a moving ref. Before creating any tag:

1. Confirm the final PR HEAD SHA, GitHub Actions CI run, and 25 required
   non-skipped jobs; never count `real-spatial-corpus` SKIP as PASS.
2. Re-run or verify the installed-package checks for static/shared Linux
   and macOS, `find_package(libaudition 1.0.0 EXACT CONFIG REQUIRED)`
   and public `audition::kVersion == "1.0.0-rc.1"`.
3. Review the staged source archive / future GitHub release assets for
   accidental ONNX/other model weights and third-party notices.
   Do not bundle CI-fetched model fixtures or imply commercial weight rights.
4. Obtain the user's explicit decision to merge PR #61 and create
   `v1.0.0-rc.1`. Until then keep PR #61 draft and **create no tag**.

Documented limitations are acceptable for a dependency-light library
RC only without a positive device/accuracy or model redistribution claim.

## Recording export/replay example (new P0 evidence)

The dependency-light `libaudition_recording_replay_cli` exports two actual
PCM16 WAV files and JSON sidecars, then reopens and validates both generations.
Its CTest smoke includes a negative sample-rate-mismatch check. See
[recording export/replay](recording_export_replay.md). This is a synthetic
file/metadata contract test, **not** live microphone or ASR timing evidence.

Earlier CI [#1597](https://github.com/ARDev1161/libaudition/actions/runs/39709847460)
passed 25 jobs with one skipped `real-spatial-corpus` on
`32d19eee6806a3abb2c4e9e4c4a89bebf8654f04`. Newer RC changes must
pass their own exact-HEAD CI before the gate is checked.

## RC verification baseline

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

The command above is a local baseline, **not a substitute** for full GitHub
Actions, installed-package isolation and optional-backend checks.
