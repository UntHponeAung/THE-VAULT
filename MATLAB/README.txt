THE VAULT - MATLAB CSI motion analysis

I use MATLAB to record the CSI measurements from the ESP32s, plot the signal
changes and run a separate motion detector. Each connected board sends 24 CSI
amplitudes over USB, while the two boards continue communicating through ESP-NOW.

Both ESP32s already calibrate and detect motion without MATLAB. MATLAB performs
its own calibration, so its threshold and warnings can differ from the firmware.
It does not control the alarm or change either board's threshold.

What you need

Use MATLAB R2019b or newer and the four .m files in this folder. No additional
MathWorks toolbox is required by these scripts. Both ESP32s need matching v4
firmware and must remain powered, but only the board you want to record needs
a USB connection to the computer. To record both detectors, connect both boards
by USB; one USB connection does not capture the other board's amplitude stream.

How to run it

1. Check the two boards first.

   Upload the receiver and sender firmware, finish any joystick calibration,
   and check that their wireless connection works. Place both boards where you
   want them and leave them fixed. If the link keeps dropping or CSI repeatedly
   restarts calibration, record and investigate that before collecting a
   stationary baseline.

2. Connect USB and close the Serial Monitor.

   Connect one or both boards to your computer. Then close PlatformIO Serial
   Monitor and any other program using those serial ports, because MATLAB needs
   to open them. Keep the other board powered if you are recording only one.

3. Open the MATLAB folder.

   Open MATLAB and set Current Folder to this repository's MATLAB folder.
   It should contain capture_csi.m, parse_csi_line.m, csi_motion_step.m and
   analyse_csi.m. Type the commands below in the Command Window; you do not
   need to put your port name inside a script.

4. Find the correct USB port.

       serialportlist("available")

   Use the port belonging to the board you want to record. On a Mac it may look
   like /dev/cu.usbserial-0001, while on Windows it may be COM3. These are
   examples, so use the names returned on your computer. If you are unsure
   which board is which, identify them one at a time before the recording,
   then reconnect both and check the port names again.

5. Start the recording.

   For one board, replace the example port with your own:

       files = capture_csi("/dev/cu.usbserial-0001", 120);

   This records for 120 seconds, including time spent waiting for data and
   calibrating. The function starts USB streaming automatically, so there is
   no need to send the m command yourself.

   For both boards, supply two different ports, with the receiver first:

       files = capture_csi(["/dev/cu.usbserial-0001", "/dev/cu.usbserial-0002"], 120);

   Node 1 is the first port in the command and Node 2 is the second. The program
   does not identify their roles automatically, so note which port belongs to
   which board. Change 120 to a longer duration if needed; for a five-minute
   stationary observation, allow extra time for calibration and settling.

6. Wait for calibration to finish.

   Opening USB may restart a board, so allow it to reconnect and leave the
   surroundings still while MATLAB shows CALIBRATING. MATLAB needs at least
   ten seconds of usable data and 80 comparisons; wait for READY rather than
   assuming calibration has finished after exactly ten seconds.

   Each board calibrates separately from MATLAB. Also, an interruption in
   usable CSI data can restart calibration, so avoid moving or unplugging
   boards once the recording has started.

7. Observe the graph and record your movements.

   The blue line is MATLAB's smoothed motion score. The dashed line is MATLAB's
   threshold, while the dotted grey line is the connected ESP32's score.
   Therefore, the dashed line is not the ESP32's alarm threshold.

   First record a period with no intentional movement. Then walk along a marked
   path, pause and repeat, noting when each movement starts and ends. Keep the
   ultrasonic sensors on valid targets beyond their clearance boundary if you
   want to examine CSI without also triggering an ultrasonic alarm.

   MATLAB shows MOTION WARNING when its own detection conditions are met.
   A firmware alarm can happen without that warning because the two detectors
   use different rules. Keep these outcomes separate in your notes.

