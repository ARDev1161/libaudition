# CLI block examples

The example build includes small dependency-light command-line programs for
checking libaudition blocks without a ROS 2 application.

Build them with the normal example configuration:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_EXAMPLES=ON
cmake --build build -j
```

The executables are created under `build/examples/`.

## SPL calibration and range prior

```bash
./build/examples/libaudition_spl_cli
```

Default demo:

- measured level: -40 dBFS,
- calibration reference: -20 dBFS = 94 dB SPL,
- assumed source level: 80 dB SPL at 1 m.

Custom values:

```bash
./build/examples/libaudition_spl_cli \
  -35 -18 94 82
```

Arguments are:

```text
measured_dbfs reference_dbfs reference_spl_db source_spl_at_1m_db
```

Output is machine-friendly `key=value` text containing the calibration offset,
calibrated SPL, and acoustic range-prior mean/variance.

## Temporal range smoothing

With an internal demo sequence:

```bash
./build/examples/libaudition_smoothing_cli
```

Or pass arbitrary range measurements in meters:

```bash
./build/examples/libaudition_smoothing_cli \
  2.0 2.4 1.8 2.2 2.1
```

Each line prints the raw range, smoothed range, and propagated variance.

## Persistent acoustic-source identity

```bash
./build/examples/libaudition_identity_cli
```

The default example creates one transient track, ends it, then presents a second
track with a nearly identical acoustic fingerprint. A successful re-identification
prints:

```text
reacquired=yes
```

To experiment with the two 2D synthetic fingerprints:

```bash
./build/examples/libaudition_identity_cli \
  1.0 0.0 0.98 0.05
```

The four values are:

```text
first_x first_y second_x second_y
```

This utility uses a tiny synthetic embedding only to exercise resolver
semantics; it is not an audio embedding model.

## AcousticEvent lifecycle

```bash
./build/examples/libaudition_event_cli
```

or:

```bash
./build/examples/libaudition_event_cli alarm "please help"
```

It creates a resolved spatial-track snapshot, starts an event, adds
classification and transcript annotations, finalizes the event, and prints the
resulting IDs and fields.

The shell must quote a transcript containing spaces.

## Small end-to-end acoustic composition

```bash
./build/examples/libaudition_pipeline_cli
```

This synthetic path composes several dependency-free v0.5 blocks:

```text
dBFS observation
  -> SPL calibration
  -> source-level range prior
  -> SpatialTrack
  -> temporal smoothing
  -> persistent AcousticSourceId
  -> AcousticEvent
```

A successful run ends with:

```text
pipeline=ok
```

It deliberately contains no ROS 2, TF, camera/radar, or robot behavior.

## CI smoke coverage

When both examples and tests are enabled, CTest also runs all five CLI programs
as smoke tests. This keeps the examples compiling and executable as the public
API evolves.
