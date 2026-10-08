# ADR-0015: separate speaker inference, search index, and persistent identity

**Status:** accepted

## Context

Speaker embedding, verification, identification, diarization, and durable
speaker memory have different lifetimes.

Sherpa-ONNX provides an embedding extractor, an in-memory embedding manager, and
an offline diarization pipeline. Treating all three as one persistent
"speaker database" would couple libaudition's identity model to one backend.

Diarization cluster numbers are especially unsafe to reuse as persistent
identity: a cluster label is local to one diarization run.

## Decision

- `ISpeakerEmbedder` produces backend-neutral `SpeakerEmbedding` values.
- `ISpeakerVerifier` compares two embeddings and returns similarity plus an
  optional calibrated probability when a backend actually provides one.
- `ISpeakerIdentifier` is a transient computational index with explicit
  enroll/remove/clear operations.
- `ISpeakerRegistry` remains the persistent source of speaker profiles.
- `ISpeakerDiarizer` returns local `speaker_index` cluster labels, never
  `SpeakerId`.
- Missing embedding quality is represented as `std::nullopt`.
- Insufficient audio for embedding extraction is a normal no-result condition.
- Native Sherpa similarity scores stay `Score`; they are not converted to a
  fake probability.

## Consequences

Applications may rebuild an identification index from any durable registry and
may replace Sherpa with a different embedding/search implementation later.
Diarization can be combined with persistent identity only through an explicit
association step.
