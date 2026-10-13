# Spatial audio integration CI

This suite uses **real libaudition ODAS**, not Qt, ALSA, or model mocks.
It includes a stdlib-only deterministic Python six-channel PCM16 WAV
generator and an independent validator consuming real ODAS traces.

## Build and execute

    cmake -S . -B build-audio-integration -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DLIBAUDITION_WITH_ODAS=ON \
      -DLIBAUDITION_WITH_SPDLOG=OFF \
      -DLIBAUDITION_BUILD_TESTS=ON \
      -DLIBAUDITION_BUILD_QT_DEMO=OFF \
      -DLIBAUDITION_WARNINGS_AS_ERRORS=ON
    cmake --build build-audio-integration \
      --target audition_offline_integration --parallel 4
    ctest --test-dir build-audio-integration \
      -L audio-integration --output-on-failure --parallel 2

CTest has five cases: fixture generator followed by silence, static,
moving and two-source replay. CTest manages fixture dependencies.
Missing audio data is FAIL, not SKIP.

## Implementation

- generate_spatial_fixtures.py creates deterministic 6-channel 16 kHz
  PCM16 WAVs and a ground_truth.json with SHA-256 and the source paths.
  Channels are: processed reference, four raw microphones, playback
  reference. The four microphone positions match the four-mic ODAS test.
  Plane-wave TDOAs are fractional samples (no rounded interchannel delay).
- audition_offline_integration processes 128-frame hops with simulation
  timestamps through OdasSpatialEngine. It validates SSL unit vectors,
  SST track IDs, SSS mono frames and their temporal correspondence.
  For tracked audio it uses SourceWavRecorder and SourceRecordingSession,
  and then re-opens every resulting WAV/JSON pair with the public readers.
- validate_spatial_fixture.py verifies fixture checksums, launches the
  C++ runner, assesses each observation against ground truth and writes
  metrics.json with angular errors and track-coverage diagnostics.
  The saved observations.csv is per-hop and retains raw direction vectors.

Reports are at build-audio-integration/tests/audio_integration_results/.
GitHub Actions uploads the reports after failures as well as successes.

## Acceptance scope and measured limitations

- silence: no stable false tracks.
- static: real SSL proposals and directions; default ODAS SST can fail to
  acquire an unchanging synthetic voice signal. This remains visible as
  sst_static_track_present=false rather than pretending SST succeeded.
- moving / two: actual SST, SSS, WAV and JSON end-to-end; check all
  structural invariants and that there are no recorder drops.

Passing these functional tests does NOT prove accurate localization or
separation of two simultaneous speakers. The synthetic planar array can
yield near-vertical tracking directions; metrics.json preserves observed
nearest-track angular errors, track coverage and simultaneous track counts.
Quality thresholds are intentionally not fabricated.

For calibrated performance gating, the Python validator supports optional
--max-median-error-deg and --min-track-coverage arguments, plus
--require-static-track to enforce stationary SST acquisition. Apply only
after validating the bearing convention and a real baseline.

## Optional near-plane source search: measured CI improvement

A four-microphone array in the XY plane is ambiguous in elevation.
**The standard upstream ReSpeaker mask (80/100 degrees relative to +Z)
does not constrain SSL to a narrow band around the XY plane**; it
permits polar directions and may allow SST to acquire an incorrect pole.
The unmodified, original upstream profile remains the default.

When it is physically justified that speakers are within 25 degrees
above or below the array XY plane, opt in to
`audition::applyOdasNearPlaneSourcePrior(options)` from
`<audition/backends/odas/source_elevation_prior.hpp>`, **after**
`audition::applyReSpeakerUsb4MicAngularProfile(options)`.
The prior replaces the SSL spatial filters with two opposing +Z/-Z
masks. It prevents polar source proposals from entering ODAS SST,
rather than projecting or cosmetically relabeling the output tracks.
The default half-band is 25 degrees, with a 5-degree taper, and both
values can be specified explicitly. Never use it when elevated source
directions must remain detectable.

