// Receives remote samples, measures CSI activity and sends the receiver state back.

#include "firmware.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>
#include <string.h>

// Remote sensor confirmation

bool RemoteSensorMonitor::isFresh(uint32_t nowMs) const
{
    return hasPacket && uint32_t(nowMs - lastPacketMs) < REMOTE_TIMEOUT_MS;
}

bool RemoteSensorMonitor::isClear(uint32_t nowMs, int boundaryCm) const
{
    return isFresh(nowMs) && readingValid && clearanceConfirmed
        && distanceMillimetres >= (boundaryCm + 5) * 10;
}

void RemoteSensorMonitor::resetConfirmation()
{
    hasNearWindow = false;
    hasClearWindow = false;
    clearanceConfirmed = false;
}

void RemoteSensorMonitor::expire(uint32_t nowMs)
{
    if (!isFresh(nowMs))
    {
        readingValid = false;
        resetConfirmation();
    }
}

bool RemoteSensorMonitor::acceptSample(bool valid,
    uint16_t distanceMm,
    uint32_t receivedMs,
    int boundaryCm,
    bool mayTriggerAlarm)
{
    if (!isFresh(receivedMs))
    {
        resetConfirmation();
    }
    hasPacket = true;
    lastPacketMs = receivedMs;
    readingValid = valid;
    distanceMillimetres = distanceMm;
    if (!valid)
    {
        resetConfirmation();
        return false;
    }

    if (distanceMm >= (boundaryCm + 5) * 10)
    {
        if (!hasClearWindow)
        {
            hasClearWindow = true;
            clearStartedMs = receivedMs;
        }
        clearanceConfirmed = uint32_t(receivedMs - clearStartedMs) >= 600;
    }
    else
    {
        hasClearWindow = false;
        clearanceConfirmed = false;
    }

    if (mayTriggerAlarm && distanceMm <= boundaryCm * 10)
    {
        if (!hasNearWindow)
        {
            hasNearWindow = true;
            nearStartedMs = receivedMs;
        }
        return uint32_t(receivedMs - nearStartedMs) >= 140;
    }
    hasNearWindow = false;
    return false;
}

bool RemoteSensorPair::isFresh(uint32_t nowMs) const
{
    return hasPacket && uint32_t(nowMs - lastPacketMs) < REMOTE_TIMEOUT_MS;
}

bool RemoteSensorPair::isClear(uint32_t nowMs, int boundaryCm) const
{
    return isFresh(nowMs) && sensors[0].isClear(nowMs, boundaryCm)
        && sensors[1].isClear(nowMs, boundaryCm);
}

void RemoteSensorPair::resetConfirmation()
{
    for (uint8_t sensor = 0; sensor < REMOTE_SENSOR_COUNT; sensor++)
    {
        sensors[sensor].resetConfirmation();
    }
}

void RemoteSensorPair::expire(uint32_t nowMs)
{
    for (uint8_t sensor = 0; sensor < REMOTE_SENSOR_COUNT; sensor++)
    {
        sensors[sensor].expire(nowMs);
    }
    if (!isFresh(nowMs))
    {
        resetConfirmation();
    }
}

bool RemoteSensorPair::acceptPacket(const WirelessPacket &packet,
    uint32_t receivedMs,
    int boundaryCm,
    bool mayTriggerAlarm,
    uint32_t queueAgeMs)
{
    if (!isFresh(receivedMs))
    {
        resetConfirmation();
    }
    hasPacket = true;
    lastPacketMs = receivedMs;
    lastTriggerMask = 0;
    for (uint8_t sensor = 0; sensor < REMOTE_SENSOR_COUNT; sensor++)
    {
        // The radio heartbeat is faster than each physical sensor. Only new
        // sensor samples may advance either confirmation timer.
        if (hasSampleSequence[sensor]
            && int32_t(packet.sensorSequence[sensor] - sampleSequence[sensor]) <= 0)
        {
            continue;
        }
        hasSampleSequence[sensor] = true;
        sampleSequence[sensor] = packet.sensorSequence[sensor];
        // Include time spent in our queue before trusting a retained measurement.
        uint32_t sampleAgeMs = uint32_t(packet.sampleAgeMs[sensor]) + queueAgeMs;
        bool valid =
            (packet.readingValidMask & (1U << sensor)) != 0 && sampleAgeMs < REMOTE_TIMEOUT_MS;
        // Confirmation follows estimated measurement times, not variable delivery times.
        uint32_t measuredMs = receivedMs - packet.sampleAgeMs[sensor];
        bool triggered = sensors[sensor].acceptSample(valid,
            packet.distanceMillimetres[sensor],
            measuredMs,
            boundaryCm,
            mayTriggerAlarm);
        if (triggered)
        {
            lastTriggerMask |= (1U << sensor);
        }
    }
    return lastTriggerMask != 0;
}

