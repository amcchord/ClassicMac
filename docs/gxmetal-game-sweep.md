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
  `wait_for_frame_change`, `wait_for_pixel`, `click`, `hold_click`,
  `double_click`, `key`, `drag`, `chord`, `text`, `screenshot`,
  `assert_frame_changed_since`,
  `assert_dominant_color_fraction_below`,
  `assert_color_range_fraction_below`, and `note`. A step can also set
  `delay_after`, `hold_ms`, or `capture_after`.

A step contains exactly one action. For example:

```json
[
  {"double_click": [941, 59], "delay_after": 2, "capture_after": true},
  {"key": "Return", "delay_after": 1},
  {"note": "Accepted the game's license dialog with the user's authorization."},
  {"click": [520, 430], "capture_after": true},
  {"hold_click": [595, 418], "hold_ms": 750, "capture_after": true},
  {"wait": 30},
  {"screenshot": "first-live-gameplay"}
]
```

`hold_click` keeps the primary mouse button down for `hold_ms` before it is
released. This is required by games such as Combat Mission that map classic
Mac click-and-hold to an order menu or use graphical controls that must be held
for camera movement. The release is sent even if the wait is interrupted.

Named screenshots can also be used as unattended input and lifecycle oracles:

```json
[
  {"screenshot": "before-input"},
  {"key": "Up", "hold_ms": 1500, "delay_after": 2},
  {
    "assert_frame_changed_since": {
      "screenshot": "before-input",
      "minimum_changed_fraction": 0.05,
      "channel_tolerance": 8,
      "region": [0, 0, 640, 400]
    }
  }
]
```

The referenced screenshot must appear earlier in the same route. The optional
region excludes animated HUD or letterbox areas; the assertion records the
measured changed-pixel fraction and fails when the expected visible transition
does not occur. Ambient animation can also change pixels, so this is a
transition/freeze oracle rather than proof of input causality by itself. Use a
game-specific region and threshold, and retain reviewed before/after captures
for claims that a particular control worked.

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

Use `wait_for_pixel` when a particular UI state has a stable visual marker.
Unlike a generic frame-change wait, it does not mistake intermediate boot
screens or animation for application readiness:

```json
{
  "wait_for_pixel": {
    "x": 22,
    "y": 11,
    "red": 221,
    "green": 0,
    "blue": 0,
    "tolerance": 8,
    "timeout_seconds": 180,
    "poll_interval_seconds": 1
  }
}
```

The example waits for a known red pixel in the Finder Apple-menu icon. The
harness captures periodic evidence while waiting, records the actual matched
RGB value, and fails explicitly on timeout or if the coordinate is outside a
resized framebuffer. Prefer a sequence of semantic gates when one screen has
multiple phases; for example, wait for Disk First Aid to appear, then for its
completion message, dismiss it, and finally wait for Finder to own the menu
bar.

`Super_L` is the Mac Command key in QEMU VNC, so a chord such as
`{"chord":"Super_L+o"}` sends Command-O. Coordinates are tied to the
specified guest resolution and Finder layout. Capture the screen after every
navigation transition while authoring a new recipe. Never silently click a
license dialog: use a `note` immediately before the action and preserve a
screenshot showing what was accepted.

Named keys are case-sensitive and validated before a VM is created. Use names
such as `Space`, `Return`, `Escape`, `Left`, `F1` through `F12`, and
`Super_L`; ordinary printable characters such as `o` are written as a single
character. Function-key coverage is important for games such as Oni that bind
their in-game pause/menu action to `F1`.

Use `{"drag":[start_x,start_y,end_x,end_y]}` to reposition windows or operate
classic sliders. The VNC client holds the primary button while sending paced
motion events, making the action deterministic with ClassicMac's normal
Virtio tablet.

For a scene with a known large-area corruption signature, add a visual reject
oracle after the semantic navigation steps:

```json
{
  "assert_dominant_color_fraction_below": {
    "maximum": 0.10,
    "ignore_colors": []
  }
}
```

The harness captures the asserted frame, counts exact RGB colors, records the
dominant color and whole-frame fraction in `events.jsonl`, and fails the run
when the fraction is at or above the limit. `ignore_colors` is optional and
accepts RGB triplets. This is a regression-specific reject oracle, not a proof
that a scene is visually correct; retain representative positive screenshots
and choose thresholds from reviewed pass/fail evidence.

When compression-free shading or animation spreads the same corrupt surface
over several nearby colors, count an inclusive RGB box instead:

