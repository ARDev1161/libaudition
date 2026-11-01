# Agent instructions — libaudition

These instructions apply to the entire repository. Read this file before changing code,
tests, build configuration, or documentation. More specific instructions in a
subdirectory, if present, take precedence for that directory.

## Mission and priorities

`libaudition` is a **backend-agnostic C++17 SDK for acoustic perception**.
Its value is a consistent, easy-to-consume API over specialized libraries,
not independent reimplementations of every DSP, spatial, or ML algorithm.
Prefer small composable interfaces, adapters, and clear contracts over new
frameworks, wrappers around wrappers, or unnecessary custom algorithms.

The immediate v1.0 goal is a usable library and demo, not a research milestone:

1. Make **live and offline ASR in the Qt demo reliable and understandable**,
   especially quiet speech, VAD segmentation, source retirement, and capture
   continuity. Fix underlying library defects in the library, not by hiding
   them in the UI.
2. Make **all supported public capabilities discoverable in the demo**.
   Where practical, provide runnable examples. Clearly distinguish live,
   offline, synthetic, model-backed, hardware-backed, and API/CTest-only
   demonstrations. Never present a placeholder as working inference.
3. Keep the **installed library consumable by `audition_ros2`** (and other
   non-Qt C++17 applications) without copying implementation code out of
   `demo/qt` or depending on ROS, ALSA, or GUI internals.
4. Maintain a lightweight default build, optional backends, predictable
   runtime behavior, and actionable configuration/error messages.

Do **not** make custom ODAS replacements, new localization algorithms,
complete real-world quality corpora, Python bindings, RK3588 tuning, or every
future roadmap item a prerequisite for v1.0. ODAS is the current spatial
backend; improve or replace it only when the task requires it. Accuracy and
hardware limitations must nevertheless be documented honestly.

## Read these sources before relevant changes

- `README.md`, `docs/specification.md`, `docs/architecture.md`:
  mission, public types, and dependency boundaries.
- `docs/functional_architecture.md`: processing flow and capability map.
- `docs/ACOUSTIC_SCENE_SPEECH_BACKENDS.md`: supported VAD/ASR families,
  model formats, languages, and demo limitations.
- `docs/qt_demo.md`, `docs/full_demo_build.md`: demo usage and presets.
- `docs/test_maintenance_map.md`, `docs/ctest_suites.md`,
  `docs/algorithm_test_matrix.md`: evidence and test ownership.
- `CONTRIBUTING.md`, `THIRD_PARTY.md`, `models/manifest.yaml`:
  development and third-party constraints.

Some documents describe historical RC checkpoints or earlier UI layouts.
For *current* behavior, confirm the target branch's headers, CMake options,
tests, and implementations rather than treating old prose as authoritative.
Update affected documentation when behavior or public contracts change.

## Repository boundaries

- `include/audition/`: reusable public C++17 types and interfaces.
- `src/`: backend-independent implementations and infrastructure.
- `backends/<name>/`: optional third-party adapters, with their own
  dependency and linking requirements. Current examples include ODAS,
  sherpa-onnx, whisper.cpp, YAMNet, EfficientAT, CLAP, WebRTC AEC3, and GTSAM.
- `demo/qt/`: optional Qt UI and Linux ALSA capture integration. This is
  a consumer of the library, not the source of its public contracts.
- `examples/`: small library consumers.
- `tests/`: unit, scenario, adapter, real-model, package-consumer, and
  integration checks; CTest is the canonical test runner.
- `cmake/`, `CMakePresets.json`, `.github/workflows/`: dependency,
  build, and CI configuration.

Dependency direction: **applications compose; pipelines use interfaces;
backends implement interfaces; domain types never depend on backends**.
Core/public interfaces must not expose Qt, ROS 2, ALSA, ODAS, sherpa,
whisper.cpp, ONNX Runtime, or GTSAM types. Keep optional third-party
headers out of generic public headers; use backend-specific APIs and
implementation files instead.

`libaudition` produces acoustic observations. ROS topics, TF2, odometry,
camera/radar fusion, Nav2 behavior, and robot policy belong to
`audition_ros2` or another consumer. Do not add ROS dependencies here.

## Public API and audio contracts

- Remain compatible with **ISO C++17** at all public SDK boundaries.
  Respect installed/exported CMake targets and both static/shared builds.
