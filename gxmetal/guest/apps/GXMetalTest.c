#include <CodeFragments.h>
#include <Dialogs.h>
#include <Files.h>
#include <Folders.h>
#include <Fonts.h>
#include <NameRegistry.h>
#include <Quickdraw.h>
#include <RAVE.h>
#include <TextEdit.h>
#include <Timer.h>
#include <Windows.h>

#include <string.h>

#include "GXMetalDiagnostics.h"
#include "GXMetalATICompatibility.h"
#include "GXMetalVersion.h"

/* QAInit/QAExit remain exported by the classic RAVE manager and import
 * library, but were omitted from the final Universal Interfaces RAVE.h. RAVE
 * does not scan tnsl engines or build device associations until QAInit. */
extern TQAError QAInit(void);
extern void QAExit(void);

#define GXMETAL_ALERT_ID 128
#define GXMETAL_WIDTH 320
#define GXMETAL_HEIGHT 220
#define GXMETAL_BENCHMARK_FRAMES 120
#define GXMETAL_BENCHMARK_WARMUP_FRAMES 4
#define GXMETAL_ATI_PIXEL_RGB16 ((TQAImagePixelType)1001)
#define GXMETAL_ATI_PREALLOC_TEST_TEXTURES 43

typedef TQAError (*GXMetalATIPrivateMethodProc)(
    unsigned long arg0, unsigned long arg1, unsigned long arg2,
    unsigned long arg3, unsigned long arg4, unsigned long arg5,
    unsigned long arg6, unsigned long arg7);

static const unsigned char kGXMetalResultName[] = {
    20, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'T', 'e', 's', 't', ' ',
    'R', 'e', 's', 'u', 'l', 't', 's'
};
static const unsigned char kGXMetalDiagnosticResultName[] = {
    26, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'D', 'i', 'a', 'g', 'n',
    'o', 's', 't', 'i', 'c', ' ', 'R', 'e', 's', 'u', 'l', 't', 's'
};
static const unsigned char kGXMetalRAVEInventoryResultName[] = {
    22, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'R', 'A', 'V', 'E', ' ',
    'I', 'n', 'v', 'e', 'n', 't', 'o', 'r', 'y'
};
static const unsigned char kGXMetalExtensionName[] = {
    7, 'G', 'X', 'M', 'e', 't', 'a', 'l'
};
static const unsigned char kGXMetalDiagnosticSymbol[] = {
    26, 'G', 'X', 'M', 'e', 't', 'a', 'l', 'G', 'e', 't',
    'D', 'i', 'a', 'g', 'n', 'o', 's', 't', 'i', 'c', 'S', 't', 'a', 't', 'u', 's'
};
static const unsigned char kGXMetalTransportProbeSymbol[] = {
    21, 'G', 'X', 'M', 'e', 't', 'a', 'l', 'P', 'r', 'o', 'b', 'e',
    'T', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't'
};
static const unsigned char kGXMetalCopyDiagnosticsSymbol[] = {
    22, 'G', 'X', 'M', 'e', 't', 'a', 'l', 'C', 'o', 'p', 'y',
    'D', 'i', 'a', 'g', 'n', 'o', 's', 't', 'i', 'c', 's'
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

/* Leave a machine-readable checkpoint in Preferences. Besides making failures
 * diagnosable on a Mac without a debugger, the host validation harness can
 * mount the isolated disk after shutdown and assert the final PASS record. */
static void GXMetalRecordNamedResult(const unsigned char *name,
                                     const char *message)
{
    FSSpec result;
    short volume = 0;
    short refNum = -1;
    long directory = 0;
    long length = (long)strlen(message);

    if (FindFolder(kOnSystemDisk, kPreferencesFolderType, false,
                   &volume, &directory) != noErr) {
        return;
    }
    (void)FSMakeFSSpec(volume, directory, name, &result);
    (void)FSpDelete(&result);
    if (FSpCreate(&result, 'GXMT', 'TEXT', smSystemScript) != noErr ||
        FSpOpenDF(&result, fsWrPerm, &refNum) != noErr) {
        return;
    }
    (void)FSWrite(refNum, &length, message);
    (void)FSClose(refNum);
    (void)FlushVol(NULL, volume);
}

static void GXMetalRecordResult(const char *message)
{
    GXMetalRecordNamedResult(kGXMetalResultName, message);
}

static void GXMetalRecordDiagnosticResult(const char *message)
{
    GXMetalRecordNamedResult(kGXMetalDiagnosticResultName, message);
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

static Boolean GXMetalGetEngineName(const TQAEngine *engine,
                                    char *name, size_t capacity)
{
    unsigned long length = 0;

    if (engine == NULL || name == NULL || capacity == 0) {
        return false;
    }
    memset(name, 0, capacity);
    return QAEngineGestalt(engine, kQAGestalt_ASCIINameLength,
                           &length) == kQANoErr &&
           length < capacity &&
           QAEngineGestalt(engine, kQAGestalt_ASCIIName,
                           name) == kQANoErr;
}

static TQAEngine *GXMetalFindEngine(const TQADevice *device)
{
    TQAEngine *engine = QADeviceGetFirstEngine(device);

    while (engine != NULL) {
        char name[64];

        if (GXMetalGetEngineName(engine, name, sizeof(name)) &&
            strcmp(name, "GXMetal") == 0) {
            return engine;
        }
        engine = QADeviceGetNextEngine(device, engine);
    }
    return NULL;
}

/* The normal path is RAVE's boot-time discovery. When that path fails, an
 * explicit CFM load tells the validation app whether the fragment itself is
 * invalid or whether its initializer registered an engine which RAVE's scan
 * overlooked. A manual load is diagnostic only and still fails the test. */
static OSErr GXMetalLoadInstalledExtension(CFragConnectionID *connection)
{
    FSSpec extension;
    short volume = 0;
    long directory = 0;
    Str255 errorMessage;
    OSErr error;

    error = FindFolder(kOnSystemDisk, kExtensionFolderType, false,
                       &volume, &directory);
    if (error != noErr) {
        return error;
    }
    error = FSMakeFSSpec(volume, directory, kGXMetalExtensionName,
                         &extension);
    if (error != noErr) {
        return error;
    }
    return GetDiskFragment(&extension, 0, kCFragGoesToEOF, NULL,
                           kLoadCFrag, connection, NULL, errorMessage);
}

static int32_t GXMetalReadDriverDiagnostic(CFragConnectionID connection)
{
    typedef int32_t (*GXMetalDiagnosticProc)(void);
    CFragSymbolClass symbolClass;
    Ptr symbol = NULL;

    if (FindSymbol(connection, kGXMetalDiagnosticSymbol,
                   &symbol, &symbolClass) != noErr ||
        symbol == NULL || symbolClass != kTVectorCFragSymbol) {
        return -1;
    }
    return ((GXMetalDiagnosticProc)symbol)();
}

static int32_t GXMetalProbeDriverTransport(CFragConnectionID connection)
{
    typedef int32_t (*GXMetalTransportProbeProc)(void);
    CFragSymbolClass symbolClass;
    Ptr symbol = NULL;

    if (FindSymbol(connection, kGXMetalTransportProbeSymbol,
                   &symbol, &symbolClass) != noErr ||
        symbol == NULL || symbolClass != kTVectorCFragSymbol) {
        return -1;
    }
    GXMetalRecordResult("PROBE: entering GXMetal Name Registry and transport discovery");
    return ((GXMetalTransportProbeProc)symbol)();
}

static int32_t GXMetalCopyDriverDiagnostics(
    CFragConnectionID connection, GXMetalDiagnosticSnapshot *snapshot)
{
    typedef int32_t (*GXMetalCopyDiagnosticsProc)(
        GXMetalDiagnosticSnapshot *, uint32_t);
    CFragSymbolClass symbolClass;
    Ptr symbol = NULL;

    if (FindSymbol(connection, kGXMetalCopyDiagnosticsSymbol,
                   &symbol, &symbolClass) != noErr ||
        symbol == NULL || symbolClass != kTVectorCFragSymbol) {
        return -1;
    }
    return ((GXMetalCopyDiagnosticsProc)symbol)(snapshot, sizeof(*snapshot));
}

static Boolean GXMetalReadPublishedDiagnostics(
    GXMetalDiagnosticSnapshot *snapshot)
{
    RegEntryIter iterator;
    RegEntryID entry;
    RegPropertyValueSize size = sizeof(*snapshot);
    Boolean done = true;
    OSStatus status;

    status = RegistryEntryIterateCreate(&iterator);
    if (status != noErr) {
        return false;
    }
    status = RegistryEntrySearch(&iterator, kRegIterSubTrees, &entry, &done,
                                 GXMETAL_DIAGNOSTIC_PROPERTY, NULL, 0);
    RegistryEntryIterateDispose(&iterator);
    if (status != noErr || done) {
        return false;
    }
    status = RegistryPropertyGet(&entry, GXMETAL_DIAGNOSTIC_PROPERTY,
                                 snapshot, &size);
    return status == noErr && size == sizeof(*snapshot) &&
           snapshot->magic == GXMETAL_DIAGNOSTIC_MAGIC;
}

static void GXMetalAppendText(char **cursor, const char *end,
                              const char *text)
{
    while (*text != '\0' && *cursor < end) {
        *(*cursor)++ = *text++;
    }
}

static void GXMetalAppendHex(char **cursor, const char *end, uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    for (shift = 28; shift >= 0 && *cursor < end; shift -= 4) {
        *(*cursor)++ = digits[(value >> shift) & 0xf];
    }
}

static void GXMetalAppendDecimal(char **cursor, const char *end,
                                 uint64_t value)
{
    char digits[24];
    int count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < (int)sizeof(digits));
    while (count > 0 && *cursor < end) {
        *(*cursor)++ = digits[--count];
    }
}

static void GXMetalAppendVersion(char **cursor, const char *end,
                                 uint32_t revision)
{
    GXMetalAppendDecimal(cursor, end, (revision >> 16) & 0xffu);
    GXMetalAppendText(cursor, end, ".");
    GXMetalAppendDecimal(cursor, end, (revision >> 8) & 0xffu);
    GXMetalAppendText(cursor, end, ".");
    GXMetalAppendDecimal(cursor, end, revision & 0xffu);
}

/* Record the order RAVE presents its engines for the main display. Legacy
 * games commonly select the first engine advertising textured drawing, so a
 * complete inventory makes a silent software/hardware selection observable
 * without changing the preferred-engine state. */
static void GXMetalRecordRAVEInventory(const TQADevice *device)
{
    static char result[2048];
    char *cursor = result;
    const char *end = result + sizeof(result) - 1;
    TQAEngine *engine;
    unsigned long index = 0;

    GXMetalAppendText(&cursor, end, "RAVE engine inventory for main display");
    engine = QADeviceGetFirstEngine(device);
    while (engine != NULL) {
        char name[64];
        unsigned long vendorID = 0;
        unsigned long engineID = 0;
        unsigned long revision = 0;
        unsigned long optionalFeatures = 0;
        unsigned long fastFeatures = 0;

        GXMetalAppendText(&cursor, end, "\norder=");
        GXMetalAppendDecimal(&cursor, end, index);
        GXMetalAppendText(&cursor, end, " name=");
        if (GXMetalGetEngineName(engine, name, sizeof(name))) {
            GXMetalAppendText(&cursor, end, name);
        } else {
            GXMetalAppendText(&cursor, end, "<unknown>");
        }
        GXMetalAppendText(&cursor, end, " vendor=");
        if (QAEngineGestalt(engine, kQAGestalt_VendorID,
                            &vendorID) == kQANoErr) {
            GXMetalAppendHex(&cursor, end, vendorID);
        } else {
            GXMetalAppendText(&cursor, end, "<unknown>");
        }
        GXMetalAppendText(&cursor, end, " engine=");
        if (QAEngineGestalt(engine, kQAGestalt_EngineID,
                            &engineID) == kQANoErr) {
            GXMetalAppendHex(&cursor, end, engineID);
        } else {
            GXMetalAppendText(&cursor, end, "<unknown>");
        }
        GXMetalAppendText(&cursor, end, " revision=");
        if (QAEngineGestalt(engine, kQAGestalt_Revision,
                            &revision) == kQANoErr) {
            GXMetalAppendVersion(&cursor, end, revision);
        } else {
            GXMetalAppendText(&cursor, end, "<unknown>");
        }
        GXMetalAppendText(&cursor, end, " optional=");
        if (QAEngineGestalt(engine, kQAGestalt_OptionalFeatures,
                            &optionalFeatures) == kQANoErr) {
            GXMetalAppendHex(&cursor, end, optionalFeatures);
        } else {
            GXMetalAppendText(&cursor, end, "<unknown>");
        }
        GXMetalAppendText(&cursor, end, " fast=");
        if (QAEngineGestalt(engine, kQAGestalt_FastFeatures,
                            &fastFeatures) == kQANoErr) {
            GXMetalAppendHex(&cursor, end, fastFeatures);
        } else {
            GXMetalAppendText(&cursor, end, "<unknown>");
        }
        index++;
        engine = QADeviceGetNextEngine(device, engine);
    }
    GXMetalAppendText(&cursor, end, "\ncount=");
    GXMetalAppendDecimal(&cursor, end, index);
    *cursor = '\0';
    GXMetalRecordNamedResult(kGXMetalRAVEInventoryResultName, result);
}

static void GXMetalRecordDiagnosticSnapshot(
    const GXMetalDiagnosticSnapshot *snapshot, int32_t probeStatus,
    Boolean automaticLoad)
{
    char result[512];
    char *cursor = result;
    const char *end = result + sizeof(result) - 1;

#define GXMETAL_DIAGNOSTIC_FIELD(label, field) do { \
    GXMetalAppendText(&cursor, end, label); \
    GXMetalAppendHex(&cursor, end, (uint32_t)(field)); \
} while (0)
    GXMetalAppendText(&cursor, end, "FAIL: diag ");
    GXMETAL_DIAGNOSTIC_FIELD("auto=", automaticLoad);
    GXMETAL_DIAGNOSTIC_FIELD("st=", snapshot->status);
    GXMETAL_DIAGNOSTIC_FIELD(" reg=", snapshot->registration_error);
    GXMETAL_DIAGNOSTIC_FIELD(" init=", snapshot->initialize_count);
    GXMETAL_DIAGNOSTIC_FIELD(" check=", snapshot->check_device_count);
    GXMETAL_DIAGNOSTIC_FIELD(" gm=", snapshot->get_method_count);
    GXMETAL_DIAGNOSTIC_FIELD(" mask=", snapshot->method_mask);
    GXMETAL_DIAGNOSTIC_FIELD(" gest=", snapshot->gestalt_count);
    GXMETAL_DIAGNOSTIC_FIELD(" sel=", snapshot->last_gestalt_selector);
    GXMETAL_DIAGNOSTIC_FIELD(" reject=", snapshot->display_reject_reason);
    GXMETAL_DIAGNOSTIC_FIELD(" ctxn=", snapshot->draw_private_new_count);
    GXMETAL_DIAGNOSTIC_FIELD(" ctxok=",
                             snapshot->draw_private_new_success_count);
    GXMETAL_DIAGNOSTIC_FIELD(" ctxdel=",
                             snapshot->draw_private_delete_count);
    GXMETAL_DIAGNOSTIC_FIELD(" flags=", snapshot->context_flags);
    GXMETAL_DIAGNOSTIC_FIELD(" err=", snapshot->context_error);
    GXMETAL_DIAGNOSTIC_FIELD(" cw=", snapshot->context_width);
    GXMETAL_DIAGNOSTIC_FIELD(" ch=", snapshot->context_height);
    GXMETAL_DIAGNOSTIC_FIELD(" crow=", snapshot->context_row_bytes);
    GXMETAL_DIAGNOSTIC_FIELD(" fmt=", snapshot->context_pixel_format);
    GXMETAL_DIAGNOSTIC_FIELD(" off=", snapshot->context_framebuffer_offset);
    GXMETAL_DIAGNOSTIC_FIELD(" rs=", snapshot->resource_stage);
    GXMETAL_DIAGNOSTIC_FIELD(" tn=", snapshot->texture_new_count);
    GXMETAL_DIAGNOSTIC_FIELD(" td=", snapshot->texture_delete_count);
    GXMETAL_DIAGNOSTIC_FIELD(" ct=", snapshot->color_table_new_count);
    GXMETAL_DIAGNOSTIC_FIELD(" cd=", snapshot->color_table_delete_count);
    GXMETAL_DIAGNOSTIC_FIELD(" tb=",
                             snapshot->texture_bind_color_table_count);
    GXMETAL_DIAGNOSTIC_FIELD(" tf=", snapshot->last_texture_flags);
    GXMETAL_DIAGNOSTIC_FIELD(" tp=", snapshot->last_texture_pixel_type);
    GXMETAL_DIAGNOSTIC_FIELD(" tw=", snapshot->last_texture_width);
    GXMETAL_DIAGNOSTIC_FIELD(" th=", snapshot->last_texture_height);
    GXMETAL_DIAGNOSTIC_FIELD(" tl=", snapshot->last_texture_levels);
    GXMETAL_DIAGNOSTIC_FIELD(" te=",
                             (uint32_t)snapshot->last_texture_error);
    GXMETAL_DIAGNOSTIC_FIELD(" cty=", snapshot->last_color_table_type);
    GXMETAL_DIAGNOSTIC_FIELD(" ctr=",
                             snapshot->last_color_table_transparent);
    GXMETAL_DIAGNOSTIC_FIELD(" cte=",
                             (uint32_t)snapshot->last_color_table_error);
    GXMETAL_DIAGNOSTIC_FIELD(" tbe=",
                             (uint32_t)snapshot->last_texture_bind_error);
    GXMETAL_DIAGNOSTIC_FIELD(" fb=", snapshot->registry_framebuffer_address);
    if (snapshot->check_device_count != 0) {
        GXMETAL_DIAGNOSTIC_FIELD(" dev=", snapshot->device_type);
        GXMETAL_DIAGNOSTIC_FIELD(" base=", snapshot->base_address);
        GXMETAL_DIAGNOSTIC_FIELD(" row=", snapshot->row_bytes);
        GXMETAL_DIAGNOSTIC_FIELD(" pix=", snapshot->pixel_size);
        GXMETAL_DIAGNOSTIC_FIELD(" target=", snapshot->target_address);
        GXMETAL_DIAGNOSTIC_FIELD(" end=", snapshot->target_end);
    }
    GXMETAL_DIAGNOSTIC_FIELD(" probe=", probeStatus);
#undef GXMETAL_DIAGNOSTIC_FIELD
    *cursor = '\0';
    GXMetalRecordResult(result);
    GXMetalRecordDiagnosticResult(result);
}

