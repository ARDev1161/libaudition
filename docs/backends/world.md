# WORLD voice analysis backend

The optional WORLD backend exposes both high-level measured pitch statistics
through `IVoiceTraitsEstimator` and frame-level acoustic measurements through
`IVoiceAcousticAnalyzer`.

Build it with:

```bash
cmake -S . -B build-world \
  -DLIBAUDITION_WITH_WORLD=ON \
  -DLIBAUDITION_WITH_SPDLOG=OFF
```

## Scope

`WorldVoiceTraitsEstimator` currently populates only:

- `VoiceTraits::pitch_mean_hz`;
- `VoiceTraits::pitch_stddev_hz`.

It intentionally leaves the following absent:

- estimated age;
- categorical voice traits;
- speaking rate.

WORLD does not provide those quantities, so this backend does not infer or
fabricate them.

## Frame-level acoustic analysis

`WorldAcousticAnalyzer` returns `VoiceAcousticFeatures` containing:

- WORLD time axis in seconds;
- F0 contour in hertz;
- explicit backend-neutral voiced mask;
- CheapTrick spectral envelope;
- D4C aperiodicity;
- sample rate, frame period, FFT size, frame count and frequency-bin count.

The spectral and aperiodicity buffers are contiguous row-major matrices:

```text
index = frame * frequency_bin_count + bin
```

No WORLD-native matrix or option type appears in the installed domain API.

The analyzer uses the same selectable F0 path as the traits estimator
(`DioStoneMask` or `Harvest`), then derives the FFT size from CheapTrick using
the configured F0 floor. `cheaptrick_q1` defaults to the pinned WORLD default
(-0.15) and `d4c_threshold` defaults to 0.85.

The acoustic result is a measured DSP representation. It does not by itself
claim age, emotion, identity, stress, gender or authenticity.

## Algorithms

Two F0 estimators are available through `WorldF0Algorithm`:

- `DioStoneMask` — DIO followed by StoneMask refinement; default;
- `Harvest` — WORLD Harvest F0 estimation.

Configurable parameters include:

- frame period;
- F0 floor and ceiling;
- DIO speed;
- DIO allowed-range threshold;
- minimum number of voiced frames required to publish pitch statistics.

The upstream dependency is pinned to WORLD commit
`d625e7608ca23a870018f01e7c562ac683d9847f`.

## Audio contract

Input is normalized float32 PCM through `AudioView`.

- mono only;
- no hidden downmixing;
- no hidden resampling;
- sample rate is supplied by the input format;
- the configured F0 ceiling must remain below the input Nyquist frequency.

WORLD internally operates on double-precision samples; the adapter converts
libaudition float32 PCM explicitly at the backend boundary.

If the input is empty, malformed or contains non-finite samples, analysis fails
with an explicit error.

## Pitch statistics

WORLD represents unvoiced frames with F0 equal to zero. libaudition filters to
finite voiced F0 values inside the configured floor/ceiling range.

If fewer than `minimum_voiced_frames` remain, both pitch fields are absent.

Otherwise:

```text
pitch_mean_hz   = arithmetic mean(voiced F0)
pitch_stddev_hz = population standard deviation(voiced F0)
```

Zero hertz is never used to mean "pitch unavailable".

## Capabilities

`VoiceTraitsCapabilities` reports:

- `pitch_statistics = true`;
- `estimated_age = false`;
- `categorical_traits = false`;
- `speaking_rate = false`;
- mono audio;
- CPU execution.

An empty supported-sample-rate list means the backend accepts the input sample
rate subject to the runtime Nyquist/F0 contract above.

The frame-level analyzer separately reports
`VoiceAcousticCapabilities { f0_contour, spectral_envelope, aperiodicity }`.
All three are true for the WORLD implementation. It uses the same mono/CPU
execution contract and likewise declares no hidden resampling.

## Threading

The adapter is synchronous and stateless between calls. It creates no scheduler
threads. Applications decide where and how often to run voice analysis.

## License

WORLD is distributed under a modified BSD license. The optional backend pins the
upstream source revision and retains the license in `LICENSES/WORLD.txt`.
No WORLD types appear in installed libaudition domain interfaces.
