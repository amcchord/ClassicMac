#include <Dialogs.h>
#include <Files.h>
#include <Finder.h>
#include <Folders.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Memory.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <TextEdit.h>
#include <Windows.h>

#include <string.h>

#define GXMETAL_ALERT_ID 128
#define GXMETAL_INSTALL_DIALOG_ID 129
#define GXMETAL_COPY_BUFFER_BYTES 32768L

static const unsigned char kDriverName[] = {
    7, 'G', 'X', 'M', 'e', 't', 'a', 'l'
};
static const unsigned char kStartupName[] = {
    15, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'S', 't', 'a', 'r', 't', 'u', 'p'
};
static const unsigned char kDriverTemporaryName[] = {
    15, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'I', 'n', 's', 't', 'a', 'l', 'l'
};
static const unsigned char kLegacyDriverBackupName[] = {
    14, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'B', 'a', 'c', 'k', 'u', 'p'
};
static const unsigned char kStartupTemporaryName[] = {
    19, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'S', 't', 'a', 'r', 't', 'u', 'p', ' ', 'N', 'e', 'w'
};
static const unsigned char kLegacyStartupBackupName[] = {
    22, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'S', 't', 'a', 'r', 't', 'u', 'p', ' ',
    'B', 'a', 'c', 'k', 'u', 'p'
};
static const unsigned char kDriverPreviousName[] = {
    16, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'P', 'r', 'e', 'v', 'i', 'o', 'u', 's'
};
static const unsigned char kStartupPreviousName[] = {
    24, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'S', 't', 'a', 'r', 't', 'u', 'p', ' ',
    'P', 'r', 'e', 'v', 'i', 'o', 'u', 's'
};
static const unsigned char kDriverLegacyDisabledName[] = {
    21, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'L', 'e', 'g', 'a', 'c', 'y', ' ',
    'B', 'a', 'c', 'k', 'u', 'p'
};
static const unsigned char kStartupLegacyDisabledName[] = {
    22, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'S', 't', 'a', 'r', 't', 'u', 'p', ' ',
    'L', 'e', 'g', 'a', 'c', 'y'
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

static short GXMetalConfirmInstall(void)
{
    DialogPtr dialog;
    short item = 0;

    dialog = GetNewDialog(GXMETAL_INSTALL_DIALOG_ID, NULL, (WindowPtr)-1L);
    if (dialog == NULL) {
        return 0;
    }
    (void)SetDialogDefaultItem(dialog, 1);
    (void)SetDialogCancelItem(dialog, 2);
    ShowWindow(GetDialogWindow(dialog));
    SelectWindow(GetDialogWindow(dialog));
    do {
        ModalDialog(NULL, &item);
    } while (item != 1 && item != 2);
    DisposeDialog(dialog);
    return item;
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

static OSErr GXMetalStageFile(const FSSpec *source,
                              ConstStr255Param temporaryName,
                              short extensionsVRef,
                              long extensionsDirID,
                              FSSpec *temporary)
{
    FInfo finderInfo;
    OSErr error;

    error = FSpGetFInfo(source, &finderInfo);
    if (error != noErr) {
        return error;
    }
    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       temporaryName, temporary);
    error = GXMetalDeleteIfPresent(temporary);
    if (error != noErr) {
        return error;
    }
    error = FSpCreate(temporary, finderInfo.fdCreator,
                      finderInfo.fdType, smSystemScript);
    if (error != noErr) {
        return error;
    }
    error = GXMetalCopyFork(source, temporary, false);
    if (error == noErr) {
        error = GXMetalCopyFork(source, temporary, true);
    }
    if (error == noErr) {
        finderInfo.fdLocation.h = 0;
        finderInfo.fdLocation.v = 0;
        error = FSpSetFInfo(temporary, &finderInfo);
    }
    if (error != noErr) {
        (void)FSpDelete(temporary);
    }
    return error;
}

static OSErr GXMetalDisableBackup(FSSpec *backup,
                                  const FInfo *originalInfo)
{
    FInfo disabledInfo = *originalInfo;

    disabledInfo.fdType = 'BINA';
    disabledInfo.fdCreator = 'GXMT';
    disabledInfo.fdFlags |= kHasNoINITs | kIsInvisible;
    disabledInfo.fdFlags &= ~(kHasCustomIcon | kHasBundle | kHasBeenInited);
    return FSpSetFInfo(backup, &disabledInfo);
}

static OSErr GXMetalRetireLegacy(ConstStr255Param activeName,
                                 ConstStr255Param retiredName,
                                 short extensionsVRef,
                                 long extensionsDirID)
{
    FSSpec active;
    FSSpec retired;
    FInfo originalInfo;
    OSErr error;

    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       activeName, &active);
    if (!GXMetalSpecExists(&active)) {
        return noErr;
    }
    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       retiredName, &retired);
    error = GXMetalDeleteIfPresent(&retired);
    if (error != noErr) {
        return error;
    }
    error = FSpGetFInfo(&active, &originalInfo);
    if (error == noErr) {
        error = FSpRename(&active, retiredName);
    }
    if (error == noErr) {
        error = GXMetalDisableBackup(&retired, &originalInfo);
    }
    if (error != noErr && GXMetalSpecExists(&retired)) {
        (void)FSpSetFInfo(&retired, &originalInfo);
        (void)FSpRename(&retired, activeName);
    }
    return error;
}

