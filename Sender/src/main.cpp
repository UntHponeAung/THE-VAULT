// Remote node: two ultrasonic sensors, CSI sampling, LED and mirrored siren.
// Both boards must use protocol v4. The receiver owns the shared alarm state.
// Holding BOOT after startup overrides both readings with a valid 5 cm test value.
// Serial c selects mock clearance; h restores hardware readings; m/n toggles CSI output.
// CSI detects changes in the radio path; it does not measure motion distance.

#include "firmware.h"
#include <esp_system.h>

uint32_t lastTransmitMs = 0;

const char *guardStateName(uint8_t state);

// Startup and main loop

void setup()
{
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    pinMode(MOCK_BUTTON_PIN, INPUT_PULLUP);
    pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(RED_LED_PIN, LOW);
    ledcSetup(BUZZER_PWM_CHANNEL, 2000, 10);
    ledcAttachPin(BUZZER_SIGNAL_PIN, BUZZER_PWM_CHANNEL);
    setRemoteBuzzerFrequency(0);
    for (uint8_t sensor = 0; sensor < REMOTE_SENSOR_COUNT; sensor++)
    {
        pinMode(TRIGGER_PINS[sensor], OUTPUT);
        digitalWrite(TRIGGER_PINS[sensor], LOW);
        pinMode(ECHO_PINS[sensor], INPUT);
    }
    attachInterrupt(digitalPinToInterrupt(ECHO_PINS[0]), captureSensor1Echo, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ECHO_PINS[1]), captureSensor2Echo, CHANGE);
    senderSession = esp_random();
    if (senderSession == 0)
    {
        senderSession = 1;
    }
    csiTelemetrySession = senderSession;
    initialiseWireless();
    printCommands();
}

void loop()
{
    uint32_t nowMs = millis();
    updateMockControls(nowMs);
    updatePhysicalSensors(nowMs);
    updateReportedReadings(nowMs);
    processReceiverAcknowledgements(nowMs);
    // ACK and CSI callbacks can carry timestamps newer than the loop's start.
    nowMs = millis();
    updateCsiMotion(
        receiverStatusIsFresh(nowMs) && uint32_t(nowMs - lastSampleAckMs) < REMOTE_TIMEOUT_MS);
    nowMs = millis();
    updateAlarmOutputs(nowMs);
    serviceCsiSerial();

    if (!transmissionsPaused && uint32_t(nowMs - lastTransmitMs) >= TRANSMIT_INTERVAL_MS)
    {
        lastTransmitMs = nowMs;
        WirelessPacket packet = makeSensorPacket(nowMs);
        if (sendWirelessPacket(packet))
        {
            lastSentSequence = packet.sequence;
        }
    }

    static uint32_t lastLogMs = 0;
    if (!csiCsvEnabled && csiSerialLength == 0 && uint32_t(nowMs - lastLogMs) >= 1000)
    {
        lastLogMs = nowMs;
        WirelessPacket sample = makeSensorPacket(nowMs);
        const char *link = !wirelessReady                            ? "RADIO ERROR"
            : !receiverStatusIsFresh(nowMs)                          ? "NO RECEIVER STATUS"
            : uint32_t(nowMs - lastSampleAckMs) >= REMOTE_TIMEOUT_MS ? "UPLINK STALE"
                                                                     : "LINK OK";
        Serial.printf(
            "%s | mode=%s boot=%u S1=%.1f/%u S2=%.1f/%u paused=%u main=%s sound=%u limit=%ucm ACKs=%lu CSI=%s score=%.2f threshold=%.2f\n",
            link,
            inputMode == InputMode::Hardware ? "HARDWARE" : "MOCK",
            mockButtonPressed,
            sample.distanceMillimetres[0] / 10.0f,
            sample.readingValidMask & 1,
            sample.distanceMillimetres[1] / 10.0f,
            (sample.readingValidMask >> 1) & 1,
            transmissionsPaused,
            guardStateName(receiverGuardState),
            receiverSoundEnabled,
            sharedGuardDistanceCm,
            (unsigned long)receivedAckCount,
            csiStatusLabel(),
            csi.score,
            csi.threshold);
    }
    delay(1);
}

// Serial status labels

const char *guardStateName(uint8_t state)
{
    const char *names[] = {"OFF", "ARMING", "ARMED", "ALARM", "WAIT_CLEAR"};
    return state < 5 ? names[state] : "UNKNOWN";
}
