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
#include <Files.h>
#include <Folders.h>
#include <InputSprocket.h>
#include <LowMem.h>
#include <MacErrors.h>
#include <MacTypes.h>
#include <Processes.h>
#include <Timer.h>
#include <string.h>

#include "GXMetalInputDiagnostics.h"
#include "gxmetal_protocol.h"

#ifndef GXMETAL_INPUT_TICKLE_ONLY_DIAGNOSTIC
#define GXMETAL_INPUT_TICKLE_ONLY_DIAGNOSTIC 0
#endif

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
static ProcessSerialNumber gDeviceOwner;
static Boolean gDeviceOwnerValid;
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
static GXMetalInputTraceSnapshot gInputTrace = {
    .magic = GXMETAL_INPUT_TRACE_MAGIC,
    .version = GXMETAL_INPUT_TRACE_VERSION,
    .snapshot_bytes = sizeof(GXMetalInputTraceSnapshot),
    .event_capacity = GXMETAL_INPUT_TRACE_EVENT_CAPACITY,
    .last_current_process_result = noErr,
    .last_owner_process_result = noErr,
    .tickle_only_diagnostic = GXMETAL_INPUT_TICKLE_ONLY_DIAGNOSTIC
};

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
static const unsigned char kGXMetalInputTraceName[] = {
    19, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'I', 'n', 'p', 'u', 't',
    ' ', 'T', 'r', 'a', 'c', 'e'
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
static void GXMetalInputPoll(void);
static OSStatus GXMetalInputCreate(void);
static void GXMetalInputResetDevices(Boolean disposeObjects);

static void GXMetalInputTraceInitialize(void)
{
    memset(&gInputTrace, 0, sizeof(gInputTrace));
    gInputTrace.magic = GXMETAL_INPUT_TRACE_MAGIC;
    gInputTrace.version = GXMETAL_INPUT_TRACE_VERSION;
    gInputTrace.snapshot_bytes = sizeof(gInputTrace);
    gInputTrace.event_capacity = GXMETAL_INPUT_TRACE_EVENT_CAPACITY;
    gInputTrace.last_current_process_result = noErr;
    gInputTrace.last_owner_process_result = noErr;
    gInputTrace.tickle_only_diagnostic =
        GXMETAL_INPUT_TICKLE_ONLY_DIAGNOSTIC;
}

static OSErr GXMetalInputTraceFile(FSSpec *trace)
{
    short volume = 0;
    long directory = 0;
    OSErr error;

    error = FindFolder(kOnSystemDisk, kPreferencesFolderType, false,
                       &volume, &directory);
    if (error != noErr) {
        return error;
    }
    error = FSMakeFSSpec(volume, directory, kGXMetalInputTraceName, trace);
    return error == fnfErr ? noErr : error;
}

static void GXMetalInputTraceLoad(void)
{
    GXMetalInputTraceSnapshot loaded;
    FSSpec trace;
    short refNum = -1;
    long length = (long)sizeof(loaded);
    OSErr error;

    GXMetalInputTraceInitialize();
    gInputTrace.trace_load_count++;
    error = GXMetalInputTraceFile(&trace);
    if (error == noErr) {
        error = FSpOpenDF(&trace, fsRdPerm, &refNum);
    }
    if (error == noErr) {
        error = FSRead(refNum, &length, &loaded);
        (void)FSClose(refNum);
    }
    if ((error == noErr || error == eofErr) &&
        length == (long)sizeof(loaded) &&
        loaded.magic == GXMETAL_INPUT_TRACE_MAGIC &&
        loaded.version == GXMETAL_INPUT_TRACE_VERSION &&
        loaded.snapshot_bytes == sizeof(loaded) &&
        loaded.event_capacity == GXMETAL_INPUT_TRACE_EVENT_CAPACITY) {
        gInputTrace = loaded;
        gInputTrace.trace_load_count++;
        gInputTrace.trace_load_valid_count++;
    }
    gInputTrace.tickle_only_diagnostic =
        GXMETAL_INPUT_TICKLE_ONLY_DIAGNOSTIC;
}

static void GXMetalInputTracePersist(void)
{
    FSSpec trace;
    short refNum = -1;
    long length = (long)sizeof(gInputTrace);
    OSErr error;

    gInputTrace.trace_persist_attempt_count++;
    gInputTrace.trace_persist_success_count++;
    error = GXMetalInputTraceFile(&trace);
    if (error == noErr) {
        error = FSpOpenDF(&trace, fsWrPerm, &refNum);
        if (error != noErr) {
            error = FSpCreate(&trace, 'GXMT', 'GXIT', smSystemScript);
            if (error == noErr) {
                error = FSpOpenDF(&trace, fsWrPerm, &refNum);
            }
        }
    }
    if (error == noErr) {
        error = SetFPos(refNum, fsFromStart, 0);
    }
    if (error == noErr) {
        error = FSWrite(refNum, &length, &gInputTrace);
        if (error == noErr && length != (long)sizeof(gInputTrace)) {
            error = ioErr;
        }
    }
    if (error == noErr) {
        error = SetEOF(refNum, (long)sizeof(gInputTrace));
    }
    if (refNum >= 0) {
        OSErr closeError = FSClose(refNum);
        if (error == noErr) {
            error = closeError;
        }
    }
    if (error != noErr) {
        gInputTrace.trace_persist_success_count--;
        gInputTrace.trace_persist_failure_count++;
    }
}

static OSErr GXMetalInputTraceCurrentProcess(ProcessSerialNumber *process)
{
    OSErr error = GetCurrentProcess(process);

    gInputTrace.current_process_query_count++;
    gInputTrace.last_current_process_result = error;
    if (error == noErr) {
        gInputTrace.current_process_success_count++;
        gInputTrace.last_current_process_high = process->highLongOfPSN;
        gInputTrace.last_current_process_low = process->lowLongOfPSN;
    } else {
        gInputTrace.current_process_failure_count++;
    }
    return error;
}

static void GXMetalInputTraceRecordEvent(UInt32 kind, UInt32 refCon,
                                         UInt32 argument, OSStatus result)
{
    ProcessSerialNumber process;
    OSErr processError = GXMetalInputTraceCurrentProcess(&process);
    UInt32 sequence = ++gInputTrace.event_sequence;
    GXMetalInputTraceEvent *event = &gInputTrace.events[
        (sequence - 1u) % GXMETAL_INPUT_TRACE_EVENT_CAPACITY];

    memset(event, 0, sizeof(*event));
    event->sequence = sequence;
    event->kind = kind;
    event->ref_con = refCon;
    event->argument = argument;
    event->result = result;
    event->current_process_result = processError;
    if (processError == noErr) {
        event->current_process_high = process.highLongOfPSN;
        event->current_process_low = process.lowLongOfPSN;
    }
    event->owner_valid = gDeviceOwnerValid;
    event->owner_process_result = gInputTrace.last_owner_process_result;
    if (gDeviceOwnerValid) {
        event->owner_process_high = gDeviceOwner.highLongOfPSN;
        event->owner_process_low = gDeviceOwner.lowLongOfPSN;
    }
    event->find_count = gInputTrace.find_count;
    event->set_active_count = gInputTrace.set_active_count;
    event->device_tickle_count = gInputTrace.device_tickle_count;
    event->poll_count = gInputTrace.poll_count;
    event->push_count = gInputTrace.delta_x_push_count +
        gInputTrace.delta_y_push_count + gInputTrace.button_1_push_count +
        gInputTrace.button_2_push_count + gInputTrace.button_3_push_count;
}

static void GXMetalInputCloseHostBridge(void)
{
    /* kReferenceCFrag owns a connection reference.  Do not let an inactive
     * InputSprocket driver pin GXMetal's fragment incarnation after its
     * rendering client exits. */
    gSetRelativeInputMode = NULL;
    gGetInputButtonState = NULL;
    gGetInputState = NULL;
    gGetInputEvents = NULL;
    if (gGXMetalConnection != NULL) {
        gInputTrace.bridge_close_count++;
        (void)CloseConnection(&gGXMetalConnection);
    }
}

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
    gInputTrace.bridge_resolve_attempt_count++;
    if (GetSharedLibrary(kGXMetalLibraryName, kPowerPCCFragArch,
                         kReferenceCFrag, &gGXMetalConnection,
                         &mainAddress, errorName) != noErr ||
        FindSymbol(gGXMetalConnection, kGXMetalInputModeSymbol, &modeSymbol,
                   &symbolClass) != noErr || modeSymbol == NULL ||
        symbolClass != kTVectorCFragSymbol) {
        GXMetalInputCloseHostBridge();
        gInputTrace.bridge_resolve_failure_count++;
        return;
    }
    gInputTrace.bridge_resolve_success_count++;
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

    if (relative) {
        gInputTrace.host_mode_enable_attempt_count++;
    } else {
        gInputTrace.host_mode_disable_attempt_count++;
    }
    GXMetalInputResolveHostBridge();
    if (gSetRelativeInputMode == NULL) {
        gInputTrace.host_mode_failure_count++;
        return false;
    }
    error = gSetRelativeInputMode(relative);
    if (error == noErr) {
        gInputTrace.host_mode_success_count++;
    } else {
        gInputTrace.host_mode_failure_count++;
    }
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
    gInputTrace.fallback_button_read_count++;
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
    gInputTrace.timer_poll_count++;
    /* Time Manager callbacks must not call Process Manager. Ownership is
     * reconciled by the task-level driver entry points before polling starts. */
    GXMetalInputPoll();
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
        gInputTrace.timer_stop_count++;
    }
}

