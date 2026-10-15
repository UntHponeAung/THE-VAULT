// Draws the exhibit, settings and diagnostic page from the current system state.

#include "firmware.h"
#include <math.h>

TFT_eSPI display;
TFT_eSprite sceneBuffer(&display);

// The 4-bit scene buffer uses 54,400 pixel bytes and 16 palette colours.
// Sprite drawing uses Ink indices; direct LCD drawing uses RGB565 values.

enum SceneInk : uint8_t
{
    InkBackground,
    InkBeamOuter,
    InkBeamMiddle,
    InkBeamInner,
    InkBeamCore,
    InkDeepBlue,
    InkBlue,
    InkCyan,
    InkIce,
    InkWhite,
    InkMetalDark,
    InkMetalFace,
    InkMetalLit,
    InkSilver,
    InkShadow,
    InkMovingFacet
};

const uint16_t SCENE_PALETTE[16] = {
    0x0843, // Background
    0x10C4, // BeamOuter
    0x1927, // BeamMiddle
    0x2189, // BeamInner
    0x29EC, // BeamCore
    0x1270, // DeepBlue
    0x2495, // Blue
    0x4DDB, // Cyan
    0x9F3D, // Ice
    0xEFFF, // White
    0x1906, // MetalDark
    0x322A, // MetalFace
    0x6BEF, // MetalLit
    0xB5F8, // Silver
    0x0021, // Shadow
    0x5D9B  // MovingFacet
};

// Display text

void drawDisplayText(const char *text, int screenX, int screenY, uint16_t textColor, int textScale)
{
    display.setTextSize(textScale);
    display.setTextColor(textColor, BACKGROUND_COLOR);
    display.drawString(text, screenX, screenY, 1);
}

// Colour and animation helpers

uint16_t blendColor(uint16_t firstColor, uint16_t secondColor, int secondWeight)
{
    int firstWeight = 255 - secondWeight;
    int red =
        (((firstColor >> 11) & 31) * firstWeight + ((secondColor >> 11) & 31) * secondWeight) / 255;
    int green =
        (((firstColor >> 5) & 63) * firstWeight + ((secondColor >> 5) & 63) * secondWeight) / 255;
    int blue = ((firstColor & 31) * firstWeight + (secondColor & 31) * secondWeight) / 255;
    return (red << 11) | (green << 5) | blue;
}

// Ease position rather than phase time: reversing a transition never jumps the gem.
float easedPosition(uint16_t positionPermille)
{
    float position = positionPermille / 1000.0f;
    return position * position * (3.0f - 2.0f * position);
}

float alarmLightStrength(uint32_t nowMs)
{
    if (guardController.currentState != GuardState::AlarmLatched)
    {
        return 0.0f;
    }

    float phase = ((nowMs - alarmStartedMs) % 960) * 0.006544985f;
    return 0.65f + 0.35f * cosf(phase);
}

void updateScenePalette(uint32_t nowMs)
{
    // Updating existing palette entries does not allocate memory.
    int alarmTint = int(alarmLightStrength(nowMs) * 185.0f);
    sceneBuffer.setPaletteColor(InkBeamOuter,
        blendColor(SCENE_PALETTE[InkBeamOuter], 0x2002, alarmTint));
    sceneBuffer.setPaletteColor(InkBeamMiddle,
        blendColor(SCENE_PALETTE[InkBeamMiddle], 0x3803, alarmTint));
    sceneBuffer.setPaletteColor(InkBeamInner,
        blendColor(SCENE_PALETTE[InkBeamInner], 0x5004, alarmTint));
    sceneBuffer.setPaletteColor(InkBeamCore,
        blendColor(SCENE_PALETTE[InkBeamCore], 0x6805, alarmTint));

    float facetPhase = (nowMs % 10000) * 0.000628319f;
    int highlight = 110 + int(sinf(facetPhase) * 65.0f);
    sceneBuffer.setPaletteColor(InkMovingFacet,
        blendColor(SCENE_PALETTE[InkBlue], SCENE_PALETTE[InkIce], highlight));
}

// Spotlight

