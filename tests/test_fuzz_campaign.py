# tests/test_fuzz_campaign.py
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("campaign", Path(__file__).resolve().parents[1] / "tools/fuzz_campaign.py")
campaign = importlib.util.module_from_spec(spec)
spec.loader.exec_module(campaign)


class CampaignTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.binary = self.root / "bin"
        self.binary.mkdir()
        self.output = self.root / "campaign"
        for target in campaign.TARGETS:
            (self.binary / ("fuzz_" + target)).write_bytes(b"fixture, not executable")
            seeds = self.root / "tools/fuzz/corpus" / target
            seeds.mkdir(parents=True)
            (seeds / "seed").write_bytes(b"seed")
        for mock in (patch.object(campaign, "SOURCE", self.root),
                     patch.object(campaign.subprocess, "check_output", side_effect=lambda args, **kw: "abc\n" if args[1] == "rev-parse" else "")):
            mock.start()
            self.addCleanup(mock.stop)

    @staticmethod
    def success(command, **kwargs):
        kwargs["stdout"].write(b"#123 DONE cov: 12 ft: 30\nstat::number_of_executed_units: 123\n")
        return subprocess.CompletedProcess(command, 0)

    def test_round_robin_logs_counters_and_completed_resume(self):
        with patch.object(campaign.subprocess, "run", side_effect=self.success) as run, patch.object(campaign.time, "monotonic", side_effect=[0, 10, 10, 20, 20, 30, 30, 40, 40, 50]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 50, 10), 0)
        state = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(state["status"], "completed")
        self.assertEqual([r["target"] for r in state["runs"]], list(campaign.TARGETS))
        self.assertEqual(state["runs"][0]["statistics"]["number_of_executed_units"], 123)
        self.assertEqual(state["runs"][0]["statistics"]["last_coverage"], 12)
        self.assertFalse((self.output / "campaign.lock").exists())
        self.assertEqual(run.call_count, 5)
        with patch.object(campaign.subprocess, "run") as resumed:
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 50, 10, True), 0)
            resumed.assert_not_called()

    def test_failure_stops_and_retains_log(self):
        with patch.object(campaign.subprocess, "run", return_value=subprocess.CompletedProcess([], 77)), patch.object(campaign.time, "monotonic", side_effect=[0, 1]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 50, 10), 1)
        state = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(state["status"], "failed")
        self.assertEqual(len(state["runs"]), 1)
        self.assertTrue((self.output / state["runs"][0]["log"]).is_file())
        with self.assertRaisesRegex(ValueError, "triage"):
            campaign.initialize(self.binary, self.output, 50, True)

    def test_pause_resume_preserves_time_and_generated_corpus(self):
        with patch.object(campaign.subprocess, "run", side_effect=KeyboardInterrupt), patch.object(campaign.time, "monotonic", side_effect=[0, 1]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 50, 10), 130)
        generated = self.output / "corpus/gltf/generated"
        generated.write_bytes(b"new seed")
        state = campaign.initialize(self.binary, self.output, 50, True)
        self.assertEqual(state["status"], "paused")
        self.assertEqual(state["runs"][0]["elapsed_seconds"], 1)
        self.assertEqual(generated.read_bytes(), b"new seed")
        (self.binary / "fuzz_gltf").write_bytes(b"different build")
        with self.assertRaisesRegex(ValueError, "same source"):
            campaign.initialize(self.binary, self.output, 50, True)

    def test_refuses_missing_harness_dirty_source_and_overwrite(self):
        with self.assertRaises(ValueError):
            campaign.initialize(self.binary, self.output, 0, False)
        with patch.object(campaign.subprocess, "check_output", return_value="dirty"):
            with self.assertRaisesRegex(ValueError, "commit source"):
                campaign.initialize(self.binary, self.output, 50, False)
        campaign.initialize(self.binary, self.output, 50, False)
        with self.assertRaisesRegex(ValueError, "fresh output"):
            campaign.initialize(self.binary, self.output, 50, False)
        (self.binary / "fuzz_gltf").unlink()
        with self.assertRaisesRegex(ValueError, "missing instrumented"):
            campaign.initialize(self.binary, self.output, 50, True)

    def test_lock_refuses_second_worker_without_removing_its_lock(self):
        campaign.initialize(self.binary, self.output, 50, False)
        lock = self.output / "campaign.lock"
        lock.write_text("another worker")
        with self.assertRaises(FileExistsError):
            campaign.run_campaign(self.binary, self.output, 50, 10, True)
        self.assertEqual(lock.read_text(), "another worker")


if __name__ == "__main__":
    unittest.main()