static Boolean GXMetalInputGetCurrentProcess(ProcessSerialNumber *process)
{
    return process != NULL &&
           GXMetalInputTraceCurrentProcess(process) == noErr;
}

static Boolean GXMetalInputSameProcess(
    const ProcessSerialNumber *left, const ProcessSerialNumber *right)
{
    return left->highLongOfPSN == right->highLongOfPSN &&
           left->lowLongOfPSN == right->lowLongOfPSN;
}

static OSErr GXMetalInputOwnerStatus(void)
{
    ProcessInfoRec information;
    OSErr error;

    if (!gDeviceOwnerValid) {
        return noErr;
    }
    memset(&information, 0, sizeof(information));
    information.processInfoLength = sizeof(information);
    error = GetProcessInformation(&gDeviceOwner, &information);
    gInputTrace.owner_process_query_count++;
    gInputTrace.last_owner_process_result = error;
    if (error == procNotFound) {
        gInputTrace.owner_process_not_found_count++;
    } else if (error != noErr) {
        gInputTrace.owner_process_other_failure_count++;
    }
    return error;
}

static Boolean GXMetalInputCurrentProcessOwnsDevices(void)
{
    ProcessSerialNumber process;

    return !gDeviceOwnerValid ||
           !GXMetalInputGetCurrentProcess(&process) ||
           GXMetalInputSameProcess(&process, &gDeviceOwner);
}

