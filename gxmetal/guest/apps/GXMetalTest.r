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
    2, 0, release, 3, verUS,
    "2.0.3",
    "GXMetal Test 2.0.3"
};

resource 'cfrg' (0) {
    {
        kPowerPCCFragArch, kIsCompleteCFrag, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kApplicationCFrag, kDataForkCFragLocator,
        kZeroOffset, kCFragGoesToEOF,
        "GXMetal Test"
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
    512 * 1024,
    512 * 1024
};

resource 'ALRT' (128, purgeable) {
    {42, 48, 228, 432},
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
        {148, 292, 168, 368}, Button { enabled, "OK" };
        {16, 64, 134, 364}, StaticText { disabled, "^0" };
    }
};
