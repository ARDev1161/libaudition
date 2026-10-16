# Independent real-array spatial acceptance

**Scope.** These tests exercise the actual C++ ODAS spatial engine, SSL/SST/SSS,
recorded mono WAV and JSON sidecars. They measure real audio *only* when
an independently annotated WAV corpus is provided. Generated analytic/synthetic
fixtures are separate tests and are **not** hardware evidence.

The existing historical cache variable and test name contain "respeaker",
but the runner and validator accept arbitrary microphone coordinates and WAV
channel mappings supplied in `array.microphones`. There is no assumption that
the test array uses a ReSpeaker layout.

## Corpus contract

The manifest is placed next to its lossless 16 kHz PCM16 WAV recordings. The
offline ODAS runner currently requires whole 128-frame hops. Include the
capture's independently measured microphone geometry, with actual WAV channel
indices and 3D positions in metres.

Example manifest (illustrative, **not a measurement**):

```json
{
  "schema": "libaudition.respeaker-corpus.v1",
  "array": {
    "microphones": [
      {"channel": 0, "xyz_m": [-0.04, 0.0, 0.0]},
      {"channel": 1, "xyz_m": [0.04, 0.0, 0.0]},
      {"channel": 2, "xyz_m": [0.0, -0.04, 0.0]},
      {"channel": 3, "xyz_m": [0.0, 0.04, 0.0]}
    ]
  },
  "recordings": [
    {
      "id": "capture_a",
      "wav": "capture_a.wav",
      "sha256": "REPLACE_WITH_REAL_64_CHARACTER_LOWERCASE_SHA256",
      "intervals": [
        {"source_id": "a", "start_s": 0.5, "end_s": 2.5, "azimuth_deg": 30},
        {"source_id": "b", "start_s": 1.0, "end_s": 2.3,
         "azimuth_start_deg": -60, "azimuth_end_deg": 60}
      ]
    }
  ]
}
```

Angles are in the microphone coordinate frame, `atan2(y, x)`, not the robot
base frame. If ground truth is measured in a different frame, calibrate and
transform it **before** building the manifest. Do not derive references from
ODAS predictions. Time annotations must refer to the capture timeline.

The format currently annotates horizontal azimuth, not elevation. A vertical
source requires a richer independent 3D reference before full 3D angular
accuracy can be claimed.

## Quantitative metrics

- Per-source tracked-hop coverage, with **distinct** predicted tracks assigned
  to simultaneously active ground-truth sources.
- Per-source median and nearest-rank P90 3D angular errors, in degrees.
- Optional association angle gate: observations farther than the configured
  reference angle are not counted as successfully tracked.
- Fraction of annotated silent hops with **any** spurious track, and total
  silent-hop track observations (proposals are not tracks).
- Fraction of multi-source hops in which **all** active references are matched
  to distinct tracks.
- Adjacent-hop track-ID switches and ID changes after a gap.

No statistic may treat an absent prediction as a zero-degree error. Rejected
non-unit/nonfinite direction vectors invalidate the evaluation. A requested
metric without applicable reference hops (e.g. no silence when testing
false positives, or no overlap when measuring simultaneous coverage) fails
closed rather than passing vacuously.

## CMake + CTest

Select experimental thresholds using independently measured baseline data.
**The numbers below illustrate the command syntax; they are not recommended
universal acceptance criteria.**

