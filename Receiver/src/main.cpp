// Receiver: sensors, alarm controls and the animated vault.
// A calibrated CSI crossing or confirmed ultrasonic intrusion latches the alarm.
// After acknowledgement, all three sensors must clear and active CSI warnings end.
// CSI unavailability permits ultrasonic-only recovery; see docs/REVIEW.md.
// Use the matching v4 sender: ESP-NOW broadcasts are unencrypted on channel 1.
// The vault is only an animation. Saved settings retain the pixel-pals namespace.

#include "firmware.h"
#include <Preferences.h>
#include <esp_system.h>
#include <math.h>

// Settings and input state

Preferences savedSettings;
int guardDistanceCm = 30;
bool isSoundEnabled = true;
bool reverseXAxis = DEFAULT_REVERSE_X_AXIS;
bool reverseYAxis = DEFAULT_REVERSE_Y_AXIS;
bool isSettingsScreenOpen = false;
bool isDiagnosticsScreenOpen = false;
int selectedSettingsRow = 0;
uint32_t nextAnimationFrameMs = 0;

struct JoystickCalibration
{
    int minimumReading;
    int centreReading;
    int maximumReading;
};

JoystickCalibration xAxisCalibration = {0, 2048, 4095};
JoystickCalibration yAxisCalibration = {0, 2048, 4095};
bool hasValidCalibration = false;

// Startup

// Initialise hardware once; allocation failure stops startup on the error screen.
void setup()
{
    // Serial output and GPIO configuration

    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
    pinMode(JOYSTICK_X_PIN, INPUT);
    pinMode(JOYSTICK_Y_PIN, INPUT);
    pinMode(ULTRASONIC_TRIGGER_PIN, OUTPUT);
    digitalWrite(ULTRASONIC_TRIGGER_PIN, LOW);
    pinMode(ULTRASONIC_ECHO_PIN, INPUT);

    // Joystick ADC configuration

    analogReadResolution(12);
    analogSetPinAttenuation(JOYSTICK_X_PIN, ADC_11db);
    analogSetPinAttenuation(JOYSTICK_Y_PIN, ADC_11db);

    // Passive buzzer PWM configuration

    ledcSetup(BUZZER_PWM_CHANNEL, 2000, 10);
    ledcAttachPin(BUZZER_SIGNAL_PIN, BUZZER_PWM_CHANNEL);
    ledcWriteTone(BUZZER_PWM_CHANNEL, 0);

    // Physical display configuration

    display.init();
    display.setRotation(1);
    display.setTextWrap(false);
    display.fillScreen(BACKGROUND_COLOR);

    // Restore settings and calibrate if required

    loadSettings();
    if (digitalRead(JOYSTICK_BUTTON_PIN) == LOW || !hasValidCalibration)
    {
        calibrateJoystick();
    }

    // Allocate the scene buffer and handle allocation failure

    sceneBuffer.setColorDepth(4);
    if (!sceneBuffer.createSprite(SCENE_WIDTH_PIXELS, SCENE_HEIGHT_PIXELS))
    {
        display.fillScreen(BACKGROUND_COLOR);
        drawDisplayText("Not enough display memory.", 35, 125, RED_COLOR, 2);
        drawDisplayText("Restart the board.", 35, 160, WHITE_COLOR, 2);
        Serial.println("Sprite allocation failed");
        while (true)
        {
            delay(1000);
        }
    }

    sceneBuffer.createPalette(SCENE_PALETTE, 16);

    // Enable Echo capture and show the startup screen

    attachInterrupt(digitalPinToInterrupt(ULTRASONIC_ECHO_PIN), captureEchoEdge, CHANGE);
    receiverSession = esp_random();
    if (receiverSession == 0)
    {
        receiverSession = 1;
    }
    csiTelemetrySession = receiverSession;
    initialiseWireless();
    uint32_t nowMs = millis();
    guardController.startArming(nowMs);
    vaultAnimation.reset(nowMs);
    drawScreenFrame();
    playStartupSound();
    Serial.println("THE VAULT ready. TRIG=16 ECHO=17 X=34 Y=35 SW=21 BUZZER=27");
}

// Main loop

