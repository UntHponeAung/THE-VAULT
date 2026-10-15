// Sends distance reports, captures CSI, and receives the main node's authoritative state.

#include "firmware.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>
#include <string.h>

// Packet validation
// Format checks reject malformed traffic; they do not authenticate the sender.

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

uint32_t senderSession = 0;
uint32_t lastSentSequence = 0;

// Radio configuration and receive queue
// Broadcast transport on a shared channel; no router connection or encryption.
// Copy callback data into owned storage so loop() can process it after return.

constexpr uint8_t WIRELESS_CHANNEL = 1;
constexpr uint8_t RX_QUEUE_CAPACITY = 8;
const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct ReceivedPacket
{
    WirelessPacket packet;
    uint8_t sourceMac[6];
    uint32_t receivedMs;
};

// The queue and send-busy flag are shared with Wi-Fi callbacks. All accesses
// use this lock; keep radio calls and logging outside critical sections.
portMUX_TYPE wirelessLock = portMUX_INITIALIZER_UNLOCKED;
ReceivedPacket receivedPackets[RX_QUEUE_CAPACITY];
// Head is the next write slot; tail is the oldest unread slot.
uint8_t rxHead = 0;
uint8_t rxTail = 0;
uint8_t rxCount = 0;
bool rxOverflow = false;
bool wirelessSendBusy = false;
bool wirelessReady = false;

// CSI sampling
// Existing 70 ms sensor/status traffic supplies the training fields.
// Threshold crossings are held for delivery; the receiver latches the alarm.
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

// USB telemetry for MATLAB (m = start, n = stop)
// Queue one line and send it only when it fits in the UART buffer. If the PC is
// slow, skip a sample rather than delaying sensing or alarm handling.
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

// CSI uses the same peer selected by the ESP-NOW status exchange.
void bindCsiPeer(const uint8_t *mac)
{
    portENTER_CRITICAL(&csiLock);
    memcpy(csiPeerMac, mac, 6);
    csiPeerKnown = true;
    csiFramePending = false;
    portEXIT_CRITICAL(&csiLock);
    csi = CsiMotionDetector();
}

// Keep the Wi-Fi callback short; signal processing runs in loop().
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
        // If loop() falls behind, replace the pending frame with the newest one.
        memcpy(pendingCsiFrame.data, info->buf, sizeof(pendingCsiFrame.data));
        pendingCsiFrame.receivedMs = millis();
        csiFramePending = true;
    }
    portEXIT_CRITICAL(&csiLock);
}

// CSI radio setup
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
    config.lltf_en = true; // This parser expects the 128-byte LLTF field.
    config.channel_filter_en = true;
    // Default ESP-NOW uses 802.11b. MCS0 provides 802.11n training for CSI.
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

// Normalisation, noise calibration and threshold detection
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

    // A data gap invalidates the baseline and requires a new calibration.
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
    csi.score = change; // Telemetry and detection use the same instantaneous value.

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

// Detector status
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

// Send only local CSI results, avoiding feedback from the combined warning.
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

// Receive callback
// Validate and copy here; loop() applies receiver status and acknowledgments.
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

    // Queue residence time counts toward the freshness limit.
    uint32_t receivedMs = millis();
    portENTER_CRITICAL(&wirelessLock);
    // Drop the oldest message on overflow and invalidate ACK confidence in loop().
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

// Send completion permits another attempt, regardless of delivery outcome.
// A matching application ACK confirms that the receiver processed a sample.
void onWirelessSend(const uint8_t *, esp_now_send_status_t)
{
    portENTER_CRITICAL(&wirelessLock);
    wirelessSendBusy = false;
    portEXIT_CRITICAL(&wirelessLock);
}

// Main-loop queue access
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

// Read and clear atomically so a concurrent callback cannot lose the flag.
bool takeQueueOverflow()
{
    portENTER_CRITICAL(&wirelessLock);
    bool overflowed = rxOverflow;
    rxOverflow = false;
    portEXIT_CRITICAL(&wirelessLock);
    return overflowed;
}

// Packet transmission
// Success means accepted by the radio, not received or acknowledged.
bool sendWirelessPacket(const WirelessPacket &packet)
{
    if (!wirelessReady)
    {
        return false;
    }
    portENTER_CRITICAL(&wirelessLock);
    // Reserve the send slot under the callback's lock, then call the radio outside it.
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
    // Release the slot here on submission failure; completion handles accepted sends.
    if (result != ESP_OK)
    {
        portENTER_CRITICAL(&wirelessLock);
        wirelessSendBusy = false;
        portEXIT_CRITICAL(&wirelessLock);
        return false;
    }
    return true;
}