```json
{
  "assert_color_range_fraction_below": {
    "minimum_rgb": [224, 224, 208],
    "maximum_rgb": [255, 255, 255],
    "maximum_fraction": 0.10,
    "region": [100, 120, 320, 180]
  }
}
```

Every pixel whose red, green, and blue channels each fall between the two
triplets, including the endpoints, contributes to the measured fraction. The
harness records the range, fraction, optional `[x, y, width, height]` region,
frame dimensions, outcome, and lossless screenshot in the evidence. Omit
`region` to measure the full frame. Prefer a stable region that excludes HUDs,
skies, and animated characters, and choose its range and threshold from
reviewed good and bad captures.

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

Prepare a new immutable base with the exact guest artifacts extracted from
that signed app before the release-wide replay. The source image is never
mounted, must have all host write bits removed (`chmod a-w`), and the command
refuses to overwrite an existing output:

```sh
scripts/prepare-gxmetal-game-base.sh \
  /path/to/installed-ten-game-golden.img \
  /path/to/os9-ten-game-gxmetal-release.img
```

The tool verifies the signed app, clones the source, replaces GXMetal,
GXMetal Input, and GXMetal Startup only in the clone, places GXMetal Test in a
top-level candidate-tools folder, moves inherited GXMetal conformance apps out
of Startup Items, and clears inherited test/trace files. This prevents an old
PASS/FAIL dialog from consuming game-automation input. While the clone is
already mounted, it reads the exact id-0 `Smrt` General Controls resource. If
that single resource already records the disabled startup-disk check, the
preference is preserved without booting the VM. Otherwise the tool uses
General Controls inside the guest, force-stops and reboots the output, requires
Finder to appear without Disk First Aid, and verifies the resource. It then
prints source, output, Tools CD, and bundled Power Mac QEMU hashes. Set
`GXMETAL_FORCE_DISK_CHECK_VERIFY=1` to force the full UI/reboot verification or
`GXMETAL_SKIP_DISK_CHECK_DISABLE=1` only when preparing a special control image
that must retain the warning.

If installed titles are split across validated bases, compose the missing
application folder before preparing or qualifying the driver. The composition
tool clones the destination base, mounts the donor read-only, preserves Mac
resource forks and Finder metadata with `ditto`, refuses replacement, records
both input hashes and the output hash, verifies both inputs again after detach,
and makes the result read-only. Both inputs must already be host-read-only:

```sh
scripts/compose-gxmetal-game-base.sh \
  /path/to/base.img \
  /path/to/donor.img \
  'FutureCop Preview' \
  /path/to/ten-installed-games.img
```

The optional fifth argument chooses a different destination-relative path.
The adjacent `.composition.txt` sidecar is part of the base provenance. A
folder containing only an installer does not satisfy an installed-game route;
verify the exact application path from a disposable clone before starting a
long batch.

Do not use Finder, `hdiutil`, or Disk Arbitration to inspect a retained evidence
disk that is still host-writable, even with read-only attach and mount flags.
During the UT investigation macOS changed such an image's SHA-256 and mtime
without changing its size; the sweep's independent before/after hash caught
the incident. Lock every golden/input image at the filesystem level, mount
only disposable clones, and quarantine any source immediately if its digest
changes.

The guest-side UI route preserves the rest of General Controls; direct host
resource-fork rewriting is deliberately avoided. The standalone operation is
also available for an explicitly writable disposable image:

```sh
scripts/disable-os9-disk-check.sh /path/to/writable-golden-clone.img
```

Run `scripts/test-gxmetal-os9.sh` against the prepared base before promoting
it to the sweep; that conformance runner also uses a disposable clone. Its VNC
helper still recognizes and dismisses only a completed Disk First Aid message,
so older/control images cannot silently prevent GXMetal Test from running as
a Startup Item.

For the fast per-build driver gate, run GXMetal Test and the accelerated AGL
probe together from independent clones:

```sh
python3 scripts/gxmetal-game-sweep.py BASE.img \
  gxmetal/compatibility/driver-smoke.example.json \
  --modes gxmetal --jobs 2 --keep-disks \
  --output /path/to/evidence/driver-smoke-YYYYMMDD
```

The GUI recipes prove that both applications launched, but their persisted
guest results are the authoritative pass/fail records. Extract those results
and the matching diagnostic snapshots directly from the retained disks:

