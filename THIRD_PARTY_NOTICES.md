# Third-Party Notices

ClassicMac includes and dynamically links third-party open-source software.
The app bundle includes the applicable license texts in
`Contents/Resources/Licenses`.

## QEMU

ClassicMac includes a modified build of QEMU 11.0.2 and QEMU firmware. The
QEMU emulator as a whole is licensed under the GNU General Public License,
version 2. Individual source and firmware files may carry compatible licenses,
as described by QEMU's `LICENSE` file and their source headers.

The exact corresponding source is reproducible from the ClassicMac 1.2.1
source at <https://github.com/amcchord/ClassicMac/tree/v1.2.1>. The repository's
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
