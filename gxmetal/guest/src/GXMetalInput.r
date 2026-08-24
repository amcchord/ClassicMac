#include "CodeFragments.r"
#include "Types.r"

#define GXMETAL_ICON_RESOURCE_ID -16455
#include "GXMetalIcon.r"

resource 'vers' (1) {
    2, 0, release, 4, verUS,
    "2.0.4",
    "GXMetal Input 2.0.4"
};

resource 'cfrg' (0) {
    {
        kPowerPCCFragArch, kIsCompleteCFrag, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kImportLibraryCFrag, kDataForkCFragLocator,
        kZeroOffset, kCFragGoesToEOF,
        "GXMetal Input"
    }
};
