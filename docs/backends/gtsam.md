# GTSAM spatial fusion backend

The optional GTSAM backend implements `ISpatialFusion` for one acoustic-source
hypothesis at a time.

Build with:

```sh
cmake -S . -B build -DLIBAUDITION_WITH_GTSAM=ON
cmake --build build
```

GTSAM 4.2 must be installed and discoverable by CMake. The core and interface
targets remain dependency-free when the backend is disabled.

## Observation contract

- `BearingObservation::bearing` is a unit direction in the **local sensor frame**.
- `BearingObservation::sensor_pose` is the sensor pose in the canonical/world frame.
- `ScalarRangeObservation::distance_m` is measured from
  `ScalarRangeObservation::sensor_pose.position`.
- `PositionObservation::estimate` is already expressed in the canonical/world frame.
- all observations in one fusion call must use the same `ClockIdentity`.

The backend keeps each sensor pose tightly constrained, then optimizes a single
3D source point with GTSAM bearing, range and position factors. Multiple bearing
observations use a deterministic least-squares ray intersection as the optimizer
initialization; timestamp order does not affect that static-batch initialization.

A single unconstrained bearing or range is geometrically underdetermined.
Bearing-only rank-deficient geometry (for example parallel rays) is rejected.
Range-only 3D fusion requires at least four sensor poses whose positions span 3D;
coplanar-only layouts are rejected because they do not provide a unique 3D anchor.
These cases return `std::nullopt` instead of fabricating a position.

## Robust loss

`GtsamSpatialFusionOptions::enable_huber_loss` is disabled by default. When
enabled, a Huber M-estimator with threshold `huber_k` wraps only source
measurement noise models (bearing, range and position). Tight sensor-pose priors
remain non-robust so an outlier cannot weaken the frame anchor.

Robustification is explicit because it changes estimator behavior; callers can
choose it when observations may contain mismatches or transient outliers.

## Uncertainty

Measured angular variance is used when present; otherwise the explicit bearing
sigma from `GtsamSpatialFusionOptions` is used. Range variance is always
explicit in `Gaussian1D`; zero variance is preserved semantically and only
regularized by `minimum_sigma` for numerical stability.

Position-observation covariance must be finite and positive semidefinite.
Numerically zero eigenvalues are regularized only by `minimum_sigma`; indefinite
covariance is rejected as invalid input. Returned marginal covariance is checked
for finite values and positive-semidefinite numerical consistency.

The returned `PositionEstimate::covariance_m2` comes from GTSAM marginals.
With robust loss enabled it is the local marginal around the final robustly
weighted solution, not a globally calibrated uncertainty probability. GTSAM does
not provide a calibrated probability that maps to
`PositionEstimate::confidence`, so this backend leaves confidence at zero rather
than inventing one.