static TQAVGouraud GXMetalGouraud(float x, float y, float z,
                                  float red, float green, float blue,
                                  float alpha)
{
    TQAVGouraud vertex;

    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.invW = 1.0f;
    vertex.r = red;
    vertex.g = green;
    vertex.b = blue;
    vertex.a = alpha;
    return vertex;
}

static TQAVTexture GXMetalTextureVertex(float x, float y, float z,
                                        float u, float v)
{
    TQAVTexture vertex;

    memset(&vertex, 0, sizeof(vertex));
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.invW = 1.0f;
    vertex.a = 1.0f;
    vertex.uOverW = u;
    vertex.vOverW = v;
    vertex.kd_r = 1.0f;
    vertex.kd_g = 1.0f;
    vertex.kd_b = 1.0f;
    return vertex;
}

enum GXMetalExpectedPixel {
    kGXMetalPixelRed,
    kGXMetalPixelBlue,
    kGXMetalPixelPurple,
    kGXMetalPixelGreen,
    kGXMetalPixelGray,
    kGXMetalPixelLightBlue,
    kGXMetalPixelWhite,
    kGXMetalPixelFogPurple
};

static Boolean GXMetalPixelMatches(GDHandle graphicsDevice,
                                   long x, long y,
                                   enum GXMetalExpectedPixel expected)
{
    PixMapHandle pixmap;
    volatile unsigned char *pixel;
    long rowBytes;
    long red;
    long green;
    long blue;
    long maximum;

    if (graphicsDevice == NULL || *graphicsDevice == NULL) {
        return false;
    }
    pixmap = (**graphicsDevice).gdPMap;
    if (pixmap == NULL || *pixmap == NULL ||
        x < (**pixmap).bounds.left || x >= (**pixmap).bounds.right ||
        y < (**pixmap).bounds.top || y >= (**pixmap).bounds.bottom) {
        return false;
    }
    rowBytes = (**pixmap).rowBytes & 0x3fff;
    pixel = (volatile unsigned char *)GetPixBaseAddr(pixmap) +
            (y - (**pixmap).bounds.top) * rowBytes;
    if ((**pixmap).pixelSize == 16) {
        unsigned short value;

        pixel += (x - (**pixmap).bounds.left) * 2;
        value = (unsigned short)(((unsigned short)pixel[0] << 8) |
                                 (unsigned short)pixel[1]);
        red = (value >> 10) & 31;
        green = (value >> 5) & 31;
        blue = value & 31;
        maximum = 31;
    } else if ((**pixmap).pixelSize == 32) {
        pixel += (x - (**pixmap).bounds.left) * 4;
        red = pixel[1];
        green = pixel[2];
        blue = pixel[3];
        maximum = 255;
    } else {
        return false;
    }

    if (expected == kGXMetalPixelRed) {
        return red > maximum * 2 / 3 &&
               green < maximum / 3 && blue < maximum / 3;
    }
    if (expected == kGXMetalPixelBlue) {
        return blue > maximum * 2 / 3 &&
               red < maximum / 3 && green < maximum / 3;
    }
    if (expected == kGXMetalPixelPurple) {
        return red > maximum / 3 && blue > maximum / 3 &&
               green < maximum / 3;
    }
    if (expected == kGXMetalPixelGreen) {
        return green > maximum * 2 / 3 &&
               red < maximum / 3 && blue < maximum / 3;
    }
    if (expected == kGXMetalPixelGray) {
        return red > maximum / 3 && red < maximum * 4 / 5 &&
               green > maximum / 3 && green < maximum * 4 / 5 &&
               blue > maximum / 3 && blue < maximum * 4 / 5 &&
               red - green < maximum / 8 &&
               green - red < maximum / 8 &&
               red - blue < maximum / 8 &&
               blue - red < maximum / 8;
    }
    if (expected == kGXMetalPixelLightBlue) {
        return red > maximum / 2 && red < maximum * 9 / 10 &&
               green > maximum / 2 && green < maximum * 9 / 10 &&
               blue > maximum * 9 / 10;
    }
    if (expected == kGXMetalPixelWhite) {
        return red > maximum * 4 / 5 &&
               green > maximum * 4 / 5 && blue > maximum * 4 / 5;
    }
    /* The display driver's programmable gamma table is applied before the
     * test reads VRAM.  The default Mac OS 9 table lifts the linear 25% red
     * component to roughly 39%, which made the old absolute upper bound fail
     * even though the fog result was correct.  Preserve the meaningful
     * checks: both components are present, blue is dominant, and green stays
     * absent. */
    return red > maximum / 6 && red < blue &&
           blue > maximum * 3 / 5 && green < maximum / 5;
}

static Boolean GXMetalAccessPixelMatches(
    const TQAPixelBuffer *buffer, long x, long y,
    enum GXMetalExpectedPixel expected)
{
    const unsigned char *pixel;
    long red;
    long green;
    long blue;
    long maximum;

    if (buffer == NULL || buffer->baseAddr == NULL || x < 0 || y < 0 ||
        x >= buffer->width || y >= buffer->height || buffer->rowBytes <= 0) {
        return false;
    }
    pixel = (const unsigned char *)buffer->baseAddr + y * buffer->rowBytes;
    if (buffer->pixelType == kQAPixel_RGB16) {
        unsigned short value;

        pixel += x * 2;
        value = (unsigned short)(((unsigned short)pixel[0] << 8) |
                                 (unsigned short)pixel[1]);
        red = (value >> 10) & 31;
        green = (value >> 5) & 31;
        blue = value & 31;
        maximum = 31;
    } else if (buffer->pixelType == kQAPixel_RGB32 ||
               buffer->pixelType == kQAPixel_ARGB32) {
        pixel += x * 4;
        red = pixel[1];
        green = pixel[2];
        blue = pixel[3];
        maximum = 255;
    } else {
        return false;
    }
    if (expected == kGXMetalPixelRed) {
        return red > maximum * 2 / 3 && green < maximum / 3 &&
               blue < maximum / 3;
    }
    if (expected == kGXMetalPixelBlue) {
        return blue > maximum * 2 / 3 && red < maximum / 3 &&
               green < maximum / 3;
    }
    return false;
}

static TQAError GXMetalRenderDrawBufferWriteback(
    TQADrawContext *context, GDHandle graphicsDevice,
    const TQARect *deviceRect)
{
    TQAPixelBuffer access;
    TQADevice privateDevice;
    GXMetalATIPrivateMethodProc *privateMethods;
    TQARect full = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQARect dirty = {32, 40, 32, 40};
    TQARect noDirty = {0, 0, 0, 0};
    TQAError error;
    long x;
    long y;

    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 1.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    error = QAClearDrawBuffer(context, &full, NULL);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    memset(&access, 0, sizeof(access));
    if (error == kQANoErr) {
        error = QAAccessDrawBuffer(context, &access);
    }
    if (error == kQANoErr &&
        (access.baseAddr == NULL || access.width != GXMETAL_WIDTH ||
         access.height != GXMETAL_HEIGHT || access.rowBytes <= 0 ||
         (access.pixelType != kQAPixel_RGB16 &&
          access.pixelType != kQAPixel_RGB32 &&
          access.pixelType != kQAPixel_ARGB32))) {
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalAccessPixelMatches(&access, 10, 20,
                                   kGXMetalPixelBlue)) {
        error = kQAError;
    }
    if (error == kQANoErr) {
        for (y = dirty.top; y < dirty.bottom; y++) {
            unsigned char *row = (unsigned char *)access.baseAddr +
                y * access.rowBytes;

            for (x = dirty.left; x < dirty.right; x++) {
                unsigned char *pixel;

                if (access.pixelType == kQAPixel_RGB16) {
                    pixel = row + x * 2;
                    pixel[0] = 0x7c;
                    pixel[1] = 0x00;
                } else {
                    pixel = row + x * 4;
                    pixel[0] = access.pixelType == kQAPixel_ARGB32 ?
                        0xff : 0x00;
                    pixel[1] = 0xff;
                    pixel[2] = 0x00;
                    pixel[3] = 0x00;
                }
            }
        }
        error = QAAccessDrawBufferEnd(context, &dirty);
        access.baseAddr = NULL;
    } else if (access.baseAddr != NULL) {
        (void)QAAccessDrawBufferEnd(context, &noDirty);
        access.baseAddr = NULL;
    }
    if (error == kQANoErr) {
        error = QASync(context);
    }

    /* Read the target back through the same API before presenting. This
     * catches a host that accepts AccessDrawBufferEnd but drops its writes. */
    memset(&access, 0, sizeof(access));
    if (error == kQANoErr) {
        error = QAAccessDrawBuffer(context, &access);
    }
    if (error == kQANoErr &&
        (!GXMetalAccessPixelMatches(&access, 35, 35, kGXMetalPixelRed) ||
         !GXMetalAccessPixelMatches(&access, 10, 20, kGXMetalPixelBlue))) {
        error = kQAError;
    }
    if (access.baseAddr != NULL) {
        TQAError endError = QAAccessDrawBufferEnd(context, &noDirty);

        if (error == kQANoErr) {
            error = endError;
        }
    }

    /* ATI private slot 4 predates public QAAccessDrawBuffer and returns the
     * same memory descriptor inside a full TQADevice. Myth II calls it more
     * than once without an End method, so two consecutive reads must succeed
     * and a subsequent public access must prove the private path left no
     * access latch set. */
    privateMethods = (GXMetalATIPrivateMethodProc *)QAGetPtr(
        context, (TQATagPtr)GXMETAL_ATI_PRIVATE_METHODS_TAG);
    if (error == kQANoErr &&
        (privateMethods == NULL || privateMethods[4] == NULL)) {
        error = kQANotSupported;
    }
    memset(&privateDevice, 0xa5, sizeof(privateDevice));
    if (error == kQANoErr) {
        error = privateMethods[4](
            (unsigned long)(uintptr_t)context,
            (unsigned long)(uintptr_t)&privateDevice, 0, 0, 0, 0, 0, 0);
    }
    if (error == kQANoErr &&
        (privateDevice.deviceType != kQADeviceMemory ||
         privateDevice.device.memoryDevice.baseAddr == NULL ||
         privateDevice.device.memoryDevice.width != GXMETAL_WIDTH ||
         privateDevice.device.memoryDevice.height != GXMETAL_HEIGHT ||
         privateDevice.device.memoryDevice.rowBytes <= 0 ||
         !GXMetalAccessPixelMatches(
             &privateDevice.device.memoryDevice, 35, 35,
             kGXMetalPixelRed) ||
         !GXMetalAccessPixelMatches(
             &privateDevice.device.memoryDevice, 10, 20,
             kGXMetalPixelBlue))) {
        error = kQAError;
    }
    memset(&privateDevice, 0x5a, sizeof(privateDevice));
    if (error == kQANoErr) {
        error = privateMethods[4](
            (unsigned long)(uintptr_t)context,
            (unsigned long)(uintptr_t)&privateDevice, 0, 0, 0, 0, 0, 0);
    }
    if (error == kQANoErr &&
        (privateDevice.deviceType != kQADeviceMemory ||
         !GXMetalAccessPixelMatches(
             &privateDevice.device.memoryDevice, 35, 35,
             kGXMetalPixelRed))) {
        error = kQAError;
    }
    memset(&access, 0, sizeof(access));
    if (error == kQANoErr) {
        error = QAAccessDrawBuffer(context, &access);
    }
    if (access.baseAddr != NULL) {
        TQAError endError = QAAccessDrawBufferEnd(context, &noDirty);

        if (error == kQANoErr) {
            error = endError;
        }
    }
    if (error == kQANoErr) {
        error = QASwapBuffers(context, &full);
    }
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        (!GXMetalPixelMatches(graphicsDevice,
                              deviceRect->left + 35,
                              deviceRect->top + 35,
                              kGXMetalPixelRed) ||
         !GXMetalPixelMatches(graphicsDevice,
                              deviceRect->left + 10,
                              deviceRect->top + 20,
                              kGXMetalPixelBlue))) {
        error = kQAError;
    }
    if (error != kQANoErr) {
        GXMetalRecordResult(
            "FAIL: mutable or ATI-private draw-buffer readback");
    }
    return error;
}