// Packet validation

bool isValidWirelessPacket(const WirelessPacket &packet)
{
    if (packet.magic != WIRE_MAGIC || packet.networkId != WIRE_NETWORK_ID
        || packet.version != WIRE_VERSION || packet.session == 0
        || (packet.kind != SAMPLE_PACKET && packet.kind != ACK_PACKET)
        || packet.readingValidMask > 3 || packet.guardState > 4 || packet.vaultState > 5
        || packet.soundEnabled > 1 || packet.csiState > 4 || packet.sharedCsiWarning > 1
        || packet.csiScore100 > 20000 || packet.csiThreshold100 > 20000 || packet.alarmCause > 8
        || packet.triggerDistanceMm > 4000 || packet.guardDistanceCm < 10
        || packet.guardDistanceCm > 100 || packet.guardDistanceCm % 5 != 0
        || (packet.csiState >= 2 && packet.csiAgeMs >= REMOTE_TIMEOUT_MS)
        || (packet.kind == SAMPLE_PACKET && packet.sharedCsiWarning != 0) || packet.reserved[0] != 0
        || packet.reserved[1] != 0)
    {
        return false;
    }
    if (packet.kind == ACK_PACKET && packet.receiverSession == 0)
    {
        return false;
    }
    for (uint8_t sensor = 0; sensor < REMOTE_SENSOR_COUNT; sensor++)
    {
        bool valid = (packet.readingValidMask & (1U << sensor)) != 0;
        if (valid
            && (packet.distanceMillimetres[sensor] < 20 || packet.distanceMillimetres[sensor] > 4000
                || packet.sampleAgeMs[sensor] >= REMOTE_TIMEOUT_MS))
        {
            return false;
        }
    }
    return true;
}

// Radio queue and callback state

constexpr uint8_t WIRELESS_CHANNEL = 1;
constexpr uint8_t RX_QUEUE_CAPACITY = 8;
const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct ReceivedPacket
{
    WirelessPacket packet;
    uint8_t sourceMac[6];
    uint32_t receivedMs;
};

portMUX_TYPE wirelessLock = portMUX_INITIALIZER_UNLOCKED;
ReceivedPacket receivedPackets[RX_QUEUE_CAPACITY];
uint8_t rxHead = 0;
uint8_t rxTail = 0;
uint8_t rxCount = 0;
bool rxOverflow = false;
bool wirelessSendBusy = false;
bool wirelessReady = false;

// CSI motion detection
// CSI measures radio-path changes, not distance or position.
// Sensor and status packets already provide the 70 ms radio traffic.
// This detector holds brief crossings; the receiver latches the shared alarm.
// LLTF has 64 complex bins. Skip DC, guard bins and the first four invalid bytes.
const uint8_t CSI_BINS[CSI_BIN_COUNT] =
    {4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60};

struct CsiFrame
{
    int8_t data[128] = {};
    uint32_t receivedMs = 0;
};

CsiMotionDetector csi;
CsiFrame pendingCsiFrame;
portMUX_TYPE csiLock = portMUX_INITIALIZER_UNLOCKED;
uint8_t csiPeerMac[6] = {};
bool csiPeerKnown = false;
bool csiFramePending = false;
bool csiEnabled = false;

// USB telemetry for MATLAB (m starts streaming; n stops it)
// Queue one line and send it only when it fits in the UART buffer. If the PC is
// slow, skip a sample rather than delaying sensing, alarm handling or rendering.
bool csiCsvEnabled = false;
uint32_t csiTelemetrySession = 0;
uint32_t csiTelemetrySequence = 0;
uint32_t csiTelemetryDropped = 0;
char csiSerialLine[512] = {};
size_t csiSerialLength = 0;

uint8_t localCsiState()
{
    if (!csiEnabled)
    {
        return 0;
    }
    if (!csi.hasPrevious)
    {
        return 1;
    }
    if (!csi.calibrated)
    {
        return 2;
    }
    return csi.motion ? 4 : 3;
}

