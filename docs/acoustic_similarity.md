# AcousticSimilarityAnalyzer: pre-tracking acoustic evidence (experimental)

The dependency-free C++17 DSP API
`<audition/dsp/acoustic_similarity.hpp>` compares **two existing mono audio
streams**. It is designed for the proposed SSL-candidate -> provisional
separation -> acoustic association -> SST pipeline. It neither generates
separated channels nor changes ODAS' current SSL/SST/SSS pipeline.

## Contract

`audition::dsp::AcousticSimilarityAnalyzer::compare(first, second)` returns:

- `waveform_correlation`: *signed* normalized waveform Pearson correlation at
  the best absolute-correlation lag, range [-1,1]. Strong magnitude suggests
  **shared signal content**, which can also be caused by leakage between two
  beams. It does **not** certify a shared physical source.
- `delay_samples`: lag maximizing absolute correlation, such that
  `second[t + delay_samples]` aligns to `first[t]`. It does not estimate
  physical propagation distance.
- `spectral_change_similarity`: cosine similarity of changes in logarithmic
  short-time power across nine fixed FFT bins (implemented using Hann-windowed
  Goertzel), also [-1,1]. Comparing **changes**, rather than average FFT
  pictures, reduces false similarity from two voices with similar timbre.
- `compared_samples` and `spectral_frames`: exact evidence window sizes.

Inputs must be finite float32 PCM, mono, same sample rate and frame count.
**The caller must ensure the two windows represent the same capture interval;**
the analyzer does not perform clock/sequence synchronization. The first
`max_analysis_samples` are examined (8192 samples by default); no history is
retained between calls and no threads or model weights are used. Only the
source-window's first segment is compared: a calling pipeline must deliberately
choose a speech-active segment, rather than passing many seconds of initial
silence. The default delay search is +/-128 samples (8 ms at 16 kHz).

Empty, too-short (<512 frames), silent, or constant windows return absent
evidence. In particular, a silent second channel is **not** evidence of two
independent acoustic sources. Non-finite PCM or format mismatch is an error.
Raw scores are deliberately **not** mapped to probabilities or an automatic
same/different decision. No calibrated similarity thresholds exist yet.

The selected frequency bins refer to fractions of the sample rate, not fixed
Hertz; use the same rate for both windows. The resulting feature vector
expresses coarse spectral changes, not a neural embedding, a speaker
fingerprint, a full-resolution FFT image, or source separation quality.

## Intended composition

```text
four synchronized microphone signals
  -> SSL potentials (direction + score)
  -> [future candidate separator: one mono stream per direction]
  -> AcousticSimilarityAnalyzer(pair of candidate mono streams)
  -> [future acoustic association: independent / shared / insufficient]
  -> SST (spatial source motion and persistent track lifecycle)
  -> final SSS, VAD, ASR
```

Provisional directions may include multipath reflections, ghosts and
independently speaking people at nearby angles. **Never merge based on
spectral resemblance alone**, and do not treat strong cross-correlation
as reliable source identity when a separator leaks the same dominant voice into
both outputs. A different voice can also leak into both outputs, or become
decorrelated by aggressive separation. A source-association decision needs
spatial consistency, calibrated leakage tests and temporal persistence.

## Reproducible verification and limits

```bash
cmake --preset ci
cmake --build --preset ci --target libaudition_tests
ctest --test-dir build/ci --no-tests=error \
  -R '^AcousticSimilarity[.]' --output-on-failure
```

The deterministic GTests cover a gain-scaled delayed copy, opposite polarity,
independent synthetic signals, common gain envelopes, silence, constant
waveforms, invalid arguments, and bounded processing. These are **algorithm
contract tests**, not speaker separation acceptance.

For real quality, run the frozen **Acoustix** T03 human-speech WAV and oracle
through the *unchanged* upstream ODAS and current `odas/dev` baseline, then
through a separate experimental candidate separator; only then compare
candidate audio and evaluate:

- duplicate/independent precision-recall on oracle-associated sources;
- stereo/beam leakage and SI-SDRi/SIRi for each clean reference stem;
- T02 false-source rate, T03 two-speaker IDF1 and switches, T04
  near-user-vs-loudspeaker overlap, T07 seven-voice stress;
- delay and CPU cost on representative speech-active windows.

**All those real candidate-separation quality measures are currently
UNMEASURED**. This analyzer alone cannot fix ODAS' second-track dropout.
