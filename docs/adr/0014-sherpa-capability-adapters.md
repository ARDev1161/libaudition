# ADR-0014: map sherpa-onnx to narrow capability adapters

**Status:** accepted

## Context

sherpa-onnx provides many speech/audio capabilities behind one project and
supports multiple execution providers and model families. Exposing sherpa-native
configuration/result types would couple libaudition's public API to one runtime,
while a single large `SherpaBackend` would violate interface segregation.

Several native APIs also do not expose calibrated probabilities even though
libaudition previously required them.

## Decision

- Implement separate classes for VAD, offline ASR, streaming ASR, KWS and spoken-language ID.
- Keep all sherpa-native types in backend implementation files.
- Use typed libaudition model-family configuration.
- Pass execution provider names through `ExecutionTarget`; reject currently unrepresentable execution knobs.
- Preserve unavailable confidence as `std::nullopt`.
- Represent native token timestamps as timed tokens, not word timestamps.
- Keep scheduling outside the backend.
- Pin sherpa-onnx and build it as an isolated optional dependency.
- Keep model licensing/provenance independent of sherpa-onnx's Apache-2.0 code license.

## Consequences

Applications can replace individual speech capabilities without knowing
sherpa-onnx. New sherpa features can be added behind existing or new narrow
interfaces without turning the runtime itself into the domain model.
