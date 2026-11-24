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

The exact corresponding source is reproducible from the ClassicMac 4.7.0
source at <https://github.com/amcchord/ClassicMac/tree/v4.7.0>. The repository's
`scripts/build-qemu.sh` retrieves the pinned upstream QEMU 11.0.2 source and
applies every ClassicMac modification stored in the repository.

For at least three years after this binary release, any third party may also
request the complete corresponding QEMU source code on a physical medium for
no more than the cost of physically performing that distribution. Submit a
request through <https://github.com/amcchord/ClassicMac/issues>.

Upstream QEMU source: <https://gitlab.com/qemu-project/qemu/-/tree/v11.0.2>

## Bundled libraries

The self-contained QEMU helper applications also bundle these dynamically
linked libraries on macOS (as applicable to the configured build):

- pixman — MIT
- libpng — libpng-2.0
- GLib (`libglib`, `libgobject`, `libgmodule`, `libgio`, `libgthread`) — LGPL-2.1-or-later
- Zstandard — BSD-3-Clause or GPL-2.0-only, plus BSD-2-Clause and MIT portions
- libslirp — BSD-3-Clause
- libusb — LGPL-2.1-or-later (when USB passthrough is enabled)
- GNU libintl/gettext runtime — LGPL-2.1-or-later and GPL-3.0-or-later portions
- PCRE2 — BSD-3-Clause
- Device Tree Compiler (`dtc` / `libfdt`, when dynamically linked) — GPL-2.0-or-later OR BSD-2-Clause

Their license texts are copied from the exact Homebrew installations used to
produce the release (and recorded in each helper's `release-libraries.json`).
Source code is available from each project's upstream site and through
Homebrew's corresponding formula source archives.

## classicvirtio

Guest Virtio drivers for shared folders (`shared/declrom`, `shared/ndrvloader`)
and Quadra removable floppy disks build from `elliotnunn/classicvirtio`
(`fc401b4c731027cb4068f3415def7fe79f6659da`, MIT) with ClassicMac and RetroBoot
patches under `classicvirtio/`.

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
with LLVM-derived portions under the included LLVM license), and uses CLI11
(BSD-3-Clause) for command-line parsing. Cubeb's ISC license is also included
for the pinned build dependencies. Full notices are bundled under
`Contents/Resources/Licenses`. The corresponding DingusPPC, SDL2 and pinned
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
and `platinum/icons/LICENSE-icons.txt`. The three fonts' Mac Roman strikes
are bitmap recreations measured from unsmoothed Mac OS 9.2.1 QuickDraw output;
the original Apple font programs are not included. Reference hashes and the
measurement method are in `platinum/reference/README.md`. Iconfactory artwork has its own
terms as described above.

## Windows and Linux desktop distributions

ClassicMac 4.6 and later use the native C++/Qt frontend described below.
ClassicMac 4.1–4.5 used Electron (MIT), Chromium and Node.js; their notices
and third-party inventories accompany those older releases. QEMU
corresponding-source archives accompany each platform's release. The Windows runtime stages its MSYS2 dependency license notices;
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

The original Screamer timing patches and their device-level test are from
RetroBoot commit `1860881266449ca2575061e0bd4055c1265cf217`.
The patches are GPL-2.0-or-later, matching the modified QEMU integration.
Original source notices are retained; provenance and hashes are in
`screamer/README.md`. Corresponding prepared engine sources accompany releases.

The November engine import, including the additional strict pre-roll patch,
is from RetroBoot `432a43d4ba8502a7afeaa5dd25ecf959c23aed1c` (2.0.1).
`docs/retroboot-2.0.1-import.json` records all CPU, VGA, Quadra, ATI and sound
patch hashes and video-driver adaptations. Upstream QEMU/driver copyright
and GPL notices are retained in the patches and sources.

The Radeon cursor correction is ported from RetroBoot
`88d074a55b00af7507e9412734ee83fd988876b3` (2.0.2), retaining only the cursor
composition/state hunks of its macOS X AGP patch. QEMU changes remain
GPL-2.0-or-later. The synthetic cursor test is adapted from that revision's
`research/gfx/tests/qtest_radeon_cursor.py`; provenance, hashes and scope are
in `docs/retroboot-2.0.2-cursor-import.json` and `ati/README.md`.

