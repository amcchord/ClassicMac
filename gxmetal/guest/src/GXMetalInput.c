/*
 * GXMetal InputSprocket capture coordinator.
 *
 * Copyright (c) 2026 ClassicMac contributors
 * SPDX-License-Identifier: MIT
 *
 * Classic Mac OS already provides the authoritative InputSprocket mouse
 * elements. GXMetal registers a mouse-class coordinator so games activating
 * those devices can request host-relative capture without creating a second
 * movement or button stream that races the system mouse driver.
 */

#include <CodeFragments.h>
#include <InputSprocket.h>
#include <MacTypes.h>
#include <string.h>

static ISpDeviceReference gDevice;
static CFragConnectionID gGXMetalConnection;
typedef OSErr (*GXMetalInputModeProc)(Boolean relative);
static GXMetalInputModeProc gSetRelativeInputMode;

static const unsigned char kGXMetalLibraryName[] = {
    7, 'G', 'X', 'M', 'e', 't', 'a', 'l'
};
static const unsigned char kGXMetalInputModeSymbol[] = {
    27, 'G', 'X', 'M', 'e', 't', 'a', 'l', 'S', 'e', 't', 'R', 'e', 'l',
    'a', 't', 'i', 'v', 'e', 'I', 'n', 'p', 'u', 't', 'M', 'o', 'd', 'e'
};

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

static void GXMetalInputResolveHostBridge(void)
{
    Ptr mainAddress = NULL;
    Ptr symbol = NULL;
    Str255 errorName;
    CFragSymbolClass symbolClass;

    if (gSetRelativeInputMode != NULL || gGXMetalConnection != NULL) {
        return;
    }
    if (GetSharedLibrary(kGXMetalLibraryName, kPowerPCCFragArch,
                         kReferenceCFrag, &gGXMetalConnection,
                         &mainAddress, errorName) != noErr ||
        FindSymbol(gGXMetalConnection, kGXMetalInputModeSymbol, &symbol,
                   &symbolClass) != noErr || symbol == NULL ||
        symbolClass != kTVectorCFragSymbol) {
        if (gGXMetalConnection != NULL) {
            (void)CloseConnection(&gGXMetalConnection);
        }
        return;
    }
    gSetRelativeInputMode = (GXMetalInputModeProc)symbol;
}

static OSStatus GXMetalInputSetActive(UInt32 refCon, Boolean active)
{
    (void)refCon;
    GXMetalInputResolveHostBridge();
    if (gSetRelativeInputMode != NULL) {
        (void)gSetRelativeInputMode(active);
    }
    return noErr;
}

static OSStatus GXMetalInputInitializeDevice(
    UInt32 refCon, UInt32 count, ISpNeed needs[],
    ISpElementReference virtualElements[], Boolean used[],
    OSType appCreatorCode, OSType subCreatorCode, UInt32 reserved,
    void *reserved2)
{
    (void)refCon;
    (void)count;
    (void)needs;
    (void)virtualElements;
    (void)used;
    (void)appCreatorCode;
    (void)subCreatorCode;
    (void)reserved;
    (void)reserved2;

    /* The system mouse driver fulfills all needs and owns all event data. */
    return noErr;
}

static OSStatus GXMetalInputStopDevice(UInt32 refCon)
{
    return GXMetalInputSetActive(refCon, false);
}

static OSStatus GXMetalInputDeviceTickle(UInt32 refCon)
{
    (void)refCon;
    return noErr;
}

static ISpDriverFunctionPtr_Generic GXMetalInputMetaHandler(
    UInt32 refCon, ISpMetaHandlerSelector selector)
{
    (void)refCon;
    switch (selector) {
    case kISpSelector_Init:
        return (ISpDriverFunctionPtr_Generic)GXMetalInputInitializeDevice;
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

static void GXMetalInputDispose(void)
{
    (void)GXMetalInputSetActive(0, false);
    if (gDevice != NULL) {
        (void)ISpDevice_Dispose(gDevice);
        gDevice = NULL;
    }
    gSetRelativeInputMode = NULL;
    if (gGXMetalConnection != NULL) {
        (void)CloseConnection(&gGXMetalConnection);
    }
}

static OSStatus GXMetalInputCreate(void)
{
    ISpDeviceDefinition deviceDefinition;
    OSStatus error;

    if (gDevice != NULL) {
        return noErr;
    }
    memset(&deviceDefinition, 0, sizeof(deviceDefinition));
    GXMetalInputSetPascalString(deviceDefinition.deviceName,
                                "GXMetal Game Input Handoff");
    deviceDefinition.theDeviceClass = kISpDeviceClass_Mouse;
    deviceDefinition.theDeviceIdentifier = 'GXMI';
    deviceDefinition.permanentID = 'GXMI';
    error = ISpDevice_New(&deviceDefinition, GXMetalInputMetaHandler, 0,
                          &gDevice);
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

void ISpDriver_Tickle(void)
{
    /* The system InputSprocket mouse driver owns polling and event delivery. */
}