```sh
cmake -S . -B build-real-array -G Ninja \
  -DLIBAUDITION_WITH_ODAS=ON \
  -DLIBAUDITION_BUILD_TESTS=ON \
  -DLIBAUDITION_RESPEAKER_CORPUS_MANIFEST="$PWD/corpus/corpus.json" \
  -DLIBAUDITION_REAL_CORPUS_MAX_MEDIAN_ERROR_DEG=35 \
  -DLIBAUDITION_REAL_CORPUS_MIN_TRACK_COVERAGE=0.70 \
  -DLIBAUDITION_REAL_CORPUS_MAX_P90_ERROR_DEG=55 \
  -DLIBAUDITION_REAL_CORPUS_MAX_ASSOCIATION_ERROR_DEG=60 \
  -DLIBAUDITION_REAL_CORPUS_MAX_ADJACENT_ID_SWITCHES=3 \
  -DLIBAUDITION_REAL_CORPUS_MIN_SIMULTANEOUS_TRACK_COVERAGE=0.50 \
  -DLIBAUDITION_REAL_CORPUS_MAX_SILENT_FALSE_POSITIVE_FRACTION=0.10
cmake --build build-real-array --parallel
ctest --test-dir build-real-array -R '^audio_real_respeaker_corpus$' \
  --output-on-failure --no-tests=error
```

The first two thresholds are mandatory when a real corpus is configured;
the other thresholds are optional so datasets without silence or simultaneous
sources can still be evaluated meaningfully. When an optional gate is set,
the corpus **must** contain the corresponding evidence.

Inspect `build-real-array/tests/real_respeaker_results/` for per-recording
`metrics.json`, aggregate `report.json`, captured ODAS trace and WAV/JSON
artifacts. Retain the manifest SHA-256 references with the report for
reproducibility.

## Default CI versus real-corpus CI

Default CI runs `audio_respeaker_manifest_selftest` through CTest. Its
deterministic input traces exercise quality scoring, input validation and
pass/fail behavior, but do **not** establish microphone or room accuracy.
Real physical recordings must be supplied in a separate configured CI job or
by a developer locally; no physical capture corpus is silently fabricated
or inferred from synthetic data.

The ODAS backend remains geometry-neutral. The other model/real-recording
tests (YAMNet, EfficientAT, speech, etc.) measure their own contracts and
must not be interpreted as microphone-array DOA accuracy.

## Importing an independently annotated arbitrary array

The legacy `import_respeaker_recording.py` entrypoint remains compatible with
six-channel captures. For a different array, use
`tests/integration/import_spatial_recording.py`: the `--array` argument is
**mandatory** and must contain independently measured channel indices and
coordinates, for example:

```json
{
  "microphones": [
    {"channel": 0, "xyz_m": [-0.04, 0.00, 0.00]},
    {"channel": 1, "xyz_m": [ 0.04, 0.00, 0.00]},
    {"channel": 2, "xyz_m": [ 0.00,-0.04, 0.00]},
    {"channel": 3, "xyz_m": [ 0.00, 0.04, 0.00]}
  ]
}
```

The annotation is a separate JSON file such as:

```json
{
  "id": "clip_001",
  "intervals": [
    {"source_id": "speaker_1", "start_s": 0.5, "end_s": 2.0,
     "azimuth_deg": 35.0}
  ]
}
```

Run:

```sh
python3 tests/integration/import_spatial_recording.py \
  --wav /data/real_4ch_pcm16_16khz.wav \
  --annotation /data/independently_measured_truth.json \
  --array /data/measured_microphone_geometry.json \
  --corpus /data/libaudition-spatial-corpus
```

The importer checks that all selected channels exist, refuses array-geometry
changes within the same corpus, validates independent annotations, verifies the
copied SHA-256, and stages the WAV and JSON before publication. It refuses
duplicate recording IDs. It does **not** convert audio, guess position labels,
or turn synthetic audio into purported hardware evidence. All imported WAVs
must already be 16 kHz PCM16, have a positive number of complete 128-sample
hops, and share the declared array geometry.

### Public measured data candidate: LOCATA

