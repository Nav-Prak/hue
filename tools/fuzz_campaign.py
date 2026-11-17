# tools/fuzz_campaign.py
"""Per-target, parallel, resumable libFuzzer campaign.

Each fuzz target has its own directory, budget and state, so a target that has
consumed its budget is complete whatever happens to the others. Running the
same command again continues whatever is unfinished: after Ctrl+C, a killed
process, a restart of the machine or a sleep. Nothing is launched by default.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import threading
import time

TARGETS = {"gltf": 1048576, "animation_events": 65537, "scene": 16385,
           "tuning": 4097, "shader_contract": 131073, "wav": 65537}
SOURCE = Path(__file__).resolve().parents[1]
SCHEMA = 2

# libFuzzer prints its statistics and runs the leak check after the time
# budget; a large corpus needs more than a few seconds for that.
EXIT_GRACE_SECONDS = 300
SANITIZER_MARKERS = (b"runtime error:", b"ERROR: AddressSanitizer", b"ERROR: LeakSanitizer",
                     b"SUMMARY: UndefinedBehaviorSanitizer")

# libFuzzer times units and its total budget with the wall clock. When that
# clock is stepped backwards (a virtual machine resynchronising with its
# host), it measures a negative duration: either it reports a timeout "after
# 18446744073709551615 seconds" (minus one as an unsigned 64-bit number; a
# two-second step gives ...614) or it believes its budget is spent and exits
# at once. Neither says anything about the input.
TIMEOUT_SECONDS = re.compile(rb"libFuzzer: timeout after (\d+) seconds")
NEGATIVE_DONE = re.compile(rb"Done \d+ runs in -\d+ second")
WRITTEN = re.compile(rb"Test unit written to (\S+)")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


# Worker threads rewrite their state files while the summary reads them; on
# Windows a file cannot be replaced while another thread has it open.
FILES = threading.RLock()


def save_json(path, value):
    with FILES:
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
        temporary.replace(path)


def load_json(path):
    with FILES:
        return json.loads(path.read_text(encoding="utf-8"))


def log_tail(log, size=1 << 20):
    with log.open("rb") as stream:
        stream.seek(max(0, log.stat().st_size - size))
        return stream.read()


def statistics(log):
    # libFuzzer's final statistics are near EOF; do not load hours of output.
    tail = log_tail(log, 65536).decode("utf-8", errors="replace")
    result = {}
    for key, value in re.findall(r"stat::(\w+):\s*(\d+)", tail):
        result[key] = int(value)
    coverage = re.findall(r"cov: (\d+) ft: (\d+)", tail)
    if coverage:
        result["last_coverage"], result["last_features"] = map(int, coverage[-1])
    return result


def sanitizer_reports(log, limit=8):
    # UBSan reports do not stop the fuzzer by default and leave no artifact,
    # so a zero exit code alone does not mean a clean slice.
    found = []
    with log.open("rb") as stream:
        for line in stream:
            if any(marker in line for marker in SANITIZER_MARKERS):
                found.append(line.decode("utf-8", errors="replace").strip()[:300])
                if len(found) == limit:
                    break
    return found


def cpu_seconds(pid):
    """CPU time the process has used, or None where /proc is not available."""
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
    except (OSError, ValueError, IndexError, AttributeError):
        return None


def run_harness(command, log, timeout, stop):
    """Runs one slice. Returns (exit code, elapsed seconds, CPU seconds or
    None, stopped on request). Raises subprocess.TimeoutExpired on overrun."""
    with log.open("xb") as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=os.name == "posix")
        start = time.monotonic()
        cpu = None
        asked_at = None
        while True:
            try:
                code = process.wait(timeout=1.0)
                break
            except subprocess.TimeoutExpired:
                pass
            cpu = cpu_seconds(process.pid) or cpu
            now = time.monotonic()
            if stop.is_set() and asked_at is None:
                asked_at = now
                process.send_signal(signal.SIGINT if os.name == "posix" else signal.SIGTERM)
            elif asked_at is not None and now - asked_at > 30:
                process.kill()
            elif asked_at is None and now - start > timeout:
                process.kill()
                process.wait()
                raise subprocess.TimeoutExpired(command, timeout)
        return code, time.monotonic() - start, cpu, asked_at is not None


def clock_step(root, binary, log, record):
    """Returns a description when a slice ended only because the wall clock
    went backwards, after proving the saved input is not slow; else None."""
    if record.get("sanitizer_reports"):
        return None
    tail = log_tail(log)
    if record["status"] == "passed":
        return "libFuzzer measured a negative run time and exited early" if NEGATIVE_DONE.search(tail) else None
    reported = TIMEOUT_SECONDS.findall(tail)
    if record.get("returncode") in (None, 0) or len(reported) != 1 or int(reported[0]) < 1 << 63:
        return None
    written = WRITTEN.findall(tail)
    directory = (root / "artifacts").resolve()
    if len(written) != 1:
        return None
    artifact = Path(os.fsdecode(written[0])).resolve()
    if artifact.parent != directory or not artifact.name.startswith("timeout-") or not artifact.is_file():
        return None
    if sorted(directory.iterdir()) != [artifact]:
        return None
    replay = log.with_suffix(".replay.log")
    start = time.monotonic()
    try:
        with replay.open("xb") as stream:
            completed = subprocess.run([binary, "-timeout=10", "-rss_limit_mb=2048", str(artifact)],
                                       stdout=stream, stderr=subprocess.STDOUT, timeout=60, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    seconds = time.monotonic() - start
    if completed.returncode != 0 or seconds >= 10 or sanitizer_reports(replay):
        return None
    kept = root / "clock-step"
    kept.mkdir(exist_ok=True)
    artifact.replace(kept / artifact.name)
    record["replay_seconds"] = seconds
    record["replay_log"] = str(replay.relative_to(root))
    return ("libFuzzer reported a timeout with a negative duration; the saved input replayed "
            f"cleanly in {seconds:.1f}s and was moved to clock-step/")


def acquire_lock(output):
    """One runner per output directory. The operating system drops the lock
    when the process dies, so a killed runner never blocks the next one."""
    handle = (output / "campaign.lock").open("a+b")
    try:
        if os.name == "posix":
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        else:
            import msvcrt
            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        handle.close()
        raise ValueError("another runner is using this output directory") from None
    return handle


def counted(state):
    return sum(run.get("counted_seconds", 0.0) for run in state["runs"])


def find_binary(binary_dir, target):
    path = binary_dir / ("fuzz_" + target)
    if not path.is_file():
        path = path.with_suffix(".exe")
    if not path.is_file():
        raise ValueError(f"missing instrumented harness: {target}")
    return {"path": str(path.resolve()), "sha256": digest(path)}


def create_target(root, target, binary, budget, seeds_from=None):
    shutil.copytree(SOURCE / "tools/fuzz/corpus" / target, root / "corpus")
    if seeds_from is not None and seeds_from.is_dir():
        for path in seeds_from.iterdir():
            if path.is_file() and not (root / "corpus" / path.name).exists():
                shutil.copy2(path, root / "corpus" / path.name)
    (root / "artifacts").mkdir()
    (root / "logs").mkdir()
    state = {"schema": SCHEMA, "target": target, "max_len": TARGETS[target], "binary": binary,
             "budget_seconds": budget, "status": "paused", "runs": []}
    save_json(root / "state.json", state)
    return state


def open_target(output, target, binary, budget, restart):
    """Creates or reopens one target's directory and returns (root, state, note)."""
    root = output / target
    if not root.exists():
        return root, create_target(root, target, binary, budget), "new"
    state = load_json(root / "state.json")
    if state.get("schema") != SCHEMA or state.get("target") != target:
        raise ValueError(f"{target}: not a campaign directory of this runner")
    changed = state["binary"]["sha256"] != binary["sha256"] or state["max_len"] != TARGETS[target]
    if changed or restart:
        # Results belong to the harness that produced them. A rebuilt harness
        # starts again; the old directory is kept, and its corpus is reused
        # as seeds because a corpus is input, not evidence.
        archive = output / "superseded"
        archive.mkdir(exist_ok=True)
        number = 1
        while (archive / f"{target}-{number}").exists():
            number += 1
        root.replace(archive / f"{target}-{number}")
        state = create_target(root, target, binary, budget, archive / f"{target}-{number}" / "corpus")
        return root, state, f"restarted; previous state kept in superseded/{target}-{number}"
    note = "resumed"
    state["binary"]["path"] = binary["path"]
    state["budget_seconds"] = budget
    if state["runs"] and state["runs"][-1]["status"] == "running":
        # The runner was killed during this slice. Its time is not counted.
        last = state["runs"][-1]
        last["status"] = "interrupted"
        last["note"] = "runner stopped without finishing this slice; time not counted"
        log = root / last["log"]
        reports = sanitizer_reports(log) if log.is_file() else []
        if reports:
            last["sanitizer_reports"] = reports
            last["status"] = "failed"
            last["error"] = "sanitizer report in the log of an interrupted slice"
            state["status"] = "failed"
        note = "resumed after an unfinished slice"
    if state["status"] != "failed" and any((root / "artifacts").iterdir()):
        state["status"] = "failed"
        state["error"] = "artifacts/ holds an input no slice accounts for"
    if state["status"] != "failed":
        state["status"] = "completed" if counted(state) >= budget else "paused"
        state.pop("error", None)
    save_json(root / "state.json", state)
    return root, state, note


