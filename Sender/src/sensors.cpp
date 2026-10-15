// Ultrasonic measurements, test inputs and outputs driven by receiver status.

#include "firmware.h"

// Per-sensor state

struct PhysicalSensor
{
    SensorReading reading;
    uint16_t history[3] = {};
    uint8_t historyCount = 0;
    uint8_t historyIndex = 0;
};

struct EchoCapture
{
    volatile bool listening = false;
    volatile bool risingEdgeSeen = false;
    volatile bool complete = false;
    volatile uint32_t riseUs = 0;
    volatile uint32_t pulseUs = 0;
};

PhysicalSensor physicalSensors[REMOTE_SENSOR_COUNT];
SensorReading reportedReadings[REMOTE_SENSOR_COUNT];
EchoCapture echoCapture[REMOTE_SENSOR_COUNT];
portMUX_TYPE echoLock = portMUX_INITIALIZER_UNLOCKED;
int8_t activeSensor = -1;
uint8_t nextSensor = 0;
uint32_t pingStartedUs = 0;
uint32_t lastPingStartedMs = 0;

// Echo capture and measurement scheduling

// Only edge timing is captured here; filtering and radio work belong in loop().
void IRAM_ATTR captureSensorEcho(uint8_t sensor)
{
    uint32_t nowUs = micros();
    bool high = digitalRead(ECHO_PINS[sensor]) == HIGH;
    portENTER_CRITICAL_ISR(&echoLock);
    if (echoCapture[sensor].listening)
    {
        if (high && !echoCapture[sensor].risingEdgeSeen)
        {
            echoCapture[sensor].riseUs = nowUs;
            echoCapture[sensor].risingEdgeSeen = true;
        }
        else if (!high && echoCapture[sensor].risingEdgeSeen)
        {
            echoCapture[sensor].pulseUs = nowUs - echoCapture[sensor].riseUs;
            echoCapture[sensor].complete = true;
            echoCapture[sensor].listening = false;
        }
    }
    portEXIT_CRITICAL_ISR(&echoLock);
}

void IRAM_ATTR captureSensor1Echo()
{
    captureSensorEcho(0);
}

void IRAM_ATTR captureSensor2Echo()
{
    captureSensorEcho(1);
}

void finishSensorMeasurement(uint8_t sensor, uint32_t pulseUs, uint32_t nowMs)
{
    PhysicalSensor &state = physicalSensors[sensor];
    SensorReading &reading = state.reading;
    // 343 m/s = 0.343 mm/us; divide the round-trip distance by two.
    uint32_t distanceMm = (pulseUs * 343UL + 1000UL) / 2000UL;
    reading.hasSample = true;
    reading.measuredMs = nowMs;
    reading.sequence++;
    reading.valid =
        pulseUs > 0 && pulseUs <= ECHO_TIMEOUT_US && distanceMm >= 20 && distanceMm <= 4000;
    if (!reading.valid)
    {
        reading.distanceMm = 0;
        state.historyCount = 0;
        state.historyIndex = 0;
        return;
    }

    state.history[state.historyIndex] = uint16_t(distanceMm);
    state.historyIndex = (state.historyIndex + 1) % 3;
    if (state.historyCount < 3)
    {
        state.historyCount++;
    }
    reading.distanceMm = state.history[0];
    if (state.historyCount >= 2)
    {
        uint16_t low = min(state.history[0], state.history[1]);
        uint16_t high = max(state.history[0], state.history[1]);
        // Maximum of two readings while warming up, then median of three.
        reading.distanceMm =
            state.historyCount == 2 ? high : constrain(state.history[2], low, high);
    }
}

void updatePhysicalSensors(uint32_t nowMs)
{
    if (activeSensor >= 0)
    {
        uint8_t sensor = uint8_t(activeSensor);
        portENTER_CRITICAL(&echoLock);
        bool complete = echoCapture[sensor].complete;
        uint32_t pulseUs = echoCapture[sensor].pulseUs;
        portEXIT_CRITICAL(&echoLock);
        if (complete || uint32_t(micros() - pingStartedUs) >= ECHO_TIMEOUT_US)
        {
            portENTER_CRITICAL(&echoLock);
            echoCapture[sensor].listening = false;
            echoCapture[sensor].complete = false;
            portEXIT_CRITICAL(&echoLock);
            finishSensorMeasurement(sensor, complete ? pulseUs : 0, nowMs);
            activeSensor = -1;
        }
    }

    // Alternate sensors with 70 ms between pings (140 ms per sensor).
    // Staggering the pings reduces interference between these two sensors.
    if (activeSensor < 0 && uint32_t(nowMs - lastPingStartedMs) >= PING_SPACING_MS)
    {
        uint8_t sensor = nextSensor;
        nextSensor = (nextSensor + 1) % REMOTE_SENSOR_COUNT;
        activeSensor = sensor;
        lastPingStartedMs = nowMs;
        pingStartedUs = micros();
        portENTER_CRITICAL(&echoLock);
        echoCapture[sensor].complete = false;
        echoCapture[sensor].risingEdgeSeen = false;
        echoCapture[sensor].listening = true;
        portEXIT_CRITICAL(&echoLock);
        digitalWrite(TRIGGER_PINS[sensor], HIGH);
        delayMicroseconds(10); // HC-SR04 trigger pulse; echo capture is interrupt-driven.
        digitalWrite(TRIGGER_PINS[sensor], LOW);
    }
}