static void GXMetalInputRememberOwner(const ProcessSerialNumber *process)
{
    if (process == NULL) {
        gDeviceOwnerValid = false;
        memset(&gDeviceOwner, 0, sizeof(gDeviceOwner));
        return;
    }
    gDeviceOwner = *process;
    gDeviceOwnerValid = true;
}

static void GXMetalInputDeactivate(void)
{
    GXMetalInputStopPolling();
    if (gSetRelativeInputMode != NULL) {
        (void)gSetRelativeInputMode(false);
    }
    gHostRelativeInput = false;
    gDirectHostInput = false;
    gActiveDeviceMask = 0;
    gActive = false;
    gHavePosition = false;
    GXMetalInputCloseHostBridge();
}

static void GXMetalInputStartPolling(void)
{
#if GXMETAL_INPUT_TICKLE_ONLY_DIAGNOSTIC
    gInputTrace.timer_start_suppressed_count++;
    return;
#endif
    if (!gPollTimerInstalled) {
        memset(&gPollTimer, 0, sizeof(gPollTimer));
        gPollTimer.tmAddr = gPollTimerProc;
        InsXTime((QElemPtr)&gPollTimer);
        gPollTimerInstalled = true;
        gInputTrace.timer_start_count++;
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

static OSStatus GXMetalInputFinishSetActive(UInt32 refCon, Boolean active,
                                             OSStatus result)
{
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceSetActiveExit, refCon,
                           active, result);
    GXMetalInputTracePersist();
    return result;
}

static OSStatus GXMetalInputSetActive(UInt32 refCon, Boolean active)
{
    ProcessSerialNumber process;
    SInt32 deltaX = 0;
    SInt32 deltaY = 0;
    UInt32 buttons;
    UInt32 buttonDownEdges = 0;
    UInt32 buttonUpEdges = 0;
    Boolean wasActive;
    Boolean processKnown = GXMetalInputGetCurrentProcess(&process);
    OSStatus error;

    gInputTrace.set_active_count++;
    if (active) {
        gInputTrace.set_active_true_count++;
    } else {
        gInputTrace.set_active_false_count++;
    }
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceSetActiveEnter, refCon,
                           active, noErr);
    if (!active && processKnown && gDeviceOwnerValid &&
        !GXMetalInputSameProcess(&process, &gDeviceOwner)) {
        /* A delayed Stop/SetActive(false) from an older InputSprocket client
         * must not tear down devices already rebound to a newer process. */
        gInputTrace.stale_callback_count++;
        GXMetalInputTraceRecordEvent(kGXMetalInputTraceStaleCallback, refCon,
                               active, noErr);
        return GXMetalInputFinishSetActive(refCon, active, noErr);
    }
    if (active && processKnown &&
        (!gDeviceOwnerValid ||
         !GXMetalInputSameProcess(&process, &gDeviceOwner))) {
        OSErr ownerStatus = GXMetalInputOwnerStatus();
        Boolean disposeObjects =
            gDeviceOwnerValid && ownerStatus != procNotFound;

        /* InputSprocket can omit both Stop and DisposeDevices when an
         * application exits. Its keep-loaded driver fragment then retains
         * device references, an active mask, timer state, and a CFM bridge
         * owned by the dead process. An activation by a new process is an
         * authoritative handoff. Never call InputSprocket disposal routines
         * for references whose owning process has already vanished. */
        gInputTrace.owner_handoff_count++;
        if (gDeviceOwnerValid && ownerStatus == procNotFound) {
            gInputTrace.dead_owner_forget_count++;
        } else if (disposeObjects) {
            gInputTrace.live_owner_dispose_count++;
        }
        GXMetalInputResetDevices(disposeObjects);
        error = GXMetalInputCreate();
        if (error != noErr) {
            return GXMetalInputFinishSetActive(refCon, active, error);
        }
        GXMetalInputRememberOwner(&process);
    }

    wasActive = gActiveDeviceMask != 0;

    if (active) {
        gActiveDeviceMask |= refCon;
    } else {
        gActiveDeviceMask &= ~refCon;
    }
    gActive = gActiveDeviceMask != 0;
    if (wasActive == gActive) {
        return GXMetalInputFinishSetActive(refCon, active, noErr);
    }
    if (!gActive) {
        GXMetalInputDeactivate();
        return GXMetalInputFinishSetActive(refCon, active, noErr);
    }
    gHostRelativeInput = GXMetalInputSetHostMode(true);
    gDirectHostInput = gHostRelativeInput &&
        (gGetInputEvents != NULL || gGetInputState != NULL);
    gHavePosition = false;
    gLastButtons = GXMetalInputReadButtons();
    if (gActive) {
        if (gDirectHostInput) {
            gInputTrace.host_event_read_attempt_count++;
            OSErr error = gGetInputEvents != NULL ?
                gGetInputEvents(&deltaX, &deltaY, &buttons,
                                &buttonDownEdges, &buttonUpEdges) :
                gGetInputState(&deltaX, &deltaY, &buttons);
            if (error == noErr) {
                gInputTrace.host_event_read_success_count++;
                /* Activating relative mode clears old host deltas.  Establish
                 * the exact current button baseline before the first poll. */
                gLastButtons = buttons;
                gHavePosition = true;
            } else {
                gInputTrace.host_event_read_failure_count++;
            }
        } else if (gHostRelativeInput) {
            GXMetalInputBeginRelativeTracking();
        }
        GXMetalInputStartPolling();
    }
    return GXMetalInputFinishSetActive(refCon, active, noErr);
}

