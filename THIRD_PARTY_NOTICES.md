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

The exact corresponding source is reproducible from the ClassicMac 1.6.1
source at <https://github.com/amcchord/ClassicMac/tree/v1.6.1>. The repository's
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
