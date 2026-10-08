# Audio frontend, resampling, and echo cancellation

The audio frontend is an explicit pipeline layer between device capture and
algorithm backends. It does not own ALSA/Pulse/ROS scheduling and it never
silently changes an `AudioView`.

## Recommended branch topology

```mermaid
flowchart LR
    CAP[Device capture] --> RAW[Raw multichannel AudioView]
    RAW --> CAL[Channel routing + calibration]
    CAL --> SPATIAL[ODAS / spatial processing]

    RAW --> SPEECHMAP[Speech channel selection/calibration]
    REF[Pre-DAC playback reference] --> AEC[WebRTC AEC3]
    SPEECHMAP --> AEC
    AEC --> RS[Resampler]
    RS --> ENH[Speech enhancement / VAD / ASR]
```

Authoritative Mermaid source: `docs/images/audio_frontend.mmd`.

The spatial branch and speech branch deliberately diverge. Speech-oriented AEC,
noise suppression, and enhancement can alter inter-channel phase and therefore
must not be inserted into the microphone-array path by default.

## Channel routing and calibration

`dsp::AudioFrontend` produces one output channel per `ChannelRoute`.
Each route selects an input channel and applies:

```text
y[n] = gain * (x[n - delay_samples] - dc_offset)
```

`delay_samples` may be fractional and is implemented with first-order linear
interpolation. State is retained across calls, so block boundaries do not reset
the delay line. `reset()` clears that history and the locked input format.

Only non-negative delays are accepted: array calibration should choose a common
reference and delay early channels to it rather than inventing future samples.

For the known ReSpeaker 4-Mic USB Array v2.0 six-channel capture layout, the
four physical microphone channels are selected with zero-based indices:

```cpp
config.routes = {
    {1, mic0_calibration},
    {2, mic1_calibration},
    {3, mic2_calibration},
    {4, mic3_calibration},
};
```

This mapping is a device profile, not a hard-coded library assumption.

For precision array work, calibration coefficients should come from measurement.
The current linear fractional-delay interpolator is a lightweight frontend
primitive; a higher-order fractional-delay/FIR calibration block can be added
later without changing the routing contract.

## Quality metrics

`dsp::analyzeQuality()` reports values that can be measured directly from the
samples:

- RMS and RMS dBFS;
- absolute peak and peak dBFS;
- DC offset;
- crest factor;
- clipping ratio;
- per-channel copies of the same metrics.

SNR is intentionally separate. `dsp::snrDb(signal_rms, noise_rms)` requires an
explicit measured/estimated noise RMS. The library does not infer a trustworthy
noise floor from an arbitrary single frame.

## libsamplerate backend

`SamplerateResampler` implements `IAudioResampler` with a stateful streaming
session over libsamplerate. It supports planar and interleaved libaudition
buffers, preserves channel count, exposes `reset()` and `flush()`, and keeps
the output timeline continuous.

The backend is optional:

```bash
-DLIBAUDITION_WITH_LIBSAMPLERATE=ON
```

When no compatible system package is found and dependency fetching is allowed,
the build uses the pinned 0.2.2 source revision.

## WebRTC AEC3 backend

`WebRtcAec3EchoCanceller` implements `IEchoCanceller`.

The reference passed to `acceptReference()` must represent the signal sent
toward the robot loudspeaker as close as practical to the pre-DAC/playback
stream. Using microphone audio as the reference defeats the purpose of
reference-based acoustic echo cancellation.

The current adapter:

- accepts 16, 32, or 48 kHz sessions;
- requires capture and reference to use the same sample rate;
- processes exactly 10 ms frames;
- permits different render and capture channel counts;
- accepts explicit stream delay through `setStreamDelay()`;
- exposes one-shot `notifyEchoPathChange()`;
- reports backend ERL, ERLE and estimated delay when available;
- creates no application scheduler threads.

The standalone AEC3 extraction is C++20 internally, but it is isolated behind a
shared backend library so libaudition consumers continue to compile as C++17.

Enable it with:

```bash
-DLIBAUDITION_WITH_WEBRTC_AEC3=ON
```

## Real-time ownership

All blocks remain caller-scheduled. A typical application may run capture,
spatial processing and speech processing in different workers with bounded
queues, but libaudition does not impose those workers.

An `AudioFrontend`, resampler session, or AEC session is stateful and should be
owned by one stream/executor unless the application provides external
synchronization.
