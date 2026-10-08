# WORLD voice-traits backend

The optional WORLD backend exposes measured fundamental-frequency statistics
through `IVoiceTraitsEstimator`.

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

## Threading

The adapter is synchronous and stateless between calls. It creates no scheduler
threads. Applications decide where and how often to run voice analysis.

## License

WORLD is distributed under a modified BSD license. The optional backend pins the
upstream source revision and retains the license in `LICENSES/WORLD.txt`.
No WORLD types appear in installed libaudition domain interfaces.
