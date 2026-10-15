# Understanding and defending THE VAULT

Your goal is to explain why the program behaves correctly, make a small change yourself, and produce evidence that it works. Memorising C++ syntax is useful, but it does not establish those things. For library calls, understand the arguments, result, timing and limitations; you do not need to memorise the library implementation.

This is a practical engineering workflow, not a claim that every company uses one identical process. Google's [code-review guidance](https://google.github.io/eng-practices/review/reviewer/looking-for.html) provides a useful reference: review design, functionality, concurrency, complexity, tests and documentation, and inspect every assigned line in context. Keep formatting changes separate from behaviour changes.

## 1. Establish the baseline

Keep the original uploaded ZIPs. This repository is the reviewable working version. Read its change and validation records before flashing it; distinguish the uploaded behaviour from corrections made during review.

From the repository root:

```bash
pio run -d Receiver
pio run -d Sender
python3 tests/run_tests.py
```

In MATLAB, open the `MATLAB` folder and run `test_csi_motion`. PlatformIO's [`pio run` documentation](https://docs.platformio.org/en/latest/core/userguide/cmd_run.html) explains the build command and `-d` project-directory option.

A successful build establishes that compilation and linking succeeded. Host tests exercise firmware logic using simulated hardware. Hardware trials establish what the boards, sensors, radio and display actually do. Record the source revision alongside each result; an old passing result does not validate later changes.

## 2. Know where each responsibility lives

There are five receiver source/header files and four sender source/header files. Each board builds a separate executable; similarly named variables on different boards do not share memory.

| File under `src/` | Responsibility |
|---|---|
| `Receiver/main.cpp` | Startup, loop order, joystick, settings and saved calibration |
| `Receiver/security.cpp` | Guard decisions, local ultrasonic readings, vault transitions and siren |
| `Receiver/display.cpp` | Diamond, pedestal, shutters, settings and diagnostics drawing |
| `Receiver/wireless.cpp` | Incoming packets, remote readings, CSI, status replies and USB telemetry |
| `Receiver/firmware.h` | Types, constants, pins and declarations used across receiver files |
| `Sender/main.cpp` | Startup and repeated servicing of sender subsystems |
| `Sender/sensors.cpp` | Two ultrasonic sensors, BOOT/mock controls, buzzer and LED |
| `Sender/wireless.cpp` | Samples, receiver acknowledgments, CSI and USB telemetry |
| `Sender/firmware.h` | Sender types, constants, pins and cross-file declarations |

In MATLAB, `capture_csi.m` records and plots USB data; `parse_csi_line.m` validates records; `csi_motion_step.m` owns the independent detector; `analyse_csi.m` replays recordings; `test_csi_motion.m` checks synthetic cases.

Read `setup()` and `loop()` first, then follow one behaviour. Use **Go to Definition** and **Find All References**. The compiler uses declarations to check calls and links separately compiled files; it does not execute files from top to bottom as a reading list.

## 3. Understand the execution model

`setup()` runs once per reset. It configures hardware, restores settings and starts the radio. The framework repeatedly calls `loop()`. Each update function handles whatever is ready and returns so other work can proceed.

An ultrasonic interrupt service routine captures Echo edges asynchronously. Wi-Fi callbacks receive packets or CSI asynchronously. They copy data for the main loop; they do not run the entire alarm sequence. This application uses a cooperative main loop with interrupts and callbacks. The ESP32 framework also has background tasks, so “one loop” does not mean nothing else can execute concurrently.

`portENTER_CRITICAL(&wirelessLock)` protects shared queue data while it is read or changed. Locate its matching exit and every other access to that data. `volatile` alone does not provide this protection. Keep drawing, serial output and lengthy calculations outside such protected sections.

Time also needs careful ownership. Imagine the loop records `now = 1000`, then a callback timestamps a packet at `1001`. Unsigned `1000 - 1001` produces a huge elapsed value, although the packet is new. Counter-wrap arithmetic is useful only with the right time ordering. When reviewing freshness checks, identify where both timestamps came from and test callback arrival between loop operations. Consult the review changes for the specific handling in this version.

“Nonblocking” here describes the normal servicing approach. The ultrasonic trigger has a short microsecond pulse, `delay(1)` yields between iterations, and startup calibration has waits. Rendering and serial calls also consume real time.

## 4. Trace six complete behaviours

Write down the state before and after each important call. Do not stop at “this function is called.”

1. **Local alarm:** `captureEchoEdge()` captures the pulse; `updateDistanceSensor()` collects it; `processEchoMeasurement()` converts and filters distance; `GuardController::processDistanceSample()` confirms intrusion; `startSecurityAlarm()` latches the alarm and requests sealing. `updateBuzzer()` and display updates produce the outputs. Explain why an invalid echo is not a clear reading.

2. **Remote alarm:** sender `updatePhysicalSensors()` and `updateReportedReadings()` feed `makeSensorPacket()`. `sendWirelessPacket()` transmits it. Receiver `onWirelessReceive()` queues validated bytes; `updateWirelessReceiver()` calls `acceptRemotePacket()`. `RemoteSensorPair::acceptPacket()` checks sensor progression and confirmation before the common alarm function. Follow `publishReceiverStatus()` back to sender `processReceiverAcknowledgements()` and `updateAlarmOutputs()` to explain the second siren.