// Radio startup
// ESP-NOW uses the station interface without joining an access point.
void initialiseWireless()
{
    // Avoid saving Wi-Fi settings to flash or automatically rejoining a router.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    if (!WiFi.mode(WIFI_STA))
    {
        Serial.println("RADIO ERROR: Wi-Fi could not start");
        return;
    }
    // Keep the radio on and saved credentials intact; disable sleep for prompt replies.
    WiFi.disconnect(false, false);
    WiFi.setSleep(false);
    // Both boards must use the same channel.
    if (esp_wifi_set_channel(WIRELESS_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK
        || esp_now_init() != ESP_OK)
    {
        Serial.println("RADIO ERROR: channel or ESP-NOW initialisation failed");
        return;
    }

    // Registering a broadcast destination does not pair or authenticate a receiver.
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

// Sensor reports

WirelessPacket makeSensorPacket(uint32_t nowMs)
{
    WirelessPacket packet;
    packet.session = senderSession;
    packet.sequence = lastSentSequence + 1;
    addLocalCsiStatus(packet, nowMs);
    for (uint8_t sensor = 0; sensor < REMOTE_SENSOR_COUNT; sensor++)
    {
        const SensorReading &reading = reportedReadings[sensor];
        uint32_t age = reading.hasSample ? uint32_t(nowMs - reading.measuredMs) : 65535;
        packet.sensorSequence[sensor] = reading.sequence;
        packet.sampleAgeMs[sensor] = uint16_t(min(age, uint32_t(65535)));
        bool valid = reading.hasSample && reading.valid && age < REMOTE_TIMEOUT_MS;
        if (valid)
        {
            packet.readingValidMask |= (1U << sensor);
            packet.distanceMillimetres[sensor] = reading.distanceMm;
        }
    }
    return packet;
}

// Receiver status, application ACKs and output timing

bool hasReceiverAck = false;
bool receiverStatusTrusted = false;
uint8_t receiverMac[6] = {};
uint32_t receiverSession = 0;
uint32_t lastStatusSequence = 0;
uint32_t lastAckSequence = 0;
uint32_t lastAckMs = 0;
uint32_t lastSampleAckMs = 0;
uint32_t receivedAckCount = 0;
uint8_t receiverGuardState = 0;
uint8_t receiverVaultState = 0;
bool receiverSoundEnabled = false;
uint32_t receiverAlarmElapsedMs = 0;
bool receiverSharedCsiWarning = false;
uint32_t receiverWarningElapsedMs = 0;
uint8_t receiverAlarmCause = 0;
uint8_t sharedGuardDistanceCm = 30;

bool receiverStatusIsFresh(uint32_t nowMs)
{
    return hasReceiverAck && receiverStatusTrusted
        && uint32_t(nowMs - lastAckMs) < REMOTE_TIMEOUT_MS;
}

void processReceiverAcknowledgements(uint32_t nowMs)
{
    if (takeQueueOverflow())
    {
        // Mark status stale, preserving the selected peer and any latched alarm.
        receiverStatusTrusted = false;
    }
    ReceivedPacket received;
    for (uint8_t count = 0; count < RX_QUEUE_CAPACITY; count++)
    {
        if (!takeReceivedPacket(received))
        {
            break;
        }
        // Read time after copying the callback's timestamp, never before it.
        nowMs = millis();
        const WirelessPacket &packet = received.packet;
        if (packet.kind != ACK_PACKET || packet.session != senderSession
            || uint32_t(nowMs - received.receivedMs) >= REMOTE_TIMEOUT_MS
            || int32_t(lastSentSequence - packet.sequence) < 0)
        {
            continue;
        }
        if (hasReceiverAck && memcmp(receiverMac, received.sourceMac, 6) != 0)
        {
            continue;
        }
        bool newReceiverBoot = hasReceiverAck && packet.receiverSession != receiverSession;
        if (newReceiverBoot && receiverStatusIsFresh(received.receivedMs))
        {
            continue;
        }
        if (hasReceiverAck && !newReceiverBoot
            && (int32_t(packet.statusSequence - lastStatusSequence) <= 0
                || int32_t(packet.sequence - lastAckSequence) < 0))
        {
            continue;
        }
        if (!hasReceiverAck || newReceiverBoot)
        {
            memcpy(receiverMac, received.sourceMac, 6);
            bindCsiPeer(receiverMac);
        }
        if (!hasReceiverAck || newReceiverBoot || int32_t(packet.sequence - lastAckSequence) > 0)
        {
            lastSampleAckMs = received.receivedMs;
        }
        hasReceiverAck = true;
        receiverStatusTrusted = true;
        receiverSession = packet.receiverSession;
        lastStatusSequence = packet.statusSequence;
        lastAckSequence = packet.sequence;
        lastAckMs = received.receivedMs;
        receivedAckCount++;
        receiverGuardState = packet.guardState;
        receiverVaultState = packet.vaultState;
        receiverSoundEnabled = packet.soundEnabled != 0;
        receiverAlarmElapsedMs = packet.alarmElapsedMs;
        receiverSharedCsiWarning = packet.sharedCsiWarning != 0;
        receiverWarningElapsedMs = packet.warningElapsedMs;
        receiverAlarmCause = packet.alarmCause;
        sharedGuardDistanceCm = packet.guardDistanceCm;
        peerCsi.accept(packet, received.receivedMs);
    }
}