static TQAError GXMetalRenderPattern(TQADrawContext *context,
                                     const TQAEngine *engine,
                                     GDHandle graphicsDevice,
                                     const TQARect *deviceRect)
{
    static unsigned char texturePixels[4] = {
        1, 2,
        3, 4
    };
    static unsigned long texturePalette[256] = {
        [1] = 0x00ff0000UL,
        [2] = 0x0000ff00UL,
        [3] = 0x000000ffUL,
        [4] = 0x00ffffffUL
    };
    static unsigned long bitmapPixels[16] = {
        0xffff0000UL, 0xffff0000UL, 0xffff0000UL, 0xffff0000UL,
        0xffff0000UL, 0xffff0000UL, 0xffff0000UL, 0xffff0000UL,
        0xff00ff00UL, 0xff00ff00UL, 0xff00ff00UL, 0xff00ff00UL,
        0xff00ff00UL, 0xff00ff00UL, 0xff00ff00UL, 0xff00ff00UL
    };
    static unsigned long chromakeyPixels[1] = {0xff0000ffUL};
    TQAImage image;
    TQAImage bitmapImage;
    TQAImage chromakeyImage;
    TQATexture *texture = NULL;
    TQATexture *chromakeyResource = NULL;
    TQAColorTable *colorTable = NULL;
    TQABitmap *bitmap = NULL;
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAVGouraud farTriangle[3];
    TQAVGouraud nearTriangle[3];
    TQAVGouraud blendBase[3];
    TQAVGouraud blendOverlay[3];
    TQAVGouraud alphaBase[3];
    TQAVGouraud alphaRejected[3];
    TQAVGouraud backfaceGouraudBase[3];
    TQAVGouraud backfaceGouraudOriented[3];
    TQAVGouraud backfaceTextureBase[3];
    TQAVGouraud perspectiveFar[3];
    TQAVGouraud perspectiveNear[3];
    TQAVGouraud fogTriangle[3];
    TQAVGouraud chromakeyBase[3];
    TQAVGouraud bitmapVertex;
    TQAVTexture backfaceTextureOriented[3];
    TQAVTexture chromakeyTexture[3];
    TQAVTexture texturedQuad[4];
    unsigned long backfaceFlags[1] = {kQATriFlags_Backfacing};
    unsigned long flags[4] = {0, 0, 0, 0};
    TQAError error;

    image.width = 2;
    image.height = 2;
    image.rowBytes = 2;
    image.pixmap = texturePixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_CL8,
                         &image, &texture);
    if (error == kQANoErr) {
        error = QAColorTableNew(engine, kQAColorTable_CL8_RGB32,
                                texturePalette, false, &colorTable);
    }
    if (error == kQANoErr) {
        error = QATextureBindColorTable(engine, texture, colorTable);
    }
    chromakeyImage.width = 1;
    chromakeyImage.height = 1;
    chromakeyImage.rowBytes = 4;
    chromakeyImage.pixmap = chromakeyPixels;
    if (error == kQANoErr) {
        error = QATextureNew(engine, kQATexture_None, kQAPixel_ARGB32,
                             &chromakeyImage, &chromakeyResource);
    }
    if (error != kQANoErr) {
        if (chromakeyResource != NULL) {
            QATextureDelete(engine, chromakeyResource);
        }
        if (colorTable != NULL) {
            QAColorTableDelete(engine, colorTable);
        }
        if (texture != NULL) {
            QATextureDelete(engine, texture);
        }
        return error;
    }
    bitmapImage.width = 4;
    bitmapImage.height = 4;
    bitmapImage.rowBytes = 16;
    bitmapImage.pixmap = bitmapPixels;
    error = QABitmapNew(engine, kQABitmap_None, kQAPixel_ARGB32,
                        &bitmapImage, &bitmap);
    if (error == kQANoErr) {
        error = QABitmapDetach(engine, bitmap);
    }
    if (error != kQANoErr) {
        if (bitmap != NULL) {
            QABitmapDelete(engine, bitmap);
        }
        QATextureDelete(engine, chromakeyResource);
        QAColorTableDelete(engine, colorTable);
        QATextureDelete(engine, texture);
        return error;
    }

    QASetFloat(context, kQATag_ColorBG_r, 0.03f);
    QASetFloat(context, kQATag_ColorBG_g, 0.03f);
    QASetFloat(context, kQATag_ColorBG_b, 0.08f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_LT);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Enable);

    farTriangle[0] = GXMetalGouraud(10.0f, 205.0f, 0.80f,
                                    0.0f, 0.15f, 1.0f, 1.0f);
    farTriangle[1] = GXMetalGouraud(82.0f, 18.0f, 0.80f,
                                    0.0f, 0.15f, 1.0f, 1.0f);
    farTriangle[2] = GXMetalGouraud(154.0f, 205.0f, 0.80f,
                                    0.0f, 0.15f, 1.0f, 1.0f);
    nearTriangle[0] = GXMetalGouraud(35.0f, 170.0f, 0.20f,
                                     1.0f, 0.08f, 0.03f, 1.0f);
    nearTriangle[1] = GXMetalGouraud(82.0f, 65.0f, 0.20f,
                                     1.0f, 0.08f, 0.03f, 1.0f);
    nearTriangle[2] = GXMetalGouraud(129.0f, 170.0f, 0.20f,
                                     1.0f, 0.08f, 0.03f, 1.0f);
    blendBase[0] = GXMetalGouraud(130.0f, 208.0f, 0.40f,
                                  0.0f, 0.0f, 1.0f, 1.0f);
    blendBase[1] = GXMetalGouraud(160.0f, 150.0f, 0.40f,
                                  0.0f, 0.0f, 1.0f, 1.0f);
    blendBase[2] = GXMetalGouraud(190.0f, 208.0f, 0.40f,
                                  0.0f, 0.0f, 1.0f, 1.0f);
    blendOverlay[0] = GXMetalGouraud(130.0f, 208.0f, 0.10f,
                                     1.0f, 0.0f, 0.0f, 0.5f);
    blendOverlay[1] = GXMetalGouraud(160.0f, 150.0f, 0.10f,
                                     1.0f, 0.0f, 0.0f, 0.5f);
    blendOverlay[2] = GXMetalGouraud(190.0f, 208.0f, 0.10f,
                                     1.0f, 0.0f, 0.0f, 0.5f);
    alphaBase[0] = GXMetalGouraud(280.0f, 215.0f, 0.40f,
                                  0.0f, 1.0f, 0.0f, 1.0f);
    alphaBase[1] = GXMetalGouraud(295.0f, 195.0f, 0.40f,
                                  0.0f, 1.0f, 0.0f, 1.0f);
    alphaBase[2] = GXMetalGouraud(310.0f, 215.0f, 0.40f,
                                  0.0f, 1.0f, 0.0f, 1.0f);
    alphaRejected[0] = GXMetalGouraud(280.0f, 215.0f, 0.10f,
                                      1.0f, 0.0f, 0.0f, 0.25f);
    alphaRejected[1] = GXMetalGouraud(295.0f, 195.0f, 0.10f,
                                      1.0f, 0.0f, 0.0f, 0.25f);
    alphaRejected[2] = GXMetalGouraud(310.0f, 215.0f, 0.10f,
                                      1.0f, 0.0f, 0.0f, 0.25f);
    backfaceGouraudBase[0] = GXMetalGouraud(5.0f, 55.0f, 0.40f,
                                            0.0f, 0.0f, 1.0f, 1.0f);
    backfaceGouraudBase[1] = GXMetalGouraud(30.0f, 5.0f, 0.40f,
                                            0.0f, 0.0f, 1.0f, 1.0f);
    backfaceGouraudBase[2] = GXMetalGouraud(55.0f, 55.0f, 0.40f,
                                            0.0f, 0.0f, 1.0f, 1.0f);
    backfaceGouraudOriented[0] = GXMetalGouraud(5.0f, 55.0f, 0.10f,
                                                1.0f, 0.0f, 0.0f, 1.0f);
    backfaceGouraudOriented[1] = GXMetalGouraud(30.0f, 5.0f, 0.10f,
                                                1.0f, 0.0f, 0.0f, 1.0f);
    backfaceGouraudOriented[2] = GXMetalGouraud(55.0f, 55.0f, 0.10f,
                                                1.0f, 0.0f, 0.0f, 1.0f);
    backfaceTextureBase[0] = GXMetalGouraud(115.0f, 55.0f, 0.40f,
                                            0.0f, 0.0f, 1.0f, 1.0f);
    backfaceTextureBase[1] = GXMetalGouraud(140.0f, 5.0f, 0.40f,
                                            0.0f, 0.0f, 1.0f, 1.0f);
    backfaceTextureBase[2] = GXMetalGouraud(165.0f, 55.0f, 0.40f,
                                            0.0f, 0.0f, 1.0f, 1.0f);
    backfaceTextureOriented[0] = GXMetalTextureVertex(
        115.0f, 55.0f, 0.10f, 0.0f, 1.0f);
    backfaceTextureOriented[1] = GXMetalTextureVertex(
        140.0f, 5.0f, 0.10f, 0.0f, 1.0f);
    backfaceTextureOriented[2] = GXMetalTextureVertex(
        165.0f, 55.0f, 0.10f, 0.0f, 1.0f);
    perspectiveFar[0] = GXMetalGouraud(60.0f, 55.0f, 0.95f,
                                       0.0f, 0.0f, 1.0f, 1.0f);
    perspectiveFar[1] = GXMetalGouraud(85.0f, 5.0f, 0.95f,
                                       0.0f, 0.0f, 1.0f, 1.0f);
    perspectiveFar[2] = GXMetalGouraud(110.0f, 55.0f, 0.95f,
                                       0.0f, 0.0f, 1.0f, 1.0f);
    perspectiveNear[0] = GXMetalGouraud(60.0f, 55.0f, 0.95f,
                                        1.0f, 0.0f, 0.0f, 1.0f);
    perspectiveNear[1] = GXMetalGouraud(85.0f, 5.0f, 0.95f,
                                        1.0f, 0.0f, 0.0f, 1.0f);
    perspectiveNear[2] = GXMetalGouraud(110.0f, 55.0f, 0.95f,
                                        1.0f, 0.0f, 0.0f, 1.0f);
    /* Weekend Warrior uses reciprocal-W values above one for near menu
     * meshes.  Keep both surfaces in that range so the guest test catches a
     * Perspective-Z implementation that saturates them onto one depth. */
    perspectiveFar[0].invW = perspectiveFar[1].invW =
        perspectiveFar[2].invW = 2.0f;
    perspectiveNear[0].invW = perspectiveNear[1].invW =
        perspectiveNear[2].invW = 4.0f;
    fogTriangle[0] = GXMetalGouraud(210.0f, 232.0f, 0.75f,
                                    1.0f, 0.0f, 0.0f, 1.0f);
    fogTriangle[1] = GXMetalGouraud(240.0f, 200.0f, 0.75f,
                                    1.0f, 0.0f, 0.0f, 1.0f);
    fogTriangle[2] = GXMetalGouraud(270.0f, 232.0f, 0.75f,
                                    1.0f, 0.0f, 0.0f, 1.0f);
    /* Perspective-Z fog uses 1/invW, not the normalized Z-buffer coordinate.
     * Keep the historical 0.75 fog depth while making Z deliberately near. */
    fogTriangle[0].z = fogTriangle[1].z = fogTriangle[2].z = 0.10f;
    fogTriangle[0].invW = fogTriangle[1].invW = fogTriangle[2].invW =
        1.0f / 0.75f;

    chromakeyBase[0] = GXMetalGouraud(303.0f, 85.0f, 0.40f,
                                      0.0f, 1.0f, 0.0f, 1.0f);
    chromakeyBase[1] = GXMetalGouraud(311.0f, 45.0f, 0.40f,
                                      0.0f, 1.0f, 0.0f, 1.0f);
    chromakeyBase[2] = GXMetalGouraud(319.0f, 85.0f, 0.40f,
                                      0.0f, 1.0f, 0.0f, 1.0f);
    /* Sample a dedicated blue texel so texture origin/filtering cannot make
     * this a false negative for the chromakey state itself. */
    chromakeyTexture[0] = GXMetalTextureVertex(
        303.0f, 85.0f, 0.10f, 0.0f, 0.0f);
    chromakeyTexture[1] = GXMetalTextureVertex(
        311.0f, 45.0f, 0.10f, 0.0f, 0.0f);
    chromakeyTexture[2] = GXMetalTextureVertex(
        319.0f, 85.0f, 0.10f, 0.0f, 0.0f);

    texturedQuad[0] = GXMetalTextureVertex(178.0f, 38.0f, 0.30f,
                                            0.0f, 0.0f);
    texturedQuad[1] = GXMetalTextureVertex(302.0f, 38.0f, 0.30f,
                                            1.0f, 0.0f);
    texturedQuad[2] = GXMetalTextureVertex(178.0f, 190.0f, 0.30f,
                                            0.0f, 1.0f);
    texturedQuad[3] = GXMetalTextureVertex(302.0f, 190.0f, 0.30f,
                                            1.0f, 1.0f);
    bitmapVertex = GXMetalGouraud(318.0f, 212.0f, 0.05f,
                                  1.0f, 1.0f, 1.0f, 1.0f);

    QARenderStart(context, &dirty, NULL);
    QADrawTriGouraud(context, &farTriangle[0], &farTriangle[1],
                     &farTriangle[2], kQATriFlags_None);
    QADrawTriGouraud(context, &nearTriangle[0], &nearTriangle[1],
                     &nearTriangle[2], kQATriFlags_None);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QADrawTriGouraud(context, &blendBase[0], &blendBase[1],
                     &blendBase[2], kQATriFlags_None);
    QADrawTriGouraud(context, &blendOverlay[0], &blendOverlay[1],
                     &blendOverlay[2], kQATriFlags_None);
    QADrawTriGouraud(context, &alphaBase[0], &alphaBase[1],
                     &alphaBase[2], kQATriFlags_None);
    QASetFloat(context, kQATag_AlphaTestRef, 0.5f);
    QASetInt(context, kQATag_AlphaTestFunc, kQAAlphaTest_GT);
    QADrawTriGouraud(context, &alphaRejected[0], &alphaRejected[1],
                     &alphaRejected[2], kQATriFlags_None);
    QASetInt(context, kQATag_AlphaTestFunc, kQAAlphaTest_None);
    QADrawTriGouraud(context, &backfaceGouraudBase[0],
                     &backfaceGouraudBase[1], &backfaceGouraudBase[2],
                     kQATriFlags_None);
    QADrawTriGouraud(context, &backfaceGouraudOriented[0],
                     &backfaceGouraudOriented[1],
                     &backfaceGouraudOriented[2],
                     kQATriFlags_Backfacing);
    QADrawTriGouraud(context, &backfaceTextureBase[0],
                     &backfaceTextureBase[1], &backfaceTextureBase[2],
                     kQATriFlags_None);
    QADrawTriGouraud(context, &chromakeyBase[0], &chromakeyBase[1],
                     &chromakeyBase[2], kQATriFlags_None);
    QASetPtr(context, kQATag_Texture, texture);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QADrawVTexture(context, 3, kQAVertexMode_Tri,
                   backfaceTextureOriented, backfaceFlags);
    QADrawVTexture(context, 4, kQAVertexMode_Strip,
                   texturedQuad, flags);
    QASetFloat(context, kQATag_Chromakey_r, 0.0f);
    QASetFloat(context, kQATag_Chromakey_g, 0.0f);
    QASetFloat(context, kQATag_Chromakey_b, 1.0f);
    QASetInt(context, kQATag_ChromakeyEnable, 1);
    QASetPtr(context, kQATag_Texture, chromakeyResource);
    QADrawVTexture(context, 3, kQAVertexMode_Tri,
                   chromakeyTexture, flags);
    QASetInt(context, kQATag_ChromakeyEnable, 0);
    /* Bitmaps bind their own resource and must not depend on an unrelated
     * current texture. Carmageddon II exercises this path before binding its
     * first scene texture. */
    QASetPtr(context, kQATag_Texture, NULL);
    QADrawBitmap(context, &bitmapVertex, bitmap);
    /* Perspective-Z must use invW while preserving ordinary LT semantics.
     * Both triangles deliberately carry the same losing normalized Z; the
     * red triangle is visible only if its larger invW wins as the nearer
     * surface. */
    QASetInt(context, kQATag_PerspectiveZ, kQAPerspectiveZ_On);
    QADrawTriGouraud(context, &perspectiveFar[0], &perspectiveFar[1],
                     &perspectiveFar[2], kQATriFlags_None);
    QADrawTriGouraud(context, &perspectiveNear[0], &perspectiveNear[1],
                     &perspectiveNear[2], kQATriFlags_None);
    QASetFloat(context, kQATag_FogColor_a, 1.0f);
    QASetFloat(context, kQATag_FogColor_r, 0.0f);
    QASetFloat(context, kQATag_FogColor_g, 0.0f);
    QASetFloat(context, kQATag_FogColor_b, 1.0f);
    QASetFloat(context, kQATag_FogStart, 0.0f);
    QASetFloat(context, kQATag_FogEnd, 1.0f);
    QASetInt(context, kQATag_FogMode, kQAFogMode_Linear);
    QADrawTriGouraud(context, &fogTriangle[0], &fogTriangle[1],
                     &fogTriangle[2], kQATriFlags_None);
    QASetInt(context, kQATag_FogMode, kQAFogMode_None);
    QASetInt(context, kQATag_PerspectiveZ, kQAPerspectiveZ_Off);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: accelerated render completion");
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 82,
                                    deviceRect->top + 120,
                                    kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: depth ordering pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 200,
                                    deviceRect->top + 60,
                                    kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: texture sampling pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 160,
                                    deviceRect->top + 190,
                                    kGXMetalPixelPurple)) {
        GXMetalRecordResult("FAIL: alpha blending pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 319,
                                    deviceRect->top + 213,
                                    kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: bitmap upper pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 319,
                                    deviceRect->top + 215,
                                    kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: bitmap lower pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 240,
                                    deviceRect->top + 218,
                                    kGXMetalPixelFogPurple)) {
        GXMetalRecordResult("FAIL: depth fog pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 85,
                                    deviceRect->top + 35,
                                    kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: perspective-Z depth ordering pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 295,
                                    deviceRect->top + 207,
                                    kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: alpha rejection pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 30,
                                    deviceRect->top + 35,
                                    kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: scalar backface orientation pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 140,
                                    deviceRect->top + 35,
                                    kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: batched textured backface orientation pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 311,
                                    deviceRect->top + 65,
                                    kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: texture chromakey rejection pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 160,
                                    deviceRect->top + 4,
                                    kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: immutable clip pixel");
        error = kQAError;
    }
    QABitmapDelete(engine, bitmap);
    QATextureDelete(engine, chromakeyResource);
    QAColorTableDelete(engine, colorTable);
    QATextureDelete(engine, texture);
    return error;
}