def run_target(root, state, slice_seconds, revision, stop, announce):
    target = state["target"]
    budget = state["budget_seconds"]
    binary = state["binary"]["path"]
    slice_limit = 4 * math.ceil(budget / slice_seconds) + 50
    while state["status"] not in ("failed", "completed") and not stop.is_set():
        if len(state["runs"]) >= slice_limit:
            state["status"] = "failed"
            state["error"] = "too many slices without consuming the budget; inspect the logs"
            break
        index = len(state["runs"])
        duration = max(1, min(slice_seconds, math.ceil(budget - counted(state))))
        log = root / "logs" / f"{index:05d}.log"
        command = [binary, str(root / "corpus"), f"-max_total_time={duration}",
                   f"-max_len={TARGETS[target]}", "-timeout=10", "-rss_limit_mb=2048",
                   "-print_final_stats=1", "-verbosity=1", f"-artifact_prefix={root / 'artifacts'}/"]
        record = {"log": str(log.relative_to(root)), "source_revision": revision,
                  "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                  "requested_seconds": duration, "elapsed_seconds": 0.0, "counted_seconds": 0.0,
                  "status": "running"}
        state["runs"].append(record)
        state["status"] = "running"
        save_json(root / "state.json", state)
        elapsed, cpu, stopped = 0.0, None, False
        try:
            code, elapsed, cpu, stopped = run_harness(command, log, duration + EXIT_GRACE_SECONDS, stop)
            record["returncode"] = code
            record["status"] = "passed" if code == 0 else "failed"
        except subprocess.TimeoutExpired:
            elapsed = duration + EXIT_GRACE_SECONDS
            record["status"] = "failed"
            record["error"] = (f"harness did not exit within {EXIT_GRACE_SECONDS}s of its "
                               "time budget; this is a runner timeout, not a saved finding")
        except OSError as error:
            record["status"] = "failed"
            record["error"] = str(error)
        record["elapsed_seconds"] = elapsed
        if cpu is not None:
            record["cpu_seconds"] = cpu
        reports = sanitizer_reports(log) if log.exists() else []
        if reports:
            record["sanitizer_reports"] = reports
            record["status"] = "failed"
            record.setdefault("error", "sanitizer report in the log")
        elif stopped:
            # Stopped on request: not a pass, not a failure, time not counted.
            record["status"] = "interrupted"
            record.pop("error", None)
        elif log.exists():
            stepped = clock_step(root, binary, log, record)
            if stepped:
                # Not a pass and not a finding: listed, not counted.
                record["status"] = "clock_step"
                record["clock_step"] = stepped
                record.pop("error", None)
        record["statistics"] = statistics(log) if log.exists() else {}
        if record["status"] == "passed":
            # Only time the harness demonstrably spent running counts. CPU
            # time, where the system reports it, is immune to a sleeping
            # machine and to clock changes.
            record["counted_seconds"] = max(0.0, min(duration, elapsed, cpu if cpu is not None else elapsed))
        if record["status"] == "failed":
            state["status"] = "failed"
            state["error"] = record.get("error", f"harness exited with code {record.get('returncode')}")
        elif counted(state) >= budget:
            state["status"] = "completed"
        else:
            state["status"] = "paused"
        save_json(root / "state.json", state)
        announce(f"{target}: slice {index} {record['status']}, {elapsed:.1f}s; "
                 f"{counted(state) / 3600:.2f} of {budget / 3600:.2f} h; {state['status']}")
    save_json(root / "state.json", state)
    return state


