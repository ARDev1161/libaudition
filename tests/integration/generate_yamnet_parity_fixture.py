#!/usr/bin/env python3
"""Generate deterministic YAMNet PCM16 parity inputs and an actual WAV."""
from __future__ import annotations

import argparse
import math
import pathlib
import struct
import wave


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    def pcm(sample_count: int) -> bytes:
        return b"".join(
            struct.pack("<h", int(12000 * math.sin(2 * math.pi * (220 + i / 80) * i / 16000)))
            for i in range(sample_count)
        )

    short_pcm = pcm(15600)
    long_pcm = pcm(80000)
    (args.output_dir / "yamnet-parity.raw").write_bytes(short_pcm)
    (args.output_dir / "yamnet-parity-5s.raw").write_bytes(long_pcm)
    with wave.open(str(args.output_dir / "yamnet-parity-5s.wav"), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(long_pcm)


if __name__ == "__main__":
    main()