static TQAError GXMetalRenderPublicMultiTexture(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    static unsigned long primaryPixels[4] = {
        0xff0000ffUL, 0xff0000ffUL,
        0xff0000ffUL, 0xff0000ffUL
    };
    static unsigned long secondaryPixels[4] = {
        0xff00ff00UL, 0xffffffffUL,
        0xff00ff00UL, 0xffffffffUL
    };
    TQAImage primaryImage;
    TQAImage secondaryImage;
    TQATexture *primaryTexture = NULL;
    TQATexture *secondaryTexture = NULL;
    TQAVTexture meshVertices[3];
    TQAVTexture directVertices[3];
    TQAVMultiTexture multiParams[3];
    TQAIndexedTriangle triangle;
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;
    int i;

    if (context == NULL || context->version < kQAVersion_1_6 ||
        context->submitMultiTextureParams == NULL) {
        GXMetalRecordResult("FAIL: RAVE 1.6 multitexture draw method");
        return kQANotSupported;
    }
    primaryImage.width = 2;
    primaryImage.height = 2;
    primaryImage.rowBytes = 8;
    primaryImage.pixmap = primaryPixels;
    secondaryImage.width = 2;
    secondaryImage.height = 2;
    secondaryImage.rowBytes = 8;
    secondaryImage.pixmap = secondaryPixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_ARGB32,
                         &primaryImage, &primaryTexture);
    if (error == kQANoErr) {
        error = QATextureNew(engine, kQATexture_None, kQAPixel_ARGB32,
                             &secondaryImage, &secondaryTexture);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: public multitexture resource creation");
        if (secondaryTexture != NULL) {
            QATextureDelete(engine, secondaryTexture);
        }
        if (primaryTexture != NULL) {
            QATextureDelete(engine, primaryTexture);
        }
        return error;
    }

    meshVertices[0] = GXMetalTextureVertex(20.0f, 190.0f, 0.5f,
                                            0.25f, 0.5f);
    meshVertices[1] = GXMetalTextureVertex(82.0f, 30.0f, 0.5f,
                                            0.25f, 0.5f);
    meshVertices[2] = GXMetalTextureVertex(145.0f, 190.0f, 0.5f,
                                            0.25f, 0.5f);
    directVertices[0] = GXMetalTextureVertex(175.0f, 190.0f, 0.5f,
                                              0.25f, 0.5f);
    directVertices[1] = GXMetalTextureVertex(237.0f, 30.0f, 0.5f,
                                              0.25f, 0.5f);
    directVertices[2] = GXMetalTextureVertex(300.0f, 190.0f, 0.5f,
                                              0.25f, 0.5f);
    for (i = 0; i < 3; i++) {
        /* Red highlight proves the public secondary UVs do not borrow the
         * TQAVTexture specular fields used by GXMetal's ATI compatibility
         * bridge. The secondary stage deliberately samples the white column
         * while the primary U coordinate points at the green column. */
        meshVertices[i].ks_r = 0.75f;
        directVertices[i].ks_r = 0.75f;
        multiParams[i].invW = 1.0f;
        multiParams[i].uOverW = 0.75f;
        multiParams[i].vOverW = 0.5f;
    }
    triangle.triangleFlags = kQATriFlags_None;
    triangle.vertices[0] = 0;
    triangle.vertices[1] = 1;
    triangle.vertices[2] = 2;

    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 0.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetPtr(context, kQATag_Texture, primaryTexture);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_Highlight);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QASetInt(context, kQATag_MultiTextureCurrent, 0);
    QASetPtr(context, kQATag_MultiTexture, secondaryTexture);
    QASetInt(context, kQATag_MultiTextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_MultiTextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATag_MultiTextureWrapV, kQAGL_Clamp);
    QASetInt(context, kQATag_MultiTextureOp, kQAMultiTexture_Modulate);
    QASetInt(context, kQATag_MultiTextureEnable, 1);

    QARenderStart(context, &dirty, NULL);
    QASubmitVerticesTexture(context, 3, meshVertices);
    QASubmitMultiTextureParams(context, 3, multiParams);
    QADrawTriMeshTexture(context, 1, &triangle);
    /* The same public secondary array must pair with immediate vertex arrays
     * and scalar triangles, not only the indexed submission path. */
    QADrawVTexture(context, 3, kQAVertexMode_Tri,
                   directVertices, NULL);
    QADrawTriTexture(context, &directVertices[0], &directVertices[1],
                     &directVertices[2], kQATriFlags_None);
    QASetInt(context, kQATag_MultiTextureEnable, 0);
    QADrawTriTexture(context, &directVertices[0], &directVertices[1],
                     &directVertices[2], kQATriFlags_None);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: public multitexture render completion");
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 82,
                                    deviceRect->top + 130,
                                    kGXMetalPixelPurple)) {
        GXMetalRecordResult(
            "FAIL: public multitexture coordinates or highlight pixel");
        error = kQAError;
    } else if (!GXMetalPixelMatches(graphicsDevice,
                                    deviceRect->left + 237,
                                    deviceRect->top + 130,
                                    kGXMetalPixelPurple)) {
        GXMetalRecordResult("FAIL: public multitexture disable pixel");
        error = kQAError;
    }

    QASetInt(context, kQATag_MultiTextureCurrent, 0);
    QASetPtr(context, kQATag_MultiTexture, NULL);
    QASetInt(context, kQATag_MultiTextureEnable, 0);
    QASetPtr(context, kQATag_Texture, NULL);
    QATextureDelete(engine, secondaryTexture);
    QATextureDelete(engine, primaryTexture);
    return error;
}

static void GXMetalFillATITexture(unsigned char pixels[8],
                                  unsigned char high,
                                  unsigned char low)
{
    int pixel;

    for (pixel = 0; pixel < 4; pixel++) {
        pixels[pixel * 2] = high;
        pixels[pixel * 2 + 1] = low;
    }
}

static TQAError GXMetalRenderATITextureMutation(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    unsigned char staticPixels[8];
    unsigned char livePixels[8];
    TQAImage staticImage;
    TQAImage liveImage;
    TQATexture *staticTexture = NULL;
    TQATexture *liveTexture = NULL;
    TQAVTexture leftQuad[4];
    TQAVTexture rightQuad[4];
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;

    /* ATI type 1001 is a big-endian ARGB4444 byte stream. */
    GXMetalFillATITexture(staticPixels, 0xff, 0x00); /* opaque red */
    GXMetalFillATITexture(livePixels, 0xff, 0x00);   /* opaque red */
    staticImage.width = 2;
    staticImage.height = 2;
    staticImage.rowBytes = 4;
    staticImage.pixmap = staticPixels;
    liveImage = staticImage;
    liveImage.pixmap = livePixels;

    error = QATextureNew(engine, kQATexture_None,
                         GXMETAL_ATI_PIXEL_RGB16,
                         &staticImage, &staticTexture);
    if (error == kQANoErr) {
        error = QATextureNew(engine, kQATexture_NoCopy,
                             GXMETAL_ATI_PIXEL_RGB16,
                             &liveImage, &liveTexture);
    }
    if (error != kQANoErr) {
        if (liveTexture != NULL) {
            QATextureDelete(engine, liveTexture);
        }
        if (staticTexture != NULL) {
            QATextureDelete(engine, staticTexture);
        }
        GXMetalRecordResult("FAIL: ATI private texture creation");
        return error;
    }

    /* A normal texture must retain its red creation-time upload even after
     * the caller reuses the source buffer. A NoCopy texture must observe the
     * green CPU-side mutation before its first draw. */
    GXMetalFillATITexture(staticPixels, 0xf0, 0x0f); /* opaque blue */
    GXMetalFillATITexture(livePixels, 0xf0, 0xf0);   /* opaque green */

    leftQuad[0] = GXMetalTextureVertex(12.0f, 12.0f, 0.5f,
                                       0.0f, 0.0f);
    leftQuad[1] = GXMetalTextureVertex(150.0f, 12.0f, 0.5f,
                                       1.0f, 0.0f);
    leftQuad[2] = GXMetalTextureVertex(12.0f, 208.0f, 0.5f,
                                       0.0f, -1.0f);
    leftQuad[3] = GXMetalTextureVertex(150.0f, 208.0f, 0.5f,
                                       1.0f, -1.0f);
    rightQuad[0] = GXMetalTextureVertex(170.0f, 12.0f, 0.5f,
                                        0.0f, 0.0f);
    rightQuad[1] = GXMetalTextureVertex(308.0f, 12.0f, 0.5f,
                                        1.0f, 0.0f);
    rightQuad[2] = GXMetalTextureVertex(170.0f, 208.0f, 0.5f,
                                        0.0f, -1.0f);
    rightQuad[3] = GXMetalTextureVertex(308.0f, 208.0f, 0.5f,
                                        1.0f, -1.0f);

    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 0.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    /* Apple's ATI OpenGL renderer binds the primary unit through the
     * multitexture pointer tag after selecting RAVE's -1 primary sentinel.
     * Exercise that exact path so validation cannot silently redirect every
     * primary OpenGL texture to GXMetal's secondary stage. */
    QASetInt(context, (TQATagInt)GXMETAL_ATI_PRIVATE_ENABLE_TAG, 1);
    QASetInt(context, kQATag_MultiTextureCurrent, (unsigned long)-1L);
    QASetPtr(context, kQATag_MultiTexture, staticTexture);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);

    QARenderStart(context, &dirty, NULL);
    QADrawVTexture(context, 4, kQAVertexMode_Strip,
                   leftQuad, vertexFlags);
    QASetPtr(context, kQATag_MultiTexture, liveTexture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip,
                   rightQuad, vertexFlags);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice,
                             deviceRect->left + 80,
                             deviceRect->top + 110,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: static ATI texture changed with caller memory");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice,
                             deviceRect->left + 240,
                             deviceRect->top + 110,
                             kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: ATI NoCopy texture did not refresh");
        error = kQAError;
    }
    QASetInt(context, (TQATagInt)GXMETAL_ATI_PRIVATE_ENABLE_TAG, 0);
    QATextureDelete(engine, liveTexture);
    QATextureDelete(engine, staticTexture);
    return error;
}

