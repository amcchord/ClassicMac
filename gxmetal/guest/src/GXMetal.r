#include "CodeFragments.r"
#include "Processes.r"
#include "Types.r"

#define GXMETAL_ICON_RESOURCE_ID -16455
#include "GXMetalIcon.r"

/* Mac OS 9's installed hardware engines are shlb/tnsl shared libraries with
 * all three of these metadata records. The ftag mirrors the system-generated
 * fragment tag: format 2, version 1.4 final, Str29 "GXMetal", engine class 1. */
data 'tnsl' (0) {
    $"0000"
};

data 'ftag' (0) {
    $"0201 4080 0007 4758 4D65 7461 6C00"
    $"0000 0000 0000 0000 0000 0000 0000 0000"
    $"0000 0000 0000 0100"
};

resource 'vers' (1) {
    2, 0, release, 3, verUS,
    "2.0.3",
    "GXMetal 2.0.3"
};

resource 'cfrg' (0) {
    {
        kPowerPCCFragArch, kIsCompleteCFrag, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kImportLibraryCFrag, kDataForkCFragLocator,
        kZeroOffset, kCFragGoesToEOF,
        "GXMetal"
    }
};