void drawSpotlight()
{
    int centreX = SCENE_WIDTH_PIXELS / 2;

    // Nested bands form the spotlight cone.
    const int beamHalfWidths[] = {132, 116, 99, 82};
    const uint8_t beamInks[] = {InkBeamOuter, InkBeamMiddle, InkBeamInner, InkBeamCore};
    for (int band = 0; band < 4; band++)
    {
        int halfWidth = beamHalfWidths[band];
        sceneBuffer.fillTriangle(centreX - 8,
            22,
            centreX - halfWidth,
            222,
            centreX + halfWidth,
            222,
            beamInks[band]);
        sceneBuffer.fillTriangle(centreX - 8,
            22,
            centreX + halfWidth,
            222,
            centreX + 8,
            22,
            beamInks[band]);
    }

    // Ceiling lamp.
    sceneBuffer.fillRect(centreX - 6, 7, 12, 7, InkMetalDark);
    sceneBuffer.fillTriangle(centreX - 6, 12, centreX - 13, 21, centreX + 13, 21, InkMetalFace);
    sceneBuffer.fillTriangle(centreX - 6, 12, centreX + 13, 21, centreX + 6, 12, InkMetalLit);
    sceneBuffer.drawFastHLine(centreX - 11, 22, 23, InkWhite);
}

// Diamond

void drawDiamondGlint(int centreX, int centreY, int radius)
{
    sceneBuffer.drawFastHLine(centreX - radius, centreY, radius * 2 + 1, InkIce);
    sceneBuffer.drawFastVLine(centreX, centreY - radius, radius * 2 + 1, InkWhite);
    sceneBuffer.fillRect(centreX - 1, centreY - 1, 3, 3, InkWhite);
}

void drawDiamond(uint32_t nowMs)
{
    // Closed or partially closed shutters never allow a diamond pixel to be drawn.
    if (vaultAnimation.diamondLiftPermille == 0 || vaultAnimation.shutterClosedPermille > 0)
    {
        return;
    }

    float lift = easedPosition(vaultAnimation.diamondLiftPermille);
    float rotationPhase = (nowMs % 12000) * 0.000523599f;
    float floatingPhase = (nowMs % 4000) * 0.001570796f;
    int centreX = SCENE_WIDTH_PIXELS / 2;
    int centreY = 121 + int((1.0f - lift) * 168.0f + sinf(floatingPhase) * 4.0f * lift);
    int halfWidth = 80 + int(cosf(rotationPhase) * 4.0f);
    int topY = centreY - 49;
    int girdleY = centreY - 13;
    int tipY = centreY + 62;
    int leftX = centreX - halfWidth;
    int rightX = centreX + halfWidth;
    int leftTableX = centreX - 43;
    int rightTableX = centreX + 43;
    int ridgeX = centreX + int(sinf(rotationPhase) * 12.0f);
    int leftFacetX = centreX - 32;
    int rightFacetX = centreX + 32;

    // Crown facets.
    sceneBuffer.fillTriangle(leftX, girdleY, leftTableX, topY, leftFacetX, girdleY, InkBlue);
    sceneBuffer.fillTriangle(leftTableX, topY, ridgeX, topY, leftFacetX, girdleY, InkIce);
    sceneBuffer.fillTriangle(ridgeX, topY, ridgeX, girdleY, leftFacetX, girdleY, InkCyan);
    sceneBuffer.fillTriangle(ridgeX, topY, rightTableX, topY, rightFacetX, girdleY, InkWhite);
    sceneBuffer.fillTriangle(ridgeX, topY, rightFacetX, girdleY, ridgeX, girdleY, InkMovingFacet);
    sceneBuffer.fillTriangle(rightTableX, topY, rightX, girdleY, rightFacetX, girdleY, InkIce);

    // Lower facets share the tip; the palette animates the reflection.
    sceneBuffer.fillTriangle(leftX, girdleY, leftFacetX, girdleY, centreX, tipY, InkDeepBlue);
    sceneBuffer.fillTriangle(leftFacetX, girdleY, ridgeX, girdleY, centreX, tipY, InkMovingFacet);
    sceneBuffer.fillTriangle(ridgeX, girdleY, rightFacetX, girdleY, centreX, tipY, InkIce);
    sceneBuffer.fillTriangle(rightFacetX, girdleY, rightX, girdleY, centreX, tipY, InkBlue);

    sceneBuffer.drawLine(leftX, girdleY, leftTableX, topY, InkIce);
    sceneBuffer.drawFastHLine(leftTableX, topY, rightTableX - leftTableX + 1, InkWhite);
    sceneBuffer.drawLine(rightTableX, topY, rightX, girdleY, InkWhite);
    sceneBuffer.drawLine(rightX, girdleY, centreX, tipY, InkCyan);
    sceneBuffer.drawLine(centreX, tipY, leftX, girdleY, InkBlue);
    sceneBuffer.drawFastHLine(leftX, girdleY, halfWidth * 2 + 1, InkIce);

    uint32_t glintTimeMs = nowMs % 4600;
    if (vaultAnimation.currentState == VaultState::Displaying && glintTimeMs < 480)
    {
        int radius = 1 + (240 - abs(int(glintTimeMs) - 240)) / 30;
        drawDiamondGlint(rightTableX - 2, topY + 1, radius);
    }
}

