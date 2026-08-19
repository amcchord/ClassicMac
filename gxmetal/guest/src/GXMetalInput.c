/*
 * GXMetal InputSprocket bridge.
 *
 * Copyright (c) 2026 ClassicMac contributors
 * SPDX-License-Identifier: MIT
 *
 * ClassicMac's Virtio tablet gives the Finder an absolute, seamless pointer,
 * but InputSprocket games only enumerate devices registered with
 * InputSprocket.  Expose the system cursor as a conventional mouse with X/Y
 * delta and button elements so those games can consume the same input stream.
 */

#include <CodeFragments.h>
#include <InputSprocket.h>
#include <LowMem.h>
#include <MacTypes.h>
#include <Timer.h>
#include <string.h>

static ISpDeviceReference gDevice;
static ISpElementReference gDeltaX;
static ISpElementReference gDeltaY;
static ISpElementReference gButton1;
static ISpElementReference gButton2;
static ISpElementReference gButton3;
static Point gLastPosition;
static UInt8 gLastButtons;
static Boolean gHavePosition;
static Boolean gActive;
static TMTask gPollTimer;
static Boolean gPollTimerInstalled;

enum {
    kGXMetalInputPollIntervalMilliseconds = 8,
    /* The standard InputSprocket mouse scale is approximately 1/400 of a
     * 16.16 unit per screen pixel (65536 / 400 = 163.84). */
    kGXMetalInputDeltaScale = 164
};

void ISpDriver_Tickle(void);

static pascal void GXMetalInputPollTimer(TMTaskPtr *task)
{
    (void)task;
    ISpDriver_Tickle();
    if (gPollTimerInstalled && gActive) {
        PrimeTime((QElemPtr)&gPollTimer,
                  kGXMetalInputPollIntervalMilliseconds);
    }
}

static const RoutineDescriptor gPollTimerDescriptor =
    BUILD_ROUTINE_DESCRIPTOR(uppTimerProcInfo, GXMetalInputPollTimer);
static const TimerUPP gPollTimerProc =
    (TimerUPP)&gPollTimerDescriptor;

static void GXMetalInputStopPolling(void)
{
    if (gPollTimerInstalled) {
        RmvTime((QElemPtr)&gPollTimer);
        gPollTimerInstalled = false;
    }
}

static void GXMetalInputStartPolling(void)
{
    if (!gPollTimerInstalled) {
        memset(&gPollTimer, 0, sizeof(gPollTimer));
        gPollTimer.tmAddr = gPollTimerProc;
        InsXTime((QElemPtr)&gPollTimer);
        gPollTimerInstalled = true;
    }
    PrimeTime((QElemPtr)&gPollTimer,
              kGXMetalInputPollIntervalMilliseconds);
}

static void GXMetalInputSetPascalString(Str63 destination,
                                        const char *source)
{
    size_t length = strlen(source);

    if (length > 63u) {
        length = 63u;
    }
    destination[0] = (UInt8)length;
    memcpy(destination + 1, source, length);
}

static OSStatus GXMetalInputSetActive(UInt32 refCon, Boolean active)
{
    (void)refCon;
    if (!active) {
        GXMetalInputStopPolling();
    }
    gActive = active;
    gHavePosition = false;
    gLastButtons = LMGetMouseButtonState();
    if (active) {
        GXMetalInputStartPolling();
    }
    return noErr;
}

static OSStatus GXMetalInputDeviceTickle(UInt32 refCon)
{
    (void)refCon;
    ISpDriver_Tickle();
    return noErr;
}

static ISpDriverFunctionPtr_Generic GXMetalInputMetaHandler(
    UInt32 refCon, ISpMetaHandlerSelector selector)
{
    (void)refCon;
    switch (selector) {
    case kISpSelector_SetActive:
        return (ISpDriverFunctionPtr_Generic)GXMetalInputSetActive;
    case kISpSelector_Tickle:
        return (ISpDriverFunctionPtr_Generic)GXMetalInputDeviceTickle;
    default:
        return NULL;
    }
}

static OSStatus GXMetalInputCreateElement(
    const char *name, ISpElementKind kind, ISpElementLabel label,
    void *configuration, UInt32 configurationSize,
    ISpElementReference *element)
{
    ISpElementDefinitionStruct definition;

    memset(&definition, 0, sizeof(definition));
    definition.device = gDevice;
    GXMetalInputSetPascalString(definition.theString, name);
    definition.kind = kind;
    definition.label = label;
    definition.configInfo = configuration;
    definition.configInfoLength = configurationSize;
    definition.dataSize = sizeof(UInt32);
    return ISpElement_New(&definition, element);
}

static void GXMetalInputDispose(void)
{
    GXMetalInputStopPolling();
    if (gButton3 != NULL) {
        (void)ISpElement_Dispose(gButton3);
        gButton3 = NULL;
    }
    if (gButton2 != NULL) {
        (void)ISpElement_Dispose(gButton2);
        gButton2 = NULL;
    }
    if (gButton1 != NULL) {
        (void)ISpElement_Dispose(gButton1);
        gButton1 = NULL;
    }
    if (gDeltaY != NULL) {
        (void)ISpElement_Dispose(gDeltaY);
        gDeltaY = NULL;
    }
    if (gDeltaX != NULL) {
        (void)ISpElement_Dispose(gDeltaX);
        gDeltaX = NULL;
    }
    if (gDevice != NULL) {
        (void)ISpDevice_Dispose(gDevice);
        gDevice = NULL;
    }
    gActive = false;
    gHavePosition = false;
}

