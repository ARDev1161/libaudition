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

The backend converts local bearings to the canonical frame using the sensor
quaternion, adds fixed sensor-position priors, then optimizes a single 3D source
point with GTSAM bearing, range and position factors.

A single unconstrained bearing or range may be geometrically underdetermined.
Such cases return `std::nullopt` instead of fabricating a position.

## Uncertainty

Measured angular/range variance is used when present and valid. Otherwise the
explicit backend defaults from `GtsamSpatialFusionOptions` are used.

The returned `PositionEstimate::covariance_m2` comes from GTSAM marginals.
GTSAM does not provide a calibrated probability that maps to
`PositionEstimate::confidence`, so this backend leaves confidence at zero rather
than inventing one.