[LOCATA Challenge dataset](https://zenodo.org/records/3630471) has real
multichannel audio from several arrays (NAO head, Eigenmike, DICIT, hearing
aids) and independent OptiTrack source/array ground truth. The final
development ZIP is roughly **6.2 GB**. Do not download the entire archive on
every PR run. A practical integration uses a small, checksum-pinned subset
from the official corpus that has been explicitly licensed for redistribution
or is fetched from the original provider.

LOCATA material is commonly sampled at 48 kHz, so preparation for the current
16 kHz offline runner requires a separately documented anti-aliased
resampling step. The local coordinate axes, synchronization, source-to-array
transform, and reference angular convention must be reconciled with LOCATA's
ground truth, rather than copying its reported angles as-is. Moving-array
recordings require time-varying geometry and are **not** yet supported by this
static-geometry runner; start with a static-array task.

After a representative real subset is published at a stable URL with a pinned
checksum, it can be retrieved once per CI run and scored via the existing
`audio_real_respeaker_corpus` CTest registration. Until then, ordinary green CI
validates the scoring logic and synthetic ODAS integration, **not** measured
real-world localization accuracy.

## Static LOCATA development recordings: verified ground-truth extraction

The optional converter `tests/integration/prepare_locata_static.py` derives
**full 3D** source bearings from LOCATA's original OptiTrack positions and
array orientation instead of entering an estimated azimuth by hand. The
reference direction for a static source is

```text
source_local = transpose(rotation_array_local_to_world) *
               (source_world - array_origin_world)
direction_xyz = normalize(source_local)
microphone_local = transpose(rotation_array_local_to_world) *
                   (microphone_world - array_origin_world)
```

This follows the array-local transform implemented in the official LOCATA
`cevers/sap_locata_io` `get_truth.m` code.

The source inputs must be present in the extracted **development** data:

- `audio_array_$array.wav`
- `audio_array_timestamps_$array.txt`
- `position_array_$array.txt`
- `position_source_$source.txt`

Use a **Task 1** single, static loudspeaker recording to begin with; do not
use a Task 2 multi-speaker interval unless all active sources have independent
annotations and the evaluation can account for them. The source label is the
suffix used by the source-position filename. The start/end times are relative
to the first array-audio timestamp and must be selected from independently
measured voice-active periods (LOCATA provides separate VAD annotations).
**Do not** derive these speech intervals or bearings from ODAS output.

```sh
python3 tests/integration/prepare_locata_static.py \
  --locata-recording /data/locata/dev/task1/recording1/benchmark2 \
  --array benchmark2 --source SOURCE_NAME_FROM_FILENAME \
  --start-s 0.512 --end-s 2.048 \
  --id locata_task1_recording1_clip1 \
  --corpus /data/libaudition-spatial-corpus
```

`ffmpeg` must be installed on the machine preparing the corpus. The converter
reads the original synchronized 48 kHz (or original rate) WAV, extracts the
explicit short segment, converts to 16 kHz PCM16 without channel remixing,
keeps a whole number of 128-frame hops, and passes the result to the existing
checksum-verifying corpus importer. A clip with non-hop-aligned duration,
truncated audio, missing geometry or timing, non-orthogonal rotation, or moving
source/array is rejected. Time-varying arrays and per-frame trajectory scoring
are outside the scope of this static converter; do not interpret a green
self-test as validating moving-robot LOCATA tasks.

The manifest includes source/array identity, original time interval, SHA-256
of the prepared WAV, a combined SHA-256 of original LOCATA input files, and
the declared coordinate transform. The original microphone geometry is
not assumed to be ReSpeaker.

The `audio_locata_converter_selftest` CTest test uses generated *metadata*
and mocked audio extraction to test time alignment, rejection of moving poses,
rotation transforms, 3D ground-truth correctness and end-to-end manifest
creation. It makes **no claim of real-array acoustic accuracy**. The actual
real-recording evaluation still requires a separately prepared corpus with
calibrated gates.

The final LOCATA release is available via
https://zenodo.org/records/3630471 (ODC-By-1.0; cite Evers et al.).
The development ZIP is about 6.2 GB, so users should prepare, checksum-pin
and store a small selected subset separately instead of downloading the full
archive in every PR-run. This tool does not bundle recordings or bypass their
attribution requirements.
