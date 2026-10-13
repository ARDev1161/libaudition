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

## Not implemented by this suite

Real-room ReSpeaker accuracy benchmarks, nightly real-model
YAMNet/Silero/SenseVoice inference with WER/CER gates, and dedicated
wall-clock realtime/backpressure benchmarks are separate future stages.
