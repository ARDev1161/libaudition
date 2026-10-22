# Spatial identity integration

`SpatialIdentityCoordinator` is the backend-independent composition block that
connects spatial fusion to persistent acoustic-source identity.

It owns neither algorithm. Applications provide an `ISpatialFusion` and an
`ISourceIdentityResolver`, so GTSAM, another fusion backend, or a custom
resolver can be substituted without changing the coordinator API.

## Update flow

For one `SpatialTrack`:

1. validate the track lifecycle timestamps,
2. validate that every supplied fusion observation uses the same clock identity
   as `track.last_seen` and is not newer than that track update,
3. run `ISpatialFusion` when observations are present,
4. build `SourceIdentityObservation` using the fused position when available,
5. resolve persistent identity,
6. commit the position and `AcousticSourceId` to the track only after the
   resolver succeeds.

If fusion is empty or underconstrained and returns no position, an existing
`SpatialTrack::position` is preserved and may still be used by the resolver.

## Propagation

The coordinator keeps only active `SpatialTrackId -> AcousticSourceId`
bindings. Those bindings can be propagated to matching `SpatialTrack` and
`TrackedAudioFrame` objects in a `SpatialProcessingResult`.

Propagation is conflict-safe: if an object already contains a different source
ID, the whole batch is rejected before any object is modified.

`attachToEvent()` copies a resolved track/source association into an
`AcousticEvent`. Existing conflicting track or source IDs are rejected.

## Lifecycle

`endTrack()` is forwarded to the resolver and removes the active coordinator
binding. `reset()` resets the resolver's transient state and clears all active
coordinator bindings.

The coordinator does not retain historical track-ID bindings after
`endTrack()`; delayed events should carry the resolved `AcousticSourceId`
forward explicitly or retain the resolved `SpatialTrack` used to create them.
This avoids ambiguous association if a tracker later reuses the same numeric
track ID.

## Resolver timestamp and source-aging safety

The `HeuristicSourceIdentityResolver` clock now advances only after an
accepted observation or a completed `endTrack()`. A rejected *new*
track (for example, when `max_active_tracks` is full) does not advance
the global timestamp or evict recent identity candidates. Otherwise,
a later observation from an already active track could be incorrectly
rejected as out-of-order after an unrelated capacity error, and nearby
geometry-backed reidentification could be lost.

Source ages are computed with unsigned 64-bit elapsed nanoseconds after
the monotonic timestamp check. This avoids signed subtraction overflow
when source lifetimes span the extreme negative and positive values
of the signed timestamp representation. Both changes have deterministic
CTest regressions for capacity rejection, cache preservation, and
extreme timestamp intervals. This does **not** make the resolver
thread-safe; callers must still serialize lifecycle operations.

## Resolver recovery when an external source registry throws

The resolver now calls potentially throwing source-registry operations
(`list`, `find`, `addFingerprint`, `create`) before changing its own
track bindings and recent spatial evidence. An active-track fingerprint
write or lookup failure leaves the previous track geometry, identity,
and timestamp intact. A new-track fingerprint failure likewise does
not install an active binding. Expired recent geometry is ignored
during candidate scoring and only pruned when an observation commits,
so a failed registry lookup cannot permanently evict previous
geometry evidence.

This provides a **registry-error exception guarantee for transient
resolver state**, not an atomic transaction spanning the external
registry: if `create()` succeeds and a later fingerprint write fails,
a persistent source profile can remain without an active track.
The `IAcousticSourceRegistry` contract currently exposes no deletion,
rollback, or transactional create-and-fingerprint operation. The
regression tests intentionally acknowledge this limitation rather
than claiming full external rollback. The resolver remains
single-thread-owned.

## Defensive registry ID admission

A custom `IAcousticSourceRegistry::create()` implementation must issue a
valid new `AcousticSourceId`. The resolver rejects invalid IDs and IDs already
present in the registry snapshot or bound to an active track. A rejected
creation does not bind the track, update the transient geometry cache, or
advance the resolver clock; deterministic fault-injection regressions exercise
both invalid and reused IDs. The registry itself remains responsible for the
atomicity of its persistent creation operation.