def summarize(output):
    targets = {}
    for target in TARGETS:
        path = output / target / "state.json"
        if not path.is_file():
            continue
        state = load_json(path)
        slices = {}
        for run in state["runs"]:
            slices[run["status"]] = slices.get(run["status"], 0) + 1
        entry = {"status": state["status"], "budget_seconds": state["budget_seconds"],
                 "counted_seconds": round(counted(state), 1), "slices": slices,
                 "executions": sum(run.get("statistics", {}).get("number_of_executed_units", 0)
                                   for run in state["runs"] if run["status"] == "passed"),
                 "binary_sha256": state["binary"]["sha256"],
                 "source_revisions": sorted({run["source_revision"] for run in state["runs"]})}
        if "error" in state:
            entry["error"] = state["error"]
        targets[target] = entry
    statuses = {entry["status"] for entry in targets.values()}
    if "failed" in statuses:
        overall = "failed"
    elif statuses == {"completed"} and len(targets) == len(TARGETS):
        overall = "completed"
    else:
        overall = "incomplete"
    superseded = sorted(p.name for p in (output / "superseded").iterdir()) if (output / "superseded").is_dir() else []
    return {"schema": SCHEMA, "status": overall, "targets": targets, "superseded": superseded}


def print_summary(summary):
    print(f"campaign: {summary['status']}")
    for target, entry in summary["targets"].items():
        slices = ", ".join(f"{count} {status}" for status, count in sorted(entry["slices"].items())) or "no slices"
        print(f"  {target:17} {entry['status']:10} {entry['counted_seconds'] / 3600:5.2f} of "
              f"{entry['budget_seconds'] / 3600:.2f} h  {entry['executions']:>13,} execs  ({slices})"
              + (f"  {entry['error']}" if "error" in entry else ""))
    if summary["superseded"]:
        print("  superseded: " + ", ".join(summary["superseded"]))


