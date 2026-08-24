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
    2, 0, release, 6, verUS,
    "2.0.7",
    "GXMetal Startup 2.0.7"
};
