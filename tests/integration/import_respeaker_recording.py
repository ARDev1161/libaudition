#!/usr/bin/env python3
"""Import independently measured microphone-array captures into a corpus.

Legacy mode accepts original six-channel ReSpeaker files. With explicit array
geometry, arbitrary channel layouts are supported (still PCM16/16kHz).
No synthetic positions, resampling, or implicit channel conversion.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import wave
from pathlib import Path

FS = 16000
HOP = 128


def import_recording(wav, annotation, destination, array=None):
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
        channels = source.getnchannels()
        if (source.getframerate(), source.getsampwidth(),
                source.getcomptype()) != (FS, 2, "NONE"):
            raise ValueError("Expected unconverted PCM16 16kHz WAV")
        if array is None and channels != 6:
            raise ValueError("Six channels required without explicit geometry")
        if array is not None:
            if not isinstance(array, dict):
                raise ValueError("Geometry must be an object")
            microphones = array.get("microphones")
            if not isinstance(microphones, list) or len(microphones) < 2:
                raise ValueError("Explicit geometry must contain >=2 microphones")
            for mic in microphones:
                if not isinstance(mic, dict) or type(mic.get("channel")) is not int:
                    raise ValueError("Invalid microphone channel")
                if not 0 <= mic["channel"] < channels:
                    raise ValueError("Microphone channel is outside WAV")
        if not frames or frames % HOP:
            raise ValueError("WAV frames must be positive and divisible by 128")
        source.setpos(frames - 1)
        if len(source.readframes(1)) != 2 * channels:
            raise ValueError("Truncated WAV payload")
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
        if corpus.get("array") != array:
            raise ValueError("Cannot mix microphone array geometries in one corpus")
    else:
        corpus = {"schema": "libaudition.respeaker-corpus.v1", "recordings": []}
        if array is not None:
            corpus["array"] = array
    if any(x.get("id") == name for x in corpus["recordings"]):
        raise ValueError("Recording ID already exists; never silently overwrite")
    output = destination / (name + ".wav")
    if output.exists():
        raise ValueError("Output WAV exists; refusing overwrite")
    # Hash in bounded memory; a multichannel real recording can be gigabytes.
    sha = hashlib.sha256()
    with wav.open("rb") as input_stream:
        for block in iter(lambda: input_stream.read(1024 * 1024), b""):
            sha.update(block)
    record = {"id": name, "wav": output.name,
              "sha256": sha.hexdigest(),
              "intervals": metadata["intervals"]}
    # Preserve array geometry when appending to an existing corpus.
    prospective = {**corpus,
                   "recordings": corpus["recordings"] + [record]}
    from validate_respeaker_corpus import validate_manifest
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        draft = Path(tmp) / "corpus.json"
        draft.write_text(json.dumps(prospective), encoding="utf-8")
        validate_manifest(draft)
    # Stage both files in the destination filesystem. An interrupted import
    # must never leave a partially copied WAV or a partial JSON manifest.
    staged_wav = None
    staged_manifest = None
    published_wav = False
    try:
        with tempfile.NamedTemporaryFile(
                dir=destination, prefix=".wav-import-", delete=False) as tmp:
            staged_wav = Path(tmp.name)
        shutil.copyfile(wav, staged_wav)
        copied = hashlib.sha256()
        with staged_wav.open("rb") as copied_stream:
            for block in iter(lambda: copied_stream.read(1024 * 1024), b""):
                copied.update(block)
        if copied.hexdigest() != record["sha256"]:
            raise ValueError("WAV changed or copy corrupted during import")
        with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", dir=destination,
                prefix=".manifest-import-", delete=False) as tmp:
            staged_manifest = Path(tmp.name)
            tmp.write(json.dumps(prospective, indent=2, sort_keys=True) + "\n")
        # A hard link publishes a finished WAV without clobbering a file
        # created by another importer between the initial check and now.
        os.link(staged_wav, output)
        published_wav = True
        os.replace(staged_manifest, manifest_path)
        staged_manifest = None
    except Exception:
        if published_wav:
            output.unlink(missing_ok=True)
        raise
    finally:
        if staged_wav is not None:
            staged_wav.unlink(missing_ok=True)
        if staged_manifest is not None:
            staged_manifest.unlink(missing_ok=True)
    print("respeaker-import=ok", name, "frames=", frames,
          "sha256=", record["sha256"])


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--wav", type=Path, required=True)
    p.add_argument("--annotation", type=Path, required=True)
    p.add_argument("--corpus", type=Path, required=True)
    p.add_argument("--array", type=Path,
                   help="JSON microphone geometry for arbitrary channel layouts")
    args = p.parse_args()
    array = json.loads(args.array.read_text(encoding="utf-8")) if args.array else None
    import_recording(args.wav, args.annotation, args.corpus, array)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print("respeaker-import=fail:", exc, file=sys.stderr)
        sys.exit(1)
