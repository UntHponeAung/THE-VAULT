# Validation record

Date: 15 October 2026. The original review and style-pass results are recorded below; the project-name update is recorded separately. Current source hashes are in `source-manifest.json`. They were obtained in a Linux build environment, without connected ESP32 boards or a MATLAB runtime.

## Executed checks

| Check | Result | What it establishes |
|---|---|---|
| Existing host suite against uploaded firmware | PASS, 8,618 assertions | Baseline behavior under the existing mocks; it did not catch the newly reproduced timing defects |
| Targeted regression against uploaded firmware | FAIL as expected | A fresh callback after the loop clock snapshot was rejected |
| Separate age/confirmation reproductions against uploaded firmware | Defects reproduced | A 410 ms old sample could trigger; 560 ms measurement history could be treated as 600 ms clearance |
| Updated host suite against reviewed firmware | PASS, **9,557 assertions** | Actual logic against mocked GPIO, radio, clocks, serial and display, including the new regressions |
| Receiver PlatformIO compilation and linking | PASS | Independently compiled receiver modules with the configured ESP32 framework/library |
| Sender PlatformIO compilation and linking | PASS | Independently compiled sender modules with the configured ESP32 framework |
| MISS_HIT 0.9.44, MATLAB R2019b language mode | PASS, all five `.m` files | Static syntax/lint checks; not MATLAB execution |
| PlatformIO configuration comparison | Identical to uploads | No pin, platform, library or build-flag changes in either `platformio.ini` |
| MATLAB detector comparison | Same executable tokens; comments/formatting changed | User's 10/10 tuning and detector logic preserved |
| Uploaded CSV inspection and Python arithmetic replay | Completed | See [CSI data review](CSI_DATA_REVIEW.md); no movement ground truth or accuracy claim |

Host assertions include repeated checks inside scenarios. **9,557 assertions does not mean 9,557 independent tests.** The harness uses the real application logic but not the real drivers or concurrent ESP32 scheduling.

After the style pass, both builds, the 9,557-assertion suite and MATLAB lint were rerun successfully. All 17 C++ files pass clang-format 18.1.8 checks. A 23-file token/AST comparison and matching allocated firmware ELF sections provide additional evidence that presentation changes preserved the program. See [source style](STYLE.md) for the method and limitations. Build footprints below remain unchanged by that pass.

## Build footprint before the project-name update

PlatformIO Core 6.2.0, `espressif32@6.9.0`, Arduino ESP32 2.0.17. Receiver library: `TFT_eSPI@2.5.43`.

| Board | Static RAM reported | Flash reported |
|---|---:|---:|
| Receiver | 48,948 / 327,680 bytes (14.9%) | 826,181 / 1,310,720 bytes (63.0%) |
| Sender | 45,812 / 327,680 bytes (14.0%) | 752,329 / 1,310,720 bytes (57.4%) |

These are linker/build figures, not runtime free heap or peak stack measurements. The receiver allocates a 400 × 272, 4-bit scene sprite at startup: 54,400 bytes of pixel payload, plus library overhead. No PSRAM assumption was added. The receiver still emits the library warning that `TOUCH_CS` is undefined; this project does not use TFT touch input.

The uploaded receiver built at 48,956 static RAM bytes. Removing unused state reduced that by eight bytes. This small change is not evidence of faster sensing or rendering.

## Reproduce locally

From the repository root, with Python 3 and a C++17 compiler installed:

```bash
python3 -m pip install -r requirements-dev.txt
python3 tests/run_tests.py
pio run -d Receiver
pio run -d Sender
mh_lint --matlab 2019b MATLAB
```

Use a Python virtual environment if your OS requires one. `tests/README.md` explains the host harness and its limits. The GitHub workflow runs equivalent commands, but **the workflow has not been executed on GitHub** in this review.

In MATLAB, with its Current Folder set to `MATLAB`, run:

```matlab
test_csi_motion
result = analyse_csi("data/example_unlabelled.csv");
```

**These MATLAB runtime commands remain unexecuted here.** The sample CSV is an unchanged copy of the uploaded `csi_20261013_184338_node1.csv`; it is an unlabelled recording, not a controlled detection benchmark. Run a live capture after validating the ordinary two-board firmware.

## Hardware release checklist — all pending

Record the source commit, setup, expected result and observed result for each trial. A failure stays visible until resolved.

| Trial | Expected observation |
|---|---|
| Startup and saved calibration | Correct display, settings restored, arming grace then monitoring; CSI calibration completes with usable traffic |
| Each ultrasonic separately | Local, remote 1 and remote 2 each latch alarm with the correct cause/distance |
| CSI separately on both boards | A qualifying processed event from either board produces the appropriate main/remote CSI cause |
| Complete sealing and early acknowledgment | Diamond retracts, shutters fully close; acknowledgment stops sirens but cannot reveal it early |
| Continued proximity / missing echoes | Acknowledged vault stays sealed; invalid ultrasonic readings cannot clear it |
| Valid clearance | All three distances sufficiently clear and confirmed; no active CSI warning; vault reopens |
| Intrusion during reopening | New alarm and reversal from current visual position |
| Mute | Visible MUTE status; both buzzers remain silent while visual alarm continues |
| Settings / diagnostics during intrusion | Monitoring continues and alarm scene takes over |
| Sender pause, power loss and reboot | Silent fault sealing while armed; acknowledgment and valid restored clearance required; existing audible alarm survives loss |
| BOOT / serial overrides | Correct mock cause, debounce and release; `h` returns to physical sensors |
| MATLAB live USB capture | Capture and cleanup release the serial port; compare plotted firmware and independent MATLAB results |
| Display, sound and load | No visible diamond through closed shutters; inspect actual animation, responsiveness, sirens and sensor cross-talk |

## Performance experiment before optimization

Measure loop duration, time spent drawing, maximum packet age, minimum free heap and dropped telemetry under the same fixed workload: idle, alarm animation, diagnostics and USB streaming. Summarize distributions and worst observed values, with the measurement method. Keep instrumentation outside interrupt critical sections. A lower line count or successful build is not a latency measurement.

Then change one measured bottleneck, repeat the experiment and compare. No runtime benchmark, CSI range measurement, detection accuracy or false-alarm rate was established by this review.

## THE VAULT name update

The project and package were renamed to THE VAULT. Comparing all 25 code/configuration files with the preceding package confirmed that only branding strings and the host-test temporary-folder prefix changed. The receiver rebuilt successfully, all 9,557 host assertions passed, all five MATLAB files passed static lint, and all 17 C++ files passed the formatter check. Sender source and both PlatformIO configurations are unchanged; the sender was not rebuilt again for this naming-only update. Hardware and MATLAB runtime checks remain pending.

Receiver build after renaming:

```text
RAM:   [=         ]  14.9% (used 48948 bytes from 327680 bytes)
Flash: [======    ]  63.0% (used 826181 bytes from 1310720 bytes)
```

The previous style-token and ELF reports remain historical records of the style pass before the name update, not a claim that the newly named display strings have identical compiled bytes.