static OSStatus GXMetalInputDeviceTickle(UInt32 refCon)
{
    ProcessSerialNumber process;

    gInputTrace.device_tickle_count++;

    if (GXMetalInputGetCurrentProcess(&process) &&
        (!gDeviceOwnerValid ||
         !GXMetalInputSameProcess(&process, &gDeviceOwner))) {
        if (gDeviceOwnerValid && GXMetalInputOwnerStatus() != procNotFound) {
            /* Do not let a background client's tickle steal live devices. */
            return noErr;
        }
        if (GXMetalInputSetActive(refCon, true) != noErr) {
            return noErr;
        }
    }
    GXMetalInputPoll();
    return noErr;
}

static OSStatus GXMetalInputStopDevice(UInt32 refCon)
{
    OSStatus error;

    gInputTrace.stop_count++;
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceStop, refCon, 0, noErr);
    error = GXMetalInputSetActive(refCon, false);
    return error;
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
    gInputTrace.element_new_attempt_count++;
    {
        OSStatus error = ISpElement_New(&definition, element);
        if (error == noErr) {
            gInputTrace.element_new_success_count++;
        }
        return error;
    }
}

static void GXMetalInputResetDevices(Boolean disposeObjects)
{
    gInputTrace.reset_count++;
    GXMetalInputTraceRecordEvent(
        kGXMetalInputTraceReset, 0, disposeObjects, noErr);
    GXMetalInputDeactivate();
    if (disposeObjects && gButton3 != NULL) {
        (void)ISpElement_Dispose(gButton3);
        gInputTrace.element_dispose_count++;
    }
    gButton3 = NULL;
    if (disposeObjects && gButton2 != NULL) {
        (void)ISpElement_Dispose(gButton2);
        gInputTrace.element_dispose_count++;
    }
    gButton2 = NULL;
    if (disposeObjects && gButton1 != NULL) {
        (void)ISpElement_Dispose(gButton1);
        gInputTrace.element_dispose_count++;
    }
    gButton1 = NULL;
    if (disposeObjects && gDeltaY != NULL) {
        (void)ISpElement_Dispose(gDeltaY);
        gInputTrace.element_dispose_count++;
    }
    gDeltaY = NULL;
    if (disposeObjects && gDeltaX != NULL) {
        (void)ISpElement_Dispose(gDeltaX);
        gInputTrace.element_dispose_count++;
    }
    gDeltaX = NULL;
    if (disposeObjects && gDevice != NULL) {
        (void)ISpDevice_Dispose(gDevice);
        gInputTrace.device_dispose_count++;
    }
    gDevice = NULL;
    if (disposeObjects && gQuakeMotionDevice != NULL) {
        (void)ISpDevice_Dispose(gQuakeMotionDevice);
        gInputTrace.device_dispose_count++;
    }
    gQuakeMotionDevice = NULL;
    GXMetalInputRememberOwner(NULL);
}

