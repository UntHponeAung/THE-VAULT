# THE VAULT receiver

Open this directory as a PlatformIO project. Build and upload all five files in `src/`; they form one receiver program. The receiver controls the display, local ultrasonic sensor, joystick, buzzer and authoritative alarm state.

Start with the [project README](../README.md), [wiring](../docs/HARDWARE.md) and [learning guide](../docs/LEARNING_GUIDE.md). Validation results are in [docs/VALIDATION.md](../docs/VALIDATION.md). The uploaded configuration, pins, preference namespace and calibration keys are preserved.

From this directory: `pio run`, then `pio run -t upload`. From the repository root, use `pio run -d Receiver` instead. Flash the matching protocol-v4 sender too.