// Pedestal and shutters

void drawVaultOpening()
{
    // Draw this surface behind the diamond.
    sceneBuffer.fillTriangle(94, 220, 118, 200, 282, 200, InkMetalLit);
    sceneBuffer.fillTriangle(94, 220, 282, 200, 306, 220, InkMetalFace);
    sceneBuffer.drawFastHLine(118, 200, 165, InkSilver);
    sceneBuffer.fillRect(112, 208, 176, 13, InkShadow);
}

void drawVaultShuttersAndFront()
{
    // Mask the entire descent, including below the stand, before drawing its front.
    sceneBuffer.fillRect(94, 221, 213, SCENE_HEIGHT_PIXELS - 221, InkBackground);
    sceneBuffer.fillRect(110, 254, 181, 4, InkShadow);
    sceneBuffer.fillRect(94, 221, 213, 33, InkMetalDark);
    sceneBuffer.fillRect(105, 221, 190, 29, InkMetalFace);
    sceneBuffer.drawFastHLine(94, 221, 213, InkSilver);
    sceneBuffer.drawFastHLine(105, 250, 190, InkMetalLit);

    // Shutters meet at the centre when fully closed.
    int panelWidth = int(easedPosition(vaultAnimation.shutterClosedPermille) * 88.0f);
    if (panelWidth > 0)
    {
        sceneBuffer.fillRect(112, 208, panelWidth, 13, InkMetalLit);
        sceneBuffer.fillRect(288 - panelWidth, 208, panelWidth, 13, InkMetalFace);
        sceneBuffer.drawFastHLine(112, 208, panelWidth, InkSilver);
        sceneBuffer.drawFastHLine(288 - panelWidth, 208, panelWidth, InkSilver);
        sceneBuffer.drawFastVLine(111 + panelWidth, 209, 12, InkSilver);
        sceneBuffer.drawFastVLine(288 - panelWidth, 209, 12, InkMetalDark);
    }
}

// Exhibit border

void drawExhibitBorder(uint32_t nowMs)
{
    uint16_t borderColor = 0x4B2D;
    if (sharedCsiWarning && guardController.currentState == GuardState::Armed)
    {
        borderColor = GOLD_COLOR; // CSI indication outside the latched alarm and clearance states.
    }
    float alarmStrength = alarmLightStrength(nowMs);
    if (alarmStrength > 0.0f)
    {
        borderColor = blendColor(0x5803, RED_COLOR, int(alarmStrength * 255.0f));
    }

    display.drawRect(8, 8, SCREEN_WIDTH_PIXELS - 16, SCREEN_HEIGHT_PIXELS - 16, borderColor);
}

void drawScreenFrame()
{
    display.fillScreen(BACKGROUND_COLOR);
    drawExhibitBorder(millis());
}

// Settings screen

void drawSettingsScreen()
{
    display.fillScreen(BACKGROUND_COLOR);
    drawDisplayText("THE VAULT / SETTINGS", 30, 22, CYAN_COLOR, 2);
    drawDisplayText("U/D select   L/R change", 30, 54, MUTED_TEXT_COLOR, 2);

    const char *settingLabels[] = {"Guard distance", "Sound", "Reverse X", "Reverse Y"};
    char settingValueText[20];
    for (int rowIndex = 0; rowIndex < 4; rowIndex++)
    {
        int rowY = 94 + rowIndex * 38;
        if (rowIndex == selectedSettingsRow)
        {
            display.drawRect(24, rowY - 7, 432, 32, CYAN_COLOR);
        }

        drawDisplayText(settingLabels[rowIndex],
            40,
            rowY,
            rowIndex == selectedSettingsRow ? WHITE_COLOR : MUTED_TEXT_COLOR,
            2);
        const char *valueText;
        if (rowIndex == 0)
        {
            snprintf(settingValueText, sizeof(settingValueText), "%d cm", guardDistanceCm);
            valueText = settingValueText;
        }
        else
        {
            bool isEnabled = rowIndex == 1 ? isSoundEnabled
                : rowIndex == 2            ? reverseXAxis
                                           : reverseYAxis;
            valueText = isEnabled ? "ON" : (rowIndex == 1 ? "MUTE" : "OFF");
        }

        drawDisplayText(valueText,
            340,
            rowY,
            rowIndex == selectedSettingsRow ? GOLD_COLOR : MUTED_TEXT_COLOR,
            2);
    }

    drawDisplayText("MONITORING: LOCAL + REMOTE", 40, 239, CYAN_COLOR, 1);

    drawDisplayText("CLICK: SAVE & RETURN", 40, 260, GREEN_COLOR, 2);
    drawDisplayText("Hold button at boot to calibrate joystick", 30, 294, MUTED_TEXT_COLOR, 1);
}