static TQAError GXMetalRenderATITextureUpdate(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    unsigned short blankPublicPixels[4] = {0, 0, 0, 0};
    unsigned short greenPublicPixels[4] = {
        0x03e0, 0x03e0, 0x03e0, 0x03e0
    };
    unsigned char blankPrivatePixels[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char redPrivatePixels[8];
    TQAImage image;
    TQATexture *textures[GXMETAL_ATI_PREALLOC_TEST_TEXTURES];
    GXMetalATIPrivateMethodProc *privateMethods;
    TQAVTexture publicQuad[4];
    TQAVTexture privateQuad[4];
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error = kQANoErr;
    int textureIndex;

    memset(textures, 0, sizeof(textures));
    GXMetalFillATITexture(redPrivatePixels, 0xff, 0x00);
    privateMethods = (GXMetalATIPrivateMethodProc *)QAGetPtr(
        context, (TQATagPtr)GXMETAL_ATI_PRIVATE_METHODS_TAG);
    if (privateMethods == NULL || privateMethods[2] == NULL) {
        GXMetalRecordResult("FAIL: ATI private texture-update method lookup");
        return kQANotSupported;
    }

    /* Match Myth II's transition pattern: 43 client-owned handles are first
     * backed by blank resources, then populated in place through slot 2. The
     * first resource also covers public RGB16 while the remaining resources
     * exercise ATI's private ARGB4444 type 1001. */
    image.width = 2;
    image.height = 2;
    image.rowBytes = 4;
    for (textureIndex = 0;
         textureIndex < GXMETAL_ATI_PREALLOC_TEST_TEXTURES;
         ++textureIndex) {
        TQAImagePixelType pixelType = textureIndex == 0 ?
            kQAPixel_RGB16 : GXMETAL_ATI_PIXEL_RGB16;

        image.pixmap = textureIndex == 0 ?
            (void *)blankPublicPixels : (void *)blankPrivatePixels;
        error = QATextureNew(engine, kQATexture_None, pixelType,
                             &image, &textures[textureIndex]);
        if (error != kQANoErr) {
            GXMetalRecordResult(
                "FAIL: ATI private texture-update pool creation");
            break;
        }
    }
    if (error == kQANoErr) {
        image.pixmap = greenPublicPixels;
        error = privateMethods[2](
            0, (unsigned long)kQAPixel_RGB16,
            (unsigned long)(uintptr_t)&image,
            (unsigned long)(uintptr_t)textures[0], 0, 0, 0, 0);
    }
    for (textureIndex = 1;
         error == kQANoErr &&
             textureIndex < GXMETAL_ATI_PREALLOC_TEST_TEXTURES;
         ++textureIndex) {
        image.pixmap = redPrivatePixels;
        error = privateMethods[2](
            0, (unsigned long)GXMETAL_ATI_PIXEL_RGB16,
            (unsigned long)(uintptr_t)&image,
            (unsigned long)(uintptr_t)textures[textureIndex], 0, 0, 0, 0);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: ATI private texture-update slot 2 upload");
    }

    publicQuad[0] = GXMetalTextureVertex(12.0f, 12.0f, 0.5f,
                                         0.0f, 0.0f);
    publicQuad[1] = GXMetalTextureVertex(150.0f, 12.0f, 0.5f,
                                         1.0f, 0.0f);
    publicQuad[2] = GXMetalTextureVertex(12.0f, 208.0f, 0.5f,
                                         0.0f, 1.0f);
    publicQuad[3] = GXMetalTextureVertex(150.0f, 208.0f, 0.5f,
                                         1.0f, 1.0f);
    privateQuad[0] = GXMetalTextureVertex(170.0f, 12.0f, 0.5f,
                                          0.0f, 0.0f);
    privateQuad[1] = GXMetalTextureVertex(308.0f, 12.0f, 0.5f,
                                          1.0f, 0.0f);
    privateQuad[2] = GXMetalTextureVertex(170.0f, 208.0f, 0.5f,
                                          0.0f, -1.0f);
    privateQuad[3] = GXMetalTextureVertex(308.0f, 208.0f, 0.5f,
                                          1.0f, -1.0f);
    if (error == kQANoErr) {
        QASetFloat(context, kQATag_ColorBG_r, 0.0f);
        QASetFloat(context, kQATag_ColorBG_g, 0.0f);
        QASetFloat(context, kQATag_ColorBG_b, 0.0f);
        QASetFloat(context, kQATag_ColorBG_a, 1.0f);
        QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
        QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
        QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
        QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
        QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
        QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
        QARenderStart(context, &dirty, NULL);
        QASetPtr(context, kQATag_Texture, textures[0]);
        QADrawVTexture(context, 4, kQAVertexMode_Strip,
                       publicQuad, vertexFlags);
        QASetPtr(context, kQATag_Texture, textures[1]);
        QADrawVTexture(context, 4, kQAVertexMode_Strip,
                       privateQuad, vertexFlags);
        error = QARenderEnd(context, &dirty);
        if (error == kQANoErr) {
            error = QASync(context);
        }
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice,
                             deviceRect->left + 80,
                             deviceRect->top + 110,
                             kGXMetalPixelGreen)) {
        GXMetalRecordResult(
            "FAIL: public RGB16 private texture-update pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice,
                             deviceRect->left + 240,
                             deviceRect->top + 110,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult(
            "FAIL: ATI-1001 private texture-update pixel");
        error = kQAError;
    }

    QASetPtr(context, kQATag_Texture, NULL);
    for (textureIndex = 0;
         textureIndex < GXMETAL_ATI_PREALLOC_TEST_TEXTURES;
         ++textureIndex) {
        if (textures[textureIndex] != NULL) {
            QATextureDelete(engine, textures[textureIndex]);
        }
    }
    return error;
}

static TQAError GXMetalRenderDynamicResources(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    unsigned long texturePixels[16];
    unsigned long bitmapPixels[16];
    TQAImage textureImage;
    TQAImage bitmapImage;
    TQATexture *texture = NULL;
    TQABitmap *bitmap = NULL;
    TQAPixelBuffer access;
    TQARect textureDirty = {0, 2, 0, 4};
    TQARect bitmapDirty = {0, 4, 0, 2};
    TQARect frameDirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAVTexture quad[4];
    TQAVGouraud bitmapVertex;
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQAError error;
    int x;
    int y;

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            texturePixels[y * 4 + x] = 0xff0000ffUL;
            bitmapPixels[y * 4 + x] = 0xff0000ffUL;
        }
    }
    textureImage.width = 4;
    textureImage.height = 4;
    textureImage.rowBytes = 16;
    textureImage.pixmap = texturePixels;
    bitmapImage = textureImage;
    bitmapImage.pixmap = bitmapPixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_ARGB32,
                         &textureImage, &texture);
    if (error == kQANoErr) {
        error = QABitmapNew(engine, kQABitmap_None, kQAPixel_ARGB32,
                            &bitmapImage, &bitmap);
    }
    memset(&access, 0, sizeof(access));
    if (error == kQANoErr) {
        error = QAAccessTexture(engine, texture, 0,
                                kQANoCopyNeeded, &access);
    }
    if (error == kQANoErr &&
        (access.pixelType != kQAPixel_ARGB32 || access.width != 4 ||
         access.height != 4 || access.rowBytes < 16 ||
         access.baseAddr == NULL)) {
        error = kQAError;
    }
    if (error == kQANoErr) {
        for (y = 0; y < 4; y++) {
            unsigned long *row = (unsigned long *)
                ((unsigned char *)access.baseAddr + y * access.rowBytes);
            row[0] = 0xffff0000UL;
            row[1] = 0xffff0000UL;
        }
        error = QAAccessTextureEnd(engine, texture, &textureDirty);
    }
    memset(&access, 0, sizeof(access));
    if (error == kQANoErr) {
        error = QAAccessBitmap(engine, bitmap, kQANoCopyNeeded, &access);
    }
    if (error == kQANoErr &&
        (access.pixelType != kQAPixel_ARGB32 || access.width != 4 ||
         access.height != 4 || access.rowBytes < 16 ||
         access.baseAddr == NULL)) {
        error = kQAError;
    }
    if (error == kQANoErr) {
        for (y = 0; y < 2; y++) {
            unsigned long *row = (unsigned long *)
                ((unsigned char *)access.baseAddr + y * access.rowBytes);
            for (x = 0; x < 4; x++) {
                row[x] = 0xffff0000UL;
            }
        }
        error = QAAccessBitmapEnd(engine, bitmap, &bitmapDirty);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: dynamic RAVE resource access");
        if (bitmap != NULL) {
            QABitmapDelete(engine, bitmap);
        }
        if (texture != NULL) {
            QATextureDelete(engine, texture);
        }
        return error;
    }

    quad[0] = GXMetalTextureVertex(16.0f, 24.0f, 0.5f, 0.0f, 0.0f);
    quad[1] = GXMetalTextureVertex(144.0f, 24.0f, 0.5f, 1.0f, 0.0f);
    quad[2] = GXMetalTextureVertex(16.0f, 196.0f, 0.5f, 0.0f, 1.0f);
    quad[3] = GXMetalTextureVertex(144.0f, 196.0f, 0.5f, 1.0f, 1.0f);
    bitmapVertex = GXMetalGouraud(200.0f, 100.0f, 0.4f,
                                  1.0f, 1.0f, 1.0f, 1.0f);
    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 0.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QARenderStart(context, &frameDirty, NULL);
    QASetPtr(context, kQATag_Texture, texture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip, quad, vertexFlags);
    QADrawBitmap(context, &bitmapVertex, bitmap);
    error = QARenderEnd(context, &frameDirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 48,
                             deviceRect->top + 110,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: dynamic texture dirty-region pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 112,
                             deviceRect->top + 110,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: dynamic texture preserved pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 201,
                             deviceRect->top + 101,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: dynamic bitmap dirty-region pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 201,
                             deviceRect->top + 103,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: dynamic bitmap preserved pixel");
        error = kQAError;
    }
    QABitmapDelete(engine, bitmap);
    QATextureDelete(engine, texture);
    return error;
}

static TQAError GXMetalRenderIntensityFormats(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    unsigned char intensityPixels[4] = {0x80, 0x11, 0x22, 0x33};
    unsigned char alphaIntensityPixels[4] = {0x80, 0xff, 0x44, 0x55};
    TQAImage intensityImage;
    TQAImage alphaIntensityImage;
    TQATexture *intensityTexture = NULL;
    TQATexture *alphaIntensityTexture = NULL;
    TQABitmap *intensityBitmap = NULL;
    TQABitmap *alphaIntensityBitmap = NULL;
    TQAVTexture leftQuad[4];
    TQAVTexture rightQuad[4];
    TQAVGouraud intensityBitmapVertex;
    TQAVGouraud alphaIntensityBitmapVertex;
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;

    intensityImage.width = 1;
    intensityImage.height = 1;
    intensityImage.rowBytes = 4;
    intensityImage.pixmap = intensityPixels;
    alphaIntensityImage = intensityImage;
    alphaIntensityImage.pixmap = alphaIntensityPixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_I8,
                         &intensityImage, &intensityTexture);
    if (error == kQANoErr) {
        error = QATextureNew(engine, kQATexture_None, kQAPixel_AI16_88,
                             &alphaIntensityImage,
                             &alphaIntensityTexture);
    }
    if (error == kQANoErr) {
        error = QABitmapNew(engine, kQABitmap_None, kQAPixel_I8,
                            &intensityImage, &intensityBitmap);
    }
    if (error == kQANoErr) {
        error = QABitmapNew(engine, kQABitmap_None, kQAPixel_AI16_88,
                            &alphaIntensityImage,
                            &alphaIntensityBitmap);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: intensity texture creation");
        if (alphaIntensityBitmap != NULL) {
            QABitmapDelete(engine, alphaIntensityBitmap);
        }
        if (intensityBitmap != NULL) {
            QABitmapDelete(engine, intensityBitmap);
        }
        if (alphaIntensityTexture != NULL) {
            QATextureDelete(engine, alphaIntensityTexture);
        }
        if (intensityTexture != NULL) {
            QATextureDelete(engine, intensityTexture);
        }
        return error;
    }

    leftQuad[0] = GXMetalTextureVertex(16.0f, 24.0f, 0.5f, 0.0f, 0.0f);
    leftQuad[1] = GXMetalTextureVertex(144.0f, 24.0f, 0.5f, 1.0f, 0.0f);
    leftQuad[2] = GXMetalTextureVertex(16.0f, 196.0f, 0.5f, 0.0f, 1.0f);
    leftQuad[3] = GXMetalTextureVertex(144.0f, 196.0f, 0.5f, 1.0f, 1.0f);
    rightQuad[0] = GXMetalTextureVertex(176.0f, 24.0f, 0.5f, 0.0f, 0.0f);
    rightQuad[1] = GXMetalTextureVertex(304.0f, 24.0f, 0.5f, 1.0f, 0.0f);
    rightQuad[2] = GXMetalTextureVertex(176.0f, 196.0f, 0.5f, 0.0f, 1.0f);
    rightQuad[3] = GXMetalTextureVertex(304.0f, 196.0f, 0.5f, 1.0f, 1.0f);
    intensityBitmapVertex = GXMetalGouraud(152.0f, 100.0f, 0.4f,
                                           1.0f, 1.0f, 1.0f, 1.0f);
    alphaIntensityBitmapVertex = GXMetalGouraud(
        162.0f, 100.0f, 0.4f, 1.0f, 1.0f, 1.0f, 1.0f);
    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 1.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QASetInt(context, kQATag_AlphaTestFunc, kQAAlphaTest_None);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QARenderStart(context, &dirty, NULL);
    QASetPtr(context, kQATag_Texture, intensityTexture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip,
                   leftQuad, vertexFlags);
    QASetPtr(context, kQATag_Texture, alphaIntensityTexture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip,
                   rightQuad, vertexFlags);
    QADrawBitmap(context, &intensityBitmapVertex, intensityBitmap);
    QADrawBitmap(context, &alphaIntensityBitmapVertex,
                 alphaIntensityBitmap);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 80,
                             deviceRect->top + 110,
                             kGXMetalPixelGray)) {
        GXMetalRecordResult("FAIL: I8 intensity pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 240,
                             deviceRect->top + 110,
                             kGXMetalPixelLightBlue)) {
        GXMetalRecordResult("FAIL: AI16_88 alpha-intensity pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 152,
                             deviceRect->top + 100,
                             kGXMetalPixelGray)) {
        GXMetalRecordResult("FAIL: I8 intensity bitmap pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 162,
                             deviceRect->top + 100,
                             kGXMetalPixelLightBlue)) {
        GXMetalRecordResult("FAIL: AI16_88 alpha-intensity bitmap pixel");
        error = kQAError;
    }
    QABitmapDelete(engine, alphaIntensityBitmap);
    QABitmapDelete(engine, intensityBitmap);
    QATextureDelete(engine, alphaIntensityTexture);
    QATextureDelete(engine, intensityTexture);
    return error;
}

static TQAError GXMetalRenderAlphaPaletteFormat(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    static unsigned char pixels[8] = {
        0x80, 0x01, 0xff, 0x00, 0x33, 0x44, 0x55, 0x66
    };
    static unsigned long palette[256] = {
        [0] = 0x00ff0000UL,
        [1] = 0x00ffffffUL
    };
    TQAImage image;
    TQAColorTable *colorTable = NULL;
    TQATexture *texture = NULL;
    TQABitmap *bitmap = NULL;
    TQAVTexture quad[4];
    TQAVGouraud bitmapVertex;
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;

    image.width = 2;
    image.height = 1;
    image.rowBytes = 8;
    image.pixmap = pixels;
    error = QAColorTableNew(engine, kQAColorTable_CL8_RGB32,
                            palette, true, &colorTable);
    if (error == kQANoErr) {
        error = QATextureNew(engine, kQATexture_None,
                             kQAPixel_ACL16_88, &image, &texture);
    }
    if (error == kQANoErr) {
        error = QATextureBindColorTable(engine, texture, colorTable);
    }
    if (error == kQANoErr) {
        error = QABitmapNew(engine, kQABitmap_None,
                            kQAPixel_ACL16_88, &image, &bitmap);
    }
    if (error == kQANoErr) {
        error = QABitmapBindColorTable(engine, bitmap, colorTable);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: ACL16_88 palette resource creation");
        if (bitmap != NULL) {
            QABitmapDelete(engine, bitmap);
        }
        if (texture != NULL) {
            QATextureDelete(engine, texture);
        }
        if (colorTable != NULL) {
            QAColorTableDelete(engine, colorTable);
        }
        return error;
    }

    quad[0] = GXMetalTextureVertex(16.0f, 24.0f, 0.5f, 0.0f, 0.0f);
    quad[1] = GXMetalTextureVertex(144.0f, 24.0f, 0.5f, 1.0f, 0.0f);
    quad[2] = GXMetalTextureVertex(16.0f, 196.0f, 0.5f, 0.0f, 1.0f);
    quad[3] = GXMetalTextureVertex(144.0f, 196.0f, 0.5f, 1.0f, 1.0f);
    bitmapVertex = GXMetalGouraud(160.0f, 100.0f, 0.4f,
                                  1.0f, 1.0f, 1.0f, 1.0f);
    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 1.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QASetInt(context, kQATag_AlphaTestFunc, kQAAlphaTest_None);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QARenderStart(context, &dirty, NULL);
    QASetPtr(context, kQATag_Texture, texture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip, quad, vertexFlags);
    QADrawBitmap(context, &bitmapVertex, bitmap);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 48,
                             deviceRect->top + 110,
                             kGXMetalPixelLightBlue)) {
        GXMetalRecordResult("FAIL: ACL16_88 per-pixel alpha");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 112,
                             deviceRect->top + 110,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: ACL16_88 transparent palette index");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 160,
                             deviceRect->top + 100,
                             kGXMetalPixelLightBlue)) {
        GXMetalRecordResult("FAIL: ACL16_88 bitmap alpha");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 161,
                             deviceRect->top + 100,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: ACL16_88 bitmap transparency");
        error = kQAError;
    }
    QABitmapDelete(engine, bitmap);
    QATextureDelete(engine, texture);
    QAColorTableDelete(engine, colorTable);
    return error;
}

