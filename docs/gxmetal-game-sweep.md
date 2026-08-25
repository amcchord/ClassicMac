# GXMetal game compatibility sweep harness

`scripts/gxmetal-game-sweep.py` runs repeatable Mac OS game sessions without
ever giving QEMU the source disk image. Each game and rendering-mode variant
gets a separate writable raw-disk clone, QEMU process, Unix VNC socket,
monitor socket, serial log, profile log, event timeline, and screenshot set.
The instances can run concurrently.

The harness records a SHA-256 digest of the base before and after the complete
sweep. QEMU command records make it auditable that only the per-run clone was
attached as a writable disk. Game CD images and the optional ClassicMac Tools
CD are attached read-only. Successful clones are discarded by default;
failed clones are retained for diagnosis.

## Prerequisites

- A raw, bootable Mac OS 9 disk containing the GXMetal build under test and
  the games to be exercised, or game CD images with scripted installation and
  launch steps. Treat this as a golden image and do not use it interactively.
- `vendor/qemu/build/qemu-system-ppc`, `shared/ndrvloader`, and
  `vendor/qemu/pc-bios` from this repository, or explicit replacement paths.
- Enough CPU and RAM for the requested parallelism. Two 512 MB guests is the
  conservative default. Raising `--jobs` changes resource contention and can
  invalidate performance comparisons.
- A copy of the example manifest. Keep downloaded game binaries and raw disk
  images outside Git; record the source URL and SHA-256 in the manifest.

The repository's ignored `context/issue9-test.img` is an empty 2 GiB sparse
placeholder, not a bootable test system. The local
`context/macos_921_ppc.iso` is a Mac OS 9.2.1 installation CD, but the sweep
still requires a separately prepared and verified golden disk. Never adopt a
file as the golden image merely because its name suggests it is a test disk:
boot it, run GXMetal Test, and record its installed OS and driver versions.

## Prepare a manifest

Copy `gxmetal/compatibility/sweep-manifest.example.json` to an evidence or
working directory and add one entry per game. Relative `cdrom` paths resolve
relative to the manifest. The optional `source_sha256` is verified against the
attached CD image before QEMU starts.

Each game supports these fields:

- `id` and `name`: stable machine and display names.
- `modes`: `gxmetal`, `software`, or both. The software variant exposes the
  same VGA device with GXMetal disabled, providing a useful launch and visual
  baseline from an otherwise identical clone.
- `source_url`, `source_sha256`, and `cdrom`: media provenance and an optional
  read-only raw/ISO CD attachment.
- `boot_wait_seconds`, `observation_seconds`, `capture_interval_seconds`, and
  `resolution`: per-game timing/display overrides.
- `steps`: ordered VNC actions. Supported actions are `wait`,
  `wait_for_frame_change`, `click`, `double_click`, `key`, `chord`, `text`,
  `screenshot`, and `note`. A step can also set `delay_after`, `hold_ms`, or
  `capture_after`.

A step contains exactly one action. For example:

```json
[
  {"double_click": [941, 59], "delay_after": 2, "capture_after": true},
  {"key": "Return", "delay_after": 1},
  {"note": "Accepted the game's license dialog with the user's authorization."},
  {"click": [520, 430], "capture_after": true},
  {"wait": 30},
  {"screenshot": "first-live-gameplay"}
]
```

For slow or variable launch paths, synchronize on visible guest progress
instead of relying only on a long fixed delay:

```json
{
  "wait_for_frame_change": {
    "timeout_seconds": 180,
    "poll_interval_seconds": 1,
    "minimum_changed_fraction": 0.05,
    "channel_tolerance": 8
  },
  "capture_after": true
}
```

The action records its baseline and detected frame, ignores per-channel noise
up to `channel_tolerance`, and fails the run if the requested fraction of
pixels does not change before the timeout. A display resize counts as a full
frame change. This makes missed launch transitions explicit in the evidence
rather than silently sending later input to the wrong screen.

`Super_L` is the Mac Command key in QEMU VNC, so a chord such as
`{"chord":"Super_L+o"}` sends Command-O. Coordinates are tied to the
specified guest resolution and Finder layout. Capture the screen after every
navigation transition while authoring a new recipe. Never silently click a
license dialog: use a `note` immediately before the action and preserve a
screenshot showing what was accepted.

Named keys are case-sensitive and validated before a VM is created. Use names
such as `Space`, `Return`, `Escape`, `Left`, and `Super_L`; ordinary printable
characters such as `o` are written as a single character.

## Validate, then run

The dry run validates the manifest, all media paths, every VNC action, and the
selected variants without creating clones:

```sh
python3 scripts/gxmetal-game-sweep.py \
  context/issue9-test.img \
  gxmetal/compatibility/sweep-manifest.example.json \
  --modes gxmetal,software \
  --dry-run
```

Run two isolated guests at a time and retain the evidence under a new,
explicit directory:

```sh
python3 scripts/gxmetal-game-sweep.py \
  /path/to/gxmetal-os9-golden.img \
  /path/to/ten-game-sweep.json \
  --modes gxmetal,software \
  --jobs 2 \
  --output /path/to/evidence/gxmetal-sweep-YYYYMMDD
```

Use the exact QEMU, loader, firmware, and Tools CD from a release candidate
when testing a packaged release:

```sh
python3 scripts/gxmetal-game-sweep.py BASE.img MANIFEST.json \
  --qemu '/path/to/ClassicMac.app/Contents/Helpers/Power Mac G4.app/Contents/MacOS/qemu-system-ppc' \
  --loader '/path/to/ClassicMac.app/Contents/Resources/ndrvloader' \
  --firmware '/path/to/ClassicMac.app/Contents/Resources/qemu/pc-bios' \
  --tools-cd '/path/to/ClassicMac.app/Contents/Resources/ClassicMacTools.iso' \
  --output /path/to/evidence/release-name
```

Do not reuse an output directory; the harness refuses to overwrite one.
`--keep-disks` retains all modified clones when an installed result is needed
for a follow-up run. `--discard-failed-disks` opts out of the safer default of
retaining a failed clone.

## Evidence and review

The top-level `session.json` contains hashes for the manifest, executable,
loader, base disk, Tools CD, and game media, plus the host/Python identity,
QEMU version, repository commit/status, and before/after base-image integrity
result. `manifest.json` is the exact archived input, and `summary.json`
contains one automation result per variant.
Each run directory contains:

- `qemu-command.json`, `qemu.log`, and `serial.log`;
- `run.json` and the timestamped `events.jsonl` automation trace;
- lossless PNG frames in `screenshots/`;
- `result.json`, which reports automation completion but deliberately does
  not claim that the game rendered correctly;
- `review.json`, a checklist for launch, menus, gameplay, visuals, input,
  audio, stability, and whether GXMetal presentation profiles appeared.

The runner attaches ClassicMac's `virtio-tablet-pci` device alongside the NDRV
loader. This matches the normal Power Mac launcher and gives Finder/menu VNC
steps deterministic absolute coordinates. GXMetal Input hands the host back to
captured relative motion only while an InputSprocket game requests it.

Populate every `review.json` from the screenshots, logs, and an interactive
VNC follow-up where needed. “Automation complete” only means the recipe ran.
Compatibility should be reported separately for launch, menus, gameplay,
visual correctness, and sustained stability. Compare matched GXMetal and
software captures before assigning a visual regression to the driver.

For performance measurements, first finish the correctness pass. Then run one
guest at a time (`--jobs 1`) with identical steps and timing. GXMetal mode sets
`GXMETAL_PROFILE=1`, so rolling presentation FPS, draw counts, batching, and
resource lookup telemetry is preserved in `qemu.log`.

Decode a persisted `GXMetal-Driver-Trace.bin` without duplicating the guest
snapshot layout in an analysis script:

```sh
python3 scripts/decode-gxmetal-diagnostics.py \
  path/to/GXMetal-Driver-Trace.bin > driver-trace.json
```

The decoder derives field order, signedness, and array extents from the current
`GXMetalDiagnostics.h`, and rejects snapshots whose exact size does not match
that schema.

## Ten-game sweep discipline

Use a staged process so discoveries remain attributable:

1. Record source, version, archive URL, checksum, and license/EULA action.
2. Install and configure the game on a disposable clone, then promote a clean
   copy to the externally stored golden image only after documenting it.
3. Author a deterministic route through launch, menus, and at least two
   minutes of representative gameplay. Capture title, menu, load, gameplay,
   effects-heavy, and exit states.
4. Run GXMetal and software variants from fresh clones. Never continue the
   second variant from the first variant's modified disk.
5. Review evidence and enter precise failures, including time, screenshot,
   display depth/resolution, and last successful transition.
6. Turn every driver defect into a small native or in-guest regression test.
7. Rerun the affected game plus one unrelated game after each driver change.
8. Tag a release only after native tests, GXMetal Test, and all currently
   supported game recipes pass from an immutable release-candidate base.

This structure separates media acquisition and human visual judgment from the
repeatable execution layer, while retaining enough evidence for another
archivist to reproduce each compatibility claim.
