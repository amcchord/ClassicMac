#include <Dialogs.h>
#include <Files.h>
#include <Finder.h>
#include <Folders.h>
#include <Fonts.h>
#include <Memory.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <TextEdit.h>
#include <Windows.h>

#include <string.h>

#define GXMETAL_ALERT_ID 128
#define GXMETAL_COPY_BUFFER_BYTES 32768L

static const unsigned char kDriverName[] = {
    7, 'G', 'X', 'M', 'e', 't', 'a', 'l'
};
static const unsigned char kTemporaryName[] = {
    15, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'I', 'n', 's', 't', 'a', 'l', 'l'
};
static const unsigned char kBackupName[] = {
    14, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'B', 'a', 'c', 'k', 'u', 'p'
};

static void GXMetalInitToolbox(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

static void GXMetalCStringToPascal(const char *source, Str255 destination)
{
    size_t length = strlen(source);

    if (length > 255) {
        length = 255;
    }
    destination[0] = (unsigned char)length;
    memcpy(destination + 1, source, length);
}

static void GXMetalShowResult(Boolean success, const char *message)
{
    Str255 text;
    Str255 empty = {0};

    GXMetalCStringToPascal(message, text);
    ParamText(text, empty, empty, empty);
    if (success) {
        (void)NoteAlert(GXMETAL_ALERT_ID, NULL);
    } else {
        (void)StopAlert(GXMETAL_ALERT_ID, NULL);
    }
}

static OSErr GXMetalCopyFork(const FSSpec *source,
                             const FSSpec *destination,
                             Boolean resourceFork)
{
    short sourceRef = -1;
    short destinationRef = -1;
    long remaining = 0;
    Ptr buffer = NULL;
    OSErr error;

    error = resourceFork ? FSpOpenRF(source, fsRdPerm, &sourceRef) :
                           FSpOpenDF(source, fsRdPerm, &sourceRef);
    if (error != noErr) {
        return error;
    }
    error = resourceFork ? FSpOpenRF(destination, fsWrPerm, &destinationRef) :
                           FSpOpenDF(destination, fsWrPerm, &destinationRef);
    if (error != noErr) {
        (void)FSClose(sourceRef);
        return error;
    }
    error = GetEOF(sourceRef, &remaining);
    if (error == noErr && remaining > 0) {
        buffer = NewPtr(GXMETAL_COPY_BUFFER_BYTES);
        if (buffer == NULL) {
            error = memFullErr;
        }
    }
    while (error == noErr && remaining > 0) {
        long count = remaining > GXMETAL_COPY_BUFFER_BYTES ?
                     GXMETAL_COPY_BUFFER_BYTES : remaining;
        long written;

        error = FSRead(sourceRef, &count, buffer);
        if (error != noErr) {
            break;
        }
        written = count;
        error = FSWrite(destinationRef, &written, buffer);
        if (error == noErr && written != count) {
            error = ioErr;
        }
        remaining -= count;
    }
    if (buffer != NULL) {
        DisposePtr(buffer);
    }
    (void)FSClose(destinationRef);
    (void)FSClose(sourceRef);
    return error;
}

static Boolean GXMetalSpecExists(const FSSpec *spec)
{
    FInfo finderInfo;
    return FSpGetFInfo(spec, &finderInfo) == noErr;
}

static OSErr GXMetalDeleteIfPresent(FSSpec *spec)
{
    OSErr error = FSpDelete(spec);
    return error == fnfErr ? noErr : error;
}

static OSErr GXMetalInstallDriver(const FSSpec *source,
                                  short extensionsVRef,
                                  long extensionsDirID)
{
    FSSpec target;
    FSSpec temporary;
    FSSpec backup;
    FInfo finderInfo;
    Boolean hadTarget;
    OSErr error;

    error = FSpGetFInfo(source, &finderInfo);
    if (error != noErr) {
        return error;
    }
    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       kTemporaryName, &temporary);
    error = GXMetalDeleteIfPresent(&temporary);
    if (error != noErr) {
        return error;
    }
    error = FSpCreate(&temporary, finderInfo.fdCreator,
                      finderInfo.fdType, smSystemScript);
    if (error != noErr) {
        return error;
    }
    error = GXMetalCopyFork(source, &temporary, false);
    if (error == noErr) {
        error = GXMetalCopyFork(source, &temporary, true);
    }
    if (error == noErr) {
        finderInfo.fdLocation.h = 0;
        finderInfo.fdLocation.v = 0;
        error = FSpSetFInfo(&temporary, &finderInfo);
    }
    if (error != noErr) {
        (void)FSpDelete(&temporary);
        return error;
    }

    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       kDriverName, &target);
    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       kBackupName, &backup);
    error = GXMetalDeleteIfPresent(&backup);
    if (error != noErr) {
        (void)FSpDelete(&temporary);
        return error;
    }
    hadTarget = GXMetalSpecExists(&target);
    if (hadTarget) {
        error = FSpRename(&target, kBackupName);
        if (error != noErr) {
            (void)FSpDelete(&temporary);
            return error;
        }
    }
    error = FSpRename(&temporary, kDriverName);
    if (error != noErr) {
        if (hadTarget) {
            (void)FSpRename(&backup, kDriverName);
        }
        (void)FSpDelete(&temporary);
        return error;
    }
    if (hadTarget) {
        (void)FSpDelete(&backup);
    }
    return FlushVol(NULL, extensionsVRef);
}

int main(void)
{
    ProcessSerialNumber process;
    ProcessInfoRec processInfo;
    FSSpec application;
    FSSpec source;
    short extensionsVRef = 0;
    long extensionsDirID = 0;
    OSErr error;

    GXMetalInitToolbox();
    memset(&processInfo, 0, sizeof(processInfo));
    processInfo.processInfoLength = sizeof(processInfo);
    processInfo.processAppSpec = &application;

    error = GetCurrentProcess(&process);
    if (error == noErr) {
        error = GetProcessInformation(&process, &processInfo);
    }
    if (error == noErr) {
        error = FSMakeFSSpec(application.vRefNum, application.parID,
                             kDriverName, &source);
    }
    if (error != noErr) {
        GXMetalShowResult(false,
            "GXMetal was not found beside this installer. Keep both files in the GXMetal folder and try again.");
        return 1;
    }
    error = FindFolder(kOnSystemDisk, kExtensionFolderType, false,
                       &extensionsVRef, &extensionsDirID);
    if (error != noErr) {
        GXMetalShowResult(false,
            "The active System Folder's Extensions folder could not be located.");
        return 1;
    }
    error = GXMetalInstallDriver(&source, extensionsVRef, extensionsDirID);
    if (error != noErr) {
        GXMetalShowResult(false,
            "GXMetal could not be installed. Make sure the startup disk is writable and has free space, then try again.");
        return 1;
    }
    GXMetalShowResult(true,
        "GXMetal is installed in the active Extensions folder. Restart the Mac, then run GXMetal Test.");
    return 0;
}