void queueCsiTelemetry(const float *amplitude)
{
    csiTelemetrySequence++;
    if (!csiCsvEnabled)
    {
        return;
    }
    if (csiSerialLength != 0)
    {
        csiTelemetryDropped++;
        return;
    }
    // CSV v1: marker, boot, sequence, device milliseconds, detector state,
    // firmware score, threshold, dropped records, then 24 unnormalised amplitudes.
    int length = snprintf(csiSerialLine,
        sizeof(csiSerialLine),
        "CSI1,%lu,%lu,%lu,%u,%.3f,%.3f,%lu",
        (unsigned long)csiTelemetrySession,
        (unsigned long)csiTelemetrySequence,
        (unsigned long)csi.lastSampleMs,
        localCsiState(),
        csi.score,
        csi.threshold,
        (unsigned long)csiTelemetryDropped);
    for (uint8_t index = 0; index < CSI_BIN_COUNT; index++)
    {
        length += snprintf(csiSerialLine + length,
            sizeof(csiSerialLine) - length,
            ",%.3f",
            amplitude[index]);
    }
    csiSerialLine[length++] = '\n';
    csiSerialLength = size_t(length);
}

void serviceCsiSerial()
{
    if (csiSerialLength == 0 || Serial.availableForWrite() < int(csiSerialLength))
    {
        return;
    }
    Serial.write(reinterpret_cast<const uint8_t *>(csiSerialLine), csiSerialLength);
    csiSerialLength = 0;
}

void setCsiTelemetry(char command)
{
    if (command == 'm')
    {
        csiCsvEnabled = true;
    }
    else if (command == 'n')
    {
        csiCsvEnabled = false;
        // A queued complete record may still be sent by serviceCsiSerial().
    }
}

// Accept CSI only from the selected ESP-NOW peer.
void bindCsiPeer(const uint8_t *mac)
{
    portENTER_CRITICAL(&csiLock);
    memcpy(csiPeerMac, mac, 6);
    csiPeerKnown = true;
    csiFramePending = false;
    portEXIT_CRITICAL(&csiLock);
    csi = CsiMotionDetector();
}

// Copy in the radio callback; process amplitudes later in loop().
void onCsiReceive(void *, wifi_csi_info_t *info)
{
    // Accept only successful 20 MHz 802.11n receptions with the full LLTF.
    if (!info || !info->buf || info->len != 128 || info->rx_ctrl.sig_mode != 1
        || info->rx_ctrl.cwb != 0 || info->rx_ctrl.rx_state != 0)
    {
        return;
    }
    portENTER_CRITICAL(&csiLock);
    if (csiPeerKnown && memcmp(info->mac, csiPeerMac, 6) == 0)
    {
        // Latest sample wins if rendering was busy; never allocate in this callback.
        memcpy(pendingCsiFrame.data, info->buf, sizeof(pendingCsiFrame.data));
        pendingCsiFrame.receivedMs = millis();
        csiFramePending = true;
    }
    portEXIT_CRITICAL(&csiLock);
}

// CSI radio configuration
void initialiseCsi()
{
    csiEnabled = false;
    csi = CsiMotionDetector();
    portENTER_CRITICAL(&csiLock);
    csiPeerKnown = false;
    csiFramePending = false;
    portEXIT_CRITICAL(&csiLock);
    if (!ENABLE_CSI_WARNING)
    {
        return;
    }
    wifi_csi_config_t config = {};
    config.lltf_en = true; // Collect one 128-byte field, not several field formats.
    config.channel_filter_en = true;
    // Default ESP-NOW uses 802.11b. MCS0 provides 802.11n training for CSI.
    // Only the PHY rate changes; channel, packet format and message timing stay.
    if (esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20) != ESP_OK
        || esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_MCS0_LGI) != ESP_OK
        || esp_wifi_set_csi_config(&config) != ESP_OK
        || esp_wifi_set_csi_rx_cb(onCsiReceive, nullptr) != ESP_OK
        || esp_wifi_set_promiscuous(true) != ESP_OK || esp_wifi_set_csi(true) != ESP_OK)
    {
        esp_wifi_set_csi(false);
        esp_wifi_set_promiscuous(false);
        Serial.println("CSI unavailable; ultrasonic security continues.");
        return;
    }
    csiEnabled = true;
    Serial.println("CSI enabled: keep room still for 10 seconds after link-up.");
}

