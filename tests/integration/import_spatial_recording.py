#!/usr/bin/env python3
"""Import a real annotated microphone-array WAV using explicit array geometry.

Input is already aligned, 16 kHz PCM16 WAV. This tool never fabricates source
bearings, infers array geometry, remixes channels, or resamples audio.
"""
import argparse
import json
import sys
from pathlib import Path

from import_respeaker_recording import import_recording


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--annotation", type=Path, required=True)
    parser.add_argument("--array", type=Path, required=True,
                        help="JSON object with microphones: [{channel, xyz_m}]")
    parser.add_argument("--corpus", type=Path, required=True)
    args = parser.parse_args()
    geometry = json.loads(args.array.read_text(encoding="utf-8"))
    import_recording(args.wav, args.annotation, args.corpus, geometry)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("spatial-corpus-import=fail:", error, file=sys.stderr)
        sys.exit(1)
