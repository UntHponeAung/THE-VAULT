// Owns the alarm decision, local distance sensing, vault movement and buzzer.

#include "firmware.h"

GuardController guardController;
VaultAnimator vaultAnimation;
bool silentLinkFault = false;
uint8_t alarmCause = 0;
uint16_t triggerDistanceMm = 0;
GuardController faultProximityGuard; // Local sensing can escalate a silent fault to an intrusion.
uint32_t alarmStartedMs = 0;

// Guard state machine

void GuardController::startArming(uint32_t nowMs)
{
    currentState = GuardState::Arming;
    armingStartedMs = nowMs;
    nearObjectStartedMs = clearSpaceStartedMs = 0;
}

void GuardController::acknowledgeAlarm()
{
    if (currentState == GuardState::AlarmLatched)
    {
        currentState = GuardState::WaitingForClear;
        nearObjectStartedMs = clearSpaceStartedMs = 0;
    }
}

void GuardController::processDistanceSample(bool isReadingValid,
    float measuredDistanceCm,
    int alarmBoundaryCm,
    uint32_t nowMs,
    bool additionalClearanceReady)
{
    if (currentState == GuardState::Off || currentState == GuardState::AlarmLatched)
    {
        return;
    }

    if (currentState == GuardState::Arming)
    {
        if ((uint32_t)(nowMs - armingStartedMs) < 1200)
        {
            return;
        }

        currentState = GuardState::Armed;
    }

    // A missing echo resets both confirmation windows; it cannot prove clearance.
    if (!isReadingValid)
    {
        nearObjectStartedMs = clearSpaceStartedMs = 0;
        return;
    }

    if (currentState == GuardState::Armed)
    {
        if (measuredDistanceCm <= alarmBoundaryCm)
        {
            if (!nearObjectStartedMs)
            {
                nearObjectStartedMs = nowMs;
            }

            if ((uint32_t)(nowMs - nearObjectStartedMs) >= 140)
            {
                currentState = GuardState::AlarmLatched;
            }
        }
        else
        {
            nearObjectStartedMs = 0;
        }
    }
    else if (currentState == GuardState::WaitingForClear)
    {
        if (measuredDistanceCm >= alarmBoundaryCm + 5)
        {
            if (!clearSpaceStartedMs)
            {
                clearSpaceStartedMs = nowMs;
            }

            if ((uint32_t)(nowMs - clearSpaceStartedMs) >= 600 && additionalClearanceReady)
            {
                currentState = GuardState::Armed;
                clearSpaceStartedMs = nearObjectStartedMs = 0;
            }
        }
        else
        {
            clearSpaceStartedMs = 0;
        }
    }
}

// Vault animation

void VaultAnimator::reset(uint32_t nowMs)
{
    currentState = VaultState::Displaying;
    diamondLiftPermille = 1000;
    shutterClosedPermille = 0;
    phaseStartedMs = nowMs;
    lastUpdateMs = nowMs;
}

void VaultAnimator::requestSeal(uint32_t nowMs)
{
    if (currentState == VaultState::Retracting || currentState == VaultState::Sealing
        || currentState == VaultState::Sealed)
    {
        return;
    }

    lastUpdateMs = nowMs;
    if (diamondLiftPermille > 0)
    {
        beginMotion(VaultState::Retracting, diamondLiftPermille, nowMs);
    }
    else
    {
        beginMotion(VaultState::Sealing, shutterClosedPermille, nowMs);
    }
}

