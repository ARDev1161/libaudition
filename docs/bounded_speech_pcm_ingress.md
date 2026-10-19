# Bounded asynchronous speech PCM ingress (foundation)

`BoundedSpeechPcmIngress` separates input audio from the synchronous
`PerSourceSpeechRuntime::push()` VAD call. It is a **source-aware bounded
queue**, not a second VAD/ASR pipeline and not a self-spawning worker.

Applications can call `push(source_id, AudioView)` on their capture
producer, then dispatch `waitPop()` events from a **separate application
worker thread**. Every accepted PCM block is copied and owned by the queue.
The producer does not run VAD or ASR. It still performs bounded
validation/copying and acquires a mutex: this is not a hard real-time
nonblocking or zero-allocation guarantee.

Limits (defaults): 16 outstanding source identities (active producers or
awaiting worker-side Forget), 256 queued chunks, 256,000
queued samples and 16,000 samples per chunk. Both chunk-count and total
PCM sample limits apply. Invalid shapes, nonfinite samples and oversized
chunks raise `std::invalid_argument` before state is changed.
A previously unseen source is rejected at the source limit without
changing the queue.

**Backpressure/continuity:** if an existing source exceeds queue limits,
`push()` returns `RejectedCapacity`, removes that source's still-queued
PCM, increments its producer epoch, and coalesces a pending `Forget`
event. The consumer receives `Forget` before further PCM of that
source and MUST invoke `PerSourceSpeechRuntime::forget(source_id)`
before passing later blocks to its synchronous `push()`. Other sources'
queued PCM is left untouched. Explicit `forget(id)` follows the same
protocol and permits future numeric ID reuse. `close()` wakes consumers
and lets them finish draining the queue.

**Integration status:** this is a foundational input transport only.
`retire()`, final ASR flushing, source-generation binding to recording
sidecars, and downstream result draining remain responsibilities of an
application-owned serialized speech worker. The existing Qt/ALSA demo
has **not yet switched** to this ingress; it still invokes synchronous
VAD on its input path. Consequently this commit does not yet close the
v1.0 capture-thread integration gate. Never state that this queue alone
proves end-to-end capture deadline or lossless ASR behavior.

The source admission cap includes distinct IDs with undelivered `Forget`
controls. Otherwise fast producer-side source churn could accumulate an
unbounded map of reset notifications when the worker is paused. Once the
consumer pops a final `Forget` for an ID with no active source, its slot
is released. `outstanding_source_slots` reports this union for overload
observability.
