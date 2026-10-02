"""Run existing lifecycle smoke and verify its actual emitted telemetry."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_fight_report import report

with tempfile.TemporaryDirectory(prefix="hue-telemetry-") as directory:
    path = Path(directory) / "fights.jsonl"
    result = subprocess.run([sys.argv[1], "--encounter-smoke", "--telemetry", str(path)], timeout=160)
    if result.returncode:
        raise SystemExit(result.returncode)
    rows = report.load(path)
    assert len(rows) == 20, len(rows)
    assert all(r["source"] == "injected" for r in rows)
    assert sum(r["outcome"] == "victory" for r in rows) == 10
    assert sum(r["outcome"] == "defeat" for r in rows) == 10
    assert all(0 < r["wall_seconds"] < 15 for r in rows)
    assert report.summarize(rows)["selected"] == 0
    assert report.summarize(rows, "injected")["selected"] == 20
    print("telemetry PASS: twenty single emissions, both outcomes, injected source excluded by default")
