# Local sustained fuzz campaign

Prepared October 14, 2026 as a runner only. On November 11, 2026 the owner
approved a run; see "Run log" at the end for what was started and what has
and has not been observed. Short CI runs and runner tests are not the
24–48-hour acceptance evidence.

Use a Linux worker with the normal Clang/Ninja build prerequisites (on Ubuntu
24.04: `clang clang-tools cmake ninja-build xorg-dev git`; `clang-tools`
provides the `clang-scan-deps` the build needs), Python 3.11+
and enough free space for generated corpus/logs. From a clean committed checkout:

```sh
cmake --preset linux-clang-sanitized -DHUE_FUZZ=ON -DHUE_TRACY_ENABLE=OFF
cmake --build --preset linux-sanitized --target fuzz_gltf fuzz_animation_events fuzz_scene fuzz_tuning fuzz_shader_contract fuzz_wav
python3 tools/fuzz_campaign.py --binary-dir build/linux-clang-sanitized/tools/fuzz --output build/campaign-24h --seconds 86400 --slice-seconds 900
```

One worker rotates through all six targets in
15-minute slices. The 24-hour budget is cumulative worker elapsed time across
the suite, **not 24 hours per target**. Startup/shutdown overhead is included;
per-target actual time, requested time, return status and available libFuzzer
execution/coverage counters are stored in `campaign.json`. Timeout cleanup can
exceed the nominal budget slightly. Counters are libFuzzer coverage/features,
not source-line coverage percentages; do not add coverage counts across runs.

The runner copies seeds into its output directory and never grows the checked-in
corpus. Each harness has a 10-second input timeout, 2 GiB RSS limit and a bounded
maximum input size. Input size caps are policy limits, not proof that every
valid large asset has been exercised. Retain `artifacts/`, `corpus/`, `logs/`
and `campaign.json` together. Runtime log output and corpus growth still consume
disk space; monitor free space during any long run.

Ctrl+C pauses the current harness; its partial time is recorded, not reported
as a passed slice. Resume with the identical command plus `--resume`. Source
revision, binary paths/hashes and total budget must match. One lock file prevents
two workers from sharing an output. A hard kill leaves a running record/lock:
inspect the process and artifacts and start a fresh output rather than deleting
evidence or pretending the interrupted time was measured.

A sanitizer finding, crash, timeout or nonzero exit stops the campaign. Triage
the saved input, add a regression, rebuild, and start a fresh campaign directory;
failed campaigns cannot be resumed into a misleading clean result. `completed`
means the requested budget was consumed, not that security is proven. Review
all slice statuses, target distribution, counters and any incomplete statistics
before recording acceptance. Resume is intended after graceful pauses, not to
merge different engine revisions.

CI exercises the same runner for only 15 seconds and retains its manifest/logs,
in addition to the six existing 60-second harness runs. This verifies plumbing,
not sustained coverage or human playability.

## Run log

**November 11, 2026, development PC, WSL2 Ubuntu 24.04.3, Clang 18.1.3, 16
logical cores, one worker.** Source revision `4aef9cf` from a clean clone at
`~/hue-campaign`; all six targets built with ASan and UBSan.

- One-minute check (`--seconds 60 --slice-seconds 10`, output
  `~/hue-campaign-smoke`): completed, every slice passed, no artifacts.
  Executions per target in about ten seconds each: glTF 12,797; animation
  events 1,581,885; scene 1,095,427; tuning 1,357,981; shader contract 4,140;
  WAV 291,697 (six seconds). This checks the plumbing and is far too short to
  say anything about the parsers.
- 24-hour campaign (`--seconds 86400 --slice-seconds 900`, output
  `~/hue-campaign-24h`, console log `~/hue-campaign-24h.log`): **started, not
  finished.** No result is claimed here. The budget is shared: each target
  gets about four hours, not twenty-four. The run pauses if the PC sleeps and
  dies if WSL or Windows restarts; a hard stop leaves the lock file and a
  `running` record and needs a fresh output directory, as described above.

Check progress from Windows:

```sh
wsl -d Ubuntu -- bash -lc "tail -n 5 ~/hue-campaign-24h.log; ls ~/hue-campaign-24h/artifacts/*"
```

When it ends, `campaign.json` holds the status, per-slice times and libFuzzer
counters. Record the outcome here, including any finding, before treating
Week 18 as having evidence.
