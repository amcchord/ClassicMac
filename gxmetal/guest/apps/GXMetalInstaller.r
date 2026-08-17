#include "CodeFragments.r"
#include "Dialogs.r"
#include "Finder.r"
#include "Processes.r"
#include "Types.r"

#define GXMETAL_ICON_RESOURCE_ID 128
#include "GXMetalIcon.r"

resource 'FREF' (128, purgeable) {
    'APPL',
    0,
    ""
};

resource 'BNDL' (128, purgeable) {
    'GXMT',
    0,
    {
        'ICN#', {
            0, 128
        },
        'FREF', {
            0, 128
        }
    }
};

resource 'vers' (1) {
    1, 7, beta, 4, verUS,
    "1.7 beta 4",
    "Install GXMetal 1.7 beta 4"
};

resource 'cfrg' (0) {
    {
        kPowerPCCFragArch, kIsCompleteCFrag, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kApplicationCFrag, kDataForkCFragLocator,
        kZeroOffset, kCFragGoesToEOF,
        "Install GXMetal"
    }
};

resource 'SIZE' (-1) {
    reserved,
    ignoreSuspendResumeEvents,
    reserved,
    cannotBackground,
    needsActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    256 * 1024,
    256 * 1024
};

resource 'ALRT' (128, purgeable) {
    {50, 60, 220, 420},
    128,
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent
    },
    centerMainScreen
};

resource 'DITL' (128, purgeable) {
    {
        {132, 270, 152, 346}, Button { enabled, "OK" };
        {16, 64, 118, 342}, StaticText { disabled, "^0" };
    }
};

resource 'DLOG' (129, purgeable) {
    {0, 0, 190, 408},
    dBoxProc,
    invisible,
    noGoAway,
    0x0,
    129,
    "Install GXMetal",
    centerMainScreen
};

resource 'DITL' (129, purgeable) {
    {
        {150, 312, 170, 392}, Button { enabled, "Install" };
        {150, 220, 170, 300}, Button { enabled, "Cancel" };
        {16, 16, 48, 48}, Icon { disabled, 128 };
        {16, 64, 134, 392}, StaticText { disabled,
            "GXMetal adds host-accelerated QuickDraw 3D RAVE to "
            "ClassicMac Power Mac G4 machines. Apple Software RAVE "
            "remains available as a safe fallback.\n\n"
            "Install or update GXMetal and its startup icon in the active "
            "System Folder? You will need to restart before using it."
        };
    }
};
