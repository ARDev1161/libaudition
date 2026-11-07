# Acoustix S2 — acquiring and validating real human speech

**Status: corpus ingress contract implemented; REAL CORPUS NOT YET ACQUIRED IN CI.**
This is a separate test-only phase after S1; no ODAS DSP or ASR quality
claim follows from passing the metadata/WAV validator.

## Recommended source and license

Use the original **LibriSpeech dev-clean**, OpenSLR SLR12:
<https://www.openslr.org/12/>. It contains actual human English read
speech with speaker identifiers and provided orthographic transcripts.
It is licensed **Creative Commons Attribution 4.0 (CC BY 4.0)**:
<https://creativecommons.org/licenses/by/4.0/>.

When redistributing rendered audio or selected source files, preserve
LibriSpeech provenance, attribution and license notice. Do not infer or
publish people's legal identities from anonymized speaker IDs.
The corpus itself is much larger than the benchmark: dev-clean archive
is approximately 337 MB and is intentionally **not fetched in ordinary CI**.
Never commit the downloaded archive or original personal voice recordings.

The importer selects **seven distinct corpus speaker IDs**, each with
three complete utterances (one development, two evaluation), and
converts the original FLAC audio to 16 kHz mono PCM16 WAV. The evaluation
split here is *our held-out utterance selection from dev-clean*, not the
LibriSpeech official `test-clean` split and not a held-out-speaker claim.
The first eligible seven corpus speakers and earliest eligible
utterances are selected deterministically in sorted order. Review the
selected IDs, voice quality, genders/accent balance and leakage risks
before freezing a real benchmark. Do not choose speakers based on ODAS
output or ASR WER.

## Prepare locally

After obtaining the original archive from the official OpenSLR page:

```bash
mkdir -p /tmp/libaudition-s2
tar -xzf dev-clean.tar.gz -C /tmp/libaudition-s2

python3 tests/integration/prepare_librispeech_s2.py \
  --librispeech-dir /tmp/libaudition-s2/LibriSpeech/dev-clean \
  --output /tmp/libaudition-s2/selected-pcm \
  --manifest /tmp/libaudition-s2/speech_corpus.json

python3 tests/integration/acoustix_speech_corpus.py \
  --manifest /tmp/libaudition-s2/speech_corpus.json \
  --root /tmp/libaudition-s2/selected-pcm \
  --report /tmp/libaudition-s2/corpus_audit.json
```

The importer requires `ffmpeg` in `PATH`. Each complete WAV must be
between 0.6 and 5 seconds by default; it never truncates an utterance
to meet a destination interval. Existing output files cause a hard
error instead of being silently overwritten. Freeze selected WAV SHA-256
and transcripts before evaluation.

Corpus manifest schema `libaudition.acoustix-speech-corpus.v1`:
- `provenance`: corpus name, source HTTPS URL, license, license URL, language
- `speakers`: seven distinct `speaker_id` values mapped one-to-one to
  `speaker_1` through `speaker_7`
- each `utterances`: unique `utterance_id`, relative `path`, SHA-256
  of the **converted WAV**, `development` or `evaluation` split,
  exact supplied `transcript`

The validator fails on duplicate IDs or identical audio, unsafe paths,
invalid sample rates/channels, silent/clipped audio, absent transcripts,
invalid hashes, unlicensed input and inadequate development/held-out
splits. Its plain-stdlib unit test generates **non-speech synthetic WAVs**
solely to validate rejection paths; it cannot assert real human voice,
speaker identity, transcript accuracy or model performance.

## Remaining S2 acceptance work

The corpus manifest alone is not a benchmark. Still required:
1. Map each complete utterance to the **start of its declared speaking
   interval**, never truncate the first word by placing speech at t=0
   then applying an activity mask that begins later.
2. Archive original clean dry target PCM and independently derived
   convolved per-source reference alongside six-channel mixtures.
3. Implement source-to-output association, real target/interferer
   alignment, SI-SDRi/SIRi leakage metrics (including silence safeguards),
   and ASR transcripts/WER with pinned model and control runs.
4. Run T02/T03/T04/T08 and seven-speaker T07; preserve 20 held-out
   scenes and do not tune algorithm thresholds on evaluation utterances.
5. Distinguish `voice / background SNR` (already in S1) from
   `target voice / other speakers SIR` (to be measured in S2).

The `--speech-dir` legacy Acoustix renderer option must **not** be used
as proof of correctly aligned real speech: its one-WAV-per-source path
does not yet implement multiple interval-specific utterances.
