# tools/fuzz_campaign.py
"""One-worker, resumable libFuzzer campaign. No jobs are launched by default."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import time

TARGETS = {"gltf": 1048576, "animation_events": 65537, "scene": 16385,
           "tuning": 4097, "shader_contract": 131073, "wav": 65537}
SOURCE = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save(root, state):
    temporary = root / "campaign.json.tmp"
    temporary.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    temporary.replace(root / "campaign.json")


def statistics(log):
    # libFuzzer's final statistics are near EOF; do not load hours of output.
    with log.open("rb") as stream:
        stream.seek(max(0, log.stat().st_size - 65536))
        tail = stream.read().decode("utf-8", errors="replace")
    result = {}
    for key, value in re.findall(r"stat::(\w+):\s*(\d+)", tail):
        result[key] = int(value)
    coverage = re.findall(r"cov: (\d+) ft: (\d+)", tail)
    if coverage:
        result["last_coverage"], result["last_features"] = map(int, coverage[-1])
    return result


def initialize(binary_dir, output, seconds, resume):
    if not len(TARGETS) <= seconds <= 172800:
        raise ValueError("budget must be 6..172800 seconds across all targets")
    binaries = {}
    for target in TARGETS:
        path = binary_dir / ("fuzz_" + target)
        if not path.is_file():
            path = path.with_suffix(".exe")
        if not path.is_file():
            raise ValueError(f"missing instrumented harness: {target}")
        binaries[target] = {"path": str(path.resolve()), "sha256": digest(path)}
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=SOURCE, text=True).strip())
    if dirty:
        raise ValueError("commit source changes before recording a campaign")
    identity = {"source_revision": revision, "binaries": binaries, "budget_seconds": seconds}
    if resume:
        state = json.loads((output / "campaign.json").read_text(encoding="utf-8"))
        if state.get("schema") != 1 or any(state.get(k) != v for k, v in identity.items()):
            raise ValueError("resume requires the same source, harness bytes/paths, and budget")
        if state.get("status") not in ("paused", "completed"):
            raise ValueError("campaign is failed or interrupted; triage it and use a fresh directory")
        return state
    if output.exists():
        raise ValueError("choose a fresh output directory, or explicitly --resume")
    output.mkdir(parents=True)
    for target in TARGETS:
        shutil.copytree(SOURCE / "tools/fuzz/corpus" / target, output / "corpus" / target)
        (output / "artifacts" / target).mkdir(parents=True)
    (output / "logs").mkdir()
    state = {"schema": 1, **identity, "status": "paused", "worker_count": 1, "runs": []}
    save(output, state)
    return state


def run_campaign(binary_dir, output, seconds, slice_seconds, resume=False):
    if not 1 <= slice_seconds <= 3600:
        raise ValueError("slice duration must be 1..3600 seconds")
    state = initialize(binary_dir.resolve(), output.resolve(), seconds, resume)
    output = output.resolve()
    # Exclusive lock prevents concurrent resumes. A hard kill deliberately leaves
    # this lock and a running record for manual inspection, not silent replay.
    lock = output / "campaign.lock"
    with lock.open("x"):
        pass
    try:
        targets = list(TARGETS)
        elapsed = sum(run["elapsed_seconds"] for run in state["runs"])
        while elapsed < seconds:
            index = len(state["runs"])
            target = targets[index % len(targets)]
            duration = max(1, min(slice_seconds, seconds // len(targets), math.ceil(seconds - elapsed)))
            log = output / "logs" / f"{index:05d}-{target}.log"
            command = [state["binaries"][target]["path"], str(output / "corpus" / target),
                       f"-max_total_time={duration}", f"-max_len={TARGETS[target]}",
                       "-timeout=10", "-rss_limit_mb=2048", "-print_final_stats=1",
                       "-verbosity=1", f"-artifact_prefix={output / 'artifacts' / target}/"]
            record = {"target": target, "command": command, "log": str(log.relative_to(output)),
                      "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                      "requested_seconds": duration, "elapsed_seconds": 0.0, "status": "running"}
            state["runs"].append(record)
            state["status"] = "running"
            save(output, state)
            start = time.monotonic()
            try:
                with log.open("xb") as stream:
                    completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                               timeout=duration + 30, check=False)
                record["returncode"] = completed.returncode
                record["status"] = "passed" if completed.returncode == 0 else "failed"
            except KeyboardInterrupt:
                record["status"] = "interrupted"
            except (OSError, subprocess.TimeoutExpired) as error:
                record["status"] = "failed"
                record["error"] = str(error)
            record["elapsed_seconds"] = time.monotonic() - start
            record["statistics"] = statistics(log) if log.exists() else {}
            elapsed += record["elapsed_seconds"]
            state["status"] = "failed" if record["status"] == "failed" else "paused"
            save(output, state)
            print(f"{target}: {record['status']}, {record['elapsed_seconds']:.1f}s", flush=True)
            if record["status"] != "passed":
                return 130 if record["status"] == "interrupted" else 1
        state["status"] = "completed"
        save(output, state)
        return 0
    finally:
        lock.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, required=True, help="total worker elapsed budget, not per target")
    parser.add_argument("--slice-seconds", type=int, default=900)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    try:
        return run_campaign(args.binary_dir, args.output, args.seconds, args.slice_seconds, args.resume)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"campaign refused: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
