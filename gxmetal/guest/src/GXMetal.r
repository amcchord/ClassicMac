#include "Processes.r"
#include "CodeFragments.r"
#include "Types.r"

/* Mac OS 9's installed hardware engines are shlb/tnsl shared libraries with
 * all three of these metadata records. The ftag mirrors the system-generated
 * fragment tag: format 2, version 1.1 final, Str29 "GXMetal", engine class 1. */
data 'tnsl' (0) {
    $"0000"
};

data 'ftag' (0) {
    $"0201 1080 0007 4758 4D65 7461 6C00"
    $"0000 0000 0000 0000 0000 0000 0000 0000"
    $"0000 0000 0000 0100"
};

resource 'vers' (1) {
    1, 1, release, 0, verUS,
    "1.1",
    "GXMetal 1.1"
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