3. **CSI alarm:** `onCsiReceive()` copies a peer frame; `updateCsiMotion()` extracts and normalises 24 amplitudes, compares successive shapes and applies the learned threshold. Local or transmitted remote status reaches receiver `updateSharedCsiWarning()`. An eligible above-threshold event calls `startSecurityAlarm()`. Explain calibration, freshness and the two-second motion hold. A processed sample crossing the threshold does not guarantee detection of every physical movement between samples.

4. **Acknowledgment and clearance:** `updateJoystickInput()` debounces the button; `handleButtonPress()` gives acknowledgment priority; `acknowledgeAlarm()` changes the guard to `WaitingForClear`. Sound stops, but sealing continues. Follow valid local and remote readings through their clearance checks. Explain the 5 cm margin, 600 ms confirmation and active CSI warning gate. Absence of a CSI warning does not itself prove a fresh, quiet CSI measurement.

5. **Reopening and interruption:** `updateVaultAnimation()` combines guard state with `canRevealDiamond()` and calls `VaultAnimator::update()`. Follow shutter opening and diamond raising separately. Then inject another intrusion and follow `requestSeal()`. Predict why acknowledgment during closing must not expose the diamond.

6. **MATLAB over USB:** `capture_csi()` sends `m` to enable records. Firmware `queueCsiTelemetry()` and `serviceCsiSerial()` export them; the parser validates them; `csi_motion_step()` independently calculates motion; capture or replay plots it. MATLAB does not command the alarm. Your uploaded MATLAB uses floor **10**, multiplier **10**, smoothing **0.25** and **three** qualifying samples; firmware uses a different, immediate detector. Explain their different outputs before adjusting either.

## 5. Review each block with six questions

Work on one function or coherent section, usually 20–60 lines. For every statement, know its effect and why it belongs there. For repeated drawing commands, understand the pattern and verify coordinates, bounds and visibility conditions.

1. **Purpose:** Which requirement does it implement?
2. **Inputs:** Where do values originate? What are their units, bounds and freshness?
3. **State:** What does it return or modify? Who owns that state?
4. **Caller/context:** Who calls it, how often, and from loop, interrupt or callback?
5. **Failure:** What happens on invalid input, reset, timeout, overflow or concurrent access?
6. **Evidence:** Which test, trace or measurement supports your explanation?

Keep your learning separate from someone else's review. Fill the blanks with function names and dates, not blanket ticks.

| File | Reviewer findings read | Functions you can explain | Independently changed and verified |
|---|---|---|---|
| Receiver `main.cpp` | ___ | ___ | ___ |
| Receiver `security.cpp` | ___ | ___ | ___ |
| Receiver `display.cpp` | ___ | ___ | ___ |
| Receiver `wireless.cpp` | ___ | ___ | ___ |
| Receiver `firmware.h` | ___ | ___ | ___ |
| Sender `main.cpp` | ___ | ___ | ___ |
| Sender `sensors.cpp` | ___ | ___ | ___ |
| Sender `wireless.cpp` | ___ | ___ | ___ |
| Sender `firmware.h` | ___ | ___ | ___ |
| MATLAB: list each `.m` file | ___ | ___ | ___ |

## 6. Study in short, testable sessions

Use 45–60 minutes per session: predict behaviour, trace it, explain it aloud, then check one example. Record unresolved questions.

| Session | Deliverable |
|---|---|
| 1: Receiver startup/loop | Explain servicing order and complete the exercise below |
| 2: Local ultrasonic/guard | Trace near, invalid and clear samples on paper |
| 3: Animation/button | Predict early acknowledgment and reopening interruption |
| 4: Sender/ESP-NOW | Explain sample versus acknowledgment and stale-data rejection |
| 5: CSI | Calculate one normalised comparison and explain calibration |
| 6: MATLAB | Replay a recording and explain disagreement with firmware |
| 7: Shared data/failures | Audit locks, time ordering, timeout and queue behaviour |
| 8: One improvement | Small diff, matching evidence and an explanatory commit |

### First exercise: receiver `loop()`

Answer before reading the key:

1. Why save `menuWasOpen` before processing inputs?
2. Does passing `nowMs` make a function wait?
3. Why limit serial command handling to 16 characters per iteration?
4. Why publish receiver status after joystick and alarm servicing?
5. Does the 40 ms frame interval mean sensing only runs every 40 ms?
6. Why can a newly received timestamp be later than the loop's first timestamp?

### Short answer key

1. It detects a menu-to-alarm transition so the surrounding display frame can be restored.
2. No. It supplies a time value for elapsed-time decisions.
3. It bounds command work so other subsystems get another turn.
4. The transmitted state should reflect decisions made during this iteration.
5. No. Sensing updates run on loop iterations; drawing has its own due-time check.
6. A callback can run after the loop took its initial snapshot.

## 7. Improve with evidence

Choose one requirement or defect. Reproduce the problem, make the smallest understandable correction, inspect the diff, run relevant checks and write a commit explaining the resulting behaviour. Keep a behaviour change separate from renaming or formatting. Retest hardware whenever sensing, timing, radio or outputs change.

Measure before claiming efficiency: loop duration, display time, dropped records, radio freshness and minimum free heap are useful candidates. Compare the same workload before and after. Shorter code is not automatically faster; clever compression can make failure paths harder to inspect.

Use AI to explain a specific block, challenge assumptions or propose a small patch. Check every changed line and test the claimed result. Never present generated tests, a compiler pass or a reviewer's confidence as physical validation. Publish only claims you can connect to source and evidence; keep remaining uncertainties in the project documentation.
