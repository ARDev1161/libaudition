#!/usr/bin/env python3
"""Import independently measured ReSpeaker USB 4-Mic recordings into a corpus.

This tool never invents source positions. Supply a JSON annotation for each WAV.
Only exact six-channel PCM16/16kHz is accepted; no implicit channel conversion.
"""
import argparse
import hashlib
import json
import shutil
import sys
import wave
from pathlib import Path

FS = 16000
HOP = 128


def import_recording(wav, annotation, destination):
    metadata = json.loads(annotation.read_text(encoding="utf-8"))
    name = metadata.get("id")
    if not isinstance(name, str) or not name or not all(
            c.isalnum() or c in "-_" for c in name):
        raise ValueError("id must contain ASCII letters, digits, '_' or '-'")
    if not name.isascii() or len(name) > 64:
        raise ValueError("id must be ASCII and at most 64 characters")
    if not isinstance(metadata.get("intervals"), list) or not metadata["intervals"]:
        raise ValueError("A measured, nonempty intervals array is required")
    with wave.open(str(wav), "rb") as source:
        frames = source.getnframes()
        if (source.getnchannels(), source.getframerate(),
                source.getsampwidth(), source.getcomptype()) != (
                6, FS, 2, "NONE"):
            raise ValueError("Expected original six-channel PCM16 16kHz WAV")
        if not frames or frames % HOP:
            raise ValueError("WAV frames must be positive and divisible by 128")
        duration = frames / FS
    for item in metadata["intervals"]:
        if not isinstance(item, dict):
            raise ValueError("Each interval must be a JSON object")
        if item.get("end_s", duration + 1) > duration:
            raise ValueError("Annotation extends past captured WAV")
    destination.mkdir(parents=True, exist_ok=True)
    manifest_path = destination / "corpus.json"
    if manifest_path.exists():
        corpus = json.loads(manifest_path.read_text(encoding="utf-8"))
        if corpus.get("schema") != "libaudition.respeaker-corpus.v1":
            raise ValueError("Existing manifest schema mismatch")
    else:
        corpus = {"schema": "libaudition.respeaker-corpus.v1", "recordings": []}
    if any(x.get("id") == name for x in corpus["recordings"]):
        raise ValueError("Recording ID already exists; never silently overwrite")
    output = destination / (name + ".wav")
    if output.exists():
        raise ValueError("Output WAV exists; refusing overwrite")
    record = {"id": name, "wav": output.name,
              "sha256": hashlib.sha256(wav.read_bytes()).hexdigest(),
              "intervals": metadata["intervals"]}
    # Validate the complete prospective corpus before writing.
    prospective = {"schema": corpus["schema"],
                   "recordings": corpus["recordings"] + [record]}
    from validate_respeaker_corpus import validate_manifest
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        draft = Path(tmp) / "corpus.json"
        draft.write_text(json.dumps(prospective), encoding="utf-8")
        validate_manifest(draft)
    shutil.copyfile(wav, output)
    try:
        manifest_path.write_text(
            json.dumps(prospective, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    except Exception:
        output.unlink(missing_ok=True)
        raise
    print("respeaker-import=ok", name, "frames=", frames,
          "sha256=", record["sha256"])


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--wav", type=Path, required=True)
    p.add_argument("--annotation", type=Path, required=True)
    p.add_argument("--corpus", type=Path, required=True)
    args = p.parse_args()
    import_recording(args.wav, args.annotation, args.corpus)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print("respeaker-import=fail:", exc, file=sys.stderr)
        sys.exit(1)
