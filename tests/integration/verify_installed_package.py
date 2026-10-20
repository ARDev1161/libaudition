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
    p.add_argument("--backend", choices=("core", "sherpa"), default="core")
    args = p.parse_args()
    prefix = args.prefix.resolve(strict=True)

    # Verify the distribution, not merely a build-tree package discovered by
    # CMake. A missing notice or exported header is a release-blocking error.
    package_dirs = list(prefix.glob("lib*/cmake/libaudition"))
    if len(package_dirs) != 1:
        raise RuntimeError(f"Expected one installed CMake package under {prefix}, "
                           f"found {package_dirs}")
    package_dir = package_dirs[0]
    for relative in (
        "include/audition/audition.hpp",
        "include/audition/io/wav_io.hpp",
        "include/audition/speech/async_speech_ingress_worker.hpp",
        "include/audition/temporal/streaming_pattern_matcher.hpp",
        "share/libaudition/LICENSE",
        "share/libaudition/NOTICE",
        "share/libaudition/THIRD_PARTY.md",
    ):
        if not (prefix / relative).is_file():
            raise FileNotFoundError(f"Missing installed package asset: {relative}")
    for filename in ("libauditionConfig.cmake", "libauditionConfigVersion.cmake",
                     "libauditionTargets.cmake"):
        if not (package_dir / filename).is_file():
            raise FileNotFoundError(f"Missing CMake export: {filename}")
    licenses_dir = prefix / "share/libaudition/LICENSES"
    if not licenses_dir.is_dir() or not any(licenses_dir.glob("*.txt")):
        raise FileNotFoundError("Installed third-party license texts are missing")

    with tempfile.TemporaryDirectory(prefix="libaudition-consumer-") as folder:
        root = Path(folder)
        extra_target = "\n                audition::backend_sherpa" if args.backend == "sherpa" else ""
        extra_sources = " speech_smoke.cpp" if args.backend == "core" else ""
        if args.backend == "core":
            # Keep substantial C++ fixtures in normal .cpp files. Unlike
            # inline C++ inside Python f-strings, braces need no escaping.
            fixture = Path(__file__).with_name("installed_speech_consumer.cpp")
            (root / "speech_smoke.cpp").write_bytes(fixture.read_bytes())
        (root / "CMakeLists.txt").write_text(textwrap.dedent(f"""\
            cmake_minimum_required(VERSION 3.20)
            project(libaudition_installed_consumer LANGUAGES CXX)
            set(CMAKE_CXX_STANDARD 17)
            set(CMAKE_CXX_STANDARD_REQUIRED ON)
            set(CMAKE_CXX_EXTENSIONS OFF)
            find_package(libaudition {args.version} EXACT CONFIG REQUIRED)
            add_executable(consumer main.cpp{extra_sources})
            target_link_libraries(consumer PRIVATE
                audition::core
                audition::interfaces
                audition::dsp
                audition::io
                audition::spatial
                audition::memory
                audition::model
                audition::logging
                audition::pipeline{extra_target})
            target_compile_features(consumer PRIVATE cxx_std_17)
            """), encoding="utf-8")
        if args.backend == "sherpa":
            source = textwrap.dedent("""\
                #include <audition/backends/sherpa.hpp>
                #include <type_traits>
                #include <variant>
                #include <optional>

                int main() {
                    audition::SherpaVadOptions options{};
                    std::get<audition::SherpaSileroVadModel>(options.model).model = "model.onnx";
                    audition::validateSherpaVadOptions(options);

                    audition::SherpaAudioTaggingOptions tagging{};
                    std::get<audition::SherpaAudioTaggingZipformerModel>(tagging.model).model =
                        "audio-tagger.onnx";
                    tagging.labels = "labels.csv";
                    audition::validateSherpaAudioTaggingOptions(tagging);

                    audition::SherpaSpeechDenoiserOptions denoiser{};
                    std::get<audition::SherpaSpeechDenoiserGtcrnModel>(denoiser.model).model =
                        "gtcrn.onnx";
                    audition::validateSherpaOfflineSpeechDenoiserOptions(denoiser);
                    audition::validateSherpaStreamingSpeechDenoiserOptions(denoiser);
                    static_assert(std::is_base_of_v<audition::INoiseSuppressor,
                                                    audition::SherpaOfflineSpeechDenoiser>);
                    static_assert(std::is_base_of_v<audition::INoiseSuppressor,
                                                    audition::SherpaStreamingSpeechDenoiser>);

                    audition::SherpaSpeakerIdentifier identifier{{2U, "test-model"}};
                    audition::SpeakerEnrollment enrollment{};
                    enrollment.speaker_id = audition::SpeakerId{1U};
                    enrollment.display_name = "speaker";
                    enrollment.embeddings.push_back(
                        {"test-model", {1.0F, 0.0F}, std::nullopt});
                    identifier.enroll(enrollment);

                    const audition::SpeakerEmbedding query{
                        "test-model", {1.0F, 0.0F}, std::nullopt};
                    const auto match = identifier.identify(query, audition::Score{0.5});
                    return match.has_value() ? 0 : 1;
                }
            """)
        else:
            source = textwrap.dedent(f"""\
            #include <audition/audition.hpp>
            #include <audition/io/wav_io.hpp>
            #include <vector>
            extern bool verifyInstalledSpeech();
            int main() {{
                if (audition::kVersion != "{args.version}") return 1;
                audition::TemporalSpatialTrackSmoother smoother{{}};
                audition::AcousticEventAssembler events{{}};
                audition::InMemoryAcousticSourceRegistry registry{{}};
                if (smoother.trackedCount() != 0U ||
                    events.activeCount() != 0U || !registry.list().empty()) return 2;

                // Force direct references to the core's out-of-line
                // classes from the installed shared library, not only
                // through other shared targets.
                const audition::AudioFormat mono{{
                    16000U,1U,audition::AudioLayout::Interleaved}};
                const audition::Timestamp origin{{
                    1000000000LL,{{audition::ClockDomain::Monotonic,4U}}}};
                const audition::AudioBuffer pcm{{
                    std::vector<float>{{0.1F,0.2F}},mono,origin}};
                const auto pcm_view=pcm.view();
                if (pcm.frameCount()!=2U || pcm_view.frameCount()!=2U ||
                    pcm_view.channel(0U).size()!=2U ||
                    pcm.duration().nanoseconds()!=125000LL ||
                    origin.advancedBy(pcm.duration()).nanoseconds()!=1000125000LL ||
                    origin.since(origin).nanoseconds()!=0LL) return 7;

                // Link and execute every core installed library target, not
                // only the five targets needed by the original smoke.
                audition::InMemoryModelRegistry models{{}};
                if (!models.list().empty()) return 9;
                audition::Logger logger{{"installed-consumer"}};
                logger.trace("installed package smoke");
                const auto selected=audition::io::selectMonoChannel(pcm_view,0U);
                if (selected.frameCount()!=2U ||
                    selected.samples()[1]!=0.2F) return 10;

                if (!verifyInstalledSpeech()) return 8;
                // The installed umbrella must expose the backend-neutral
                // temporal types and their C++17 inline implementations.
                audition::temporal::DynamicTimeWarping dtw{{}};
                const std::vector<std::vector<double>> sequence{{{{1.0}}, {{2.0}}}};
                const auto aligned=dtw.compare(sequence,sequence);
                if (!aligned.reachable || aligned.normalized_cost != 0.0) return 3;

                audition::temporal::StreamingPatternMatcher streaming{{}};
                streaming.registerTemplate("pulse",sequence);
                const audition::AcousticSourceId id{{1}};
                if (!streaming.push(id,{{1.0}}).empty()) return 4;
                const auto detected=streaming.push(id,{{2.0}});
                if (detected.size() != 1U || detected[0].label != "pulse") return 5;

                audition::temporal::MotifOptions motif_options{{}};
                motif_options.window_frames=2;
                motif_options.max_normalized_cost=0;
                audition::temporal::AcousticMotifFinder motifs{{motif_options}};
                const auto motif=motifs.find({{{{1.0}}, {{2.0}}, {{9.0}},
                                               {{8.0}}, {{1.0}}, {{2.0}}}});
                if (!motif.found || motif.first_start != 0U ||
                    motif.second_start != 4U) return 6;
                audition::temporal::RecurrenceAnalyzer recurrence{{}};
                audition::temporal::TemporalScalarAnalyzer scalar{{}};
                (void)recurrence;
                (void)scalar;
                return 0;
            }}
            """)
        (root / "main.cpp").write_text(source, encoding="utf-8")
        build = root / "build"
        run([args.cmake, "-S", str(root), "-B", str(build), "-G",
             args.generator, f"-DCMAKE_PREFIX_PATH={prefix}",
             f"-Dlibaudition_DIR={package_dir}",
             "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
             "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"])
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