void VaultAnimator::update(uint32_t nowMs, bool mustStaySealed, bool mayRevealDiamond)
{
    uint32_t elapsedSinceUpdateMs = nowMs - lastUpdateMs;
    lastUpdateMs = nowMs;

    if (mustStaySealed)
    {
        requestSeal(nowMs);
    }

    // Uncertain readings or an open menu pause recovery, never the closing motion.
    if ((currentState == VaultState::Opening || currentState == VaultState::Raising)
        && !mayRevealDiamond)
    {
        phaseStartedMs += elapsedSinceUpdateMs;
        return;
    }

    uint32_t elapsedMs = nowMs - phaseStartedMs;
    if (currentState == VaultState::Sealed)
    {
        // Show a completed lock before recovery, even after an immediate acknowledgement.
        if (!mustStaySealed && mayRevealDiamond && elapsedMs >= SEALED_HOLD_MS)
        {
            beginMotion(VaultState::Opening, shutterClosedPermille, nowMs);
        }
        return;
    }

    if (currentState == VaultState::Displaying)
    {
        return;
    }

    uint32_t progressPermille =
        elapsedMs >= phaseDurationMs ? 1000 : elapsedMs * 1000 / phaseDurationMs;
    uint32_t phaseFinishedMs = phaseStartedMs + phaseDurationMs;

    switch (currentState)
    {
    case VaultState::Retracting:
        diamondLiftPermille = phaseStartPosition * (1000 - progressPermille) / 1000;
        if (progressPermille == 1000)
        {
            beginMotion(VaultState::Sealing, shutterClosedPermille, phaseFinishedMs);
        }
        break;

    case VaultState::Sealing:
        shutterClosedPermille =
            phaseStartPosition + (1000 - phaseStartPosition) * progressPermille / 1000;
        if (progressPermille == 1000)
        {
            currentState = VaultState::Sealed;
            phaseStartedMs = nowMs;
        }
        break;

    case VaultState::Opening:
        shutterClosedPermille = phaseStartPosition * (1000 - progressPermille) / 1000;
        if (progressPermille == 1000)
        {
            beginMotion(VaultState::Raising, diamondLiftPermille, phaseFinishedMs);
        }
        break;

    case VaultState::Raising:
        diamondLiftPermille =
            phaseStartPosition + (1000 - phaseStartPosition) * progressPermille / 1000;
        if (progressPermille == 1000)
        {
            currentState = VaultState::Displaying;
        }
        break;

    default:
        break;
    }
}

void VaultAnimator::beginMotion(VaultState nextState, uint16_t startPosition, uint32_t nowMs)
{
    currentState = nextState;
    phaseStartedMs = nowMs;
    phaseStartPosition = startPosition;

    bool isDiamondMoving = nextState == VaultState::Retracting || nextState == VaultState::Raising;
    bool isPositionIncreasing =
        nextState == VaultState::Sealing || nextState == VaultState::Raising;
    uint32_t distanceToTravel = isPositionIncreasing ? 1000 - startPosition : startPosition;
    uint32_t fullTravelMs = isDiamondMoving ? DIAMOND_TRAVEL_MS : SHUTTER_TRAVEL_MS;
    phaseDurationMs = fullTravelMs * distanceToTravel / 1000;
    if (phaseDurationMs == 0)
    {
        phaseDurationMs = 1;
    }
}

// Shared siren and chirp timing

int sirenFrequency(uint32_t elapsedMs)
{
    uint32_t phase = elapsedMs % 1600;
    uint32_t ramp = phase <= 800 ? phase : 1600 - phase;
    return 850 + int(ramp / 20) * 25; // 850 -> 1850 -> 850 Hz; no blocking delays.
}

int warningChirpFrequency(uint32_t elapsedMs)
{
    return elapsedMs % 3000 < 80 ? 2200 : 0;
}

// Buzzer playback

struct SoundNote
{
    uint16_t frequencyHz;
    uint16_t durationMs;
};

const SoundNote STARTUP_MELODY[] = {{523, 80}, {0, 35}, {659, 80}, {784, 130}};
const SoundNote BUTTON_CLICK_SOUND[] = {{1100, 35}};

const SoundNote *activeMelody = nullptr;
int activeMelodyLength = 0;
int nextNoteIndex = 0;
uint32_t currentNoteEndsMs = 0;
int currentBuzzerFrequencyHz = -1;

