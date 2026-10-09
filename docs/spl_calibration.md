# Sound-pressure-level calibration

`SoundPressureLevelCalibrator` converts a measured digital RMS level in dBFS
into a calibrated physical level in dB SPL.

It is intentionally a small, auditable offset calibration rather than a hidden
microphone model.

## Reference contract

A `SoundPressureCalibrationProfile` stores:

- `profile_id`: human/application-visible profile identity,
- `signal_path_id`: the exact digital path whose level was calibrated,
- reference sound pressure as Gaussian dB SPL,
- dBFS measured while that reference was applied,
- reference frequency,
- A/C/Z weighting metadata,
- additional transfer/repeatability variance.

The derived offset is

```text
offset_db = reference_spl_db - measured_reference_dbfs
```

and the calibration variance is

```text
var_cal = var_reference_spl
        + var_reference_dbfs
        + var_transfer
```

For a new observation:

```text
spl_db = measured_dbfs + offset_db
var_spl = var_measured_dbfs + var_cal
```

All variances are in dB².

## Example

Suppose a 1 kHz acoustic calibrator produces 94 dB SPL and the complete capture
path measures -26 dBFS RMS:

```text
offset = 94 - (-26) = 120 dB
```

A later measurement of -60 dBFS through that same path becomes:

```text
60 dB SPL
```

That calibrated `SoundLevelObservation` can be supplied directly to
`SoundLevelRangePriorEstimator`.

## Signal-path identity

Calibration is only meaningful for the path that was actually measured.
`signal_path_id` therefore participates in validation rather than being passive
documentation.

A path ID should change when any level-changing element changes, including:

- hardware input gain,
- digital microphone gain,
- `AudioFrontend` route gain,
- AGC,
- normalization,
- EQ/filter gain,
- weighting filter,
- any additional fixed digital scaling.

The library rejects a measurement whose path ID differs from the profile.

## Weighting

The calibrator does not implement A- or C-weighting filters.

`SoundLevelWeighting` states which weighting was present in the calibrated
measurement path. A profile and observation must use the same weighting.
For ordinary unweighted RMS from `dsp::signalMetrics()`, use Z weighting unless
the input samples were already passed through an explicit weighting filter.

The reference frequency is retained as calibration metadata. A one-frequency
calibration does not imply flat microphone response across the whole spectrum;
frequency-response compensation, if required, must be a separate explicit
processing/calibration step.

## Silence and invalid input

`dsp::SignalMetrics::rms_dbfs` is negative infinity for exact zero RMS.
The calibrator maps that case to `std::nullopt`: a finite physical SPL cannot
be inferred from exact digital zero without an explicit noise-floor model.

NaN, positive infinity, negative variances, invalid poses, mismatched signal
paths, and mismatched weighting are rejected.

## Typical use

```cpp
const auto metrics = audition::dsp::signalMetrics(audio);

audition::DbfsLevelObservation digital{};
digital.timestamp = audio.captureTime();
digital.level_dbfs = metrics.rms_dbfs;
digital.weighting = audition::SoundLevelWeighting::Z;
digital.sensor_pose = microphone_pose;
digital.signal_path_id = "respeaker_usb_v2.frontend.route0";

const audition::SoundPressureLevelCalibrator calibrator{profile};
const auto spl = calibrator.calibrate(digital);
```

The calibration helper stays inside libaudition's acoustic boundary and has no
ROS, TF, robot-state, or non-audio sensor semantics.
