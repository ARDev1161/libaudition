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
