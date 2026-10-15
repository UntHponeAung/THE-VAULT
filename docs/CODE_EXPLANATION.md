# THE VAULT — Code explanation

THE VAULT uses two ESP32s to combine ultrasonic proximity sensing with experimental Wi-Fi CSI motion detection. The receiver controls the LCD, local ultrasonic sensor, joystick and buzzer, while the sender handles two ultrasonic sensors, a red LED and a second buzzer. Both boards run CSI detection; however, the receiver owns the shared alarm state, so the sender reports measurements and follows its instructions.

Alongside the firmware, MATLAB records CSI amplitudes over USB and runs an independent detector. This allows me to compare the two approaches using the same measurements, while the alarm system continues to work without a computer.

## 1. Code organisation

The receiver has five C++ source/header files and the sender has four. Their responsibilities are separated as follows, while each board's `firmware.h` provides the declarations needed to connect its source files.

| Board | File | Responsibility |
| --- | --- | --- |
| Receiver | `main.cpp` | Startup, loop scheduling, joystick input and saved settings |
| Receiver | `firmware.h` | Constants, packet layout, shared types and module interfaces |
| Receiver | `security.cpp` | Local distance sensing, guard state, vault movement and sound |
| Receiver | `wireless.cpp` | ESP-NOW, remote sensor confirmation, CSI and USB telemetry |
| Receiver | `display.cpp` | Exhibit drawing, settings and diagnostics |
| Sender | `main.cpp` | Startup and coordination of sensing, communication and outputs |
| Sender | `firmware.h` | Pins, packet layout, sensor types and module interfaces |
| Sender | `sensors.cpp` | Ultrasonic measurements, test inputs, LED and buzzer |
| Sender | `wireless.cpp` | Sample reports, receiver acknowledgments, CSI and USB telemetry |

Each board also has a `platformio.ini` containing its build configuration. In addition, the receiver configuration defines the LCD interface, so its display settings must match the wiring.

## 2. Startup, execution and timing

### Startup and the main loop

On each board, `setup()` configures the hardware and starts communication, while `loop()` repeatedly services the application. The receiver also restores settings, calibrates the joystick when required and allocates its display buffer before starting the 1,200 ms arming grace period; if buffer allocation fails, startup stops with an error message.

The receiver loop processes wireless input, link status and CSI before updating its local ultrasonic sensor. It then handles USB telemetry, joystick input, animation and sound, followed by outgoing status and any screen update that is due. Meanwhile, the sender loop prepares measurements, processes receiver replies, updates CSI and outputs, and schedules its next sample report.

The normal display interval is 40 ms, while diagnostics uses 200 ms. Consequently, sensing and alarm handling can run between redraws, although these intervals do not guarantee a particular frame rate under every workload.

### Interrupts, callbacks and shared data

Ultrasonic interrupts capture echo edges, while Wi-Fi callbacks copy incoming packets and CSI into buffers for the loop to process. No additional application worker task is created, but Arduino/ESP-IDF still runs framework tasks underneath, so callback data can arrive while the application is running.

Short critical sections protect data shared between these execution contexts. For example, `portENTER_CRITICAL(&wirelessLock)` and `portEXIT_CRITICAL(&wirelessLock)` protect queue updates, while drawing, printing and signal processing stay outside the lock to avoid holding it during longer operations.

### Elapsed-time scheduling

Animation and sound use elapsed time from `millis()`, so their update functions return without waiting for a whole sequence to finish. The loop also refreshes its clock after consuming callback data, because subtracting a new sample timestamp from an older unsigned loop timestamp could incorrectly make that sample appear stale.

Normal unsigned time differences handle clock wraparound. However, startup calibration, the 10 µs trigger pulse, the 1 ms loop yield and potentially blocking library I/O still take time, so the implementation has no measured hard real-time guarantee.

## 3. Ultrasonic sensing and alarm decisions

### Measuring and filtering distance

On the receiver, `updateDistanceSensor()` schedules measurements and `captureEchoEdge()` captures the echo pulse. Then, `processEchoMeasurement()` converts pulse duration into distance using the sound's round trip and filters recent readings.

On the sender, `updatePhysicalSensors()` alternates pings every 70 ms, giving each sensor an update approximately every 140 ms. Its interrupt handlers capture the pulse edges, while `finishSensorMeasurement()` converts and filters the result; staggering these pings reduces interference between the sender's sensors, but does not synchronize all three sensors across both boards.

The filter uses the larger reading while two samples are available, followed by the median of three. This reduces sensitivity to isolated low readings, although it can also delay the response to a real sudden approach. Validity is tracked separately, so an old filtered distance cannot turn a missing echo into fresh clearance evidence.

### Guard states and recovery

