#!/usr/bin/env python3
"""Reproducible full-split VoxConverse v0.3 evaluation using real libaudition ONNX.

Inputs: official Oxford WAV ZIP, GitHub annotation ZIP pinned to an exact commit,
and real Sherpa Pyannote/3D-Speaker models. No artificial speaker-count gate,
no inferred labels used as reference, no unexplained excluded recordings.
Full dev/test results are reported separately with exact source hashes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile
import time
from zipfile import ZipFile

from score_diarization_corpus import evaluate
from score_diarization_rttm import parse_rttm


VOXCONVERSE_COMMIT = "24bf60be297701cd7e4ef18550c6d390c1b87365"
SPLIT_COUNTS = {"dev": 216, "test": 232}
SPLIT_AUDIO_URLS = {
    "dev": "https://www.robots.ox.ac.uk/~vgg/data/voxconverse/data/voxconverse_dev_wav.zip",
    "test": "https://www.robots.ox.ac.uk/~vgg/data/voxconverse/data/voxconverse_test_wav.zip",
}


def select_shard(sorted_ids: list[str], index: int, count: int) -> list[str]:
    """Stable disjoint partition; never dependent on source ZIP entry order."""
    if count < 1 or not 0 <= index < count:
        raise ValueError(f"Invalid shard {index}/{count}")
    return [stem for position, stem in enumerate(sorted_ids)
            if position % count == index]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            data = source.read(4 * 1024 * 1024)
            if not data:
                break
            digest.update(data)
    return digest.hexdigest()


def reference_members(zipfile: ZipFile, split: str, commit: str) -> dict[str, str]:
    """Demand the exact pinned archive root and complete reference split."""
    files = {}
    for member in zipfile.infolist():
        if member.is_dir():
            continue
        path = PurePosixPath(member.filename)
        if (len(path.parts) != 3 or
                path.parts[0] != "voxconverse-" + commit or
                path.parts[1] != split or
                path.suffix.lower() != ".rttm"):
            continue
        stem = path.stem
        if stem in files:
            raise ValueError(f"Duplicate {split} RTTM: {stem}")
        files[stem] = member.filename
    if len(files) != SPLIT_COUNTS[split]:
        raise ValueError(
            f"Expected full VoxConverse v0.3 {split} RTTM set of "
            f"{SPLIT_COUNTS[split]}, got {len(files)}")
    return files


def audio_members(zipfile: ZipFile, required: set[str]) -> dict[str, str]:
    """Accept audio nested under upstream ZIP roots, reject missing/duplicates.

    Do not trust extracted paths or follow ZIP file names as destinations.
    Ignore __MACOSX metadata, fail on any unrecognized real WAV.
    """
    files = {}
    for member in zipfile.infolist():
        if member.is_dir():
            continue
        path = PurePosixPath(member.filename)
        if "__MACOSX" in path.parts or path.name.startswith("._"):
            continue
        if path.suffix.lower() != ".wav":
            continue
        stem = path.stem
        if stem in files:
            raise ValueError(f"Duplicate audio basename: {stem}")
        files[stem] = member.filename
    missing = sorted(required - files.keys())
    extra = sorted(files.keys() - required)
    if missing or extra:
        raise ValueError(
            f"Official VoxConverse audio/RTTM mismatch: "
            f"missing WAVs={missing[:12]} (n={len(missing)}), "
            f"extra WAVs={extra[:12]} (n={len(extra)})")
    return files


def validate_input_paths(args: argparse.Namespace) -> None:
    for name in ("audio_zip", "reference_zip", "diarizer",
                 "segmentation_model", "embedding_model"):
        path = getattr(args, name)
        if not path.is_file() or path.stat().st_size <= 0:
            raise ValueError(f"Missing or empty {name}: {path}")
    if not args.diarizer.stat().st_mode & 0o111:
        raise ValueError(f"Diarizer is not executable: {args.diarizer}")
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        raise ValueError(
            f"Results directory must be empty to avoid stale predictions: "
            f"{args.output_dir}")
    if args.audio_zip.resolve() == args.reference_zip.resolve():
        raise ValueError("Audio ZIP and RTTM ZIP cannot be the same file")


def run_benchmark(args: argparse.Namespace) -> None:
    validate_input_paths(args)
    hashes = {
        "audio_archive_sha256": sha256_file(args.audio_zip),
        "reference_archive_sha256": sha256_file(args.reference_zip),
        "segmentation_model_sha256": sha256_file(args.segmentation_model),
        "embedding_model_sha256": sha256_file(args.embedding_model),
    }
    if (args.expected_audio_sha256 is not None and
            hashes["audio_archive_sha256"] != args.expected_audio_sha256):
        raise ValueError(
            "Official VoxConverse WAV archive changed from pinned SHA-256 "
            f"{args.expected_audio_sha256}: {hashes['audio_archive_sha256']}")
    if args.expected_reference_sha256 is not None and (
            hashes["reference_archive_sha256"] != args.expected_reference_sha256):
        raise ValueError("Pinned VoxConverse annotation archive SHA-256 changed")
    print(f"VoxConverse split={args.split}, expected_count={SPLIT_COUNTS[args.split]}", flush=True)
    for key, value in hashes.items():
        print(f"{key}={value}", flush=True)

    # The official RTTMs must be parsed before we spend CPU time on inference.
    with ZipFile(args.reference_zip) as zref:
        reference_files = reference_members(zref, args.split, VOXCONVERSE_COMMIT)
        with tempfile.TemporaryDirectory(prefix="voxconverse-preflight-") as temp:
            root = Path(temp)
            for stem, member in sorted(reference_files.items()):
                path = root / f"{stem}.rttm"
                with zref.open(member) as src, path.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
                recording, turns = parse_rttm(path)
                if recording != stem or not turns:
                    raise ValueError(f"Invalid reference annotation ID/turns for {stem}")

    with ZipFile(args.audio_zip) as zaudio:
        # Validate the ENTIRE official split before selecting deterministic
        # shards; no clip may disappear from the source data silently.
        wavs_all = audio_members(zaudio, set(reference_files))
        ordered = sorted(reference_files)
        chosen = select_shard(ordered, args.shard_index, args.shard_count)
        if not chosen:
            raise ValueError("Empty shard is not allowed")
        wavs = {stem: wavs_all[stem] for stem in chosen}
        args.output_dir.mkdir(parents=True, exist_ok=True)
        ref_dir = args.output_dir / "reference"
        pred_dir = args.output_dir / "predicted"
        ref_dir.mkdir()
        pred_dir.mkdir()

        with ZipFile(args.reference_zip) as zref:
            for stem in chosen:
                member = reference_files[stem]
                target = ref_dir / f"{stem}.rttm"
                with zref.open(member) as src, target.open("wb") as dst:
                    shutil.copyfileobj(src, dst)

        source_sha = {
            "schema": "libaudition.voxconverse-source.v1",
            "voxconverse_version": "0.3",
            "reference_git_commit": VOXCONVERSE_COMMIT,
            "audio_source_url": SPLIT_AUDIO_URLS[args.split],
            "split": args.split,
            "expected_recordings": SPLIT_COUNTS[args.split],
            "observed_audio_recordings": len(wavs_all),
            "observed_reference_recordings": len(reference_files),
            "shard_index": args.shard_index,
            "shard_count": args.shard_count,
            "shard_recordings": len(chosen),
            "full_reference_ids_sha256": hashlib.sha256(
                "".join(stem + "\n" for stem in ordered).encode("utf-8")
            ).hexdigest(),
            "resampling": "ffmpeg -ac 1 -ar 16000 -c:a pcm_s16le",
            "inference_backend": "sherpa-onnx Pyannote 3.0 + 3D-Speaker",
            "cluster_estimation": "automatic (num_clusters=0)",
            "scoring": {"collar_seconds": args.collar,
                        "skip_overlap": args.skip_overlap},
            "audio_archive_sha256_pinned": args.expected_audio_sha256 is not None,
            **hashes,
        }
        (args.output_dir / "provenance.json").write_text(
            json.dumps(source_sha, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")

        durations = {}
        with tempfile.TemporaryDirectory(prefix="voxconverse-wav-") as t:
            scratch = Path(t)
            source_path = scratch / "upstream.wav"
            for index, stem in enumerate(sorted(wavs), 1):
                # Archive entries are copied to fixed basenames, never extracted
                # to attacker-controlled paths. One WAV pair exists at a time.
                t0 = time.monotonic()
                with zaudio.open(wavs[stem]) as src, source_path.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
                wav = scratch / f"{stem}.wav"
                try:
                    subprocess.run(
                        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
                         "-y", "-i", str(source_path),
                         "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le",
                         str(wav)],
                        check=True, timeout=args.clip_timeout_seconds)
                    subprocess.run(
                        [str(args.diarizer.resolve()),
                         str(args.segmentation_model.resolve()),
                         str(args.embedding_model.resolve()),
                         str(wav.resolve()),
                         str((pred_dir / f"{stem}.txt").resolve()),
                         "--benchmark"],
                        check=True, timeout=args.clip_timeout_seconds)
                finally:
                    source_path.unlink(missing_ok=True)
                    wav.unlink(missing_ok=True)
                predicted = pred_dir / f"{stem}.rttm"
                if not predicted.exists():
                    raise RuntimeError(
                        f"Diarizer did not write predicted RTTM for {stem}")
                elapsed = time.monotonic() - t0
                durations[stem] = round(elapsed, 3)
                print(f"VoxConverse {args.split} shard "
                      f"{args.shard_index}/{args.shard_count}: {index}/{len(wavs)} "
                      f"{stem} elapsed_s={elapsed:.2f}", flush=True)
                # Lightweight progress checkpoint survives a failed runner.
                (args.output_dir / "progress.json").write_text(
                    json.dumps({"finished": index, "total": len(wavs),
                                "last_recording": stem,
                                "elapsed_s_by_recording": durations},
                               indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")

    result = evaluate(ref_dir, pred_dir,
                      collar=args.collar, skip_overlap=args.skip_overlap)
    if result["recordings"] != len(chosen):
        raise AssertionError("Incomplete shard results cannot be reported")
    result["voxconverse_version"] = "0.3"
    result["split"] = args.split
    result["source"] = source_sha
    result["inference_seconds_per_recording"] = durations
    # Only shard-count=1 may write a headline full-split DER. A shard DER
    # MUST NOT be mistaken for a verified complete 216/232 recording score.
    destination = "der.json" if args.shard_count == 1 else "shard-der.json"
    result["shard_index"] = args.shard_index
    result["shard_count"] = args.shard_count
    (args.output_dir / destination).write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    print(json.dumps({
        "split": args.split, "recordings": result["recordings"],
        "corpus_der": result["corpus_der"],
        "totals": result["totals"],
    }, sort_keys=True), flush=True)


def self_test() -> None:
    """No network/model/assets: audit archive pairing, missing files, pinned roots."""
    from zipfile import ZipFile
    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp)
        ref = folder / "ref.zip"
        audio = folder / "audio.zip"
        with ZipFile(ref, "w") as z:
            for i in range(SPLIT_COUNTS["dev"]):
                name = f"clip{i:03d}"
                z.writestr(
                    f"voxconverse-{VOXCONVERSE_COMMIT}/dev/{name}.rttm",
                    f"SPEAKER {name} 1 0.0 1.0 <NA> <NA> spk0 <NA> <NA>\n")
            z.writestr(
                f"voxconverse-{VOXCONVERSE_COMMIT}/test/ignore.rttm", "ignored")
        with ZipFile(audio, "w") as z:
            for i in range(SPLIT_COUNTS["dev"]):
                z.writestr(f"nested/audio/clip{i:03d}.wav", b"test")
        with ZipFile(ref) as z:
            reference = reference_members(z, "dev", VOXCONVERSE_COMMIT)
            assert len(reference) == 216
            try:
                reference_members(z, "dev", "bad" * 13 + "b")
            except ValueError:
                pass
            else:
                raise AssertionError("Mismatched annotation commit accepted")
        with ZipFile(audio) as z:
            paired = audio_members(z, set(reference))
            assert len(paired) == 216
            try:
                audio_members(z, set(reference) | {"not_exists"})
            except ValueError as e:
                assert "missing WAVs" in str(e)
            else:
                raise AssertionError("Missing recording ignored")
        with ZipFile(audio, "a") as z:
            z.writestr("audio/additional.wav", b"test")
        with ZipFile(audio) as z:
            try:
                audio_members(z, set(reference))
            except ValueError as e:
                assert "extra WAVs" in str(e)
            else:
                raise AssertionError("Unmatched audio silently accepted")
        sorted_ids = sorted(reference)
        shards = [select_shard(sorted_ids, shard, 4) for shard in range(4)]
        assert [len(shard) for shard in shards] == [54, 54, 54, 54]
        assert set().union(*(set(shard) for shard in shards)) == set(reference)
        assert all(set(shards[i]).isdisjoint(shards[j])
                   for i in range(4) for j in range(i + 1, 4)
        for invalid_index, invalid_count in [(-1, 4), (4, 4), (0, 0)]:
            try:
                select_shard(sorted_ids, invalid_index, invalid_count)
            except ValueError:
                pass
            else:
                raise AssertionError("Invalid shard index/count accepted")
    print("voxconverse-full-split-preflight-self-test=ok")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--split", choices=SPLIT_COUNTS)
    p.add_argument("--audio-zip", type=Path)
    p.add_argument("--reference-zip", type=Path)
    p.add_argument("--diarizer", type=Path)
    p.add_argument("--segmentation-model", type=Path)
    p.add_argument("--embedding-model", type=Path)
    p.add_argument("--output-dir", type=Path)
    p.add_argument("--shard-index", type=int, default=0)
    p.add_argument("--shard-count", type=int, default=1)
    p.add_argument("--expected-audio-sha256")
    p.add_argument("--expected-reference-sha256")
    p.add_argument("--clip-timeout-seconds", type=int, default=1500)
    p.add_argument("--collar", type=float, default=0.0)
    p.add_argument("--skip-overlap", action="store_true")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    for name in ("split", "audio_zip", "reference_zip", "diarizer",
                 "segmentation_model", "embedding_model", "output_dir"):
        if getattr(args, name) is None:
            p.error(f"--{name.replace('_', '-')} is required")
    if not (1 <= args.shard_count <= SPLIT_COUNTS[args.split]):
        p.error("--shard-count must be in [1, number of split recordings]")
    if not (0 <= args.shard_index < args.shard_count):
        p.error("--shard-index must be in [0, shard-count)")
    if not (args.clip_timeout_seconds > 0):
        p.error("--clip-timeout-seconds must be positive")
    if not (0 <= args.collar < float("inf")):
        p.error("--collar must be finite and nonnegative")
    for name in ("expected_audio_sha256", "expected_reference_sha256"):
        value = getattr(args, name)
        if value is not None and (
                len(value) != 64 or any(c not in "0123456789abcdef" for c in value)):
            p.error(f"--{name.replace('_', '-')} must be a lowercase SHA-256")
    run_benchmark(args)


if __name__ == "__main__":
    main()
