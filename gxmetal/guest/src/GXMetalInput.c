/*
 * GXMetal InputSprocket bridge.
 *
 * Copyright (c) 2026 ClassicMac contributors
 * SPDX-License-Identifier: MIT
 *
 * ClassicMac's Virtio tablet gives the Finder an absolute, seamless pointer,
 * but InputSprocket games only enumerate devices registered with
 * InputSprocket.  Expose the system cursor as a conventional mouse with X/Y
 * delta and button elements.  While a game owns the device, host deltas travel
 * directly through GXMetal instead of moving and recentering the system cursor.
 */

#include <CodeFragments.h>
#include <InputSprocket.h>
#include <LowMem.h>
#include <MacTypes.h>
#include <Processes.h>
#include <Timer.h>
#include <string.h>

#include "gxmetal_protocol.h"

static ISpDeviceReference gDevice;
static ISpDeviceReference gQuakeMotionDevice;
static ISpElementReference gDeltaX;
static ISpElementReference gDeltaY;
static ISpElementReference gButton1;
static ISpElementReference gButton2;
static ISpElementReference gButton3;
static Point gLastPosition;
static UInt32 gLastButtons;
static Boolean gHavePosition;
static Boolean gActive;
static Boolean gHostRelativeInput;
static Boolean gDirectHostInput;
static UInt32 gActiveDeviceMask;
static Point gRelativeAnchor;
static TMTask gPollTimer;
static Boolean gPollTimerInstalled;
static CFragConnectionID gGXMetalConnection;
typedef OSErr (*GXMetalInputModeProc)(Boolean relative);
typedef OSErr (*GXMetalInputButtonsProc)(UInt32 *buttons);
typedef OSErr (*GXMetalInputStateProc)(SInt32 *deltaX, SInt32 *deltaY,
                                      UInt32 *buttons);
typedef OSErr (*GXMetalInputEventsProc)(SInt32 *deltaX, SInt32 *deltaY,
                                       UInt32 *buttons,
                                       UInt32 *buttonDownEdges,
                                       UInt32 *buttonUpEdges);
static GXMetalInputModeProc gSetRelativeInputMode;
static GXMetalInputButtonsProc gGetInputButtonState;
static GXMetalInputStateProc gGetInputState;
static GXMetalInputEventsProc gGetInputEvents;

static const unsigned char kGXMetalLibraryName[] = {
    7, 'G', 'X', 'M', 'e', 't', 'a', 'l'
};
static const unsigned char kGXMetalInputModeSymbol[] = {
    27, 'G', 'X', 'M', 'e', 't', 'a', 'l', 'S', 'e', 't', 'R', 'e', 'l',
    'a', 't', 'i', 'v', 'e', 'I', 'n', 'p', 'u', 't', 'M', 'o', 'd', 'e'
};
static const unsigned char kGXMetalInputButtonsSymbol[] = {
    26, 'G', 'X', 'M', 'e', 't', 'a', 'l', 'G', 'e', 't', 'I', 'n', 'p',
    'u', 't', 'B', 'u', 't', 't', 'o', 'n', 'S', 't', 'a', 't', 'e'
};
static const unsigned char kGXMetalInputStateSymbol[] = {
    20, 'G', 'X', 'M', 'e', 't', 'a', 'l', 'G', 'e', 't', 'I', 'n', 'p',
    'u', 't', 'S', 't', 'a', 't', 'e'
};
static const unsigned char kGXMetalInputEventsSymbol[] = {
    21, 'G', 'X', 'M', 'e', 't', 'a', 'l', 'G', 'e', 't', 'I', 'n', 'p',
    'u', 't', 'E', 'v', 'e', 'n', 't', 's'
};

enum {
    kGXMetalInputPollIntervalMilliseconds = 8,
    /* The standard InputSprocket mouse scale is approximately 1/400 of a
     * 16.16 unit per screen pixel (65536 / 400 = 163.84). */
    kGXMetalInputDeltaScale = 164,
    kGXMetalInputMainDevice = 1u << 0,
    kGXMetalInputQuakeMotionDevice = 1u << 1
};

