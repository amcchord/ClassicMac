#include "Finder.r"
#include "Retro68.r"
#include "Types.r"

#define GXMETAL_ICON_RESOURCE_ID -16455
#include "GXMetalIcon.r"

type 'INIT' {
    RETRO68_CODE_TYPE
};

resource 'INIT' (128, "GXMetal Startup Icon", locked) {
    dontBreakAtEntry,
    $$read("bin/GXMetalStartupIcon.flt")
};

resource 'vers' (1) {
    2, 3, release, 0, verUS,
    "2.3.0",
    "GXMetal Startup 2.3.0"
};
