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

A sanitizer finding, crash, timeout or nonzero exit stops the campaign. So does
a sanitizer report in a slice's log even when the harness exits zero: UBSan
reports do not stop libFuzzer and leave no artifact, so an empty `artifacts/`
directory alone is not a clean result. A harness that has not exited 300
seconds after its time budget is stopped and recorded as a runner timeout.
Triage
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
- First 24-hour attempt (`--seconds 86400 --slice-seconds 900`, output
  `~/hue-campaign-24h`): **stopped at 11.0 of 24 hours, status `failed`, with
  one finding.** It is not acceptance evidence.
  - *Why it stopped.* Slice 43 (glTF) had not exited 30 seconds after its
    900-second budget and the runner killed it. No crash, no artifact. Every
    slice had been taking 922 to 930 seconds, so the 30-second allowance was
    too small from the start; it is now 300 seconds.
  - *Finding.* All eight glTF slice logs contain the same four UBSan reports
    ("load of misaligned address") inside cgltf, at `cgltf.h` lines 1544,
    2195, 2207 and 2410. cgltf reads 16- and 32-bit components and floats by
    casting a byte pointer, and mutated files placed accessor data off its
    component alignment, which glTF forbids. UBSan reports do not stop
    libFuzzer, so the slices exited zero, saved no artifact, and the runner
    recorded them as passed. On x86-64 the reads return the expected bytes;
    it is still undefined behaviour reached from untrusted input.
  - *Fix.* The loader now rejects a file whose accessor (or sparse accessor)
    data address or stride is not a multiple of its component size, before
    `cgltf_validate` or any read. A doctest covers four misaligned layouts.
    Replaying the stopped run's 2,288-file glTF corpus reproduces the four
    reports with the old harness and none with the patched one. The runner
    now fails a slice whose log holds a sanitizer report.
  - *What the 42 completed slices did.* Executions: glTF 6.35 million (2.05
    h), animation events 671 million, scene 754 million, tuning 1,078
    million, shader contract 0.80 million, WAV 156 million (1.8 h each). No
    ASan report, crash, timeout or artifact in any target, and no sanitizer
    report outside glTF.
- Second 24-hour attempt, revision `50fb994` with the fix (output
  `~/hue-campaign2-24h`): **interrupted after 1.03 of 24 hours; not
  acceptance evidence.** The WSL virtual machine powered off at 14:31 PST on
  November 12, eight minutes into the fifth slice, and took the runner with
  it. Windows itself stayed up; the `wsl.exe` that held the distribution open
  had been started from inside the development tool's process tree and
  evidently ended with it. The lock file and a `running` record remain, as
  the procedure above expects. The four completed slices (glTF 0.79 million
  executions, animation events 47.6 million, scene 85.5 million, tuning
  129.4 million) passed with no artifact and no sanitizer report; the
  partial shader-contract log has none either. WAV did not run.
- Third 24-hour attempt, same clean clone at `50fb994` (output
  `~/hue-campaign3-24h`): **stopped by the runner at 3.43 of 24 hours, status
  `failed`, with one finding.** It is not acceptance evidence. Its `wsl.exe`
  was created through WMI, outside any tool's process tree, and it ran
  until the runner stopped it.
  - *Finding.* Slice 13 (glTF) logged one UBSan report: `stb_image.h:1683`,
    "null pointer passed as argument 1, which is declared to never be null".
    stb_image calls `memcpy` with a null destination and a zero length for a
    PNG whose first IDAT chunk is empty. Nothing is copied and the image is
    rejected ("no IDAT"), but the call is undefined behaviour reached from
    untrusted input. The harness exited zero and saved no artifact; the
    runner's log check, added after the first attempt, is what stopped the
    campaign.
  - *Fix.* The one translation unit that compiles stb_image now routes its
    `memcpy` and `memset` calls through wrappers that skip a zero length. A
    doctest and a new seed (`png_empty_idat.glb`) hold the triggering file,
    rebuilt by hand since libFuzzer kept no artifact. The seed reproduces
    the report with the `50fb994` harness and gives none with the patched
    one; replaying the stopped run's 1,294-file glTF corpus through the
    patched harness gives none either.
  - *What the 12 completed slices did.* Executions: glTF 1.65 million (two
    clean slices, plus 0.89 million in the failed one), animation events 110
    million, scene 168 million, tuning 237 million, shader contract 0.26
    million, WAV 60 million. No ASan report, crash, timeout or artifact in
    any target, no misaligned-load report, and no sanitizer report outside
    that glTF slice.
- Fourth 24-hour campaign, from the commit that contains the stb_image fix
  (clean clone `~/hue-campaign4`, output `~/hue-campaign4-24h`, console log
  `~/hue-campaign4-24h.log`): **started November 14, not finished.** No
  result is claimed here. Started through WMI like the third.
  The budget is shared: each target gets about four hours, not twenty-four.
  The run pauses if the PC sleeps and dies if WSL (`wsl --shutdown`) or
  Windows restarts; a hard stop leaves the lock file and a `running` record
  and needs a fresh output directory, as described above.

Check progress from Windows:

```sh
wsl -d Ubuntu -- bash -lc "tail -n 5 ~/hue-campaign4-24h.log; ls ~/hue-campaign4-24h/artifacts/*"
```

When it ends, `campaign.json` holds the status, per-slice times and libFuzzer
counters. Record the outcome here, including any finding, before treating
Week 18 as having evidence.