`GuardController` tracks whether the system is arming, armed, alarmed or waiting for clearance. An ultrasonic intrusion requires 140 ms of confirming samples, while the shared boundary defaults to 30 cm and is adjustable from 10–100 cm in 5 cm steps.

| State | Behaviour |
| --- | --- |
| `Arming` | Allow the 1,200 ms startup grace period |
| `Armed` | Monitor for confirmed proximity, CSI events and remote link loss |
| `AlarmLatched` | Keep the alarm active until joystick acknowledgment |
| `WaitingForClear` | Keep the vault secured until recovery conditions are satisfied |

Acknowledgment stops the siren but does not immediately reopen the vault. Recovery requires all three ultrasonic sensors to confirm valid readings at least 5 cm beyond the boundary for 600 ms, with no active CSI warning; the extra distance provides hysteresis, which separates the trigger and clearance boundaries.

### Alarm and connection-loss handling

`startSecurityAlarm()` records the cause, latches the alarm, requests sealing and updates the buzzer immediately. It also closes settings or diagnostics and requests a fresh display frame, so the alarm scene takes priority while the receiver prepares a status update for the sender.

If the remote heartbeat becomes stale while armed, `updateLinkSafety()` seals the vault silently. A confirmed intrusion can then escalate the fault into an audible alarm, while restoring the link alone cannot bypass acknowledgment and valid clearance.

## 4. Animation, display and controls

### Separating the guard from the animation

The guard decides whether the exhibit must remain secured, while `VaultAnimator` controls how the diamond and shutters move. Its states are `Displaying`, `Retracting`, `Sealing`, `Sealed`, `Opening` and `Raising`, with 420 ms diamond travel, 260 ms shutter travel and a minimum 120 ms completed-seal hold.

`requestSeal()` starts or reverses movement towards sealing, and `update()` advances the animation from its current position. Therefore, early acknowledgment cannot interrupt closure, and a new intrusion during reopening can reverse the movement without a sudden position jump.

### Drawing and sound

`drawAnimatedScene()` combines the spotlight, diamond, pedestal, shutters, border and status overlays. It reuses a 400 × 272, 4-bit scene buffer with a 16-colour palette, avoiding a new allocation for each frame; the diamond is also suppressed whenever any shutter closure remains.

Meanwhile, `updateBuzzer()` selects the sound and `sirenFrequency()` produces the rising and falling siren. The mute setting affects both buzzers through receiver status, while detection and the visual response continue.

### Joystick and saved settings

`readJoystickAxis()` applies calibration, a centre dead zone and optional axis reversal, while `updateJoystickInput()` handles movement, 25 ms button debouncing and the one-second hold action. A short press normally toggles diagnostics and a hold opens settings, but alarm acknowledgment takes priority over both actions.

`loadSettings()` restores the distance, sound, axis and calibration settings, while `saveSettings()` stores them in flash. The existing `pixel-pals` namespace is retained for compatibility, and opening settings does not stop monitoring, although it can pause reopening.

## 5. ESP-NOW communication

### Packet format and radio setup

`initialiseWireless()` starts Wi-Fi station mode and ESP-NOW broadcast on channel 1, so no router is required. Both boards use the same version-4 `WirelessPacket`, containing distances, validity flags, ages, sequence numbers, CSI results and receiver control information.

A compile-time `static_assert` checks that the packet is 68 bytes. However, matching size alone does not prove matching meaning, so the field layout must also remain identical in both headers.

### Receiving and validating messages

`onWirelessReceive()` validates the format and copies packets into an eight-slot queue, which the loop drains through `takeReceivedPacket()`. If the queue fills, it drops the oldest packet; the receiver then resets remote confirmation, while the sender distrusts receiver status until a valid newer reply arrives.

Further processing checks the selected peer MAC, boot session, sequence and age. These checks distinguish restarts and reject duplicate, reordered or stale information, while `RemoteSensorPair` and its two `RemoteSensorMonitor` objects maintain separate confirmation histories for the sender's sensors.

Each sensor has its own sample counter, so repeated radio reports cannot turn one measurement into multiple pieces of evidence. In addition, reported sample age plus receiver queue time must remain below 350 ms; measurement timing is estimated from age and arrival time, without synchronizing the boards or removing unknown radio latency.

### Reports, acknowledgments and output control

The sender builds reports using `makeSensorPacket()` and submits them through `sendWirelessPacket()` approximately every 70 ms. `onWirelessSend()` releases the send-busy flag when an attempt finishes, but application-level acknowledgment is still needed to confirm that the receiver processed a sample.

