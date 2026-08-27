/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GXMetalInputTestPlatform.h"

static unsigned failures;
static unsigned get_library_calls;
static unsigned close_connection_calls;
static unsigned enable_relative_calls;
static unsigned disable_relative_calls;
static unsigned device_new_calls;
static unsigned device_dispose_calls;
static unsigned element_new_calls;
static unsigned element_dispose_calls;
static unsigned install_timer_calls;
static unsigned remove_timer_calls;
static unsigned get_current_process_calls;
static unsigned get_process_information_calls;
static uintptr_t next_device = 1;
static uintptr_t next_element = 1;
static ProcessSerialNumber current_process = {0, 1};
static UInt32 exited_process_low;
static Boolean current_process_available = true;
static unsigned char persisted_trace[8192];
static long persisted_trace_size;
static long persisted_trace_position;
static Boolean persisted_trace_exists;
static unsigned trace_read_calls;
static unsigned trace_write_calls;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #expression); \
        failures++; \
    } \
} while (0)

static OSErr test_set_relative_input_mode(Boolean relative)
{
    if (relative) {
        enable_relative_calls++;
    } else {
        disable_relative_calls++;
    }
    return noErr;
}

OSErr GetSharedLibrary(const unsigned char *libraryName, UInt32 architecture,
                       UInt32 loadFlags, CFragConnectionID *connection,
                       Ptr *mainAddress, Str255 errorName)
{
    (void)libraryName;
    (void)architecture;
    (void)loadFlags;
    (void)errorName;
    get_library_calls++;
    *connection = (CFragConnectionID)(uintptr_t)get_library_calls;
    *mainAddress = NULL;
    return noErr;
}

OSErr FindSymbol(CFragConnectionID connection,
                 const unsigned char *symbolName, Ptr *symbolAddress,
                 CFragSymbolClass *symbolClass)
{
    union {
        OSErr (*function)(Boolean relative);
        Ptr address;
    } mode;

    (void)connection;
    if (symbolName[0] != 27) {
        return -1;
    }
    mode.function = test_set_relative_input_mode;
    *symbolAddress = mode.address;
    *symbolClass = kTVectorCFragSymbol;
    return noErr;
}

OSErr CloseConnection(CFragConnectionID *connection)
{
    CHECK(*connection != NULL);
    close_connection_calls++;
    *connection = NULL;
    return noErr;
}

OSStatus ISpDevice_New(const ISpDeviceDefinition *definition,
                       ISpDriverMetaHandler metaHandler, UInt32 refCon,
                       ISpDeviceReference *device)
{
    (void)definition;
    (void)metaHandler;
    (void)refCon;
    device_new_calls++;
    *device = (ISpDeviceReference)next_device++;
    return noErr;
}

OSStatus ISpDevice_Dispose(ISpDeviceReference device)
{
    CHECK(device != NULL);
    device_dispose_calls++;
    return noErr;
}

OSStatus ISpElement_New(const ISpElementDefinitionStruct *definition,
                        ISpElementReference *element)
{
    (void)definition;
    element_new_calls++;
    *element = (ISpElementReference)next_element++;
    return noErr;
}

OSStatus ISpElement_Dispose(ISpElementReference element)
{
    CHECK(element != NULL);
    element_dispose_calls++;
    return noErr;
}

OSStatus ISpElement_PushSimpleData(ISpElementReference element, UInt32 data,
                                   const AbsoluteTime *when)
{
    (void)element;
    (void)data;
    (void)when;
    return noErr;
}

AbsoluteTime ISpUptime(void)
{
    return 0;
}

UInt8 LMGetMouseButtonState(void)
{
    return 0x80u;
}

Point LMGetMouseLocation(void)
{
    Point position = {0, 0};
    return position;
}

void LMSetMouseTemp(Point position)
{
    (void)position;
}

void LMSetRawMouseLocation(Point position)
{
    (void)position;
}

void LMSetMouseLocation(Point position)
{
    (void)position;
}

GDHandle GetMainDevice(void)
{
    return NULL;
}

OSErr GetCurrentProcess(ProcessSerialNumber *process)
{
    get_current_process_calls++;
    if (!current_process_available) {
        return -1;
    }
    *process = current_process;
    return noErr;
}

OSErr GetProcessInformation(const ProcessSerialNumber *process,
                            ProcessInfoRec *information)
{
    get_process_information_calls++;
    if (process->lowLongOfPSN == exited_process_low) {
        return procNotFound;
    }
    information->processSignature = 0;
    return noErr;
}