static void GXMetalInputDispose(void)
{
    GXMetalInputResetDevices(true);
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
    gInputTrace.device_new_attempt_count++;
    {
        OSStatus error = ISpDevice_New(
            &definition, GXMetalInputMetaHandler, refCon, device);
        if (error == noErr) {
            gInputTrace.device_new_success_count++;
        }
        return error;
    }
}

static OSStatus GXMetalInputFinishCreate(OSStatus error)
{
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceCreate, 0, 0, error);
    return error;
}

static OSStatus GXMetalInputCreate(void)
{
    ISpDeltaConfigurationInfo deltaConfiguration = {0, 0};
    ISpButtonConfigurationInfo buttonConfiguration;
    Boolean quakeLayout;
    OSStatus error;

    if (gDevice != NULL) {
        GXMetalInputTraceRecordEvent(kGXMetalInputTraceCreate, 0, 1, noErr);
        return noErr;
    }

    quakeLayout = GXMetalInputUseQuakeLayout();
    error = GXMetalInputCreateDevice("GXMetal Seamless Mouse", 'GXMI',
                                     kGXMetalInputMainDevice, &gDevice);
    if (error != noErr) {
        GXMetalInputDispose();
        return GXMetalInputFinishCreate(error);
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
    return GXMetalInputFinishCreate(error);
}

OSErr GXMetalInputCFMInitialize(const CFragInitBlock *initBlock)
{
    GXMetalInputTraceLoad();
    gInputTrace.cfm_initialize_count++;
    if (initBlock != NULL) {
        gInputTrace.cfrag_context_id =
            (UInt32)(uintptr_t)initBlock->contextID;
        gInputTrace.cfrag_closure_id =
            (UInt32)(uintptr_t)initBlock->closureID;
        gInputTrace.cfrag_connection_id =
            (UInt32)(uintptr_t)initBlock->connectionID;
    }
    GXMetalInputTraceRecordEvent(
        kGXMetalInputTraceCFMInitialize, 0, 0, noErr);
    GXMetalInputTracePersist();
    return noErr;
}

OSStatus ISpDriver_CheckConfiguration(Boolean *validConfiguration)
{
    gInputTrace.check_configuration_count++;
    if (validConfiguration == NULL) {
        GXMetalInputTraceRecordEvent(kGXMetalInputTraceCheckConfiguration,
                               0, 0, paramErr);
        GXMetalInputTracePersist();
        return paramErr;
    }
    *validConfiguration = true;
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceCheckConfiguration,
                           0, 1, noErr);
    GXMetalInputTracePersist();
    return noErr;
}

