#pragma once

// Receiver types and declarations shared by the four source files.
// extern refers to an object defined in another file; it does not create a copy.

#include <Arduino.h>
#include <TFT_eSPI.h>

// Hardware and display constants

// Power the joystick from 3.3 V. Divide the HC-SR04 Echo down to 3.3 V.
constexpr int JOYSTICK_X_PIN = 34;
constexpr int JOYSTICK_Y_PIN = 35;
constexpr int JOYSTICK_BUTTON_PIN = 21;
constexpr int BUZZER_SIGNAL_PIN = 27;
constexpr int BUZZER_PWM_CHANNEL = 4;
constexpr int ULTRASONIC_TRIGGER_PIN = 16;
constexpr int ULTRASONIC_ECHO_PIN = 17;
constexpr bool DEFAULT_REVERSE_X_AXIS = false;
constexpr bool DEFAULT_REVERSE_Y_AXIS = false;
constexpr int SCREEN_WIDTH_PIXELS = 480;
constexpr int SCREEN_HEIGHT_PIXELS = 320;
constexpr int SCENE_WIDTH_PIXELS = 400;
constexpr int SCENE_HEIGHT_PIXELS = 272;
constexpr int SCENE_SCREEN_X = (SCREEN_WIDTH_PIXELS - SCENE_WIDTH_PIXELS) / 2;
constexpr int SCENE_SCREEN_Y = (SCREEN_HEIGHT_PIXELS - SCENE_HEIGHT_PIXELS) / 2;
constexpr uint32_t MEASUREMENT_INTERVAL_MS = 70;
constexpr uint32_t ECHO_TIMEOUT_MICROSECONDS = 26000;

constexpr uint16_t BACKGROUND_COLOR = 0x0843;
constexpr uint16_t WHITE_COLOR = 0xFFFF;
constexpr uint16_t MUTED_TEXT_COLOR = 0x8CB3;
constexpr uint16_t CYAN_COLOR = 0x6F9F;
constexpr uint16_t GOLD_COLOR = 0xFEA0;
constexpr uint16_t RED_COLOR = 0xF986;
constexpr uint16_t GREEN_COLOR = 0x47EC;

// Shared packet layout
// Shared protocol v4. Copy this block identically to both firmware.h files.
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

// CSI detector state (wireless.cpp)

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

// Guard and vault states (security.cpp)

enum class GuardState
{
    Off,
    Arming,
    Armed,
    AlarmLatched,
    WaitingForClear
};

class GuardController
{
public:
    GuardState currentState = GuardState::Off;
    uint32_t armingStartedMs = 0;
    uint32_t nearObjectStartedMs = 0;
    uint32_t clearSpaceStartedMs = 0;

    void startArming(uint32_t nowMs);

    void acknowledgeAlarm();

    void processDistanceSample(bool isReadingValid,
        float measuredDistanceCm,
        int alarmBoundaryCm,
        uint32_t nowMs,
        bool additionalClearanceReady = true);
};

enum class VaultState
{
    Displaying,
    Retracting,
    Sealing,
    Sealed,
    Opening,
    Raising
};

class VaultAnimator
{
public:
    VaultState currentState = VaultState::Displaying;
    uint16_t diamondLiftPermille = 1000;
    uint16_t shutterClosedPermille = 0;

    void reset(uint32_t nowMs);

    // Preserve the current position when an intrusion reverses a reopening vault.
    void requestSeal(uint32_t nowMs);

    void update(uint32_t nowMs, bool mustStaySealed, bool mayRevealDiamond);

private:
    static constexpr uint32_t DIAMOND_TRAVEL_MS = 420;
    static constexpr uint32_t SHUTTER_TRAVEL_MS = 260;
    static constexpr uint32_t SEALED_HOLD_MS = 120;
    uint32_t phaseStartedMs = 0;
    uint32_t lastUpdateMs = 0;
    uint32_t phaseDurationMs = 1;
    uint16_t phaseStartPosition = 0;

    void beginMotion(VaultState nextState, uint16_t startPosition, uint32_t nowMs);
};

// Remote sensor state (wireless.cpp)

