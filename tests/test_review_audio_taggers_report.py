#!/usr/bin/env python3
"""Guardrail tests for confidence saturation and silent clips."""
import importlib.util
from pathlib import Path
path=Path(__file__).resolve().parents[1]/"tools/review_audio_taggers_report.py"
spec=importlib.util.spec_from_file_location("audio_review",path)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
report={"models":{
 "mn10_as":{"observations":[{"wav":"chainsaw.wav","top5":[{"label":f"Speech {i}","score":1.0} for i in range(5)]}]},
 "yamnet":{"observations":[{"wav":"dog.wav","top5":[{"label":"Silence","score":1.0}]}]},
 "dymn10_as":{"observations":[{"wav":"rain.wav","top5":[{"label":"Rain","score":0.6}]}]}
}}
findings=module.analyze(report)
assert any(f["kind"]=="saturated_top5" and f["model"]=="mn10_as" for f in findings)
assert any(f["kind"]=="silence_dominant" and f["model"]=="yamnet" for f in findings)
assert not any(f["kind"] in ("saturated_top5","silence_dominant") and f["model"]=="dymn10_as" for f in findings)
assert sum(f["kind"]=="insufficient_corpus" for f in findings)==3
print("PASS: real-event score diagnostic guardrails")
