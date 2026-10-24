# ClassicMac guest display service

The firmware loader attaches this MIT-licensed NDRV to the dedicated
`classicmac-display-control` PCI mailbox (1234:1112). It does not install or
modify guest files. It requires the ATI display software already needed by the
emulated Radeon R100.

A host window resize publishes width, height and a serial. In application event
context, after Finder has initialized, the service programs an inactive detailed
timing through ATI's own video driver and switches through Display Manager.
Two slots are reused: ATI can enumerate an in-use programmable slot first, so
allocation explicitly skips the other owned slot. The active timing is never
rewritten. The current color depth is retained and the returned desktop bounds
are checked before acknowledging success. Reduced blanking keeps 640×480 through
1920×1440 (including portrait sizes) below the R100 pixel-clock limit.

The event filter chains its predecessor, guards re-entry, coalesces requests,
waits across startup/pause, and bounds retries. DisplayLib is resolved at task
time because it is not available when boot-time NDRVs are initialized. A safe
unload restores the previous filter; a filter chained by someone else prevents
unloading the still-referenced code.

Mailbox registers are 32-bit big-endian: magic `CMrs` at0, requested width at4,
height at8, serial at12, signed result at16, completed serial at20. A new request
has result INT32_MAX. Zero result with matching completed serial confirms the
Mac adopted the mode. QOM `x-resize-status` exposes these fields for diagnostics.
The Radeon `host-resize-request` property re-arms a size left by a guest app.

Build with `scripts/build-classicvirtio-floppy.sh`; it applies the existing
classicvirtio patches and `classicvirtio/display-control.patch`, adds this source,
and produces the tracked `shared/ndrvloader`. The NDRV runtime and loader are
Elliot Nunn's MIT-licensed classicvirtio (pinned by that script); the existing
VirtIO drivers retain their behavior. The mailbox device and ATI integration are
GPL-2.0-or-later QEMU code under `ati/`.