static OSErr GXMetalBackupTarget(FSSpec *target,
                                 ConstStr255Param backupName,
                                 FSSpec *backup,
                                 FInfo *originalInfo)
{
    OSErr error = FSpGetFInfo(target, originalInfo);

    if (error == noErr) {
        error = FSpRename(target, backupName);
    }
    if (error == noErr) {
        error = GXMetalDisableBackup(backup, originalInfo);
    }
    if (error != noErr && GXMetalSpecExists(backup)) {
        (void)FSpSetFInfo(backup, originalInfo);
        (void)FSpRename(backup, target->name);
    }
    return error;
}

static void GXMetalRestoreBackup(FSSpec *backup,
                                 ConstStr255Param targetName,
                                 const FInfo *originalInfo)
{
    (void)FSpSetFInfo(backup, originalInfo);
    (void)FSpRename(backup, targetName);
}

static OSErr GXMetalInstallFiles(const FSSpec *driverSource,
                                 const FSSpec *startupSource,
                                 short extensionsVRef,
                                 long extensionsDirID)
{
    FSSpec driverTarget;
    FSSpec driverTemporary;
    FSSpec driverPrevious;
    FSSpec startupTarget;
    FSSpec startupTemporary;
    FSSpec startupPrevious;
    FInfo driverOriginalInfo;
    FInfo startupOriginalInfo;
    Boolean hadDriver;
    Boolean hadStartup;
    OSErr error;

    error = GXMetalStageFile(driverSource, kDriverTemporaryName,
                             extensionsVRef, extensionsDirID,
                             &driverTemporary);
    if (error != noErr) {
        return error;
    }
    error = GXMetalStageFile(startupSource, kStartupTemporaryName,
                             extensionsVRef, extensionsDirID,
                             &startupTemporary);
    if (error != noErr) {
        (void)FSpDelete(&driverTemporary);
        return error;
    }

    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       kDriverName, &driverTarget);
    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       kStartupName, &startupTarget);
    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       kDriverPreviousName, &driverPrevious);
    (void)FSMakeFSSpec(extensionsVRef, extensionsDirID,
                       kStartupPreviousName, &startupPrevious);
    error = GXMetalRetireLegacy(kLegacyDriverBackupName,
                                kDriverLegacyDisabledName,
                                extensionsVRef, extensionsDirID);
    if (error != noErr) {
        (void)FSpDelete(&driverTemporary);
        (void)FSpDelete(&startupTemporary);
        return error;
    }
    error = GXMetalRetireLegacy(kLegacyStartupBackupName,
                                kStartupLegacyDisabledName,
                                extensionsVRef, extensionsDirID);
    if (error == noErr) {
        error = GXMetalDeleteIfPresent(&driverPrevious);
    }
    if (error == noErr) {
        error = GXMetalDeleteIfPresent(&startupPrevious);
    }
    if (error != noErr) {
        (void)FSpDelete(&driverTemporary);
        (void)FSpDelete(&startupTemporary);
        return error;
    }
    hadDriver = GXMetalSpecExists(&driverTarget);
    hadStartup = GXMetalSpecExists(&startupTarget);
    if (hadDriver) {
        error = GXMetalBackupTarget(&driverTarget, kDriverPreviousName,
                                    &driverPrevious, &driverOriginalInfo);
        if (error != noErr) {
            (void)FSpDelete(&driverTemporary);
            (void)FSpDelete(&startupTemporary);
            return error;
        }
    }
    if (hadStartup) {
        error = GXMetalBackupTarget(&startupTarget, kStartupPreviousName,
                                    &startupPrevious, &startupOriginalInfo);
        if (error != noErr) {
            if (hadDriver) {
                GXMetalRestoreBackup(&driverPrevious, kDriverName,
                                     &driverOriginalInfo);
            }
            (void)FSpDelete(&driverTemporary);
            (void)FSpDelete(&startupTemporary);
            return error;
        }
    }
    error = FSpRename(&driverTemporary, kDriverName);
    if (error != noErr) {
        if (hadDriver) {
            GXMetalRestoreBackup(&driverPrevious, kDriverName,
                                 &driverOriginalInfo);
        }
        if (hadStartup) {
            GXMetalRestoreBackup(&startupPrevious, kStartupName,
                                 &startupOriginalInfo);
        }
        (void)FSpDelete(&driverTemporary);
        (void)FSpDelete(&startupTemporary);
        return error;
    }
    error = FSpRename(&startupTemporary, kStartupName);
    if (error != noErr) {
        (void)FSpDelete(&driverTarget);
        if (hadDriver) {
            GXMetalRestoreBackup(&driverPrevious, kDriverName,
                                 &driverOriginalInfo);
        }
        if (hadStartup) {
            GXMetalRestoreBackup(&startupPrevious, kStartupName,
                                 &startupOriginalInfo);
        }
        (void)FSpDelete(&startupTemporary);
        return error;
    }
    return FlushVol(NULL, extensionsVRef);
}

