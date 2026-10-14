#!/usr/bin/env python3
"""Test the installed libaudition CMake package as an external C++17 consumer.

The test intentionally compiles and executes a fresh project rather than
using targets from the source tree. Missing installs and broken exports fail.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap


def run(argv: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", *map(str, argv), flush=True)
    subprocess.run(argv, check=True, env=env)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--prefix", required=True, type=Path)
    p.add_argument("--cmake", required=True)
    p.add_argument("--generator", required=True)
    p.add_argument("--version", required=True)
    args = p.parse_args()
    prefix = args.prefix.resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="libaudition-consumer-") as folder:
        root = Path(folder)
        (root / "CMakeLists.txt").write_text(textwrap.dedent(f"""\
            cmake_minimum_required(VERSION 3.20)
            project(libaudition_installed_consumer LANGUAGES CXX)
            find_package(libaudition {args.version} EXACT CONFIG REQUIRED)
            add_executable(consumer main.cpp)
            target_link_libraries(consumer PRIVATE
                audition::core
                audition::interfaces
                audition::dsp
                audition::spatial
                audition::memory
                audition::pipeline)
            target_compile_features(consumer PRIVATE cxx_std_17)
            """), encoding="utf-8")
        (root / "main.cpp").write_text(textwrap.dedent(f"""\
            #include <audition/audition.hpp>
            int main() {{
                if (audition::kVersion != "{args.version}") return 1;
                audition::TemporalSpatialTrackSmoother smoother{{}};
                audition::AcousticEventAssembler events{{}};
                audition::InMemoryAcousticSourceRegistry registry{{}};
                return smoother.trackedCount() == 0U &&
                       events.activeCount() == 0U &&
                       registry.list().empty() ? 0 : 2;
            }}
            """), encoding="utf-8")
        build = root / "build"
        run([args.cmake, "-S", str(root), "-B", str(build), "-G",
             args.generator, f"-DCMAKE_PREFIX_PATH={prefix}"])
        run([args.cmake, "--build", str(build), "--parallel", "2"])
        exe = build / ("consumer.exe" if os.name == "nt" else "consumer")
        env = dict(os.environ)
        if os.name == "nt":
            key = "PATH"
        elif sys.platform == "darwin":
            key = "DYLD_LIBRARY_PATH"
        else:
            key = "LD_LIBRARY_PATH"
        library_paths = [str(prefix / subdir) for subdir in ("lib", "lib64", "bin")]
        env[key] = os.pathsep.join(library_paths + ([env[key]] if key in env else []))
        run([str(exe)], env=env)
        print("PASS: installed libaudition package consumed from separate C++17 project")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
