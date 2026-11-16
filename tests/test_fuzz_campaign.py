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
        budget = 10 * len(campaign.TARGETS)  # one ten-second slice per target
        with patch.object(campaign.subprocess, "run", side_effect=self.success) as run, patch.object(campaign.time, "monotonic", side_effect=[t for i in range(len(campaign.TARGETS)) for t in (i * 10, i * 10 + 10)]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, budget, 10), 0)
        state = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(state["status"], "completed")
        self.assertEqual([r["target"] for r in state["runs"]], list(campaign.TARGETS))
        self.assertEqual(state["runs"][0]["statistics"]["number_of_executed_units"], 123)
        self.assertEqual(state["runs"][0]["statistics"]["last_coverage"], 12)
        self.assertFalse((self.output / "campaign.lock").exists())
        self.assertEqual(run.call_count, len(campaign.TARGETS))
        with patch.object(campaign.subprocess, "run") as resumed:
            self.assertEqual(campaign.run_campaign(self.binary, self.output, budget, 10, True), 0)
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

    def test_sanitizer_report_with_zero_exit_fails_the_slice(self):
        def noisy(command, **kwargs):
            kwargs["stdout"].write(b"cgltf.h:2195:11: runtime error: load of misaligned address\n")
            return subprocess.CompletedProcess(command, 0)
        with patch.object(campaign.subprocess, "run", side_effect=noisy), patch.object(campaign.time, "monotonic", side_effect=[0, 1]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 50, 10), 1)
        state = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(state["status"], "failed")
        self.assertIn("runtime error", state["runs"][0]["sanitizer_reports"][0])

    def test_exit_overrun_is_reported_as_a_runner_timeout(self):
        def hang(command, **kwargs):
            self.assertEqual(kwargs["timeout"], 10 + campaign.EXIT_GRACE_SECONDS)
            raise subprocess.TimeoutExpired(command, kwargs["timeout"])
        with patch.object(campaign.subprocess, "run", side_effect=hang), patch.object(campaign.time, "monotonic", side_effect=[0, 1]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 60, 10), 1)
        state = json.loads((self.output / "campaign.json").read_text())
        self.assertIn("runner timeout", state["runs"][0]["error"])

    def test_negative_duration_timeout_is_replayed_and_does_not_stop_the_campaign(self):
        calls = []

        def run(command, **kwargs):
            calls.append(command)
            if len(command) == 4:  # the replay of the saved input
                kwargs["stdout"].write(b"Executed in 140 ms\n")
                return subprocess.CompletedProcess(command, 0)
            if len(calls) == 1:
                saved = Path(command[-1].split("=", 1)[1]) / "timeout-abc"
                saved.write_bytes(b"input")
                kwargs["stdout"].write(b"ALARM: working on the last Unit for -1 seconds\n"
                                       b"Test unit written to " + str(saved).encode() + b"\n"
                                       b"==1== ERROR: libFuzzer: timeout after 18446744073709551615 seconds\n")
                return subprocess.CompletedProcess(command, 70)
            return self.success(command, **kwargs)

        with patch.object(campaign.subprocess, "run", side_effect=run), patch.object(campaign.time, "monotonic", side_effect=[0, 5, 5, 6, 6, 16, 16, 26, 26, 36, 36, 46, 46, 60]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 60, 10), 0)
        state = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(state["status"], "completed")
        self.assertEqual(state["runs"][0]["status"], "clock_step")
        self.assertNotIn("error", state["runs"][0])
        first = next(iter(campaign.TARGETS))
        self.assertTrue((self.output / "clock-step" / first / "timeout-abc").is_file())
        self.assertEqual(list((self.output / "artifacts" / first).iterdir()), [])

    def test_real_timeout_and_slow_replay_still_fail(self):
        def run(command, **kwargs):
            if len(command) == 4:
                raise subprocess.TimeoutExpired(command, 60)
            saved = Path(command[-1].split("=", 1)[1]) / "timeout-abc"
            saved.write_bytes(b"input")
            kwargs["stdout"].write(b"Test unit written to " + str(saved).encode() + b"\n"
                                   b"==1== ERROR: libFuzzer: timeout after 18446744073709551615 seconds\n")
            return subprocess.CompletedProcess(command, 70)

        with patch.object(campaign.subprocess, "run", side_effect=run), patch.object(campaign.time, "monotonic", side_effect=[0, 5, 5]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 60, 10), 1)
        state = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(state["runs"][0]["status"], "failed")
        first = next(iter(campaign.TARGETS))
        self.assertTrue((self.output / "artifacts" / first / "timeout-abc").is_file())

    def test_negative_run_time_exit_is_not_recorded_as_a_pass(self):
        def run(command, **kwargs):
            kwargs["stdout"].write(b"#8 DONE cov: 12 ft: 30\nDone 8 runs in -1 second(s)\n")
            return subprocess.CompletedProcess(command, 0)

        with patch.object(campaign.subprocess, "run", side_effect=run), patch.object(campaign.time, "monotonic", side_effect=[0, 60]):
            self.assertEqual(campaign.run_campaign(self.binary, self.output, 60, 10), 0)
        state = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(state["runs"][0]["status"], "clock_step")

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
