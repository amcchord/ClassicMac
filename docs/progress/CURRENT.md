# ClassicMac source synced with RetroBoot downloads — November 22, 2026

Source commit `e664ec0` adds the public RetroBoot catalog, sparse disk imports,
and the classic IDE, USB, Quadra and folder-sharing updates. The local
control checkout and GitHub main contain the source. The built Mac app is
under `output/retroboot-parity/ClassicMac.app`.

The supported Library shows 18 distinct editions across the two catalogs.
System 7.5.5, Mac OS 9.2.1 and Tiger 10.4.11 passed import and boot checks.
Median Finder startup in three default-clock Mac OS 9 boot pairs dropped
from 9.806 s to 8.952 s. See the [compatibility report](../retroboot-3.0.0-parity.md)
for measurement conditions and desktop build checks.

# ClassicMac 4.7.0 published on November 17, 2026

ClassicMac 4.7.0 is the stable latest GitHub release. The three desktop
packages boot Mac OS X 10.4.11 Tiger on the independent RetroBoot QEMU 10
engine. The signed Mac app also boots the original Tiger 10.4.6 DVD to the
installer and recognizes an Apple Partition Map target. The guest Apple menu
handles OS X shutdown and restart. OS X uses standard VGA.

The compiled application source is `2691984`. GitHub release ID 449345793
points to tag `v4.7.0` at `6fe3304`. All ten uploaded assets and anonymous
downloads match in size and SHA-256. The local source backup is verified
and 4.6.4 remains available. [GitHub release](https://github.com/amcchord/ClassicMac/releases/tag/v4.7.0).

Windows and Ubuntu package checks, Mac signing and notarization, and guest
and device coverage are recorded in the [validation report](../4.7.0-validation.md).
The installer and Finder test guests are stopped. Release work is complete.