int main(void)
{
    ProcessSerialNumber process;
    ProcessInfoRec processInfo;
    FSSpec application;
    FSSpec driverSource;
    FSSpec startupSource;
    short extensionsVRef = 0;
    long extensionsDirID = 0;
    long systemVersion = 0;
    short installChoice;
    OSErr error;

    GXMetalInitToolbox();
    error = Gestalt(gestaltSystemVersion, &systemVersion);
    if (error != noErr || systemVersion < 0x0850) {
        GXMetalShowResult(false,
            "GXMetal requires a Power Mac running Mac OS 8.5 through 9.2. This beta is fully tested on Mac OS 9.2.2.");
        return 1;
    }
    memset(&processInfo, 0, sizeof(processInfo));
    processInfo.processInfoLength = sizeof(processInfo);
    processInfo.processAppSpec = &application;

    error = GetCurrentProcess(&process);
    if (error == noErr) {
        error = GetProcessInformation(&process, &processInfo);
    }
    if (error == noErr) {
        error = FSMakeFSSpec(application.vRefNum, application.parID,
                             kDriverName, &driverSource);
    }
    if (error == noErr) {
        error = FSMakeFSSpec(application.vRefNum, application.parID,
                             kStartupName, &startupSource);
    }
    if (error != noErr) {
        GXMetalShowResult(false,
            "GXMetal or GXMetal Startup was not found beside this installer. Keep the complete GXMetal folder together and try again.");
        return 1;
    }
    error = FindFolder(kOnSystemDisk, kExtensionFolderType, false,
                       &extensionsVRef, &extensionsDirID);
    if (error != noErr) {
        GXMetalShowResult(false,
            "The active System Folder's Extensions folder could not be located.");
        return 1;
    }
    installChoice = GXMetalConfirmInstall();
    if (installChoice == 0) {
        GXMetalShowResult(false,
            "The GXMetal installer window could not be created. Restart the Mac and try again.");
        return 1;
    }
    if (installChoice == 2) {
        return 0;
    }
    error = GXMetalInstallFiles(&driverSource, &startupSource,
                                extensionsVRef, extensionsDirID);
    if (error != noErr) {
        GXMetalShowResult(false,
            "GXMetal could not be installed. Make sure the startup disk is writable and has free space, then try again.");
        return 1;
    }
    GXMetalShowResult(true,
        "GXMetal and its startup icon are installed. The previous active copy is disabled and will be removed during restart. Then run GXMetal Test.");
    return 0;
}