// Sensing and alarms run on every pass; drawing is limited to its frame deadline.
void loop()
{
    // Service radio and sensor input before updating the interface.

    uint32_t nowMs = millis();
    bool menuWasOpen = isSettingsScreenOpen || isDiagnosticsScreenOpen;
    updateWirelessReceiver(nowMs);
    // Callbacks may arrive after the loop began. Age checks need a newer clock.
    nowMs = millis();
    updateLinkSafety(nowMs);
    updateCsiMotion(remoteSensor.isFresh(nowMs));
    nowMs = millis();
    updateSharedCsiWarning(nowMs);
    updateDistanceSensor(nowMs);
    for (uint8_t count = 0; count < 16 && Serial.available(); count++)
    {
        setCsiTelemetry(char(Serial.read()));
    }
    serviceCsiSerial();
    if (menuWasOpen && !isSettingsScreenOpen && !isDiagnosticsScreenOpen)
    {
        drawScreenFrame();
    }

    // Service input, vault animation and sound

    updateJoystickInput(nowMs);
    updateVaultAnimation(nowMs);
    updateBuzzer(nowMs);
    publishReceiverStatus(nowMs);

    // Draw the next complete exhibit frame

    if (!isSettingsScreenOpen && (int32_t)(nowMs - nextAnimationFrameMs) >= 0)
    {
        nextAnimationFrameMs = nowMs + (isDiagnosticsScreenOpen ? 200 : 40);
        if (isDiagnosticsScreenOpen)
        {
            drawDiagnosticsScreen(nowMs);
        }
        else
        {
            drawAnimatedScene(nowMs);
        }
    }

    // Print periodic serial diagnostics

    static uint32_t lastSerialLogMs = 0;
    if (!csiCsvEnabled && csiSerialLength == 0 && nowMs - lastSerialLogMs >= 1000)
    {
        lastSerialLogMs = nowMs;
        Serial.printf(
            "local=%.1f valid=%d remote=%s remote1=%.1f/%d remote2=%.1f/%d packets=%lu vault=%d guard=%d freeHeap=%u CSI=%s score=%.2f threshold=%.2f\n",
            filteredDistanceCm,
            hasValidDistance && isLatestDistanceSampleValid,
            getRemoteLinkLabel(nowMs),
            remoteSensor.sensors[0].distanceMillimetres / 10.0f,
            remoteSensor.sensors[0].readingValid && remoteSensor.sensors[0].isFresh(nowMs),
            remoteSensor.sensors[1].distanceMillimetres / 10.0f,
            remoteSensor.sensors[1].readingValid && remoteSensor.sensors[1].isFresh(nowMs),
            (unsigned long)acceptedRemotePackets,
            (int)vaultAnimation.currentState,
            (int)guardController.currentState,
            ESP.getFreeHeap(),
            csiStatusLabel(),
            csi.score,
            csi.threshold);
    }

    delay(1);
}

// Saved settings and calibration

bool isCalibrationValid(const JoystickCalibration &axisCalibration)
{
    return axisCalibration.minimumReading >= 0 && axisCalibration.maximumReading <= 4095
        && axisCalibration.centreReading - axisCalibration.minimumReading > 150
        && axisCalibration.maximumReading - axisCalibration.centreReading > 150;
}

// Keep these namespace/key names compatible with existing saved settings.
void saveSettings()
{
    if (!savedSettings.begin("pixel-pals", false))
    {
        return;
    }

    savedSettings.putInt("distance", guardDistanceCm);
    savedSettings.putBool("sound", isSoundEnabled);
    savedSettings.putBool("invx", reverseXAxis);
    savedSettings.putBool("invy", reverseYAxis);

    savedSettings.putInt("xmin", xAxisCalibration.minimumReading);
    savedSettings.putInt("xrest", xAxisCalibration.centreReading);
    savedSettings.putInt("xmax", xAxisCalibration.maximumReading);

    savedSettings.putInt("ymin", yAxisCalibration.minimumReading);
    savedSettings.putInt("yrest", yAxisCalibration.centreReading);
    savedSettings.putInt("ymax", yAxisCalibration.maximumReading);

    savedSettings.putBool("cal", hasValidCalibration);
    savedSettings.end();
}

// If Pixel Pals has no calibration, read the original arcade calibration.
void loadSettings()
{
    if (savedSettings.begin("pixel-pals", true))
    {
        guardDistanceCm = constrain(savedSettings.getInt("distance", 30), 10, 100);
        isSoundEnabled = savedSettings.getBool("sound", true);
        reverseXAxis = savedSettings.getBool("invx", DEFAULT_REVERSE_X_AXIS);
        reverseYAxis = savedSettings.getBool("invy", DEFAULT_REVERSE_Y_AXIS);
        hasValidCalibration = savedSettings.getBool("cal", false);
        xAxisCalibration = {savedSettings.getInt("xmin", 0),
            savedSettings.getInt("xrest", 2048),
            savedSettings.getInt("xmax", 4095)};
        yAxisCalibration = {savedSettings.getInt("ymin", 0),
            savedSettings.getInt("yrest", 2048),
            savedSettings.getInt("ymax", 4095)};
        savedSettings.end();
    }

    if (!hasValidCalibration && savedSettings.begin("arcade", true))
    {
        if (savedSettings.getBool("jcal", false))
        {
            xAxisCalibration = {savedSettings.getInt("jxmn", 0),
                savedSettings.getInt("jxr", 2048),
                savedSettings.getInt("jxmx", 4095)};
            yAxisCalibration = {savedSettings.getInt("jymn", 0),
                savedSettings.getInt("jyr", 2048),
                savedSettings.getInt("jymx", 4095)};
            hasValidCalibration = true;
        }

        savedSettings.end();
    }

    hasValidCalibration = hasValidCalibration && isCalibrationValid(xAxisCalibration)
        && isCalibrationValid(yAxisCalibration);
}

