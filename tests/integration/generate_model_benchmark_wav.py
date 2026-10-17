#!/usr/bin/env python3
"""Deterministic shared WAV fixture for genuine-model benchmark CTests."""
from __future__ import annotations

import argparse
import math
import pathlib
import struct
import wave


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sample_rate = 32000
    sample_count = 160000
    with wave.open(str(args.output), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        # Preserve the exact deterministic signal previously made by CI.
        wav.writeframes(
            b"".join(
                struct.pack(
                    "<h",
                    int(0.2 * 32767 * math.sin(2 * math.pi * 440 * i / sample_rate)),
                )
                for i in range(sample_count)
            )
        )


if __name__ == "__main__":
    main()
