# Week 15 fight telemetry

Engineering checkpoint, October 2, 2026; human balance acceptance is pending.
Two existing attacks remain the combat scope. No third attack or phase was added.

## Capture and report

Run `hue.exe --telemetry C:/path/to/fights.jsonl` to append one versioned record
per completed fight. The parent directory must exist and be writable. Telemetry
is opt-in, local only, and contains no player identity. Interrupted/restarted
fights are not completed samples. A write failure logs a warning and disables
recording for the session without stopping gameplay; preserve partial files for
inspection rather than silently treating them as completed records.

Each schema-1 JSON line records outcome, seed, author-maintained tuning revision,
hits landed/taken, staggers inflicted/received, dodges, fixed ticks, hitstop ticks,
simulation seconds including hitstop, active seconds excluding it, and monotonic
wall seconds excluding ready/result screens. Wall time is not derived from the
clamped simulation delta. A stalled game can have more wall than simulated time;
a headless automated fixture can simulate faster than real time.

```sh
python tools/fight_report.py path/to/fights.jsonl
```

The tool requires ten eligible records by default (exit 2 otherwise). It groups
by tuning revision and seed; do not reuse a revision for different settings.
Its default source is `interactive`, a description of the input path, **not** a
certification that an external human played. Use `--source automated` for the
fixture, or `--source injected` for lifecycle smoke. Those sources are excluded
from the default report. Preserve settings and build identity beside a study.

The report reader rejects unknown/duplicate fields, invalid counters/times,
incomplete final lines, more than 10,000 records, over 4 KiB per line or over
16 MiB per file. It uses Python's standard JSON decoder, never evaluates input,
loads assets or starts an engine process. The runtime formatter has fixed storage.

## Recorded engineering evidence

Ten actual contact-damage fights in the imported-rig/Jolt test fixture (seed 2026,
tuning revision 1): five repeated-heavy victories and five passive defeats.
No lethal damage injection is used in this fixture. The simplified flat arena,
fixed seed and two repetitive policies are intentional regression coverage;
these are not ten independent human observations.

| Policy (five repeats each) | Result | Simulated ticks | Hits landed / taken |
|---|---|---|---|
| Repeated heavy, locked, stationary | Victory | 293 | 4 / 1 |
| Passive, locked, stationary | Defeat | 684 | 0 / 6 |

Aggregate mean: 8.141666 simulated seconds, 7.791666 active seconds, 21 hitstop
ticks, 2 hits landed, 3.5 taken. Local raw output is
`build/week15-ten-fights.jsonl`; regenerate by setting `HUE_TEST_TELEMETRY` to a
fresh destination and running the `encounter: ten automated*` test. Do not append
reruns into the same study accidentally.

The real rendered smoke also verifies exactly twenty emitted records, ten of
each injected outcome, with automatic exclusion from the default balance report.
AI tests cover two minutes without either attack starving and a heavy-whiff
recovery experiment: 0.65 to 0.85 seconds adds approximately twelve non-attacking
ticks after the clip ends. This demonstrates the tuning mechanism, **not** a
verified feel improvement; the shipped default was deliberately left unchanged.

Windows Release full CTest: 155/155, no skips, 99.16 seconds. The report tool has
three passing Python tests; C++ tests cover one-shot emission, abandoned fights,
clock separation, reset, invalid data and bounded output. Windows ASan full CTest:
155/155, no skips, 145.41 seconds.

## Acceptance still pending

Collect ten interactive fights, compare a specific revision change with the same
scenario and retain tester observations of readability, punishability and fairness.
Do not infer a verified tuning improvement from a larger recovery number alone.
External playtests, physical gamepad, Linux CI and later release gates remain open.
