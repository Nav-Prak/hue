"""Bounded, standard-library-only aggregation of Hue schema-1 fight JSONL."""
import argparse
import collections
import json
import math
from pathlib import Path
import statistics

FIELDS = {"schema", "source", "outcome", "seed", "tuning_revision", "ticks",
          "hitstop_ticks", "simulation_seconds", "active_seconds", "wall_seconds",
          "hits_landed", "hits_taken", "staggers_inflicted", "staggers_received", "dodges"}
COUNTS = ("ticks", "hitstop_ticks", "hits_landed", "hits_taken", "staggers_inflicted",
          "staggers_received", "dodges", "seed", "tuning_revision")
TIMES = ("simulation_seconds", "active_seconds", "wall_seconds")

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate field: {key}")
        result[key] = value
    return result

def validate(row):
    if not isinstance(row, dict) or set(row) != FIELDS or type(row["schema"]) is not int or row["schema"] != 1:
        raise ValueError("expected exact schema-1 fight object")
    if row["source"] not in ("interactive", "automated", "injected") or row["outcome"] not in ("victory", "defeat"):
        raise ValueError("invalid source/outcome")
    for key in COUNTS:
        if type(row[key]) is not int or not 0 <= row[key] <= 2**53 - 1:
            raise ValueError(f"invalid count: {key}")
    if row["ticks"] == 0 or row["tuning_revision"] == 0 or row["hitstop_ticks"] > row["ticks"]:
        raise ValueError("invalid tick/revision relationship")
    if any(row[key] > row["ticks"] for key in ("hits_landed", "hits_taken", "dodges")):
        raise ValueError("event count exceeds ticks")
    if row["staggers_inflicted"] > row["hits_landed"] or row["staggers_received"] > row["hits_taken"]:
        raise ValueError("staggers exceed contacts")
    for key in TIMES:
        if type(row[key]) not in (int, float) or not math.isfinite(row[key]) or row[key] < 0:
            raise ValueError(f"invalid duration: {key}")
    if abs(row["simulation_seconds"] - row["ticks"] / 60) > 0.00001 or abs(row["active_seconds"] - (row["ticks"] - row["hitstop_ticks"]) / 60) > 0.00001:
        raise ValueError("timing does not match 60 Hz counters")
    return row

def load(path):
    rows = []
    total = 0
    with Path(path).open("rb") as stream:
        while line := stream.readline(4097):
            total += len(line)
            if len(line) > 4096 or total > 16 * 1024 * 1024 or len(rows) >= 10000:
                raise ValueError("report input exceeds line/file/record limit")
            if not line.endswith(b"\n"):
                raise ValueError("incomplete JSONL record; preserve file and investigate failed write")
            try:
                rows.append(validate(json.loads(line, object_pairs_hook=unique_object)))
            except (ValueError, TypeError, RecursionError) as exc:
                raise ValueError(f"record {len(rows)+1}: {exc}") from exc
    return rows

def summarize(rows, source="interactive"):
    groups = collections.defaultdict(list)
    for row in rows:
        if row["source"] == source:
            groups[(row["tuning_revision"], row["seed"])].append(row)
    summary = {"schema": 1, "selected_source": source, "selected": sum(map(len, groups.values())),
               "excluded": sum(row["source"] != source for row in rows), "groups": []}
    for (revision, seed), group in sorted(groups.items()):
        summary["groups"].append({"tuning_revision": revision, "seed": seed, "fights": len(group),
            "victories": sum(r["outcome"] == "victory" for r in group),
            "defeats": sum(r["outcome"] == "defeat" for r in group),
            "mean": {key: round(statistics.mean(r[key] for r in group), 6) for key in TIMES + COUNTS[:7]}})
    return summary

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--source", choices=("interactive", "automated", "injected"), default="interactive")
    parser.add_argument("--minimum", type=int, default=10)
    args = parser.parse_args()
    if not 1 <= args.minimum <= 10000:
        parser.error("minimum must be 1..10000")
    try:
        result = summarize(load(args.path), args.source)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"report rejected: {exc}\n")
    result["sample_requirement_met"] = result["selected"] >= args.minimum
    result["acceptance_note"] = "Counts are not playtest acceptance. Injected/automated data is not human balance evidence."
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result["sample_requirement_met"] else 2

if __name__ == "__main__":
    raise SystemExit(main())
