"""Contract tests only; fake rows are NOT an Acoustix/ODAS quality result."""
import json
from pathlib import Path
import tempfile
import unittest
import wave

from acoustix_s2_gmm_controlled_sweep import (
    EXPECTED, VARIANTS, SweepError, file_sha256, silent_control,
    trace_metrics, verify_recorded_case, sweep)


def row(index, tracks=None, births=(), retirements=()):
    tracks = tracks or []
    return {
        "schema": "odas_sst_raw_v1",
        "odas_timestamp": index + 1,
        "hop_index": index,
        "sample_offset": index * 128,
        "hop_frames": 128,
        "tracks_after": tracks,
        "births": list(births),
        "retirements": list(retirements),
    }


class GmmSensitivityContractTests(unittest.TestCase):
    def test_one_factor_variants_keep_other_parameters_frozen(self):
        base = EXPECTED["baseline"]
        self.assertEqual(tuple(VARIANTS), tuple(EXPECTED))
        for variant, args in VARIANTS.items():
            changed = [key for key, val in EXPECTED[variant].items()
                       if val != base[key]]
            self.assertEqual(len(changed), int(variant != "baseline"))
            self.assertIn(len(args), (0, 2))

    def test_null_voice_control_must_have_actual_pcm_frames(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)
            source, silent = p/"speech.wav", p/"zero.wav"
            with wave.open(str(source), "wb") as wav:
                wav.setnchannels(6)
                wav.setsampwidth(2)
                wav.setframerate(16000)
                wav.writeframes(b"\x01\x00" * 6 * 240000)
            self.assertNotEqual(file_sha256(source),
                                silent_control(source, silent))
            with wave.open(str(silent), "rb") as wav:
                self.assertEqual(wav.getnframes(), 240000)
                self.assertEqual(wav.getnchannels(), 6)
                self.assertFalse(any(wav.readframes(240000)))

    def test_invalid_t00_source_fails_closed(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)
            with wave.open(str(p/"short.wav"), "wb") as wav:
                wav.setnchannels(6)
                wav.setsampwidth(2)
                wav.setframerate(16000)
                wav.writeframes(b"\0" * 6 * 2 * 128)
            with self.assertRaisesRegex(SweepError, "frozen six-channel"):
                silent_control(p/"short.wav", p/"zero.wav")

    def test_actual_hop_clock_is_checked(self):
        rows = [row(i) for i in range(1875)]
        self.assertEqual(trace_metrics(rows, 240000)["hops_any_active"], 0)
        rows[10]["sample_offset"] += 128
        with self.assertRaisesRegex(SweepError, "noncontiguous"):
            trace_metrics(rows, 240000)

    def test_active_is_not_provisional(self):
        rows = [row(i) for i in range(1875)]
        rows[101] = row(101, [{"slot": 0, "track_id": 5, "state": "P"}],
                        births=[5])
        rows[102] = row(102, [{"slot": 0, "track_id": 5, "state": "A"}])
        result = trace_metrics(rows, 240000, frozenset({101, 102}))
        self.assertEqual(result["hops_any_active"], 1)
        self.assertEqual(result["birth_events"], 1)
        self.assertEqual(result["total_provisional_track_hops"], 1)
        self.assertEqual(result["oracle_two_voice_hops_with_at_least_two_active_tracks"], 0)

    def test_two_active_tracks_requires_distinct_real_sst_ids(self):
        rows = [row(i) for i in range(1875)]
        rows[100] = row(100, [{"track_id": 1, "state": "A"},
                              {"track_id": 2, "state": "A"}])
        result = trace_metrics(rows, 240000, frozenset({100}))
        self.assertEqual(result["oracle_two_voice_hops_with_at_least_two_active_tracks"], 1)
        self.assertEqual(result["max_simultaneous_active_tracks"], 2)
        rows[100]["tracks_after"][1]["track_id"] = 1
        with self.assertRaisesRegex(SweepError, "duplicate"):
            trace_metrics(rows, 240000, frozenset({100}))

    def test_empty_oracle_overlap_cannot_count_as_pass(self):
        rows = [row(i) for i in range(1875)]
        with self.assertRaisesRegex(SweepError, "overlap"):
            trace_metrics(rows, 240000, frozenset())

    def test_trace_overcapacity_rejected(self):
        rows = [row(i) for i in range(1875)]
        rows[0]["tracks_after"] = [{"track_id": i, "state": "P"}
                                  for i in range(5)]
        with self.assertRaisesRegex(SweepError, "capacity"):
            trace_metrics(rows, 240000)

    def test_refuse_reusing_existing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)
            (p/"output").mkdir()
            (p/"runner").write_text("")
            (p/"validator").write_text("")
            with self.assertRaisesRegex(SweepError, "already exists"):
                sweep(p, p/"runner", p/"validator", p/"output")

    def test_original_wav_hash_cannot_be_ignored(self):
        self.assertNotEqual(file_sha256(__file__),
                            "c14fe750e47064de1b105c5fceab1ffe8e5330157b7519818a5c175d6c54f0ee")


if __name__ == "__main__":
    unittest.main()
