# VoxConverse v0.3: full speaker-diarization benchmark

This benchmark runs real libaudition Sherpa-ONNX Pyannote Segmentation 3.0
plus 3D-Speaker speaker embeddings on **all** original VoxConverse v0.3
recordings. Independently annotated RTTM files come from
[joonson/voxconverse](https://github.com/joonson/voxconverse).

The splits are **not mixed**: dev = **216 recordings**, test = **232
recordings** (448 total). Report dev and test results separately with exact
model and dataset hashes.

## Fixed inputs and provenance

| Component | Source / version |
| --- | --- |
| Reference RTTM | VoxConverse v0.3, Git commit `24bf60be297701cd7e4ef18550c6d390c1b87365` |
| Original dev WAVs | https://www.robots.ox.ac.uk/~vgg/data/voxconverse/data/voxconverse_dev_wav.zip |
| Original test WAVs | https://www.robots.ox.ac.uk/~vgg/data/voxconverse/data/voxconverse_test_wav.zip |
| Segmentation | Pyannote 3.0 ONNX archive, SHA-256 `24615ee884c897d9d2ba09bb4d30da6bb1b15e685065962db5b02e76e4996488` |
| Embedding | 3D-Speaker ONNX, SHA-256 `1a331345f04805badbb495c775a6ddffcdd1a732567d5ec8b3d5749e3c7a5e4b` |

VoxConverse WAV archives are hosted by Oxford and are not versioned in the
annotation Git repository. The workflow **computes and records SHA-256
of each WAV archive**. After the first validated complete run, these values
must be supplied as `--expected-audio-sha256` for future runs to reject
unannounced upstream changes. Until then, **the WAV ZIPs are not
cryptographically pinned**; results still record exact input digests.

Large audio archives, converted WAVs and ONNX weights are NOT stored in the
repository or uploaded as artifacts. VoxConverse is available for research
under CC BY 4.0, with attribution and copyrights retained by its owners.

## GitHub Actions benchmark

Workflow: [voxconverse-benchmark.yml](../.github/workflows/voxconverse-benchmark.yml).

Two independent jobs evaluate all 216 dev and all 232 test recordings. The
workflow is **separate from mandatory CI**. On this PR branch,
changing `tests/integration/benchmark_voxconverse.py` triggers the heavy
workflow. Manual dispatch is supported once this workflow is available on
the default branch. Unrelated PR documentation and code changes do **not**
rerun the multi-gigabyte evaluation. Models are SHA-256 pinned.

Each job records source hashes; copies independently annotated reference
RTTMs unchanged; converts each original WAV to mono 16 kHz in isolation; runs
the C++ diarizer in `--benchmark` mode; and collects a predicted RTTM.
Results cannot be called full-split unless *every* reference and WAV has
a matching recording ID and an inference result.

Per-split artifacts:
- `der.json` — aggregate and per-recording DER, error categories, scoring rules;
- `provenance.json` — dataset commit, archive hashes, model hashes, processing;
- `progress.json` — exact completion count and per-recording processing time;
- `reference/*.rttm` and `predicted/*.rttm` — human labels and model predictions.

No source WAV audio or model weights are uploaded.

## Run locally

Prerequisites: Python 3.10+, FFmpeg, C++17 compiler, CMake/Ninja, downloaded
Oxford WAV archive (dev or test), pinned annotation archive and Sherpa models.

```sh
cmake -S . -B build-voxconverse -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_WITH_SHERPA=ON \
  -DLIBAUDITION_BUILD_TESTS=ON \
  -DLIBAUDITION_WITH_SPDLOG=OFF \
  -DLIBAUDITION_BUILD_QT_DEMO=OFF \
  -DLIBAUDITION_BUILD_EXAMPLES=OFF
cmake --build build-voxconverse --target sherpa_real_speaker_diarization --parallel 2
```

RTTM annotation archive at exact commit:

```text
https://codeload.github.com/joonson/voxconverse/zip/24bf60be297701cd7e4ef18550c6d390c1b87365
```

Get models from the official Sherpa releases and verify their hashes against
the table above. Then run:

```sh
python3 tests/integration/benchmark_voxconverse.py \
  --split dev \
  --audio-zip /data/voxconverse_dev_wav.zip \
  --reference-zip /data/voxconverse-v0.3.zip \
  --diarizer build-voxconverse/tests/sherpa_real_speaker_diarization \
  --segmentation-model /models/pyannote/model.onnx \
  --embedding-model /models/3dspeaker.onnx \
  --output-dir /data/libaudition-voxconverse-dev
```

Use `--split test` and the test WAV archive for all 232 test recordings.
Pass `--expected-audio-sha256` to cryptographically pin a validated ZIP.

Offline fixture preflight (no network, models or WAV):
```sh
python3 tests/integration/benchmark_voxconverse.py --self-test
```

## Metric definition

The primary corpus metric is total error seconds divided by total reference
speaker seconds (micro-average), **not** the mean per-recording DER.
Error seconds = missed speaker seconds + false-alarm speaker seconds
+ speaker confusion seconds. Speaker IDs are globally optimally mapped
within each recording; overlaps are included by default. The reference
collar is `0.0 s` by default, and there is no arbitrary DER gate.
Alternative `--collar` or `--skip-overlap` policies require explicit
documentation for meaningful comparisons.

The 5.66% DER on the short independent diagnostic in CI #1060 is **not**
a VoxConverse result. Only add real VoxConverse DER numbers to this document
after all 216 or 232 recordings have been evaluated.

### Citation

Chung, J. S., Huh, J., Nagrani, A., Afouras, T., & Zisserman, A. (2020).
*Spot the Conversation: Speaker Diarisation in the Wild*. Interspeech.
https://github.com/joonson/voxconverse
