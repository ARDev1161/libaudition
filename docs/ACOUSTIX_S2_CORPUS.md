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

## Interval-aligned S2 renderer

Use the explicit `--speech-plan` mode, **not** legacy `--speech-dir`,
for evidence based on real speech. Its JSON schema is
`libaudition.acoustix-speech-plan.v1`:

```json
{
  "schema": "libaudition.acoustix-speech-plan.v1",
  "corpus_manifest_sha256": "<64-character actual SHA-256 of speech_corpus.json>",
  "split": "development",
  "segments": [
    {
      "source_id": "speaker_1",
      "interval_index": 0,
      "utterance_id": "<actual ID in that speaker's corpus manifest>"
    }
  ]
}
```

The example is **structural documentation only**; replace placeholder IDs
and hashes with verified real files and add exactly one entry for every
voice interval in the actual scene. All clips must belong to the one
selected split; clips cannot be silently reused within a scene. A
complete utterance must fit inside its assigned activity window. If
not, rendering **fails** rather than truncating speech. Do not loosen
the utterance/interval contract to mask lost first words.

The renderer command is:

```bash
python3 tests/integration/acoustix_scene.py \
  --scene /path/to/s2_scene.json \
  --output /tmp/libaudition-s2/render_01 \
  --seed 1161 --hop-ms 250 --stems \
  --speech-corpus-manifest /tmp/libaudition-s2/speech_corpus.json \
  --speech-corpus-root /tmp/libaudition-s2/selected-pcm \
  --speech-plan /path/to/s2_speech_plan.json
```

The result contains `input.wav` (dry summed reference + four wet mics
+ silent playback reference), `stems/*.wav` (per-source four-channel
reverberant signals), **`clean_refs/speaker_N.wav`** (sample-exact
single-speaker emission-aligned, unnormalized PCM16), plus
`speech_schedule.json` (source speaker IDs, original complete clip
lengths, exact sample offsets, supplied transcripts, hashes and split).
`ground_truth.json` references this speech schedule. The wet
microphone mix uses one shared global normalization gain; clean
references are unnormalized and must be adjusted consistently by any
future separation metric.

Regular CTest verifies exact first-sample placement and refusal to
truncate using fabricated **non-speech** PCM. An optional Acoustix CPU
test executes the genuine RIR renderer with those synthetic reference
files to verify the audio path only. Real human-voice fidelity, word
boundaries, source separation and WER remain unvalidated until actual
corpus acquisition and independent scoring.

## Build complete T02/T03/T07 scene/plan pairs automatically

After the real corpus has been imported and verified, build an immutable
S2 scenario **without hand-editing time boundaries**. This utility retains
the base 7×7 m positions and source identities but creates a **new** S2
scene: the chosen original human utterances are complete and each gets
one activity interval sized to its exact length plus 100 ms of
post-utterance silence. S1's original scene is never mutated.

```bash
python3 tests/integration/prepare_acoustix_s2_scene.py \
  --corpus-manifest /tmp/libaudition-s2/speech_corpus.json \
  --corpus-root /tmp/libaudition-s2/selected-pcm \
  --speakers 2 --split evaluation \
  --robot-motion moving --rt60-s 0.36 \
  --output /tmp/libaudition-s2/cases/t03_eval_moving

python3 tests/integration/acoustix_scene.py \
  --scene /tmp/libaudition-s2/cases/t03_eval_moving/scene.json \
  --speech-plan /tmp/libaudition-s2/cases/t03_eval_moving/speech_plan.json \
  --speech-corpus-manifest /tmp/libaudition-s2/speech_corpus.json \
  --speech-corpus-root /tmp/libaudition-s2/selected-pcm \
  --output /tmp/libaudition-s2/cases/t03_eval_moving/render \
  --seed 5161 --hop-ms 250 --stems
```

Choose `--speakers 1` (T02), `2` (initial T03) or `7` (T07
stress case), `--split development|evaluation`, and
`--robot-motion stationary|moving`. Use `--no-background` to
isolate voices, or keep fridge/AC/car to exercise competing background.
Targets `--rt60-s 0.2|0.36|0.7` are supported; use S1 calibrated
acoustic methods. The CLI rejects overwriting frozen output files.

This initial builder chooses one complete utterance per speaker and a
fixed first occurrence start; it does **not** yet explore T03 angle
separations 30°/60°/120°, T04 speaker+playback, quiet-gain variants,
multiple turns per speaker or true ASR. Those require dedicated reviewed
S2 scenarios. Test WAVs in the builder's CTest are **not human voices**;
passing these tests does not claim real S2 benchmark readiness.

## Remaining S2 acceptance work

The corpus manifest alone is not a benchmark. Still required:
1. **Implemented, CI pending:** sample-aligned, non-truncating input
   placement, explicit per-interval utterance ownership and clean dry
   target PCM export. Genuine licensed corpus evaluation is still required.
2. Archive a frozen S2 scene/corpus/plan and validate separated output
   against independent convolved per-source references.
3. Implement source-to-output association, real target/interferer
   alignment, SI-SDRi/SIRi leakage metrics (including silence safeguards),
   and ASR transcripts/WER with pinned model and control runs.
4. Run T02/T03/T04/T08 and seven-speaker T07; preserve 20 held-out
   scenes and do not tune algorithm thresholds on evaluation utterances.
5. Distinguish `voice / background SNR` (already in S1) from
   `target voice / other speakers SIR` (to be measured in S2).

The `--speech-dir` legacy option remains supported for older experiments
but is **not** acceptable evidence for S2 because it can clip speech
starts. Use only the new `--speech-plan` mode for interval-accurate
S2 results.
