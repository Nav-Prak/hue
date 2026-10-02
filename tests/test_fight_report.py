import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("fight_report", Path(__file__).resolve().parents[1] / "tools/fight_report.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)

def row(source="interactive"):
    return dict(schema=1, source=source, outcome="victory", seed=2026, tuning_revision=1,
                ticks=120, hitstop_ticks=12, simulation_seconds=2, active_seconds=1.8,
                wall_seconds=2.1, hits_landed=3, hits_taken=1, staggers_inflicted=1,
                staggers_received=0, dodges=2)

class ReportTests(unittest.TestCase):
    def test_filter_and_group(self):
        rows = [row() for _ in range(10)] + [row("injected"), row("automated")]
        result = report.summarize(rows)
        self.assertEqual((result["selected"], result["excluded"]), (10, 2))
        self.assertEqual(result["groups"][0]["mean"]["wall_seconds"], 2.1)
        rows[0]["tuning_revision"] = 2
        self.assertEqual(len(report.summarize(rows)["groups"]), 2)
    def test_invalid_data(self):
        for key, value in (("schema", True), ("ticks", -1), ("ticks", True),
                           ("wall_seconds", float("nan")), ("hitstop_ticks", 121),
                           ("active_seconds", 2), ("staggers_inflicted", 4)):
            bad = row()
            bad[key] = value
            with self.assertRaises(ValueError): report.validate(bad)
    def test_bounded_and_partial_files(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fights.jsonl"
            path.write_text(json.dumps(row()) + "\n")
            self.assertEqual(len(report.load(path)), 1)
            for text in (json.dumps(row()), "{" + '"schema":1,' + json.dumps(row())[1:] + "\n", " " * 4097):
                path.write_text(text)
                with self.assertRaises(ValueError): report.load(path)

if __name__ == "__main__": unittest.main()