// BOOT override and serial controls

InputMode inputMode = InputMode::Hardware;
bool mockButtonPressed = false;
bool transmissionsPaused = false;
uint32_t demoStartedMs = 0;

void printCommands()
{
    Serial.println("DUAL SENSOR v4: default mode uses TWO real HC-SR04 sensors.");
    Serial.println(
        "BOOT held after startup = valid 5 cm on BOTH sensors; release restores selected mode.");
    Serial.println("h=hardware, 1=mock near S1, 2=mock near S2, t=mock both near");
    Serial.println("c=mock both clear, i=mock both invalid, d=8s clear/4s near demo");
    Serial.println("p=pause outgoing samples, r=resume, ?=help. Serial: 115200 baud.");
    Serial.println("m=MATLAB CSI stream on, n=stream off (no effect on security).");
    Serial.println(
        "Main joystick acknowledges BOTH sirens; its mute setting controls BOTH buzzers.");
    Serial.println("With no sensors, use c for bench clearance, then hold/release BOOT to test.");
}

void updateMockControls(uint32_t nowMs)
{
    static bool previousRawPressed = false;
    static uint32_t rawChangedMs = 0;
    bool rawPressed = digitalRead(MOCK_BUTTON_PIN) == LOW;
    if (rawPressed != previousRawPressed)
    {
        previousRawPressed = rawPressed;
        rawChangedMs = nowMs;
    }
    if (uint32_t(nowMs - rawChangedMs) >= 25)
    {
        mockButtonPressed = rawPressed;
    }

    for (uint8_t count = 0; count < 16 && Serial.available(); count++)
    {
        char command = char(Serial.read());
        switch (command)
        {
        case 'h':
            inputMode = InputMode::Hardware;
            Serial.println("MODE: real sensors");
            break;
        case '1':
            inputMode = InputMode::NearSensor1;
            Serial.println("MOCK: S1=5 cm, S2=200 cm");
            break;
        case '2':
            inputMode = InputMode::NearSensor2;
            Serial.println("MOCK: S1=200 cm, S2=5 cm");
            break;
        case 't':
            inputMode = InputMode::NearBoth;
            Serial.println("MOCK: both near");
            break;
        case 'c':
            inputMode = InputMode::Clear;
            Serial.println("MOCK: both clear (test override, NOT physical clearance)");
            break;
        case 'i':
            inputMode = InputMode::Invalid;
            Serial.println("MOCK: both invalid");
            break;
        case 'd':
            inputMode = InputMode::Demo;
            demoStartedMs = nowMs;
            Serial.println("MOCK: automatic demo");
            break;
        case 'p':
            transmissionsPaused = true;
            Serial.println("RADIO: sample transmissions paused");
            break;
        case 'r':
            transmissionsPaused = false;
            Serial.println("RADIO: sample transmissions resumed");
            break;
        case 'm':
        case 'n':
            setCsiTelemetry(command);
            break;
        case '?':
            printCommands();
            break;
        default:
            break;
        }
    }
}

