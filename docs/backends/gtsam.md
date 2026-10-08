# GTSAM spatial fusion backend

The optional GTSAM backend implements `ISpatialFusion` without exposing GTSAM
or Eigen types through the public domain API.

Enable it with:

```bash
cmake -S . -B build -DLIBAUDITION_WITH_GTSAM=ON
```

## Observation contract

A fusion batch represents observations associated with one source hypothesis.
The application is responsible for association and for transforming sensor poses
into the common canonical frame before calling the backend.

`BearingObservation::bearing` is expressed in the local sensor frame.
`BearingObservation::sensor_pose` is the sensor pose in the common frame.
The backend predicts the source direction in that local frame and compares it
with the measured unit direction.

`ScalarRangeObservation` also carries its own `sensor_pose`. Range
observations are therefore self-contained and are never paired with a bearing
implicitly by timestamp.

`PositionObservation` is already expressed in the common frame and carries a
3x3 row-major covariance.

## Uncertainty

Bearing variance, range variance and position covariance are used as native
factor noise. A missing bearing variance uses
`GtsamSpatialFusionOptions::default_bearing_sigma_rad`. Zero or extremely
small variances are floored by explicit backend options so GTSAM never receives
a singular Gaussian noise model.

The fused result exposes GTSAM's marginal 3x3 covariance. The backend does not
invent a calibrated scalar confidence, so `PositionEstimate::confidence`
remains absent.

## Observability and failure

The backend estimates one static `Point3` per `fuse()` call. Sensor poses are
treated as known constants rather than optimized variables. This first slice is
therefore suitable for fixed-pose observations or batches whose poses have
already been estimated elsewhere.

An empty or geometrically underconstrained graph returns `std::nullopt`.
Invalid input such as non-finite geometry, a zero quaternion, negative range,
or a non-positive-semidefinite position covariance raises
`audition::Error`.

No timestamp pairing, pose interpolation, motion model, source identity
association, resampling, or coordinate conversion is hidden inside this
backend.