static TQAError GXMetalRenderAlpha1Format(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    /* Apple Software RAVE keeps Alpha1 textures byte-addressed, while RAVE's
     * public bitmap contract is packed one-bit alpha. The bitmap uses ten
     * pixels in four-byte padded rows to prove MSB-first order, byte crossing,
     * row padding, and a source rowBytes smaller than the pixel width. */
    static unsigned char texturePixels[4] = {0x00, 0x01, 0xa5, 0x5a};
    static unsigned char bitmapPixels[2][4] = {
        {0x40, 0x40, 0xff, 0xff},
        {0x80, 0x80, 0xff, 0xff}
    };
    TQAImage textureImage;
    TQAImage bitmapImage;
    TQATexture *texture = NULL;
    TQABitmap *bitmap = NULL;
    TQAVTexture quad[4];
    TQAVGouraud bitmapVertex;
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;

    textureImage.width = 2;
    textureImage.height = 1;
    textureImage.rowBytes = 4;
    textureImage.pixmap = texturePixels;
    bitmapImage.width = 10;
    bitmapImage.height = 2;
    bitmapImage.rowBytes = 4;
    bitmapImage.pixmap = bitmapPixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_Alpha1,
                         &textureImage, &texture);
    if (error == kQANoErr) {
        error = QABitmapNew(engine, kQABitmap_None, kQAPixel_Alpha1,
                            &bitmapImage, &bitmap);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: Alpha1 resource creation");
        if (bitmap != NULL) {
            QABitmapDelete(engine, bitmap);
        }
        if (texture != NULL) {
            QATextureDelete(engine, texture);
        }
        return error;
    }

    quad[0] = GXMetalTextureVertex(16.0f, 24.0f, 0.5f, 0.0f, 0.0f);
    quad[1] = GXMetalTextureVertex(144.0f, 24.0f, 0.5f, 1.0f, 0.0f);
    quad[2] = GXMetalTextureVertex(16.0f, 196.0f, 0.5f, 0.0f, 1.0f);
    quad[3] = GXMetalTextureVertex(144.0f, 196.0f, 0.5f, 1.0f, 1.0f);
    bitmapVertex = GXMetalGouraud(160.0f, 100.0f, 0.4f,
                                  1.0f, 1.0f, 1.0f, 1.0f);
    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 1.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QASetFloat(context, kQATag_AlphaTestRef, 0.5f);
    QASetInt(context, kQATag_AlphaTestFunc, kQAAlphaTest_GT);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QARenderStart(context, &dirty, NULL);
    QASetPtr(context, kQATag_Texture, texture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip, quad, vertexFlags);
    QADrawBitmap(context, &bitmapVertex, bitmap);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 48,
                             deviceRect->top + 110,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: Alpha1 zero texture alpha");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 112,
                             deviceRect->top + 110,
                             kGXMetalPixelWhite)) {
        GXMetalRecordResult("FAIL: Alpha1 byte texture layout");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 160,
                             deviceRect->top + 100,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: Alpha1 zero bitmap alpha");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 161,
                             deviceRect->top + 100,
                             kGXMetalPixelWhite)) {
        GXMetalRecordResult("FAIL: Alpha1 packed bitmap bit order");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 168,
                             deviceRect->top + 100,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: Alpha1 packed bitmap byte crossing");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 169,
                             deviceRect->top + 100,
                             kGXMetalPixelWhite)) {
        GXMetalRecordResult("FAIL: Alpha1 packed bitmap second byte");
        error = kQAError;
    }
    if (error == kQANoErr &&
        (!GXMetalPixelMatches(graphicsDevice, deviceRect->left + 160,
                              deviceRect->top + 101,
                              kGXMetalPixelWhite) ||
         !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 161,
                              deviceRect->top + 101,
                              kGXMetalPixelBlue) ||
         !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 168,
                              deviceRect->top + 101,
                              kGXMetalPixelWhite))) {
        GXMetalRecordResult("FAIL: Alpha1 packed bitmap row padding");
        error = kQAError;
    }
    QABitmapDelete(engine, bitmap);
    QATextureDelete(engine, texture);
    return error;
}

static TQAError GXMetalRenderRGB332Format(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    /* RAVE defines each byte as RRR GGG BB. A three-pixel row proves all
     * channels independently while the fourth byte proves row padding is
     * not interpreted as another texel. */
    static unsigned char pixels[4] = {0xe0, 0x1c, 0x03, 0xff};
    TQAImage image;
    TQATexture *texture = NULL;
    TQABitmap *bitmap = NULL;
    TQAVTexture quad[4];
    TQAVGouraud bitmapVertex;
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;

    image.width = 3;
    image.height = 1;
    image.rowBytes = 4;
    image.pixmap = pixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_RGB8_332,
                         &image, &texture);
    if (error == kQANoErr) {
        error = QABitmapNew(engine, kQABitmap_None, kQAPixel_RGB8_332,
                            &image, &bitmap);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: RGB8_332 resource creation");
        if (bitmap != NULL) {
            QABitmapDelete(engine, bitmap);
        }
        if (texture != NULL) {
            QATextureDelete(engine, texture);
        }
        return error;
    }

    quad[0] = GXMetalTextureVertex(16.0f, 24.0f, 0.5f, 0.0f, 0.0f);
    quad[1] = GXMetalTextureVertex(145.0f, 24.0f, 0.5f, 1.0f, 0.0f);
    quad[2] = GXMetalTextureVertex(16.0f, 196.0f, 0.5f, 0.0f, 1.0f);
    quad[3] = GXMetalTextureVertex(145.0f, 196.0f, 0.5f, 1.0f, 1.0f);
    bitmapVertex = GXMetalGouraud(160.0f, 100.0f, 0.4f,
                                  1.0f, 1.0f, 1.0f, 1.0f);
    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 0.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QASetInt(context, kQATag_AlphaTestFunc, kQAAlphaTest_None);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QARenderStart(context, &dirty, NULL);
    QASetPtr(context, kQATag_Texture, texture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip, quad, vertexFlags);
    QADrawBitmap(context, &bitmapVertex, bitmap);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 37,
                             deviceRect->top + 110,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: RGB8_332 red texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 80,
                             deviceRect->top + 110,
                             kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: RGB8_332 green texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 123,
                             deviceRect->top + 110,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: RGB8_332 blue texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 160,
                             deviceRect->top + 100,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: RGB8_332 red bitmap pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 161,
                             deviceRect->top + 100,
                             kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: RGB8_332 green bitmap pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 162,
                             deviceRect->top + 100,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: RGB8_332 blue bitmap pixel");
        error = kQAError;
    }
    QABitmapDelete(engine, bitmap);
    QATextureDelete(engine, texture);
    return error;
}

static TQAError GXMetalRenderRGB24Format(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    /* RGB24 is a packed three-byte R, G, B stream. The final three bytes are
     * row padding and must never become a fourth texel. This format is marked
     * Win32-only in RAVE.h, but Apple's ATI OpenGL bridge uses it on Mac OS 9
     * for GL_RGB/UNSIGNED_BYTE texture images. */
    static unsigned char pixels[12] = {
        0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
        0xff, 0xff, 0xff
    };
    TQAImage image;
    TQATexture *texture = NULL;
    TQABitmap *bitmap = NULL;
    TQAVTexture quad[4];
    TQAVGouraud bitmapVertex;
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;

    image.width = 3;
    image.height = 1;
    image.rowBytes = 12;
    image.pixmap = pixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_RGB24,
                         &image, &texture);
    if (error == kQANoErr) {
        error = QABitmapNew(engine, kQABitmap_None, kQAPixel_RGB24,
                            &image, &bitmap);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: RGB24 resource creation");
        if (bitmap != NULL) {
            QABitmapDelete(engine, bitmap);
        }
        if (texture != NULL) {
            QATextureDelete(engine, texture);
        }
        return error;
    }

    quad[0] = GXMetalTextureVertex(16.0f, 24.0f, 0.5f, 0.0f, 0.0f);
    quad[1] = GXMetalTextureVertex(145.0f, 24.0f, 0.5f, 1.0f, 0.0f);
    quad[2] = GXMetalTextureVertex(16.0f, 196.0f, 0.5f, 0.0f, 1.0f);
    quad[3] = GXMetalTextureVertex(145.0f, 196.0f, 0.5f, 1.0f, 1.0f);
    bitmapVertex = GXMetalGouraud(160.0f, 100.0f, 0.4f,
                                  1.0f, 1.0f, 1.0f, 1.0f);
    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 0.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QASetInt(context, kQATag_AlphaTestFunc, kQAAlphaTest_None);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QARenderStart(context, &dirty, NULL);
    QASetPtr(context, kQATag_Texture, texture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip, quad, vertexFlags);
    QADrawBitmap(context, &bitmapVertex, bitmap);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 37,
                             deviceRect->top + 110,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: RGB24 red texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 80,
                             deviceRect->top + 110,
                             kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: RGB24 green texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 123,
                             deviceRect->top + 110,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: RGB24 blue texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 160,
                             deviceRect->top + 100,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: RGB24 red bitmap pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 161,
                             deviceRect->top + 100,
                             kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: RGB24 green bitmap pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 162,
                             deviceRect->top + 100,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: RGB24 blue bitmap pixel");
        error = kQAError;
    }
    QABitmapDelete(engine, bitmap);
    QATextureDelete(engine, texture);
    return error;
}

static TQAError GXMetalRenderBitmapScale(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    static unsigned char pixels[4] = {0x00, 0xff, 0x00, 0x00};
    TQAImage image;
    TQABitmap *bitmap = NULL;
    TQAVGouraud vertex;
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;

    image.width = 1;
    image.height = 1;
    image.rowBytes = 4;
    image.pixmap = pixels;
    error = QABitmapNew(engine, kQABitmap_None, kQAPixel_RGB32,
                        &image, &bitmap);
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: scaled bitmap creation");
        return error;
    }

    vertex = GXMetalGouraud(160.0f, 100.0f, 0.4f,
                             1.0f, 1.0f, 1.0f, 1.0f);
    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 1.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetFloat(context, kQATag_BitmapScale_x, 3.0f);
    QASetFloat(context, kQATag_BitmapScale_y, 2.0f);
    QASetInt(context, kQATag_BitmapFilter, kQAFilter_Mid);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QARenderStart(context, &dirty, NULL);
    QADrawBitmap(context, &vertex, bitmap);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 162,
                             deviceRect->top + 101,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: scaled bitmap interior pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 163,
                             deviceRect->top + 101,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: scaled bitmap horizontal extent");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 162,
                             deviceRect->top + 102,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: scaled bitmap vertical extent");
        error = kQAError;
    }
    QASetFloat(context, kQATag_BitmapScale_x, 1.0f);
    QASetFloat(context, kQATag_BitmapScale_y, 1.0f);
    QASetInt(context, kQATag_BitmapFilter, kQAFilter_Fast);
    QABitmapDelete(engine, bitmap);
    return error;
}

static TQAError GXMetalRenderCL4Format(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    /* Three pixels prove high-nibble-left packing and odd-width handling.
     * The low nibble of byte two and the final two bytes are padding that
     * must never leak into the expanded host resource. */
    static unsigned char pixels[4] = {0x12, 0x0f, 0xa5, 0x5a};
    static unsigned long palette[16] = {
        [0] = 0x00ffffffUL,
        [1] = 0x00ff0000UL,
        [2] = 0x0000ff00UL,
        [15] = 0x00ff00ffUL
    };
    TQAImage image;
    TQAColorTable *colorTable = NULL;
    TQATexture *texture = NULL;
    TQABitmap *bitmap = NULL;
    TQAVTexture quad[4];
    TQAVGouraud bitmapVertex;
    unsigned long vertexFlags[4] = {0, 0, 0, 0};
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;

    image.width = 3;
    image.height = 1;
    image.rowBytes = 4;
    image.pixmap = pixels;
    error = QAColorTableNew(engine, kQAColorTable_CL4_RGB32,
                            palette, true, &colorTable);
    if (error == kQANoErr) {
        error = QATextureNew(engine, kQATexture_None, kQAPixel_CL4,
                             &image, &texture);
    }
    if (error == kQANoErr) {
        error = QATextureBindColorTable(engine, texture, colorTable);
    }
    if (error == kQANoErr) {
        error = QABitmapNew(engine, kQABitmap_None, kQAPixel_CL4,
                            &image, &bitmap);
    }
    if (error == kQANoErr) {
        error = QABitmapBindColorTable(engine, bitmap, colorTable);
    }
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: CL4 palette resource creation");
        if (bitmap != NULL) {
            QABitmapDelete(engine, bitmap);
        }
        if (texture != NULL) {
            QATextureDelete(engine, texture);
        }
        if (colorTable != NULL) {
            QAColorTableDelete(engine, colorTable);
        }
        return error;
    }

    quad[0] = GXMetalTextureVertex(16.0f, 24.0f, 0.5f, 0.0f, 0.0f);
    quad[1] = GXMetalTextureVertex(145.0f, 24.0f, 0.5f, 1.0f, 0.0f);
    quad[2] = GXMetalTextureVertex(16.0f, 196.0f, 0.5f, 0.0f, 1.0f);
    quad[3] = GXMetalTextureVertex(145.0f, 196.0f, 0.5f, 1.0f, 1.0f);
    bitmapVertex = GXMetalGouraud(160.0f, 100.0f, 0.4f,
                                  1.0f, 1.0f, 1.0f, 1.0f);
    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 1.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QASetInt(context, kQATag_AlphaTestFunc, kQAAlphaTest_None);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QARenderStart(context, &dirty, NULL);
    QASetPtr(context, kQATag_Texture, texture);
    QADrawVTexture(context, 4, kQAVertexMode_Strip, quad, vertexFlags);
    QADrawBitmap(context, &bitmapVertex, bitmap);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 37,
                             deviceRect->top + 110,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: CL4 high-nibble texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 80,
                             deviceRect->top + 110,
                             kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: CL4 low-nibble texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 123,
                             deviceRect->top + 110,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: CL4 odd-width transparent texture pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 160,
                             deviceRect->top + 100,
                             kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: CL4 high-nibble bitmap pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 161,
                             deviceRect->top + 100,
                             kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: CL4 low-nibble bitmap pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 162,
                             deviceRect->top + 100,
                             kGXMetalPixelBlue)) {
        GXMetalRecordResult("FAIL: CL4 odd-width transparent bitmap pixel");
        error = kQAError;
    }
    QABitmapDelete(engine, bitmap);
    QATextureDelete(engine, texture);
    QAColorTableDelete(engine, colorTable);
    return error;
}

static TQAError GXMetalRenderLargeMeshBatches(
    TQADrawContext *context, const TQAEngine *engine,
    GDHandle graphicsDevice, const TQARect *deviceRect)
{
    enum { kTriangleCount = 256 };
    static unsigned char pixels[4] = {0x00, 0x00, 0xff, 0x00};
    static TQAIndexedTriangle triangles[kTriangleCount];
    TQAImage image;
    TQATexture *texture = NULL;
    TQAVGouraud gouraud[3];
    TQAVTexture textured[3];
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAError error;
    unsigned long i;

    image.width = 1;
    image.height = 1;
    image.rowBytes = 4;
    image.pixmap = pixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_RGB32,
                         &image, &texture);
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: large mesh texture creation");
        return error;
    }
    for (i = 0; i < kTriangleCount; i++) {
        triangles[i].triangleFlags = kQATriFlags_None;
        triangles[i].vertices[0] = 0;
        triangles[i].vertices[1] = 1;
        triangles[i].vertices[2] = 2;
    }
    gouraud[0] = GXMetalGouraud(16.0f, 16.0f, 0.5f,
                                 1.0f, 0.0f, 0.0f, 1.0f);
    gouraud[1] = GXMetalGouraud(144.0f, 16.0f, 0.5f,
                                 1.0f, 0.0f, 0.0f, 1.0f);
    gouraud[2] = GXMetalGouraud(80.0f, 196.0f, 0.5f,
                                 1.0f, 0.0f, 0.0f, 1.0f);
    textured[0] = GXMetalTextureVertex(176.0f, 16.0f, 0.5f, 0.0f, 0.0f);
    textured[1] = GXMetalTextureVertex(304.0f, 16.0f, 0.5f, 1.0f, 0.0f);
    textured[2] = GXMetalTextureVertex(240.0f, 196.0f, 0.5f, 0.5f, 1.0f);

    QASetFloat(context, kQATag_ColorBG_r, 0.0f);
    QASetFloat(context, kQATag_ColorBG_g, 0.0f);
    QASetFloat(context, kQATag_ColorBG_b, 1.0f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_None);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Disable);
    QASetInt(context, kQATag_Blend, kQABlend_PreMultiply);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QARenderStart(context, &dirty, NULL);
    QASubmitVerticesGouraud(context, 3, gouraud);
    QADrawTriMeshGouraud(context, kTriangleCount, triangles);
    QASetPtr(context, kQATag_Texture, texture);
    QASubmitVerticesTexture(context, 3, textured);
    QADrawTriMeshTexture(context, kTriangleCount, triangles);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 80,
                             deviceRect->top + 64, kGXMetalPixelRed)) {
        GXMetalRecordResult("FAIL: large Gouraud mesh batch pixel");
        error = kQAError;
    }
    if (error == kQANoErr &&
        !GXMetalPixelMatches(graphicsDevice, deviceRect->left + 240,
                             deviceRect->top + 64, kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: large textured mesh batch pixel");
        error = kQAError;
    }
    QATextureDelete(engine, texture);
    return error;
}

