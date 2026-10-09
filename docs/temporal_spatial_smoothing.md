# Temporal acoustic spatial smoothing

`TemporalSpatialTrackSmoother` is a dependency-free stateful smoother for the
metric acoustic fields already present in `SpatialTrack`:

- `RangeEstimate`,
- `PositionEstimate`.

It deliberately does not smooth direction. Direction tracking is normally
provided by the acoustic spatial backend itself (for example ODAS SST).

## Why this is not a Kalman filter

Consecutive acoustic range/position estimates are usually correlated: they may
share the same microphone block, range prior, tracker history, or GTSAM factors.
Treating every update as an independent Gaussian measurement would shrink
covariance too aggressively.

The smoother therefore uses an explicit two-component moment mixture.

For scalar range with previous distribution `(mu0, var0)`, current measurement
`(mu1, var1)`, and configured current-measurement weight `w`:

```text
mu = (1-w) * mu0 + w * mu1

var = (1-w) * var0
    + w * var1
    + (1-w) * w * (mu0 - mu1)^2
```

The final term represents disagreement between the two estimates. Identical
estimates with identical variance therefore keep that variance instead of
artificially becoming more certain.

The 3D position path uses the corresponding full-covariance mixture formula:

```text
P = (1-w) * P0
  + w * P1
  + (1-w) * w * (x0-x1)(x0-x1)^T
```

so disagreement can also create off-diagonal covariance.

## Process uncertainty

The previous distribution can be inflated before mixing by explicit random-walk
variance rates:

- `range_process_variance_m2_per_s`,
- `position_process_variance_m2_per_s`.

Both default to zero. A moving-source process model is therefore never silently
invented by the library. Applications should configure these rates from their
expected source dynamics and update cadence.

## Lifecycle and time

State is keyed by transient `SpatialTrackId`, not persistent
`AcousticSourceId`.

- updates for one track must use one `ClockIdentity`,
- timestamps must be non-decreasing,
- `endTrack()` removes one track's state,
- `reset()` removes all state,
- exceeding `max_tracks` is an explicit error rather than silent eviction.

A missing range or position does not create a synthetic estimate and does not
advance that metric's last-measurement timestamp. The track-level timestamp
ordering is still remembered once the smoother has state for that track.

When the time since the last measurement of a metric exceeds `max_gap`, that
metric is reinitialized from the new measurement rather than blended with stale
history.

## Output semantics

The first measurement, and measurements after a gap reset, pass through without
changing provenance or confidence.

When range values are actually combined:

- `RangeEstimate::Method` becomes `Fused`,
- `confidence` becomes `Probability::zero()`,
- `distance_m.variance` carries the quantitative uncertainty.

When positions are combined:

- the full 3x3 covariance is propagated,
- `PositionEstimate::confidence` becomes zero.

The smoother validates position covariance as finite, symmetric and positive
semidefinite before it is accepted.

## Composition

The smoother is intentionally a standalone acoustic component. It is not hidden
inside `SpatialIdentityCoordinator` because both the smoother and identity
resolver own state, and implicit nesting would make failure/rollback semantics
ambiguous.

Applications can compose the primitives explicitly, for example:

```text
acoustic observations
  -> ISpatialFusion
  -> SpatialTrack
  -> TemporalSpatialTrackSmoother
  -> ISourceIdentityResolver
```

or run smoothing after identity resolution when only stable published track
geometry is required. This remains entirely inside libaudition's acoustic
boundary; ROS/TF/non-audio fusion remains the responsibility of the consuming
application such as audio_nav2.