OSErr FindFolder(short volume, OSType folderType, Boolean createFolder,
                 short *foundVolume, long *foundDirectory)
{
    (void)volume;
    (void)folderType;
    (void)createFolder;
    *foundVolume = 1;
    *foundDirectory = 2;
    return noErr;
}

OSErr FSMakeFSSpec(short volume, long directory,
                   const unsigned char *name, FSSpec *spec)
{
    (void)volume;
    (void)directory;
    (void)name;
    spec->reserved = 1;
    return persisted_trace_exists ? noErr : fnfErr;
}

OSErr FSpOpenDF(const FSSpec *spec, signed char permission, short *refNum)
{
    (void)spec;
    (void)permission;
    if (!persisted_trace_exists) {
        return fnfErr;
    }
    persisted_trace_position = 0;
    *refNum = 1;
    return noErr;
}

OSErr FSpCreate(const FSSpec *spec, OSType creator, OSType fileType,
                short scriptTag)
{
    (void)spec;
    (void)creator;
    (void)fileType;
    (void)scriptTag;
    persisted_trace_exists = true;
    persisted_trace_size = 0;
    persisted_trace_position = 0;
    return noErr;
}

OSErr SetFPos(short refNum, short positionMode, long positionOffset)
{
    (void)refNum;
    CHECK(positionMode == fsFromStart);
    persisted_trace_position = positionOffset;
    return noErr;
}

OSErr FSRead(short refNum, long *count, void *buffer)
{
    long available;
    long actual;
    long requested = *count;

    (void)refNum;
    trace_read_calls++;
    available = persisted_trace_size - persisted_trace_position;
    if (available < 0) {
        available = 0;
    }
    actual = *count < available ? *count : available;
    memcpy(buffer, persisted_trace + persisted_trace_position,
           (size_t)actual);
    persisted_trace_position += actual;
    *count = actual;
    return actual == requested ? noErr : eofErr;
}

OSErr FSWrite(short refNum, long *count, const void *buffer)
{
    long end = persisted_trace_position + *count;

    (void)refNum;
    trace_write_calls++;
    CHECK(end <= (long)sizeof(persisted_trace));
    if (end > (long)sizeof(persisted_trace)) {
        return ioErr;
    }
    memcpy(persisted_trace + persisted_trace_position, buffer,
           (size_t)*count);
    persisted_trace_position = end;
    if (persisted_trace_size < end) {
        persisted_trace_size = end;
    }
    return noErr;
}

OSErr SetEOF(short refNum, long logicalEOF)
{
    (void)refNum;
    persisted_trace_size = logicalEOF;
    return noErr;
}

OSErr FSClose(short refNum)
{
    (void)refNum;
    return noErr;
}

void InsXTime(QElemPtr task)
{
    (void)task;
    install_timer_calls++;
}

void RmvTime(QElemPtr task)
{
    (void)task;
    remove_timer_calls++;
}

void PrimeTime(QElemPtr task, UInt32 milliseconds)
{
    (void)task;
    (void)milliseconds;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#include "../guest/src/GXMetalInput.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void test_connection_follows_active_lifetime(void)
{
    Boolean keep_driver_loaded = false;

    current_process.lowLongOfPSN = 1;
    exited_process_low = 0;
    CHECK(ISpDriver_FindAndLoadDevices(&keep_driver_loaded) == noErr);
    CHECK(keep_driver_loaded);
    CHECK(get_library_calls == 0);
    CHECK(gDeviceOwnerValid);
    CHECK(gDeviceOwner.lowLongOfPSN == 1);

    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, true) == noErr);
    CHECK(get_library_calls == 1);
    CHECK(enable_relative_calls == 1);
    CHECK(disable_relative_calls == 0);
    CHECK(close_connection_calls == 0);
    CHECK(gGXMetalConnection != NULL);

    /* The Time Manager path is interrupt-time polling only. Ownership checks
     * and device creation must remain in task-level callbacks. */
    {
        unsigned processQueries = get_current_process_calls;
        unsigned processInformationQueries = get_process_information_calls;

        GXMetalInputPollTimer(&gPollTimer);
        CHECK(get_current_process_calls == processQueries);
        CHECK(get_process_information_calls == processInformationQueries);
    }

    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, false) == noErr);
    CHECK(disable_relative_calls == 1);
    CHECK(close_connection_calls == 1);
    CHECK(gGXMetalConnection == NULL);
    CHECK(gSetRelativeInputMode == NULL);

    /* A redundant inactive notification must not reacquire GXMetal. */
    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, false) == noErr);
    CHECK(get_library_calls == 1);
    CHECK(close_connection_calls == 1);

    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, true) == noErr);
    CHECK(get_library_calls == 2);
    CHECK(enable_relative_calls == 2);

    /* Keep the bridge until the last active logical device stops. */
    CHECK(GXMetalInputSetActive(kGXMetalInputQuakeMotionDevice, true) == noErr);
    CHECK(get_library_calls == 2);
    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, false) == noErr);
    CHECK(close_connection_calls == 1);
    CHECK(GXMetalInputSetActive(kGXMetalInputQuakeMotionDevice, false) == noErr);
    CHECK(disable_relative_calls == 2);
    CHECK(close_connection_calls == 2);

    CHECK(ISpDriver_DisposeDevices() == noErr);
    CHECK(get_library_calls == 2);
    CHECK(close_connection_calls == 2);
}