// Normalise amplitudes, calibrate noise and detect threshold crossings.
void updateCsiMotion(bool linkFresh)
{
    CsiFrame frame;
    portENTER_CRITICAL(&csiLock);
    bool available = csiFramePending;
    if (available)
    {
        frame = pendingCsiFrame;
        csiFramePending = false;
    }
    portEXIT_CRITICAL(&csiLock);
    uint32_t nowMs = millis();

    // Missing radio data is unavailable, never evidence that an area is clear.
    if (!csiEnabled || !linkFresh
        || (csi.hasPrevious && uint32_t(nowMs - csi.lastSampleMs) >= CSI_TIMEOUT_MS))
    {
        csi = CsiMotionDetector();
    }
    if (!csiEnabled || !linkFresh)
    {
        return;
    }
    if (csi.motion && uint32_t(nowMs - csi.lastMotionMs) >= CSI_WARNING_HOLD_MS)
    {
        csi.motion = false;
    }
    if (!available || uint32_t(nowMs - frame.receivedMs) >= CSI_TIMEOUT_MS)
    {
        return;
    }
    // Ignore bursts of immediate status replies; normal sampling is every 70 ms.
    if (csi.hasPrevious && uint32_t(frame.receivedMs - csi.lastSampleMs) < 40)
    {
        return;
    }

    float amplitude[CSI_BIN_COUNT];
    float meanAmplitude = 0;
    for (uint8_t index = 0; index < CSI_BIN_COUNT; index++)
    {
        int offset = CSI_BINS[index] * 2;
        float imaginary = frame.data[offset];
        float real = frame.data[offset + 1];
        amplitude[index] = sqrtf(real * real + imaginary * imaginary);
        meanAmplitude += amplitude[index];
    }
    meanAmplitude /= CSI_BIN_COUNT;
    if (meanAmplitude < 1.0f)
    {
        return; // Unusable signal; it must not refresh the valid-sample timeout.
    }

    float change = 0;
    for (uint8_t index = 0; index < CSI_BIN_COUNT; index++)
    {
        // Compare the shape across frequencies, reducing uniform gain changes.
        float normalised = amplitude[index] / meanAmplitude;
        change += fabsf(normalised - csi.previous[index]);
        csi.previous[index] = normalised;
    }
    csi.lastSampleMs = frame.receivedMs;
    if (!csi.hasPrevious)
    {
        csi.hasPrevious = true;
        csi.calibrationStartedMs = frame.receivedMs;
        queueCsiTelemetry(amplitude);
        return;
    }
    change *= 100.0f / CSI_BIN_COUNT;
    csi.score = change; // Show the same instantaneous score that triggers detection.

    if (!csi.calibrated)
    {
        csi.noiseSum += change;
        csi.noiseSquaredSum += change * change;
        csi.calibrationSamples++;
        if (uint32_t(frame.receivedMs - csi.calibrationStartedMs) >= CSI_CALIBRATION_MS
            && csi.calibrationSamples >= 80)
        {
            float meanNoise = csi.noiseSum / csi.calibrationSamples;
            float variance = csi.noiseSquaredSum / csi.calibrationSamples - meanNoise * meanNoise;
            csi.threshold = max(CSI_MIN_THRESHOLD,
                meanNoise + CSI_NOISE_MULTIPLIER * sqrtf(max(0.0f, variance)));
            csi.calibrated = true;
            if (!csiCsvEnabled && csiSerialLength == 0)
            {
                Serial.printf("CSI calibrated: threshold=%.2f. Reset board to recalibrate.\n",
                    csi.threshold);
            }
        }
        queueCsiTelemetry(amplitude);
        return;
    }

    // One usable sample above threshold is enough, even for a brief spike.
    // Hold the event for radio delivery; this is not a confirmation delay.
    if (csi.score > csi.threshold)
    {
        csi.motion = true;
        csi.lastMotionMs = frame.receivedMs;
    }
    queueCsiTelemetry(amplitude);
}

// CSI labels for Serial and the LCD
const char *csiStatusLabel()
{
    if (!csiEnabled)
    {
        return "CSI OFF";
    }
    if (!csi.hasPrevious)
    {
        return "CSI NO DATA";
    }
    if (!csi.calibrated)
    {
        return "CSI CALIBRATING";
    }
    return csi.motion ? "WIFI MOTION" : "CSI READY";
}

