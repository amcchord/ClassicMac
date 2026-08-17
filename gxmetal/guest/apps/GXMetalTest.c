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

static const unsigned char kGXMetalResultName[] = {
    20, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'T', 'e', 's', 't', ' ',
    'R', 'e', 's', 'u', 'l', 't', 's'
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
static void GXMetalRecordResult(const char *message)
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
    (void)FSMakeFSSpec(volume, directory, kGXMetalResultName, &result);
    (void)FSpDelete(&result);
    if (FSpCreate(&result, 'GXMT', 'TEXT', smSystemScript) != noErr ||
        FSpOpenDF(&result, fsWrPerm, &refNum) != noErr) {
        return;
    }
    (void)FSWrite(refNum, &length, message);
    (void)FSClose(refNum);
    (void)FlushVol(NULL, volume);
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

static void GXMetalRecordDiagnosticSnapshot(
    const GXMetalDiagnosticSnapshot *snapshot, int32_t probeStatus,
    Boolean automaticLoad)
{
    char result[240];
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
    return red > maximum / 6 && red < maximum * 2 / 5 &&
           blue > maximum * 3 / 5 && green < maximum / 5;
}

static TQAError GXMetalRenderPattern(TQADrawContext *context,
                                     const TQAEngine *engine,
                                     GDHandle graphicsDevice,
                                     const TQARect *deviceRect)
{
    static unsigned long texturePixels[4] = {
        0xffff0000UL, 0xff00ff00UL,
        0xff0000ffUL, 0xffffffffUL
    };
    static unsigned long bitmapPixels[16] = {
        0xffff0000UL, 0xffff0000UL, 0xffff0000UL, 0xffff0000UL,
        0xffff0000UL, 0xffff0000UL, 0xffff0000UL, 0xffff0000UL,
        0xff00ff00UL, 0xff00ff00UL, 0xff00ff00UL, 0xff00ff00UL,
        0xff00ff00UL, 0xff00ff00UL, 0xff00ff00UL, 0xff00ff00UL
    };
    TQAImage image;
    TQAImage bitmapImage;
    TQATexture *texture = NULL;
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
    TQAVGouraud fogTriangle[3];
    TQAVGouraud bitmapVertex;
    TQAVTexture backfaceTextureOriented[3];
    TQAVTexture texturedQuad[4];
    unsigned long backfaceFlags[1] = {kQATriFlags_Backfacing};
    unsigned long flags[4] = {0, 0, 0, 0};
    TQAError error;

    image.width = 2;
    image.height = 2;
    image.rowBytes = 8;
    image.pixmap = texturePixels;
    error = QATextureNew(engine, kQATexture_None, kQAPixel_ARGB32,
                         &image, &texture);
    if (error != kQANoErr) {
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
    fogTriangle[0] = GXMetalGouraud(210.0f, 232.0f, 0.75f,
                                    1.0f, 0.0f, 0.0f, 1.0f);
    fogTriangle[1] = GXMetalGouraud(240.0f, 200.0f, 0.75f,
                                    1.0f, 0.0f, 0.0f, 1.0f);
    fogTriangle[2] = GXMetalGouraud(270.0f, 232.0f, 0.75f,
                                    1.0f, 0.0f, 0.0f, 1.0f);
    /* RAVE depth fog uses 1/invW, not the normalized Z-buffer coordinate.
     * Keep the historical 0.75 fog depth while making Z deliberately near. */
    fogTriangle[0].z = fogTriangle[1].z = fogTriangle[2].z = 0.10f;
    fogTriangle[0].invW = fogTriangle[1].invW = fogTriangle[2].invW =
        1.0f / 0.75f;

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
    QASetPtr(context, kQATag_Texture, texture);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QADrawVTexture(context, 3, kQAVertexMode_Tri,
                   backfaceTextureOriented, backfaceFlags);
    QADrawVTexture(context, 4, kQAVertexMode_Strip,
                   texturedQuad, flags);
    QADrawBitmap(context, &bitmapVertex, bitmap);
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
                                    deviceRect->left + 160,
                                    deviceRect->top + 4,
                                    kGXMetalPixelGreen)) {
        GXMetalRecordResult("FAIL: immutable clip pixel");
        error = kQAError;
    }
    QABitmapDelete(engine, bitmap);
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
                                   uint64_t gxMetalMicroseconds,
                                   uint64_t softwareMicroseconds,
                                   uint64_t speedupTimes100)
{
    char *cursor = result;
    const char *end = result + resultCapacity - 1;

    GXMetalAppendText(&cursor, end,
        "PASS: RAVE discovery depth blend alpha-test backface clip texture bitmap dirty-present double-buffer framebuffer gx_us=");
    GXMetalAppendDecimal(&cursor, end, gxMetalMicroseconds);
    GXMetalAppendText(&cursor, end, " sw_us=");
    GXMetalAppendDecimal(&cursor, end, softwareMicroseconds);
    GXMetalAppendText(&cursor, end, " speedup_x100=");
    GXMetalAppendDecimal(&cursor, end, speedupTimes100);
    *cursor = '\0';
}

