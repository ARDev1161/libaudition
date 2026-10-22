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
versioned release branch. A successful PR-head run on this release branch
is still required before an RC tag.

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
- [ ] Verify the release PR's *exact HEAD* passes core Linux/macOS, ASan/UBSan,
      optional model-backed/backend, Qt, and all independent installed C++17
      package consumers. Save the workflow run link and commit SHA.
- [ ] Verify unchanged public headers, version constants, CMake export paths,
      downstream `find_package` and a clean Release build/install.
- [ ] Review `CHANGELOG.md`, model provenance, notices and supported-platform
      claims against the RC artifact. Keep optional weights unbundled.
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
