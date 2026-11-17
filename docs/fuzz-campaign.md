# Local sustained fuzz campaign

Prepared October 14, 2026 as a runner only. On November 11, 2026 the owner
approved a run; see "Run log" at the end for what was started and what has
and has not been observed. Short CI runs and runner tests are not the
sustained acceptance evidence.

After six attempts that each lost everything to one event (a finding in one
target, a restart, a clock step), the runner was rebuilt on November 17 around
three rules: every target is its own unit of evidence, targets run in
parallel, and running the same command again continues whatever is unfinished.

## Running it

Use a Linux worker with the normal Clang/Ninja build prerequisites (on Ubuntu
24.04: `clang clang-tools cmake ninja-build xorg-dev git`; `clang-tools`
provides the `clang-scan-deps` the build needs), Python 3.11+ and enough free
space for generated corpus and logs. From a clean committed checkout:

```sh
tools/fuzz_local.sh
```

That builds the six sanitized harnesses and starts or resumes the campaign in
`~/hue-fuzz-out` (`HUE_FUZZ_OUTPUT` changes it), logging to
`~/hue-fuzz-out.log`. It is the same as:

```sh
cmake --preset linux-clang-sanitized -DHUE_FUZZ=ON -DHUE_TRACY_ENABLE=OFF
cmake --build --preset linux-sanitized --target fuzz_gltf fuzz_animation_events fuzz_scene fuzz_tuning fuzz_shader_contract fuzz_wav
python3 tools/fuzz_campaign.py --binary-dir build/linux-clang-sanitized/tools/fuzz --output ~/hue-fuzz-out
```

On the development PC the checkout is `~/hue-fuzz` inside WSL, and Windows
drives it:

```sh
powershell -File tools\fuzz_wsl.ps1            # start or resume, detached
powershell -File tools\fuzz_wsl.ps1 -Status    # progress of each target
powershell -File tools\fuzz_wsl.ps1 -Update    # git pull in the WSL clone, then resume
```

Defaults: four hours of fuzzing per target (`--seconds-per-target 14400`), in
15-minute slices, with up to six targets at once (`--jobs`, default half the
logical cores). That is the same 24 worker-hours the old single-worker plan
asked for, in about four and a half hours of wall time. `--targets` limits a
run to some targets and `--restart TARGET` archives one target's state and
starts it again.

## What counts

- **Each target stands alone.** `<output>/<target>/` holds that target's
  `state.json`, corpus, logs and artifacts. A target is `completed` when its
  passed slices add up to its budget. A finding fails that target only; the
  others carry on and their completed status is unaffected.
- **Only demonstrated fuzzing time counts.** A slice counts the smallest of
  its requested time, its measured wall time and, where `/proc` reports it,
  the CPU time the harness actually used. A PC that sleeps through a slice
  therefore adds nothing, and a stepped clock cannot inflate the total.
  Interrupted, `clock_step` and failed slices count zero.
- **A result belongs to the harness that produced it.** State records the
  harness's SHA-256. If a rebuilt harness differs, that target's old directory
  moves to `<output>/superseded/` and the target starts again from zero, with
  the old corpus added to its seeds (a corpus is input, not evidence). Targets
  whose harness is byte-identical keep their progress, so a fix in one parser
  does not throw away the others. Each slice also records the source
  revision it ran from.
- **Stopping loses one slice at most.** Ctrl+C or a termination signal asks
  each harness to stop; those slices are recorded as `interrupted`. If the
  runner is killed outright, or the machine restarts, the next run finds the
  slice still marked `running`, records it as `interrupted`, scans its
  partial log for sanitizer reports, and continues. The lock is an operating
  system file lock, so a dead runner never blocks the next one and two live
  runners cannot share an output.
- **A failure is sticky.** A crash, a real timeout, a nonzero exit, a
  sanitizer report anywhere in a slice's log (UBSan reports do not stop
  libFuzzer and leave no artifact), a harness that has not exited 300 seconds
  after its budget, or a file in `artifacts/` that no slice accounts for all
  mark the target `failed`. It stays failed on every later run until its
  harness changes or `--restart` is given. Triage the saved input, add a
  regression test, rebuild, and run again.
- **Clock steps are listed, not counted and not fatal.** libFuzzer times
  units with the wall clock, and a virtual machine's clock can be stepped
  backwards by its host. libFuzzer then measures a negative duration and
  either reports a timeout "after 18446744073709551615 seconds" (minus one
  second as an unsigned number) or exits at once believing its budget is
  spent. The runner records such a slice as `clock_step`. For the timeout
  form it does so only if the one reported duration is that kind of
  impossible number (2^63 or more), no sanitizer report is present, and the
  single saved input replays through the harness with a zero exit in under
  ten seconds; the input then moves to `<target>/clock-step/` and the replay
  log is kept. A target that produces nothing but uncounted slices fails
  after a bounded number of them rather than looping.

`<output>/campaign.json` summarises every target: status, counted seconds,
slices by outcome, executions in passed slices, harness hash and source
revisions. Its overall status is `completed` only when all six targets are.
Record a result from that file and list `clock_step` and `interrupted` slices
beside it. `completed` means the budget was consumed without a finding, not
that security is proven. Counters are libFuzzer coverage/features, not
source-line coverage percentages; do not add coverage counts across runs.

