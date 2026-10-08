# Calibrated sound-level range priors

`SoundLevelRangePriorEstimator` provides a weak metric range prior from a
calibrated sound-pressure-level observation and one or more source-type acoustic
level priors.

It is dependency-free and implements `IRangeEstimator`.

## Input contract

`SoundLevelObservation::level_db_spl` is **dB SPL**, not dBFS. A microphone
capture must therefore be calibrated before it is passed to this estimator.
The ordinary signal-quality helpers report dBFS and cannot be substituted for a
calibrated SPL measurement.

Each observation also carries:

- timestamp,
- sensor pose,
- explicit Z/A/C frequency weighting.

Every `SourceLevelPrior` contains:

- a source-type label,
- Gaussian source level in dB SPL at an explicit reference distance,
- a non-negative relative hypothesis weight,
- the same frequency weighting as the measurement.

A/C/Z weighting mismatch is rejected rather than silently mixed.

## Propagation model

The estimator uses the log-distance model

```text
L(r) = L(r0) - 10 n log10(r / r0)
```

where `n` is `path_loss_exponent`. The default `n = 2` gives free-field
spherical spreading:

```text
L(r) = L(r0) - 20 log10(r / r0)
```

For example, a source prior of 80 dB SPL at 1 m and a calibrated observation of
60 dB SPL gives a 10 m nominal distance under the default free-field model.

Indoor reverberation, occlusion, source directivity and other propagation error
are not hidden inside a magic confidence value. Applications can add explicit
`propagation_variance_db2` or fit a different path-loss exponent from measured
data.

## Uncertainty

The source-level prior and measured SPL are Gaussian in dB. Their difference is
therefore Gaussian in dB, while distance is log-normal.

The estimator computes the exact first and second moments of that log-normal
distribution. It does not linearize the dB-to-distance transform.

When several source-type hypotheses are supplied, their non-negative weights are
normalized over the supplied set and the output mean/variance are the exact
first and second moments of the resulting mixture. The weights are association
weights used for this mixture; the estimator does not reinterpret them as a
calibrated range confidence.

The returned `RangeEstimate` has:

- `method = RangeEstimate::Method::LevelPrior`,
- exact moment-matched `Gaussian1D distance_m`,
- `confidence = Probability::zero()`.

The Gaussian range output is a moment-matched representation required by the
current range/fusion domain contract. The underlying single-hypothesis distance
distribution is log-normal, and a multi-type result is a log-normal mixture.

## Fusion

`estimateObservation()` converts the prior directly into
`ScalarRangeObservation`, preserving the sound-level timestamp and sensor pose.
That observation can be supplied to `ISpatialFusion` together with bearing,
vision, radar or other independent geometry.

Because acoustic source level can vary substantially even within one semantic
class, this estimate should normally be treated as a weak prior unless the
source type and environment are tightly calibrated.