void setBuzzerFrequency(int frequencyHz)
{
    if (!isSoundEnabled)
    {
        frequencyHz = 0;
    }

    if (frequencyHz != currentBuzzerFrequencyHz)
    {
        ledcWriteTone(BUZZER_PWM_CHANNEL, frequencyHz);
        currentBuzzerFrequencyHz = frequencyHz;
    }
}

void stopSound()
{
    activeMelody = nullptr;
    setBuzzerFrequency(0);
}

template <size_t NOTE_COUNT> void playMelody(const SoundNote (&notesToPlay)[NOTE_COUNT])
{
    if (!isSoundEnabled || guardController.currentState == GuardState::AlarmLatched)
    {
        return;
    }

    // Global melody arrays remain valid until playback finishes.
    activeMelody = notesToPlay;
    activeMelodyLength = (int)NOTE_COUNT;
    nextNoteIndex = 0;
    currentNoteEndsMs = millis();
}

void updateBuzzer(uint32_t nowMs)
{
    if (guardController.currentState == GuardState::AlarmLatched)
    {
        activeMelody = nullptr;
        setBuzzerFrequency(silentLinkFault ? 0 : sirenFrequency(nowMs - alarmStartedMs));
        return;
    }

    if (guardController.currentState == GuardState::WaitingForClear)
    {
        activeMelody = nullptr;
        setBuzzerFrequency(0);
        return;
    }

    if (sharedCsiWarning && guardController.currentState == GuardState::Armed)
    {
        activeMelody = nullptr;
        setBuzzerFrequency(warningChirpFrequency(nowMs - warningStartedMs));
        return;
    }

    if (!activeMelody)
    {
        setBuzzerFrequency(0);
        return;
    }

    if ((int32_t)(nowMs - currentNoteEndsMs) >= 0)
    {
        if (nextNoteIndex >= activeMelodyLength)
        {
            stopSound();
            return;
        }

        const SoundNote nextNote = activeMelody[nextNoteIndex++];
        setBuzzerFrequency(nextNote.frequencyHz);
        currentNoteEndsMs = nowMs + nextNote.durationMs;
    }
}

// Other modules start sounds through these helpers.
void playStartupSound()
{
    playMelody(STARTUP_MELODY);
}

void playButtonClick()
{
    playMelody(BUTTON_CLICK_SOUND);
}

// Alarm and link-loss handling

void startSecurityAlarm(uint32_t nowMs, bool triggeredByRemote, uint8_t csiCause)
{
    guardController.currentState = GuardState::AlarmLatched;
    silentLinkFault = false;
    alarmCause = csiCause != 0 ? csiCause
        : triggeredByRemote    ? uint8_t(remoteSensor.lastTriggerMask + 1)
                               : 1;
    if (csiCause != 0)
    {
        triggerDistanceMm = 0; // CSI measures change, never distance.
    }
    else if (triggeredByRemote)
    {
        uint8_t sensor = remoteSensor.lastTriggerMask == 2 ? 1 : 0;
        triggerDistanceMm = remoteSensor.sensors[sensor].distanceMillimetres;
        if (remoteSensor.lastTriggerMask == 3)
        {
            triggerDistanceMm = min(triggerDistanceMm, remoteSensor.sensors[1].distanceMillimetres);
        }
    }
    remoteSensor.resetConfirmation();
    hasPendingAcknowledgement = true;
    vaultAnimation.requestSeal(nowMs);
    alarmStartedMs = nowMs;
    updateBuzzer(nowMs);
    nextAnimationFrameMs = nowMs;
    isSettingsScreenOpen = false;
    isDiagnosticsScreenOpen = false;
    Serial.printf("ALARM: %s sensor triggered; press joystick to acknowledge\n",
        csiCause != 0           ? "CSI"
            : triggeredByRemote ? "REMOTE"
                                : "LOCAL");
}

