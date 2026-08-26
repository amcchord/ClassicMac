/*
 * GXMetal AGL Probe
 *
 * A small Classic Mac OS application which exercises Apple's public AGL and
 * OpenGL path without importing OpenGLLibrary at launch time. Dynamic CFM
 * discovery is deliberate: a missing or damaged OpenGL installation becomes
 * a machine-readable probe result instead of a Finder launch failure.
 */

#include <CodeFragments.h>
#include <Dialogs.h>
#include <Files.h>
#include <Folders.h>
#include <Fonts.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>

#include <stdint.h>
#include <string.h>

#include "GXMetalAGLProbeLogic.h"
#include "GXMetalVersion.h"

#define GXMETAL_AGL_ALERT_ID 128
#define GXMETAL_AGL_WIDTH 320
#define GXMETAL_AGL_HEIGHT 220
#define GXMETAL_AGL_REPORT_CAPACITY 3072

/* Minimal declarations from Apple's OpenGL 1.2 SDK. Keeping the probe on a
 * dynamic CFM boundary avoids adding a second SDK/toolchain dependency to the
 * normal GXMetal build. Classic OpenGL uses 32-bit long integers. */
typedef unsigned long GXGLenum;
typedef unsigned char GXGLboolean;
typedef unsigned long GXGLbitfield;
typedef long GXGLint;
typedef long GXGLsizei;
typedef unsigned char GXGLubyte;
typedef unsigned long GXGLuint;
typedef float GXGLfloat;
typedef double GXGLdouble;
typedef void GXGLvoid;
typedef GDHandle GXAGLDevice;
typedef CGrafPtr GXAGLDrawable;
typedef struct GXAGLPixelFormatRec *GXAGLPixelFormat;
typedef struct GXAGLContextRec *GXAGLContext;

enum {
    GX_AGL_NONE = 0,
    GX_AGL_ALL_RENDERERS = 1,
    GX_AGL_RGBA = 4,
    GX_AGL_DOUBLEBUFFER = 5,
    GX_AGL_DEPTH_SIZE = 12,
    GX_AGL_PIXEL_SIZE = 50,
    GX_AGL_RENDERER_ID = 70,
    GX_AGL_ACCELERATED = 73,
    GX_GL_NO_ERROR = 0,
    GX_GL_TRIANGLES = 0x0004,
    GX_GL_TRIANGLE_STRIP = 0x0005,
    GX_GL_TRIANGLE_FAN = 0x0006,
    GX_GL_QUADS = 0x0007,
    GX_GL_QUAD_STRIP = 0x0008,
    GX_GL_POLYGON = 0x0009,
    GX_GL_DEPTH_BUFFER_BIT = 0x00000100,
    GX_GL_COLOR_BUFFER_BIT = 0x00004000,
    GX_GL_LESS = 0x0201,
    GX_GL_SRC_ALPHA = 0x0302,
    GX_GL_ONE_MINUS_SRC_ALPHA = 0x0303,
    GX_GL_TEXTURE_MAG_FILTER = 0x2800,
    GX_GL_TEXTURE_MIN_FILTER = 0x2801,
    GX_GL_NEAREST = 0x2600,
    GX_GL_LINEAR = 0x2601,
    GX_GL_LINEAR_MIPMAP_LINEAR = 0x2703,
    GX_GL_BLEND = 0x0be2,
    GX_GL_DEPTH_TEST = 0x0b71,
    GX_GL_TEXTURE_2D = 0x0de1,
    GX_GL_UNSIGNED_BYTE = 0x1401,
    GX_GL_MODELVIEW = 0x1700,
    GX_GL_PROJECTION = 0x1701,
    GX_GL_RGB = 0x1907,
    GX_GL_RGBA = 0x1908,
    GX_GL_VENDOR = 0x1f00,
    GX_GL_RENDERER = 0x1f01,
    GX_GL_VERSION = 0x1f02,
    GX_GL_EXTENSIONS = 0x1f03,
    GX_GL_MAX_LIGHTS = 0x0d31,
    GX_GL_MAX_TEXTURE_SIZE = 0x0d33,
    GX_GL_TEXTURE0_ARB = 0x84c0,
    GX_GL_TEXTURE1_ARB = 0x84c1,
    GX_GL_MAX_TEXTURE_UNITS_ARB = 0x84e2
};

typedef GXAGLPixelFormat (*GXAGLChoosePixelFormatProc)(
    const GXAGLDevice *, GXGLint, const GXGLint *);
typedef void (*GXAGLDestroyPixelFormatProc)(GXAGLPixelFormat);
typedef GXGLboolean (*GXAGLDescribePixelFormatProc)(
    GXAGLPixelFormat, GXGLint, GXGLint *);
typedef GXAGLContext (*GXAGLCreateContextProc)(GXAGLPixelFormat,
                                               GXAGLContext);
typedef GXGLboolean (*GXAGLDestroyContextProc)(GXAGLContext);
typedef GXGLboolean (*GXAGLSetCurrentContextProc)(GXAGLContext);
typedef GXGLboolean (*GXAGLSetDrawableProc)(GXAGLContext, GXAGLDrawable);
typedef void (*GXAGLSwapBuffersProc)(GXAGLContext);
typedef void (*GXAGLGetVersionProc)(GXGLint *, GXGLint *);
typedef GXGLenum (*GXAGLGetErrorProc)(void);
typedef const GXGLubyte *(*GXAGLErrorStringProc)(GXGLenum);
typedef const GXGLubyte *(*GXGLGetStringProc)(GXGLenum);
typedef void (*GXGLGetIntegervProc)(GXGLenum, GXGLint *);
typedef void (*GXGLViewportProc)(GXGLint, GXGLint, GXGLsizei, GXGLsizei);
typedef void (*GXGLMatrixModeProc)(GXGLenum);
typedef void (*GXGLLoadIdentityProc)(void);
typedef void (*GXGLOrthoProc)(GXGLdouble, GXGLdouble, GXGLdouble,
                              GXGLdouble, GXGLdouble, GXGLdouble);
typedef void (*GXGLClearColorProc)(GXGLfloat, GXGLfloat, GXGLfloat,
                                   GXGLfloat);
typedef void (*GXGLClearDepthProc)(GXGLdouble);
typedef void (*GXGLClearProc)(GXGLbitfield);
typedef void (*GXGLEnableProc)(GXGLenum);
typedef void (*GXGLDisableProc)(GXGLenum);
typedef void (*GXGLDepthFuncProc)(GXGLenum);
typedef void (*GXGLBlendFuncProc)(GXGLenum, GXGLenum);
typedef void (*GXGLBeginProc)(GXGLenum);
typedef void (*GXGLColor3fProc)(GXGLfloat, GXGLfloat, GXGLfloat);
typedef void (*GXGLColor4fProc)(GXGLfloat, GXGLfloat, GXGLfloat, GXGLfloat);
typedef void (*GXGLVertex2fProc)(GXGLfloat, GXGLfloat);
typedef void (*GXGLVertex3fProc)(GXGLfloat, GXGLfloat, GXGLfloat);
typedef void (*GXGLTexCoord2fProc)(GXGLfloat, GXGLfloat);
typedef void (*GXGLActiveTextureARBProc)(GXGLenum);
typedef void (*GXGLMultiTexCoord2fARBProc)(GXGLenum, GXGLfloat, GXGLfloat);
typedef void (*GXGLEndProc)(void);
typedef void (*GXGLGenTexturesProc)(GXGLsizei, GXGLuint *);
typedef void (*GXGLBindTextureProc)(GXGLenum, GXGLuint);
typedef void (*GXGLTexParameteriProc)(GXGLenum, GXGLenum, GXGLint);
typedef void (*GXGLTexImage2DProc)(GXGLenum, GXGLint, GXGLint, GXGLsizei,
                                  GXGLsizei, GXGLint, GXGLenum, GXGLenum,
                                  const GXGLvoid *);
typedef void (*GXGLDeleteTexturesProc)(GXGLsizei, const GXGLuint *);
typedef void (*GXGLFinishProc)(void);
typedef void (*GXGLReadPixelsProc)(GXGLint, GXGLint, GXGLsizei, GXGLsizei,
                                   GXGLenum, GXGLenum, GXGLvoid *);
typedef GXGLenum (*GXGLGetErrorProc)(void);

