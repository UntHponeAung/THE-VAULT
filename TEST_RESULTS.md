# THE VAULT — Test results

This record separates successful firmware uploads from physical behaviour. The build and upload results below come from the supplied terminal logs, while the remaining sections are left for observations from the assembled system.

## 1. Test setup

| Item | Record |
| --- | --- |
| Test date and time | [fill in] |
| Tester | [fill in] |
| Receiver commit / local changes | [fill in — the upload log does not identify the source commit] |
| Sender commit / local changes | [fill in] |
| MATLAB commit / local changes, if used | [fill in] |
| Alarm boundary | [fill in] cm |
| Sound setting | [enabled / muted] |
| Receiver and sender power supplies | [fill in] |
| Distance between boards | [fill in] m |
| Board positions and antenna orientation | [fill in] |
| Local / remote 1 / remote 2 clear-target distances | [fill in] cm / [fill in] cm / [fill in] cm |
| Environment | [people, fans, nearby movement and other relevant conditions] |
| Setup photograph / wiring reference | [filename or link] |
| Physical-test video | [filename or link] |

Keep the boards stationary and wait until both CSI detectors show ready before normal intrusion trials. Give all three ultrasonic sensors valid distant targets: a missing echo does not count as clearance. After each trial, acknowledge and restore the normal armed/displaying state before starting the next independent trial.

## 2. Build and upload — completed

Evidence: PlatformIO logs supplied on **15 October 2026**. Both logs end with `[SUCCESS]`, verified flash-write hashes and a reset command; however, this does not establish successful application startup or sensor operation.

| Item | Receiver | Sender |
| --- | --- | --- |
| Build and upload | **PASS** | **PASS** |
| ESP32 MAC reported by esptool | `8c:94:df:4d:4e:10` | `8c:94:df:57:57:7c` |
| Chip | ESP32-D0WD-V3, revision v3.1 | ESP32-D0WD-V3, revision v3.1 |
| Board / platform | `esp32dev` / `espressif32@6.9.0` | `esp32dev` / `espressif32@6.9.0` |
| Arduino framework package | `3.20017.241212+sha.dcc1105b` | `3.20017.241212+sha.dcc1105b` |
| esptool | 4.5.1 | 4.5.1 |
| Xtensa toolchain | `8.4.0+2021r2-patch5` | `8.4.0+2021r2-patch5` |
| TFT_eSPI | 2.5.43 | Not used |
| Serial upload port shown | `/dev/cu.usbserial-0001` | `/dev/cu.usbserial-0001` |
| Static RAM reported | 48,948 / 327,680 bytes (14.9%) | 45,812 / 327,680 bytes (14.0%) |
| Program flash usage reported | 826,037 / 1,310,720 bytes (63.0%) | 752,249 / 1,310,720 bytes (57.4%) |
| Firmware image bytes written | 832,608 bytes | 758,832 bytes |
| Total PlatformIO task time | 25.25 s | 51.38 s |
| Upload-log file | [save log and enter filename] | [save log and enter filename] |

The different MAC addresses identify two different ESP32s despite the repeated serial-port name. Static RAM figures are build-time allocations, not measured runtime free memory, and flash percentages use the application partition rather than the entire 4 MB chip. The task times are build/upload times, not alarm response times.

**Receiver warning:** `TOUCH_CS pin not defined`. TFT_eSPI reports that its touch functions are unavailable; this is expected for the joystick-controlled design and did not block the build. Actual LCD operation remains a physical test below.

## 3. Physical test checklist

Use **Not run**, **Pass**, **Fail** or **Inconclusive**. Replace each status only after recording an observation, and use the attempt log below for evidence. Repeat the three ultrasonic trigger tests and the acknowledgment/recovery tests three times where practical.

| ID | Test action | Expected behaviour | Result |
| --- | --- | --- | --- |
| T01 | Start both boards and finish any joystick calibration | LCD scene appears, radio link becomes fresh, guard arms and both CSI detectors eventually show ready | Not run |
| T02 | Bring a target inside the local ultrasonic boundary | Local sensor is identified; vault seals and both buzzers sound when enabled | Not run |
| T03 | Bring a target inside remote sensor 1's boundary | Remote sensor 1 is identified; vault seals and both buzzers sound | Not run |
| T04 | Repeat for remote sensor 2 | Remote sensor 2 is identified; vault seals and both buzzers sound | Not run |
| T05 | Remove the target after an alarm without acknowledging | Alarm remains latched and vault stays sealed | Not run |
| T06 | Acknowledge while the diamond is still retracting or shutters are closing | Both sirens stop after the status update, but sealing completes and the diamond does not reappear early | Not run |
| T07 | Acknowledge while a target remains close | Sirens stop, but vault remains sealed; sender LED indicates waiting for clearance | Not run |
| T08 | After acknowledgment, provide valid clearance on all three sensors and allow CSI warnings to end | System rearms and vault reopens only once recovery conditions are satisfied | Not run |
| T09 | After acknowledgment, make one sensor return an invalid/missing echo | Invalid readings prevent clearance; record which sensor was tested and repeat for the others | Not run |
| T10 | Introduce a new intrusion while the vault is reopening | Alarm returns and animation reverses towards sealing | Not run |
| T11 | Trigger an intrusion with settings open | Monitoring continues and the alarm scene takes over | Not run |
| T12 | Mute sound, then trigger an intrusion | Both buzzers stay silent, MUTE is indicated and visual sealing still occurs | Not run |
| T13 | While armed, switch off the sender, then restore it | Receiver seals silently on link loss; reconnection alone does not bypass acknowledgment and valid clearance | Not run |
| T14 | Start an audible alarm, then switch off the receiver while leaving the sender powered | Sender retains its commanded alarm; record recovery after the receiver restarts | Not run |
| T15 | During CSI trials, observe an event attributed to main CSI while all ultrasonic readings remain clear | Receiver reports the CSI cause, seals and commands both sirens when sound is enabled | Not run |
| T16 | Repeat for an event attributed to remote CSI | Remote CSI can produce the same shared alarm response | Not run |
| T17 | Hold and release sender BOOT after startup | Both reported remote distances become 5 cm while held; release restores the selected mode, but does not acknowledge the alarm | Not run |
| T18 | Change distance/sound settings, save, then restart the receiver | Saved settings return; restore the chosen test settings afterwards | Not run |