void updateLinkSafety(uint32_t nowMs)
{
    if (guardController.currentState == GuardState::Arming
        && uint32_t(nowMs - guardController.armingStartedMs) >= 1200)
    {
        guardController.processDistanceSample(false, 0, guardDistanceCm, nowMs);
    }
    if (guardController.currentState != GuardState::Armed || remoteSensor.isFresh(nowMs))
    {
        return;
    }
    guardController.currentState = GuardState::AlarmLatched;
    guardController.nearObjectStartedMs = 0;
    guardController.clearSpaceStartedMs = 0;
    silentLinkFault = true;
    alarmCause = 5;
    triggerDistanceMm = 0;
    faultProximityGuard = GuardController();
    faultProximityGuard.currentState = GuardState::Armed;
    remoteSensor.resetConfirmation();
    vaultAnimation.requestSeal(nowMs);
    alarmStartedMs = nowMs;
    stopSound();
    hasPendingAcknowledgement = true;
    isSettingsScreenOpen = false;
    isDiagnosticsScreenOpen = false;
    nextAnimationFrameMs = nowMs;
    Serial.println("LINK LOST: vault sealing silently; acknowledge, then restore valid clearance.");
}

// Local ultrasonic sensor

// Volatile values shared with the ISR must also be protected by this lock.
portMUX_TYPE echoCaptureLock = portMUX_INITIALIZER_UNLOCKED;
volatile bool isListeningForEcho = false;
volatile bool hasEchoStarted = false;
volatile bool isEchoPulseComplete = false;
volatile uint32_t echoRiseTimeUs = 0;
volatile uint32_t echoPulseDurationUs = 0;

bool isMeasurementPending = false;
bool hasValidDistance = false;
bool isLatestDistanceSampleValid = false;
uint32_t measurementStartedUs = 0;
uint32_t lastMeasurementStartedMs = 0;
uint32_t lastValidDistanceMs = 0;

float filteredDistanceCm = 0;
float recentDistancesCm[3] = {0, 0, 0};
int distanceHistoryCount = 0;
int nextDistanceHistoryIndex = 0;
int consecutiveMissedEchoes = 0;

// Keep the ISR short: capture edges only; no display, sound or flash operations.
void IRAM_ATTR captureEchoEdge()
{
    uint32_t currentTimeUs = micros();
    bool isEchoPinHigh = digitalRead(ULTRASONIC_ECHO_PIN);
    portENTER_CRITICAL_ISR(&echoCaptureLock);
    if (isListeningForEcho)
    {
        if (isEchoPinHigh)
        {
            echoRiseTimeUs = currentTimeUs;
            hasEchoStarted = true;
        }
        else if (hasEchoStarted)
        {
            echoPulseDurationUs = currentTimeUs - echoRiseTimeUs;
            isEchoPulseComplete = true;
            isListeningForEcho = false;
        }
    }

    portEXIT_CRITICAL_ISR(&echoCaptureLock);
}