OSStatus ISpDriver_FindAndLoadDevices(Boolean *keepDriverLoaded)
{
    ProcessSerialNumber process;
    Boolean processKnown;
    OSStatus error;

    gInputTrace.find_count++;
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceFindEnter, 0, 0, noErr);
    if (keepDriverLoaded == NULL) {
        GXMetalInputTraceRecordEvent(kGXMetalInputTraceFindExit,
                               0, 0, paramErr);
        GXMetalInputTracePersist();
        return paramErr;
    }
    processKnown = GXMetalInputGetCurrentProcess(&process);
    if (processKnown && gDevice != NULL &&
        (!gDeviceOwnerValid ||
         !GXMetalInputSameProcess(&process, &gDeviceOwner))) {
        OSErr ownerStatus = GXMetalInputOwnerStatus();
        Boolean disposeObjects =
            gDeviceOwnerValid && ownerStatus != procNotFound;

        /* Device and element references are scoped to the InputSprocket
         * client that created them even though the driver fragment itself is
         * kept loaded. A process which exits without DisposeDevices leaves
         * stale non-NULL references behind. Forget dead-client references and
         * enumerate fresh objects for the new process. */
        gInputTrace.owner_handoff_count++;
        if (gDeviceOwnerValid && ownerStatus == procNotFound) {
            gInputTrace.dead_owner_forget_count++;
        } else if (disposeObjects) {
            gInputTrace.live_owner_dispose_count++;
        }
        GXMetalInputResetDevices(disposeObjects);
    }
    error = GXMetalInputCreate();
    if (error == noErr && processKnown) {
        GXMetalInputRememberOwner(&process);
    }
    *keepDriverLoaded = error == noErr;
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceFindExit, 0,
                           *keepDriverLoaded, error);
    GXMetalInputTracePersist();
    return error;
}

OSStatus ISpDriver_DisposeDevices(void)
{
    gInputTrace.dispose_count++;
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceDisposeEnter, 0, 0, noErr);
    if (!GXMetalInputCurrentProcessOwnsDevices()) {
        /* A late callback from the old client must not dispose the current
         * process's replacement devices. */
        gInputTrace.stale_callback_count++;
        GXMetalInputTraceRecordEvent(
            kGXMetalInputTraceStaleCallback, 0, 0, noErr);
        GXMetalInputTraceRecordEvent(
            kGXMetalInputTraceDisposeExit, 0, 0, noErr);
        GXMetalInputTracePersist();
        return noErr;
    }
    GXMetalInputDispose();
    GXMetalInputTraceRecordEvent(kGXMetalInputTraceDisposeExit, 0, 0, noErr);
    GXMetalInputTracePersist();
    return noErr;
}

static void GXMetalInputPushSimple(ISpElementReference element, UInt32 data,
                                   const AbsoluteTime *now, UInt32 *counter)
{
    OSStatus error;

    (*counter)++;
    error = ISpElement_PushSimpleData(element, data, now);
    if (error != noErr) {
        gInputTrace.push_failure_count++;
    }
}