void ISpDriver_Tickle(void);

static void GXMetalInputResolveHostBridge(void)
{
    Ptr mainAddress = NULL;
    Ptr modeSymbol = NULL;
    Ptr buttonsSymbol = NULL;
    Ptr stateSymbol = NULL;
    Ptr eventsSymbol = NULL;
    Str255 errorName;
    CFragSymbolClass symbolClass;

    if (gGXMetalConnection != NULL) {
        return;
    }
    if (GetSharedLibrary(kGXMetalLibraryName, kPowerPCCFragArch,
                         kReferenceCFrag, &gGXMetalConnection,
                         &mainAddress, errorName) != noErr ||
        FindSymbol(gGXMetalConnection, kGXMetalInputModeSymbol, &modeSymbol,
                   &symbolClass) != noErr || modeSymbol == NULL ||
        symbolClass != kTVectorCFragSymbol) {
        if (gGXMetalConnection != NULL) {
            (void)CloseConnection(&gGXMetalConnection);
        }
        return;
    }
    gSetRelativeInputMode = (GXMetalInputModeProc)modeSymbol;
    if (FindSymbol(gGXMetalConnection, kGXMetalInputButtonsSymbol,
                   &buttonsSymbol, &symbolClass) == noErr &&
        buttonsSymbol != NULL && symbolClass == kTVectorCFragSymbol) {
        gGetInputButtonState = (GXMetalInputButtonsProc)buttonsSymbol;
    }
    if (FindSymbol(gGXMetalConnection, kGXMetalInputStateSymbol,
                   &stateSymbol, &symbolClass) == noErr &&
        stateSymbol != NULL && symbolClass == kTVectorCFragSymbol) {
        gGetInputState = (GXMetalInputStateProc)stateSymbol;
    }
    if (FindSymbol(gGXMetalConnection, kGXMetalInputEventsSymbol,
                   &eventsSymbol, &symbolClass) == noErr &&
        eventsSymbol != NULL && symbolClass == kTVectorCFragSymbol) {
        gGetInputEvents = (GXMetalInputEventsProc)eventsSymbol;
    }
}

static Boolean GXMetalInputSetHostMode(Boolean relative)
{
    OSErr error;

    GXMetalInputResolveHostBridge();
    if (gSetRelativeInputMode == NULL) {
        return false;
    }
    error = gSetRelativeInputMode(relative);
    return error == noErr;
}

static UInt32 GXMetalInputReadButtons(void)
{
    UInt32 buttons;

    GXMetalInputResolveHostBridge();
    if (gGetInputButtonState != NULL &&
        gGetInputButtonState(&buttons) == noErr) {
        return buttons & (GXMETAL_INPUT_BUTTON_ONE |
                          GXMETAL_INPUT_BUTTON_TWO |
                          GXMETAL_INPUT_BUTTON_THREE);
    }

    /* Classic low memory stores the primary button active-low in bit 7. */
    return (LMGetMouseButtonState() & 0x80u) == 0 ?
        GXMETAL_INPUT_BUTTON_ONE : 0;
}

static void GXMetalInputMoveCursor(Point position)
{
    /* Keep all three classic low-memory cursor locations together. The
     * relative ADB mouse can then travel forever without hitting an edge. */
    LMSetMouseTemp(position);
    LMSetRawMouseLocation(position);
    LMSetMouseLocation(position);
}

static void GXMetalInputBeginRelativeTracking(void)
{
    GDHandle mainDevice = GetMainDevice();
    Rect bounds;

    if (mainDevice != NULL && *mainDevice != NULL) {
        bounds = (**mainDevice).gdRect;
        gRelativeAnchor.h =
            (short)(bounds.left + (bounds.right - bounds.left) / 2);
        gRelativeAnchor.v =
            (short)(bounds.top + (bounds.bottom - bounds.top) / 2);
    } else {
        gRelativeAnchor = LMGetMouseLocation();
    }
    GXMetalInputMoveCursor(gRelativeAnchor);
    gLastPosition = gRelativeAnchor;
    gHavePosition = true;
}

