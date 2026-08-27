/* SPDX-License-Identifier: MIT */

#ifndef GXMETAL_INPUT_TEST_PLATFORM_H
#define GXMETAL_INPUT_TEST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifndef pascal
#define pascal
#endif

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

typedef int16_t OSErr;
typedef int32_t OSStatus;
typedef uint8_t Boolean;
typedef uint8_t UInt8;
typedef uint32_t UInt32;
typedef int32_t SInt32;
typedef uint32_t OSType;
typedef void *Ptr;
typedef unsigned char Str63[64];
typedef unsigned char Str255[256];

enum {
    noErr = 0,
    ioErr = -36,
    eofErr = -39,
    fnfErr = -43,
    paramErr = -50,
    procNotFound = -600
};

typedef struct Point {
    int16_t v;
    int16_t h;
} Point;

typedef struct Rect {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
} Rect;

typedef struct GDevice {
    Rect gdRect;
} GDevice;
typedef GDevice **GDHandle;

GDHandle GetMainDevice(void);

typedef void *CFragConnectionID;
typedef UInt32 CFragContextID;
typedef UInt32 CFragClosureID;
typedef UInt32 CFragSymbolClass;
typedef struct CFragInitBlock {
    CFragContextID contextID;
    CFragClosureID closureID;
    CFragConnectionID connectionID;
} CFragInitBlock;

enum {
    kPowerPCCFragArch = 1,
    kReferenceCFrag = 1,
    kTVectorCFragSymbol = 2
};

OSErr GetSharedLibrary(const unsigned char *libraryName, UInt32 architecture,
                       UInt32 loadFlags, CFragConnectionID *connection,
                       Ptr *mainAddress, Str255 errorName);
OSErr FindSymbol(CFragConnectionID connection,
                 const unsigned char *symbolName, Ptr *symbolAddress,
                 CFragSymbolClass *symbolClass);
OSErr CloseConnection(CFragConnectionID *connection);

typedef void *ISpDeviceReference;
typedef void *ISpElementReference;
typedef UInt32 ISpElementKind;
typedef UInt32 ISpElementLabel;
typedef UInt32 ISpMetaHandlerSelector;
typedef OSStatus (*ISpDriverFunctionPtr_Generic)(void);
typedef ISpDriverFunctionPtr_Generic (*ISpDriverMetaHandler)(
    UInt32 refCon, ISpMetaHandlerSelector selector);

typedef struct ISpDeviceDefinition {
    Str63 deviceName;
    OSType theDeviceClass;
    OSType theDeviceIdentifier;
    OSType permanentID;
} ISpDeviceDefinition;

typedef struct ISpElementDefinitionStruct {
    ISpDeviceReference device;
    Str63 theString;
    ISpElementKind kind;
    ISpElementLabel label;
    void *configInfo;
    UInt32 configInfoLength;
    UInt32 dataSize;
} ISpElementDefinitionStruct;

typedef struct ISpDeltaConfigurationInfo {
    UInt32 minimum;
    UInt32 maximum;
} ISpDeltaConfigurationInfo;

typedef struct ISpButtonConfigurationInfo {
    UInt32 id;
} ISpButtonConfigurationInfo;

typedef uint64_t AbsoluteTime;

enum {
    kISpDeviceClass_Mouse = 1,
    kISpElementKind_Delta = 2,
    kISpElementKind_Button = 3,
    kISpElementLabel_Delta_Cursor_X = 4,
    kISpElementLabel_Delta_Cursor_Y = 5,
    kISpElementLabel_Btn_MouseOne = 6,
    kISpElementLabel_Btn_MouseTwo = 7,
    kISpElementLabel_Btn_MouseThree = 8,
    kISpButtonUp = 9,
    kISpButtonDown = 10,
    kISpSelector_Stop = 11,
    kISpSelector_SetActive = 12,
    kISpSelector_Tickle = 13
};

OSStatus ISpDevice_New(const ISpDeviceDefinition *definition,
                       ISpDriverMetaHandler metaHandler, UInt32 refCon,
                       ISpDeviceReference *device);
OSStatus ISpDevice_Dispose(ISpDeviceReference device);
OSStatus ISpElement_New(const ISpElementDefinitionStruct *definition,
                        ISpElementReference *element);
OSStatus ISpElement_Dispose(ISpElementReference element);
OSStatus ISpElement_PushSimpleData(ISpElementReference element, UInt32 data,
                                   const AbsoluteTime *when);
AbsoluteTime ISpUptime(void);

UInt8 LMGetMouseButtonState(void);
Point LMGetMouseLocation(void);
void LMSetMouseTemp(Point position);
void LMSetRawMouseLocation(Point position);
void LMSetMouseLocation(Point position);

typedef struct ProcessSerialNumber {
    UInt32 highLongOfPSN;
    UInt32 lowLongOfPSN;
} ProcessSerialNumber;

typedef struct FSSpec {
    UInt32 reserved;
} FSSpec;

enum {
    fsRdPerm = 1,
    fsWrPerm = 2,
    fsFromStart = 1,
    kOnSystemDisk = -32768,
    kPreferencesFolderType = 1,
    smSystemScript = 0
};

OSErr FindFolder(short volume, OSType folderType, Boolean createFolder,
                 short *foundVolume, long *foundDirectory);
OSErr FSMakeFSSpec(short volume, long directory,
                   const unsigned char *name, FSSpec *spec);
OSErr FSpOpenDF(const FSSpec *spec, signed char permission, short *refNum);
OSErr FSpCreate(const FSSpec *spec, OSType creator, OSType fileType,
                short scriptTag);
OSErr SetFPos(short refNum, short positionMode, long positionOffset);
OSErr FSRead(short refNum, long *count, void *buffer);
OSErr FSWrite(short refNum, long *count, const void *buffer);
OSErr SetEOF(short refNum, long logicalEOF);
OSErr FSClose(short refNum);

typedef struct ProcessInfoRec {
    UInt32 processInfoLength;
    unsigned char *processName;
    FSSpec *processAppSpec;
    OSType processSignature;
} ProcessInfoRec;

OSErr GetCurrentProcess(ProcessSerialNumber *process);
OSErr GetProcessInformation(const ProcessSerialNumber *process,
                            ProcessInfoRec *information);

struct TMTask;
typedef struct TMTask *TMTaskPtr;
typedef void (*TimerUPP)(TMTaskPtr task);
typedef struct TMTask {
    TimerUPP tmAddr;
} TMTask;
typedef void *QElemPtr;
typedef TimerUPP RoutineDescriptor;

#define uppTimerProcInfo 0
#define BUILD_ROUTINE_DESCRIPTOR(info, proc) (proc)

void InsXTime(QElemPtr task);
void RmvTime(QElemPtr task);
void PrimeTime(QElemPtr task, UInt32 milliseconds);

#endif