// Joystick calibration

// Startup calibration blocks; security monitoring begins after it finishes.
void calibrateJoystick()
{
    // Show instructions and wait for button release

    display.fillScreen(BACKGROUND_COLOR);
    drawDisplayText("JOYSTICK SETUP", 70, 35, CYAN_COLOR, 3);
    drawDisplayText("Release the button.", 65, 93, WHITE_COLOR);
    while (digitalRead(JOYSTICK_BUTTON_PIN) == LOW)
    {
        delay(5);
    }

    // Measure the full joystick travel

    drawDisplayText("Move stick in full circles", 65, 120, WHITE_COLOR);
    drawDisplayText("for 5 seconds...", 65, 147, MUTED_TEXT_COLOR);
    xAxisCalibration = {4095, 2048, 0};
    yAxisCalibration = {4095, 2048, 0};
    uint32_t calibrationStartedMs = millis();
    while (millis() - calibrationStartedMs < 5000)
    {
        int rawXReading = analogRead(JOYSTICK_X_PIN);
        int rawYReading = analogRead(JOYSTICK_Y_PIN);
        xAxisCalibration.minimumReading = min(xAxisCalibration.minimumReading, rawXReading);
        xAxisCalibration.maximumReading = max(xAxisCalibration.maximumReading, rawXReading);
        yAxisCalibration.minimumReading = min(yAxisCalibration.minimumReading, rawYReading);
        yAxisCalibration.maximumReading = max(yAxisCalibration.maximumReading, rawYReading);
        display.fillRect(65, 190, (millis() - calibrationStartedMs) * 350 / 5000, 12, CYAN_COLOR);
        delay(5);
    }

    // Measure the resting centre

    display.fillRect(30, 90, 420, 90, BACKGROUND_COLOR);
    drawDisplayText("Let go of the stick now.", 65, 115, GOLD_COLOR);
    delay(1200);
    long sumOfXReadings = 0;
    long sumOfYReadings = 0;
    for (int sampleIndex = 0; sampleIndex < 64; sampleIndex++)
    {
        sumOfXReadings += analogRead(JOYSTICK_X_PIN);
        sumOfYReadings += analogRead(JOYSTICK_Y_PIN);
        delay(4);
    }

    xAxisCalibration.centreReading = sumOfXReadings / 64;
    yAxisCalibration.centreReading = sumOfYReadings / 64;

    // Validate calibration and display the result

    hasValidCalibration =
        isCalibrationValid(xAxisCalibration) && isCalibrationValid(yAxisCalibration);
    display.fillRect(30, 210, 440, 75, BACKGROUND_COLOR);
    if (hasValidCalibration)
    {
        saveSettings();
        drawDisplayText("Calibration saved!", 65, 230, GREEN_COLOR);
    }
    else
    {
        xAxisCalibration = {0, 2048, 4095};
        yAxisCalibration = {0, 2048, 4095};
        drawDisplayText("Check joystick wiring.", 65, 218, RED_COLOR);
        drawDisplayText("Hold button at boot to retry.", 65, 245, MUTED_TEXT_COLOR);
    }

    delay(1000);
}

// Average two ADC reads, apply calibration/dead zone, then optional direction reversal.
float readJoystickAxis(int analogInputPin,
    const JoystickCalibration &axisCalibration,
    bool shouldReverseDirection)
{
    int averagedReading = (analogRead(analogInputPin) + analogRead(analogInputPin)) / 2;
    float normalizedPosition = averagedReading >= axisCalibration.centreReading
        ? float(averagedReading - axisCalibration.centreReading)
            / max(1, axisCalibration.maximumReading - axisCalibration.centreReading)
        : float(averagedReading - axisCalibration.centreReading)
            / max(1, axisCalibration.centreReading - axisCalibration.minimumReading);
    if (fabsf(normalizedPosition) < 0.18f)
    {
        return 0;
    }

    normalizedPosition = constrain(normalizedPosition, -1.0f, 1.0f);
    return shouldReverseDirection ? -normalizedPosition : normalizedPosition;
}

// Button actions and settings navigation