// Diagnostics and motion overlays
const char *shortCsiLabel(uint8_t state)
{
    const char *labels[] = {"OFF", "NO DATA", "CALIBRATING", "READY", "MOTION"};
    return state <= 4 ? labels[state] : "NO DATA";
}

void drawCsiActivity(const char *label,
    uint8_t state,
    float score,
    float threshold,
    int x,
    int y,
    int width)
{
    char text[40];
    snprintf(text, sizeof(text), "%s: %s", label, shortCsiLabel(state));
    uint16_t color = state == 4 ? GOLD_COLOR : MUTED_TEXT_COLOR;
    drawDisplayText(text, x, y, color, 1);
    display.drawRect(x, y + 12, width, 6, MUTED_TEXT_COLOR);
    float fraction = state >= 3 && threshold > 0 ? score / (threshold * 2.0f) : 0;
    int barWidth = int(constrain(fraction, 0.0f, 1.0f) * (width - 2));
    display.fillRect(x + 1, y + 13, barWidth, 4, color);
    display.drawFastVLine(x + width / 2, y + 12, 6, WHITE_COLOR); // Warning threshold.
}

void drawExhibitInformation(uint32_t nowMs)
{
    // Clear only the two narrow margins outside the reusable scene buffer.
    display.fillRect(20, 12, 440, 12, BACKGROUND_COLOR);
    display.fillRect(20, 296, 440, 14, BACKGROUND_COLOR);
    if (!isSoundEnabled)
    {
        drawDisplayText("MUTE", 430, 14, GOLD_COLOR, 1);
    }
    bool latched = guardController.currentState == GuardState::AlarmLatched;
    bool waiting = guardController.currentState == GuardState::WaitingForClear;
    if (latched || waiting)
    {
        const char *title = waiting ? "VAULT SEALED"
            : silentLinkFault       ? "REMOTE LINK LOST"
                                    : "INTRUSION DETECTED";
        drawDisplayText(title, 40, 34, waiting ? MUTED_TEXT_COLOR : RED_COLOR, 2);
        char detail[64];
        const char *causes[] = {"SENSOR",
            "LOCAL",
            "REMOTE S1",
            "REMOTE S2",
            "S1+S2 NEAREST",
            "LINK LOST",
            "MAIN CSI",
            "REMOTE CSI",
            "BOTH CSI"};
        if (latched && !silentLinkFault)
        {
            const char *vaultLabel =
                vaultAnimation.currentState == VaultState::Sealed ? "VAULT SEALED" : "SEALING...";
            if (alarmCause >= 6 && alarmCause <= 8)
            {
                snprintf(detail, sizeof(detail), "%s MOTION   %s", causes[alarmCause], vaultLabel);
            }
            else
            {
                snprintf(detail,
                    sizeof(detail),
                    "%s: %.1f cm   %s",
                    causes[alarmCause],
                    triggerDistanceMm / 10.0f,
                    vaultLabel);
            }
            drawDisplayText(detail, 40, 274, RED_COLOR, 1);
        }
        else if (latched)
        {
            drawDisplayText(vaultAnimation.currentState == VaultState::Sealed
                    ? "VAULT SEALED - RESTORE REMOTE CONNECTION"
                    : "SEALING...",
                40,
                274,
                RED_COLOR,
                1);
        }
        drawDisplayText(waiting ? "Clear all 3 sensors and wait for CSI to settle"
                                : "PRESS JOYSTICK TO ACKNOWLEDGE",
            40,
            298,
            MUTED_TEXT_COLOR,
            1);
    }
    else if (sharedCsiWarning)
    {
        drawDisplayText("CSI sensing activated", 40, 34, GOLD_COLOR, 2);
        drawCsiActivity("MAIN", localCsiState(), csi.score, csi.threshold, 40, 274, 180);
        drawCsiActivity("REMOTE",
            peerCsi.freshState(nowMs, remoteSensor.isFresh(nowMs)),
            peerCsi.score,
            peerCsi.threshold,
            260,
            274,
            180);
    }
}