- Prefer typed configurations, narrow interfaces, explicit capabilities,
  and backend-neutral results. No silent fallback to a different model or
  backend after the user selected one.
- Normalized `float32` PCM is the internal audio representation.
  `AudioView` borrows memory; `AudioBuffer` owns it. Do not retain a view
  beyond the source buffer's lifetime. Make sample rate, channels, layout,
  frame size, conversions, and resampling explicit.
- A `Timestamp` refers to the first captured frame; preserve clock
  identity and reject invalid cross-domain arithmetic. Preserve source-local
  sample offsets, especially across gaps, reset, and ID reuse.
- Keep `SpatialTrackId` (transient tracker ID), `AcousticSourceId`
  (persistent acoustic hypothesis), and `SpeakerId` (vocal identity)
  distinct. Never use a reused ODAS numeric ID to attach stale ASR or
  recording data to a new source.
- Distinguish uncalibrated `Score` from calibrated `Probability`;
  preserve unknown/optional confidence and uncertainty. Do not invent
  direction accuracy, confidence, speaker identity, or model capability.
- Document ownership, lifetime, threading, error behavior, reset/flush,
  and output ordering when adding or changing a public API. Check source
  compatibility and installed-consumer builds when altering public structs.

## Streaming and realtime behavior

- Applications own scheduling. An explicitly documented asynchronous
  runtime may own workers; ordinary algorithms must not unexpectedly
  create threads.
- Keep capture-facing work bounded. Do not run model loading, blocking
  inference, unbounded allocations, filesystem operations, or UI work on
  an audio capture callback.
- `PerSourceSpeechRuntime::push()` performs VAD synchronously; only its
  ASR stage runs on a worker. A strict-deadline producer must use the
  bounded asynchronous ingress path or an equivalent caller-owned queue.
- Bound PCM queues, tracked sources, pending ASR segments, retained
  generations/results, and recording annotations. Surface drops,
  overload, timeouts, and failures; do not silently swallow them.
- Handle start/stop/restart, source retirement, late acknowledgements,
  `Forget`, ID reuse, gap/XRUN recovery, flush, and shutdown without
  deadlocks, stale results, or cross-source contamination.
- Keep ODAS spatial PCM geometry/phase intact. Apply source gain,
  denoising, VAD, and ASR-specific processing on an explicit speech branch.
  RMS presence or other diagnostics must **not** implicitly retire SST
  tracks or terminate the overall capture session.
- Prefer predictable, measurable work and reusable buffers on hot paths.
  If an operation allocates or is unsuitable for hard realtime, document
  where it must run rather than claiming realtime safety.

## ASR and optional model backends

- Prioritize **speech audibility, stable segmentation, correct transcripts,
  and non-blocking capture** over cosmetic UI adjustments.
- Diagnose separately: ALSA input, ODAS SSL/SST acquisition, SSS audio,
  speech gain, VAD activity, segment creation, ASR queueing, model inference,
  and transcript delivery. Improving post-SSS gain does not fix failure
  to acquire an ODAS source.
- Preserve start-of-utterance pre-roll when possible, source attribution,
  source generations, recording timeline, and accurate segment end reasons.
  Silence ending a segment must not stop live capture.
- Keep real ASR results intact. Diagnostic detection of repetition or
  hallucination is not permission to silently rewrite recognition output.
- Expose backend and language capabilities honestly. For example,
  SenseVoice is not a Russian-language fallback; Russian speech requires
  an appropriately supported multilingual/Russian model. Sherpa Whisper
  ONNX assets and whisper.cpp GGML assets are not interchangeable.
- Model paths and optional runtime dependencies must be discoverable and
  validated, with actionable errors. Do not assume that enabling a
  CMake option installs compatible weights; do not bundle third-party
  model weights without an explicit licensing/provenance review.
- Backend errors should be isolated from unrelated sources and capture
  where the public contract allows recovery; do not fabricate success.

## Demo requirements

- Keep Qt confined to `demo/qt`. Demo changes must not make the core
  depend on Qt or ALSA.
- The feature catalogue must reflect *actual current* implementations,
  build availability, required models, and where/how to exercise them.
  Add or update catalogue entries when adding capabilities.
- Favor meaningful backend-backed interactions, WAV replay, and synthetic
  deterministic examples where appropriate. Label each honestly.
