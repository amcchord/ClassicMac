# Third-Party Notices

ClassicMac includes and dynamically links third-party open-source software.
The macOS bundle keeps applicable license texts in
`Contents/Resources/Licenses`; the iPad bundle includes its UTM license and
third-party settings notices as application resources.

## UTM

ClassicMac for iPad is built on the UTM SE application and its shared-framework
QEMU runtime. UTM is licensed under the Apache License 2.0. The iPad build pins
UTM commit `8e4de50817e76a83d6840212311627a78dd4f8b2`, applies the changes in
`ios/utm-classicmac.patch`, and includes UTM's complete `LICENSE` file in the
application bundle.

Upstream source: <https://github.com/utmapp/UTM/tree/8e4de50817e76a83d6840212311627a78dd4f8b2>

## QEMU

ClassicMac includes a modified build of QEMU 11.0.2 and QEMU firmware. The
QEMU emulator as a whole is licensed under the GNU General Public License,
version 2. Individual source and firmware files may carry compatible licenses,
as described by QEMU's `LICENSE` file and their source headers.

The exact corresponding source is reproducible from the ClassicMac 4.2.0
source at <https://github.com/amcchord/ClassicMac/tree/v4.2.0>. The repository's
`scripts/build-qemu.sh` retrieves the pinned upstream QEMU 11.0.2 source and
applies every ClassicMac modification stored in the repository.

For at least three years after this binary release, any third party may also
request the complete corresponding QEMU source code on a physical medium for
no more than the cost of physically performing that distribution. Submit a
request through <https://github.com/amcchord/ClassicMac/issues>.

Upstream QEMU source: <https://gitlab.com/qemu-project/qemu/-/tree/v11.0.2>

## Bundled libraries

The self-contained QEMU helper applications also bundle these dynamically
linked libraries:

- pixman — MIT
- libpng — libpng-2.0
- GLib — LGPL-2.1-or-later
- Zstandard — BSD-3-Clause or GPL-2.0-only, plus BSD-2-Clause and MIT portions
- libslirp — BSD-3-Clause
- libusb — LGPL-2.1-or-later
- GNU libintl/gettext runtime — LGPL-2.1-or-later and GPL-3.0-or-later portions
- PCRE2 — BSD-3-Clause

Their license texts are copied from the exact Homebrew installations used to
produce the release. Source code is available from each project's upstream
site and through Homebrew's corresponding formula source archives.

## noVNC

ClassicMac includes the noVNC 1.7.0 browser VNC client, modified to support
QEMU's pointer-type-change extension and ClassicMac's optional secondary-click
and scrolling helpers. noVNC is licensed under the Mozilla Public License 2.0;
its JavaScript source and license text are included in the application bundle.

Upstream source: <https://github.com/novnc/noVNC/tree/v1.7.0>

noVNC includes an ES-module adaptation of pako 1.0.3 under the MIT License.
The pako license text is included with the browser source and in the
application's Licenses directory.

Upstream source: <https://github.com/nodeca/pako/tree/1.0.3>

## Copland engine: DingusPPC and SDL2

Copland uses a separate, modified DingusPPC helper, licensed under GPL-3.0-or-later.
It pins Michael Steil's `copland-boot` fork at
`8dcac6fb160adfd8860c2252fba321d412b2c8a6`, including eleven Copland hardware fixes.
The RTC patch and debugger protocol derive from his `wasm-port` work. ClassicMac's
patches, silent audio backend, build instructions and protocol tests are in
`copland/` and `scripts/build-copland.sh`. This is an independent integration and
is not an upstream DingusPPC release.

Source: <https://github.com/mist64/dingusppc/tree/8dcac6fb160adfd8860c2252fba321d412b2c8a6>
Research and working reference: <https://www.pagetable.com/300>

The helper statically links SDL2 2.32.10 (zlib license) and Capstone (BSD license,
with LLVM-derived portions under the included LLVM license). Cubeb's ISC license
is also included for the pinned build dependencies. Full notices are bundled
under `Contents/Resources/Licenses`. The corresponding DingusPPC, SDL2 and pinned
submodule sources are supplied in the Copland source archive beside the release;
ClassicMac's own source and build patches are in the release's tagged repository.
Apple's guest software and ROM are separate from these open-source components.

## The Iconfactory

The bundled classic Macintosh machine icons are copyright The Iconfactory,
Inc. and are included under separate permission confirmed by Austin McChord
on October 18, 2026. The G4 Side List View is from G4 v2; the 800/8000 series
and 7200+series/G3 Desktop icons are from the Apple Desktop Collection.
The original, unmodified icns resources, collection licenses and readmes
are in `Resources/Iconfactory`. This permission does not relicense the
artwork or grant downstream redistribution rights.

Artist and collections: <https://freeware.iconfactory.com>

## Platinum appearance

PlatinumKit includes MIT-licensed fonts, icons and control tables shared
with RetroBoot. Their notices are in `platinum/fonts/LICENSE-fonts.txt`
and `platinum/icons/LICENSE-icons.txt`. System 12 printable ASCII uses the
M5Tab-Macintosh screenshot-traced pixel recreation; the original Apple font
program is not included. Iconfactory artwork has its own
terms as described above.

## Windows and Linux desktop distributions

The shared desktop front end uses Electron (MIT), Chromium and Node.js.
Their license notices and Electron's third-party license inventory accompany
its runtime. QEMU corresponding-source archives accompany each platform's
release. The Windows runtime stages its MSYS2 dependency license notices;
the Linux runtime includes distribution copyright notices for its copied
shared libraries. Linux glibc, graphics/audio drivers and EGL/GLES libraries
are supplied by the host distribution. Linux builds target Ubuntu 24.04's
glibc baseline; the Linux ATI backend uses EGL and OpenGL ES 3.

Upstream Electron: <https://github.com/electron/electron>
Upstream Node.js: <https://github.com/nodejs/node>
Distribution library sources are available from the matching Ubuntu/MSYS2
source-package repositories, identified by the bundled notices. Contact the
ClassicMac maintainer for the corresponding library sources under the terms
of those licenses. The application contains no additional guest OS disk.

## RetroBoot Power Mac sound timing

The Screamer timing patches in `screamer/patches/` and their device-level test
are from RetroBoot commit `1860881266449ca2575061e0bd4055c1265cf217`.
The patches are GPL-2.0-or-later, matching the modified QEMU integration.
Original source notices are retained; provenance and hashes are in
`screamer/README.md`. Corresponding prepared engine sources accompany releases.

## Original Mac OS icons

The CD, floppy, folder, clipboard, display, alert and other system artwork
identified in `platinum/macos-icons/manifest.json` is copyright Apple Computer,
Inc. Original Mac OS 9 resource members are preserved, including native
32-bit colour and 8-bit alpha masks where available. These assets are separate
from the MIT-licensed Platinum drawings and Iconfactory artwork. Provenance
and attribution accompany every desktop package in its Licenses directory.
