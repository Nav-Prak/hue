# tests/test_fuzz_campaign.py
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("campaign", Path(__file__).resolve().parents[1] / "tools/fuzz_campaign.py")
campaign = importlib.util.module_from_spec(spec)
spec.loader.exec_module(campaign)

DONE = b"#123 DONE cov: 12 ft: 30\nstat::number_of_executed_units: 123\n"


def duration_of(command):
    return int(next(a for a in command if a.startswith("-max_total_time=")).split("=")[1])


def artifacts_of(command):
    return Path(command[-1].split("=", 1)[1])


class CampaignTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.binary = self.root / "bin"
        self.binary.mkdir()
        self.output = self.root / "campaign"
        for target in campaign.TARGETS:
            (self.binary / ("fuzz_" + target)).write_bytes(b"fixture, not executable: " + target.encode())
            seeds = self.root / "tools/fuzz/corpus" / target
            seeds.mkdir(parents=True)
            (seeds / "seed").write_bytes(b"seed")
        for mock in (patch.object(campaign, "SOURCE", self.root),
                     patch.object(campaign.subprocess, "check_output", side_effect=lambda args, **kw: "abc\n" if args[1] == "rev-parse" else "")):
            mock.start()
            self.addCleanup(mock.stop)

    @staticmethod
    def success(command, log, timeout, stop):
        log.write_bytes(DONE)
        return 0, float(duration_of(command)), None, False

    def run_with(self, harness, budget=20, slice_seconds=10, **options):
        with patch.object(campaign, "run_harness", side_effect=harness) as run:
            code = campaign.run_campaign(self.binary, self.output, budget, slice_seconds, **options)
        return code, run

    def state(self, target):
        return json.loads((self.output / target / "state.json").read_text())

    def summary(self):
        return json.loads((self.output / "campaign.json").read_text())

    def test_every_target_completes_its_own_budget_in_parallel(self):
        code, run = self.run_with(self.success, jobs=3)
        self.assertEqual(code, 0)
        self.assertEqual(run.call_count, 2 * len(campaign.TARGETS))
        summary = self.summary()
        self.assertEqual(summary["status"], "completed")
        for target in campaign.TARGETS:
            entry = summary["targets"][target]
            self.assertEqual((entry["status"], entry["counted_seconds"], entry["slices"]),
                             ("completed", 20.0, {"passed": 2}))
            self.assertEqual(entry["executions"], 246)
            self.assertEqual(self.state(target)["runs"][0]["statistics"]["last_coverage"], 12)
        # Running the same command again does nothing and stays complete.
        code, run = self.run_with(self.success, jobs=3)
        self.assertEqual((code, run.call_count), (0, 0))

    def test_a_failing_target_does_not_stop_the_others(self):
        def harness(command, log, timeout, stop):
            if "fuzz_gltf" in command[0]:
                log.write_bytes(b"crash\n")
                (artifacts_of(command) / "crash-abc").write_bytes(b"input")
                return 77, 1.0, None, False
            return self.success(command, log, timeout, stop)

        code, _ = self.run_with(harness, jobs=2)
        self.assertEqual(code, 1)
        summary = self.summary()
        self.assertEqual(summary["status"], "failed")
        self.assertEqual(summary["targets"]["gltf"]["status"], "failed")
        self.assertEqual(len(self.state("gltf")["runs"]), 1)
        for target in campaign.TARGETS:
            if target != "gltf":
                self.assertEqual(summary["targets"][target]["status"], "completed")
        # A failed target stays failed on the next run; the others are not redone.
        code, run = self.run_with(self.success)
        self.assertEqual((code, run.call_count), (1, 0))
        # --restart archives it and starts it again, keeping the evidence.
        code, run = self.run_with(self.success, restart=["gltf"])
        self.assertEqual((code, run.call_count), (0, 2))
        self.assertTrue((self.output / "superseded/gltf-1/artifacts/crash-abc").is_file())
        self.assertEqual(self.summary()["superseded"], ["gltf-1"])

    def test_sanitizer_report_with_zero_exit_fails_the_target(self):
        def noisy(command, log, timeout, stop):
            log.write_bytes(b"cgltf.h:2195:11: runtime error: load of misaligned address\n")
            return 0, 10.0, None, False

        code, _ = self.run_with(noisy, targets=["gltf"])
        self.assertEqual(code, 1)
        state = self.state("gltf")
        self.assertEqual(state["status"], "failed")
        self.assertIn("runtime error", state["runs"][0]["sanitizer_reports"][0])
        self.assertEqual(state["runs"][0]["counted_seconds"], 0.0)

    def test_exit_overrun_is_reported_as_a_runner_timeout(self):
        def hang(command, log, timeout, stop):
            self.assertEqual(timeout, 10 + campaign.EXIT_GRACE_SECONDS)
            log.write_bytes(b"")
            raise subprocess.TimeoutExpired(command, timeout)

        code, _ = self.run_with(hang, targets=["wav"])
        self.assertEqual(code, 1)
        self.assertIn("runner timeout", self.state("wav")["runs"][0]["error"])

    def test_only_cpu_time_counts_when_the_system_reports_it(self):
        # A machine that slept: fifteen minutes on the clock, two seconds of work.
        def slept(command, log, timeout, stop):
            log.write_bytes(DONE)
            return 0, 900.0, 2.0, False

        calls = []

        def harness(command, log, timeout, stop):
            calls.append(command)
            return slept(command, log, timeout, stop) if len(calls) == 1 else self.success(command, log, timeout, stop)

        code, _ = self.run_with(harness, budget=10, targets=["scene"])
        self.assertEqual(code, 0)
        runs = self.state("scene")["runs"]
        self.assertEqual([run["counted_seconds"] for run in runs], [2.0, 8.0])
        self.assertEqual(duration_of(calls[1]), 8)

    def test_stop_request_pauses_and_the_same_command_resumes(self):
        stop = threading.Event()

        def harness(command, log, timeout, stop_event):
            log.write_bytes(DONE)
            if len(self.state("tuning")["runs"]) == 2:
                stop.set()
                return 72, 4.0, None, True
            return self.success(command, log, timeout, stop_event)

        code, _ = self.run_with(harness, budget=30, targets=["tuning"], stop=stop)
        self.assertEqual(code, 130)
        state = self.state("tuning")
        self.assertEqual([run["status"] for run in state["runs"]], ["passed", "interrupted"])
        self.assertEqual((state["status"], campaign.counted(state)), ("paused", 10.0))
        generated = self.output / "tuning/corpus/generated"
        generated.write_bytes(b"new seed")
        code, run = self.run_with(self.success, budget=30, targets=["tuning"])
        self.assertEqual((code, run.call_count), (0, 2))
        self.assertEqual(generated.read_bytes(), b"new seed")
        self.assertEqual(self.summary()["targets"]["tuning"]["slices"], {"interrupted": 1, "passed": 3})

    def test_killed_runner_leaves_a_slice_that_is_not_counted_and_resumes(self):
        class Killed(BaseException):
            pass

        def killed(command, log, timeout, stop):
            log.write_bytes(b"#5 NEW cov: 3 ft: 4\n")
            raise Killed()

        with self.assertRaises(Killed):
            self.run_with(killed, targets=["wav"])
        self.assertEqual(self.state("wav")["runs"][0]["status"], "running")
        code, run = self.run_with(self.success, targets=["wav"])
        self.assertEqual((code, run.call_count), (0, 2))
        runs = self.state("wav")["runs"]
        self.assertEqual([run["status"] for run in runs], ["interrupted", "passed", "passed"])
        self.assertEqual(runs[0]["counted_seconds"], 0.0)

    def test_killed_slice_with_a_sanitizer_report_or_stray_artifact_fails(self):
        class Killed(BaseException):
            pass

        def killed(command, log, timeout, stop):
            log.write_bytes(b"x.h:1:1: runtime error: something\n")
            raise Killed()

        with self.assertRaises(Killed):
            self.run_with(killed, targets=["wav"])
        code, run = self.run_with(self.success, targets=["wav"])
        self.assertEqual((code, run.call_count), (1, 0))
        self.assertEqual(self.state("wav")["status"], "failed")

        self.run_with(self.success, targets=["scene"])
        (self.output / "scene/artifacts/crash-late").write_bytes(b"input")
        code, run = self.run_with(self.success, targets=["scene"])
        self.assertEqual((code, run.call_count), (1, 0))
        self.assertIn("no slice accounts for", self.state("scene")["error"])

    def test_rebuilt_harness_restarts_only_its_own_target_and_keeps_its_corpus(self):
        self.run_with(self.success, targets=["gltf", "wav"])
        (self.output / "gltf/corpus/found").write_bytes(b"found input")
        (self.binary / "fuzz_gltf").write_bytes(b"different build")
        code, run = self.run_with(self.success, targets=["gltf", "wav"])
        self.assertEqual((code, run.call_count), (0, 2))
        self.assertTrue(all("fuzz_gltf" in call.args[0][0] for call in run.call_args_list))
        self.assertEqual((self.output / "gltf/corpus/found").read_bytes(), b"found input")
        self.assertTrue((self.output / "superseded/gltf-1/state.json").is_file())
        self.assertEqual(len(self.state("wav")["runs"]), 2)

    def test_negative_duration_timeout_is_replayed_and_does_not_stop_the_target(self):
        calls = []

        def harness(command, log, timeout, stop):
            calls.append(command)
            if len(calls) == 1:
                saved = artifacts_of(command) / "timeout-abc"
                saved.write_bytes(b"input")
                log.write_bytes(b"ALARM: working on the last Unit for -2 seconds\n"
                                b"Test unit written to " + str(saved).encode() + b"\n"
                                b"==1== ERROR: libFuzzer: timeout after 18446744073709551614 seconds\n")
                return 70, 5.0, None, False
            return self.success(command, log, timeout, stop)

        def replay(command, **kwargs):
            kwargs["stdout"].write(b"Executed in 140 ms\n")
            return subprocess.CompletedProcess(command, 0)

        with patch.object(campaign.subprocess, "run", side_effect=replay):
            code, _ = self.run_with(harness, targets=["gltf"])
        self.assertEqual(code, 0)
        state = self.state("gltf")
        self.assertEqual([run["status"] for run in state["runs"]], ["clock_step", "passed", "passed"])
        self.assertNotIn("error", state["runs"][0])
        self.assertEqual(state["runs"][0]["counted_seconds"], 0.0)
        self.assertTrue((self.output / "gltf/clock-step/timeout-abc").is_file())
        self.assertEqual(list((self.output / "gltf/artifacts").iterdir()), [])

    def test_real_timeout_and_slow_replay_still_fail(self):
        def harness(seconds):
            def run(command, log, timeout, stop):
                saved = artifacts_of(command) / "timeout-abc"
                saved.write_bytes(b"input")
                log.write_bytes(b"Test unit written to " + str(saved).encode() + b"\n"
                                b"==1== ERROR: libFuzzer: timeout after " + seconds + b" seconds\n")
                return 70, 5.0, None, False
            return run

        with patch.object(campaign.subprocess, "run", side_effect=subprocess.TimeoutExpired([], 60)):
            code, _ = self.run_with(harness(b"18446744073709551615"), targets=["gltf"])
        self.assertEqual((code, self.state("gltf")["status"]), (1, "failed"))
        self.assertTrue((self.output / "gltf/artifacts/timeout-abc").is_file())

        with patch.object(campaign.subprocess, "run", side_effect=AssertionError("a real timeout must not be replayed away")):
            code, _ = self.run_with(harness(b"11"), targets=["wav"])
        self.assertEqual((code, self.state("wav")["status"]), (1, "failed"))

    def test_negative_run_time_exit_is_not_counted_and_cannot_loop_forever(self):
        def early(command, log, timeout, stop):
            log.write_bytes(b"#8 DONE cov: 12 ft: 30\nDone 8 runs in -1 second(s)\n")
            return 0, 0.4, None, False

        code, run = self.run_with(early, targets=["scene"])
        self.assertEqual(code, 1)
        state = self.state("scene")
        self.assertEqual({r["status"] for r in state["runs"]}, {"clock_step"})
        self.assertEqual(campaign.counted(state), 0.0)
        self.assertIn("too many slices", state["error"])
        self.assertEqual(run.call_count, 4 * 2 + 50)

    def test_refuses_bad_arguments_missing_harness_dirty_source_and_second_runner(self):
        for arguments in ((0, 10), (20, 0), (20, 4000)):
            with self.assertRaises(ValueError):
                campaign.run_campaign(self.binary, self.output, *arguments)
        with self.assertRaisesRegex(ValueError, "unknown target"):
            campaign.run_campaign(self.binary, self.output, 20, 10, targets=["nope"])
        with patch.object(campaign.subprocess, "check_output", return_value="dirty"):
            with self.assertRaisesRegex(ValueError, "commit source"):
                campaign.run_campaign(self.binary, self.output, 20, 10)
        self.output.mkdir(exist_ok=True)
        held = campaign.acquire_lock(self.output)
        try:
            with self.assertRaisesRegex(ValueError, "another runner"):
                campaign.run_campaign(self.binary, self.output, 20, 10)
        finally:
            held.close()
        (self.binary / "fuzz_gltf").unlink()
        with self.assertRaisesRegex(ValueError, "missing instrumented"):
            campaign.run_campaign(self.binary, self.output, 20, 10)

    def test_run_harness_runs_stops_and_times_out_real_processes(self):
        import sys
        log = self.root / "one.log"
        code, elapsed, _, stopped = campaign.run_harness([sys.executable, "-c", "print('hello')"], log, 30, threading.Event())
        self.assertEqual((code, stopped, log.read_bytes().strip()), (0, False, b"hello"))
        stop = threading.Event()
        stop.set()
        code, elapsed, _, stopped = campaign.run_harness([sys.executable, "-c", "import time; time.sleep(60)"],
                                                         self.root / "two.log", 30, stop)
        self.assertTrue(stopped)
        self.assertLess(elapsed, 45)
        with self.assertRaises(subprocess.TimeoutExpired):
            campaign.run_harness([sys.executable, "-c", "import time; time.sleep(60)"],
                                 self.root / "three.log", 1, threading.Event())


if __name__ == "__main__":
    unittest.main()
