# THE-VAULT
**Dual Node ESP32 Intrusion detection with ultrasonic sensing, experimental WIFI CSI and MATLAB analysis**

Inspired by a jewel heist in *Lupin*, THE VAULT protects an animated diamond on an LCD. Two ESP32s exchange sensor and alarm information over ESP-NOW. Three ultrasonic sensors detect nearby objects and two radios independently detect changes in Wi-Fi Channel State Information (CSI). Any intrusion would start the sirens and retracts a virtual diamond into an animated vault shown on an ILI9488 3.5" TFT LCD Display.

Note: CSI indicates changes in the radio channel, this implementation does not locate a person, guarantee room coverage, or enforce a sensing radius.

## Demo
[Watch THE VAULT demo on YouTube](https://youtube.com/shorts/hcL8IggtzgA)

 
## What is implemented?

- Receiver Node: LCD, one ultrasonic sensor, joystick, buzzer, saved settings and diagnostics.
- Sender Node: two ultrasonic sensors, red LED, buzzer, BOOT-button intrusion test and serial test controls.
- Automatic arming, latched alarms, acknowledgment, confirmed clearance and reversible vault animation.
- ESP-NOW protocol v4: per-sensor sequence numbers and ages, application acknowledgments and link monitoring.
- CSI calibration and detection on both boards and uses MATLAB to independently capture, plot and analyse the same selected amplitudes.
- Cooperative main loops, short interrupt/radio callbacks, fixed-size buffers and one startup display allocation.

## Start here


| Task                                    | Read                                                     |
| --------------------------------------- | -------------------------------------------------------- |
| Wire the boards                         | [Hardware and controls](docs/HARDWARE.md)                |
| Understand the state machines and radio | [Architecture](docs/ARCHITECTURE.md)                     |
| Find out how the code works             | [The Code](docs/CODE_EXPLANATION.md)                     |
| See what was actually tested            | [Validation](docs/VALIDATION.md)                         |
| Test Results                            | [Results](docs/TEST_RESULTS.md)                          |   
| Connect MATLAB                          | [MATLAB](MATLAB/README.txt)                 |

## Build and upload

`Receiver/` and `Sender/` are independent PlatformIO projects. Open the appropriate folder in VS Code with PlatformIO. Upload **all source files in that project**, not only `main.cpp`. Both projects retain their uploaded `platformio.ini` unchanged: `esp32dev`, Arduino, `espressif32@6.9.0`; the receiver uses `TFT_eSPI@2.5.43`.

From the repository root, with PlatformIO installed:

```bash
pio run -d Receiver
pio run -d Sender
pio run -d Receiver -t upload
pio run -d Sender -t upload
```

If both boards are connected, add `--upload-port YOUR_PORT` to each upload command. Power both boards and keep the room and boards still for at least ten seconds of usable CSI after linking. The ultrasonic guard arms after its separate 1,200 ms grace period.

## Controls

Press the receiver joystick to acknowledge an alarm. It silences the sirens and keeps the vault closed until clearance. In the normal view, a short press toggles diagnostics; a one-second hold opens settings. Move up/down to select a setting and left/right to change it; press to save and leave. Hold the joystick during startup to recalibrate.

Hold the sender BOOT button **after startup** to report a test intrusion from both remote sensors. Releasing it restores the selected input mode. Physical sensors are the default. See the hardware guide for serial test commands.

## CSI and MATLAB use different detectors


| Setting                      | Both ESP32s                         | MATLAB                               |
| ---------------------------- | ----------------------------------- | ------------------------------------ |
| Minimum threshold            | 2%                                  | 10%                                  |
| Calibration noise multiplier | 2                                   | 10                                   |
| Score                        | Instantaneous shape change          | Exponential average, weight 0.25     |
| Confirmation                 | One processed sample over threshold | Three consecutive qualifying samples |
| Motion hold                  | 2 seconds                           | 2 seconds                            |

## Boundaries

Broadcast communication is unencrypted and unauthenticated. When CSI is unavailable, acknowledged recovery can use confirmed ultrasonic clearance, so it does not require fresh, calibrated CSI from both nodes. A missing remote link  seals the vault silently and requires acknowledgment plus restored valid clearance.