### RetroBoot 3.0.0 parity import

Additional engine, guest-driver, and catalog parity changes are ported from
RetroBoot `37664e17` (`v3.0.0`, <https://github.com/amcchord/RetroBoot/tree/v3.0.0>):
Power Mac IDE PIO status drain (`macio-ide.pio-status-drain`) and ATAPI zero-byte
abort IRQ (`startup/`), OHCI idle-frame batching and HID idle handling (`usb/`),
Quadra QFB snapshot-mode consistency and slow-host Timer 2 hold (`qfb/`), and
classicvirtio shared-folder date and directory-cache patches (`classicvirtio/`),
plus GNU sparse 1.0 tar archive reading (`app/Sources/CMachineArchive/sparse_tar.c`)
for the shared public RetroBoot download catalog. QEMU modifications remain
GPL-2.0-or-later, and provenance and test verification are recorded in
`docs/retroboot-3.0.0-parity.md`.

## Original Mac OS icons

The CD, floppy, folder, clipboard, display, alert and other system artwork
identified in `platinum/macos-icons/manifest.json` is copyright Apple Computer,
Inc. Original Mac OS 9 resource members are preserved, including native
32-bit colour and 8-bit alpha masks where available. These assets are separate
from the MIT-licensed Platinum drawings and Iconfactory artwork. Provenance
and attribution accompany every desktop package in its Licenses directory.

## Native Windows/Linux frontend (4.6 and later)

Qt 6 Core, Gui, Widgets, Network and Concurrent and their platform plugins are
unmodified, dynamically linked libraries under LGPL-3.0 (or applicable Qt
module licenses). Notices are bundled in Licenses. Corresponding upstream
source: https://download.qt.io/official_releases/qt/ . The package manifest
identifies every bundled binary. Extract the portable package as documented
in native/README.md to inspect or replace compatible Qt libraries and relink.
The ClassicMac C++ frontend source and CMake build are in this repository.

OpenSSL is dynamically linked under Apache-2.0; upstream source and license:
https://www.openssl.org/source/ and https://www.openssl.org/source/license.html .
Its legacy DES operation is used only for the loopback RFB password challenge;
HTTPS uses the platform Qt TLS backend with normal certificate verification.

zlib is dynamically linked under the zlib License (<https://zlib.net/zlib_license.html>).
On Windows, per-package MSYS2 dependency license notices (covering Qt 6,
OpenSSL, zlib, GCC runtime / winpthreads, GLib, pixman, libpng, Zstandard,
libslirp, GNU libintl/gettext, PCRE2, ICU, HarfBuzz, FreeType, Graphite2,
Brotli, bzip2, libiconv, libjpeg-turbo, libffi, dtc, double-conversion, md4c,
libb2, libgcrypt/libgpg-error, GnuTLS/Nettle/GMP/libtasn1/p11-kit/libunistring/libidn2,
cURL/libssh2/libpsl/nghttp2/nghttp3/ngtcp2, and any other bundled DLLs) are
staged in `Licenses/MSYS2` and `runtime/Licenses/MSYS2-runtime`. On Linux,
distribution copyright notices for all bundled shared libraries are staged in
`Licenses/` and `runtime/Licenses/`.

The native desktop packages contain no Electron or Chromium runtime. The
optional browser display in the macOS edition retains its existing noVNC
and pako notices above.

### PowerPC Mac OS X engine

ClassicMac includes QEMU 10.0.13 for Mac OS X, under the same QEMU licenses
described above. The corresponding QEMU 10 engine source is included in
the platform QEMU source archives. The RetroBoot patch, Windows adapter,
OpenBIOS source pins/patches and VGA driver build provenance are in
`osx/` in the matching ClassicMac source. The OS X OpenBIOS firmware is
GPL-2.0-or-later, with upstream notices retained in its source.
