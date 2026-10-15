# THE VAULT sender

Open this directory as a separate PlatformIO project. The four files in `src/` implement two physical ultrasonic sensors, the red LED, passive buzzer, ESP-NOW and CSI detection. Hardware sensing is the startup default.

Hold BOOT after startup for a simulated 5 cm intrusion on both remote sensors; release restores the selected mode. Serial `c` enables mock clearance, `h` restores physical sensing, `p` pauses transmissions and `r` resumes. Mock readings are test overrides, not physical evidence of clearance.

See the [project README](../README.md), [wiring and complete controls](../docs/HARDWARE.md), [learning guide](../docs/LEARNING_GUIDE.md) and [validation](../docs/VALIDATION.md). From this directory, use `pio run` and `pio run -t upload`. Flash the matching protocol-v4 receiver too.
