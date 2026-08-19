/* Draw GXMetal's icon in the shared classic-Mac startup extension row. */

#include <Files.h>
#include <Folders.h>
#include <Icons.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <Types.h>

#include "Retro68Runtime.h"

#define GXMETAL_CUSTOM_ICON_ID (-16455)

static const unsigned char kDriverPreviousName[] = {
    16, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'P', 'r', 'e', 'v', 'i', 'o', 'u', 's'
};
static const unsigned char kStartupPreviousName[] = {
    24, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'S', 't', 'a', 'r', 't', 'u', 'p', ' ',
    'P', 'r', 'e', 'v', 'i', 'o', 'u', 's'
};
static const unsigned char kInputPreviousName[] = {
    22, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'I', 'n', 'p', 'u', 't', ' ',
    'P', 'r', 'e', 'v', 'i', 'o', 'u', 's'
};
static const unsigned char kDriverLegacyName[] = {
    21, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'L', 'e', 'g', 'a', 'c', 'y', ' ',
    'B', 'a', 'c', 'k', 'u', 'p'
};
static const unsigned char kStartupLegacyName[] = {
    22, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ',
    'S', 't', 'a', 'r', 't', 'u', 'p', ' ',
    'L', 'e', 'g', 'a', 'c', 'y'
};

/* ShowINIT-compatible shared coordinates in the classic low-memory globals. */
#define GXMETAL_LM_V_CHECKSUM (*(volatile unsigned short *)0x0928)
#define GXMETAL_LM_V_COORD    (*(volatile short *)0x092A)
#define GXMETAL_LM_H_COORD    (*(volatile short *)0x092C)
#define GXMETAL_LM_H_CHECKSUM (*(volatile unsigned short *)0x092E)

typedef struct GXMetalQuickDrawWorld {
    QDGlobals globals;
    long globalsPointer;
} GXMetalQuickDrawWorld;

static unsigned short GXMetalStartupChecksum(short coordinate)
{
    unsigned short value = (unsigned short)coordinate;
    return (unsigned short)(((value << 1) | (value >> 15)) ^ 0x1021U);
}

static void GXMetalRemoveBackup(ConstStr255Param name,
                                short extensionsVRef,
                                long extensionsDirID)
{
    FSSpec backup;

    if (FSMakeFSSpec(extensionsVRef, extensionsDirID,
                     name, &backup) == noErr) {
        (void)FSpDelete(&backup);
    }
}

static void GXMetalRemoveInstallerBackups(void)
{
    short extensionsVRef = 0;
    long extensionsDirID = 0;

    if (FindFolder(kOnSystemDisk, kExtensionFolderType, false,
                   &extensionsVRef, &extensionsDirID) != noErr) {
        return;
    }
    GXMetalRemoveBackup(kDriverPreviousName,
                        extensionsVRef, extensionsDirID);
    GXMetalRemoveBackup(kStartupPreviousName,
                        extensionsVRef, extensionsDirID);
    GXMetalRemoveBackup(kInputPreviousName,
                        extensionsVRef, extensionsDirID);
    GXMetalRemoveBackup(kDriverLegacyName,
                        extensionsVRef, extensionsDirID);
    GXMetalRemoveBackup(kStartupLegacyName,
                        extensionsVRef, extensionsDirID);
}

static void GXMetalStartupRect(Rect *iconRect, const Rect *screen)
{
    short horizontal = 8;
    short vertical = (short)(screen->bottom - 40);

    if (GXMetalStartupChecksum(GXMETAL_LM_H_COORD) ==
        GXMETAL_LM_H_CHECKSUM) {
        horizontal = GXMETAL_LM_H_COORD;
    }
    if (GXMetalStartupChecksum(GXMETAL_LM_V_COORD) ==
        GXMETAL_LM_V_CHECKSUM) {
        vertical = GXMETAL_LM_V_COORD;
    }
    if (horizontal + 34 > screen->right) {
        horizontal = 8;
        vertical = (short)(vertical - 40);
    }
    SetRect(iconRect, horizontal, vertical,
            (short)(horizontal + 32), (short)(vertical + 32));
}

static void GXMetalAdvanceStartupRow(const Rect *iconRect)
{
    GXMETAL_LM_H_COORD = (short)(iconRect->left + 40);
    GXMETAL_LM_V_COORD = iconRect->top;
    GXMETAL_LM_H_CHECKSUM = GXMetalStartupChecksum(GXMETAL_LM_H_COORD);
    GXMETAL_LM_V_CHECKSUM = GXMetalStartupChecksum(GXMETAL_LM_V_COORD);
}

static void GXMetalDrawMonochromeIcon(const Rect *destination)
{
    Handle family = Get1Resource('ICN#', GXMETAL_CUSTOM_ICON_ID);

    if (family != NULL) {
        BitMap source;
        BitMap target;
        GrafPtr port;

        HLock(family);
        source.baseAddr = *family + 128;
        source.rowBytes = 4;
        SetRect(&source.bounds, 0, 0, 32, 32);
        GetPort(&port);
        target = port->portBits;
        CopyBits(&source, &target, &source.bounds, destination, srcBic, NULL);
        source.baseAddr = *family;
        CopyBits(&source, &target, &source.bounds, destination, srcOr, NULL);
        HUnlock(family);
    }
}

static void GXMetalDrawStartupIcon(void)
{
    GXMetalQuickDrawWorld world;
    SysEnvRec environment;
    Rect destination;
    long previousA5;

    previousA5 = SetA5((long)&world.globalsPointer);
    InitGraf(&world.globals.thePort);
    (void)SysEnvirons(curSysEnvVers, &environment);
    GXMetalStartupRect(&destination, &world.globals.screenBits.bounds);

    if (environment.systemVersion >= 0x0700 && environment.hasColorQD) {
        CGrafPort port;
        OpenCPort(&port);
        (void)PlotIconID(&destination, atNone, ttNone,
                         GXMETAL_CUSTOM_ICON_ID);
        CloseCPort(&port);
    } else {
        GrafPort port;
        OpenPort(&port);
        GXMetalDrawMonochromeIcon(&destination);
        ClosePort(&port);
    }
    GXMetalAdvanceStartupRow(&destination);
    (void)SetA5(previousA5);
}

void _start(void)
{
    RETRO68_RELOCATE();
    Retro68CallConstructors();
    GXMetalRemoveInstallerBackups();
    GXMetalDrawStartupIcon();
    Retro68FreeGlobals();
}