typedef struct GXMetalAGLAPI {
    GXAGLChoosePixelFormatProc aglChoosePixelFormat;
    GXAGLDestroyPixelFormatProc aglDestroyPixelFormat;
    GXAGLDescribePixelFormatProc aglDescribePixelFormat;
    GXAGLCreateContextProc aglCreateContext;
    GXAGLDestroyContextProc aglDestroyContext;
    GXAGLSetCurrentContextProc aglSetCurrentContext;
    GXAGLSetDrawableProc aglSetDrawable;
    GXAGLSwapBuffersProc aglSwapBuffers;
    GXAGLGetVersionProc aglGetVersion;
    GXAGLGetErrorProc aglGetError;
    GXAGLErrorStringProc aglErrorString;
    GXGLGetStringProc glGetString;
    GXGLGetIntegervProc glGetIntegerv;
    GXGLViewportProc glViewport;
    GXGLMatrixModeProc glMatrixMode;
    GXGLLoadIdentityProc glLoadIdentity;
    GXGLOrthoProc glOrtho;
    GXGLClearColorProc glClearColor;
    GXGLClearDepthProc glClearDepth;
    GXGLClearProc glClear;
    GXGLEnableProc glEnable;
    GXGLDisableProc glDisable;
    GXGLDepthFuncProc glDepthFunc;
    GXGLBlendFuncProc glBlendFunc;
    GXGLBeginProc glBegin;
    GXGLColor3fProc glColor3f;
    GXGLColor4fProc glColor4f;
    GXGLVertex2fProc glVertex2f;
    GXGLVertex3fProc glVertex3f;
    GXGLTexCoord2fProc glTexCoord2f;
    GXGLActiveTextureARBProc glActiveTextureARB;
    GXGLMultiTexCoord2fARBProc glMultiTexCoord2fARB;
    GXGLEndProc glEnd;
    GXGLGenTexturesProc glGenTextures;
    GXGLBindTextureProc glBindTexture;
    GXGLTexParameteriProc glTexParameteri;
    GXGLTexImage2DProc glTexImage2D;
    GXGLDeleteTexturesProc glDeleteTextures;
    GXGLFinishProc glFinish;
    GXGLReadPixelsProc glReadPixels;
    GXGLGetErrorProc glGetError;
} GXMetalAGLAPI;

enum GXMetalAGLStage {
    kGXMetalAGLStageStart,
    kGXMetalAGLStageLibrary,
    kGXMetalAGLStageSymbols,
    kGXMetalAGLStagePixelFormat,
    kGXMetalAGLStageContext,
    kGXMetalAGLStageDrawable,
    kGXMetalAGLStageCurrent,
    kGXMetalAGLStageRender,
    kGXMetalAGLStageReadback,
    kGXMetalAGLStageTeardown,
    kGXMetalAGLStageComplete
};

typedef struct GXMetalAGLReport {
    enum GXMetalAGLStage stage;
    const char *failureReason;
    OSErr libraryError;
    char failedSymbol[64];
    GXGLenum aglError;
    GXGLenum glError;
    GXGLint aglMajor;
    GXGLint aglMinor;
    GXGLint accelerated;
    GXGLint rendererID;
    GXGLint pixelSize;
    GXGLint depthSize;
    GXGLint doubleBuffer;
    GXGLint maxTextureSize;
    GXGLint maxLights;
    GXGLint maxTextureUnits;
    char vendor[128];
    char renderer[128];
    char version[128];
    char extensions[512];
    uint8_t triangle[3];
    uint8_t background[3];
    uint8_t displayTriangle[3];
    uint8_t displayBackground[3];
    uint8_t texture[3];
    uint8_t clippedTexture[3];
    uint8_t blend[3];
    uint8_t depth[3];
    uint8_t filledModes[GXMETAL_AGL_PROBE_FILLED_MODE_COUNT][3];
    uint8_t triangleListGuard[3];
    uint8_t samplerBaseOnly[3];
    uint8_t samplerTrilinear[3];
    uint8_t samplerAsymmetric[3];
    uint8_t samplerUnit1[3];
    Boolean glReadbackMatches;
    Boolean extendedReadbackMatches;
    Boolean clippedTextureMatches;
    Boolean filledModesMatch;
    Boolean samplerPrimaryMatches;
    Boolean samplerUnit1Extension;
    Boolean samplerUnit1Symbols;
    Boolean samplerUnit1Available;
    Boolean samplerUnit1Tested;
    Boolean samplerUnit1Matches;
    Boolean displayReadbackMatches;
    Boolean textureDeleted;
    Boolean currentReleased;
    Boolean drawableReleased;
    Boolean contextDestroyed;
    Boolean pixelFormatDestroyed;
    Boolean libraryClosed;
    Boolean functional;
} GXMetalAGLReport;

static const unsigned char kGXMetalAGLLibraryName[] = {
    13, 'O', 'p', 'e', 'n', 'G', 'L', 'L', 'i', 'b', 'r', 'a', 'r', 'y'
};
static const unsigned char kGXMetalAGLResultName[] = {
    25, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'A', 'G', 'L', ' ', 'P',
    'r', 'o', 'b', 'e', ' ', 'R', 'e', 's', 'u', 'l', 't', 's'
};

static const char *GXMetalAGLStageName(enum GXMetalAGLStage stage)
{
    static const char *const names[] = {
        "start", "library-load", "symbol-resolution", "pixel-format",
        "context-create", "drawable-bind", "make-current", "render",
        "readback", "teardown", "complete"
    };

    if ((unsigned long)stage >= sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[stage];
}

static void GXMetalAGLInitToolbox(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

static void GXMetalAGLCStringToPascal(const char *source,
                                      Str255 destination)
{
    size_t length = strlen(source);

    if (length > 255) {
        length = 255;
    }
    destination[0] = (unsigned char)length;
    memcpy(destination + 1, source, length);
}

static void GXMetalAGLRecordText(const char *message)
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
    (void)FSMakeFSSpec(volume, directory, kGXMetalAGLResultName, &result);
    (void)FSpDelete(&result);
    if (FSpCreate(&result, 'GXMA', 'TEXT', smSystemScript) != noErr ||
        FSpOpenDF(&result, fsWrPerm, &refNum) != noErr) {
        return;
    }
    (void)FSWrite(refNum, &length, message);
    (void)FSClose(refNum);
    (void)FlushVol(NULL, volume);
}

static void GXMetalAGLRecordCheckpoint(enum GXMetalAGLStage stage)
{
    char checkpoint[96];
    char *cursor = checkpoint;
    const char *name = GXMetalAGLStageName(stage);

    memcpy(cursor, "RUNNING: GXMetal AGL Probe stage=", 33);
    cursor += 33;
    while (*name != '\0' && cursor < checkpoint + sizeof(checkpoint) - 2) {
        *cursor++ = *name++;
    }
    *cursor++ = '\n';
    *cursor = '\0';
    GXMetalAGLRecordText(checkpoint);
}

static void GXMetalAGLAppendText(char **cursor, const char *end,
                                 const char *text)
{
    while (*text != '\0' && *cursor < end) {
        *(*cursor)++ = *text++;
    }
}

static void GXMetalAGLAppendUnsigned(char **cursor, const char *end,
                                     unsigned long value)
{
    char digits[16];
    int count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < (int)sizeof(digits));
    while (count > 0 && *cursor < end) {
        *(*cursor)++ = digits[--count];
    }
}

static void GXMetalAGLAppendSigned(char **cursor, const char *end, long value)
{
    unsigned long magnitude;

    if (value < 0) {
        GXMetalAGLAppendText(cursor, end, "-");
        magnitude = (unsigned long)(-(value + 1)) + 1;
    } else {
        magnitude = (unsigned long)value;
    }
    GXMetalAGLAppendUnsigned(cursor, end, magnitude);
}

static void GXMetalAGLAppendHex(char **cursor, const char *end,
                                unsigned long value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    GXMetalAGLAppendText(cursor, end, "0x");
    for (shift = 28; shift >= 0 && *cursor < end; shift -= 4) {
        *(*cursor)++ = digits[(value >> shift) & 0xf];
    }
}

