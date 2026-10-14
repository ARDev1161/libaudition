#!/usr/bin/env python3
"""Fail a CTest build if expected algorithm test registrations disappear.

This audits TEST DISCOVERY, not measured statement/branch coverage or accuracy.
The observed tests come from CTest's machine-readable API, including
GoogleTest-discovered cases; no scraping log text or hard-coded test counts.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

CORE = {
    "time and timestamps": r"^Time\.",
    "audio buffer and memory layout": r"^AudioBuffer\.",
    "spatial probability / direction": r"^(Probability|Direction|DirectionEstimate)\.",
    "factory registration": r"^FactoryRegistry\.",
    "bounded queues": r"^BoundedQueue\.",
    "model registry": r"^ModelRegistry\.",
    "public API headers": r"^PublicHeaders\.",
    "memory registry": r"^MemoryRegistry\.",
    "sound prototype matching": r"^(SoundPrototypeRegistry|SoundPrototypeMatcher)\.",
    "source identity resolution": r"^SourceIdentityResolver\.",
    "spatial / identity coordination": r"^SpatialIdentityCoordinator\.",
    "acoustic event assembly": r"^AcousticEventAssembler\.",
    "end-to-end acoustic pipeline scenario": r"^AcousticPipelineScenario\.",
    "source range prior": r"^SoundLevelRangePrior\.",
    "SPL calibration": r"^SplCalibration\.",
    "temporal track smoothing": r"^TemporalSpatialTrackSmoother\.",
    "basic DSP / channel conversions": r"^Dsp\.",
    "multi-channel audio frontend": r"^AudioFrontend\.",
    "audio quality": r"^AudioQuality\.",
    "speech contracts": r"^SpeechContracts\.",
    "speaker contracts": r"^SpeakerContracts\.",
    "shared vocabulary": r"^Vocabulary\.",
    "per-source classification": r"^SourceClassificationRuntime\.",
    "progressive audio classification": r"^ProgressiveSourceClassificationRuntime\.",
    "per-source VAD/ASR runtime": r"^PerSourceSpeechRuntime\.",
    "synthetic multi-source stress (5)": r"^progressive_stress_5_sources$",
    "synthetic multi-source stress (10)": r"^progressive_stress_10_sources$",
    "WAV file recording": r"^source_wav_recorder_test$",
    "public audio recording APIs": r"^recording_audio_apis_test$",
    "recording session lifecycle": r"^source_recording_session_test$",
}
BACKENDS = {
    "LIBAUDITION_WITH_ODAS": {
        "ODAS configuration": r"^OdasOptions\.",
        "ODAS spatial processing": r"^OdasBackend\.",
        "ODAS synthetic six-channel SSL/SST": r"^audio_integration_(static|moving|two)$",
        "ODAS DOA/track numeric gates": r"^audio_integration_near_plane_(static|moving|two)$",
    },
    "LIBAUDITION_WITH_LIBSAMPLERATE": {"resampler": r"^SamplerateBackend\."},
    "LIBAUDITION_WITH_WEBRTC_AEC3": {"AEC3": r"^WebRtcAec3Backend\."},
    "LIBAUDITION_WITH_SHERPA": {
        "Sherpa configuration": r"^SherpaConfig\.",
        "Sherpa VAD": r"^SherpaVad\.",
        "speaker verification": r"^SherpaSpeakerVerifier\.",
        "speaker identification": r"^SherpaSpeakerIdentifier\.",
        "per-source speech runtime": r"^PerSourceSpeechRuntime\.",
        "speech quality scorer correctness": r"^speech_report_scoring_selftest$",
    },
    "LIBAUDITION_WITH_CLAP": {
        "CLAP audio frontend": r"^ClapFrontend\.",
        "CLAP embeddings": r"^ClapBackend\.",
        "CLAP text tokenizer": r"^ClapTokenizer\.",
        "CLAP open vocabulary": r"^ClapOpenVocabulary\.",
    },
    "LIBAUDITION_WITH_YAMNET": {
        "YAMNet pipeline contract": r"^SourceClassificationRuntime\.",
    },
    "LIBAUDITION_WITH_EFFICIENTAT": {
        "EfficientAT frontend": r"^EfficientAtFrontend\.",
        "EfficientAT inference": r"^EfficientAtBackend\.",
        "EfficientAT resampling": r"^EfficientAtResampler\.",
    },
    "LIBAUDITION_WITH_AASIST": {
        "authenticity detector": r"^AasistBackend\.",
        "authenticity configuration": r"^AasistConfig\.",
    },
    "LIBAUDITION_WITH_WORLD": {
        "WORLD voice traits": r"^WorldBackend\.",
        "WORLD acoustic feature analysis": r"^WorldAcousticBackend\.",
    },
    "LIBAUDITION_WITH_GTSAM": {
        "GTSAM spatial fusion": r"^GtsamSpatialFusion\.",
    },
}
MODEL_TOOL_CASES = (
    "test_efficientat_wav_evaluator",
    "test_compare_efficientat_wavs",
    "test_audioset_vocabulary_audit",
    "test_compare_audio_taggers",
    "test_analyze_yamnet_event_timeline",
    "test_yamnet_esc10_extended",
    "test_review_audio_taggers_report",
)
REAL_YAMNET = (
    "real_yamnet_wav_replay_ctest",
    "real_yamnet_onnx_cpp_parity_short",
    "real_yamnet_onnx_cpp_parity_long",
    "real_yamnet_label_rejection",
    "real_yamnet_behavior",
    "real_yamnet_progressive_smoke",
    "real_yamnet_stress_5",
    "real_yamnet_stress_10",
    "real_yamnet_wav_benchmark",
)
REAL_EFFICIENTAT = (
    "real_efficientat_mn10_as",
    "real_efficientat_dymn10_as",
    "real_efficientat_wav_benchmark_mn10_as",
    "real_efficientat_wav_benchmark_dymn10_as",
)
REAL_COMMON = (
    "real_audioset_vocabulary_audit",
    "real_audioset_smoke_comparison",
    "real_esc10_diagnose_mn10_wav",
    "real_esc10_diagnose_mn10_duration",
    "real_esc10_compare_models",
    "real_esc10_yamnet_timeline",
    "real_esc10_review_scores",
    "real_esc10_yamnet_80",
)
REAL_SPEECH = (
    "speech_real_whisper_two_sources",
    "speech_real_silero_sensevoice_wav",
    "speech_real_sensevoice_quality",
)
REAL_QT_SHERPA = ("qt_real_silero", "qt_real_whisper-real", "qt_real_speaker-real")


def cache_flags(build: Path) -> dict[str, bool]:
    raw = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    result = {}
    for line in raw.splitlines():
        match = re.fullmatch(r"(LIBAUDITION_[A-Z0-9_]+|BUILD_TESTING):BOOL=(\S+)", line)
        if match:
            result[match.group(1)] = match.group(2).upper() in {"ON", "TRUE", "YES", "1"}
    return result


def observed_ctest(ctest: str, build: Path) -> list[str]:
    proc = subprocess.run([ctest, "--test-dir", str(build), "--show-only=json-v1"],
                          check=True, capture_output=True, text=True)
    data = json.loads(proc.stdout)
    names = [record["name"] for record in data["tests"]]
    if len(names) != len(set(names)):
        raise ValueError("duplicate CTest test registrations")
    return names


def expectations(flags: dict[str, bool]) -> dict[str, str]:
    required = dict(CORE)
    for option, suite in BACKENDS.items():
        if flags.get(option):
            required.update({f"{option}: {name}": pattern for name, pattern in suite.items()})
    if flags.get("LIBAUDITION_REGISTER_MODEL_TOOL_TESTS"):
        required.update({f"model tool: {name}": f"^{re.escape('model_tool_' + name)}$"
                         for name in MODEL_TOOL_CASES})
        if flags.get("LIBAUDITION_WITH_YAMNET"):
            required.update({f"real YAMNet: {name}": f"^{re.escape(name)}$"
                             for name in REAL_YAMNET})
        if flags.get("LIBAUDITION_WITH_EFFICIENTAT"):
            required.update({f"real EfficientAT: {name}": f"^{re.escape(name)}$"
                             for name in REAL_EFFICIENTAT})
        if flags.get("LIBAUDITION_WITH_YAMNET") and flags.get("LIBAUDITION_WITH_EFFICIENTAT"):
            required.update({f"real AudioSet/ESC-10: {name}": f"^{re.escape(name)}$"
                             for name in REAL_COMMON})
    if flags.get("LIBAUDITION_REGISTER_REAL_QT_MODEL_TESTS") and flags.get("LIBAUDITION_WITH_SHERPA"):
        required.update({f"real speech: {name}": f"^{re.escape(name)}$" for name in REAL_SPEECH})
    if flags.get("LIBAUDITION_REGISTER_EFFICIENTAT_REFERENCE_TESTS") and flags.get("LIBAUDITION_WITH_EFFICIENTAT"):
        required.update({f"reference frontend: {name}": f"^{name}$" for name in (
            "efficientat_synthetic_onnx_fixture",
            "efficientat_frontend_reference_dump",
            "efficientat_frontend_torch_parity",
        )})
    return required


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    flags = cache_flags(build)
    observed = observed_ctest(args.ctest, build)
    expected = expectations(flags)
    checks = {name: any(re.search(pattern, test) for test in observed)
              for name, pattern in expected.items()}
    untracked_options = sorted(option for option in flags
                               if option.startswith("LIBAUDITION_WITH_")
                               and option not in BACKENDS
                               and option not in {"LIBAUDITION_WITH_SPDLOG"})
    # Platform/backend accelerator selection isn't itself a new algorithm.
    untracked_options = [opt for opt in untracked_options if
                         not opt.startswith("LIBAUDITION_WITH_") or
                         opt in ("LIBAUDITION_WITH_",)]
    report = {
        "kind": "CTest registration contract, NOT branch or hardware accuracy coverage",
        "build": str(build),
        "configured_algorithms": [opt for opt in BACKENDS if flags.get(opt)],
        "registered_test_count": len(observed),
        "required_case_count": len(expected),
        "registered_case_checks": checks,
        "missing": sorted(name for name, passed in checks.items() if not passed),
        "limits": [
            "Names establish registration, not functional quality or code/branch coverage",
            "Real 6-channel ReSpeaker DOA accuracy requires ground-truth hardware recordings",
            "Unconfigured optional backends are not validated by this CTest build",
        ],
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("registered_test_count", "required_case_count", "missing")},
                     indent=2))
    return 0 if not report["missing"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"CTest coverage audit failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