void drawDiagnosticsScreen(uint32_t nowMs)
{
    display.fillScreen(BACKGROUND_COLOR);
    drawDisplayText("THE VAULT / DIAGNOSTICS", 30, 22, CYAN_COLOR, 2);
    char text[72];
    snprintf(text,
        sizeof(text),
        "%s  |  boundary %d cm  |  %s",
        getRemoteLinkLabel(nowMs),
        guardDistanceCm,
        isSoundEnabled ? "SOUND ON" : "MUTE");
    drawDisplayText(text, 30, 55, WHITE_COLOR, 1);
    const char *states[] = {"OFF", "ARMING", "ARMED", "LATCHED", "WAIT CLEAR"};
    const char *vaultStates[] =
        {"DISPLAYING", "RETRACTING", "CLOSING", "SEALED", "OPENING", "RAISING"};
    snprintf(text,
        sizeof(text),
        "Guard: %s  |  vault: %s",
        states[uint8_t(guardController.currentState)],
        vaultStates[uint8_t(vaultAnimation.currentState)]);
    drawDisplayText(text, 30, 75, MUTED_TEXT_COLOR, 1);
    for (uint8_t index = 0; index < 3; index++)
    {
        bool valid = index == 0 ? hasValidDistance && isLatestDistanceSampleValid
                && uint32_t(nowMs - lastValidDistanceMs) < 350
                                : remoteSensor.sensors[index - 1].readingValid
                && remoteSensor.sensors[index - 1].isFresh(nowMs);
        float distance = index == 0 ? filteredDistanceCm
                                    : remoteSensor.sensors[index - 1].distanceMillimetres / 10.0f;
        const char *label = index == 0 ? "LOCAL" : index == 1 ? "REMOTE S1" : "REMOTE S2";
        if (valid)
        {
            snprintf(text,
                sizeof(text),
                "%s: %.1f cm %s",
                label,
                distance,
                distance <= guardDistanceCm           ? "NEAR"
                    : distance >= guardDistanceCm + 5 ? "CLEAR"
                                                      : "MARGIN");
        }
        else
        {
            snprintf(text, sizeof(text), "%s: NO VALID ECHO", label);
        }
        drawDisplayText(text, 30, 105 + index * 25, valid ? WHITE_COLOR : GOLD_COLOR, 2);
    }
    drawCsiActivity("MAIN CSI", localCsiState(), csi.score, csi.threshold, 30, 195, 190);
    drawCsiActivity("REMOTE CSI",
        peerCsi.freshState(nowMs, remoteSensor.isFresh(nowMs)),
        peerCsi.score,
        peerCsi.threshold,
        255,
        195,
        190);
    snprintf(text,
        sizeof(text),
        "Packets: %lu  |  link age: %lu ms",
        (unsigned long)acceptedRemotePackets,
        (unsigned long)(remoteSensor.hasPacket ? uint32_t(nowMs - remoteSensor.lastPacketMs) : 0));
    drawDisplayText(text, 30, 235, MUTED_TEXT_COLOR, 1);
    drawDisplayText("CLICK: EXHIBIT   HOLD: SETTINGS", 30, 270, GREEN_COLOR, 2);
    drawDisplayText("Monitoring continues on every screen", 30, 298, MUTED_TEXT_COLOR, 1);
}

// Composing the exhibit

void drawAnimatedScene(uint32_t nowMs)
{
    updateScenePalette(nowMs);
    sceneBuffer.fillSprite(InkBackground);
    drawSpotlight();

    // Draw back to front so the pedestal hides the retracting diamond completely.
    drawVaultOpening();
    drawDiamond(nowMs);
    drawVaultShuttersAndFront();
    sceneBuffer.pushSprite(SCENE_SCREEN_X, SCENE_SCREEN_Y);
    drawExhibitBorder(nowMs);
    drawExhibitInformation(nowMs);
}
