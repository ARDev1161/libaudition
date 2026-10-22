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
| Original dev WAVs | Oxford ZIP SHA-256 `e83a68b5df3bc945a3cf4544102038792ae79972753c585769e58ea677c523a8` |
| Original test WAVs | Oxford ZIP SHA-256 `472ebf1eaeb1dcb5c311b07a8b5c31bcedcccbf98f386d90a88cde2452da8c68` |
| RTTM annotation ZIP | GitHub codeload SHA-256 `c340d2cd9da2f19eff8a3adc55b0818ec45cd0770ef72c6926c4de6efe9e9a1d` |
| Segmentation | Pyannote 3.0 ONNX archive, SHA-256 `24615ee884c897d9d2ba09bb4d30da6bb1b15e685065962db5b02e76e4996488` |
| Embedding | 3D-Speaker ONNX, SHA-256 `1a331345f04805badbb495c775a6ddffcdd1a732567d5ec8b3d5749e3c7a5e4b` |

VoxConverse WAV archives are hosted by Oxford separately from the GitHub
annotation repository. SHA-256 hashes of both Oxford ZIPs and the exact GitHub
annotation ZIP were captured during the initial GitHub Actions runs and are
**now enforced before each benchmark run**. Any upstream byte change fails
closed rather than silently modifying the baseline. The original URLs are
in `tests/integration/benchmark_voxconverse.py` and the workflow.

Large audio archives, converted WAVs and ONNX weights are NOT stored in the
repository or uploaded as artifacts. VoxConverse is available for research
under CC BY 4.0, with attribution and copyrights retained by its owners.

## GitHub Actions benchmark

Workflow: [voxconverse-benchmark.yml](../.github/workflows/voxconverse-benchmark.yml).

The workflow runs **eight model-backed shard jobs**, covering dev and test
with four non-overlapping deterministic shards per split (54 dev clips per
shard; 58 test clips per shard). Each shard verifies the **entire** archive
before selecting clips, and records source/model hashes plus all predictions.

Two separate aggregation jobs download shard results, verify that every
recording is present exactly once, check full source hashes and deterministic
shard assignments, and **re-score all original RTTMs** into one complete
split DER. A shard-level diagnostic never counts as a final benchmark. If
any shard fails, the corresponding full-split DER is **not published**.

This heavy workflow is separate from mandatory CI. On the PR branch, changes
to `tests/integration/benchmark_voxconverse.py` trigger a run. Manual
dispatch is available after the workflow reaches the default branch.
Unrelated PR changes do not cause new full-corpus runs.

**Per-shard artifacts:** `shard-der.json` (diagnostic only),
`provenance.json`, `progress.json`, `reference/*.rttm`,
`predicted/*.rttm`. **Final per-split artifact:**
`voxconverse-v0.3-<split>-VERIFIED-DER`, with `der.json` and all matching
reference/predicted RTTMs.

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

Offline fixture preflight and fail-closed shard-aggregation tests:
```sh
python3 tests/integration/benchmark_voxconverse.py --self-test
python3 tests/integration/aggregate_voxconverse_shards.py --self-test
```

Optional local sharding: add `--shard-count 4 --shard-index 0` through
`--shard-index 3` in independent output directories. These runs write
`shard-der.json`, **not** a valid full-split `der.json`; use
`aggregate_voxconverse_shards.py` to combine and validate them.

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
