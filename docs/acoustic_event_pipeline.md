# AcousticEvent lifecycle

`AcousticEvent` is the aggregate output object for one caller-defined acoustic
event. `AcousticEventAssembler` provides deterministic lifecycle and consistency
checks without deciding where event boundaries are.

## Explicit lifecycle

The application owns event segmentation:

```text
begin(start_time)
    -> zero or more update(patch) / updateFromTrack(track)
    -> finish(end_time)
```

The assembler never starts a scheduler thread, never applies an inactivity
timeout, and never guesses whether two nearby sounds are the same event.

This is deliberate: a speech segment, alarm burst, continuous machine sound, and
impact transient need different segmentation policies.

## Active state

Active events are bounded by `max_active_events`. Several events may be active
at the same time.

Generated `AcousticEventId` values are monotonic within an assembler lifetime
and are not reused by `cancel()` or `reset()`.

`activeEvents()` returns deterministic snapshots sorted by event ID.

## Atomic patches

`AcousticEventPatch` may contain:

- transient spatial track ID,
- persistent acoustic source ID,
- direction/range/position,
- classification,
- transcript,
- speaker identity,
- voice traits/state,
- authenticity result.

An omitted field means "no update"; it does not clear an existing event field.

Each patch is applied transactionally. Identity, time, and metric geometry are
validated on a candidate copy before the stored active event is modified.

The patch timestamp must:

- use the same `ClockIdentity` as the event,
- be no earlier than the previous event update.

The event's active `end_time` is the timestamp of its latest successful update.
`finish()` may extend that time but cannot move it backward.

## Identity continuity

`AcousticSourceId` is persistent identity for the event. Once set, it cannot
change.

`SpatialTrackId` is transient and may change during one event only when the
handoff update explicitly contains the same persistent `AcousticSourceId`.

This allows:

```text
track 11 -> AcousticSourceId 5
track 11 ends
track 37 -> AcousticSourceId 5
same AcousticEvent continues
```

while rejecting an unproven track switch.

## Spatial snapshots

`updateFromTrack()` copies the latest track identity and acoustic geometry.

A track always supplies direction. Range and position remain optional. If a
later track snapshot has no range or position, the assembler does not erase a
previously known value; missing optional data means no new information.

Temporal smoothing, spatial fusion, and persistent identity remain separate
components. A typical explicit composition is:

```text
ODAS / acoustic spatial observations
  -> spatial fusion / range prior
  -> optional TemporalSpatialTrackSmoother
  -> SpatialIdentityCoordinator
  -> AcousticEventAssembler
```

## Finalization

`finish()` returns the completed `AcousticEvent` by value and removes it from
active state. The library does not persist completed events; storage, ROS
messages, databases, and application history belong to the consuming
application.

`cancel()` discards an event without producing a completed value.

## Scenario coverage

The deterministic scenario tests cover:

- persistent source re-identification after a spatial tracker handoff,
- continuation of one event across that handoff,
- retention of semantic annotations across spatial updates,
- simultaneous independent acoustic sources remaining separate events.

All of this remains inside the acoustic-domain boundary. Robot behavior and
cross-modal interpretation remain outside libaudition.
