#include <Dialogs.h>
#include <Fonts.h>
#include <Quickdraw.h>
#include <RAVE.h>
#include <TextEdit.h>
#include <Windows.h>

#include <string.h>

#define GXMETAL_ALERT_ID 128
#define GXMETAL_WIDTH 320
#define GXMETAL_HEIGHT 220

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

static TQAEngine *GXMetalFindEngine(const TQADevice *device)
{
    TQAEngine *engine = QADeviceGetFirstEngine(device);

    while (engine != NULL) {
        char name[64];
        unsigned long length = 0;

        memset(name, 0, sizeof(name));
        if (QAEngineGestalt(engine, kQAGestalt_ASCIINameLength,
                            &length) == kQANoErr &&
            length < sizeof(name) &&
            QAEngineGestalt(engine, kQAGestalt_ASCIIName,
                            name) == kQANoErr &&
            strcmp(name, "GXMetal") == 0) {
            return engine;
        }
        engine = QADeviceGetNextEngine(device, engine);
    }
    return NULL;
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
    kGXMetalPixelPurple
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
    return red > maximum / 3 && blue > maximum / 3 &&
           green < maximum / 3;
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
    TQAImage image;
    TQATexture *texture = NULL;
    TQARect dirty = {0, GXMETAL_WIDTH, 0, GXMETAL_HEIGHT};
    TQAVGouraud farTriangle[3];
    TQAVGouraud nearTriangle[3];
    TQAVGouraud blendBase[3];
    TQAVGouraud blendOverlay[3];
    TQAVTexture texturedQuad[4];
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

    texturedQuad[0] = GXMetalTextureVertex(178.0f, 38.0f, 0.30f,
                                            0.0f, 0.0f);
    texturedQuad[1] = GXMetalTextureVertex(302.0f, 38.0f, 0.30f,
                                            1.0f, 0.0f);
    texturedQuad[2] = GXMetalTextureVertex(178.0f, 190.0f, 0.30f,
                                            0.0f, 1.0f);
    texturedQuad[3] = GXMetalTextureVertex(302.0f, 190.0f, 0.30f,
                                            1.0f, 1.0f);

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
    QASetPtr(context, kQATag_Texture, texture);
    QASetInt(context, kQATag_TextureFilter, kQATextureFilter_Fast);
    QASetInt(context, kQATag_TextureOp, kQATextureOp_None);
    QASetInt(context, kQATagGL_TextureWrapU, kQAGL_Clamp);
    QASetInt(context, kQATagGL_TextureWrapV, kQAGL_Clamp);
    QADrawVTexture(context, 4, kQAVertexMode_Strip,
                   texturedQuad, flags);
    error = QARenderEnd(context, &dirty);
    if (error == kQANoErr) {
        error = QASync(context);
    }
    if (error == kQANoErr &&
        (!GXMetalPixelMatches(graphicsDevice,
                              deviceRect->left + 82,
                              deviceRect->top + 120,
                              kGXMetalPixelRed) ||
         !GXMetalPixelMatches(graphicsDevice,
                              deviceRect->left + 200,
                              deviceRect->top + 160,
                              kGXMetalPixelBlue) ||
         !GXMetalPixelMatches(graphicsDevice,
                              deviceRect->left + 160,
                              deviceRect->top + 190,
                              kGXMetalPixelPurple))) {
        error = kQAError;
    }
    QATextureDelete(engine, texture);
    return error;
}

int main(void)
{
    static const unsigned char kWindowTitle[] = {
        12, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'T', 'e', 's', 't'
    };
    Rect windowRect;
    WindowPtr window;
    TQADevice device;
    TQAEngine *engine;
    TQARect deviceRect;
    TQADrawContext *context = NULL;
    unsigned long optionalFeatures = 0;
    unsigned long optionalFeatures2 = 0;
    unsigned long requiredFeatures;
    TQAError error;

    GXMetalInitToolbox();
    SetRect(&windowRect, 70, 58, 70 + GXMETAL_WIDTH,
            58 + GXMETAL_HEIGHT);
    window = NewCWindow(NULL, &windowRect, kWindowTitle, true,
                        documentProc, (WindowPtr)-1, false, 0);
    if (window == NULL) {
        GXMetalShowResult(false, "The GXMetal test window could not be created.");
        return 1;
    }
    SetPort(window);

    memset(&device, 0, sizeof(device));
    device.deviceType = kQADeviceGDevice;
    device.device.gDevice = GetMainDevice();
    engine = GXMetalFindEngine(&device);
    if (engine == NULL) {
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal is not registered with RAVE. Install it, restart the Mac, and verify GXMetal acceleration is enabled for this machine.");
        return 1;
    }
    if (QAEngineGestalt(engine, kQAGestalt_OptionalFeatures,
                        &optionalFeatures) != kQANoErr ||
        QAEngineGestalt(engine, kQAGestalt_OptionalFeatures2,
                        &optionalFeatures2) != kQANoErr) {
        DisposeWindow(window);
        GXMetalShowResult(false, "GXMetal did not return its RAVE feature set.");
        return 1;
    }
    requiredFeatures = kQAOptional_Texture | kQAOptional_TextureHQ |
                       kQAOptional_Blend | kQAOptional_ClearDrawBuffer |
                       kQAOptional_ClearZBuffer;
    if ((optionalFeatures & requiredFeatures) != requiredFeatures ||
        (optionalFeatures2 & kQAOptional2_SwapBuffers) == 0) {
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal registered, but the host did not expose the complete depth, blend, texture, and double-buffer feature set.");
        return 1;
    }

    deviceRect.left = windowRect.left;
    deviceRect.right = windowRect.right;
    deviceRect.top = windowRect.top;
    deviceRect.bottom = windowRect.bottom;
    error = QADrawContextNew(&device, &deviceRect, NULL, engine,
                             kQAContext_DoubleBuffer, &context);
    if (error == kQANoErr) {
        error = GXMetalRenderPattern(context, engine,
                                     device.device.gDevice, &deviceRect);
    }
    if (context != NULL) {
        QADrawContextDelete(context);
    }
    if (error != kQANoErr) {
        DisposeWindow(window);
        GXMetalShowResult(false,
            "GXMetal was selected, but the depth/texture/double-buffer render test did not complete.");
        return 1;
    }
    GXMetalShowResult(true,
        "GXMetal passed RAVE discovery plus framebuffer-verified depth, Gouraud, alpha blend, texture upload/filtering, presentation, and double-buffer synchronization.");
    DisposeWindow(window);
    return 0;
}