On the receiver, `acceptRemotePacket()` handles incoming reports, while `publishReceiverStatus()` returns acknowledgments and the current security state. The sender's `processReceiverAcknowledgements()` accepts these replies, then `updateAlarmOutputs()` uses them to control its buzzer and LED.

The sender estimates siren phase using the receiver's elapsed alarm time plus time since receipt. This keeps the patterns approximately aligned, although wireless latency prevents exact acoustic synchronization; if contact is lost during an already latched alarm, the sender retains the last alarm command.

The protocol has no encryption or cryptographic authentication. Therefore, identifier, MAC and sequence checks improve message handling but do not prevent a capable attacker from constructing matching packets.

## 6. CSI motion detection

### From a radio frame to a motion score

`onCsiReceive()` copies a suitable peer CSI frame into a protected pending buffer, while `updateCsiMotion()` processes it in the loop. Only one frame is pending at a time, so a newer frame can overwrite an unprocessed one, and unusable, stale or excessively closely spaced samples are rejected.

For each of 24 selected subcarriers, the detector calculates the complex magnitude and divides it by the mean amplitude. It then compares this normalized vector with the previous one, so the score captures changes in the pattern across subcarriers while reducing uniform receiver-gain effects.

```text
amplitude = sqrt(real² + imaginary²)
shape = amplitude / mean(amplitude)
change = 100 × mean(abs(shape - previousShape))
threshold = max(minimum, baselineMean + multiplier × baselineStandardDeviation)
```

### Calibration and triggering

Calibration requires at least ten seconds and 80 comparisons, with both firmware detectors using a minimum threshold of 2 and noise multiplier of 2. These are dimensionless score settings rather than distances, and the learned threshold may be higher than the minimum depending on the calibration data.

Once calibrated, a single processed score strictly above threshold marks motion, which is held for two seconds for reporting. This hold is not a confirmation delay, although accepting one crossing also makes isolated signal changes more likely to cause an alarm; it does not guarantee capture of every physical movement.

### Combining both detectors

`updateSharedCsiWarning()` combines local motion with fresh sender CSI status and can trigger the receiver's alarm while armed. After acknowledgment, an active warning prevents clearance instead of repeatedly cancelling the acknowledgment, and each board reports only its own detector result to avoid warning feedback.

Stale CSI resets calibration, while an active warning blocks recovery. However, the current policy permits ultrasonic-only recovery when CSI is unavailable, disabled or calibrating, so absent CSI evidence is not treated as confirmed quiet.

CSI measures radio-path changes and does not establish a person's exact location or a guaranteed detection radius. Consequently, its behaviour needs to be evaluated for the actual board placement and environment.

## 7. MATLAB analysis

| File | Role |
| --- | --- |
| `capture_csi.m` | Open one or two USB ports, request telemetry, save CSV records and plot live results |
| `parse_csi_line.m` | Validate the `CSI1` marker, field count and numeric values |
| `csi_motion_step.m` | Update MATLAB's independent detector for each accepted sample |
| `analyse_csi.m` | Replay a saved capture and return a results table, with optional plots |
| `test_csi_motion.m` | Check detector, parser and replay-ordering behaviour with controlled inputs |

Live capture and replay reject duplicate or out-of-order samples, while data gaps require recalibration. This prevents malformed records and repeated measurements from silently becoming new evidence in the analysis.

MATLAB uses the same basic normalized pattern comparison, but its minimum threshold and noise multiplier are both 10. It also smooths the score using `score = score + 0.25 × (change - score)` and requires both the instantaneous change and smoothed score to exceed the threshold for three consecutive comparisons.

As a result, a brief spike can alarm the firmware without producing a MATLAB warning. MATLAB learns its threshold during calibration, but does not automatically learn whether a warning was correct, change the ESP32 thresholds or send alarm/clearance commands.

## 8. Testing and verification

`updateMockControls()` reads BOOT and serial commands, while `updateReportedReadings()` selects physical or simulated sender readings. Holding BOOT after startup reports 5 cm for both remote sensors; releasing it restores the selected mode, and `h` returns to hardware readings.

Serial commands allow either sensor to be simulated near (`1` or `2`), both near (`t`), both clear (`c`) or invalid (`i`). Additional commands run the repeating demo (`d`), pause/resume samples (`p`/`r`) and start/stop CSI telemetry (`m`/`n`), so fault and recovery paths can be exercised independently of object placement.

The host test runner compiles `system_test.cpp` against replacement hardware interfaces, allowing the application logic to run with controlled readings, clocks and radio events. The recorded review passed 9,557 assertions, including repeated checks within scenarios; this is not a count of independent physical experiments.

Firmware builds and MATLAB static lint also passed in that review. However, these checks do not establish physical detection accuracy, worst-case response time or MATLAB runtime behaviour, so those require separate measurements on the actual setup.