static OSStatus GXMetalInputCreate(void)
{
    ISpDeviceDefinition deviceDefinition;
    ISpDeltaConfigurationInfo deltaConfiguration = {0, 0};
    ISpButtonConfigurationInfo buttonConfiguration;
    OSStatus error;

    if (gDevice != NULL) {
        return noErr;
    }

    memset(&deviceDefinition, 0, sizeof(deviceDefinition));
    GXMetalInputSetPascalString(deviceDefinition.deviceName,
                                "GXMetal Seamless Mouse");
    deviceDefinition.theDeviceClass = kISpDeviceClass_Mouse;
    deviceDefinition.theDeviceIdentifier = 'GXMI';
    deviceDefinition.permanentID = 'GXMI';
    error = ISpDevice_New(&deviceDefinition, GXMetalInputMetaHandler, 0,
                          &gDevice);
    if (error != noErr) {
        GXMetalInputDispose();
        return error;
    }

    error = GXMetalInputCreateElement(
        "Horizontal motion", kISpElementKind_Delta,
        kISpElementLabel_Delta_Cursor_X, &deltaConfiguration,
        sizeof(deltaConfiguration), &gDeltaX);
    if (error == noErr) {
        error = GXMetalInputCreateElement(
            "Vertical motion", kISpElementKind_Delta,
            kISpElementLabel_Delta_Cursor_Y, &deltaConfiguration,
            sizeof(deltaConfiguration), &gDeltaY);
    }

    buttonConfiguration.id = 1;
    if (error == noErr) {
        error = GXMetalInputCreateElement(
            "Mouse button 1", kISpElementKind_Button,
            kISpElementLabel_Btn_MouseOne, &buttonConfiguration,
            sizeof(buttonConfiguration), &gButton1);
    }
    buttonConfiguration.id = 2;
    if (error == noErr) {
        error = GXMetalInputCreateElement(
            "Mouse button 2", kISpElementKind_Button,
            kISpElementLabel_Btn_MouseTwo, &buttonConfiguration,
            sizeof(buttonConfiguration), &gButton2);
    }
    buttonConfiguration.id = 3;
    if (error == noErr) {
        error = GXMetalInputCreateElement(
            "Mouse button 3", kISpElementKind_Button,
            kISpElementLabel_Btn_MouseThree, &buttonConfiguration,
            sizeof(buttonConfiguration), &gButton3);
    }
    if (error != noErr) {
        GXMetalInputDispose();
    }
    return error;
}

OSErr GXMetalInputCFMInitialize(const CFragInitBlock *initBlock)
{
    (void)initBlock;
    return noErr;
}

OSStatus ISpDriver_CheckConfiguration(Boolean *validConfiguration)
{
    if (validConfiguration == NULL) {
        return paramErr;
    }
    *validConfiguration = true;
    return noErr;
}

OSStatus ISpDriver_FindAndLoadDevices(Boolean *keepDriverLoaded)
{
    OSStatus error;

    if (keepDriverLoaded == NULL) {
        return paramErr;
    }
    error = GXMetalInputCreate();
    *keepDriverLoaded = error == noErr;
    return error;
}

OSStatus ISpDriver_DisposeDevices(void)
{
    GXMetalInputDispose();
    return noErr;
}

void ISpDriver_Tickle(void)
{
    Point position;
    UInt8 buttons;
    AbsoluteTime now;
    SInt32 deltaX;
    SInt32 deltaY;

    if (!gActive || gDevice == NULL) {
        return;
    }
    position = LMGetMouseLocation();
    buttons = LMGetMouseButtonState();
    now = ISpUptime();

    if (!gHavePosition) {
        gLastPosition = position;
        gLastButtons = buttons;
        gHavePosition = true;
        return;
    }

    deltaX = (SInt32)position.h - (SInt32)gLastPosition.h;
    deltaY = (SInt32)position.v - (SInt32)gLastPosition.v;
    if (deltaX != 0) {
        (void)ISpElement_PushSimpleData(
            gDeltaX, (UInt32)(deltaX * kGXMetalInputDeltaScale), &now);
    }
    if (deltaY != 0) {
        /* InputSprocket's positive Y direction is up; QuickDraw screen
         * coordinates increase toward the bottom of the display. */
        (void)ISpElement_PushSimpleData(
            gDeltaY, (UInt32)(-deltaY * kGXMetalInputDeltaScale), &now);
    }
    if ((buttons & 0x80u) != (gLastButtons & 0x80u)) {
        (void)ISpElement_PushSimpleData(
            gButton1, (buttons & 0x80u) != 0 ?
                kISpButtonUp : kISpButtonDown, &now);
    }

    gLastPosition = position;
    gLastButtons = buttons;
}