// Guard uses the validity of this sample, not just the retained filtered distance.
void processEchoMeasurement(uint32_t pulseWidthUs, uint32_t nowMs)
{
    // Sound travels about 0.0343 cm/us; Echo measures the round trip.
    float rawDistanceCm = pulseWidthUs * 0.0343f / 2.0f;
    bool isNewReadingValid = pulseWidthUs > 0 && rawDistanceCm >= 2 && rawDistanceCm <= 400;

    isLatestDistanceSampleValid = isNewReadingValid;

    if (isNewReadingValid)
    {
        consecutiveMissedEchoes = 0;
        lastValidDistanceMs = nowMs;
        recentDistancesCm[nextDistanceHistoryIndex] = rawDistanceCm;
        nextDistanceHistoryIndex = (nextDistanceHistoryIndex + 1) % 3;
        if (distanceHistoryCount < 3)
        {
            distanceHistoryCount++;
        }

        // One reading is used directly; two readings retain the upper value.
        filteredDistanceCm = recentDistancesCm[0];
        if (distanceHistoryCount >= 2)
        {
            float nearerDistanceCm = min(recentDistancesCm[0], recentDistancesCm[1]);
            float fartherDistanceCm = max(recentDistancesCm[0], recentDistancesCm[1]);

            // For three readings, clamp the third between the other two to get the median.
            filteredDistanceCm = distanceHistoryCount == 2
                ? fartherDistanceCm
                : constrain(recentDistancesCm[2], nearerDistanceCm, fartherDistanceCm);
        }
        hasValidDistance = true;
    }
    else
    {
        consecutiveMissedEchoes++;
        if (consecutiveMissedEchoes >= 3)
        {
            hasValidDistance = false;
            distanceHistoryCount = nextDistanceHistoryIndex = 0;
        }
    }

    // A confirmed local intrusion upgrades a silently latched radio fault.
    if (silentLinkFault && guardController.currentState == GuardState::AlarmLatched)
    {
        faultProximityGuard.processDistanceSample(isNewReadingValid,
            filteredDistanceCm,
            guardDistanceCm,
            nowMs);
        if (faultProximityGuard.currentState == GuardState::AlarmLatched)
        {
            triggerDistanceMm = uint16_t(filteredDistanceCm * 10.0f);
            startSecurityAlarm(nowMs, false);
        }
    }

    GuardState previousGuardState = guardController.currentState;
    guardController.processDistanceSample(isNewReadingValid,
        filteredDistanceCm,
        guardDistanceCm,
        nowMs,
        remoteSensor.isClear(nowMs, guardDistanceCm) && !sharedCsiWarning);
    if (guardController.currentState != previousGuardState)
    {
        if (guardController.currentState == GuardState::AlarmLatched)
        {
            triggerDistanceMm = uint16_t(filteredDistanceCm * 10.0f);
            startSecurityAlarm(nowMs, false);
        }
        else if (guardController.currentState == GuardState::Armed)
        {
            alarmCause = 0;
            silentLinkFault = false;
        }
    }
}

void updateDistanceSensor(uint32_t nowMs)
{
    if (isMeasurementPending)
    {
        bool hasCompletePulse;
        uint32_t capturedPulseUs;
        portENTER_CRITICAL(&echoCaptureLock);
        hasCompletePulse = isEchoPulseComplete;
        capturedPulseUs = echoPulseDurationUs;
        portEXIT_CRITICAL(&echoCaptureLock);
        if (hasCompletePulse
            || uint32_t(micros() - measurementStartedUs) >= ECHO_TIMEOUT_MICROSECONDS)
        {
            portENTER_CRITICAL(&echoCaptureLock);
            isListeningForEcho = false;
            isEchoPulseComplete = false;
            portEXIT_CRITICAL(&echoCaptureLock);
            isMeasurementPending = false;
            processEchoMeasurement(hasCompletePulse ? capturedPulseUs : 0, nowMs);
        }
    }

    if (!isMeasurementPending
        && uint32_t(nowMs - lastMeasurementStartedMs) >= MEASUREMENT_INTERVAL_MS)
    {
        lastMeasurementStartedMs = nowMs;
        measurementStartedUs = micros();
        isMeasurementPending = true;
        portENTER_CRITICAL(&echoCaptureLock);
        isEchoPulseComplete = false;
        hasEchoStarted = false;
        isListeningForEcho = true;
        portEXIT_CRITICAL(&echoCaptureLock);
        digitalWrite(ULTRASONIC_TRIGGER_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(ULTRASONIC_TRIGGER_PIN, LOW);
    }

    if (hasValidDistance && uint32_t(nowMs - lastValidDistanceMs) > 350)
    {
        hasValidDistance = false;
    }
}

// Coordinating the guard and vault

bool canRevealDiamond()
{
    return guardController.currentState == GuardState::Armed && isLatestDistanceSampleValid
        && hasValidDistance && filteredDistanceCm >= guardDistanceCm + 5
        && remoteSensor.isClear(millis(), guardDistanceCm) && !isSettingsScreenOpen;
}

void updateVaultAnimation(uint32_t nowMs)
{
    bool mustStaySealed = guardController.currentState == GuardState::AlarmLatched
        || guardController.currentState == GuardState::WaitingForClear;
    vaultAnimation.update(nowMs, mustStaySealed, canRevealDiamond());
}