def run_campaign(binary_dir, output, seconds_per_target, slice_seconds, jobs=1, targets=None,
                 restart=(), stop=None):
    if not 1 <= slice_seconds <= 3600:
        raise ValueError("slice duration must be 1..3600 seconds")
    if not 1 <= seconds_per_target <= 172800:
        raise ValueError("budget must be 1..172800 seconds per target")
    if not 1 <= jobs <= 64:
        raise ValueError("jobs must be 1..64")
    targets = list(targets or TARGETS)
    unknown = [name for name in list(targets) + list(restart) if name not in TARGETS]
    if unknown:
        raise ValueError("unknown target: " + ", ".join(unknown))
    binary_dir, output = binary_dir.resolve(), output.resolve()
    binaries = {target: find_binary(binary_dir, target) for target in targets}
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=SOURCE, text=True).strip():
        raise ValueError("commit source changes before recording a campaign")
    stop = stop or threading.Event()
    output.mkdir(parents=True, exist_ok=True)
    lock = acquire_lock(output)
    console = threading.Lock()

    def announce(message):
        with console:
            print(message, flush=True)
            save_json(output / "campaign.json", summarize(output))

    try:
        opened = []
        for target in targets:
            root, state, note = open_target(output, target, binaries[target], seconds_per_target,
                                            target in restart)
            announce(f"{target}: {note}; {state['status']}, {counted(state) / 3600:.2f} of "
                     f"{seconds_per_target / 3600:.2f} h")
            opened.append((root, state))
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(run_target, root, state, slice_seconds, revision, stop, announce)
                       for root, state in opened]
            pending = set(futures)
            try:
                while pending:
                    _, pending = concurrent.futures.wait(pending, timeout=0.5)
            except KeyboardInterrupt:
                stop.set()
                concurrent.futures.wait(pending)
            for future in futures:
                future.result()
        summary = summarize(output)
        save_json(output / "campaign.json", summary)
        print_summary(summary)
        # The exit code speaks for the targets this run was asked to do; the
        # summary's own status is "completed" only when all six are.
        statuses = {summary["targets"][target]["status"] for target in targets}
        if "failed" in statuses:
            return 1
        return 0 if statuses == {"completed"} else 130
    finally:
        lock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary-dir", type=Path)
    parser.add_argument("--seconds-per-target", type=int, default=14400,
                        help="fuzzing time each target must accumulate (default four hours)")
    parser.add_argument("--slice-seconds", type=int, default=900)
    parser.add_argument("--jobs", type=int, default=max(1, min(len(TARGETS), (os.cpu_count() or 2) // 2)),
                        help="targets fuzzed at the same time")
    parser.add_argument("--targets", nargs="+", metavar="TARGET", help="default: all")
    parser.add_argument("--restart", nargs="+", default=[], metavar="TARGET",
                        help="archive these targets' state and start them again")
    parser.add_argument("--status", action="store_true", help="print the recorded state and exit")
    args = parser.parse_args()
    if args.status:
        print_summary(summarize(args.output.resolve()))
        return 0
    if args.binary_dir is None:
        parser.error("--binary-dir is required to run")
    stop = threading.Event()
    for number in (signal.SIGINT, signal.SIGTERM):
        signal.signal(number, lambda *_: stop.set())
    try:
        return run_campaign(args.binary_dir, args.output, args.seconds_per_target, args.slice_seconds,
                            args.jobs, args.targets, args.restart, stop)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"campaign refused: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
