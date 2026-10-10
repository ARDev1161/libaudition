# EfficientAT audio tagging (experimental)

Enable with `-DLIBAUDITION_WITH_EFFICIENTAT=ON`. This builds
`audition::backend_efficientat` and installs the C++ header
`<audition/backends/efficientat/audio_tagger.hpp>`.

## C++ API

```cpp
#include <audition/backends/efficientat/audio_tagger.hpp>

audition::EfficientAtOnnxOptions options;
options.model = "/path/to/mn10_as.onnx";
options.labels = "/path/to/527-ordered-labels.txt";
options.top_k = 5;
audition::EfficientAtAudioTagger tagger{options};
audition::ClassificationResult result = tagger.classify(audio.view());
```

**The example is a contract, not a published working model package.**
Supply an exported ONNX artifact matching the exact IO below. No model
weights are bundled or downloaded for this backend.

- Input waveform: mono float32 at 32,000 Hz, provided through
  `IAudioClassifier::classify(AudioView)`. No implicit resampling.
- Preemphasis: `x[i + 1] - 0.97*x[i]`, reducing length by one.
- STFT: `n_fft=1024`, 800-sample symmetric Hann, hop 320,
  centered reflect padding, power spectrum.
- Mel: 128 Kaldi-style triangular banks, `fmin=0`, `fmax=15000`;
  last Nyquist column padded with zeros.
- Model input: contiguous float32 `[1,1,128,T]` normalized logmel,
  `(log(mel_power + 1e-5) + 4.5)/5`.
- Output: float32 `[1,527]` or `[527]` **logits**;
  sigmoid is applied in libaudition. Labels are ordered plain-text lines.
- `EfficientAtSpectrogramTagger::classifyLogMel()` accepts externally
  prepared, normalized log-mel directly if a reference frontend is preferred.

## Validation limits

The test suite generates a **synthetic** ONNX graph with fixed logits.
It verifies ONNX execution, sigmoid, Top-K, label mapping and rejects
non-finite / malformed inputs. A silence frontend regression test is included.

Qt acoustic scene can select `EfficientAT` when built with
`LIBAUDITION_WITH_EFFICIENTAT=ON`. The ODAS-separated 16 kHz source audio is
explicitly upsampled to 32 kHz by the backend's windowed-sinc 2x adapter;
this interpolation is not part of original EfficientAT evaluation and must
be benchmarked for classification quality. Select a matching ONNX and its
527 ordered labels manually; model files are not downloaded.

**Numerical parity against the original Python
`models/preprocess.py::AugmentMelSTFT` is not yet established.**
Do not publish accuracy or latency benchmark claims from this C++ path
until vectors computed by the original PyTorch + torchaudio Kaldi mel
implementation are checked at the tensor level. Do not assume arbitrary
public ONNX exports have the required tensor shapes and raw logits;
some may include their own frontend or output post-sigmoid scores.

Source of preprocessing contract:
https://github.com/fschmid56/EfficientAT/blob/main/models/preprocess.py

The model's original license and the export's license must be reviewed
independently before bundling artifacts.