static void GXMetalAGLCopyGLString(char *destination, size_t capacity,
                                   const GXGLubyte *source)
{
    size_t index = 0;

    if (capacity == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    while (source[index] != '\0' && index + 1 < capacity) {
        unsigned char value = source[index];

        destination[index] = value >= 32 && value < 127 ? (char)value : '?';
        index++;
    }
    destination[index] = '\0';
}

static Boolean GXMetalAGLResolve(CFragConnectionID connection,
                                  const char *name, Ptr *resolved)
{
    Str255 symbolName;
    CFragSymbolClass symbolClass;

    GXMetalAGLCStringToPascal(name, symbolName);
    *resolved = NULL;
    return FindSymbol(connection, symbolName, resolved, &symbolClass) == noErr &&
           *resolved != NULL && symbolClass == kTVectorCFragSymbol;
}

static Boolean GXMetalAGLResolveAPI(CFragConnectionID connection,
                                    GXMetalAGLAPI *api, char *failedSymbol,
                                    size_t failedSymbolCapacity)
{
    Ptr resolved = NULL;

#define GXMETAL_AGL_RESOLVE(field, type, symbol) do { \
    if (!GXMetalAGLResolve(connection, symbol, &resolved)) { \
        size_t symbolLength = strlen(symbol); \
        if (symbolLength >= failedSymbolCapacity) { \
            symbolLength = failedSymbolCapacity - 1; \
        } \
        memcpy(failedSymbol, symbol, symbolLength); \
        failedSymbol[symbolLength] = '\0'; \
        return false; \
    } \
    api->field = (type)resolved; \
} while (0)
    GXMETAL_AGL_RESOLVE(aglChoosePixelFormat, GXAGLChoosePixelFormatProc,
                        "aglChoosePixelFormat");
    GXMETAL_AGL_RESOLVE(aglDestroyPixelFormat, GXAGLDestroyPixelFormatProc,
                        "aglDestroyPixelFormat");
    GXMETAL_AGL_RESOLVE(aglDescribePixelFormat, GXAGLDescribePixelFormatProc,
                        "aglDescribePixelFormat");
    GXMETAL_AGL_RESOLVE(aglCreateContext, GXAGLCreateContextProc,
                        "aglCreateContext");
    GXMETAL_AGL_RESOLVE(aglDestroyContext, GXAGLDestroyContextProc,
                        "aglDestroyContext");
    GXMETAL_AGL_RESOLVE(aglSetCurrentContext, GXAGLSetCurrentContextProc,
                        "aglSetCurrentContext");
    GXMETAL_AGL_RESOLVE(aglSetDrawable, GXAGLSetDrawableProc,
                        "aglSetDrawable");
    GXMETAL_AGL_RESOLVE(aglSwapBuffers, GXAGLSwapBuffersProc,
                        "aglSwapBuffers");
    GXMETAL_AGL_RESOLVE(aglGetVersion, GXAGLGetVersionProc, "aglGetVersion");
    GXMETAL_AGL_RESOLVE(aglGetError, GXAGLGetErrorProc, "aglGetError");
    GXMETAL_AGL_RESOLVE(aglErrorString, GXAGLErrorStringProc,
                        "aglErrorString");
    GXMETAL_AGL_RESOLVE(glGetString, GXGLGetStringProc, "glGetString");
    GXMETAL_AGL_RESOLVE(glGetIntegerv, GXGLGetIntegervProc, "glGetIntegerv");
    GXMETAL_AGL_RESOLVE(glViewport, GXGLViewportProc, "glViewport");
    GXMETAL_AGL_RESOLVE(glMatrixMode, GXGLMatrixModeProc, "glMatrixMode");
    GXMETAL_AGL_RESOLVE(glLoadIdentity, GXGLLoadIdentityProc,
                        "glLoadIdentity");
    GXMETAL_AGL_RESOLVE(glOrtho, GXGLOrthoProc, "glOrtho");
    GXMETAL_AGL_RESOLVE(glClearColor, GXGLClearColorProc, "glClearColor");
    GXMETAL_AGL_RESOLVE(glClearDepth, GXGLClearDepthProc, "glClearDepth");
    GXMETAL_AGL_RESOLVE(glClear, GXGLClearProc, "glClear");
    GXMETAL_AGL_RESOLVE(glEnable, GXGLEnableProc, "glEnable");
    GXMETAL_AGL_RESOLVE(glDisable, GXGLDisableProc, "glDisable");
    GXMETAL_AGL_RESOLVE(glDepthFunc, GXGLDepthFuncProc, "glDepthFunc");
    GXMETAL_AGL_RESOLVE(glBlendFunc, GXGLBlendFuncProc, "glBlendFunc");
    GXMETAL_AGL_RESOLVE(glBegin, GXGLBeginProc, "glBegin");
    GXMETAL_AGL_RESOLVE(glColor3f, GXGLColor3fProc, "glColor3f");
    GXMETAL_AGL_RESOLVE(glColor4f, GXGLColor4fProc, "glColor4f");
    GXMETAL_AGL_RESOLVE(glVertex2f, GXGLVertex2fProc, "glVertex2f");
    GXMETAL_AGL_RESOLVE(glVertex3f, GXGLVertex3fProc, "glVertex3f");
    GXMETAL_AGL_RESOLVE(glTexCoord2f, GXGLTexCoord2fProc, "glTexCoord2f");
    GXMETAL_AGL_RESOLVE(glEnd, GXGLEndProc, "glEnd");
    GXMETAL_AGL_RESOLVE(glGenTextures, GXGLGenTexturesProc,
                        "glGenTextures");
    GXMETAL_AGL_RESOLVE(glBindTexture, GXGLBindTextureProc,
                        "glBindTexture");
    GXMETAL_AGL_RESOLVE(glTexParameteri, GXGLTexParameteriProc,
                        "glTexParameteri");
    GXMETAL_AGL_RESOLVE(glTexImage2D, GXGLTexImage2DProc, "glTexImage2D");
    GXMETAL_AGL_RESOLVE(glDeleteTextures, GXGLDeleteTexturesProc,
                        "glDeleteTextures");
    GXMETAL_AGL_RESOLVE(glFinish, GXGLFinishProc, "glFinish");
    GXMETAL_AGL_RESOLVE(glReadPixels, GXGLReadPixelsProc, "glReadPixels");
    GXMETAL_AGL_RESOLVE(glGetError, GXGLGetErrorProc, "glGetError");
#undef GXMETAL_AGL_RESOLVE

    /* These entry points are optional. Resolve them through the same CFM
     * boundary, but do not make an OpenGL 1.1 installation fail the core
     * probe merely because ARB multitexture is unavailable. */
    if (GXMetalAGLResolve(connection, "glActiveTextureARB", &resolved)) {
        api->glActiveTextureARB = (GXGLActiveTextureARBProc)resolved;
    }
    if (GXMetalAGLResolve(connection, "glMultiTexCoord2fARB", &resolved)) {
        api->glMultiTexCoord2fARB = (GXGLMultiTexCoord2fARBProc)resolved;
    }
    return true;
}

static GXGLint GXMetalAGLDescribe(const GXMetalAGLAPI *api,
                                  GXAGLPixelFormat pixelFormat,
                                  GXGLint attribute)
{
    GXGLint value = -1;

    if (!api->aglDescribePixelFormat(pixelFormat, attribute, &value)) {
        return -1;
    }
    return value;
}

static Boolean GXMetalAGLReadDisplayPixel(GDHandle graphicsDevice,
                                           Point location,
                                           uint8_t color[3])
{
    PixMapHandle pixmap;
    volatile unsigned char *pixel;
    long rowBytes;

    if (graphicsDevice == NULL || *graphicsDevice == NULL) {
        return false;
    }
    pixmap = (**graphicsDevice).gdPMap;
    if (pixmap == NULL || *pixmap == NULL ||
        location.h < (**pixmap).bounds.left ||
        location.h >= (**pixmap).bounds.right ||
        location.v < (**pixmap).bounds.top ||
        location.v >= (**pixmap).bounds.bottom) {
        return false;
    }
    rowBytes = (**pixmap).rowBytes & 0x3fff;
    pixel = (volatile unsigned char *)GetPixBaseAddr(pixmap) +
            (location.v - (**pixmap).bounds.top) * rowBytes;
    if ((**pixmap).pixelSize == 16) {
        unsigned short value;

        pixel += (location.h - (**pixmap).bounds.left) * 2;
        value = (unsigned short)(((unsigned short)pixel[0] << 8) | pixel[1]);
        color[0] = (uint8_t)((((value >> 10) & 31) * 255) / 31);
        color[1] = (uint8_t)((((value >> 5) & 31) * 255) / 31);
        color[2] = (uint8_t)(((value & 31) * 255) / 31);
        return true;
    }
    if ((**pixmap).pixelSize == 32) {
        pixel += (location.h - (**pixmap).bounds.left) * 4;
        color[0] = pixel[1];
        color[1] = pixel[2];
        color[2] = pixel[3];
        return true;
    }
    return false;
}