```sh
scripts/extract-gxmetal-guest-results.sh test \
  /path/to/evidence/driver-smoke-YYYYMMDD/gxmetal-conformance__gxmetal
scripts/extract-gxmetal-guest-results.sh agl \
  /path/to/evidence/driver-smoke-YYYYMMDD/gxmetal-agl-probe__gxmetal
scripts/extract-gxmetal-guest-results.sh input \
  /path/to/evidence/lifecycle-YYYYMMDD/game__gxmetal
```

The `input` mode copies and decodes the fixed-format `GXMetal Input Trace`
from the guest Preferences folder. It records process/CFM identity and
InputSprocket lifecycle, bridge, timer, polling, and push counters separately
from the rendering-driver trace.

For boundaries that cannot be inferred from screenshots, manifests may pair
`key_down`/`key_up` around a bounded `monitor_memory_snapshot`, or use
`monitor_register_snapshot` to dump selected PPC registers and the memory they
reference. Held keys must be balanced when the manifest is loaded. Interrupted
runs release every tracked key and mouse button during cleanup. Fixed memory
reads are limited to 4,096 bytes in the 32-bit virtual address space. These
actions use
QEMU's read-only `x`/`info registers` monitor commands; they do not patch the
guest or weaken its memory protections.

Games that switch into relative mouse mode may warp the guest cursor without
changing the VNC client's remembered absolute position. Before clicking a
Finder dialog after such gameplay, use `{"rehome_pointer": true}`. It sends a
bounded corner-to-corner sequence that resynchronizes QEMU's relative pointer
conversion and leaves the cursor at the upper-left; the next normal click can
then move from a known position. This is deliberately explicit because doing
it during live gameplay would create unwanted camera motion.

The AGL probe rejects software pixel formats, verifies the renderer identity,
draws and clears through Apple's OpenGL stack, and checks all six common
filled primitive modes: triangles, triangle strips, triangle fans, quads,
quad strips, and polygons. Its two disjoint `GL_TRIANGLES` plus a background
guard pixel distinguish a real triangle list from an accidentally connected
strip. The probe also uploads and samples a real RGBA texture, checks source-
alpha blending and near/far depth ordering, validates `glReadPixels` against
every sample, deletes the texture, proves that readback does not alter the
display, and checks teardown. This makes it a much faster OpenGL integration
gate than launching a full game after every low-level driver change. Use
`--keep-disks` whenever the persisted result and driver trace must be
extracted; successful clones are otherwise discarded after their screenshots
and host logs are saved.

For per-commit functional coverage of the four currently proven game routes,
use `gxmetal/compatibility/four-game-smoke.example.json`. It replaces most
fixed delays with accepted Finder, menu, and gameplay pixels and gives each
game its own copy-on-write disk and Unix sockets. Each route closes inherited
Finder windows before navigating, so a base's last interactive window cannot
redirect coordinate-driven input. The base must contain the
installed top-level `FutureCop Preview` folder; media-only bases that contain
just its installer are intentionally rejected by the launch recipe's semantic
gate. Future Cop explicitly clicks the root-window contents before selecting
by initial so an offscreen Finder selection from base preparation cannot
redirect Command-O to the candidate-tools folder. On a sufficiently large
development host, run all four at once:

```sh
python3 scripts/gxmetal-game-sweep.py BASE.img \
  gxmetal/compatibility/four-game-smoke.example.json \
  --modes gxmetal --jobs 4 --ram-mb 512 \
  --output /path/to/evidence/four-game-smoke-YYYYMMDD
```

This is a short correctness regression, not performance evidence. Use two
batches (`--jobs 2`) with at least 120 seconds of gameplay per title for a
release qualification, keep Bugdom's separately proven quit/relaunch recipe,
and use `--jobs 1` for comparable performance measurements.

After a driver change that touches Apple's ATI/OpenGL compatibility path, run
the shorter Cro-Mag Rally and Oni pair concurrently. These recipes skip the
long exploratory soaks but retain the video-mode selection, textured loading,
Oni's direct Start-button activation, Combat Training gameplay/input,
profiles, and screenshots:

```sh
python3 scripts/gxmetal-game-sweep.py BASE.img \
  gxmetal/compatibility/ati-opengl-game-smoke.example.json \
  --modes gxmetal --jobs 2 \
  --output /path/to/evidence/ati-opengl-smoke-YYYYMMDD
```

Use the longer `signed-unresolved-launch.example.json` routes when a short
regression changes visually or stops reaching its named semantic capture.

