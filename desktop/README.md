# Windows and Linux

The desktop frontend is now native C++/Qt, under [`native/`](../native/README.md).
Electron and its renderer, package dependencies, IPC host and browser client
have been removed from the desktop application. This directory retains the
platform QEMU build/staging scripts and shared exported image/font assets.

Download and run `ClassicMac-4.6.0-windows-x64.exe` on Windows 11, or mark
`ClassicMac-4.6.0-linux-x64.AppImage` executable and run it on Linux.
Both are single portable downloads. On Linux, `--appimage-extract-and-run`
can be used when FUSE is unavailable. Windows binaries remain unsigned.

New Mac, Open Mac, Library and About are in the window. Running Macs have
native screen windows with pause/resume, restart, shutdown, paste and full
screen controls. Escape releases a captured game mouse. Use Shut Down inside
Mac OS; Force Off is available in Media for an unresponsive guest.
