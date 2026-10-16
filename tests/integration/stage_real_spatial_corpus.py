#!/usr/bin/env python3
"""Stage a checksum-pinned, independently annotated real-array ZIP for CTest.

ZIP root: corpus.json and exactly its referenced PCM16/16-kHz WAVs.
Integrity is verifiable; whether labels are independent measurements must
still be established when curating the original dataset.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
from urllib.parse import urlsplit
from urllib.request import urlopen
import wave
import zipfile

from validate_respeaker_corpus import FS, HOP, validate_manifest

MAX_ZIP = 256 * 1024 * 1024
MAX_EXPANDED = 512 * 1024 * 1024
CHUNK = 1024 * 1024
SHA = re.compile(r"[0-9a-f]{64}\Z")
FILE = re.compile(r"[A-Za-z0-9_-]{1,64}\.wav\Z")


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def validate_contents(root):
    manifest = root / "corpus.json"
    records = validate_manifest(manifest)
    document = json.loads(manifest.read_text(encoding="utf-8"))
    require(isinstance(document.get("array"), dict),
            "Real spatial corpus requires explicit microphone geometry")
    geometry = document["array"]["microphones"]
    expected = {"corpus.json"}
    for rec in records:
        name = rec["wav"]
        require(bool(FILE.fullmatch(name)), "Unsafe or nested WAV filename")
        require(name not in expected, "Duplicate WAV reference")
        expected.add(name)
        audio = root / name
        require(audio.is_file(), "Referenced WAV missing: " + name)
        require(digest(audio) == rec["sha256"], "WAV SHA-256 mismatch: " + name)
        provenance = rec.get("provenance")
        require(isinstance(provenance, dict) and
                isinstance(provenance.get("dataset"), str) and
                bool(provenance["dataset"].strip()),
                "Missing real recording dataset provenance")
        with wave.open(str(audio)) as reader:
            frames = reader.getnframes()
            channels = reader.getnchannels()
            require(reader.getframerate() == FS and
                    reader.getsampwidth() == 2 and
                    reader.getcomptype() == "NONE" and
                    frames > 0 and frames % HOP == 0,
                    "Real WAV must be PCM16 16-kHz with complete ODAS hops")
            require(all(mic["channel"] < channels for mic in geometry),
                    "Microphone channel outside WAV")
            reader.setpos(frames - 1)
            require(len(reader.readframes(1)) == 2 * channels,
                    "Truncated real WAV payload")
        for interval in rec["intervals"]:
            require(interval["end_s"] <= frames / FS,
                    "Independent annotation exceeds WAV duration")
    require({p.name for p in root.iterdir()} == expected,
            "Unreferenced or missing corpus ZIP files")
    return len(records)


def stage_archive(archive, sha256, output, max_expanded=MAX_EXPANDED):
    require(bool(SHA.fullmatch(sha256)), "Expected lowercase SHA-256")
    require(not output.exists(), "Corpus destination already exists")
    require(archive.is_file() and 0 < archive.stat().st_size <= MAX_ZIP,
            "Missing, empty or oversized corpus ZIP")
    require(digest(archive) == sha256, "Corpus archive SHA-256 mismatch")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="staging-real-array-",
                                     dir=output.parent) as tmp:
        staged = Path(tmp) / "corpus"
        staged.mkdir()
        with zipfile.ZipFile(archive) as zip_file:
            infos = zip_file.infolist()
            require(1 < len(infos) <= 200, "Invalid corpus ZIP file count")
            names = set()
            expanded = 0
            for entry in infos:
                name = entry.filename
                require(name == "corpus.json" or bool(FILE.fullmatch(name)),
                        "Unsafe or unexpected ZIP entry: " + name)
                require(name.lower() not in names, "Duplicate ZIP path")
                names.add(name.lower())
                mode = (entry.external_attr >> 16) & 0o170000
                require(not entry.is_dir() and mode in (0, stat.S_IFREG),
                        "Archive contains a symlink or special file")
                expanded += entry.file_size
                require(expanded <= max_expanded,
                        "Corpus ZIP expanded size exceeds limit")
                with zip_file.open(entry) as source:
                    with (staged / name).open("xb") as dest:
                        copied = 0
                        while True:
                            block = source.read(CHUNK)
                            if not block:
                                break
                            copied += len(block)
                            require(copied <= entry.file_size,
                                    "ZIP entry exceeds declared size")
                            dest.write(block)
                        require(copied == entry.file_size, "Truncated ZIP entry")
        require("corpus.json" in names, "Corpus manifest missing")
        count = validate_contents(staged)
        os.replace(staged, output)
    return {"archive_sha256": sha256, "recordings": count,
            "manifest": str(output / "corpus.json")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument("--archive", type=Path)
    input_group.add_argument("--url")
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    require(bool(SHA.fullmatch(args.sha256)), "Expected pinned SHA-256")
    if args.url:
        uri = urlsplit(args.url)
        require(uri.scheme == "https" and uri.hostname and
                not uri.username and not uri.password,
                "Corpus URL must be HTTPS without embedded credentials")
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "corpus.zip"
            with urlopen(args.url, timeout=90) as response:
                require(urlsplit(response.geturl()).scheme == "https",
                        "Corpus redirected to non-HTTPS URL")
                count = 0
                with archive.open("xb") as dest:
                    while True:
                        chunk = response.read(CHUNK)
                        if not chunk:
                            break
                        count += len(chunk)
                        require(count <= MAX_ZIP,
                                "Downloaded corpus ZIP exceeds size limit")
                        dest.write(chunk)
            report = stage_archive(archive, args.sha256, args.output)
    else:
        report = stage_archive(args.archive, args.sha256, args.output)
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, KeyError, ValueError, TypeError, wave.Error,
            zipfile.BadZipFile) as exc:
        print("real-spatial-corpus=FAIL: " + str(exc), file=sys.stderr)
        raise SystemExit(2)