static pascal void GXMetalInputPollTimer(TMTaskPtr task)
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
    SInt32 deltaX = 0;
    SInt32 deltaY = 0;
    UInt32 buttons;
    UInt32 buttonDownEdges = 0;
    UInt32 buttonUpEdges = 0;
    Boolean wasActive = gActiveDeviceMask != 0;

    if (active) {
        gActiveDeviceMask |= refCon;
    } else {
        gActiveDeviceMask &= ~refCon;
    }
    gActive = gActiveDeviceMask != 0;
    if (wasActive == gActive) {
        return noErr;
    }
    if (!gActive) {
        GXMetalInputStopPolling();
    }
    gHostRelativeInput = GXMetalInputSetHostMode(gActive) && gActive;
    gDirectHostInput = gHostRelativeInput &&
        (gGetInputEvents != NULL || gGetInputState != NULL);
    gHavePosition = false;
    gLastButtons = GXMetalInputReadButtons();
    if (gActive) {
        if (gDirectHostInput) {
            OSErr error = gGetInputEvents != NULL ?
                gGetInputEvents(&deltaX, &deltaY, &buttons,
                                &buttonDownEdges, &buttonUpEdges) :
                gGetInputState(&deltaX, &deltaY, &buttons);
            if (error == noErr) {
                /* Activating relative mode clears old host deltas.  Establish
                 * the exact current button baseline before the first poll. */
                gLastButtons = buttons;
                gHavePosition = true;
            }
        } else if (gHostRelativeInput) {
            GXMetalInputBeginRelativeTracking();
        }
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

static OSStatus GXMetalInputStopDevice(UInt32 refCon)
{
    return GXMetalInputSetActive(refCon, false);
}

static ISpDriverFunctionPtr_Generic GXMetalInputMetaHandler(
    UInt32 refCon, ISpMetaHandlerSelector selector)
{
    (void)refCon;
    switch (selector) {
    case kISpSelector_Stop:
        return (ISpDriverFunctionPtr_Generic)GXMetalInputStopDevice;
    case kISpSelector_SetActive:
        return (ISpDriverFunctionPtr_Generic)GXMetalInputSetActive;
    case kISpSelector_Tickle:
        return (ISpDriverFunctionPtr_Generic)GXMetalInputDeviceTickle;
    default:
        return NULL;
    }
}

static OSStatus GXMetalInputCreateElement(
    ISpDeviceReference device, const char *name,
    ISpElementKind kind, ISpElementLabel label,
    void *configuration, UInt32 configurationSize,
    ISpElementReference *element)
{
    ISpElementDefinitionStruct definition;

    memset(&definition, 0, sizeof(definition));
    definition.device = device;
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
    (void)GXMetalInputSetHostMode(false);
    gHostRelativeInput = false;
    gDirectHostInput = false;
    gActiveDeviceMask = 0;
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
    if (gQuakeMotionDevice != NULL) {
        (void)ISpDevice_Dispose(gQuakeMotionDevice);
        gQuakeMotionDevice = NULL;
    }
    gSetRelativeInputMode = NULL;
    gGetInputButtonState = NULL;
    gGetInputState = NULL;
    gGetInputEvents = NULL;
    if (gGXMetalConnection != NULL) {
        (void)CloseConnection(&gGXMetalConnection);
    }
    gActive = false;
    gHavePosition = false;
}

static Boolean GXMetalInputUseQuakeLayout(void)
{
    ProcessSerialNumber process;
    ProcessInfoRec information;
    Str255 processName;
    FSSpec application;

    memset(&information, 0, sizeof(information));
    information.processInfoLength = sizeof(information);
    information.processName = processName;
    information.processAppSpec = &application;
    return GetCurrentProcess(&process) == noErr &&
           GetProcessInformation(&process, &information) == noErr &&
           information.processSignature == 'IDQ3';
}

static OSStatus GXMetalInputCreateDevice(const char *name, OSType identifier,
                                         UInt32 refCon,
                                         ISpDeviceReference *device)
{
    ISpDeviceDefinition definition;

    memset(&definition, 0, sizeof(definition));
    GXMetalInputSetPascalString(definition.deviceName, name);
    definition.theDeviceClass = kISpDeviceClass_Mouse;
    definition.theDeviceIdentifier = identifier;
    definition.permanentID = identifier;
    return ISpDevice_New(&definition, GXMetalInputMetaHandler, refCon, device);
}

static OSStatus GXMetalInputCreate(void)
{
    ISpDeltaConfigurationInfo deltaConfiguration = {0, 0};
    ISpButtonConfigurationInfo buttonConfiguration;
    Boolean quakeLayout;
    OSStatus error;

    if (gDevice != NULL) {
        return noErr;
    }

    quakeLayout = GXMetalInputUseQuakeLayout();
    error = GXMetalInputCreateDevice("GXMetal Seamless Mouse", 'GXMI',
                                     kGXMetalInputMainDevice, &gDevice);
    if (error != noErr) {
        GXMetalInputDispose();
        return error;
    }

    if (quakeLayout) {
        error = GXMetalInputCreateDevice(
            "GXMetal Quake Motion", 'GXQM',
            kGXMetalInputQuakeMotionDevice, &gQuakeMotionDevice);
    } else {
        error = GXMetalInputCreateElement(
            gDevice, "Horizontal motion", kISpElementKind_Delta,
            kISpElementLabel_Delta_Cursor_X, &deltaConfiguration,
            sizeof(deltaConfiguration), &gDeltaX);
        if (error == noErr) {
            error = GXMetalInputCreateElement(
                gDevice, "Vertical motion", kISpElementKind_Delta,
                kISpElementLabel_Delta_Cursor_Y, &deltaConfiguration,
                sizeof(deltaConfiguration), &gDeltaY);
        }
    }

    memset(&buttonConfiguration, 0, sizeof(buttonConfiguration));
    buttonConfiguration.id = 1;
    if (error == noErr) {
        error = GXMetalInputCreateElement(
            gDevice, "Mouse button 1", kISpElementKind_Button,
            kISpElementLabel_Btn_MouseOne, &buttonConfiguration,
            sizeof(buttonConfiguration), &gButton1);
    }
    buttonConfiguration.id = 2;
    if (error == noErr) {
        error = GXMetalInputCreateElement(
            gDevice, "Mouse button 2", kISpElementKind_Button,
            kISpElementLabel_Btn_MouseTwo, &buttonConfiguration,
            sizeof(buttonConfiguration), &gButton2);
    }
    buttonConfiguration.id = 3;
    if (error == noErr) {
        error = GXMetalInputCreateElement(
            gDevice, "Mouse button 3", kISpElementKind_Button,
            kISpElementLabel_Btn_MouseThree, &buttonConfiguration,
            sizeof(buttonConfiguration), &gButton3);
    }
    if (error == noErr && quakeLayout) {
        error = GXMetalInputCreateElement(
            gQuakeMotionDevice, "Horizontal motion", kISpElementKind_Delta,
            kISpElementLabel_Delta_Cursor_X, &deltaConfiguration,
            sizeof(deltaConfiguration), &gDeltaX);
        if (error == noErr) {
            error = GXMetalInputCreateElement(
                gQuakeMotionDevice, "Vertical motion", kISpElementKind_Delta,
                kISpElementLabel_Delta_Cursor_Y, &deltaConfiguration,
                sizeof(deltaConfiguration), &gDeltaY);
        }
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
    GXMetalInputResolveHostBridge();
    error = GXMetalInputCreate();
    *keepDriverLoaded = error == noErr;
    return error;
}

OSStatus ISpDriver_DisposeDevices(void)
{
    GXMetalInputDispose();
    return noErr;
}

static void GXMetalInputPushButton(ISpElementReference element, UInt32 mask,
                                   UInt32 buttons, UInt32 buttonDownEdges,
                                   UInt32 buttonUpEdges,
                                   const AbsoluteTime *now)
{
    Boolean down = (buttons & mask) != 0;
    Boolean reportedDown = (gLastButtons & mask) != 0;
    Boolean downEdge = (buttonDownEdges & mask) != 0;
    Boolean upEdge = (buttonUpEdges & mask) != 0;

    if (downEdge && upEdge) {
        /* Two transitions occurred between polls. The final held state tells
         * us which ordering preserves both the click and the current state. */
        (void)ISpElement_PushSimpleData(
            element, down ? kISpButtonUp : kISpButtonDown, now);
        (void)ISpElement_PushSimpleData(
            element, down ? kISpButtonDown : kISpButtonUp, now);
        return;
    }
    if (downEdge && !reportedDown) {
        (void)ISpElement_PushSimpleData(element, kISpButtonDown, now);
        reportedDown = true;
    }
    if (upEdge && reportedDown) {
        (void)ISpElement_PushSimpleData(element, kISpButtonUp, now);
        reportedDown = false;
    }
    if (reportedDown != down) {
        (void)ISpElement_PushSimpleData(
            element, down ? kISpButtonDown : kISpButtonUp, now);
    }
}

void ISpDriver_Tickle(void)
{
    Point position;
    UInt32 buttons;
    UInt32 buttonDownEdges = 0;
    UInt32 buttonUpEdges = 0;
    AbsoluteTime now;
    SInt32 deltaX;
    SInt32 deltaY;

    if (!gActive || gDevice == NULL) {
        return;
    }
    deltaX = 0;
    deltaY = 0;
    if (gDirectHostInput) {
        OSErr error = gGetInputEvents != NULL ?
            gGetInputEvents(&deltaX, &deltaY, &buttons,
                            &buttonDownEdges, &buttonUpEdges) :
            gGetInputState(&deltaX, &deltaY, &buttons);
        if (error != noErr) {
            return;
        }
        position = gLastPosition;
    } else {
        position = LMGetMouseLocation();
        buttons = GXMetalInputReadButtons();
    }
    now = ISpUptime();

    if (!gHavePosition) {
        gLastPosition = position;
        gLastButtons = buttons;
        gHavePosition = true;
        return;
    }

    if (!gDirectHostInput) {
        if (gHostRelativeInput) {
            deltaX = (SInt32)position.h - (SInt32)gRelativeAnchor.h;
            deltaY = (SInt32)position.v - (SInt32)gRelativeAnchor.v;
        } else {
            deltaX = (SInt32)position.h - (SInt32)gLastPosition.h;
            deltaY = (SInt32)position.v - (SInt32)gLastPosition.v;
        }
    }
    if (deltaX != 0) {
        (void)ISpElement_PushSimpleData(
            gDeltaX, (UInt32)(deltaX * kGXMetalInputDeltaScale), &now);
    }
    if (deltaY != 0) {
        /* QEMU relative motion and QuickDraw positions increase toward the
         * bottom. InputSprocket delta elements define positive Y as up, so
         * both the direct host bridge and cursor fallback need inversion. */
        (void)ISpElement_PushSimpleData(
            gDeltaY, (UInt32)(-deltaY * kGXMetalInputDeltaScale),
            &now);
    }
    GXMetalInputPushButton(gButton1, GXMETAL_INPUT_BUTTON_ONE, buttons,
                           buttonDownEdges, buttonUpEdges, &now);
    GXMetalInputPushButton(gButton2, GXMETAL_INPUT_BUTTON_TWO, buttons,
                           buttonDownEdges, buttonUpEdges, &now);
    GXMetalInputPushButton(gButton3, GXMETAL_INPUT_BUTTON_THREE, buttons,
                           buttonDownEdges, buttonUpEdges, &now);

    if (gHostRelativeInput && !gDirectHostInput) {
        GXMetalInputMoveCursor(gRelativeAnchor);
        position = gRelativeAnchor;
    }
    gLastPosition = position;
    gLastButtons = buttons;
}