static void GXMetalBuildPassMessage(char *message, size_t messageCapacity,
                                    const char *softwareEngineName,
                                    uint64_t speedupTimes100)
{
    char *cursor = message;
    const char *end = message + messageCapacity - 1;

    GXMetalAppendText(&cursor, end,
        "GXMetal passed automatic RAVE discovery, framebuffer correctness, bitmap drawing, presentation, and fallback. The repeatable mixed texture/Gouraud workload ran ");
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

int main(void)
{
    static const unsigned char kWindowTitle[] = {
        12, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'T', 'e', 's', 't'
    };
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
    unsigned long requiredFeatures;
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
    char passResult[240];
    char passMessage[256];

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
    SetRect(&windowRect, 70, 58, 70 + GXMETAL_WIDTH,
            58 + GXMETAL_HEIGHT);
    window = NewCWindow(NULL, &windowRect, kWindowTitle, true,
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
                        &optionalFeatures2) != kQANoErr) {
        GXMetalRecordResult("FAIL: RAVE feature gestalt");
        DisposeWindow(window);
        GXMetalShowResult(false, "GXMetal did not return its RAVE feature set.");
        QAExit();
        return 1;
    }
    requiredFeatures = kQAOptional_Texture | kQAOptional_TextureHQ |
                       kQAOptional_Blend | kQAOptional_ClearDrawBuffer |
                       kQAOptional_ClearZBuffer | kQAOptional_FogDepth |
                       kQAOptional_AlphaTest;
    if ((optionalFeatures & requiredFeatures) != requiredFeatures ||
        (optionalFeatures2 & kQAOptional2_SwapBuffers) == 0) {
        GXMetalRecordResult("FAIL: incomplete RAVE feature set");
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal registered, but the host did not expose the complete depth, fog, alpha-test, blend, texture, and double-buffer feature set.");
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
        if (unexpectedContext != NULL) {
            QADrawContextDelete(unexpectedContext);
            unexpectedContext = NULL;
        }
        error = error == kQANotSupported ? kQANoErr : kQAError;
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
        error = GXMetalRenderPattern(context, engine,
                                     device.device.gDevice, &deviceRect);
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
            "GXMetal was selected, but the depth/texture/double-buffer render test did not complete.");
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
    GXMetalBuildPassResult(passResult, sizeof(passResult),
                           gxMetalMicroseconds, softwareMicroseconds,
                           speedupTimes100);
    GXMetalBuildPassMessage(passMessage, sizeof(passMessage),
                            softwareEngineName, speedupTimes100);
    GXMetalRecordResult(passResult);
    GXMetalShowResult(true, passMessage);
    DisposeWindow(window);
    QAExit();
    return 0;
}
