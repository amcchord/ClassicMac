/* A reference fixture, drawn by the guest's own Appearance Manager.
 * No production ClassicMac renderer code or artwork is used here. */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Events.h>
#include <Appearance.h>
#include <Icons.h>
#include <Resources.h>
#include <Files.h>
#include <Folders.h>
#include <stdio.h>
#include <string.h>

static WindowPtr window;
static short page = 1;
static void exportIconServices(void) {
    OSType types[] = { 'cddr', 'flpy', 'fldr', 'ofld', 'CLIP', 'caut', 'note', 'stop', 'hdsk', 'pref', 'trsh', 'gnet' };
    short i;
    for (i = 0; i < 12; ++i) {
        IconRef icon; IconFamilyHandle family; FSSpec spec;
        Str255 name; short file; long size;
        if (GetIconRef(kOnSystemDisk, kSystemIconsCreator, types[i], &icon) != noErr) continue;
        if (IconRefToIconFamily(icon, kSelectorAllAvailableData, &family) != noErr) { ReleaseIconRef(icon); continue; }
        name[0] = 14; BlockMoveData("Native-", name + 1, 7);
        BlockMoveData(&types[i], name + 8, 4); BlockMoveData(".icns", name + 12, 5); name[0] = 16;
        if (FSMakeFSSpec(0, 0, name, &spec) != noErr) FSpCreate(&spec, 'CMpr', 'icns', smSystemScript);
        if (FSpOpenDF(&spec, fsWrPerm, &file) == noErr) {
            size = GetHandleSize((Handle)family); HLock((Handle)family);
            FSWrite(file, &size, *family); HUnlock((Handle)family); FSClose(file);
        }
        DisposeHandle((Handle)family); ReleaseIconRef(icon);
    }
}
static void text(short x, short y, const char *s) {
    Str255 p; short n = strlen(s); p[0] = n;
    BlockMoveData(s, p + 1, n); MoveTo(x, y); DrawString(p);
}
static void button(short x, short y, short w, short h, ThemeButtonKind kind,
                   ThemeDrawState state, short value, short adornment) {
    Rect r; ThemeButtonDrawInfo info;
    SetRect(&r, x, y, x + w, y + h);
    info.state = state; info.value = value; info.adornment = adornment;
    DrawThemeButton(&r, kind, &info, NULL, NULL, NULL, 0);
}
static void draw(void) {
    Rect area, r; short i, state, x, y;
    SetPort(window);
    SetThemeBackground(kThemeBrushDialogBackgroundActive, 32, true);
    SetRect(&area, 0, 0, 800, 600); EraseRect(&area);
    RGBColor black = { 0, 0, 0 }; RGBForeColor(&black);
    UseThemeFont(kThemeSystemFont, smSystemScript);
    if (page == 3 || page == 4) {
        ThemeWindowMetrics m; ThemeWindowAttributes a;
        memset(&m, 0, sizeof(m)); m.metricSize = sizeof(m);
        m.titleHeight = 16; m.titleWidth = 70;
        a = kThemeWindowHasGrow | kThemeWindowHasFullZoom |
            kThemeWindowHasCloseBox | kThemeWindowHasCollapseBox | kThemeWindowHasTitleText;
        text(20, 25, "Native window frames: active / inactive / collapsed / modal");
        for (i = 0; i < 4; ++i) {
            SetRect(&r, 30 + (i % 2) * 380, 80 + (i / 2) * 220,
                    350 + (i % 2) * 380, 220 + (i / 2) * 220);
            DrawThemeWindowFrame(i == 3 ? kThemeDialogWindow : kThemeDocumentWindow,
                &r, i == 1 ? kThemeStateInactive : kThemeStateActive, &m,
                i == 2 ? a | kThemeWindowIsCollapsed : i == 3 ? 0 : a, NULL, 0);
            if (i < 3 && page == 4) {
                short widget;
                for (widget = 0; widget < 3; ++widget)
                    DrawThemeTitleBarWidget(kThemeDocumentWindow, &r,
                        kThemeStateActive, &m,
                        i == 2 ? a | kThemeWindowIsCollapsed : a, widget);
            }
        }
        for (i = 0; i < 3; ++i) {
            SetRect(&r, 30 + i * 150, 490, 130 + i * 150, 512);
            ThemeButtonDrawInfo b; b.state = i == 0 ? kThemeStateActive : i == 1 ? kThemeStatePressed : kThemeStateInactive;
            b.value = kThemeButtonOff; b.adornment = 0;
            DrawThemeButton(&r, kThemeSmallBevelButton, &b, NULL, NULL, NULL, 0);
        }
        return;
    }
    if (page == 2) {
        text(20, 25, "ClassicMac  Cancel  Shut Down  Get Info...  Name:");
        text(20, 55, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        text(20, 85, "abcdefghijklmnopqrstuvwxyz");
        text(20, 115, "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
        text(20, 155, "The quick brown fox jumps over the lazy dog.");
        text(20, 185, "Mac OS Library  Iconfactory Icons  ClassicMac Tools");
        return;
    }
    text(20, 25, "Native Mac OS Platinum reference: 1 controls, 2 fonts, Q quits");
    for (i = 0; i < 3; ++i) {
        x = 30 + i * 150;
        state = i == 0 ? kThemeStateActive : i == 1 ? kThemeStatePressed : kThemeStateInactive;
        text(x, 55, i == 0 ? "Normal" : i == 1 ? "Pressed" : "Disabled");
        button(x, 70, 80, 20, kThemePushButton, state, kThemeButtonOff, 0);
        button(x, 115, 80, 20, kThemePushButton, state, kThemeButtonOff, kThemeAdornmentDefault);
        button(x, 165, 14, 12, kThemeCheckBox, state, kThemeButtonOff, kThemeAdornmentDrawIndicatorOnly);
        button(x, 195, 14, 12, kThemeCheckBox, state, kThemeButtonOn, kThemeAdornmentDrawIndicatorOnly);
        button(x, 225, 14, 12, kThemeCheckBox, state, kThemeButtonMixed, kThemeAdornmentDrawIndicatorOnly);
        button(x, 265, 12, 12, kThemeRadioButton, state, kThemeButtonOff, kThemeAdornmentDrawIndicatorOnly);
        button(x, 295, 12, 12, kThemeRadioButton, state, kThemeButtonOn, kThemeAdornmentDrawIndicatorOnly);
        button(x, 335, 100, 20, kThemePopupButton, state, kThemeButtonOff, 0);
        SetRect(&r, x, 390, x + 100, 412); DrawThemeEditTextFrame(&r, state);
        SetRect(&r, x, 440, x + 100, 490); DrawThemeListBoxFrame(&r, state);
    }
    SetRect(&r, 500, 70, 750, 170); DrawThemePrimaryGroup(&r, kThemeStateActive);
    SetRect(&r, 500, 220, 750, 270); DrawThemeFocusRect(&r, true);
    for (i = 0; i < 2; ++i) {
        ThemeTrackDrawInfo t; memset(&t, 0, sizeof(t));
        t.kind = kThemeMediumProgressBar; SetRect(&t.bounds, 500, 320 + 40 * i, 750, 332 + 40 * i);
        t.min = 0; t.max = 100; t.value = i ? 70 : 30;
        t.attributes = kThemeTrackHorizontal; t.enableState = kThemeTrackActive;
        DrawThemeTrack(&t, NULL, NULL, 0);
    }
    y = 555; text(30, y, "Reference content origin is (50,70) in the raw guest framebuffer.");
}
int main(void) {
    Rect bounds; Str255 title; EventRecord event; Boolean done = false;
    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); TEInit(); InitDialogs(NULL); InitCursor();
    RegisterAppearanceClient();
    exportIconServices();
    title[0] = 18; BlockMoveData("Platinum Reference", title + 1, 18);
    SetRect(&bounds, 50, 70, 850, 670);
    window = NewCWindow(NULL, &bounds, title, true, zoomDocProc, (WindowPtr)-1, true, 0);
    SetPort(window); draw();
    while (!done) {
        if (!WaitNextEvent(everyEvent, &event, 60, NULL)) continue;
        if (event.what == updateEvt) { BeginUpdate(window); draw(); EndUpdate(window); }
        if (event.what == activateEvt) { SetPort(window); draw(); }
        if (event.what == keyDown) {
            char key = event.message & charCodeMask;
            if (key == 'q' || key == 'Q') done = true;
            else if (key >= '1' && key <= '4') { page = key - '0'; draw(); }
        }
        if (event.what == mouseDown) {
            WindowPtr hit; short part = FindWindow(event.where, &hit);
            if (part == inGoAway && TrackGoAway(hit, event.where)) done = true;
            else if (part == inDrag) DragWindow(hit, event.where, &qd.screenBits.bounds);
        }
    }
    DisposeWindow(window); UnregisterAppearanceClient(); return 0;
}