8. Stop and keep the recording.

   Let the chosen duration finish, or close the plot window to stop early.
   The function then stops streaming, closes the serial connections and returns
   the CSV filenames in files. Recordings are saved in MATLAB's Current Folder
   with a timestamp and a node number, so keep them alongside your movement notes.

9. Replay the saved data.

   In the same MATLAB session, run:

       result = analyse_csi(files(1));

   If you captured both boards, use files(2) for the second one. In a later
   session, replace the example below with the actual saved filename:

       result = analyse_csi("your_recording.csv");

   You can also open the included example without connecting either board:

       result = analyse_csi("data/example_unlabelled.csv");

   To return the results table without opening a plot, use:

       result = analyse_csi(files(1), false);

   The table includes MATLAB's score, threshold and motion result, alongside
   the recorded firmware score and motion status. A NaN motion result means
   the detector was not ready, rather than confirming there was no movement.

How the detector works

For each sample, MATLAB divides the 24 amplitudes by their mean and compares
this normalized pattern with the previous sample. The mean absolute difference,
multiplied by 100, becomes the instantaneous change score. Normalization reduces
uniform receiver-gain changes, although it does not remove every source of noise.

During calibration, the detector calculates a threshold from the average change
and its standard deviation:

    threshold = max(minimumThreshold, noiseMean + noiseMultiplier * noiseStd)

In csi_motion_step.m, minimumThreshold and noiseMultiplier are both 10.0.
The minimum is only a floor, so the learned threshold can be higher. Once
calibration finishes, the threshold stays fixed until a data gap or reset
requires a new calibration; it does not keep learning which warnings are correct.

MATLAB smooths the score using:

    score = score + 0.25 * (change - score)

Both the instantaneous change and the smoothed score must exceed the threshold
for three consecutive comparisons. The motion indication is then held for two
seconds and extended if further qualifying comparisons occur.

The supplied ESP32 firmware uses a minimum of 2.0 and multiplier of 2.0 instead,
and one processed above-threshold sample can trigger its alarm after calibration
while the guard is armed. As a result, MATLAB applies stricter detection rules,
but whether either detector performs well still depends on the recorded data.

To adjust MATLAB, change minimumThreshold or noiseMultiplier in csi_motion_step.m
and replay the same labelled recording. Change one value at a time so you can
compare its effect. This does not change the C++ firmware or its alarm settings.

What each file does

capture_csi.m opens the USB ports, saves valid records and plots live results.
parse_csi_line.m checks incoming lines and rejects malformed records.
csi_motion_step.m performs normalization, calibration and motion detection.
analyse_csi.m runs the detector over a saved recording and returns the results.

If something does not look right

If MATLAB cannot find capture_csi, check Current Folder and confirm that all
four files are present. If it cannot open a port, check the selected port name
and close any Serial Monitor or earlier capture still using that connection.

If the graph stays at CALIBRATING or returns to it, check that both boards remain
powered and the wireless link is stable. A gap of 350 ms or more requires MATLAB
to recalibrate, and NO CSI DATA means measurements are unavailable, not that
there is no movement. Keep still again when usable data returns.

The CSV includes the boot ID, sequence, device time, firmware state, score,
threshold, skipped USB-record count and 24 amplitudes. Skipped records show that
some measurements were not logged; live capture and replay also reject duplicate
or out-of-order records rather than treating them as new samples.

Use the recorded firmware_threshold column if you need the ESP32's threshold
from that capture. Its value is provisional while the firmware state is 2
(calibrating); states 3 and 4 mean ready and motion respectively.

CSI indicates changes in the radio path, so these plots do not give a person's
position or a guaranteed sensing radius. Keep the setup and movement times in
your notes, since a large peak alone does not establish what caused it. The
included example has no movement labels, so it is useful for trying replay,
but cannot establish detection accuracy.

The scripts passed static syntax checks during the code review. MATLAB itself
was not available there, so that review did not verify live capture or plots.
