# v0.5 release checklist

This checklist describes the validation required before publishing/tagging
libaudition v0.5.0. The repository version may be prepared at 0.5.0 before a tag
is created; tagging/publishing remains an explicit maintainer action.

## Automated CI

Required green jobs:

- dependency-free strict Ubuntu build + tests,
- strict macOS build + tests,
- minimal dependency-free build,
- installed default-package C++17 consumer smoke test,
- AddressSanitizer + UndefinedBehaviorSanitizer default test suite,
- ODAS backend build/test/install/downstream consumer,
- libsamplerate backend build/test/install/downstream consumer,
- WebRTC AEC3 build/test/install/downstream consumer,
- sherpa speech build/test/install/downstream consumer,
- sherpa TTS build/test/install/downstream consumer,
- CLAP build/test/install/downstream consumer,
- AASIST build/test/install/downstream consumer,
- WORLD build/test/install/downstream consumer,
- GTSAM build/test/install/downstream consumer.

## Version metadata

Before tagging:

- `project(libaudition VERSION ...)` is 0.5.0,
- `audition::kVersion` is `"0.5.0"`,
- public-header tests assert the same value,
- `CHANGELOG.md` has a dated `[0.5.0]` section,
- README status describes v0.5 rather than an older release.

## Public boundary

Verify that public API remains acoustic/application-agnostic:

- no ROS 2 or Nav2 types,
- no camera/OAK-D/radar domain types,
- no non-audio provenance in acoustic range enums,
- third-party backend types stay behind adapters,
- C++17 remains the consumer language contract.

The intended application boundary is:

```text
libaudition
  -> acoustic-domain observations/events

audio_nav2 (or another consumer)
  -> ROS 2 / TF / robot state / non-audio sensors / behavior
```

## Deterministic scenario coverage

The v0.5 suite includes deterministic scenarios for:

- persistent acoustic-source identity after tracker handoff,
- event continuity across that handoff,
- simultaneous sources remaining separate,
- calibrated dBFS -> SPL -> acoustic range prior,
- temporal range/position uncertainty propagation,
- degenerate and outlier spatial-fusion geometry.

Recorded real-room fixtures and hardware-in-loop tests are valuable follow-up
validation but are not required for deterministic v0.5 unit/package correctness.

## Manual release actions

After the release-hardening PR is green:

1. inspect the final diff and CI result,
2. create the `v0.5.0` git tag,
3. optionally create a GitHub release from the `CHANGELOG.md` v0.5.0 section,
4. validate the tagged package in the first real `audio_nav2` integration.

The tag and GitHub release should not be created implicitly by library code or
ordinary CI validation.