// Report counters are independent of physical counters, so entering/leaving a
// test override cannot move the transmitted sensor sequence backwards.
void updateReportedReadings(uint32_t nowMs)
{
    static bool initialised = false;
    static InputMode previousMode = InputMode::Hardware;
    static bool previousBoot = false;
    static uint32_t lastMockSampleMs = 0;
    static uint32_t usedPhysicalSequence[REMOTE_SENSOR_COUNT] = {};
    bool modeChanged =
        !initialised || previousMode != inputMode || previousBoot != mockButtonPressed;
    initialised = true;
    previousMode = inputMode;
    previousBoot = mockButtonPressed;
    bool simulated = mockButtonPressed || inputMode != InputMode::Hardware;
    bool newMockSample = modeChanged || uint32_t(nowMs - lastMockSampleMs) >= TRANSMIT_INTERVAL_MS;
    if (simulated && newMockSample)
    {
        lastMockSampleMs = nowMs;
    }

    for (uint8_t sensor = 0; sensor < REMOTE_SENSOR_COUNT; sensor++)
    {
        const SensorReading &physical = physicalSensors[sensor].reading;
        bool physicalChanged = physical.sequence != usedPhysicalSequence[sensor];
        if ((simulated && !newMockSample) || (!simulated && !modeChanged && !physicalChanged))
        {
            continue;
        }
        SensorReading &reported = reportedReadings[sensor];
        uint32_t nextSequence = reported.sequence + 1;
        if (!simulated)
        {
            reported = physical;
            usedPhysicalSequence[sensor] = physical.sequence;
        }
        else
        {
            bool near = mockButtonPressed || inputMode == InputMode::NearBoth
                || (inputMode == InputMode::NearSensor1 && sensor == 0)
                || (inputMode == InputMode::NearSensor2 && sensor == 1)
                || (inputMode == InputMode::Demo
                    && uint32_t(nowMs - demoStartedMs) % 12000 >= 8000);
            reported.hasSample = true;
            reported.valid = mockButtonPressed || inputMode != InputMode::Invalid;
            reported.distanceMm = reported.valid ? (near ? MOCK_NEAR_MM : MOCK_CLEAR_MM) : 0;
            reported.measuredMs = nowMs;
        }
        reported.sequence = nextSequence;
    }
}

// Siren and warning patterns

int sirenFrequency(uint32_t elapsedMs)
{
    uint32_t phase = elapsedMs % 1600;
    uint32_t ramp = phase <= 800 ? phase : 1600 - phase;
    return 850 + int(ramp / 20) * 25; // 850 -> 1850 -> 850 Hz.
}

int warningChirpFrequency(uint32_t elapsedMs)
{
    return elapsedMs % 3000 < 80 ? 2200 : 0;
}

// Buzzer and LED outputs

int currentBuzzerFrequencyHz = -1;
int currentLedLevel = -1;

void setRemoteBuzzerFrequency(int frequencyHz)
{
    if (frequencyHz != currentBuzzerFrequencyHz)
    {
        ledcWriteTone(BUZZER_PWM_CHANNEL, frequencyHz);
        currentBuzzerFrequencyHz = frequencyHz;
    }
}

void updateAlarmOutputs(uint32_t nowMs)
{
    bool alarmLatched = hasReceiverAck && receiverGuardState == 3;
    bool waitingForClear = hasReceiverAck && receiverGuardState == 4;
    bool linkFresh =
        receiverStatusIsFresh(nowMs) && uint32_t(nowMs - lastSampleAckMs) < REMOTE_TIMEOUT_MS;
    int frequencyHz = 0;
    bool ledOn = false;
    if (alarmLatched)
    {
        // Use the main node's elapsed alarm time plus local time since receipt.
        // Radio latency prevents sample-exact acoustic synchronisation.
        uint32_t elapsed = receiverAlarmElapsedMs + uint32_t(nowMs - lastAckMs);
        if (receiverSoundEnabled && receiverAlarmCause != 5)
        {
            frequencyHz = sirenFrequency(elapsed);
        }
        ledOn = ((elapsed / 140) & 1) == 0;
        // If contact is lost during an alarm, retain its last sound setting and
        // keep alarming until a valid main-node status clears it.
    }
    else if (waitingForClear)
    {
        ledOn = true; // Acknowledged: silent, but the vault is still secured.
    }
    else if (!linkFresh)
    {
        ledOn = nowMs % 1000 < 100; // Communication fault, never invent a siren command.
    }
    else if (receiverSharedCsiWarning)
    {
        uint32_t elapsed = receiverWarningElapsedMs + uint32_t(nowMs - lastAckMs);
        uint32_t phase = elapsed % 1200;
        ledOn = phase < 80 || (phase >= 180 && phase < 260);
        if (receiverSoundEnabled && receiverGuardState == 2)
        {
            frequencyHz = warningChirpFrequency(elapsed);
        }
    }
    else
    {
        ledOn = nowMs % 3000 < 40; // Brief "system alive" blink once every three seconds.
    }
    setRemoteBuzzerFrequency(frequencyHz);
    int level = ledOn ? HIGH : LOW;
    if (level != currentLedLevel)
    {
        digitalWrite(RED_LED_PIN, level);
        currentLedLevel = level;
    }
}