static void GXMetalAGLFillSolidRGBA(uint8_t *pixels, size_t pixelCount,
                                    uint8_t red, uint8_t green,
                                    uint8_t blue)
{
    size_t index;

    for (index = 0; index < pixelCount; ++index) {
        pixels[index * 4 + 0] = red;
        pixels[index * 4 + 1] = green;
        pixels[index * 4 + 2] = blue;
        pixels[index * 4 + 3] = 255;
    }
}

static void GXMetalAGLDrawTexturedQuad(const GXMetalAGLAPI *api,
                                       GXGLfloat left, GXGLfloat bottom,
                                       GXGLfloat right, GXGLfloat top)
{
    api->glBegin(GX_GL_QUADS);
    api->glTexCoord2f(0.0f, 0.0f); api->glVertex2f(left, bottom);
    api->glTexCoord2f(1.0f, 0.0f); api->glVertex2f(right, bottom);
    api->glTexCoord2f(1.0f, 1.0f); api->glVertex2f(right, top);
    api->glTexCoord2f(0.0f, 1.0f); api->glVertex2f(left, top);
    api->glEnd();
}

static void GXMetalAGLDrawMultiTexturedQuad(const GXMetalAGLAPI *api,
                                            GXGLfloat left,
                                            GXGLfloat bottom,
                                            GXGLfloat right,
                                            GXGLfloat top)
{
    api->glBegin(GX_GL_QUADS);
    api->glTexCoord2f(0.0f, 0.0f);
    api->glMultiTexCoord2fARB(GX_GL_TEXTURE1_ARB, 0.0f, 0.0f);
    api->glVertex2f(left, bottom);
    api->glTexCoord2f(1.0f, 0.0f);
    api->glMultiTexCoord2fARB(GX_GL_TEXTURE1_ARB, 1.0f, 0.0f);
    api->glVertex2f(right, bottom);
    api->glTexCoord2f(1.0f, 1.0f);
    api->glMultiTexCoord2fARB(GX_GL_TEXTURE1_ARB, 1.0f, 1.0f);
    api->glVertex2f(right, top);
    api->glTexCoord2f(0.0f, 1.0f);
    api->glMultiTexCoord2fARB(GX_GL_TEXTURE1_ARB, 0.0f, 1.0f);
    api->glVertex2f(left, top);
    api->glEnd();
}

