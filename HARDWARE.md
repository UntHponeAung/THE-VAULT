# Hardware and controls

Use two classic ESP32 development boards matching the `esp32dev` configuration. The assignments below are GPIO numbers; labels vary by board. On the commonly used ESP32-WROOM boards, RX2 is GPIO16 and TX2 is GPIO17. HC-SR04 uses trigger/echo pulses, not a UART connection.

## Receiver

| Component / signal | GPIO / configuration |
|---|---|
| HC-SR04 trigger | 16 |
| HC-SR04 echo | 17, through voltage divider |
| Passive piezo buzzer signal | 27, PWM channel 4 |
| Joystick X / Y / button | 34 / 35 / 21 |
| Joystick supply | 3.3 V |
| ILI9488 display | 480 × 320, parallel 8-bit TFT_eSPI |
| Display CS / DC / RST / WR | 33 / 32 / 26 / 25 |
| Display RD | Disabled in configuration (`-1`); follow the display module's wiring requirements |
| Display D0 / D1 / D2 / D3 | 4 / 5 / 13 / 14 |
| Display D4 / D5 / D6 / D7 | 18 / 19 / 22 / 23 |

Display power and backlight requirements depend on the actual module. Retain your verified wiring and supply; the GPIO table alone does not specify its power circuit.

## Sender

| Component / signal | GPIO / configuration |
|---|---|
| HC-SR04 sensor 1 trigger / echo | 16 / 17 |
| HC-SR04 sensor 2 trigger / echo | 18 / 19 |
| Red LED | 25, through a series resistor; 1 kΩ is suitable for the existing indicator setup |
| Passive piezo buzzer signal | 27, PWM channel 4 |
| Built-in BOOT button | 0, active low |

Each HC-SR04 needs a 5 V supply and a common ground with its ESP32. Give **each echo its own divider**: echo output → 1 kΩ → GPIO junction → 2 kΩ → ground. Nominally, 5 V × 2/(1+2) = 3.33 V at the GPIO junction. Check actual supply and resistor values. Do not connect 5 V echo directly to an ESP32 input. Larger buzzers require a suitable driver; this code targets the existing passive piezo setup.

## Controls and test modes

Receiver joystick: short press acknowledges a latched alarm; otherwise it toggles diagnostics. Hold one second to open settings. Alarm acknowledgment takes priority over settings. Up/down selects distance, sound, X reversal or Y reversal; left/right changes the selection. Press saves and exits. Holding at startup runs joystick calibration before security monitoring starts.

The sender's BOOT button overrides both reported sensors with valid 5 cm test readings while held after boot. Release restores hardware or the selected mock mode. The ultrasonic confirmation timer still applies; this is not a direct siren command.

| Sender serial command | Result |
|---|---|
| `h` | Physical sensor mode, the startup default |
| `1` / `2` | Mock intrusion from sensor 1 / sensor 2 |
| `t` | Mock intrusion from both sensors |
| `c` | Mock clearance |
| `i` | Mock invalid readings |
| `d` | Timed mock demonstration |
| `p` / `r` | Pause / resume sample transmission for link-fault testing |
| `?` | Print available commands |
| `m` / `n` | Start / stop CSI CSV telemetry |

Check `printCommands()` in `Sender/src/sensors.cpp` for the precise behavior of the test modes. Close a serial monitor before MATLAB opens that same USB port.

The LED briefly blinks while healthy, flashes during an alarm, stays on while waiting for clearance, and indicates link loss with a faster periodic blink. Both buzzers follow the receiver's mute setting and alarm state; radio delivery means they are not acoustically sample-synchronized.