// Acknowledgement consumes this gesture, so a hold cannot also open settings.
void handleButtonPress(bool isLongPress, uint32_t nowMs)
{
    if (guardController.currentState == GuardState::AlarmLatched)
    {
        vaultAnimation.requestSeal(nowMs);
        guardController.acknowledgeAlarm();
        remoteSensor.resetConfirmation();
        stopSound();
        return;
    }

    if (isSettingsScreenOpen)
    {
        saveSettings();
        isSettingsScreenOpen = false;
        stopSound();
        drawScreenFrame();
        nextAnimationFrameMs = nowMs;
        if (!isLongPress)
        {
            playButtonClick();
        }
        return;
    }

    if (isLongPress)
    {
        isDiagnosticsScreenOpen = false;
        isSettingsScreenOpen = true;
        selectedSettingsRow = 0;
        drawSettingsScreen();
    }
    else
    {
        isDiagnosticsScreenOpen = !isDiagnosticsScreenOpen;
        drawScreenFrame();
        nextAnimationFrameMs = nowMs;
    }
}

void handleJoystickMovement(int xDirection, int yDirection)
{
    if (!isSettingsScreenOpen || guardController.currentState == GuardState::AlarmLatched)
    {
        return;
    }

    if (yDirection)
    {
        selectedSettingsRow = (selectedSettingsRow + yDirection + 4) % 4;
    }

    if (xDirection)
    {
        if (selectedSettingsRow == 0)
        {
            int newDistanceCm = constrain(guardDistanceCm + xDirection * 5, 10, 100);
            if (newDistanceCm != guardDistanceCm)
            {
                guardDistanceCm = newDistanceCm;
                guardController.nearObjectStartedMs = 0;
                guardController.clearSpaceStartedMs = 0;
                remoteSensor.resetConfirmation();

                // A new boundary requires fresh clearance without restarting the arming grace period.
                if (guardController.currentState == GuardState::Armed
                    && vaultAnimation.currentState != VaultState::Displaying)
                {
                    guardController.currentState = GuardState::WaitingForClear;
                }
            }
        }
        else if (selectedSettingsRow == 1)
        {
            isSoundEnabled = !isSoundEnabled;
            stopSound();
        }
        else if (selectedSettingsRow == 2)
        {
            reverseXAxis = !reverseXAxis;
        }
        else if (selectedSettingsRow == 3)
        {
            reverseYAxis = !reverseYAxis;
        }
    }

    drawSettingsScreen();
}

// Input polling

// Debounce for 25 ms. A handled hold must not also produce a click on release.
void updateJoystickInput(uint32_t nowMs)
{
    static bool previousRawButtonPressed = false;
    static bool isButtonPressed = false;
    static bool longPressAlreadyHandled = false;
    static uint32_t rawButtonChangedMs = 0;
    static uint32_t buttonPressedMs = 0;

    bool rawButtonPressed = digitalRead(JOYSTICK_BUTTON_PIN) == LOW;
    if (rawButtonPressed != previousRawButtonPressed)
    {
        previousRawButtonPressed = rawButtonPressed;
        rawButtonChangedMs = nowMs;
    }

    if (nowMs - rawButtonChangedMs >= 25 && rawButtonPressed != isButtonPressed)
    {
        isButtonPressed = rawButtonPressed;
        if (isButtonPressed)
        {
            buttonPressedMs = nowMs;
            longPressAlreadyHandled = false;
        }
        else if (!longPressAlreadyHandled)
        {
            handleButtonPress(false, nowMs);
        }
    }

    if (isButtonPressed && !longPressAlreadyHandled && nowMs - buttonPressedMs >= 1000)
    {
        longPressAlreadyHandled = true;
        handleButtonPress(true, nowMs);
    }

    static uint32_t lastJoystickReadMs = 0;
    static bool joystickHasRecentred = true;
    if (nowMs - lastJoystickReadMs < 20)
    {
        return;
    }

    lastJoystickReadMs = nowMs;
    float xPosition = readJoystickAxis(JOYSTICK_X_PIN, xAxisCalibration, reverseXAxis);
    float yPosition = readJoystickAxis(JOYSTICK_Y_PIN, yAxisCalibration, reverseYAxis);

    // Require a return to centre so holding the stick cannot scroll repeatedly.

    if (fabsf(xPosition) < 0.3f && fabsf(yPosition) < 0.3f)
    {
        joystickHasRecentred = true;
    }

    if (!joystickHasRecentred || isButtonPressed)
    {
        return;
    }

    int xDirection = 0;
    int yDirection = 0;
    if (fabsf(xPosition) > fabsf(yPosition) && fabsf(xPosition) > 0.6f)
    {
        xDirection = xPosition > 0 ? 1 : -1;
    }
    else if (fabsf(yPosition) > 0.6f)
    {
        yDirection = yPosition > 0 ? 1 : -1;
    }

    if (xDirection || yDirection)
    {
        joystickHasRecentred = false;
        handleJoystickMovement(xDirection, yDirection);
    }
}