class RemoteSensorMonitor
{
public:
    bool hasPacket = false;
    bool readingValid = false;
    bool clearanceConfirmed = false;
    uint16_t distanceMillimetres = 0;
    uint32_t lastPacketMs = 0;

    bool isFresh(uint32_t nowMs) const;

    bool isClear(uint32_t nowMs, int boundaryCm) const;

    void resetConfirmation();

    void expire(uint32_t nowMs);

    bool acceptSample(bool valid,
        uint16_t distanceMm,
        uint32_t receivedMs,
        int boundaryCm,
        bool mayTriggerAlarm);

private:
    bool hasNearWindow = false;
    bool hasClearWindow = false;
    uint32_t nearStartedMs = 0;
    uint32_t clearStartedMs = 0;
};

class RemoteSensorPair
{
public:
    RemoteSensorMonitor sensors[REMOTE_SENSOR_COUNT];
    bool hasPacket = false;
    uint32_t lastPacketMs = 0;
    uint8_t lastTriggerMask = 0;

    bool isFresh(uint32_t nowMs) const;

    bool isClear(uint32_t nowMs, int boundaryCm) const;

    void resetConfirmation();

    void expire(uint32_t nowMs);

    bool acceptPacket(const WirelessPacket &packet,
        uint32_t receivedMs,
        int boundaryCm,
        bool mayTriggerAlarm,
        uint32_t queueAgeMs = 0);

private:
    bool hasSampleSequence[REMOTE_SENSOR_COUNT] = {};
    uint32_t sampleSequence[REMOTE_SENSOR_COUNT] = {};
};

// Shared objects and functions

// wireless.cpp owns these values. extern refers to that existing instance.
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

// main.cpp: user controls, saved settings and frame scheduling.
extern int guardDistanceCm;
extern bool isSoundEnabled;
extern bool reverseXAxis;
extern bool reverseYAxis;
extern bool isSettingsScreenOpen;
extern bool isDiagnosticsScreenOpen;
extern int selectedSettingsRow;
extern uint32_t nextAnimationFrameMs;
void loadSettings();
void calibrateJoystick();
void updateJoystickInput(uint32_t nowMs);

// security.cpp: local measurements, alarm latch, vault motion and buzzer.
extern GuardController guardController;
extern VaultAnimator vaultAnimation;
extern bool silentLinkFault;
extern uint8_t alarmCause;
extern uint16_t triggerDistanceMm;
extern uint32_t alarmStartedMs;
extern bool hasValidDistance;
extern bool isLatestDistanceSampleValid;
extern uint32_t lastValidDistanceMs;
extern float filteredDistanceCm;
void captureEchoEdge();
void updateDistanceSensor(uint32_t nowMs);
void startSecurityAlarm(uint32_t nowMs, bool triggeredByRemote, uint8_t csiCause = 0);
void updateLinkSafety(uint32_t nowMs);
void updateVaultAnimation(uint32_t nowMs);
void stopSound();
void updateBuzzer(uint32_t nowMs);
void playStartupSound();
void playButtonClick();

// wireless.cpp: remote readings and combined CSI warnings.
extern RemoteSensorPair remoteSensor;
extern uint32_t receiverSession;
extern uint32_t acceptedRemotePackets;
extern bool hasPendingAcknowledgement;
extern bool sharedCsiWarning;
extern uint32_t warningStartedMs;
void updateWirelessReceiver(uint32_t nowMs);
void updateSharedCsiWarning(uint32_t nowMs);
void publishReceiverStatus(uint32_t nowMs);
const char *getRemoteLinkLabel(uint32_t nowMs);

// display.cpp: LCD objects, shared colours and all drawing functions.
extern TFT_eSPI display;
extern TFT_eSprite sceneBuffer;
extern const uint16_t SCENE_PALETTE[16];
void drawDisplayText(const char *text,
    int screenX,
    int screenY,
    uint16_t textColor,
    int textScale = 2);
void drawScreenFrame();
void drawSettingsScreen();
void drawDiagnosticsScreen(uint32_t nowMs);
void drawAnimatedScene(uint32_t nowMs);