static void GXMetalAGLBuildReport(const GXMetalAGLReport *report,
                                  char *result, size_t capacity)
{
    char *cursor = result;
    const char *end = result + capacity - 1;
    const char *status = "FAIL";

    if (report->functional) {
        status = report->accelerated == 1 ? "PASS_ACCELERATED" :
                                            "PASS_SOFTWARE";
    } else if (report->displayReadbackMatches) {
        status = report->accelerated == 1 ? "PARTIAL_ACCELERATED" :
                                            "PARTIAL_SOFTWARE";
    }
    GXMetalAGLAppendText(&cursor, end, "GXMetal AGL Probe 1\nstatus=");
    GXMetalAGLAppendText(&cursor, end, status);
    GXMetalAGLAppendText(&cursor, end, "\nstage=");
    GXMetalAGLAppendText(&cursor, end, GXMetalAGLStageName(report->stage));
    GXMetalAGLAppendText(&cursor, end, "\nreason=");
    GXMetalAGLAppendText(&cursor, end, report->failureReason != NULL ?
                                      report->failureReason : "none");
    GXMetalAGLAppendText(&cursor, end, "\nlibrary_error=");
    GXMetalAGLAppendSigned(&cursor, end, report->libraryError);
    GXMetalAGLAppendText(&cursor, end, "\nfailed_symbol=");
    GXMetalAGLAppendText(&cursor, end, report->failedSymbol[0] != '\0' ?
                                      report->failedSymbol : "none");
    GXMetalAGLAppendText(&cursor, end, "\nagl_error=");
    GXMetalAGLAppendHex(&cursor, end, report->aglError);
    GXMetalAGLAppendText(&cursor, end, "\ngl_error=");
    GXMetalAGLAppendHex(&cursor, end, report->glError);
    GXMetalAGLAppendText(&cursor, end, "\nagl_version=");
    GXMetalAGLAppendSigned(&cursor, end, report->aglMajor);
    GXMetalAGLAppendText(&cursor, end, ".");
    GXMetalAGLAppendSigned(&cursor, end, report->aglMinor);
    GXMetalAGLAppendText(&cursor, end, "\npixel_format accelerated=");
    GXMetalAGLAppendSigned(&cursor, end, report->accelerated);
    GXMetalAGLAppendText(&cursor, end, " renderer_id=");
    GXMetalAGLAppendHex(&cursor, end, (unsigned long)report->rendererID);
    GXMetalAGLAppendText(&cursor, end, " pixel_size=");
    GXMetalAGLAppendSigned(&cursor, end, report->pixelSize);
    GXMetalAGLAppendText(&cursor, end, " depth_size=");
    GXMetalAGLAppendSigned(&cursor, end, report->depthSize);
    GXMetalAGLAppendText(&cursor, end, " double_buffer=");
    GXMetalAGLAppendSigned(&cursor, end, report->doubleBuffer);
    GXMetalAGLAppendText(&cursor, end, "\nGL_VENDOR=");
    GXMetalAGLAppendText(&cursor, end, report->vendor);
    GXMetalAGLAppendText(&cursor, end, "\nGL_RENDERER=");
    GXMetalAGLAppendText(&cursor, end, report->renderer);
    GXMetalAGLAppendText(&cursor, end, "\nGL_VERSION=");
    GXMetalAGLAppendText(&cursor, end, report->version);
    GXMetalAGLAppendText(&cursor, end, "\nGL_MAX_TEXTURE_SIZE=");
    GXMetalAGLAppendSigned(&cursor, end, report->maxTextureSize);
    GXMetalAGLAppendText(&cursor, end, "\nGL_MAX_LIGHTS=");
    GXMetalAGLAppendSigned(&cursor, end, report->maxLights);
    GXMetalAGLAppendText(&cursor, end, "\nGL_MAX_TEXTURE_UNITS_ARB=");
    GXMetalAGLAppendSigned(&cursor, end, report->maxTextureUnits);
    GXMetalAGLAppendText(&cursor, end, "\nGL_EXTENSIONS=");
    GXMetalAGLAppendText(&cursor, end, report->extensions);
    GXMetalAGLAppendText(&cursor, end, "\nreadback triangle=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->triangle[0] << 16) |
        ((unsigned long)report->triangle[1] << 8) | report->triangle[2]);
    GXMetalAGLAppendText(&cursor, end, " background=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->background[0] << 16) |
        ((unsigned long)report->background[1] << 8) | report->background[2]);
    GXMetalAGLAppendText(&cursor, end, " match=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->glReadbackMatches);
    GXMetalAGLAppendText(&cursor, end, "\nextended_readback texture=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->texture[0] << 16) |
        ((unsigned long)report->texture[1] << 8) | report->texture[2]);
    GXMetalAGLAppendText(&cursor, end, " blend=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->blend[0] << 16) |
        ((unsigned long)report->blend[1] << 8) | report->blend[2]);
    GXMetalAGLAppendText(&cursor, end, " depth=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->depth[0] << 16) |
        ((unsigned long)report->depth[1] << 8) | report->depth[2]);
    GXMetalAGLAppendText(&cursor, end, " match=");
    GXMetalAGLAppendUnsigned(&cursor, end,
                             report->extendedReadbackMatches);
    GXMetalAGLAppendText(&cursor, end, " texture_deleted=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->textureDeleted);
    GXMetalAGLAppendText(&cursor, end, "\nclipped_texture sample=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->clippedTexture[0] << 16) |
        ((unsigned long)report->clippedTexture[1] << 8) |
        report->clippedTexture[2]);
    GXMetalAGLAppendText(&cursor, end, " match=");
    GXMetalAGLAppendUnsigned(&cursor, end,
                             report->clippedTextureMatches);
    GXMetalAGLAppendText(&cursor, end, "\nfilled_modes samples=");
    {
        unsigned long modeIndex;

        for (modeIndex = 0;
             modeIndex < GXMETAL_AGL_PROBE_FILLED_MODE_COUNT;
             ++modeIndex) {
            if (modeIndex != 0) {
                GXMetalAGLAppendText(&cursor, end, ",");
            }
            GXMetalAGLAppendHex(&cursor, end,
                ((unsigned long)report->filledModes[modeIndex][0] << 16) |
                ((unsigned long)report->filledModes[modeIndex][1] << 8) |
                report->filledModes[modeIndex][2]);
        }
    }
    GXMetalAGLAppendText(&cursor, end, " guard=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->triangleListGuard[0] << 16) |
        ((unsigned long)report->triangleListGuard[1] << 8) |
        report->triangleListGuard[2]);
    GXMetalAGLAppendText(&cursor, end, " match=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->filledModesMatch);
    GXMetalAGLAppendText(&cursor, end, "\nsampler_primary base_only=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->samplerBaseOnly[0] << 16) |
        ((unsigned long)report->samplerBaseOnly[1] << 8) |
        report->samplerBaseOnly[2]);
    GXMetalAGLAppendText(&cursor, end, " trilinear=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->samplerTrilinear[0] << 16) |
        ((unsigned long)report->samplerTrilinear[1] << 8) |
        report->samplerTrilinear[2]);
    GXMetalAGLAppendText(&cursor, end, " asymmetric=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->samplerAsymmetric[0] << 16) |
        ((unsigned long)report->samplerAsymmetric[1] << 8) |
        report->samplerAsymmetric[2]);
    GXMetalAGLAppendText(&cursor, end, " match=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->samplerPrimaryMatches);
    GXMetalAGLAppendText(&cursor, end, "\nsampler_unit1 extension=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->samplerUnit1Extension);
    GXMetalAGLAppendText(&cursor, end, " symbols=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->samplerUnit1Symbols);
    GXMetalAGLAppendText(&cursor, end, " available=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->samplerUnit1Available);
    GXMetalAGLAppendText(&cursor, end, " tested=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->samplerUnit1Tested);
    GXMetalAGLAppendText(&cursor, end, " sample=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->samplerUnit1[0] << 16) |
        ((unsigned long)report->samplerUnit1[1] << 8) |
        report->samplerUnit1[2]);
    GXMetalAGLAppendText(&cursor, end, " match=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->samplerUnit1Matches);
    GXMetalAGLAppendText(&cursor, end, " coverage=");
    GXMetalAGLAppendText(&cursor, end,
        report->samplerUnit1Tested ? "agl" : "native-only");
    GXMetalAGLAppendText(&cursor, end, "\ndisplay_readback triangle=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->displayTriangle[0] << 16) |
        ((unsigned long)report->displayTriangle[1] << 8) |
        report->displayTriangle[2]);
    GXMetalAGLAppendText(&cursor, end, " background=");
    GXMetalAGLAppendHex(&cursor, end,
        ((unsigned long)report->displayBackground[0] << 16) |
        ((unsigned long)report->displayBackground[1] << 8) |
        report->displayBackground[2]);
    GXMetalAGLAppendText(&cursor, end, " match=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->displayReadbackMatches);
    GXMetalAGLAppendText(&cursor, end, "\nteardown current=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->currentReleased);
    GXMetalAGLAppendText(&cursor, end, " drawable=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->drawableReleased);
    GXMetalAGLAppendText(&cursor, end, " context=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->contextDestroyed);
    GXMetalAGLAppendText(&cursor, end, " pixel_format=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->pixelFormatDestroyed);
    GXMetalAGLAppendText(&cursor, end, " library=");
    GXMetalAGLAppendUnsigned(&cursor, end, report->libraryClosed);
    GXMetalAGLAppendText(&cursor, end, "\n");
    *cursor = '\0';
}

static void GXMetalAGLShowResult(const GXMetalAGLReport *report)
{
    Str255 text;
    Str255 empty = {0};
    char message[256];
    char *cursor = message;
    const char *end = message + sizeof(message) - 1;

    if (report->functional) {
        GXMetalAGLAppendText(&cursor, end,
            report->accelerated == 1 ?
            "PASS: Apple AGL created an accelerated OpenGL context. " :
            "PASS: Apple AGL created a software OpenGL context. ");
        GXMetalAGLAppendText(&cursor, end, report->renderer);
        GXMetalAGLAppendText(&cursor, end,
            " passed filled primitive, clipped texture, sampler MIN/MAG/mip, alpha-blend, depth, readback, presentation, and resource-lifecycle checks. Detailed caps are in Preferences:GXMetal AGL Probe Results.");
    } else if (report->displayReadbackMatches) {
        GXMetalAGLAppendText(&cursor, end,
            "PARTIAL: the accelerated OpenGL context presented the expected triangle, but core glReadPixels returned the wrong pixels. Detailed caps are in Preferences:GXMetal AGL Probe Results.");
    } else {
        GXMetalAGLAppendText(&cursor, end, "FAIL at ");
        GXMetalAGLAppendText(&cursor, end, GXMetalAGLStageName(report->stage));
        GXMetalAGLAppendText(&cursor, end, ": ");
        GXMetalAGLAppendText(&cursor, end, report->failureReason != NULL ?
                                          report->failureReason : "unknown");
        GXMetalAGLAppendText(&cursor, end,
            ". Details are in Preferences:GXMetal AGL Probe Results.");
    }
    *cursor = '\0';
    GXMetalAGLCStringToPascal(message, text);
    ParamText(text, empty, empty, empty);
    if (report->functional) {
        (void)NoteAlert(GXMETAL_AGL_ALERT_ID, NULL);
    } else {
        (void)StopAlert(GXMETAL_AGL_ALERT_ID, NULL);
    }
}

int main(void)
{
    enum {
        kGXMetalAGLTextureScene,
        kGXMetalAGLTextureSamplerMip,
        kGXMetalAGLTextureAsymmetric,
        kGXMetalAGLTextureUnit1Mip,
        kGXMetalAGLTextureUnit1White,
        kGXMetalAGLTextureCount
    };
    static const uint8_t texturePixels[16] = {
        255, 240, 8, 255, 255, 240, 8, 255,
        255, 240, 8, 255, 255, 240, 8, 255
    };
    static const uint8_t asymmetricPixels[16] = {
        255, 0, 0, 255, 0, 0, 255, 255,
        255, 0, 0, 255, 0, 0, 255, 255
    };
    static const uint8_t whitePixel[4] = {255, 255, 255, 255};
    static const char windowTitleText[] =
        "GXMetal AGL Probe " GXMETAL_PRODUCT_VERSION_STRING;
    static const GXGLint attributes[] = {
        GX_AGL_ALL_RENDERERS,
        GX_AGL_RGBA,
        GX_AGL_DOUBLEBUFFER,
        GX_AGL_DEPTH_SIZE, 16,
        GX_AGL_NONE
    };
    GXMetalAGLAPI api;
    GXMetalAGLReport report;
    CFragConnectionID connection = NULL;
    Ptr mainAddress = NULL;
    Str255 cfmErrorName;
    Str255 windowTitle;
    Rect windowRect;
    WindowPtr window = NULL;
    GXAGLPixelFormat pixelFormat = NULL;
    GXAGLContext context = NULL;
    GXGLuint textureObjects[kGXMetalAGLTextureCount];
    GXGLsizei textureObjectCount = 0;
    uint8_t mipLevel0[4 * 4 * 4];
    uint8_t mipLevel1[2 * 2 * 4];
    uint8_t mipLevel2[4];
    GXAGLDevice device;
    Point triangleLocation;
    Point backgroundLocation;
    static const GXGLint filledSampleX[
        GXMETAL_AGL_PROBE_FILLED_MODE_COUNT] = {
            12, 76, 128, 180, 232, 284
        };
    GXGLint filledModeIndex;
    enum GXMetalAGLStage failureStage = kGXMetalAGLStageStart;
    char result[GXMETAL_AGL_REPORT_CAPACITY];

    memset(&api, 0, sizeof(api));
    memset(&report, 0, sizeof(report));
    memset(textureObjects, 0, sizeof(textureObjects));
    report.aglMajor = -1;
    report.aglMinor = -1;
    report.accelerated = -1;
    report.rendererID = -1;
    report.pixelSize = -1;
    report.depthSize = -1;
    report.doubleBuffer = -1;
    report.maxTextureSize = -1;
    report.maxLights = -1;
    report.maxTextureUnits = 1;
    GXMetalAGLInitToolbox();

    SetRect(&windowRect, 80, 70, 80 + GXMETAL_AGL_WIDTH,
            70 + GXMETAL_AGL_HEIGHT);
    GXMetalAGLCStringToPascal(windowTitleText, windowTitle);
    window = NewCWindow(NULL, &windowRect, windowTitle, true,
                        documentProc, (WindowPtr)-1, false, 0);
    if (window == NULL) {
        report.stage = kGXMetalAGLStageStart;
        report.failureReason = "test window creation failed";
        goto cleanup;
    }
    SetPort(window);
    GXMetalAGLRecordCheckpoint(kGXMetalAGLStageLibrary);
    report.stage = kGXMetalAGLStageLibrary;
    report.libraryError = GetSharedLibrary(
        kGXMetalAGLLibraryName, kPowerPCCFragArch, kReferenceCFrag,
        &connection, &mainAddress, cfmErrorName);
    if (report.libraryError != noErr || connection == NULL) {
        report.failureReason = "OpenGLLibrary could not be loaded";
        goto cleanup;
    }

    GXMetalAGLRecordCheckpoint(kGXMetalAGLStageSymbols);
    report.stage = kGXMetalAGLStageSymbols;
    if (!GXMetalAGLResolveAPI(connection, &api, report.failedSymbol,
                              sizeof(report.failedSymbol))) {
        report.failureReason = "required AGL/OpenGL symbol is missing";
        goto cleanup;
    }
    api.aglGetVersion(&report.aglMajor, &report.aglMinor);

    GXMetalAGLRecordCheckpoint(kGXMetalAGLStagePixelFormat);
    report.stage = kGXMetalAGLStagePixelFormat;
    device = GetMainDevice();
    pixelFormat = api.aglChoosePixelFormat(&device, 1, attributes);
    if (pixelFormat == NULL) {
        report.aglError = api.aglGetError();
        report.failureReason = "aglChoosePixelFormat returned NULL";
        goto cleanup;
    }
    report.accelerated = GXMetalAGLDescribe(&api, pixelFormat,
                                            GX_AGL_ACCELERATED);
    report.rendererID = GXMetalAGLDescribe(&api, pixelFormat,
                                           GX_AGL_RENDERER_ID);
    report.pixelSize = GXMetalAGLDescribe(&api, pixelFormat,
                                          GX_AGL_PIXEL_SIZE);
    report.depthSize = GXMetalAGLDescribe(&api, pixelFormat,
                                          GX_AGL_DEPTH_SIZE);
    report.doubleBuffer = GXMetalAGLDescribe(&api, pixelFormat,
                                             GX_AGL_DOUBLEBUFFER);
    /* Some older renderer modules reject optional description attributes and
     * leave that diagnostic in AGL's sticky error slot. The recorded swap
     * result below must describe the swap itself, not an optional metadata
     * query performed before context creation. */
    (void)api.aglGetError();

    GXMetalAGLRecordCheckpoint(kGXMetalAGLStageContext);
    report.stage = kGXMetalAGLStageContext;
    context = api.aglCreateContext(pixelFormat, NULL);
    if (context == NULL) {
        report.aglError = api.aglGetError();
        report.failureReason = "aglCreateContext returned NULL";
        goto cleanup;
    }

    GXMetalAGLRecordCheckpoint(kGXMetalAGLStageDrawable);
    report.stage = kGXMetalAGLStageDrawable;
    if (!api.aglSetDrawable(context, GetWindowPort(window))) {
        report.aglError = api.aglGetError();
        report.failureReason = "aglSetDrawable rejected the window";
        goto cleanup;
    }

    GXMetalAGLRecordCheckpoint(kGXMetalAGLStageCurrent);
    report.stage = kGXMetalAGLStageCurrent;
    if (!api.aglSetCurrentContext(context)) {
        report.aglError = api.aglGetError();
        report.failureReason = "aglSetCurrentContext failed";
        goto cleanup;
    }

    GXMetalAGLRecordCheckpoint(kGXMetalAGLStageRender);
    report.stage = kGXMetalAGLStageRender;
    GXMetalAGLCopyGLString(report.vendor, sizeof(report.vendor),
                           api.glGetString(GX_GL_VENDOR));
    GXMetalAGLCopyGLString(report.renderer, sizeof(report.renderer),
                           api.glGetString(GX_GL_RENDERER));
    GXMetalAGLCopyGLString(report.version, sizeof(report.version),
                           api.glGetString(GX_GL_VERSION));
    GXMetalAGLCopyGLString(report.extensions, sizeof(report.extensions),
                           api.glGetString(GX_GL_EXTENSIONS));
    api.glGetIntegerv(GX_GL_MAX_TEXTURE_SIZE, &report.maxTextureSize);
    api.glGetIntegerv(GX_GL_MAX_LIGHTS, &report.maxLights);
    report.samplerUnit1Extension = gxmetal_agl_probe_has_extension(
        report.extensions, "GL_ARB_multitexture");
    report.samplerUnit1Symbols = api.glActiveTextureARB != NULL &&
                                 api.glMultiTexCoord2fARB != NULL;
    if (report.samplerUnit1Extension) {
        api.glGetIntegerv(GX_GL_MAX_TEXTURE_UNITS_ARB,
                          &report.maxTextureUnits);
    }
    report.samplerUnit1Available = report.samplerUnit1Extension &&
                                   report.samplerUnit1Symbols &&
                                   report.maxTextureUnits >= 2;
    if (report.vendor[0] == '\0' || report.renderer[0] == '\0' ||
        report.version[0] == '\0' || report.maxTextureSize <= 0 ||
        report.maxLights <= 0 || report.maxTextureUnits <= 0) {
        report.glError = api.glGetError();
        report.failureReason = "OpenGL identity or capability query failed";
        goto cleanup;
    }

    api.glViewport(0, 0, GXMETAL_AGL_WIDTH, GXMETAL_AGL_HEIGHT);
    api.glMatrixMode(GX_GL_PROJECTION);
    api.glLoadIdentity();
    api.glOrtho(0.0, GXMETAL_AGL_WIDTH, 0.0, GXMETAL_AGL_HEIGHT,
                -1.0, 1.0);
    api.glMatrixMode(GX_GL_MODELVIEW);
    api.glLoadIdentity();
    api.glClearColor(0.04f, 0.06f, 0.14f, 1.0f);
    api.glClearDepth(1.0);
    api.glClear(GX_GL_COLOR_BUFFER_BIT | GX_GL_DEPTH_BUFFER_BIT);

    /* Ordinary immediate-mode geometry. */
    api.glBegin(GX_GL_TRIANGLES);
    api.glColor3f(0.02f, 0.95f, 0.04f);
    api.glVertex2f(64.0f, 145.0f);
    api.glVertex2f(256.0f, 145.0f);
    api.glVertex2f(160.0f, 215.0f);
    api.glEnd();

    /* Exercise every filled immediate-mode topology. The first call contains
     * two disjoint triangles and leaves a sampled blue gap between them; a
     * driver that incorrectly treats GL_TRIANGLES as one long strip fills
     * that guard pixel, reproducing the topology failure first found in Oni. */
    api.glColor3f(0.02f, 0.95f, 0.04f);
    api.glBegin(GX_GL_TRIANGLES);
    api.glVertex2f(6.0f, 92.0f);
    api.glVertex2f(22.0f, 92.0f);
    api.glVertex2f(6.0f, 125.0f);
    api.glVertex2f(26.0f, 125.0f);
    api.glVertex2f(42.0f, 125.0f);
    api.glVertex2f(42.0f, 92.0f);
    api.glEnd();

    api.glBegin(GX_GL_TRIANGLE_STRIP);
    api.glVertex2f(59.0f, 92.0f);
    api.glVertex2f(59.0f, 125.0f);
    api.glVertex2f(93.0f, 92.0f);
    api.glVertex2f(93.0f, 125.0f);
    api.glEnd();

    api.glBegin(GX_GL_TRIANGLE_FAN);
    api.glVertex2f(128.0f, 108.0f);
    api.glVertex2f(111.0f, 92.0f);
    api.glVertex2f(145.0f, 92.0f);
    api.glVertex2f(145.0f, 125.0f);
    api.glVertex2f(111.0f, 125.0f);
    api.glVertex2f(111.0f, 92.0f);
    api.glEnd();

    api.glBegin(GX_GL_QUADS);
    api.glVertex2f(163.0f, 92.0f);
    api.glVertex2f(197.0f, 92.0f);
    api.glVertex2f(197.0f, 125.0f);
    api.glVertex2f(163.0f, 125.0f);
    api.glEnd();

    api.glBegin(GX_GL_QUAD_STRIP);
    api.glVertex2f(215.0f, 92.0f);
    api.glVertex2f(215.0f, 125.0f);
    api.glVertex2f(249.0f, 92.0f);
    api.glVertex2f(249.0f, 125.0f);
    api.glEnd();

    api.glBegin(GX_GL_POLYGON);
    api.glVertex2f(267.0f, 92.0f);
    api.glVertex2f(301.0f, 92.0f);
    api.glVertex2f(301.0f, 125.0f);
    api.glVertex2f(267.0f, 125.0f);
    api.glEnd();

    /* Real texture objects and uploads, sampled with normalized coordinates. */
    textureObjectCount = report.samplerUnit1Available ?
                         kGXMetalAGLTextureCount : 3;
    api.glGenTextures(textureObjectCount, textureObjects);
    if (textureObjects[kGXMetalAGLTextureScene] == 0 ||
        textureObjects[kGXMetalAGLTextureSamplerMip] == 0 ||
        textureObjects[kGXMetalAGLTextureAsymmetric] == 0 ||
        (report.samplerUnit1Available &&
         (textureObjects[kGXMetalAGLTextureUnit1Mip] == 0 ||
          textureObjects[kGXMetalAGLTextureUnit1White] == 0))) {
        report.glError = api.glGetError();
        report.failureReason = "glGenTextures returned zero";
        goto cleanup;
    }
    api.glBindTexture(GX_GL_TEXTURE_2D,
                      textureObjects[kGXMetalAGLTextureScene]);
    api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MIN_FILTER,
                        GX_GL_NEAREST);
    api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MAG_FILTER,
                        GX_GL_NEAREST);
    api.glTexImage2D(GX_GL_TEXTURE_2D, 0, GX_GL_RGBA, 2, 2, 0,
                     GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, texturePixels);
    api.glEnable(GX_GL_TEXTURE_2D);
    api.glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    api.glBegin(GX_GL_QUADS);
    api.glTexCoord2f(0.0f, 0.0f); api.glVertex2f(16.0f, 16.0f);
    api.glTexCoord2f(1.0f, 0.0f); api.glVertex2f(80.0f, 16.0f);
    api.glTexCoord2f(1.0f, 1.0f); api.glVertex2f(80.0f, 80.0f);
    api.glTexCoord2f(0.0f, 1.0f); api.glVertex2f(16.0f, 80.0f);
    api.glEnd();

    /* Force Apple's GLD to clip a textured filled primitive at the viewport
     * boundary. The visible yellow remnant exercises the private pointer-
     * array polygon path that entirely-on-screen samples miss. */
    api.glBegin(GX_GL_POLYGON);
    api.glTexCoord2f(0.0f, 0.0f); api.glVertex2f(-20.0f, 130.0f);
    api.glTexCoord2f(1.0f, 0.0f); api.glVertex2f(30.0f, 130.0f);
    api.glTexCoord2f(1.0f, 1.0f); api.glVertex2f(30.0f, 144.0f);
    api.glTexCoord2f(0.0f, 1.0f); api.glVertex2f(-20.0f, 144.0f);
    api.glEnd();
    api.glDisable(GX_GL_TEXTURE_2D);

    /* A red/green/blue mip chain gives each MIN behavior an unambiguous
     * readback. A non-mipmap MIN remains red at level zero. Rendering the 4x4
     * image into three pixels selects fractional LOD log2(4/3), so trilinear
     * MIN must contain both red level zero and green level one.
     */
    GXMetalAGLFillSolidRGBA(mipLevel0, 16, 255, 0, 0);
    GXMetalAGLFillSolidRGBA(mipLevel1, 4, 0, 255, 0);
    GXMetalAGLFillSolidRGBA(mipLevel2, 1, 0, 0, 255);
    api.glBindTexture(GX_GL_TEXTURE_2D,
                      textureObjects[kGXMetalAGLTextureSamplerMip]);
    api.glTexImage2D(GX_GL_TEXTURE_2D, 0, GX_GL_RGBA, 4, 4, 0,
                     GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, mipLevel0);
    api.glTexImage2D(GX_GL_TEXTURE_2D, 1, GX_GL_RGBA, 2, 2, 0,
                     GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, mipLevel1);
    api.glTexImage2D(GX_GL_TEXTURE_2D, 2, GX_GL_RGBA, 1, 1, 0,
                     GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, mipLevel2);
    api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MAG_FILTER,
                        GX_GL_NEAREST);
    api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MIN_FILTER,
                        GX_GL_NEAREST);
    api.glEnable(GX_GL_TEXTURE_2D);
    GXMetalAGLDrawTexturedQuad(&api, 40.0f, 132.0f, 42.0f, 134.0f);
    api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MIN_FILTER,
                        GX_GL_LINEAR_MIPMAP_LINEAR);
    GXMetalAGLDrawTexturedQuad(&api, 46.0f, 132.0f, 49.0f, 135.0f);
    api.glDisable(GX_GL_TEXTURE_2D);

    /* Set MAG before MIN so a bridge which collapses the two independent tags
     * will incorrectly make this magnified red/blue boundary nearest-filtered.
     */
    api.glBindTexture(GX_GL_TEXTURE_2D,
                      textureObjects[kGXMetalAGLTextureAsymmetric]);
    api.glTexImage2D(GX_GL_TEXTURE_2D, 0, GX_GL_RGBA, 2, 2, 0,
                     GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, asymmetricPixels);
    api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MAG_FILTER,
                        GX_GL_LINEAR);
    api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MIN_FILTER,
                        GX_GL_NEAREST);
    api.glEnable(GX_GL_TEXTURE_2D);
    GXMetalAGLDrawTexturedQuad(&api, 56.0f, 130.0f, 88.0f, 144.0f);
    api.glDisable(GX_GL_TEXTURE_2D);

    if (report.samplerUnit1Available) {
        /* Unit zero contributes white while unit one's magenta/cyan mip chain
         * contributes both colors at the same fractional LOD. This proves
         * that Apple's ARB path selected the secondary binding, independent
         * MIN state, and trilinear mip sampler. */
        api.glActiveTextureARB(GX_GL_TEXTURE0_ARB);
        api.glBindTexture(GX_GL_TEXTURE_2D,
                          textureObjects[kGXMetalAGLTextureUnit1White]);
        api.glTexImage2D(GX_GL_TEXTURE_2D, 0, GX_GL_RGBA, 1, 1, 0,
                         GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, whitePixel);
        api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MAG_FILTER,
                            GX_GL_NEAREST);
        api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MIN_FILTER,
                            GX_GL_NEAREST);
        api.glEnable(GX_GL_TEXTURE_2D);

        GXMetalAGLFillSolidRGBA(mipLevel0, 16, 255, 0, 255);
        GXMetalAGLFillSolidRGBA(mipLevel1, 4, 0, 255, 255);
        GXMetalAGLFillSolidRGBA(mipLevel2, 1, 255, 255, 0);
        api.glActiveTextureARB(GX_GL_TEXTURE1_ARB);
        api.glBindTexture(GX_GL_TEXTURE_2D,
                          textureObjects[kGXMetalAGLTextureUnit1Mip]);
        api.glTexImage2D(GX_GL_TEXTURE_2D, 0, GX_GL_RGBA, 4, 4, 0,
                         GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, mipLevel0);
        api.glTexImage2D(GX_GL_TEXTURE_2D, 1, GX_GL_RGBA, 2, 2, 0,
                         GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, mipLevel1);
        api.glTexImage2D(GX_GL_TEXTURE_2D, 2, GX_GL_RGBA, 1, 1, 0,
                         GX_GL_RGBA, GX_GL_UNSIGNED_BYTE, mipLevel2);
        api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MAG_FILTER,
                            GX_GL_NEAREST);
        api.glTexParameteri(GX_GL_TEXTURE_2D, GX_GL_TEXTURE_MIN_FILTER,
                            GX_GL_LINEAR_MIPMAP_LINEAR);
        api.glEnable(GX_GL_TEXTURE_2D);
        api.glActiveTextureARB(GX_GL_TEXTURE0_ARB);
        GXMetalAGLDrawMultiTexturedQuad(&api, 92.0f, 132.0f,
                                       95.0f, 135.0f);
        api.glActiveTextureARB(GX_GL_TEXTURE1_ARB);
        api.glDisable(GX_GL_TEXTURE_2D);
        api.glActiveTextureARB(GX_GL_TEXTURE0_ARB);
        api.glDisable(GX_GL_TEXTURE_2D);
        report.samplerUnit1Tested = true;
    }

    /* Source-alpha blending over the blue clear color. */
    api.glEnable(GX_GL_BLEND);
    api.glBlendFunc(GX_GL_SRC_ALPHA, GX_GL_ONE_MINUS_SRC_ALPHA);
    api.glColor4f(1.0f, 0.0f, 0.0f, 0.5f);
    api.glBegin(GX_GL_QUADS);
    api.glVertex2f(96.0f, 16.0f);
    api.glVertex2f(160.0f, 16.0f);
    api.glVertex2f(160.0f, 80.0f);
    api.glVertex2f(96.0f, 80.0f);
    api.glEnd();
    api.glDisable(GX_GL_BLEND);

    /* Draw the near quad first, then prove a farther quad cannot overwrite it. */
    api.glEnable(GX_GL_DEPTH_TEST);
    api.glDepthFunc(GX_GL_LESS);
    api.glColor3f(0.95f, 0.02f, 0.02f);
    api.glBegin(GX_GL_QUADS);
    api.glVertex3f(176.0f, 16.0f, 0.5f);
    api.glVertex3f(304.0f, 16.0f, 0.5f);
    api.glVertex3f(304.0f, 80.0f, 0.5f);
    api.glVertex3f(176.0f, 80.0f, 0.5f);
    api.glEnd();
    api.glColor3f(0.02f, 0.95f, 0.02f);
    api.glBegin(GX_GL_QUADS);
    api.glVertex3f(176.0f, 16.0f, -0.5f);
    api.glVertex3f(304.0f, 16.0f, -0.5f);
    api.glVertex3f(304.0f, 80.0f, -0.5f);
    api.glVertex3f(176.0f, 80.0f, -0.5f);
    api.glEnd();
    api.glDisable(GX_GL_DEPTH_TEST);
    api.glFinish();

    GXMetalAGLRecordCheckpoint(kGXMetalAGLStageReadback);
    report.stage = kGXMetalAGLStageReadback;
    api.glReadPixels(160, 180, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.triangle);
    api.glReadPixels(8, 8, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.background);
    api.glReadPixels(48, 48, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.texture);
    api.glReadPixels(8, 136, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.clippedTexture);
    api.glReadPixels(128, 48, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.blend);
    api.glReadPixels(240, 48, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.depth);
    for (filledModeIndex = 0;
         filledModeIndex < GXMETAL_AGL_PROBE_FILLED_MODE_COUNT;
         ++filledModeIndex) {
        api.glReadPixels(filledSampleX[filledModeIndex], 105, 1, 1,
                         GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                         report.filledModes[filledModeIndex]);
    }
    api.glReadPixels(22, 105, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.triangleListGuard);
    api.glReadPixels(40, 132, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.samplerBaseOnly);
    api.glReadPixels(47, 133, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.samplerTrilinear);
    api.glReadPixels(72, 137, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                     report.samplerAsymmetric);
    if (report.samplerUnit1Tested) {
        api.glReadPixels(93, 133, 1, 1, GX_GL_RGB, GX_GL_UNSIGNED_BYTE,
                         report.samplerUnit1);
    }
    api.glDeleteTextures(textureObjectCount, textureObjects);
    textureObjectCount = 0;
    memset(textureObjects, 0, sizeof(textureObjects));
    report.glError = api.glGetError();
    report.textureDeleted = report.glError == GX_GL_NO_ERROR;
    report.glReadbackMatches = gxmetal_agl_probe_readback_matches(
        report.triangle, report.background, report.glError);
    report.extendedReadbackMatches =
        gxmetal_agl_probe_extended_readback_matches(
            report.texture, report.blend, report.depth, report.glError);
    report.clippedTextureMatches =
        gxmetal_agl_probe_clipped_texture_matches(
            report.clippedTexture, report.glError);
    report.filledModesMatch = gxmetal_agl_probe_filled_modes_match(
        report.filledModes, report.triangleListGuard, report.glError);
    report.samplerPrimaryMatches =
        gxmetal_agl_probe_sampler_primary_matches(
            report.samplerBaseOnly, report.samplerTrilinear,
            report.samplerAsymmetric, report.glError);
    if (report.samplerUnit1Tested) {
        report.samplerUnit1Matches =
            gxmetal_agl_probe_sampler_unit1_matches(
                report.samplerUnit1, report.glError);
    }
    api.aglSwapBuffers(context);
    report.aglError = api.aglGetError();
    if (report.aglError != GX_GL_NO_ERROR) {
        report.failureReason = "aglSwapBuffers failed";
        goto cleanup;
    }
    SetPort(window);
    triangleLocation.h = 160;
    triangleLocation.v = GXMETAL_AGL_HEIGHT - 1 - 180;
    backgroundLocation.h = 8;
    backgroundLocation.v = GXMETAL_AGL_HEIGHT - 1 - 8;
    LocalToGlobal(&triangleLocation);
    LocalToGlobal(&backgroundLocation);
    if (GXMetalAGLReadDisplayPixel(device, triangleLocation,
                                    report.displayTriangle) &&
        GXMetalAGLReadDisplayPixel(device, backgroundLocation,
                                    report.displayBackground)) {
        report.displayReadbackMatches = gxmetal_agl_probe_readback_matches(
            report.displayTriangle, report.displayBackground, 0);
    }
    if (!report.displayReadbackMatches) {
        report.failureReason = "presented framebuffer pixels did not match";
        goto cleanup;
    }
    if (!report.glReadbackMatches) {
        report.failureReason =
            "core glReadPixels did not match the displayed frame";
        goto cleanup;
    }
    if (!report.extendedReadbackMatches || !report.clippedTextureMatches ||
        !report.filledModesMatch || !report.samplerPrimaryMatches ||
        (report.samplerUnit1Available && !report.samplerUnit1Matches) ||
        !report.textureDeleted) {
        report.failureReason =
            "topology, clipping, sampler, texture, blend, depth, or lifecycle check failed";
        goto cleanup;
    }
    report.functional = true;
    report.stage = kGXMetalAGLStageComplete;

cleanup:
    failureStage = report.stage;
    GXMetalAGLRecordCheckpoint(kGXMetalAGLStageTeardown);
    if (textureObjectCount > 0 && api.glDeleteTextures != NULL &&
        api.aglSetCurrentContext != NULL && context != NULL) {
        api.glDeleteTextures(textureObjectCount, textureObjects);
        textureObjectCount = 0;
        memset(textureObjects, 0, sizeof(textureObjects));
    }
    if (connection != NULL && api.aglSetCurrentContext != NULL) {
        report.currentReleased = api.aglSetCurrentContext(NULL);
    } else {
        report.currentReleased = true;
    }
    if (context != NULL && api.aglSetDrawable != NULL) {
        report.drawableReleased = api.aglSetDrawable(context, NULL);
    } else {
        report.drawableReleased = true;
    }
    if (context != NULL && api.aglDestroyContext != NULL) {
        report.contextDestroyed = api.aglDestroyContext(context);
        context = NULL;
    } else {
        report.contextDestroyed = true;
    }
    if (pixelFormat != NULL && api.aglDestroyPixelFormat != NULL) {
        api.aglDestroyPixelFormat(pixelFormat);
        pixelFormat = NULL;
        report.pixelFormatDestroyed = true;
    } else {
        report.pixelFormatDestroyed = true;
    }
    if (connection != NULL) {
        report.libraryClosed = CloseConnection(&connection) == noErr;
    } else {
        report.libraryClosed = true;
    }
    report.stage = report.functional ? kGXMetalAGLStageComplete : failureStage;
    if (report.functional &&
        (!report.currentReleased || !report.drawableReleased ||
         !report.contextDestroyed || !report.pixelFormatDestroyed ||
         !report.libraryClosed)) {
        report.functional = false;
        report.stage = kGXMetalAGLStageTeardown;
        report.failureReason = "AGL/OpenGL teardown failed";
    }
    GXMetalAGLBuildReport(&report, result, sizeof(result));
    GXMetalAGLRecordText(result);
    GXMetalAGLShowResult(&report);
    if (window != NULL) {
        DisposeWindow(window);
    }
    return report.functional ? 0 : 1;
}