static void GXMetalInputPushButton(ISpElementReference element, UInt32 mask,
                                   UInt32 buttons, UInt32 buttonDownEdges,
                                   UInt32 buttonUpEdges,
                                   const AbsoluteTime *now, UInt32 *counter)
{
    Boolean down = (buttons & mask) != 0;
    Boolean reportedDown = (gLastButtons & mask) != 0;
    Boolean downEdge = (buttonDownEdges & mask) != 0;
    Boolean upEdge = (buttonUpEdges & mask) != 0;

    if (downEdge && upEdge) {
        /* Two transitions occurred between polls. The final held state tells
         * us which ordering preserves both the click and the current state. */
        GXMetalInputPushSimple(
            element, down ? kISpButtonUp : kISpButtonDown, now, counter);
        GXMetalInputPushSimple(
            element, down ? kISpButtonDown : kISpButtonUp, now, counter);
        return;
    }
    if (downEdge && !reportedDown) {
        GXMetalInputPushSimple(element, kISpButtonDown, now, counter);
        reportedDown = true;
    }
    if (upEdge && reportedDown) {
        GXMetalInputPushSimple(element, kISpButtonUp, now, counter);
        reportedDown = false;
    }
    if (reportedDown != down) {
        GXMetalInputPushSimple(
            element, down ? kISpButtonDown : kISpButtonUp, now, counter);
    }
}

static void GXMetalInputPoll(void)
{
    Point position;
    UInt32 buttons;
    UInt32 buttonDownEdges = 0;
    UInt32 buttonUpEdges = 0;
    AbsoluteTime now;
    SInt32 deltaX;
    SInt32 deltaY;

    gInputTrace.poll_count++;
    if (!gActive || gDevice == NULL) {
        gInputTrace.inactive_poll_count++;
        return;
    }
    gInputTrace.active_poll_count++;
    deltaX = 0;
    deltaY = 0;
    if (gDirectHostInput) {
        gInputTrace.host_event_read_attempt_count++;
        OSErr error = gGetInputEvents != NULL ?
            gGetInputEvents(&deltaX, &deltaY, &buttons,
                            &buttonDownEdges, &buttonUpEdges) :
            gGetInputState(&deltaX, &deltaY, &buttons);
        if (error != noErr) {
            gInputTrace.host_event_read_failure_count++;
            return;
        }
        gInputTrace.host_event_read_success_count++;
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
        GXMetalInputPushSimple(
            gDeltaX, (UInt32)(deltaX * kGXMetalInputDeltaScale), &now,
            &gInputTrace.delta_x_push_count);
    }
    if (deltaY != 0) {
        /* QEMU relative motion and QuickDraw positions increase toward the
         * bottom. Convert once to InputSprocket's positive-up delta here. */
        GXMetalInputPushSimple(
            gDeltaY, (UInt32)(
                GXMETAL_INPUT_CURSOR_DELTA_Y(deltaY) *
                kGXMetalInputDeltaScale),
            &now, &gInputTrace.delta_y_push_count);
    }
    GXMetalInputPushButton(gButton1, GXMETAL_INPUT_BUTTON_ONE, buttons,
                           buttonDownEdges, buttonUpEdges, &now,
                           &gInputTrace.button_1_push_count);
    GXMetalInputPushButton(gButton2, GXMETAL_INPUT_BUTTON_TWO, buttons,
                           buttonDownEdges, buttonUpEdges, &now,
                           &gInputTrace.button_2_push_count);
    GXMetalInputPushButton(gButton3, GXMETAL_INPUT_BUTTON_THREE, buttons,
                           buttonDownEdges, buttonUpEdges, &now,
                           &gInputTrace.button_3_push_count);

    if (gHostRelativeInput && !gDirectHostInput) {
        GXMetalInputMoveCursor(gRelativeAnchor);
        position = gRelativeAnchor;
    }
    gLastPosition = position;
    gLastButtons = buttons;
}

void ISpDriver_Tickle(void)
{
    ProcessSerialNumber process;

    gInputTrace.driver_tickle_count++;

    if (GXMetalInputGetCurrentProcess(&process) &&
        (!gDeviceOwnerValid ||
         !GXMetalInputSameProcess(&process, &gDeviceOwner))) {
        if (gDeviceOwnerValid && GXMetalInputOwnerStatus() != procNotFound) {
            return;
        }
        if (GXMetalInputSetActive(kGXMetalInputMainDevice, true) != noErr) {
            return;
        }
    }
    GXMetalInputPoll();
}
