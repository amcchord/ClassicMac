# Actual Windows desktop qualification

Use a disposable Power Mac OS 9 machine imported through the Library. This
test changes its name to `Windows QA é ñ`, boots/restarts/shuts it down and
writes only an isolated test profile. It operates the real portable package
and sends Windows mouse/Unicode keyboard input. Keep other ClassicMac windows
closed. The active desktop must have the requested OS display scale; the test
checks `GetDpiForWindow` and Per-Monitor V2 awareness. Qt scale overrides are
removed from the child environment.

Before launching, the runner sets a distinct temporary name in the disposable
config. The native input must replace it and persist the expected accented name.

Run in the logged-in user's interactive desktop, rather than service session 0:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File native/tests/windows/run-desktop-flow.ps1 -Package C:/QA/ClassicMac-4.6.2-windows-x64.exe -Machine C:/QA/Test.classic -Output C:/QA/results/100 -DesktopScale 100
```

Repeat at actual 100/125/150/175/200% Windows desktop scale, with a new output
folder each time. The 59 steps cover Home, New Mac/model popup, settings/graphics
popup, Media, Library, setup/license, native accented input, guest Apple-menu
mouse input, narrow/full-screen display, pause/resume, engine replacement on
restart, Paste and clean shutdown. The default boot wait is 55 seconds;
`-BootWaitMilliseconds 110000` allows a slower fixture. This is elapsed startup
time, not proof of a healthy Finder: inspect the screenshots as well.

Check collected captures using Python with Pillow:

```sh
python3 native/scripts/check-ui-pixel-grid.py C:/QA/results --windows-matrix --output C:/QA/grid.json
python3 native/scripts/check-guest-pixels.py C:/QA/results --output C:/QA/guest.json
```

Each native screen capture is cropped from the actual Windows desktop and
compared exactly with the application-rendered capture. Guest pixels are
compared separately with raw RFB frames. Full screen has no host title bar.
Reference component tests (`--self-test`) remain separate from the desktop
grid and input tests. These flows do not qualify mixed-monitor moves, IME
composition, accelerated 3D, sound output or performance.