static TQAError GXMetalBenchmarkFrame(TQADrawContext *context,
                                      TQATexture *texture,
                                      unsigned long frame)
{
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    unsigned long flags[4] = {0, 0, 0, 0};
    int primitive;
    TQAError error;

    QASetFloat(context, kQATag_ColorBG_r, 0.01f);
    QASetFloat(context, kQATag_ColorBG_g, 0.01f);
    QASetFloat(context, kQATag_ColorBG_b, 0.02f);
    QASetFloat(context, kQATag_ColorBG_a, 1.0f);
    QASetInt(context, kQATag_ZFunction, kQAZFunction_LT);
    QASetInt(context, kQATag_ZBufferMask, kQAZBufferMask_Enable);
    QASetInt(context, kQATag_Blend, kQABlend_Interpolate);
    QASetPtr(context, kQATag_Texture, texture);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Repeat);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Repeat);

    QARenderStart(context, &dirty, NULL);
    for (primitive = 0; primitive < 24; primitive++) {
        TQAVTexture quad[4];
        TQAVGouraud triangle[3];
        float left = (float)((primitive % 6) * 52 +
                             ((frame + primitive) & 7));
        float top = (float)((primitive / 6) * 52 +
                            ((frame + primitive * 3) & 7));
        float right = left + 44.0f;
        float bottom = top + 44.0f;
        float red = (float)((primitive * 5) & 15) / 15.0f;
        float green = (float)((primitive * 9) & 15) / 15.0f;
        float blue = (float)((primitive * 13) & 15) / 15.0f;

        quad[0] = GXMetalTextureVertex(left, top, 0.55f, 0.0f, 0.0f);
        quad[1] = GXMetalTextureVertex(right, top, 0.55f, 2.0f, 0.0f);
        quad[2] = GXMetalTextureVertex(left, bottom, 0.55f, 0.0f, 2.0f);
        quad[3] = GXMetalTextureVertex(right, bottom, 0.55f, 2.0f, 2.0f);
        QADrawVTexture(context, 4, kQAVertexMode_Strip, quad, flags);

        triangle[0] = GXMetalGouraud(left + 4.0f, bottom - 3.0f, 0.20f,
                                     red, green, blue, 0.72f);
        triangle[1] = GXMetalGouraud((left + right) * 0.5f,
                                     top + 3.0f, 0.20f,
                                     blue, red, green, 0.72f);
        triangle[2] = GXMetalGouraud(right - 4.0f, bottom - 3.0f, 0.20f,
                                     green, blue, red, 0.72f);
        QADrawTriGouraud(context, &triangle[0], &triangle[1],
                         &triangle[2], kQATriFlags_None);
    }
    error = QARenderEnd(context, &dirty);
    return error == kQANoErr ? QASync(context) : error;
}

static uint64_t GXMetalMicrosecondValue(const UnsignedWide *time)
{
    return ((uint64_t)time->hi << 32) | time->lo;
}

static TQAError GXMetalBenchmarkEngine(const TQADevice *device,
                                       const TQARect *deviceRect,
                                       const TQAEngine *engine,
                                       uint64_t *elapsedMicroseconds)
{
    static unsigned long texturePixels[4] = {
        0xffff4010UL, 0xff10d040UL,
        0xff2040ffUL, 0xffffe040UL
    };
    TQAImage image;
    TQATexture *texture = NULL;
    TQADrawContext *context = NULL;
    UnsignedWide start;
    UnsignedWide end;
    unsigned long frame;
    TQAError error;

    if (device == NULL || deviceRect == NULL || engine == NULL ||
        elapsedMicroseconds == NULL) {
        return kQAParamErr;
    }
    image.width = 2;
    image.height = 2;
    image.rowBytes = 8;
    image.pixmap = texturePixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_ARGB32,
                         &image, &texture);
    if (error != kQANoErr) {
        return error;
    }
    error = QATextureDetach(engine, texture);
    if (error == kQANoErr) {
        error = QADrawContextNew(device, deviceRect, NULL, engine,
                                 kQAContext_DoubleBuffer, &context);
    }
    for (frame = 0;
         error == kQANoErr && frame < GXMETAL_BENCHMARK_WARMUP_FRAMES;
         frame++) {
        error = GXMetalBenchmarkFrame(context, texture, frame);
    }
    if (error == kQANoErr) {
        Microseconds(&start);
    }
    for (frame = 0;
         error == kQANoErr && frame < GXMETAL_BENCHMARK_FRAMES;
         frame++) {
        error = GXMetalBenchmarkFrame(context, texture, frame);
    }
    if (error == kQANoErr) {
        Microseconds(&end);
        *elapsedMicroseconds = GXMetalMicrosecondValue(&end) -
                               GXMetalMicrosecondValue(&start);
        if (*elapsedMicroseconds == 0) {
            error = kQAError;
        }
    }
    if (context != NULL) {
        QADrawContextDelete(context);
    }
    QATextureDelete(engine, texture);
    return error;
}

static TQAError GXMetalBenchmarkSoftware(const TQADevice *device,
                                         const TQARect *deviceRect,
                                         const TQAEngine *gxMetalEngine,
                                         uint64_t *elapsedMicroseconds,
                                         char *engineName,
                                         size_t engineNameCapacity)
{
    TQAEngine *engine = QADeviceGetFirstEngine(device);

    while (engine != NULL) {
        char name[64];

        if (engine != gxMetalEngine &&
            GXMetalGetEngineName(engine, name, sizeof(name)) &&
            strcmp(name, "GXMetal") != 0 &&
            GXMetalBenchmarkEngine(device, deviceRect, engine,
                                   elapsedMicroseconds) == kQANoErr) {
            strncpy(engineName, name, engineNameCapacity - 1);
            engineName[engineNameCapacity - 1] = '\0';
            return kQANoErr;
        }
        engine = QADeviceGetNextEngine(device, engine);
    }
    return kQANotSupported;
}

static void GXMetalBuildPassResult(char *result, size_t resultCapacity,
                                   uint32_t revision,
                                   uint64_t gxMetalMicroseconds,
                                   uint64_t softwareMicroseconds,
                                   uint64_t speedupTimes100)
{
    char *cursor = result;
    const char *end = result + resultCapacity - 1;

    GXMetalAppendText(&cursor, end, "PASS: version=");
    GXMetalAppendVersion(&cursor, end, revision);
    GXMetalAppendText(&cursor, end,
        " RAVE discovery capability-contract depth perspective-z blend alpha-test chromakey backface clip texture intensity-formats acl16-88 alpha1-texture-byte+bitmap-packed cl4 rgb8-332 rgb24 public-multitexture dynamic-resources ATI-private-nocopy ATI-private-update ATI-private-readback large-mesh-batches bitmap bitmap-scale dirty-present double-buffer framebuffer gx_us=");
    GXMetalAppendDecimal(&cursor, end, gxMetalMicroseconds);
    GXMetalAppendText(&cursor, end, " sw_us=");
    GXMetalAppendDecimal(&cursor, end, softwareMicroseconds);
    GXMetalAppendText(&cursor, end, " speedup_x100=");
    GXMetalAppendDecimal(&cursor, end, speedupTimes100);
    *cursor = '\0';
}

static void GXMetalBuildPassMessage(char *message, size_t messageCapacity,
                                    uint32_t revision,
                                    const char *softwareEngineName,
                                    uint64_t speedupTimes100)
{
    char *cursor = message;
    const char *end = message + messageCapacity - 1;

    GXMetalAppendText(&cursor, end, "GXMetal ");
    GXMetalAppendVersion(&cursor, end, revision);
    GXMetalAppendText(&cursor, end,
        " passed RAVE discovery, capability, texture/bitmap format, multitexture, dynamic-resource, framebuffer, ATI compatibility, presentation, and software-fallback checks. The matched workload ran ");
    GXMetalAppendDecimal(&cursor, end, speedupTimes100 / 100);
    GXMetalAppendText(&cursor, end, ".");
    if (speedupTimes100 % 100 < 10) {
        GXMetalAppendText(&cursor, end, "0");
    }
    GXMetalAppendDecimal(&cursor, end, speedupTimes100 % 100);
    GXMetalAppendText(&cursor, end, "x faster than ");
    GXMetalAppendText(&cursor, end, softwareEngineName);
    GXMetalAppendText(&cursor, end, ".");
    *cursor = '\0';
}

static void GXMetalBuildVersionMismatch(char *message,
                                        size_t messageCapacity,
                                        uint32_t revision)
{
    char *cursor = message;
    const char *end = message + messageCapacity - 1;

    GXMetalAppendText(&cursor, end, "GXMetal Test ");
    GXMetalAppendText(&cursor, end, GXMETAL_PRODUCT_VERSION_STRING);
    GXMetalAppendText(&cursor, end,
        " found installed GXMetal driver revision ");
    GXMetalAppendVersion(&cursor, end, revision);
    GXMetalAppendText(&cursor, end, ". Install GXMetal ");
    GXMetalAppendText(&cursor, end, GXMETAL_PRODUCT_VERSION_STRING);
    GXMetalAppendText(&cursor, end, " and restart Mac OS before testing.");
    *cursor = '\0';
}

