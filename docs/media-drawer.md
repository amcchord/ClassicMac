# Media drawer

ClassicMac 3.0 has one media window for each machine, opened from the library,
the app's **Machine > Media…** command, the native guest's **Mac > Media…**
command, or **Media…** in the browser display. The same window shows the source
filenames for the current disc, ClassicMac Tools, and the Quadra floppy, along
with the next startup choice and recent images.

## Behavior

- When shut down, changes save directly to the machine's configuration.
- On a running Power Mac, changes are saved for the next startup. The drawer
  separately shows the image currently attached and the next startup image.
  Mac OS 9 does not reliably notice IDE swaps, so the app never claims those
  changes have appeared in the guest. Tools is a startup-mounted Virtio disk.
- On a running Quadra, the drawer reads actual drive state before each change
  and confirms it afterward. Disc/Tools changes confirm host drive insertion;
  whether Mac OS mounts a new CD also depends on its guest driver. The drawer
  explains that a restart may be needed if a disc does not appear in Mac OS.
  Floppy insertion is disabled until the current writable disk is
  ejected. Eject requests the classicvirtio guest flush/eject handshake and
  polls for completion for up to twelve seconds, with an actionable error if
  the Mac is still busy. It never force-ejects or force-replaces a floppy.
- Live operations are disabled while paused or when status is unavailable.
  Power Mac next-startup settings may still be edited while paused.
- A guest-side eject can leave the saved next-startup image unchanged; both
  states are shown explicitly when different.
- Choosing a game disc does not change the startup device. Ejecting the selected
  startup disc switches the next startup to the hard disk. Installer boot
  completion continues to use the launcher's existing blessing detection.
- Recent images are stored in host preferences, limited to twelve entries.
  Missing entries stay visible with **Locate…** and can be removed individually.
  The picker validates the new file before saving; missing, empty, unreadable,
  directory, and non-writable floppy selections fail without changing settings.
- Images remain in their chosen locations. The drawer accepts uncompressed raw
  CD/floppy images; it does not convert compressed disk formats.

## Integration

`MediaController.shared.present(for: vmID)` opens or focuses the shared window.
`MediaDrawerView(config:isRunning:isPaused:)` is also available for an embedded
view, where the supplied binding must save changes through `VMStore`.

The app delegate calls `MediaController.shared.startListening()` once. The
native menu uses `CLASSICMAC_VM_ID` and posts `com.classicmac.media` with only
the machine UUID as its object after releasing grabbed input. The listener
validates the UUID and requires a known, running machine. In QEMU launched
without that environment variable, the original native Disc/Tools/Floppy menus
remain available.

Apply `cocoaui/media-drawer.patch` after the existing Cocoa, paste-text, and
GXMetal status menu patches. Apply `monitor/media-control.patch` after the
existing boot-clock/GXMetal HMP patches. The latter modifies `hmp-commands.hx`,
`include/monitor/hmp.h`, and `monitor/hmp-cmds.c`; these files are already in the
build script's reset list. The integration owner registers both patches.

`classicmac-media status` returns a single `CLASSICMAC_MEDIA` JSON line with
`liveChanges` and `drives`. The other commands are `insert <drive> <filename>`
and `eject <drive>`. Only `cd0`, `tools0`, and `fd0` are accepted; runtime writes
are rejected unless the Quadra's floppy backend is present and the guest is
running. Paths use HMP quoting, and the app rejects control characters. Errors
are returned in the JSON result, never mistaken for a successful transaction.

Power Mac Tools uses a `-blockdev` node, which creates an unnamed BlockBackend.
The monitor resolves `inserted.node_name == "classicmac-tools"` and reports it
as logical `tools0`; when present, it suppresses the empty IDE Tools placeholder.
This identity was verified against QEMU 11.0.2 with a paused mac99 probe.

The browser's POST `/actions/media` requires the per-run capability token and
exact local Origin, using the same protection as Paste Text. It only opens the
local media window; it accepts no filenames and returns no filesystem paths.

For Power Mac pending changes to survive a guest restart, the launcher's
termination handler must read the latest saved configuration and preserve
explicit disc/startup edits against its installation-completion logic. The
integration branch supplies that change.

## Verification

Focused Swift tests cover status parsing, monitor errors, source names with
quotes and Unicode, command injection rejection, startup-device rules, invalid
files, recent image persistence/deduplication/limits, paused Quadra rejection,
Power Mac staging without a monitor connection, and browser token/origin checks.
The release Swift build passes in the feature worktree. The new Cocoa patch
applies after the existing Cocoa patches, Paste Text, and GXMetal status.

Before the 3.0 release, the integration pass should build the new QEMU patches
and check the drawer from all three entry points. On an OS 9 Power Mac, stage a
different disc and Tools setting, verify the live desktop stays unchanged, and
verify a shutdown/start or guest restart uses the pending settings. On a Quadra,
insert/replace/eject a SCSI CD; insert a writable floppy, write a guest file,
eject through the drawer, and reinsert it to verify the write survived. Confirm
that a busy/paused floppy is not force-removed. Check a missing recent image's
Locate flow and source filenames containing spaces, quotes, and accents.

### September 10, 2026 candidate runtime check

The signed 3.0.0 candidate passed the Power Mac Tools identity/staging checks,
Quadra pause/error checks, CD backend insertion/ejection checks, and preservation
of the old image after a failed replacement. In an OS 8.1 Finder, a disposable
floppy appeared, its volume was renamed to `Media QA`, and the guest completed
the safe eject handshake. Reading the ejected image confirmed that the guest's
rename was written to disk; all original source-image hashes were unchanged.

This OS 8.1 boot-CD fixture did not demonstrate Tools mounting in Finder when
inserted into an initially empty SCSI CD drive, or in the populated-at-start
CD eject/reinsert case. The app therefore describes confirmed drive insertion
and advises restarting if the disc does not appear in Mac OS. It does not claim
that a monitor acknowledgment proves guest-visible mounting. Full evidence is
retained under the integration checkout's
`output/3.0/test-evidence/media-runtime/`.