For T02–T04, record the displayed cause. If CSI triggers first, the ultrasonic trigger test is **inconclusive**, not a demonstrated pass. Similarly, T15/T16 require evidence of the named detector: record a combined CSI event separately rather than treating it as an isolated detector test.

During every sealing test, also check that the diamond disappears fully and never shows through closed shutters. Acknowledged recovery normally needs valid readings at least 5 cm beyond the boundary for 600 ms on all three sensors, with no active CSI warning; unavailable or calibrating CSI currently permits ultrasonic-only recovery.

### Attempt log — add a row for each run

| Test / attempt | Starting state and action | Displayed cause / distances | Actual observation | Result | Video time / log / photo |
| --- | --- | --- | --- | --- | --- |
| [T02 / 1] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| [T02 / 2] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| [T02 / 3] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| [test / attempt] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |

## 4. CSI observations

Keep ultrasonic readings valid and beyond the clearance boundary, then record a stationary period and repeated movement along one marked path. Record both detectors independently where possible; their radio paths are related, so a physical movement may affect either or both.

### Stationary period

| Item | Record |
| --- | --- |
| Usable observation duration | [fill in — aim for at least 5 minutes] |
| Main / remote CSI thresholds after calibration | [fill in] / [fill in] |
| Alarm activations during intended stationary time | [count and timestamps] |
| Unplanned movement, acknowledgment or recalibration | [times and details; exclude these from the stationary interval] |
| CSV / serial log / video evidence | [fill in] |

An alarm during intended stationary time is an observed unwanted alarm under those conditions. Count new alarm activations rather than every row containing a held motion flag; a latched alarm cannot generate further distinct activations until it has been acknowledged and the system has rearmed. If it stays latched, record the time to first alarm rather than claiming the remaining period tested new activations.

### Movement trials

- Movement path, speed and distance from each board: [fill in]
- Definition of a detected trial / observation window: [fill in before testing]
- Timing reference used to label movement: [video time / capture time / other]
- How that reference is matched to the CSV: [fill in, if applicable]

| Trial | Movement start/end | Main CSI warning | Remote CSI warning | Alarm cause | MATLAB warning, if recorded | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | [fill in] | [yes/no/unknown] | [yes/no/unknown] | [fill in] | [fill in] | [fill in] |
| 2 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| 3 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| 4 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| 5 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| 6 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| 7 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| 8 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| 9 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |
| 10 | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] | [fill in] |

Main detections: [ ] / [ ] valid trials. Remote detections: [ ] / [ ] valid trials. Trials with either detector: [ ] / [ ] valid trials. These results describe this setup and path, not guaranteed room coverage or a general accuracy figure.

## 5. MATLAB checks — optional for the first hardware session

| Check | Observation / evidence | Result |
| --- | --- | --- |
| Run `test_csi_motion` | [command output] | Not run |
| Capture live CSI after closing other serial monitors | [ports, board identities, duration and CSV filenames] | Not run |
| Confirm calibration and live score/threshold plotting | [plot image and movement labels] | Not run |
| Replay a saved CSV with `analyse_csi` | [file and returned result / plot] | Not run |

The provided detector settings are firmware minimum/multiplier **2/2** and MATLAB **10/10**. Record any changes: [fill in]. MATLAB also smooths and confirms motion differently, so agreement with every firmware alarm is not the pass criterion.

## 6. Timing measurements — only fill in if measured

| Measurement | Start event → end event | Method / frame rate / resolution | Attempts and measured values |
| --- | --- | --- | --- |
| Ultrasonic response | Target crosses marked boundary → receiver alarm begins | [fill in] | [fill in] |
| Sealing duration | Visible retraction begins → shutters fully close | [fill in] | [fill in] |
| Buzzer onset difference | Receiver buzzer begins → sender buzzer begins | [how each buzzer was distinguished] | [fill in] |
| Clearance to reopening | Last required valid-clear condition is established → opening begins | [how the start condition was observed] | [fill in] |

The 140 ms intrusion confirmation, 420 ms diamond travel and 260 ms shutter travel are configured timings, not measurements. If a start event cannot be identified reliably, write **not measured** rather than estimating a precise latency.

## 7. Issues and outcome

| Issue | Conditions and evidence | Change made, if any | Retest result |
| --- | --- | --- | --- |
| [fill in, or none observed] | [fill in] | [fill in] | [fill in] |

- Physical checks passed: [ ]
- Physical checks failed: [ ]
- Inconclusive / not run: [ ]
- Main result supported by the evidence: [fill in]
- Remaining limitations and next test: [fill in]

Keep failed attempts in this record. If firmware or thresholds change, record the new revision and settings before retesting so the results remain traceable.