// Send local detector results only; echoing a shared warning would create feedback.
void addLocalCsiStatus(WirelessPacket &packet, uint32_t nowMs)
{
    packet.csiState = localCsiState();
    packet.csiScore100 = uint16_t(constrain(csi.score * 100.0f, 0.0f, 20000.0f));
    packet.csiThreshold100 = uint16_t(constrain(csi.threshold * 100.0f, 0.0f, 20000.0f));
    uint32_t age = csi.hasPrevious ? uint32_t(nowMs - csi.lastSampleMs) : 65535;
    packet.csiAgeMs = uint16_t(min(age, uint32_t(65535)));
    if (packet.csiState >= 2 && age >= CSI_TIMEOUT_MS)
    {
        packet.csiState = 1;
    }
}

void PeerCsiStatus::accept(const WirelessPacket &packet, uint32_t receivedMs)
{
    state = packet.csiState;
    score = packet.csiScore100 / 100.0f;
    threshold = packet.csiThreshold100 / 100.0f;
    sampleMs = receivedMs - packet.csiAgeMs;
}

uint8_t PeerCsiStatus::freshState(uint32_t nowMs, bool linkFresh) const
{
    if (!linkFresh || (state >= 2 && uint32_t(nowMs - sampleMs) >= CSI_TIMEOUT_MS))
    {
        return 1;
    }
    return state;
}

PeerCsiStatus peerCsi;

// Radio callbacks and transmission

void onWirelessReceive(const uint8_t *sourceMac, const uint8_t *data, int length)
{
    if (!sourceMac || !data || length != int(sizeof(WirelessPacket)))
    {
        return;
    }
    WirelessPacket packet;
    memcpy(&packet, data, sizeof(packet));
    if (!isValidWirelessPacket(packet))
    {
        return;
    }

    uint32_t receivedMs = millis();
    portENTER_CRITICAL(&wirelessLock);
    if (rxCount == RX_QUEUE_CAPACITY)
    {
        rxOverflow = true;
        rxTail = (rxTail + 1) % RX_QUEUE_CAPACITY;
        rxCount--;
    }
    receivedPackets[rxHead].packet = packet;
    memcpy(receivedPackets[rxHead].sourceMac, sourceMac, 6);
    receivedPackets[rxHead].receivedMs = receivedMs;
    rxHead = (rxHead + 1) % RX_QUEUE_CAPACITY;
    rxCount++;
    portEXIT_CRITICAL(&wirelessLock);
}

void onWirelessSend(const uint8_t *, esp_now_send_status_t)
{
    portENTER_CRITICAL(&wirelessLock);
    wirelessSendBusy = false;
    portEXIT_CRITICAL(&wirelessLock);
}

bool takeReceivedPacket(ReceivedPacket &received)
{
    portENTER_CRITICAL(&wirelessLock);
    bool available = rxCount > 0;
    if (available)
    {
        received = receivedPackets[rxTail];
        rxTail = (rxTail + 1) % RX_QUEUE_CAPACITY;
        rxCount--;
    }
    portEXIT_CRITICAL(&wirelessLock);
    return available;
}

bool takeQueueOverflow()
{
    portENTER_CRITICAL(&wirelessLock);
    bool overflowed = rxOverflow;
    rxOverflow = false;
    portEXIT_CRITICAL(&wirelessLock);
    return overflowed;
}

bool sendWirelessPacket(const WirelessPacket &packet)
{
    if (!wirelessReady)
    {
        return false;
    }
    portENTER_CRITICAL(&wirelessLock);
    bool canSend = !wirelessSendBusy;
    if (canSend)
    {
        wirelessSendBusy = true;
    }
    portEXIT_CRITICAL(&wirelessLock);
    if (!canSend)
    {
        return false;
    }

    esp_err_t result =
        esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
    if (result != ESP_OK)
    {
        portENTER_CRITICAL(&wirelessLock);
        wirelessSendBusy = false;
        portEXIT_CRITICAL(&wirelessLock);
        return false;
    }
    return true;
}

