# Review of Edward's uploaded system

Reviewed 15 October 2026. This working package combines the exact uploads `pixel-pals(3).zip`, `ESP32_Remote_Sender(2).zip` and `MATLAB(1).zip`. Originals are retained separately. All nine firmware source/header files, both PlatformIO configurations, all five MATLAB scripts, supplied instructions, legacy test and twelve recordings were inspected. This is a source review with targeted regression tests, not a formal proof or hardware certification.

## Corrections made

| Finding | Reproduction / consequence | Correction |
|---|---|---|
| Fresh radio callbacks rejected | Loop time 1000 ms, callback arrival 1001 ms: unsigned subtraction looked almost 49 days old | Read processing time after dequeue; refresh loop time after radio and CSI updates on both boards |
| Fresh CSI temporarily advertised as unavailable | The same stale clock preceded a newly consumed CSI frame | Refresh before decisions, outputs and publication; exercise both real loops in host regressions |
| Old queued sensor reading could trigger | Reported sample age 70 ms plus queue wait 340 ms totals 410 ms, but both separate checks passed the 350 ms limit | Combine reported age and queue age before remote sensor confirmation |
| Arrival delays inflated confirmation | A 600 ms arrival span contained only 560 ms of estimated measurement history | Confirm using arrival time minus reported measurement age; retain arrival time for radio heartbeat |
| MATLAB synthetic test used obsolete threshold | It expected 2.5 although the uploaded detector floor is 10 | Derive the expected stationary-noise floor from the detector's initial state |
| MATLAB parser accepted collapsed empty CSV fields | Repeated delimiters could be collapsed before field-count validation | Preserve empty fields; reject invalid counts and out-of-range dropped-record counters |
| Offline replay lacked live sequence checks | Duplicate/reordered rows could reset calibration or introduce a huge apparent time gap | Apply the same sequence progression rule as live capture; add replay tests |
| Old instructions described different software | Receiver README described avatars/no Wi-Fi; sender README said mock default; build status and native test were stale | Replace instructions, remove obsolete legacy test from release and use maintained host harness |
| Unused receiver state | `alarmWasRemote` was assigned but never read by firmware; `turnOff()` had no production caller | Remove both; tests check the actual alarm cause |

The firmware timing and age regressions were reproduced against the uploaded implementation before the fixes. The maintained original host suite passed 8,618 assertions despite those gaps. Additional targeted regressions now cover the failure conditions; see validation for the reviewed result.

`source-changes.patch` records changes to uploaded source/configuration and MATLAB code. It deliberately excludes generated build files and new documentation. `source-manifest.json` identifies original archive hashes and reviewed source hashes.

## Preserved choices

- Five receiver and four sender source/header files; no extra firmware modules or new runtime dependencies.
- GPIOs, display configuration, ultrasonic filtering, measurement intervals, 140 ms intrusion confirmation, 600 ms clearance, 1,200 ms arming grace and animation travel durations.
- Both `platformio.ini` files byte-for-byte; ESP-NOW protocol version 4 and 68-byte format.
- Existing `pixel-pals` preference namespace and calibration keys. Renaming the presentation to THE VAULT does not discard settings.
- Firmware CSI floor/multiplier 2/2 and MATLAB 10/10, with its separate smoothing and confirmation. The MATLAB detector's executable logic is unchanged; the subsequent style pass edited comments and spacing.
- Acknowledgment, mute, BOOT override, settings navigation and link-loss policy.

## Decisions and limits still visible

**CSI recovery is an ultrasonic fallback policy.** An active CSI warning blocks recovery, but missing/calibrating CSI does not. This review corrects the documentation instead of silently changing that policy. If future requirements demand both CSI detectors positively ready and quiet, that needs a deliberate change and new recovery/failure tests.

**No measured runtime-speed claim.** The code already uses fixed buffers, bounded command handling and one startup scene allocation. Removing unused state slightly reduces static RAM; build sizes are recorded separately. No ESP32 profiling was possible, so there is no defensible “maximum efficiency” or latency claim. Profile display flush, loop time, queue pressure and serial load before optimizing them.

**CSI frames can be overwritten.** A single pending frame favors bounded memory and responsiveness. The program guarantees neither all RF-frame processing nor detection of all physical movements. The available CSV `dropped` count concerns telemetry serialization, not every CSI callback overwrite.

**Communication is a bench-demo design.** First-peer binding and sequence validation are not authentication. No radio encryption, secure provisioning or anti-replay cryptography was added. The boards must use the same channel and matching protocol.

**Warning-only branches remain.** Earlier warning chirp/display paths still exist, but normal armed CSI motion now takes the full alarm path before those outputs run. They are not advertised as a separate early-warning stage. A later focused cleanup can remove unused branches while preserving protocol compatibility.

**Some protocol definitions are duplicated.** They remain in each project's header to retain independently openable PlatformIO projects and the small file count. Tests and review check interoperability. A shared protocol package is a future trade-off, not a necessary release refactor.

**Calibration and electrical behavior require the real setup.** Quiet calibration, sensor mounting, radio reflections, supply quality and possible ultrasonic cross-talk change results. Neither static analysis nor host mocks establishes real accuracy or sound/display quality.

## Publishing and continuing the review

1. Extract this package and make a private GitHub repository named `esp32-csi-the-vault` (or choose a different name). Create it empty, without a generated README.
2. From the extracted `THE_VAULT` directory, run the checks in validation and review `source-changes.patch`.
3. Initialise and commit the local files:

   ```bash
   git init -b main
   git add README.md .gitignore .editorconfig .clang-format requirements-dev.txt .github Receiver Sender MATLAB tests docs
   git diff --cached --stat
   git status --short
   git commit -m "Add reviewed dual-node ESP32 security demonstrator"
   ```

4. Copy your own repository URL from GitHub, then run `git remote add origin YOUR_REPOSITORY_URL` and `git push -u origin main`. `YOUR_REPOSITORY_URL` is a placeholder to replace, not a literal command value. Check the Actions result after pushing; it has not run on GitHub here.
5. Flash both boards and complete the hardware checklist. Record the commit, test conditions and observed result. Add a short real demonstration video, one wiring photo and a labelled CSI plot. These are remaining evidence tasks, not missing firmware placeholders.
6. Choose a license appropriate to how you want others to reuse your work before presenting it as an open-source release. No license has been selected on your behalf.
7. Work through [LEARNING_GUIDE.md](LEARNING_GUIDE.md). The next session is receiver `setup()` and `loop()`: explain every call and its execution context, then trace one intrusion through to both buzzers.

For subsequent fixes, use a branch, a narrow diff, relevant tests and a commit explaining why the change is necessary. Formatting, detector tuning and behavior changes should be separately reviewable. You can explain AI assistance candidly: the evidence is your ability to reason about the implementation, reproduce defects and validate changes.

References: [Google code review guidance](https://google.github.io/eng-practices/review/reviewer/looking-for.html) and [PlatformIO GitHub Actions integration](https://docs.platformio.org/en/latest/integration/ci/github-actions.html). These inform the workflow; no single process is universal across developers.

The later presentation-only pass is documented in [STYLE.md](STYLE.md). It retains the fixes above while simplifying comments, standardizing layout and renaming selected local variables. The recorded source hashes and source patch reflect the current styled files.
