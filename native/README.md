# Native Windows and Linux frontend

ClassicMac 4.6 uses compiled C++17/Qt Widgets, direct QMP process pipes and a
native authenticated RFB client. It does not use Electron, Node, Chromium,
HTML, a web view or a localhost web server. The existing `.classic` folders
and `classicmac-desktop/library.json` preferences remain compatible.

The UI draws integer pixels from the Mac PlatinumKit measurement tables and
the shared MIT BDF fonts. Host themes and font rasterizers do not draw the
window chrome, labels, buttons, toggles, popup buttons or fields. Scrollbars
use the Mac renderer's original tables. Qt supplies native window/input,
accessibility, editing, file-dialog, TLS, networking and process services.

Build with Qt 6.4+ Widgets/Network/Concurrent, OpenSSL Crypto, zlib and CMake:

```
cmake -S native -B output/native-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build output/native-build
QT_QPA_PLATFORM=offscreen output/native-build/classicmac --self-test app/Tests/ClassicMacTests/Fixtures/Platinum output/native-pixels
python3 native/scripts/export-assets.py --check
```

Build QEMU with `desktop/scripts/build-engine.sh` on MSYS2 UCRT64 or
`desktop/scripts/build-linux.sh` on Linux. The Windows packager needs NSIS;
the Linux packager uses a checksum-pinned AppImage tool. Run the matching
`native/scripts/package-{windows,linux}.py output/native-build`.

Windows ships one portable EXE, extracts into a private temporary folder and
waits for the native app to exit. `--extract=C:\path\to\new-folder` exposes
the dynamic libraries for inspection or replacement. Linux ships one AppImage;
`--appimage-extract` exposes its contents. Qt/OpenSSL remain dynamically linked.
Replace compatible libraries in an extracted copy and run `classicmac.exe`
or `AppRun`. No installation or system-wide configuration is performed.

The single download contains the app, QEMU engines, firmware, Tools CD,
dynamic libraries and license notices. Writable machine disks and preferences
live outside it. Linux requires x86-64 glibc 2.39+ and a working X11 or Wayland
desktop; audio uses the host service. Copland remains macOS-only, and the
Windows QEMU build does not support host folder sharing.

The Library uses catalog-v2.json, bounded resumable downloads, SHA-256 and
flat archive extraction. New raw disks and imported zero blocks are sparse,
including explicit NTFS sparse marking on Windows. PR #36's prepared Quadra
and Mac OS 8.6 defaults are preserved; the 8.6 profile uses the matched loader,
Virtio startup, G3, ADB, RTL8139 and opt-in timing. Media/hardware changes are
available after shutdown. Quit never silently kills a mounted guest disk.

`--qualify recipe.json` is enabled only with explicit isolated test home,
documents and qualification environment variables. It operates the real
native widgets and engine and records screenshots, state and results. It
has no remote listener. Keep qualification output and disposable guests in
ignored `output/` directories.