void initialiseWireless()
{
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    if (!WiFi.mode(WIFI_STA))
    {
        Serial.println("RADIO ERROR: Wi-Fi could not start");
        return;
    }
    WiFi.disconnect(false, false);
    WiFi.setSleep(false);
    if (esp_wifi_set_channel(WIRELESS_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK
        || esp_now_init() != ESP_OK)
    {
        Serial.println("RADIO ERROR: channel or ESP-NOW initialisation failed");
        return;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = WIRELESS_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK || esp_now_register_recv_cb(onWirelessReceive) != ESP_OK
        || esp_now_register_send_cb(onWirelessSend) != ESP_OK)
    {
        esp_now_deinit();
        Serial.println("RADIO ERROR: peer or callback setup failed");
        return;
    }
    wirelessReady = true;
    initialiseCsi();
    Serial.printf("ESP-NOW ready: MAC=%s channel=%u\n",
        WiFi.macAddress().c_str(),
        WIRELESS_CHANNEL);
}

// Remote sensor identity, packet freshness and application acknowledgements

bool hasRemotePeer = false;
uint8_t remotePeerMac[6] = {};
uint32_t remoteSession = 0;
uint32_t lastRemoteSequence = 0;
uint32_t acceptedRemotePackets = 0;
WirelessPacket pendingAcknowledgement;
bool hasPendingAcknowledgement = false;
uint32_t receiverSession = 0;
uint32_t nextStatusSequence = 0;
uint32_t lastStatusSentMs = 0;
uint8_t lastSentGuardState = 255;
uint8_t lastSentVaultState = 255;
bool lastSentSoundEnabled = false;

RemoteSensorPair remoteSensor;
bool sharedCsiWarning = false;
uint32_t warningStartedMs = 0;

// Remote events and status replies

void updateSharedCsiWarning(uint32_t nowMs)
{
    bool remoteMotion = peerCsi.freshState(nowMs, remoteSensor.isFresh(nowMs)) == 4;
    bool localMotion = csiEnabled && csi.calibrated && csi.motion && csi.hasPrevious
        && uint32_t(nowMs - csi.lastSampleMs) < CSI_TIMEOUT_MS;
    bool warning = localMotion || remoteMotion;
    if (warning != sharedCsiWarning)
    {
        if (warning)
        {
            warningStartedMs = nowMs;
        }
        sharedCsiWarning = warning;
        hasPendingAcknowledgement = true;
    }

    // Acknowledgment stays effective while the held CSI event expires.
    // Active movement prevents clearance; only a rearmed exhibit can alarm again.
    if (warning && guardController.currentState == GuardState::WaitingForClear)
    {
        guardController.clearSpaceStartedMs = 0;
    }
    bool mayTrigger = guardController.currentState == GuardState::Armed
        || (silentLinkFault && guardController.currentState == GuardState::AlarmLatched);
    if (warning && mayTrigger)
    {
        uint8_t cause = localMotion ? (remoteMotion ? 8 : 6) : 7;
        startSecurityAlarm(nowMs, remoteMotion, cause);
    }
}

void acceptRemotePacket(const ReceivedPacket &received, uint32_t nowMs)
{
    const WirelessPacket &packet = received.packet;
    if (packet.kind != SAMPLE_PACKET || uint32_t(nowMs - received.receivedMs) >= REMOTE_TIMEOUT_MS)
    {
        return;
    }

    if (hasRemotePeer && memcmp(remotePeerMac, received.sourceMac, 6) != 0)
    {
        return;
    }
    if (!hasRemotePeer)
    {
        memcpy(remotePeerMac, received.sourceMac, 6);
        bindCsiPeer(remotePeerMac);
        hasRemotePeer = true;
        remoteSession = packet.session;
        lastRemoteSequence = packet.sequence - 1;
        Serial.printf("REMOTE FOUND: %02X:%02X:%02X:%02X:%02X:%02X\n",
            remotePeerMac[0],
            remotePeerMac[1],
            remotePeerMac[2],
            remotePeerMac[3],
            remotePeerMac[4],
            remotePeerMac[5]);
    }

    if (packet.session != remoteSession)
    {
        // A reboot creates a new session. Wait out the old heartbeat before accepting it.
        if (remoteSensor.isFresh(received.receivedMs))
        {
            return;
        }
        remoteSession = packet.session;
        lastRemoteSequence = packet.sequence - 1;
        remoteSensor = RemoteSensorPair();
        bindCsiPeer(remotePeerMac);
    }
    if (int32_t(packet.sequence - lastRemoteSequence) <= 0)
    {
        return;
    }

    lastRemoteSequence = packet.sequence;
    acceptedRemotePackets++;
    peerCsi.accept(packet, received.receivedMs);
    bool intrusionConfirmed = remoteSensor.acceptPacket(packet,
        received.receivedMs,
        guardDistanceCm,
        guardController.currentState == GuardState::Armed
            || (silentLinkFault && guardController.currentState == GuardState::AlarmLatched),
        uint32_t(nowMs - received.receivedMs));
    if (intrusionConfirmed)
    {
        Serial.printf("Remote trigger mask: %u (1=sensor 1, 2=sensor 2, 3=both)\n",
            remoteSensor.lastTriggerMask);
        startSecurityAlarm(nowMs, true);
    }
    pendingAcknowledgement = packet;
    pendingAcknowledgement.kind = ACK_PACKET;
    hasPendingAcknowledgement = true;
}

void updateWirelessReceiver(uint32_t nowMs)
{
    if (takeQueueOverflow())
    {
        remoteSensor.resetConfirmation();
    }

    ReceivedPacket received;
    for (uint8_t packetIndex = 0; packetIndex < RX_QUEUE_CAPACITY; packetIndex++)
    {
        if (!takeReceivedPacket(received))
        {
            break;
        }
        // Read time after copying the callback's timestamp, never before it.
        nowMs = millis();
        acceptRemotePacket(received, nowMs);
    }

    remoteSensor.expire(millis());
}

// Run after local sensing and button handling. Send state changes promptly;
// periodic replies recover from dropped updates even without a new sensor sample.
void publishReceiverStatus(uint32_t nowMs)
{
    if (!hasRemotePeer)
    {
        return;
    }
    uint8_t guardState = uint8_t(guardController.currentState);
    uint8_t vaultState = uint8_t(vaultAnimation.currentState);
    bool stateChanged = guardState != lastSentGuardState || vaultState != lastSentVaultState
        || isSoundEnabled != lastSentSoundEnabled;
    if (!hasPendingAcknowledgement && !stateChanged
        && uint32_t(nowMs - lastStatusSentMs) < STATUS_INTERVAL_MS)
    {
        return;
    }

    WirelessPacket status = pendingAcknowledgement;
    status.kind = ACK_PACKET;
    status.receiverSession = receiverSession;
    status.statusSequence = nextStatusSequence + 1;
    status.guardState = guardState;
    status.vaultState = vaultState;
    status.soundEnabled = isSoundEnabled;
    status.guardDistanceCm = uint8_t(guardDistanceCm);
    status.alarmCause = alarmCause;
    status.triggerDistanceMm = triggerDistanceMm;
    status.sharedCsiWarning = sharedCsiWarning;
    status.warningElapsedMs = sharedCsiWarning ? uint32_t(nowMs - warningStartedMs) : 0;
    addLocalCsiStatus(status, nowMs); // Replace the sender's CSI fields, never echo them.
    status.alarmElapsedMs = guardController.currentState == GuardState::AlarmLatched
        ? uint32_t(nowMs - alarmStartedMs)
        : 0;
    if (sendWirelessPacket(status))
    {
        nextStatusSequence = status.statusSequence;
        lastStatusSentMs = nowMs;
        lastSentGuardState = guardState;
        lastSentVaultState = vaultState;
        lastSentSoundEnabled = isSoundEnabled;
        hasPendingAcknowledgement = false;
    }
}

const char *getRemoteLinkLabel(uint32_t nowMs)
{
    if (!wirelessReady)
    {
        return "RADIO ERROR";
    }
    if (!remoteSensor.hasPacket)
    {
        return "REMOTE WAIT";
    }
    if (!remoteSensor.isFresh(nowMs))
    {
        return "REMOTE LOST";
    }
    for (uint8_t sensor = 0; sensor < REMOTE_SENSOR_COUNT; sensor++)
    {
        if (remoteSensor.sensors[sensor].readingValid && remoteSensor.sensors[sensor].isFresh(nowMs)
            && remoteSensor.sensors[sensor].distanceMillimetres <= guardDistanceCm * 10)
        {
            return "REMOTE NEAR";
        }
    }
    if (!remoteSensor.sensors[0].readingValid || !remoteSensor.sensors[1].readingValid
        || !remoteSensor.sensors[0].isFresh(nowMs) || !remoteSensor.sensors[1].isFresh(nowMs))
    {
        return "REMOTE NO ECHO";
    }
    return "REMOTE LINK OK";
}