static void test_dead_process_devices_are_forgotten_and_recreated(void)
{
    Boolean keep_driver_loaded = false;
    unsigned devicesCreated;
    unsigned devicesDisposed;
    unsigned elementsCreated;
    unsigned elementsDisposed;
    unsigned librariesOpened;
    unsigned librariesClosed;
    unsigned relativeDisabled;
    unsigned timersRemoved;

    current_process.lowLongOfPSN = 101;
    exited_process_low = 0;
    CHECK(ISpDriver_FindAndLoadDevices(&keep_driver_loaded) == noErr);
    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, true) == noErr);
    CHECK(gDeviceOwner.lowLongOfPSN == 101);
    CHECK(gActive);
    CHECK(gPollTimerInstalled);

    devicesCreated = device_new_calls;
    devicesDisposed = device_dispose_calls;
    elementsCreated = element_new_calls;
    elementsDisposed = element_dispose_calls;
    librariesOpened = get_library_calls;
    librariesClosed = close_connection_calls;
    relativeDisabled = disable_relative_calls;
    timersRemoved = remove_timer_calls;

    /* Command-Q can terminate the owner without Stop or DisposeDevices. */
    exited_process_low = 101;
    current_process.lowLongOfPSN = 202;
    CHECK(ISpDriver_FindAndLoadDevices(&keep_driver_loaded) == noErr);
    CHECK(keep_driver_loaded);
    CHECK(gDeviceOwnerValid);
    CHECK(gDeviceOwner.lowLongOfPSN == 202);
    CHECK(!gActive);
    CHECK(!gPollTimerInstalled);
    CHECK(gGXMetalConnection == NULL);
    CHECK(device_new_calls == devicesCreated + 1);
    CHECK(element_new_calls == elementsCreated + 5);
    /* References owned by an exited process are dangling; forget them rather
     * than calling InputSprocket disposal APIs in the new process. */
    CHECK(device_dispose_calls == devicesDisposed);
    CHECK(element_dispose_calls == elementsDisposed);
    CHECK(get_library_calls == librariesOpened);
    CHECK(close_connection_calls == librariesClosed + 1);
    CHECK(disable_relative_calls == relativeDisabled + 1);
    CHECK(remove_timer_calls == timersRemoved + 1);

    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, true) == noErr);
    CHECK(gActive);
    CHECK(gGXMetalConnection != NULL);
    CHECK(get_library_calls == librariesOpened + 1);

    /* A delayed callback carrying the old process identity cannot tear down
     * the replacement devices. */
    current_process.lowLongOfPSN = 101;
    CHECK(ISpDriver_DisposeDevices() == noErr);
    CHECK(gDevice != NULL);
    CHECK(gActive);
    CHECK(gDeviceOwner.lowLongOfPSN == 202);

    current_process.lowLongOfPSN = 202;
    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, false) == noErr);
    CHECK(ISpDriver_DisposeDevices() == noErr);
    CHECK(gDevice == NULL);
    CHECK(!gDeviceOwnerValid);
}

