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
    "logging sink contracts": r"^Logging\.",
    "public API headers": r"^PublicHeaders\.",
    "memory registry": r"^MemoryRegistry\.",
    "sound prototype matching": r"^(SoundPrototypeRegistry|SoundPrototypeMatcher)\.",
    "source identity resolution": r"^SourceIdentityResolver\.",
    "spatial / identity coordination": r"^SpatialIdentityCoordinator\.",
    "acoustic event assembly": r"^AcousticEventAssembler\.",
    "temporal acoustic event detection": r"^TemporalAcousticEventDetector\.",
    "end-to-end acoustic pipeline scenario": r"^AcousticPipelineScenario\.",
    "source range prior": r"^SoundLevelRangePrior\.",
    "SPL calibration": r"^SplCalibration\.",
    "temporal track smoothing": r"^TemporalSpatialTrackSmoother\.",
    "basic DSP / channel conversions": r"^Dsp\.",
    "WAV parser and streaming I/O": r"^WavIoTest\.",
    "strict timeline JSON parser": r"^RecordingTimelineJson\.",
    "source noise suppression bypass": r"^SourceNoiseSuppression\.",
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
    "audio corpus manifest and independent annotations": r"^audio_respeaker_manifest_selftest$",
    "CI fail-closed CTest entrypoints": r"^ci_ctest_entrypoints_selftest$",
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
    "LIBAUDITION_WITH_SPEEXDSP_AGC": {
        "SpeexDSP uneven hops and reset": r"^SourceGain\.SpeexHandlesUnevenInputFramesAndReset$",
        "SpeexDSP invalid input and recovery": r"^SourceGain\.SpeexRejectsNonfiniteChunkWithoutMutatingStream$",
    },
    "LIBAUDITION_WITH_WEBRTC_AEC3": {"AEC3": r"^WebRtcAec3Backend\."},
    "LIBAUDITION_WITH_WEBRTC_APM": {
        "APM noise suppression": r"^WebRtcApmNoiseSuppressor\.",
        "APM AGC1/AGC2 streaming": r"^SourceGain\.UnifiedApm",
    },
    "LIBAUDITION_WITH_SHERPA": {
        "Sherpa configuration": r"^SherpaConfig\.",
        "Sherpa VAD": r"^SherpaVad\.",
        "speaker verification": r"^SherpaSpeakerVerifier\.",
        "speaker identification": r"^SherpaSpeakerIdentifier\.",
        "per-source speech runtime": r"^PerSourceSpeechRuntime\.",
        "speech quality scorer correctness": r"^speech_report_scoring_selftest$",
        "reference RTTM DER scoring and permutation": r"^speech_diarization_scoring_selftest$",
        "multi-recording corpus DER aggregation": r"^speech_diarization_corpus_scoring_selftest$",
        "complete VoxConverse archive and label pairing": r"^speech_voxconverse_full_split_preflight_selftest$",
        "fail-closed shard aggregation": r"^speech_voxconverse_shard_aggregation_selftest$",
        "streaming ASR model variants and runtime validation": r"^SherpaOnlineAsrContract\.",
        "keyword spotting model variants and thresholds": r"^SherpaKwsContract\.",
        "language identification configuration": r"^SherpaLanguageIdContract\.",
        "offline and streaming denoising constraints": r"^SherpaDenoiserContract\.",
        "diarization clustering and timing constraints": r"^SherpaDiarizationContract\.",
        "speaker embedding model constraints": r"^SherpaSpeakerEmbeddingContract\.",
    },
    "LIBAUDITION_WITH_WHISPERCPP": {
        "whisper.cpp adapter smoke": r"^whispercpp_adapter_smoke$",
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
# The source inventory is a change-detection contract, not a declaration
# that every source statement has been executed. Any newly compiled .cpp
# requires explicit inventory review in the same PR.
CORE_SOURCE_UNITS = """
core/error.cpp core/time.cpp core/audio_buffer.cpp
io/wav_io.cpp io/recording_timeline_json.cpp
spatial/level_range_prior.cpp spatial/spl_calibration.cpp spatial/temporal_smoother.cpp
model/in_memory_model_registry.cpp logging/logging.cpp
pipeline/acoustic_event_assembler.cpp pipeline/temporal_acoustic_event_detector.cpp pipeline/spatial_identity.cpp
pipeline/vocabulary.cpp pipeline/source_classification_runtime.cpp
pipeline/progressive_source_classification_runtime.cpp pipeline/per_source_speech_runtime.cpp
memory/in_memory_registries.cpp memory/sound_prototype_matcher.cpp memory/source_identity_resolver.cpp
dsp/basic.cpp dsp/audio_frontend.cpp dsp/quality.cpp
""".split()
BACKEND_SOURCE_UNITS = {
    "odas": "odas_options.cpp odas_config.cpp odas_runtime.cpp odas_spatial_engine.cpp",
    "samplerate": "samplerate_resampler.cpp",
    "webrtc_aec3": "webrtc_apm_echo_canceller.cpp",
    "sherpa": """sherpa_config.cpp sherpa_vad.cpp sherpa_offline_asr.cpp
        sherpa_streaming_asr.cpp sherpa_keyword_spotter.cpp sherpa_language_identifier.cpp
        sherpa_audio_tagger.cpp sherpa_speech_denoiser_config.cpp
        sherpa_offline_speech_denoiser.cpp sherpa_streaming_speech_denoiser.cpp
        sherpa_speaker_config.cpp sherpa_speaker_embedder.cpp sherpa_speaker_verifier.cpp
        sherpa_speaker_identifier.cpp sherpa_speaker_diarizer.cpp
        sherpa_tts_config.cpp sherpa_tts.cpp""",
    "whispercpp": "whispercpp_asr.cpp",
    "clap": "clap_features.cpp clap_audio_embedder.cpp roberta_tokenizer.cpp clap_text_encoder.cpp clap_open_vocabulary_classifier.cpp",
    "yamnet": "yamnet_audio_tagger.cpp",
    "efficientat": "efficientat_audio_tagger.cpp frontend.cpp resampler.cpp",
    "aasist": "aasist_authenticity_detector.cpp",
    "world": "world_voice_traits_estimator.cpp world_acoustic_analyzer.cpp",
    "gtsam": "gtsam_spatial_fusion.cpp",
}


def source_inventory(root: Path) -> dict:
    """Read compiled C++ translation units from their actual CMake manifests."""
    base = (root / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
    core = {"src/" + p for p in
            re.findall(r"\b(?:core|io|spatial|model|logging|pipeline|memory|dsp)/[\w]+\.cpp", base)}
    expected_core = {"src/" + p for p in CORE_SOURCE_UNITS}
    unknown = sorted(core - expected_core)
    missing = sorted(expected_core - core)
    observed = sorted(core)
    for backend, filenames in BACKEND_SOURCE_UNITS.items():
        cmake_path = root / "backends" / backend / "CMakeLists.txt"
        manifest = cmake_path.read_text(encoding="utf-8")
        src = {f"backends/{backend}/src/" + p
               for p in re.findall(r"\bsrc/([\w]+\.cpp)", manifest)}
        expected = {f"backends/{backend}/src/" + p for p in filenames.split()}
        unknown.extend(sorted(src - expected))
        missing.extend(sorted(expected - src))
        observed.extend(sorted(src))
    on_disk_missing = [p for p in observed if not (root / p).is_file()]
    return {"compiled_translation_units": len(observed),
            "untracked_compiled_sources": sorted(unknown),
            "missing_inventory_sources": sorted(missing),
            "missing_source_files": sorted(on_disk_missing)}


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
    "speech_real_gtcrn_denoising",
    "speech_real_kws_gigaspeech",
    "speech_real_streaming_asr_zipformer",
    "speech_real_language_id_whisper",
    "speech_real_speaker_diarization",
    "speech_real_speaker_diarization_benchmark",
    "speech_real_speaker_diarization_reference_der",
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
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    flags = cache_flags(build)
    sources = source_inventory(args.source.resolve(strict=True))
    observed = observed_ctest(args.ctest, build)
    expected = expectations(flags)
    checks = {name: any(re.search(pattern, test) for test in observed)
              for name, pattern in expected.items()}
    # A newly introduced backend must extend this contract explicitly.
    untracked_options = sorted(option for option in flags
                               if option.startswith("LIBAUDITION_WITH_")
                               and option not in BACKENDS
                               and option != "LIBAUDITION_WITH_SPDLOG")
    report = {
        "kind": "CTest registration contract, NOT branch or hardware accuracy coverage",
        "build": str(build),
        "configured_algorithms": [opt for opt in BACKENDS if flags.get(opt)],
        "registered_test_count": len(observed),
        "required_case_count": len(expected),
        "registered_case_checks": checks,
        "missing": sorted(name for name, passed in checks.items() if not passed),
        "untracked_backend_options": untracked_options,
        "source_inventory": sources,
        "limits": [
            "Names establish registration, not functional quality or code/branch coverage",
            "Real 6-channel ReSpeaker DOA accuracy requires ground-truth hardware recordings",
            "Unconfigured optional backends are not validated by this CTest build",
        ],
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("registered_test_count", "required_case_count", "missing", "untracked_backend_options", "source_inventory")},
                     indent=2))
    source_gaps = (sources["untracked_compiled_sources"]
                   + sources["missing_inventory_sources"]
                   + sources["missing_source_files"])
    return 0 if not report["missing"] and not untracked_options and not source_gaps else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"CTest coverage audit failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
