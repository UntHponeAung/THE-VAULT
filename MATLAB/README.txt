THE VAULT - MATLAB CSI MOTION ANALYSIS

WHAT IT DOES

Each ESP32 sends 24 measured CSI amplitudes from its own radio receiver over USB.
MATLAB normalizes these amplitudes, calibrates a baseline, and detects repeated
changes independently; it does not just display the ESP32's motion flag.

The ESP32 captures CSI; MATLAB performs the separate analysis. This does not
measure distance or position, and the MATLAB result never commands the alarm,
acknowledges it or satisfies the firmware's clearance requirement.

REQUIREMENTS

Use MATLAB R2019b or newer with serialport and ordinary MATLAB graphics.
The scripts do not call Wi-Fi Toolbox, Signal Processing Toolbox, Simulink,
Arduino Support Package or Python; both boards need the supplied v4 firmware.

STEPS

1. Upload both projects and verify their ordinary wireless connection first.
   Complete any joystick calibration on the LCD, and keep both boards powered.
2. Connect the board(s) you want to analyse to your computer by USB.
   Close PlatformIO Serial Monitor and any other program using those ports.
3. In MATLAB, set Current Folder to this MATLAB folder.
4. Run this synthetic detector/parser check (no hardware required):

       test_csi_motion

5. List your actual USB ports:

       serialportlist("available")

6. Capture one board for 120 seconds, substituting YOUR actual port:

       files = capture_csi("/dev/cu.usbserial-0001", 120);

   Or capture both simultaneously, main port first and sender port second:

       files = capture_csi(["/dev/cu.usbserial-0001", ...
                            "/dev/cu.usbserial-0002"], 120);

   On Windows use names such as "COM3" and "COM4" from serialportlist.
   Node 1 and Node 2 follow the order you supply, not automatic board identification.

7. Keep the room and both boards still for the first 10 seconds OF LIVE DATA.
   Then walk across the radio path, pause, and repeat at marked positions;
   avoid the ultrasonic boundary if you want to observe CSI warnings alone.
8. Watch the plot title change from CALIBRATING to READY or MOTION WARNING.
   The blue line is MATLAB's score, dashed line its threshold, dotted line
   the ESP32 score. MATLAB smooths its score; the firmware score is instantaneous.
   Their thresholds and calibration start times can also differ (see below).
9. Close the plot to stop early, or allow the duration to finish.
   Files are saved in MATLAB's Current Folder with a timestamp and node suffix.
10. Replay one recording later:

       result = analyse_csi(files(1));

   Or try the included unlabelled recording without either board connected:

       result = analyse_csi("data/example_unlabelled.csv");

   To return the same table without opening a plot:

       result = analyse_csi(files(1), false);

   result is a table of score, threshold and motion over time. The MatlabMotion
   column is NaN during calibration and after missing data, not evidence of
   no movement. Scores and provisional thresholds can still appear during calibration.

THE TWO DETECTORS ARE DELIBERATELY DIFFERENT

The uploaded settings have been preserved:

  Firmware (both ESP32 boards): minimum threshold 2.0, noise multiplier 2.0.
  Its score is the instantaneous normalized shape change. One valid calibrated
  sample above threshold can start the firmware alarm while the guard is armed.

  MATLAB: minimumThreshold = 10.0 and noiseMultiplier = 10.0 in csi_motion_step.m.
  It smooths the score with score += 0.25 * (change - score), then requires
  three consecutive samples where BOTH raw change and the smoothed score
  exceed threshold. MATLAB is therefore substantially more conservative here.

Both calculate threshold = max(minimumThreshold, noiseMean + multiplier * noiseStd)
after 10 seconds and at least 80 comparisons. The learned threshold is fixed
until a reset, new boot, or data gap requires calibration again. Raising the
floor only matters when it exceeds the learned noise threshold; raising the
multiplier changes the margin above the measured calibration noise.

Changing MATLAB does not change the ESP32 thresholds or alarm. Tune one setting
at a time using labelled stationary/movement intervals; the supplied recordings
have no movement labels and cannot establish an optimal threshold. Current
10/10 settings produced no warnings in an independent Python arithmetic replay
of those recordings; this is not MATLAB runtime validation or proof of accuracy.
See ../docs/CSI_DATA_REVIEW.md for the method and complete capture inventory.

USB CAPTURE DETAILS

capture_csi automatically sends m to enable streaming and n when it finishes.
Opening a USB port may reset an ESP32, so the command is retried during startup;
this is why the ordinary firmware should be tested before starting MATLAB.

Logging uses a fixed 512-byte record and a UART transmit buffer allocated once
at startup. The firmware skips records when busy; dropped counts and sequence
numbers expose gaps instead of delaying the alarm or fabricating measurements.

The CSV contains: CSI1 marker, boot ID, record sequence, device milliseconds,
firmware state, firmware score, threshold, dropped-record count, and 24 amplitudes.
The selected LLTF bin order is 4,6,...,26 then 38,40,...,60 (zero-based); these are
unnormalized magnitudes sqrt(real^2 + imaginary^2), not complete complex CSI.

Firmware states in this stream: 2=CALIBRATING, 3=READY, 4=MOTION.
No usable data means no amplitude row; the live display marks the detector
unavailable after a 350 ms gap and requires a new calibration on recovery.

FUNCTIONS TO READ

capture_csi.m opens one/two USB ports, saves validated records and plots live scores.
parse_csi_line.m rejects ordinary console output, malformed rows and impossible values.
csi_motion_step.m contains the independent normalization/calibration/motion detector.
analyse_csi.m replays a saved CSV and returns the analysis table.
test_csi_motion.m supplies synthetic data to check the detector and parser.

RELIABLE EXPERIMENTS

Record stationary and walking sessions separately, with the boards fixed and
people/fans documented. Use known movement intervals to count warnings inside
and outside them; a stronger graph alone is not proof of accuracy or range.

The MATLAB analysis calibrates when capture begins, which may be later than the
firmware's calibration. Keep still again after a dropped-data period or board
reset; USB samples are rounded to three decimals, so near-threshold scores may differ.

Live capture and offline replay discard duplicate or reordered sequence numbers
within the current boot, while accepting uint32 sequence wrap. Offline replay
uses device timestamps; across a new boot it inserts a labelled
0.35-second separation because the actual power-off duration is unknown.
The two boards' plots do not provide synchronized phase or location estimates.

VALIDATION STATUS

The five revised MATLAB files passed MISS_HIT syntax/lint checks in MATLAB 2019b
language mode during the 15 October 2026 review. Neither MATLAB nor Octave was
available in the review environment: test_csi_motion, serial connection and
graphics have not been executed here. Run test_csi_motion on your PC first.

The uploaded synthetic test expected a hardcoded floor of 2.5 despite the
detector's current floor of 10.0. It now reads the detector's initial floor,
checks that zero-noise calibration preserves it, and covers empty CSV fields,
out-of-range dropped counts, and duplicate/reordered/wrapped replay sequences.
Supplied CSV records show that data was captured previously; they do not verify
these revised scripts on hardware or label when physical movement occurred.

API REFERENCES

MathWorks serialport:
https://www.mathworks.com/help/matlab/ref/serialport.html
MathWorks serial read:
https://www.mathworks.com/help/matlab/ref/serialport.read.html
Espressif ESP32 CSI / Wi-Fi guide for the pinned framework generation:
https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-guides/wifi.html#wi-fi-channel-state-information
