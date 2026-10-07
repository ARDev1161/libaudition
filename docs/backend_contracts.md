# Backend implementation contract

Every backend must document and test the following properties.

## Construction

Construction or factory creation performs configuration validation and model/runtime
initialization. Invalid configuration fails early with a libacoustic `Error`; third-party
exceptions must not cross a generic interface boundary.

## Thread safety

Declare one of the following in backend documentation:

- **immutable/thread-safe** — concurrent calls are supported;
- **instance-confined** — one caller/executor owns an instance;
- **session-confined** — the engine can be shared, but each stream requires its own session;
- **externally synchronized** — caller must serialize access.

Streaming VAD, ASR, KWS, resampling, echo cancellation and similar algorithms should
prefer an immutable/shareable engine plus stateful per-stream sessions when the backend
supports that structure.

## Scheduling

A backend must not decide application scheduling. Synchronous calls may be executed by
the application inline, in a worker, in a ROS executor, or offline. A backend may use
internal implementation threads only if required by the underlying runtime and clearly
documented/configured.

## Capabilities

Optional behavior is reported through capability structures; generic code must not branch
on concrete backend names.

Bad:

```cpp
if (backend == "sherpa") { /* enable timestamps */ }
```

Good:

```cpp
if (engine.capabilities().word_timestamps) { /* use timestamps */ }
```

## Time and geometry

Adapters convert native timestamps, coordinate conventions, units and channel layouts at
the boundary. Generic code sees only libacoustic types.

## Models

Backend model loading consumes a validated `ModelDescriptor`. Distributed model artifacts
must appear in `models/manifest.yaml` with immutable provenance, hash and licensing data.

## Errors

Expected observations such as "no source", "no speech", or "no match" are normal result
states (`optional`, empty result, status), not exceptions. Configuration failures, corrupt
models, unsupported formats and runtime backend failures use libacoustic error categories.