For Oni's historical post-load corruption signature, one focused route checks
both the +30-second coherent scene and the +45-second transition from the same
launch. This avoids paying for a second boot, intro, and menu traversal while
retaining a lossless screenshot and inclusive bright-range assertion at each
milestone:

```sh
python3 scripts/gxmetal-game-sweep.py BASE.img \
  gxmetal/compatibility/oni-transition-smoke.example.json \
  --modes gxmetal --jobs 1 \
  --output /path/to/evidence/oni-transition-YYYYMMDD
```

To replay only failed or recently changed routes without copying the manifest,
select their stable ids with `--games`:

```sh
python3 scripts/gxmetal-game-sweep.py BASE.img \
  gxmetal/compatibility/four-game-smoke.example.json \
  --games future-cop-smoke,weekend-warrior-smoke --jobs 2 \
  --output /path/to/evidence/smoke-retry-YYYYMMDD
```

Do not reuse an output directory; the harness refuses to overwrite one.
`--keep-disks` retains all modified clones when an installed result is needed
for a follow-up run. `--discard-failed-disks` opts out of the safer default of
retaining a failed clone.

Audio is disabled at the host by default so unattended parallel sweeps remain
silent and preserve the harness's established behavior. To test game audio on
macOS, select the same buffered CoreAudio backend used by ClassicMac's normal
Power Mac sound-on launcher:

```sh
python3 scripts/gxmetal-game-sweep.py BASE.img MANIFEST.json \
  --audio-backend coreaudio \
  --output /path/to/evidence/audio-check-YYYYMMDD
```

`--audio-backend` accepts `none` (the default) or `coreaudio`. The selection is
recorded in the top-level `session.json`, each run's `run.json` and
`result.json`, the `qemu_started` event, and the archived QEMU command. A live
CoreAudio run can mix sound from parallel guests, so use a small `--jobs` value
when listening to and reviewing individual games.

For transport diagnosis, enable one or more built-in QEMU trace events without
rebuilding the host. The option is repeatable, accepts an event name or glob,
and rejects comma-separated trace-output options so every run keeps its output
inside the archived `qemu.log`:

```sh
python3 scripts/gxmetal-game-sweep.py BASE.img MANIFEST.json \
  --audio-backend none \
  --trace-event input_event_key_qcode \
  --trace-event input_event_btn \
  --output /path/to/evidence/input-transport-YYYYMMDD
```

The selected events are recorded in `session.json`, `run.json`, the
`qemu_started` event, and `qemu-command.json`. This distinguishes VNC/QEMU
delivery from guest InputSprocket behavior while retaining the exact signed
QEMU executable under test.

The classic Unreal Tournament port currently stops before its first RAVE
submission when `UseSound=True` under the test VM's audio path. Fullscreen is
not causal: sound-disabled windowed and fullscreen controls render the same
menu. Prepare a disposable, copy-on-write test image without mutating the
corpus base:

```sh
scripts/prepare-unreal-tournament-test-image.sh \
  BASE-WITH-UT.img UT-NOSOUND.img
```

The helper verifies the source hash before and after, requires the expected two
CR-delimited `UseSound` entries, preserves the fullscreen setting, and refuses
to overwrite its output.

## Evidence and review

The top-level `session.json` contains hashes for the manifest, executable,
loader, base disk, Tools CD, and game media, plus the selected audio backend
and trace events, host/Python identity, QEMU version, repository commit/status,
and before/after base-image integrity result. `manifest.json` is the exact
archived input, and `summary.json` contains one automation result per variant.
Each run directory contains:

- `qemu-command.json`, `qemu.log`, and `serial.log`; the runner enables QEMU's
  `guest_errors` log class so a permanent GXMetal queue fault retains its
  exact error, opcode, context, sequence, and leading payload words;
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
python3 scripts/decode-gxmetal-input-trace.py \
  path/to/GXMetal-Input-Trace.bin > input-trace.json
```

The decoder derives field order, signedness, and array extents from the current
`GXMetalDiagnostics.h`. It also recognizes supported append-only historical
snapshots from v1.12 through v1.27 while the current schema is v1.28; other
size mismatches are rejected. The input decoder validates its own fixed
big-endian snapshot size, magic, version, and event ring before emitting JSON.

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

The current evidence-backed status of the installed ten-game corpus is in
`docs/gxmetal-ten-game-compatibility.md`. It distinguishes current-candidate
results from historical incremental candidates and does not treat a runtime
failure before GXMetal context creation as a renderer failure.