The runner copies seeds into its output directory and never grows the
checked-in corpus. Each harness has a 10-second input timeout, a 2 GiB RSS
limit and a bounded maximum input size. Input size caps are policy limits, not
proof that every valid large asset has been exercised. Logs and corpus growth
consume disk space; watch free space during a long run.

The PC still has to be on and awake for fuzzing to happen. What changed is
the cost of it not being: a sleep or restart loses the slices in flight and
nothing else, and one command resumes. Nothing resumes it automatically.

CI exercises the same runner for three seconds per target and retains its
state and logs, in addition to the six existing 60-second harness runs. This
verifies plumbing, not sustained coverage or human playability.

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
- Fourth 24-hour attempt, revision `27fd18e` with the stb_image fix (output
  `~/hue-campaign4-24h`, started November 15): **stopped by the runner at
  5.61 of 24 hours, status `failed`; no parser finding.** It is not
  acceptance evidence.
  - *Why it stopped.* Slice 23 (shader contract) ended after 312 seconds
    with a libFuzzer timeout "after 18446744073709551615 seconds" ("working
    on the last Unit for -1 seconds") and a saved `timeout-` input. That is a
    negative duration: the WSL clock was stepped backwards under libFuzzer.
    The saved input replays through the same harness in 0.14 seconds with a
    zero exit and no sanitizer report.
  - *A second clock step.* Slice 5 (shader contract) exited zero after 0.4
    seconds and eight executions, printing "Done 8 runs in -1 second(s)",
    and was recorded as passed. Its log's modification time is a second
    earlier than the previous slice's. It did no fuzzing worth counting.
  - *Runner change.* Both forms are now recorded as `clock_step` under the
    rule above, and the campaign continues. Run against the real logs of
    this attempt, the new check classifies both slices and replays the saved
    input in 0.2 seconds.
  - *What the other 21 slices did.* Executions: glTF 3.05 million,
    animation events 287 million, scene 371 million, tuning 530 million,
    shader contract 0.25 million, WAV 117 million (about one hour each,
    shader contract 0.5 h). No sanitizer report, crash or artifact in any
    target; neither earlier finding recurred.
- Fifth 24-hour attempt, revision `477a3c7` with the runner change (output
  `~/hue-campaign5-24h`, started November 16): **stopped by the runner at
  12.0 of 24 hours, status `failed`; no parser finding.** It is not
  acceptance evidence.
  - *Why it stopped.* Slice 49 (glTF) ended with a libFuzzer timeout "after
    18446744073709551614 seconds" ("working on the last Unit for -2
    seconds"): the same backwards clock step, this time by two seconds. The
    runner only recognised the one-second form, so it failed the slice. The
    saved input replays in 0.02 seconds, loads as a valid mesh, and produces
    no sanitizer report. The runner now accepts any duration of 2^63 seconds
    or more, under the same replay rule.
  - *Clock steps handled.* Five earlier slices were recorded as
    `clock_step` and the campaign continued: three shader-contract timeouts
    with a negative duration (slices 5, 17 and 47; each saved input replayed
    cleanly in under 0.2 seconds) and two immediate exits (tuning slice 16,
    scene slice 45). Six clock steps in twelve hours is the environment this
    runs in; their cause on this PC is not known.
  - *What the 43 passed slices did.* Executions: glTF 7.25 million,
    animation events 583 million, scene 713 million, tuning 1,036 million,
    shader contract 0.58 million, WAV 219 million (1.9 to 2.1 hours each,
    shader contract 1.3 h). No sanitizer report, crash or genuine timeout
    in any target; neither earlier finding recurred.
- Sixth 24-hour attempt, revision `437f9be` (output `~/hue-campaign6-24h`,
  started November 16): **interrupted at 2.89 of 24 hours when the PC was
  restarted; not acceptance evidence.** Ten slices passed (glTF 1.60 million
  executions, animation events 105 million, scene 156 million, tuning 243
  million, shader contract 0.23 million, WAV 46 million), one
  shader-contract slice was a verified `clock_step`, and the twelfth was in
  flight. No sanitizer report, crash or artifact.

Those six attempts used the first runner: one worker, one shared 24-hour
budget, and a campaign that any single event ended. Between them they ran
about 36 hours and produced two real findings, both fixed (misaligned glTF
accessors in cgltf, a null zero-length `memcpy` in stb_image), but no
completed campaign. Everything below uses the per-target runner described
at the top of this page.

- **Per-target campaign, started November 17** (WSL clone `~/hue-fuzz`,
  output `~/hue-fuzz-out`, four hours per target, six targets at once).
  **Not finished; no result is claimed here.**
  It makes no progress while the PC sleeps and stops if WSL or Windows
  restarts; `tools\fuzz_wsl.ps1` then continues it from where each target
  was.

Check progress from Windows:

```sh
powershell -File tools\fuzz_wsl.ps1 -Status
```

When every target reads `completed`, record the contents of
`~/hue-fuzz-out/campaign.json` here, including `clock_step` and
`interrupted` slices and any finding, before treating Week 18 as having
evidence.