static void test_tickle_repairs_omitted_reenumeration(void)
{
    Boolean keep_driver_loaded = false;
    unsigned devicesCreated;
    unsigned devicesDisposed;

    current_process.lowLongOfPSN = 301;
    exited_process_low = 0;
    CHECK(ISpDriver_FindAndLoadDevices(&keep_driver_loaded) == noErr);
    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, true) == noErr);
    devicesCreated = device_new_calls;
    devicesDisposed = device_dispose_calls;

    /* Some InputSprocket clients can reuse a keep-loaded driver without a
     * second FindAndLoadDevices call. A task-level tickle must still detect
     * that the previous owner exited and rebuild the process-scoped objects. */
    exited_process_low = 301;
    current_process.lowLongOfPSN = 302;
    CHECK(GXMetalInputDeviceTickle(kGXMetalInputMainDevice) == noErr);
    CHECK(gDeviceOwnerValid);
    CHECK(gDeviceOwner.lowLongOfPSN == 302);
    CHECK(device_new_calls == devicesCreated + 1);
    CHECK(device_dispose_calls == devicesDisposed);
    CHECK(gActive);
    CHECK(gPollTimerInstalled);
    CHECK(gGXMetalConnection != NULL);

    CHECK(GXMetalInputSetActive(kGXMetalInputMainDevice, false) == noErr);
    CHECK(ISpDriver_DisposeDevices() == noErr);
}

static void test_live_process_handoff_disposes_owned_objects(void)
{
    Boolean keep_driver_loaded = false;
    unsigned devicesDisposed;
    unsigned elementsDisposed;

    current_process.lowLongOfPSN = 401;
    exited_process_low = 0;
    CHECK(ISpDriver_FindAndLoadDevices(&keep_driver_loaded) == noErr);
    devicesDisposed = device_dispose_calls;
    elementsDisposed = element_dispose_calls;

    current_process.lowLongOfPSN = 402;
    CHECK(ISpDriver_FindAndLoadDevices(&keep_driver_loaded) == noErr);
    CHECK(gDeviceOwner.lowLongOfPSN == 402);
    CHECK(device_dispose_calls == devicesDisposed + 1);
    CHECK(element_dispose_calls == elementsDisposed + 5);

    CHECK(ISpDriver_DisposeDevices() == noErr);
}

static void test_trace_survives_fragment_reinitialization(void)
{
    CFragInitBlock initBlock;
    UInt32 initializes = gInputTrace.cfm_initialize_count;
    UInt32 events = gInputTrace.event_sequence;
    unsigned reads = trace_read_calls;

    memset(&gInputTrace, 0, sizeof(gInputTrace));
    memset(&initBlock, 0, sizeof(initBlock));
    initBlock.contextID = 0x1234;
    initBlock.closureID = 0x5678;
    initBlock.connectionID = (CFragConnectionID)(uintptr_t)0x9abc;
    CHECK(GXMetalInputCFMInitialize(&initBlock) == noErr);
    CHECK(trace_read_calls == reads + 1);
    CHECK(gInputTrace.cfm_initialize_count == initializes + 1);
    CHECK(gInputTrace.trace_load_valid_count != 0);
    CHECK(gInputTrace.event_sequence == events + 1);
    CHECK(gInputTrace.cfrag_context_id == 0x1234);
    CHECK(gInputTrace.cfrag_closure_id == 0x5678);
    CHECK(gInputTrace.cfrag_connection_id == 0x9abc);
    CHECK(persisted_trace_size == (long)sizeof(gInputTrace));
}

int main(void)
{
    CFragInitBlock initBlock;

    memset(&initBlock, 0, sizeof(initBlock));
    initBlock.contextID = 1;
    initBlock.closureID = 2;
    initBlock.connectionID = (CFragConnectionID)(uintptr_t)3;
    CHECK(sizeof(gInputTrace) == 2456u);
    CHECK(GXMetalInputCFMInitialize(&initBlock) == noErr);
    CHECK(persisted_trace_exists);
    CHECK(persisted_trace_size == (long)sizeof(gInputTrace));
    CHECK(gInputTrace.cfm_initialize_count == 1);
    CHECK(trace_write_calls == 1);
    test_connection_follows_active_lifetime();
    test_dead_process_devices_are_forgotten_and_recreated();
    test_tickle_repairs_omitted_reenumeration();
    test_live_process_handoff_disposes_owned_objects();
    test_trace_survives_fragment_reinitialization();
    if (failures != 0) {
        fprintf(stderr, "GXMetal input lifecycle: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal input lifecycle: all tests passed");
    return EXIT_SUCCESS;
}
