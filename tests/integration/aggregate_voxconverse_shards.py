#!/usr/bin/env python3
"""Combine independently verified VoxConverse shards into a full-split DER.

Shard-level DER is NEVER an official baseline. Fail closed unless every
recording, every expected shard, the source manifest and input model SHA-256
agree. Re-score merged RTTMs, not the rounded individual shard reports.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

from benchmark_voxconverse import SPLIT_COUNTS, VOXCONVERSE_COMMIT
from score_diarization_corpus import evaluate

HASH_FIELDS = (
    "audio_archive_sha256",
    "reference_archive_sha256",
    "segmentation_model_sha256",
    "embedding_model_sha256",
    "full_reference_ids_sha256",
)


def sha256_ids(ids: set[str]) -> str:
    return hashlib.sha256(
        "".join(name + "\n" for name in sorted(ids)).encode("utf-8")
    ).hexdigest()


def aggregate(root: Path, output: Path, split: str,
              shards: int, collar: float, skip_overlap: bool) -> dict:
    if not root.is_dir() or not 1 <= shards <= SPLIT_COUNTS[split]:
        raise ValueError("Missing shard directory or invalid shard count")
    name_prefix = f"voxconverse-v0.3-{split}-shard-"
    shard_dirs = sorted(p for p in root.iterdir()
                        if p.is_dir() and p.name.startswith(name_prefix))
    if len(shard_dirs) != shards:
        raise ValueError(
            f"Expected {shards} completed {split} shard artifacts, "
            f"found {len(shard_dirs)}")
    if output.exists() and any(output.iterdir()):
        raise ValueError("Aggregate result directory must be empty")
    output.mkdir(parents=True, exist_ok=True)
    refs_out = output / "reference"
    hyps_out = output / "predicted"
    refs_out.mkdir()
    hyps_out.mkdir()
    observed_shards = set()
    all_stems: set[str] = set()
    shared = None
    per_shard = []
    for directory in shard_dirs:
        prov_path = directory / "provenance.json"
        progress_path = directory / "progress.json"
        if not prov_path.is_file() or not progress_path.is_file():
            raise ValueError(f"Missing shard provenance/progress: {directory}")
        prov = json.loads(prov_path.read_text(encoding="utf-8"))
        progress = json.loads(progress_path.read_text(encoding="utf-8"))
        index = prov["shard_index"]
        if (not isinstance(index, int) or index in observed_shards or
                not 0 <= index < shards or
                directory.name != f"{name_prefix}{index}-der"):
            raise ValueError(f"Missing, repeated or mislabeled shard: {directory}")
        observed_shards.add(index)
        if (prov["schema"] != "libaudition.voxconverse-source.v1" or
                prov["split"] != split or
                prov["reference_git_commit"] != VOXCONVERSE_COMMIT or
                prov["shard_count"] != shards or
                prov["expected_recordings"] != SPLIT_COUNTS[split] or
                prov["observed_reference_recordings"] != SPLIT_COUNTS[split] or
                prov["observed_audio_recordings"] != SPLIT_COUNTS[split] or
                prov["scoring"] != {"collar_seconds": collar,
                                    "skip_overlap": skip_overlap}):
            raise ValueError(f"Shard corpus or scoring policy mismatch: {directory}")
        if not prov.get("audio_archive_sha256_pinned"):
            raise ValueError(f"Unpinned source audio not accepted: {directory}")
        common = {key: prov[key] for key in HASH_FIELDS}
        if any(len(value) != 64 for value in common.values()):
            raise ValueError("Invalid SHA-256 metadata")
        if shared is None:
            shared = common
        elif common != shared:
            raise ValueError(f"Different audio/models/references across shards: {directory}")
        refs = sorted((directory / "reference").glob("*.rttm"))
        hyps = sorted((directory / "predicted").glob("*.rttm"))
        stems = {p.stem for p in refs}
        hyp_stems = {p.stem for p in hyps}
        if (not stems or stems != hyp_stems or
                progress.get("finished") != len(stems) or
                progress.get("total") != len(stems) or
                prov["shard_recordings"] != len(stems) or
                stems.intersection(all_stems)):
            raise ValueError(f"Incomplete/overlapping shard RTTMs: {directory}")
        # The partition itself is deterministic; verify each reference ID's
        # position relative to the canonical full-split IDs after collection.
        all_stems.update(stems)
        for path in refs:
            shutil.copyfile(path, refs_out / path.name)
        for path in hyps:
            shutil.copyfile(path, hyps_out / path.name)
        per_shard.append({"index": index, "recordings": len(stems)})
    if observed_shards != set(range(shards)):
        raise ValueError("Shard indices did not cover complete range")
    if len(all_stems) != SPLIT_COUNTS[split] or sha256_ids(all_stems) != shared["full_reference_ids_sha256"]:
        raise ValueError("Incomplete full-split reference ID coverage")
    ordered = sorted(all_stems)
    for index in range(shards):
        expected = {stem for i, stem in enumerate(ordered) if i % shards == index}
        directory = root / f"{name_prefix}{index}-der"
        actual = {p.stem for p in (directory / "reference").glob("*.rttm")}
        if actual != expected:
            raise ValueError(f"Incorrect deterministic speaker shard {index}")

    report = evaluate(refs_out, hyps_out, collar=collar,
                      skip_overlap=skip_overlap)
    if report["recordings"] != SPLIT_COUNTS[split]:
        raise ValueError("Never publish DER for incomplete split")
    report["voxconverse_version"] = "0.3"
    report["split"] = split
    report["source_sha256"] = shared
    report["shards"] = sorted(per_shard, key=lambda row: row["index"])
    (output / "der.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    print(json.dumps({
        "split": split, "recordings": report["recordings"],
        "corpus_der": report["corpus_der"], "totals": report["totals"],
    }, sort_keys=True))
    return report


def self_test() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        base = root / "shards"
        base.mkdir()
        ids = [f"clip{i:03d}" for i in range(SPLIT_COUNTS["dev"])]
        digests = {key: "f" * 64 for key in HASH_FIELDS}
        digests["full_reference_ids_sha256"] = sha256_ids(set(ids))
        for shard in range(4):
            folder = base / f"voxconverse-v0.3-dev-shard-{shard}-der"
            (folder / "reference").mkdir(parents=True)
            (folder / "predicted").mkdir()
            stems = [stem for i, stem in enumerate(ids) if i % 4 == shard]
            for stem in stems:
                for kind in ("reference", "predicted"):
                    (folder / kind / f"{stem}.rttm").write_text(
                        f"SPEAKER {stem} 1 0 1 <NA> <NA> "
                        f"{'ref' if kind == 'reference' else 'hyp'} <NA> <NA>\n",
                        encoding="utf-8")
            prov = dict(schema="libaudition.voxconverse-source.v1",
                        split="dev", reference_git_commit=VOXCONVERSE_COMMIT,
                        shard_count=4, shard_index=shard,
                        shard_recordings=len(stems),
                        expected_recordings=216,
                        observed_reference_recordings=216,
                        observed_audio_recordings=216,
                        audio_archive_sha256_pinned=True,
                        scoring={"collar_seconds": 0., "skip_overlap": False},
                        **digests)
            (folder / "provenance.json").write_text(json.dumps(prov), encoding="utf-8")
            (folder / "progress.json").write_text(
                json.dumps({"finished": len(stems), "total": len(stems)}),
                encoding="utf-8")
        result = aggregate(base, root / "merged", "dev", 4, 0., False)
        assert result["recordings"] == 216 and result["corpus_der"] == 0
        # Catch model-version mismatches rather than publishing mixed baselines.
        folder = base / "voxconverse-v0.3-dev-shard-2-der"
        data = json.loads((folder / "provenance.json").read_text())
        data["embedding_model_sha256"] = "0" * 64
        (folder / "provenance.json").write_text(json.dumps(data), encoding="utf-8")
        try:
            aggregate(base, root / "bad-merged", "dev", 4, 0., False)
        except ValueError as error:
            assert "Different audio/models" in str(error)
        else:
            raise AssertionError("Mixed-model corpus incorrectly accepted")
    print("voxconverse-complete-shard-aggregation-self-test=ok")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--split", choices=SPLIT_COUNTS)
    parser.add_argument("--shards", type=int, default=4)
    parser.add_argument("--collar", type=float, default=0.)
    parser.add_argument("--skip-overlap", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.input_dir is None or args.output_dir is None or args.split is None:
        parser.error("--input-dir --output-dir --split required")
    aggregate(args.input_dir, args.output_dir, args.split, args.shards,
              args.collar, args.skip_overlap)


if __name__ == "__main__":
    main()
