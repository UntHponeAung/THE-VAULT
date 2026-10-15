#pragma once

// Shared types and interfaces for the remote sensor node.

#include <Arduino.h>

// Wire protocol
// Keep this layout identical in both firmware.h files.
// Each sensor has its own sample counter and age. Repeating a radio packet must
// never turn a single old measurement into confirmed intrusion or clearance.
constexpr uint32_t WIRE_MAGIC = 0x5641554CUL;
constexpr uint32_t WIRE_NETWORK_ID = 0xD1A02026UL;
constexpr uint8_t WIRE_VERSION = 4;
constexpr uint8_t SAMPLE_PACKET = 1;
constexpr uint8_t ACK_PACKET = 2;
constexpr uint8_t REMOTE_SENSOR_COUNT = 2;
constexpr uint32_t REMOTE_TIMEOUT_MS = 350;
constexpr uint32_t STATUS_INTERVAL_MS = 70;

struct WirelessPacket
{
    uint32_t magic = WIRE_MAGIC;
    uint32_t networkId = WIRE_NETWORK_ID;
    uint32_t session = 0;            // Sender boot ID; echoed in receiver replies.
    uint32_t sequence = 0;           // Radio sample packet number.
    uint32_t sensorSequence[2] = {}; // Advances only after a new measurement.
    uint32_t receiverSession = 0;    // Receiver boot ID, used only in replies.
    uint32_t statusSequence = 0;     // Reply number, including unsolicited status.
    uint32_t alarmElapsedMs = 0;     // Siren phase reference from the main node.
    uint16_t distanceMillimetres[2] = {};
    uint16_t sampleAgeMs[2] = {65535, 65535};
    uint8_t version = WIRE_VERSION;
    uint8_t kind = SAMPLE_PACKET;
    uint8_t readingValidMask = 0; // Bit 0 = sensor 1; bit 1 = sensor 2.
    uint8_t guardState = 0;
    uint8_t vaultState = 0;
    uint8_t soundEnabled = 0;
    uint8_t csiState = 0;         // 0=off, 1=no data, 2=calibrating, 3=ready, 4=motion.
    uint8_t sharedCsiWarning = 0; // Receiver's combined warning; ACK packets only.
    uint16_t csiScore100 = 0;     // This board's detector, never an echoed warning.
    uint16_t csiThreshold100 = 200;
    uint16_t csiAgeMs = 65535;
    uint16_t triggerDistanceMm = 0; // Snapshot when the alarm started; 0 for CSI or link loss.
    uint32_t warningElapsedMs = 0;  // Common chirp timing reference.
    uint8_t guardDistanceCm = 30;   // One main-node setting for all three sensors.
    uint8_t alarmCause =
        0; // 0=none, 1=local, 2=S1, 3=S2, 4=both, 5=link loss, 6=main CSI, 7=remote CSI, 8=both CSI.
    uint8_t reserved[2] = {};
};

static_assert(sizeof(WirelessPacket) == 68, "Both boards require the 68-byte v4 packet");

// Pins and measurement timing
// Each 5 V Echo output needs its own voltage divider before the ESP32 input.

constexpr uint8_t TRIGGER_PINS[REMOTE_SENSOR_COUNT] = {16, 18};
constexpr uint8_t ECHO_PINS[REMOTE_SENSOR_COUNT] = {17, 19};
constexpr uint8_t RED_LED_PIN = 25;
constexpr uint8_t BUZZER_SIGNAL_PIN = 27;
constexpr uint8_t BUZZER_PWM_CHANNEL = 4;
constexpr uint8_t MOCK_BUTTON_PIN = 0;
constexpr uint32_t TRANSMIT_INTERVAL_MS = 70;
constexpr uint32_t PING_SPACING_MS = 70;
constexpr uint32_t ECHO_TIMEOUT_US = 26000;
constexpr uint16_t MOCK_NEAR_MM = 50;
constexpr uint16_t MOCK_CLEAR_MM = 2000;

