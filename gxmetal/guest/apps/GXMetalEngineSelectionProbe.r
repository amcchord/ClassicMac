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
    'GXMS',
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
    2, 2, release, 4, verUS,
    "2.2.4",
    "GXMetal RAVE Selection 2.2.4"
};

resource 'cfrg' (0) {
    {
        kPowerPCCFragArch, kIsCompleteCFrag, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kApplicationCFrag, kDataForkCFragLocator,
        kZeroOffset, kCFragGoesToEOF,
        "GXMetal RAVE Selection"
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
    768 * 1024,
    768 * 1024
};

resource 'ALRT' (128, purgeable) {
    {42, 48, 220, 432},
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
        {140, 292, 160, 368}, Button { enabled, "OK" };
        {16, 48, 126, 368}, StaticText { disabled, "^0" };
    }
};
