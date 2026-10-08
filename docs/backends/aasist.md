# AASIST ONNX authenticity backend

The optional AASIST backend implements `IAudioAuthenticityDetector` using an
ONNX export of the official AASIST anti-spoofing model.

Build it with:

```bash
cmake -S . -B build-aasist \
  -DLIBAUDITION_WITH_AASIST=ON \
  -DLIBAUDITION_WITH_SPDLOG=OFF
```

## Model contract

The supported reference model is the official AASIST variant trained on
ASVspoof2019 logical-access data.

The pinned CI fixture is:

- source: `SpeechAntiSpoofingBenchmarks/AASIST`
- revision: `16774d458d86d2a021ae31646c1bf66a5331b53e`
- file: `aasist.onnx`
- SHA-256:
  `130e536266b7c537f9a13029e1612a9f392fd1cc827783683b6d1c062a3db5e1`

The artifact is not distributed by libaudition.

Native ONNX contract:

```text
wav:    float32 [batch, 64600]
logits: float32 [batch, 2]
```

Class index 0 is spoof and class index 1 is bona fide.

## Audio contract

Application input must be mono float32 PCM at 16 kHz.

AASIST's reference evaluation window is 64600 samples, approximately 4.04 s.

- exactly 64600 frames: used directly;
- shorter clips: repeat-padded to 64600 frames, matching upstream evaluation
  preprocessing;
- longer clips: rejected.

The adapter deliberately does not silently crop or aggregate long recordings.
Applications that need whole-recording analysis should segment speech explicitly
and define their own aggregation/calibration policy.

There is no hidden resampling or downmixing.

## Score and probability semantics

The model produces two raw logits. libaudition exposes those as:

- `AuthenticityResult::bona_fide_score`;
- `AuthenticityResult::spoof_score`.

They are `Score`, not `Probability`.

By default:

- `bona_fide_probability` is empty;
- `spoof_probability` is empty;
- `replay_probability` is empty;
- `synthetic_probability` is empty.

If `AasistOnnxOptions::calibration` is configured, the backend applies:

```text
P(bona_fide) = sigmoid(slope * bona_fide_logit + intercept)
P(spoof)     = 1 - P(bona_fide)
```

The coefficients must be fitted on a representative calibration set for the
target deployment domain. libaudition does not ship default coefficients.

AASIST is a binary anti-spoofing detector. This adapter does not claim that a
spoof score identifies replay specifically or synthetic/TTS/VC specifically.

## Execution

The first slice supports ONNX Runtime CPU execution only. Unsupported
provider/device/precision options are rejected instead of ignored.

The same standalone ONNX Runtime dependency path used by the CLAP backend is
reused. On Linux x86_64 CI can fetch the pinned runtime package automatically.
Other platforms may provide an ONNX Runtime installation through
`LIBAUDITION_ONNXRUNTIME_ROOT` or `ONNXRUNTIME_ROOT`.

## Limitations

AASIST performance is strongly domain-dependent. The reference checkpoint is
trained on ASVspoof2019 logical-access attacks and should not be treated as a
general-purpose guarantee that arbitrary real-world speech is authentic.

Deployment should evaluate the exact microphone/channel/noise/compression
conditions and fit calibration on representative data before using probabilities
or hard thresholds in a safety policy.