Comparative results on the pinned synthetic ReSpeaker-layout fixtures
(CI #924; default ReSpeaker profile versus near-plane prior):

| Scene | Default SST median angular error | Near-plane SST median angular error |
|---|---:|---:|
| Static | No SST track | 15.68 deg |
| Moving | 85.21 deg | 17.77 deg |
| Two speakers | 88.46 deg | 24.45 deg |

Near-plane runs eliminated the SST pole collapse (from all near-polar
to none) and yielded 177 hops containing two simultaneous tracks,
compared with zero previously. Those are **synthetic ground-truth
measurements**, not demonstrated hardware accuracy. The source
generator assumes plane-wave propagation and ideal identical mics.

CTest now includes three strict `audio_integration_near_plane_*`
tests with generously calibrated quality gates: at most 50-degree
median DOA error, at most 10% near-polar tracks, at least 70% temporal
track coverage, required static-source acquisition, and >=40
simultaneous-track hops in the two-source scene. The three original
upstream-profile tests remain present and still report baseline
limitations without hiding them.

Qt Acoustic Scene offers the checkbox
**Optional 2D source prior / Limit SSL search to sources within +/-25 deg**
for both live ALSA and offline WAV analysis. It is OFF by default.

To compare offline output directly:

    ./build-audio-integration/tests/audition_offline_integration \
      /tmp/respeaker-raw.wav /tmp/replayed-default

    ./build-audio-integration/tests/audition_offline_integration \
      /tmp/respeaker-raw.wav /tmp/replayed-near-plane --horizontal-sources

Compare observations.csv in these directories, preferably against
independently measured source directions. A good synthetic test does not
establish that the restriction is appropriate for your mounted robot.

## Replaying a physical ReSpeaker capture

Use the C++ runner directly on a 6-channel 16 kHz file with a sample
count divisible by 128. Reorder physical mic channels to 1,2,3,4 if
necessary, but preserve their original relative timing:

    ./build-audio-integration/tests/audition_offline_integration \
      /tmp/respeaker-raw.wav /tmp/replayed-odas

Such a replay checks integration and produces observations and WAV/JSON.
To score accuracy, collect a separate, independently measured source
azimuth/elevation trajectory and microphone orientation. Do not treat
the visual tracker or its own output as ground truth.

## Real ONNX model WAV checks in existing CI

Two existing model CI jobs now exercise **actual audio files**, in addition to
the model-only smoke tests:

- `efficientat-real-weights`: uses the pinned YAMNet ONNX model, converts
  its independently generated 5-second PCM16 fixture to WAV, decodes it via
  `audition::io::loadWav()`, runs quick 0.975-second and 5-second
  classifications, and asserts valid, repeatable ranked probabilities.
  Output: `yamnet-real-wav-report.txt` (job log).
- `sherpa-backend`: builds `real_speech_model_replay`, downloads SHA-256
  verified Silero VAD and SenseVoiceSmall INT8 weights and tokens,
  synthesizes a deterministic English utterance with espeak-ng and converts
  it to mono 16 kHz PCM16 WAV. Silero processes real 512-sample chunks,
  SenseVoice decodes the WAV twice, and the test requires non-empty stable
  ASR text with preserved segment ID. A standalone scorer reports
  Unicode-normalized **WER** and **CER** against the exact source text.
  The `real-speech-model-integration` CI artifact retains the transcript,
  VAD activity statistics, WER and CER.

The generated espeak-ng voice is not a natural conversation or measured
ReSpeaker speech. The decoder's non-empty output and stable identity are
integration gates. WER/CER remain explicitly **diagnostic**: there is no
arbitrary accuracy threshold. The scorer supports optional calibrated
`--max-wer` and `--max-cer` gates.

`validate_spatial_fixture.py` also reports the median angular error
of the nearest SSL proposal and the fraction of SST directions with
`abs(z) >= 0.9` (a track-pole-collapse indicator). When calibrated, use
`--max-median-proposal-error-deg` and
`--max-track-near-vertical-fraction` to enforce these metrics.

## Not implemented by this suite

Real-room ReSpeaker angular accuracy benchmarks, calibrated WER/CER
quality gates on natural multilingual speech, and dedicated wall-clock
real-time/backpressure benchmarks are separate future stages.