int main(void)
{
    static const char kWindowTitleText[] =
        "GXMetal Test " GXMETAL_PRODUCT_VERSION_STRING;
    Str255 windowTitle;
    Rect windowRect;
    Rect localRect;
    WindowPtr window;
    RGBColor green = {0, 0xffff, 0};
    TQADevice device;
    TQAClip clip;
    TQAClip complexClip;
    RgnHandle clipRegion = NULL;
    RgnHandle complexRegion = NULL;
    RgnHandle complexPart = NULL;
    TQAEngine *engine;
    CFragConnectionID diagnosticConnection = NULL;
    TQARect deviceRect;
    TQADrawContext *context = NULL;
    TQADrawContext *unexpectedContext = NULL;
    unsigned long optionalFeatures = 0;
    unsigned long optionalFeatures2 = 0;
    unsigned long fastFeatures = 0;
    unsigned long textureMemory = 0;
    unsigned long fastTextureMemory = 0;
    unsigned long multiTextureMax = 0;
    unsigned long drawPixelTypes = 0;
    unsigned long preferredDrawPixelTypes = 0;
    unsigned long texturePixelTypes = 0;
    unsigned long preferredTexturePixelTypes = 0;
    unsigned long bitmapPixelTypes = 0;
    unsigned long preferredBitmapPixelTypes = 0;
    unsigned long unknownGestaltResponse = 0x47584d54UL;
    unsigned long vendorID = 0;
    unsigned long engineID = 0;
    unsigned long revision = 0;
    unsigned long requiredFeatures;
    unsigned long requiredFeatures2;
    unsigned long requiredFastFeatures;
    unsigned long requiredDrawPixelTypes;
    unsigned long requiredTexturePixelTypes;
    unsigned long requiredBitmapPixelTypes;
    TQAError error;
    OSErr loadError;
    int32_t diagnosticStatus;
    int32_t probeStatus;
    GXMetalDiagnosticSnapshot diagnosticSnapshot;
    GXMetalDiagnosticSnapshot publishedSnapshot;
    Boolean automaticLoad;
    uint64_t gxMetalMicroseconds = 0;
    uint64_t softwareMicroseconds = 0;
    uint64_t speedupTimes100;
    char softwareEngineName[64];
    char passResult[448];
    char passMessage[320];
    char versionMessage[256];

    GXMetalInitToolbox();
    GXMetalRecordResult("START: GXMetal Test entered main");
    error = QAInit();
    if (error != kQANoErr) {
        GXMetalRecordResult("FAIL: QAInit could not initialize RAVE");
        GXMetalShowResult(false,
            "The installed QuickDraw 3D RAVE manager could not initialize.");
        return 1;
    }
    GXMetalRecordResult("RAVE: QAInit succeeded");
    memset(&publishedSnapshot, 0, sizeof(publishedSnapshot));
    if (GXMetalReadPublishedDiagnostics(&publishedSnapshot)) {
        GXMetalRecordDiagnosticSnapshot(&publishedSnapshot, -1, true);
    }
    /* Always snapshot the live fragment before this app changes any driver
     * state. This also turns GXMetal Test into a post-crash diagnostic probe
     * for games which leave the RAVE manager running but unstable. */
    loadError = GXMetalLoadInstalledExtension(&diagnosticConnection);
    if (loadError == noErr && diagnosticConnection != NULL) {
        memset(&diagnosticSnapshot, 0, sizeof(diagnosticSnapshot));
        if (GXMetalCopyDriverDiagnostics(diagnosticConnection,
                                         &diagnosticSnapshot) == kQANoErr) {
            GXMetalRecordDiagnosticSnapshot(&diagnosticSnapshot, -1, true);
        }
        CloseConnection(&diagnosticConnection);
        diagnosticConnection = NULL;
    }
    SetRect(&windowRect, 70, 58, 70 + GXMETAL_WIDTH,
            58 + GXMETAL_HEIGHT);
    GXMetalCStringToPascal(kWindowTitleText, windowTitle);
    window = NewCWindow(NULL, &windowRect, windowTitle, true,
                        documentProc, (WindowPtr)-1, false, 0);
    if (window == NULL) {
        GXMetalRecordResult("FAIL: test window creation");
        GXMetalShowResult(false, "The GXMetal test window could not be created.");
        QAExit();
        return 1;
    }
    SetPort(window);

    memset(&device, 0, sizeof(device));
    device.deviceType = kQADeviceGDevice;
    device.device.gDevice = GetMainDevice();
    GXMetalRecordRAVEInventory(&device);
    engine = GXMetalFindEngine(&device);
    if (engine == NULL) {
        memset(&publishedSnapshot, 0, sizeof(publishedSnapshot));
        automaticLoad = GXMetalReadPublishedDiagnostics(&publishedSnapshot);
        loadError = GXMetalLoadInstalledExtension(&diagnosticConnection);
        if (loadError != noErr) {
            GXMetalRecordResult("FAIL: GXMetal CFM fragment could not load");
            DisposeWindow(window);
            GXMetalShowResult(false,
                "GXMetal was not discovered by RAVE, and CFM could not load its installed fragment.");
            QAExit();
            return 1;
        }
        memset(&diagnosticSnapshot, 0, sizeof(diagnosticSnapshot));
        diagnosticStatus = GXMetalCopyDriverDiagnostics(
            diagnosticConnection, &diagnosticSnapshot);
        probeStatus = GXMetalProbeDriverTransport(diagnosticConnection);
        engine = GXMetalFindEngine(&device);
        if (engine != NULL) {
            GXMetalRecordResult("FAIL: GXMetal required manual CFM loading");
            GXMetalShowResult(false,
                "GXMetal's CFM initializer works, but RAVE did not discover the extension automatically at startup.");
            CloseConnection(&diagnosticConnection);
            DisposeWindow(window);
            QAExit();
            return 1;
        }
        if (diagnosticStatus != kQANoErr) {
            diagnosticStatus = GXMetalReadDriverDiagnostic(diagnosticConnection);
        } else {
            diagnosticStatus = diagnosticSnapshot.status;
            GXMetalRecordDiagnosticSnapshot(&diagnosticSnapshot, probeStatus,
                                             automaticLoad);
        }
        if (diagnosticSnapshot.magic == GXMETAL_DIAGNOSTIC_MAGIC) {
            /* The complete snapshot above is intentionally the final host
             * record. Keep the human-readable branches for older drivers. */
        } else if (diagnosticStatus == kGXMetalDiagnosticRegistrationFailed) {
            GXMetalRecordResult("FAIL: QARegisterEngine rejected GXMetal");
        } else if (diagnosticStatus == kGXMetalDiagnosticTransportUnavailable) {
            GXMetalRecordResult("FAIL: GXMetal transport unavailable in CheckDevice");
        } else if (diagnosticStatus == kGXMetalDiagnosticInvalidDevice) {
            GXMetalRecordResult("FAIL: RAVE supplied an invalid GXMetal device");
        } else if (diagnosticStatus == kGXMetalDiagnosticDisplayRejected) {
            GXMetalRecordResult("FAIL: GXMetal rejected the display mapping");
        } else if (diagnosticStatus == kGXMetalDiagnosticRegistryUnavailable) {
            GXMetalRecordResult("FAIL: AAPL,GXMetal Name Registry property unavailable");
        } else if (diagnosticStatus == kGXMetalDiagnosticRegistryReady) {
            GXMetalRecordResult("FAIL: Name Registry ready; GXMetal transport probe bypassed");
        } else if (diagnosticStatus == kGXMetalDiagnosticTransportReady) {
            GXMetalRecordResult("FAIL: GXMetal transport ready but RAVE device association missing");
        } else if (diagnosticStatus == kGXMetalDiagnosticTransportConnecting) {
            GXMetalRecordResult("FAIL: GXMetal faulted while connecting transport MMIO");
        } else if (diagnosticStatus == kGXMetalDiagnosticTransportConnectionFailed) {
            GXMetalRecordResult("FAIL: GXMetal transport MMIO or feature negotiation failed");
        } else if (diagnosticStatus == kGXMetalDiagnosticRegistered) {
            GXMetalRecordResult("FAIL: GXMetal registered but CheckDevice was not called");
        } else {
            GXMetalRecordResult("FAIL: GXMetal driver diagnostic unavailable");
        }
        CloseConnection(&diagnosticConnection);
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal loaded, but RAVE rejected it for the main display. The host result file identifies the failed registration stage.");
        QAExit();
        return 1;
    }
    GXMetalRecordResult("RAVE: GXMetal enumerated for main device");
    if (QAEngineGestalt(engine, kQAGestalt_OptionalFeatures,
                        &optionalFeatures) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_OptionalFeatures2,
                        &optionalFeatures2) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_FastFeatures,
                        &fastFeatures) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_TextureMemory,
                        &textureMemory) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_FastTextureMemory,
                        &fastTextureMemory) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_MultiTextureMax,
                        &multiTextureMax) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_DrawContextPixelTypesAllowed,
                        &drawPixelTypes) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_DrawContextPixelTypesPreferred,
                        &preferredDrawPixelTypes) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_TexturePixelTypesAllowed,
                        &texturePixelTypes) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_TexturePixelTypesPreferred,
                        &preferredTexturePixelTypes) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_BitmapPixelTypesAllowed,
                        &bitmapPixelTypes) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_BitmapPixelTypesPreferred,
                        &preferredBitmapPixelTypes) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_VendorID,
                        &vendorID) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_EngineID,
                        &engineID) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_Revision,
                        &revision) != kQANoErr) {
        GXMetalRecordResult("FAIL: RAVE feature gestalt");
        DisposeWindow(window);
        GXMetalShowResult(false, "GXMetal did not return its RAVE feature set.");
        QAExit();
        return 1;
    }
    if (vendorID != 1) { /* kQAVendor_ATI */
        GXMetalRecordResult("FAIL: legacy RAVE vendor compatibility");
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal did not advertise the ATI-compatible RAVE identity required by legacy game launchers.");
        QAExit();
        return 1;
    }
    if (engineID != GXMETAL_ATI_ENGINE_ID ||
        !gxmetal_ati_legacy_generation_is_current(
            (uint32_t)engineID, (uint32_t)revision)) {
        GXMetalRecordResult("FAIL: ATI-compatible engine generation");
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal's ATI-compatible RAVE identity is too old for later classic game launchers.");
        QAExit();
        return 1;
    }
    if ((uint32_t)revision != GXMETAL_PRODUCT_REVISION) {
        GXMetalBuildVersionMismatch(versionMessage, sizeof(versionMessage),
                                    (uint32_t)revision);
        GXMetalRecordResult(versionMessage);
        DisposeWindow(window);
        GXMetalShowResult(false, versionMessage);
        QAExit();
        return 1;
    }
    requiredFeatures = kQAOptional_BoundToDevice | kQAOptional_NoDither |
                       kQAOptional_ClearDrawBuffer | kQAOptional_OpenGL |
                       kQAOptional_PerspectiveZ | kQAOptional_Blend |
                       kQAOptional_BlendAlpha | kQAOptional_Texture |
                       kQAOptional_TextureHQ | kQAOptional_TextureColor |
                       kQAOptional_CL4 | kQAOptional_CL8 |
                       kQAOptional_ChannelMask |
                       kQAOptional_ZBufferMask |
                       kQAOptional_ClearZBuffer | kQAOptional_FogDepth |
                       kQAOptional_AlphaTest |
                       kQAOptional_MultiTextures |
                       kQAOptional_AccessTexture |
                       kQAOptional_AccessBitmap |
                       kQAOptional_AccessDrawBuffer;
    requiredFeatures2 = kQAOptional2_SwapBuffers | kQAOptional2_Chromakey |
                        kQAOptional2_FlipOrigin | kQAOptional2_BitmapScale;
    requiredFastFeatures = kQAFast_Line | kQAFast_Gouraud |
                           kQAFast_Blend | kQAFast_Texture |
                           kQAFast_TextureHQ | kQAFast_CL4 | kQAFast_CL8 |
                           kQAFast_FogDepth | kQAFast_MultiTextures |
                           kQAFast_BitmapScale;
    if (optionalFeatures != requiredFeatures ||
        optionalFeatures2 != requiredFeatures2 ||
        fastFeatures != requiredFastFeatures || multiTextureMax != 1) {
        GXMetalRecordResult("FAIL: inaccurate RAVE capability declaration");
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal's advertised RAVE features do not exactly match its tested capability contract.");
        QAExit();
        return 1;
    }
    requiredDrawPixelTypes = (1UL << kQAPixel_RGB16) |
                             (1UL << kQAPixel_RGB32) |
                             (1UL << kQAPixel_ARGB32);
    requiredTexturePixelTypes = (1UL << kQAPixel_RGB16) |
                                (1UL << kQAPixel_Alpha1) |
                                (1UL << kQAPixel_ARGB16) |
                                (1UL << kQAPixel_RGB32) |
                                (1UL << kQAPixel_RGB24) |
                                (1UL << kQAPixel_ARGB32) |
                                (1UL << kQAPixel_CL4) |
                                (1UL << kQAPixel_CL8) |
                                (1UL << kQAPixel_ARGB16_4444) |
                                (1UL << kQAPixel_ACL16_88) |
                                (1UL << kQAPixel_RGB8_332) |
                                (1UL << kQAPixel_I8) |
                                (1UL << kQAPixel_AI16_88);
    requiredBitmapPixelTypes = (1UL << kQAPixel_RGB16) |
                               (1UL << kQAPixel_Alpha1) |
                               (1UL << kQAPixel_RGB32) |
                               (1UL << kQAPixel_RGB24) |
                               (1UL << kQAPixel_ARGB32) |
                               (1UL << kQAPixel_CL4) |
                               (1UL << kQAPixel_CL8) |
                               (1UL << kQAPixel_ACL16_88) |
                               (1UL << kQAPixel_RGB8_332) |
                               (1UL << kQAPixel_I8) |
                               (1UL << kQAPixel_AI16_88);
    if (textureMemory == 0 || fastTextureMemory != textureMemory ||
        drawPixelTypes != requiredDrawPixelTypes ||
        preferredDrawPixelTypes != requiredDrawPixelTypes ||
        texturePixelTypes != requiredTexturePixelTypes ||
        preferredTexturePixelTypes != requiredTexturePixelTypes ||
        bitmapPixelTypes != requiredBitmapPixelTypes ||
        preferredBitmapPixelTypes != requiredBitmapPixelTypes) {
        GXMetalRecordResult("FAIL: inaccurate RAVE resource declaration");
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal's texture memory or pixel-format declaration does not match its tested resource contract.");
        QAExit();
        return 1;
    }
    if (QAEngineGestalt(engine, (TQAGestaltSelector)999,
                        &unknownGestaltResponse) != kQAGestaltUnknown ||
        unknownGestaltResponse != 0x47584d54UL) {
        GXMetalRecordResult("FAIL: unsafe unknown RAVE gestalt probe");
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal did not safely decline an unknown RAVE capability probe.");
        QAExit();
        return 1;
    }

    deviceRect.left = windowRect.left;
    deviceRect.right = windowRect.right;
    deviceRect.top = windowRect.top;
    deviceRect.bottom = windowRect.bottom;
    SetRect(&localRect, 0, 0, GXMETAL_WIDTH, GXMETAL_HEIGHT);
    RGBForeColor(&green);
    PaintRect(&localRect);
    ForeColor(blackColor);
    complexRegion = NewRgn();
    complexPart = NewRgn();
    if (complexRegion == NULL || complexPart == NULL) {
        error = kQAOutOfMemory;
    } else {
        /* A complex source region whose intersection with the draw surface
         * is rectangular must be accepted.  QuickDraw commonly produces
         * this shape for fullscreen windows with unrelated visible runs. */
        SetRectRgn(complexRegion, (short)windowRect.left,
                   (short)windowRect.top, (short)windowRect.right,
                   (short)windowRect.bottom);
        SetRectRgn(complexPart, (short)(windowRect.right + 8),
                   (short)windowRect.top, (short)(windowRect.right + 24),
                   (short)(windowRect.top + 16));
        UnionRgn(complexRegion, complexPart, complexRegion);
        memset(&complexClip, 0, sizeof(complexClip));
        complexClip.clipType = kQAClipRgn;
        complexClip.clip.clipRgn = complexRegion;
        error = QADrawContextNew(&device, &deviceRect, &complexClip, engine,
                                 kQAContext_DoubleBuffer,
                                 &unexpectedContext);
        if (error == kQANoErr && unexpectedContext != NULL) {
            QADrawContextDelete(unexpectedContext);
            unexpectedContext = NULL;
        } else {
            error = kQAError;
        }
    }
    if (error == kQANoErr) {
        /* A genuinely disjoint intersection exercises the exact region-list
         * transport rather than falling back to its bounding rectangle. */
        SetRectRgn(complexRegion, (short)windowRect.left,
                   (short)windowRect.top, (short)(windowRect.left + 16),
                   (short)(windowRect.top + 16));
        SetRectRgn(complexPart, (short)(windowRect.left + 32),
                   (short)windowRect.top, (short)(windowRect.left + 48),
                   (short)(windowRect.top + 16));
        UnionRgn(complexRegion, complexPart, complexRegion);
        memset(&complexClip, 0, sizeof(complexClip));
        complexClip.clipType = kQAClipRgn;
        complexClip.clip.clipRgn = complexRegion;
        error = QADrawContextNew(&device, &deviceRect, &complexClip, engine,
                                 kQAContext_DoubleBuffer,
                                 &unexpectedContext);
        if (error == kQANoErr && unexpectedContext != NULL) {
            QADrawContextDelete(unexpectedContext);
            unexpectedContext = NULL;
        } else {
            error = kQAError;
        }
    }
    if (complexPart != NULL) {
        DisposeRgn(complexPart);
    }
    if (complexRegion != NULL) {
        DisposeRgn(complexRegion);
    }
    clipRegion = NewRgn();
    if (error == kQANoErr && clipRegion == NULL) {
        error = kQAOutOfMemory;
    } else if (error == kQANoErr) {
        SetRectRgn(clipRegion, (short)windowRect.left,
                   (short)(windowRect.top + 8), (short)windowRect.right,
                   (short)windowRect.bottom);
        memset(&clip, 0, sizeof(clip));
        clip.clipType = kQAClipRgn;
        clip.clip.clipRgn = clipRegion;
        error = QADrawContextNew(&device, &deviceRect, &clip, engine,
                                 kQAContext_DoubleBuffer, &context);
    }
    if (error == kQANoErr) {
        error = GXMetalRenderDrawBufferWriteback(
            context, device.device.gDevice, &deviceRect);
    }
    if (error == kQANoErr) {
        error = GXMetalRenderPattern(context, engine,
                                     device.device.gDevice, &deviceRect);
        if (error == kQANoErr) {
            error = GXMetalRenderPublicMultiTexture(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderATITextureMutation(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderATITextureUpdate(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderDynamicResources(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderIntensityFormats(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderAlphaPaletteFormat(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderAlpha1Format(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderCL4Format(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderRGB332Format(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderRGB24Format(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderBitmapScale(
                context, engine, device.device.gDevice, &deviceRect);
        }
        if (error == kQANoErr) {
            error = GXMetalRenderLargeMeshBatches(
                context, engine, device.device.gDevice, &deviceRect);
        }
    } else {
        GXMetalRecordResult("FAIL: accelerated draw context creation");
    }
    if (context != NULL) {
        QADrawContextDelete(context);
    }
    if (clipRegion != NULL) {
        DisposeRgn(clipRegion);
    }
    if (error != kQANoErr) {
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal was selected, but the depth/texture/multitexture/double-buffer render test did not complete.");
        QAExit();
        return 1;
    }
    error = GXMetalBenchmarkEngine(&device, &deviceRect, engine,
                                   &gxMetalMicroseconds);
    if (error == kQANoErr) {
        error = GXMetalBenchmarkSoftware(&device, &deviceRect, engine,
                                         &softwareMicroseconds,
                                         softwareEngineName,
                                         sizeof(softwareEngineName));
    }
    if (error != kQANoErr || gxMetalMicroseconds == 0) {
        GXMetalRecordResult("FAIL: GXMetal or software fallback benchmark");
        DisposeWindow(window);
        GXMetalShowResult(false,
            "Framebuffer correctness passed, but the matched GXMetal/software fallback benchmark could not complete.");
        QAExit();
        return 1;
    }
    speedupTimes100 = softwareMicroseconds * 100 / gxMetalMicroseconds;
    GXMetalBuildPassResult(passResult, sizeof(passResult), (uint32_t)revision,
                           gxMetalMicroseconds, softwareMicroseconds,
                           speedupTimes100);
    GXMetalBuildPassMessage(passMessage, sizeof(passMessage),
                            (uint32_t)revision,
                            softwareEngineName, speedupTimes100);
    GXMetalRecordResult(passResult);
    GXMetalShowResult(true, passMessage);
    DisposeWindow(window);
    QAExit();
    return 0;
}