// CSI detector state

constexpr bool ENABLE_CSI_WARNING = true;
constexpr uint32_t CSI_CALIBRATION_MS = 10000; // Keep both boards and the room still.
constexpr uint32_t CSI_TIMEOUT_MS = 350;
constexpr uint32_t CSI_WARNING_HOLD_MS = 2000;
constexpr float CSI_MIN_THRESHOLD = 2.0f;    // Minimum per-sample change, in percent.
constexpr float CSI_NOISE_MULTIPLIER = 2.0f; // Margin above the measured calibration noise.
constexpr uint8_t CSI_BIN_COUNT = 24;

struct CsiMotionDetector
{
    float previous[CSI_BIN_COUNT] = {};
    float score = 0;
    float threshold = CSI_MIN_THRESHOLD;
    float noiseSum = 0;
    float noiseSquaredSum = 0;
    uint32_t calibrationStartedMs = 0;
    uint32_t lastSampleMs = 0;
    uint32_t lastMotionMs = 0;
    uint16_t calibrationSamples = 0;
    bool hasPrevious = false;
    bool calibrated = false;
    bool motion = false;
};

struct PeerCsiStatus
{
    uint8_t state = 1;
    float score = 0;
    float threshold = CSI_MIN_THRESHOLD;
    uint32_t sampleMs = 0;

    void accept(const WirelessPacket &packet, uint32_t receivedMs);

    uint8_t freshState(uint32_t nowMs, bool linkFresh) const;
};

// Sensor readings and test modes

struct SensorReading
{
    bool hasSample = false;
    bool valid = false;
    uint16_t distanceMm = 0;
    uint32_t measuredMs = 0;
    uint32_t sequence = 0;
};

enum class InputMode
{
    Hardware,
    NearSensor1,
    NearSensor2,
    NearBoth,
    Clear,
    Invalid,
    Demo
};

// Module interfaces

// wireless.cpp: local and peer CSI state.
extern CsiMotionDetector csi;
extern PeerCsiStatus peerCsi;
extern bool wirelessReady;
extern bool csiCsvEnabled;
extern uint32_t csiTelemetrySession;
extern size_t csiSerialLength;

void initialiseWireless();
void updateCsiMotion(bool linkFresh);
void setCsiTelemetry(char command);
void serviceCsiSerial();
const char *csiStatusLabel();
uint8_t localCsiState();

// sensors.cpp: two ultrasonic sensors, BOOT/serial controls, LED and buzzer.
extern SensorReading reportedReadings[REMOTE_SENSOR_COUNT];
extern InputMode inputMode;
extern bool mockButtonPressed;
extern bool transmissionsPaused;
void captureSensor1Echo();
void captureSensor2Echo();
void updatePhysicalSensors(uint32_t nowMs);
void updateMockControls(uint32_t nowMs);
void updateReportedReadings(uint32_t nowMs);
void printCommands();
void setRemoteBuzzerFrequency(int frequencyHz);
void updateAlarmOutputs(uint32_t nowMs);

// wireless.cpp: accepted receiver status and outgoing message identity.
extern uint32_t senderSession;
extern uint32_t lastSentSequence;
extern bool hasReceiverAck;
extern uint32_t lastAckMs;
extern uint32_t lastSampleAckMs;
extern uint32_t receivedAckCount;
extern uint8_t receiverGuardState;
extern bool receiverSoundEnabled;
extern uint32_t receiverAlarmElapsedMs;
extern bool receiverSharedCsiWarning;
extern uint32_t receiverWarningElapsedMs;
extern uint8_t receiverAlarmCause;
extern uint8_t sharedGuardDistanceCm;
bool receiverStatusIsFresh(uint32_t nowMs);
void processReceiverAcknowledgements(uint32_t nowMs);
WirelessPacket makeSensorPacket(uint32_t nowMs);
bool sendWirelessPacket(const WirelessPacket &packet);
