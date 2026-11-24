/* ClassicMac WGL backend, GPL-2.0-or-later. Included only on 64-bit Windows.
 * All context/window operations stay on the ATI renderer thread. */
#include <windows.h>

static struct {
    HMODULE library;
    HWND window;
    HDC dc;
    HGLRC context;
    HGLRC (WINAPI *CreateContext)(HDC);
    BOOL (WINAPI *DeleteContext)(HGLRC);
    BOOL (WINAPI *MakeCurrent)(HDC, HGLRC);
    PROC (WINAPI *GetProcAddress)(LPCSTR);
} cm_wgl;

static void *cm_gl_symbol(const char *name)
{
    PROC p = cm_wgl.GetProcAddress(name);
    /* WGL uses these sentinel values for unsupported functions. */
    if (!p || (uintptr_t)p <= 3 || (intptr_t)p == -1) {
        p = GetProcAddress(cm_wgl.library, name);
    }
    return (void *)p;
}

static bool cm_renderer_has_ci(const char *haystack, const char *needle)
{
    size_t nlen;
    if (!haystack || !needle || !*needle) {
        return false;
    }
    nlen = strlen(needle);
    for (; *haystack; haystack++) {
        size_t i = 0;
        while (i < nlen && haystack[i]) {
            char a = haystack[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) break;
            i++;
        }
        if (i == nlen) {
            return true;
        }
    }
    return false;
}

void ati_gl_close(void)
{
    if (cm_wgl.MakeCurrent) cm_wgl.MakeCurrent(NULL, NULL);
    if (cm_wgl.context && cm_wgl.DeleteContext) cm_wgl.DeleteContext(cm_wgl.context);
    cm_wgl.context = NULL;
    if (cm_wgl.dc) ReleaseDC(cm_wgl.window, cm_wgl.dc);
    cm_wgl.dc = NULL;
    if (cm_wgl.window) DestroyWindow(cm_wgl.window);
    cm_wgl.window = NULL;
    /* Keep the system GL module loaded, matching the other host backends. */
}

bool ati_gl_open(ATIGL *gl, char *info, size_t infolen)
{
    const wchar_t *class_name = L"ClassicMacATIHiddenGL";
    WNDCLASSW cls = { 0 };
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    HGLRC legacy = NULL;
    HGLRC (WINAPI *create_attribs)(HDC, HGLRC, const int *);
    const int attributes[] = {
        0x2091, 4, /* WGL_CONTEXT_MAJOR_VERSION_ARB */
        0x2092, 1, /* WGL_CONTEXT_MINOR_VERSION_ARB */
        0x9126, 1, /* WGL_CONTEXT_PROFILE_MASK_ARB, CORE_PROFILE_BIT_ARB */
        0
    };
    const char *version, *renderer;
    int format;

    memset(gl, 0, sizeof(*gl));
    snprintf(info, infolen, "Windows OpenGL 4.1 is unavailable");
    if (!cm_wgl.library) {
        cm_wgl.library = LoadLibraryExW(L"opengl32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!cm_wgl.library) return false;
#define WGL_LOAD(member, symbol) \
    *(void **)&cm_wgl.member = (void *)GetProcAddress(cm_wgl.library, symbol)
    WGL_LOAD(CreateContext, "wglCreateContext");
    WGL_LOAD(DeleteContext, "wglDeleteContext");
    WGL_LOAD(MakeCurrent, "wglMakeCurrent");
    WGL_LOAD(GetProcAddress, "wglGetProcAddress");
#undef WGL_LOAD
    if (!cm_wgl.CreateContext || !cm_wgl.DeleteContext ||
        !cm_wgl.MakeCurrent || !cm_wgl.GetProcAddress) goto fail;
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = class_name;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) goto fail;
    cm_wgl.window = CreateWindowExW(0, class_name, L"", WS_POPUP,
                                  0, 0, 1, 1, NULL, NULL, cls.hInstance, NULL);
    if (!cm_wgl.window) goto fail;
    cm_wgl.dc = GetDC(cm_wgl.window);
    if (!cm_wgl.dc) goto fail;
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    format = ChoosePixelFormat(cm_wgl.dc, &pfd);
    if (!format || !SetPixelFormat(cm_wgl.dc, format, &pfd)) goto fail;
    legacy = cm_wgl.CreateContext(cm_wgl.dc);
    if (!legacy || !cm_wgl.MakeCurrent(cm_wgl.dc, legacy)) goto fail;
    create_attribs = (void *)cm_gl_symbol("wglCreateContextAttribsARB");
    if (!create_attribs) goto fail;
    cm_wgl.context = create_attribs(cm_wgl.dc, NULL, attributes);
    cm_wgl.MakeCurrent(NULL, NULL);
    cm_wgl.DeleteContext(legacy);
    legacy = NULL;
    if (!cm_wgl.context || !cm_wgl.MakeCurrent(cm_wgl.dc, cm_wgl.context)) goto fail;
#define ATI_GL_LOAD(ret, name, args) \
    *(void **)&gl->name = cm_gl_symbol("gl" #name); \
    if (!gl->name) { snprintf(info, infolen, "missing gl" #name); goto fail; }
    ATI_GL_FUNCS(ATI_GL_LOAD)
#undef ATI_GL_LOAD
    version = (const char *)gl->GetString(GL_VERSION);
    renderer = (const char *)gl->GetString(GL_RENDERER);
    if (!version || atoi((const char *)version) < 4) {
        snprintf(info, infolen, "OpenGL 4.1+ required (got %s)",
                 version ? version : "none");
        goto fail;
    }
    if (!renderer || cm_renderer_has_ci(renderer, "GDI Generic") ||
        cm_renderer_has_ci(renderer, "Microsoft Basic Render Driver") ||
        cm_renderer_has_ci(renderer, "WARP")) {
        snprintf(info, infolen, "software OpenGL renderer rejected (%s)",
                 renderer ? renderer : "unknown renderer");
        goto fail;
    }
    snprintf(info, infolen, "WGL; GL %s; %s", version, renderer);
    return true;
fail:
    if (legacy) { cm_wgl.MakeCurrent(NULL, NULL); cm_wgl.DeleteContext(legacy); }
    ati_gl_close();
    return false;
}