- Preserve working 3D spatial visualization, source tables, recording,
  classification, and transcript workflows while extending them.
- Keep capture, processing, and model inference off the GUI thread.
  UI controls must have valid effects, cancellation, and understandable
  failure states. Missing models/backends should not crash the demo.
- For ASR changes, test voice → silence → voice, quiet speech, short
  utterances, model failures, source disappearance/reappearance, and
  continuous capture without unintended stop.

## Testing and evidence

1. Reproduce each bug with the smallest meaningful failing test or
   recorded case **before** changing semantics when practical.
2. Add deterministic regression tests for the intended behavior and
   important negative cases, especially timing, concurrency, reset,
   segmentation, and ID reuse.
3. Run the smallest relevant CTest subset, then the broader configured
   suite for changes that affect shared contracts or release readiness.
   `--no-tests=error` is required for filtered CTest runs.
4. When touching an optional backend, configure it and run its enabled
   tests; passing the dependency-free core is not backend verification.
5. For demo UI changes, run `qt_self_test` and relevant model-backed
   tests *when the actual model assets are available*. Manual
   device/model checks must be clearly marked as manual evidence.
6. Verify install/export and an **independent non-Qt C++17 consumer**
   after changing public headers, exported targets, or package config.
7. Report exact commands, outcome, branch/commit, and anything not run.
   Do not claim real-room accuracy, device uptime, model WER, or
   realtime guarantees from synthetic or mock tests. SKIP is not PASS.

### Useful commands

Dependency-light development baseline:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --test-dir build/dev --output-on-failure --no-tests=error
```

Full Qt demo (requires the documented system dependencies; no demo
model-weight downloads with this preset):

```sh
cmake --preset full-demo-no-model-download
cmake --build --preset full-demo-no-model-download
ctest --test-dir build/full-demo-no-model-download \
  -R '^qt_self_test$' --output-on-failure --no-tests=error
```

To run the complete full-demo test matrix, use
`ctest --preset full-demo-no-model-download` after configuring/building
it. For model-backed and hardware tests, provision the required assets
and device separately; see `docs/ctest_suites.md`.
Before claiming that installed packaging works, run the appropriate
`installed_package_cpp17_consumer` configuration as described there.

## Changes, documentation, and delivery

- Keep changes small, focused, and compatible with established interfaces.
  Prefer minimal fixes over speculative refactors; do not expand a demo
  change into an unrelated SDK redesign.
- Follow `.clang-format`, `.clang-tidy`, the repository's compiler
  warnings, and `CONTRIBUTING.md`. Use Conventional Commit messages.
- Do not disable or weaken tests, relax assertions, suppress errors,
  or change CI requirements merely to obtain green checks.
- Keep CMake's **minimal/default** profile light. Optional model and
  hardware backends must remain opt-in. Avoid configure-time large
  downloads unless the user explicitly selected a model-provisioning
  profile.
- Update README, feature catalog, backend instructions, and relevant
  API docs when behavior changes; remove stale claims rather than adding
  conflicting ones.
- Use the **actual target branch** as the basis for changes and check
  current CI on its exact HEAD. Do not treat a successful earlier run as
  evidence for newer commits.
- Do not merge branches, tag releases, push unrelated changes, delete
  branches, or rewrite shared history unless explicitly requested.
- At handoff, summarize actual changes, tests/evidence, remaining known
  limitations, and whether a physical microphone/model smoke was performed.

## v1.0 acceptance orientation

The stable v1.0 candidate should let a developer:

1. Install `libaudition`, select supported optional backends, and use
   public C++17 interfaces without private source/header dependencies.
2. Open the Qt demo, discover the library's capabilities, and operate
   representative real/synthetic workflows without misleading results.
3. Run reliable per-source VAD → ASR with supported model files, including
   continued capture across ordinary speech pauses and source lifecycles.
4. Build the same PCM → spatial processing → per-source speech/classification
   → acoustic observation flow from an independent non-Qt consumer,
   suitable as the foundation of `audition_ros2`.
5. Pass relevant CTest/CI and package-consumer checks on the exact
   release commit, with optional hardware/accuracy limits stated plainly.

Treat the future roadmap as guidance, **not** a mandate to implement
new algorithms before this release.
