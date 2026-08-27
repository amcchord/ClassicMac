# Changelog

## Unreleased

### Fixed

- Restored valid ATI OpenGL pointer-fan draws whose clip marker is zero. Quake
  III uses that form for ordinary world geometry; treating the marker as an
  enable bit caused missing floors, walls, pedestals, and other polygon-shaped
  gaps in the 2.2.0 betas.

### Changed

- Added a silent, deterministic Q3DM1 regression route with six gameplay
  viewpoints covering the courtyard, ornate and side arches, pedestals,
  passage geometry, portal effects, and HUD. A region-scoped near-black pixel
  oracle is derived from reviewed broken and corrected frames. The game-sweep
  harness can now apply inclusive RGB-range assertions to a stable subregion
  while excluding skies, HUDs, and animated scene content.
- Added reusable silent Cro-Mag Rally gameplay and Myth II transition routes.
  The current ten-game ledger now records Cro-Mag's accelerated race/input
  pass, Combat Mission's independently repeated Setup-to-Orders rendering
  pass, Unreal Tournament's current-driver menu gate, Dark Vengeance 1.2's
  retail-media blocker, and the isolated Myth II software/GXMetal comparison
  that localizes its black gameplay transition after blank texture-pool setup.

## 2.2.0 beta 2 — 2026-08-26

### Fixed

- Restored QEMU's native Cocoa machine window as the default viewer for both
  the Quadra 800 and Power Mac G4. Existing machine packages without an
  explicit viewer preference migrate to the native window automatically.
- Browser/VNC viewing is now an opt-in per-machine setting instead of the only
  display path. The **View in browser (VNC)** toggle appears during machine
  creation and under Viewer in machine settings, and takes effect on the next
  start.
- Start, restart, toolbar activation, screen-preview activation, and browser
  server cleanup now follow the selected viewer. Native machines do not start
  a VNC server; browser machines retain their private loopback-only noVNC URL
  across Power Mac restarts.

### Changed

- Release verification now requires both bundled emulators to contain the
  Cocoa backend, optional VNC/WebSocket support, and ClassicMac's native-window
  Command-key, secondary-click, and scrolling options.

## 2.2.0 beta 1 — 2026-08-26

### Changed

- The parallel game-sweep harness can now select either its silent null audio
  backend or the same buffered CoreAudio backend used by ClassicMac. The exact
  choice is recorded in session, run, result, event, and QEMU-command evidence
  so sound-sensitive startup failures can be tested as reproducible A/B pairs.
- GXMetal game-base preparation now reads the persisted General Controls
  resource while the cloned disk is already mounted. A verified disabled
  startup disk-check preference skips two redundant VM boots; release testing
  can force the full reboot verification with
  `GXMETAL_FORCE_DISK_CHECK_VERIFY=1`.
- Game smokes now close inherited Finder windows before coordinate-driven
  launches. Oni's two post-load corruption milestones share one scripted VM
  session, avoiding a duplicate boot, intro, and menu traversal.
- GXMetal now preserves independent OpenGL MIN, MAG, and mip filtering for
  both texture units. All six legal MIN modes select their exact nearest,
  bilinear, or trilinear Metal sampler behavior, MAG changes no longer erase
  mip state, and legacy RAVE Fast/Mid/Best presets retain their prior mapping.
  Temporary `QADrawBitmap` sampler changes restore the exact asymmetric
  primary-unit state afterward. Native Metal render/readback coverage uses
  distinct mip colors to verify base-only minification, trilinear blending,
  and asymmetric magnification through the real sampler bindings. The Mac OS
  9 AGL Probe now repeats those checks through accelerated Apple OpenGL and
  conditionally validates the advertised ARB unit-1 binding/state path, with
  explicit machine-readable coverage and pixel fields.
- GXMetal protocol 1.24 adds a separately negotiated draw-buffer-writeback
  command. `QAAccessDrawBufferEnd` now uploads only the validated dirty
  rectangle from the shared readback aperture, fences before that staging
  memory can be reused, and preserves the active target outside the rectangle.
- GXMetal Test now clears the draw buffer blue, writes a red 8-by-8 region
  through the public RAVE access API, and verifies the mutation through a
  second access, presentation, and framebuffer readback in both supported
  display pixel layouts.
- Draw-buffer reads now expose the pre-display-gamma render target so an
  unchanged read/modify/write round trip receives the gamma ramp exactly once
  at presentation. The portable renderer also invalidates the precise written
  VGA range so software fallback displays cannot retain stale pixels.

### Fixed

- Unreal Tournament preparation now makes only its copy-on-write clone
  writable while editing and locks the resulting sweep base again afterward.
  Immutable source images remain protected throughout preparation.

## 2.1.4 — 2026-08-26

### Added

- Diagnostic schema 1.0.28 records geometry-callback counts for the current
  private frame, each method's maximum per-frame count and frame number, and
  the first converted triangle to cross a generic 256-callback burst threshold.
  This preserves the exact transition primitive and viewport even when later
  submissions would overwrite the ordinary last-vertex snapshot.

### Changed

- The shortened Oni transition pair, accelerated AGL/conformance pair, and
  four public-RAVE game smokes now run from independent clones of one
  reboot-verified immutable base. rc23 completes Oni's formerly corrupt
  transition plus Bugdom, Future Cop, Combat Mission, and Weekend Warrior;
  its RAVE comparison workload is 10.34 times Apple Software RAVE.

### Fixed

- Corrected Apple's ATI/OpenGL private primitive-family mapping: slots 49/50
  are center-plus-contiguous-rim triangle fans, while slots 51/52 are triangle
  strips. Split-center and direct-flush fan layouts are normalized without
  reading residual PPC registers as callback arguments. Oni's four-vertex fan
  now emits `(v0,v1,v2)` and `(v0,v2,v3)` instead of connecting `v0,v1` to an
  unrelated residual pointer, the direct cause of repeated giant wedges.
- ATI triangle-list, fan, and strip callbacks reject unsupported OpenGL point
  and line polygon modes instead of silently rasterizing them as filled
  triangles.

## 2.1.3 — 2026-08-26

### Added

- Added public RAVE draw-buffer access and a synchronized host readback command
  with guest pixel packing, plus color-channel and depth-write masks.
- Added verified Apple ATI private-driver clear-state, synchronization, clear,
  and contiguous triangle-list entry points used by classic OpenGL.
- Added GXMetal AGL Probe, which requires an accelerated Apple OpenGL pixel
  format and verifies renderer identity, clear, every common filled OpenGL
  primitive (`GL_TRIANGLES`, strips, fans, quads, quad strips, and polygons),
  RGBA texture upload/sampling, source-alpha blending, depth ordering,
  `glReadPixels`, texture deletion, display preservation, and context teardown.
  A disjoint-triangle guard sample rejects the list-as-strip topology error
  that a single center sample would miss.
- Added a two-VM driver smoke manifest, a four-game semantic smoke manifest,
  read-only guest-result/trace extraction, exact pixel waits, held keys, paced
  drags, deterministic Virtio tablet input, dominant-color and inclusive RGB-
  range corruption assertions, parallel jobs, and per-game replay.
- Added signed-candidate base preparation and a General Controls automation
  that persistently disables the improper-shutdown disk check and verifies the
  resulting preference resource after reboot.
- Diagnostic schema 1.0.27 now captures public RAVE entry points, ATI private
  slot calls and arguments, per-callback OpenGL primitive masks and batch
  sizes, readback, transport state, context lifecycle, and bounded vertex/
  pointer snapshots for ATI contiguous/reduced triangles and the redundant
  geometry registers observed at its finish path, plus zero/nonzero clip-
  marker counts, method-48 count histograms, method-50 ABI branches,
  renderer-context resolution, private swap sequence, per-method triangle
  outcomes, and a first-suspicious-geometry record.

### Changed

- GXMetal protocol 1.23 retains the 8 MiB framebuffer-readback aperture, adds
  a bounded exact-rectangle command for disjoint QuickDraw regions and holes,
  and carries public RAVE `kQAPixel_RGB24` textures as tightly packed RGB
  bytes. Deep-Z contexts now use the host depth path instead of falling back,
  ATI's private RGBA byte layout is carried without channel rotation, and an
  explicit alpha-test-false value preserves OpenGL `GL_NEVER`.
- GXMetal Test exercises the expanded RAVE capability contract and persists a
  machine-readable result. AGL Probe and GXMetal Test can run concurrently on
  isolated clones before any slower game regressions.
- App bundling now signs and verifies a staged candidate before atomically
  promoting it. Release verification checks the bundled NDRV and its recorded
  GXMetal protocol-header hash in addition to QEMU and the Tools CD.
- The exact signed candidate completes Bugdom, Future Cop, Combat Mission, and
  Weekend Warrior smoke routes with direct presentation and zero fallback
  frames; Cro-Mag Rally and Oni render through the ATI/OpenGL path, and Unreal
  Tournament reaches its accelerated RAVE main menu with sound disabled in
  either windowed or fullscreen startup. The accelerated AGL core probe and
  in-guest conformance gate pass.

### Fixed

- Power Mac builds now automatically rebuild the committed video NDRV when the
  GXMetal protocol layout changes. This prevents a stale BAR contract from
  silently withholding the guest transport and forcing both RAVE and AGL to
  software.
- Legacy QuickDraw 3D applications that accidentally pass the RAVE linear-fog
  value through the QD3D fog API no longer render as nearly solid white. The
  compatibility path is restricted to the observed perspective-Z,
  normalized-depth, high-density signature; normal exponential-squared fog is
  unchanged.
- Fog now modifies RGB while preserving the fragment's source alpha, avoiding
  unintended changes to later alpha blending.
- ATI/OpenGL private state synchronization now translates the vendor block's
  alpha test, blend factors, depth test and write mask, fog parameters, color
  mask/dither, clear color/depth, and primary/secondary texture wrap, filter,
  and border state. The first private state call forces a complete supported
  snapshot instead of assuming Apple's defaults match GXMetal's. This retains
  the earlier Oni coplanar-menu fix and preserves `GL_NEVER` alpha rejection.
- Apple's ATI/OpenGL acquire/release, clear, and swap callbacks now follow
  their actual division of responsibility: slots 23/24 begin and end host
  work without implicit clears or presentation, slot 28 clears depth, and
  slot 29 presents the optional damage rectangle. Geometry callbacks resolve
  the public RAVE context from the ATI renderer object rather than relying on
  a process-global last context.
- ATI private dispatch slot 59 no longer treats its internal four-float state
  values as geometry. Slot 60 reconstructs a clipped pointer fan only when the
  GLD supplies a nonzero clip marker, then flushes the pending batch; ordinary
  zero-marker finish calls no longer redraw redundant register values.
- ATI multitexture stage discovery treats the vendor renderer's `Current=-1`
  convention as its primary texture binding, restoring Cro-Mag Rally's
  textured title and loading path.
- Apple's ATI/OpenGL filled-geometry callback families now use their exact
  ABIs and topology. Contiguous batches and GLD-reduced pointer triangles,
  triangle strips, triangle fans, quads, quad strips, and polygons are
  decomposed with correct winding. The paired dispatch+0x48 slots preserve
  the GLD's staged-polygon and finish distinction instead of unconditionally
  redrawing the redundant geometry arguments Apple leaves in registers. Each draw callback also
  honors the GLD's actual loaded-primary-texture parity so an untextured draw
  cannot sample a stale binding.
- Exact complex QuickDraw regions are rasterized into vertically merged,
  bounded rect lists and used consistently by Metal draw, clear, and present.

## 2.1.2 — 2026-08-25

### Added

- Expanded the Mac OS 9 GXMetal conformance app with real 256-triangle indexed
  Gouraud and textured batches plus pixel verification. The native Metal suite
  now also covers Perspective-Z above one, ATI source-color blending, and both
  observed ATI private-texture V-coordinate conventions under clamp sampling.
- Added an isolated ten-game compatibility campaign with immutable source
  images, independent writable clones, deterministic VNC interaction and
  screenshots, rolling GXMetal profiles, matched software controls, and source
  integrity checks. The harness now validates display-resize negotiation,
  sanitizes game names before passing them to QEMU, and can synchronize recipes
  on a material framebuffer change with recorded baseline/detected evidence.
- Rolling GXMetal profiles now report per-blend-mode draw distribution and
  translucent/zero-alpha vertex percentages for diagnosing composition faults
  in unfamiliar engines.
- Added a schema-derived Driver Trace decoder that emits persisted guest
  diagnostic snapshots as JSON and rejects layout/size mismatches.

### Changed

- The current ten-game sweep fully qualifies Future Cop on GXMetal, verifies
  accelerated 3D progress in Bugdom, Cro-Mag Rally, Weekend Warrior, and Combat
  Mission, and records reproducible pre-driver or matched-control blockers for
  Dark Vengeance, Myth II, Unreal Tournament, Havoc, and Oni. Combat Mission's
  full Chance Encounter setup rendering and sustained stability pass, while
  its input and turn execution remain under qualification.

### Fixed

- Large Gouraud and textured meshes no longer exceed Metal's inline vertex
  upload limit. Oversized batches use command-buffer-retained buffers while
  smaller draws keep the low-overhead inline path.
- Perspective-Z now maps the complete positive finite reciprocal-W range
  monotonically instead of collapsing values at and above one onto the same
  depth, preserving near/far ordering while keeping reciprocal-distance fog.
- ATI's classic OpenGL/RAVE compatibility path now accepts `SRC_COLOR` and
  `ONE_MINUS_SRC_COLOR` as source blend factors, preventing valid vendor draws
  from faulting the guest command stream.
- ATI private textures now distinguish the observed negative top-origin and
  nonnegative RAVE V-coordinate conventions per primitive. This removes
  clamp-induced texture stripes in Future Cop without regressing the negative-V
  path used by previously qualified software.
- RAVE Alpha1 bitmaps now use their packed one-bit, MSB-first row contract,
  including odd widths, row padding, and dirty subregions. They are expanded to
  host Alpha8 without changing the byte-per-texel layout of Alpha1 textures;
  this restores Combat Mission's complete 3D setup scene.

## 2.1.1 — 2026-08-24

### Changed

- Simplified the browser display with a compact toolbar, quieter status
  presentation, and sharper classic Mac pixels. Enlarged displays use
  whole-number scaling, while downscaled displays keep exact aspect ratios
  wherever the viewport permits.

### Fixed

- Reconnecting browser tabs now hold one stable waiting screen across retry
  attempts instead of alternating status copy and flashing transient noVNC
  display nodes.
- The waiting screen explains that the virtual machine may be shut down or
  restarting, points users back to ClassicMac to start it when needed, and
  reconnects automatically when the display returns.

## 2.1.0 — 2026-08-24

### Added

- Starting a virtual Mac now opens its display in the user's preferred web
  browser instead of creating a separate QEMU window. ClassicMac shows the
  private local URL with Open and Copy actions while the machine is running.
- Added a bundled, branded noVNC 1.7.0 client with Fit, Actual Size, Full
  Screen, sticky Command/Option/Control, and Escape controls.
- Browser input supports ClassicMac's secondary-click and scrolling helpers
  plus QEMU relative pointer capture for GXMetal/InputSprocket games.

### Changed

- Each VM now exposes its framebuffer through isolated HTTP and VNC WebSocket
  listeners bound exclusively to loopback. Browser tabs reconnect
  automatically while a Power Mac restart relaunches QEMU.
- Removable media and display configuration are changed while the machine is
  shut down; the SwiftUI app remains the control center for power, settings,
  live previews, and the browser address.

## 2.0.8 — 2026-08-24

### Fixed

- Corrected Quake III Arena Demo's reversed vertical mouse movement. ClassicMac
  2.0.7 negated Cocoa relative Y before GXMetal Input performed the existing
  QEMU-to-InputSprocket conversion, producing two inversions.
- Cocoa now forwards raw relative mouse Y unchanged, and GXMetal Input remains
  the single coordinate-system boundary. The normal seamless absolute pointer
  used by Mac OS is unchanged.
- QEMU build validation and GXMetal protocol tests now lock both sides of that
  contract: raw host forwarding and positive-down to positive-up guest mapping.

## 2.0.7 — 2026-08-24

### Fixed

- Quake III Arena Demo once again completes Q3DM1 loading and enters live
  play. The 2.0.3 dynamic-resource change retained a second guest-memory copy
  of every texture and bitmap, exhausting Quake's fixed classic-Mac
  application heap and ultimately faulting GXMetal on an unbound draw.
- Direct-color resources now upload without permanent guest shadow copies.
  Writable backing is allocated only when an application actually requests
  dynamic access with `kQANoCopyNeeded`, preserving dirty-region updates
  without making ordinary games pay twice for all texture data.
- Cocoa relative mouse Y is normalized from AppKit's bottom-left coordinate
  convention to QEMU's positive-down convention before GXMetal Input consumes
  it. Physical up/down motion now pitches Quake III in the matching direction.

## 2.0.6 — 2026-08-24

### Fixed

- Restored GXMetal Input's real InputSprocket movement and button elements.
  The coordinator-only 2.0.5 device could move Quake III's menu pointer but
  could not activate menu items, making a normal match impossible to start.
- Captured motion now travels as direct host-relative deltas instead of moving
  and recentering Mac OS's shared cursor. Quake III receives the device layout
  its Mac input loop expects, and vertical movement observes InputSprocket's
  positive-up coordinate contract.
- Host-side button edge latches preserve complete clicks that begin and end
  between guest polls. Short clicks now activate menus and fire reliably while
  held-button state remains coherent across release and reacquisition.
- Deleting a texture now flushes and invalidates every draw-context binding
  that references it before the host resource is destroyed. This prevents an
  ATI private draw from submitting an unbound textured packet and freezing
  Quake III at `AWAITING SNAPSHOT...` while Q3DM1 starts.

## 2.0.5 — 2026-08-24

### Fixed

- InputSprocket games now negotiate captured relative input through a
  coordinator-only GXMetal Input device. Mac OS's system mouse remains the
  sole source of movement and button events, eliminating the competing guest
  cursor streams that caused Quake III's host and guest positions to diverge.
- Relative motion is capability-routed to the ADB mouse while button
  transitions are mirrored coherently to both installed Mac mouse drivers.
  The seamless Virtio tablet remains selected for normal absolute input.
- Games that hide the guest cursor while a GXMetal context is active now enter
  relative capture automatically, including older titles that do not call the
  explicit GXMetal input handoff. Context teardown restores absolute input
  even after an abnormal game exit.
- ClassicMac hides its hardware-cursor layer throughout relative capture and
  restores it with the seamless pointer afterward, preventing a second cursor
  from appearing during play.

## 2.0.4 — 2026-08-24

### Added

- GXMetal now implements and advertises the QuickDraw 3D RAVE chromakey
  contract. Primary texture texels matching the configured 8-bit RGB key are
  rejected before texture operations, fog, blending, and depth writes, with
  native Metal and guest conformance coverage for both matching and
  non-matching colors.

## 2.0.3 — 2026-08-22

### Added

- Expanded GXMetal's public RAVE contract for games that have not been tested
  explicitly: one accelerated multitexture stage, dynamic texture and bitmap
  access, independent mip-level updates, perspective-Z, and positive X/Y
  bitmap scaling with nearest or linear filtering.
- Added deterministic support for Alpha1, RGB8_332, packed CL4, ACL16_88, I8,
  and AI16_88 resources, including odd widths, padded rows, alpha palettes,
  transparent indices, and dirty-rectangle uploads.
- Added an executable compatibility contract. GXMetal Test now checks every
  advertised capability while unsupported complex regions, offscreen/scaled
  contexts, deep Z, CSG, antialiasing, chromakey, channel masks, and Z sorting
  continue to fall back to Apple Software RAVE.

### Changed

- The GXMetal extension, InputSprocket bridge, startup companion, installer,
  and conformance app now share the exact supplied red-accented puzzle-piece
  M artwork at every classic icon depth and size.
- The original 32-pixel GIF is the canonical tracked artwork. The GXMetal
  package build verifies its SHA-256 and compares every generated icon-family
  payload across all five guest components, preventing future visual drift.
- Fresh Power Mac setup now includes an in-app four-step Mac OS installation
  guide and points directly to the GXMetal folder on ClassicMac Tools after
  the first hard-disk start. Default G4/Mac OS 9 starts keep the selected image
  on one native IDE path, eliminating duplicate installer-disc icons while
  preserving Installer's reliable source I/O. G3/Mac OS 8 compatibility starts
  retain their read-only Virtio startup mirror alongside the IDE source.
- GXMetal's installer now reports the exact driver, startup companion, and
  InputSprocket bridge it installed and gives an explicit restart/test path.

### Fixed

- Games can temporarily hand ClassicMac's seamless absolute pointer to
  captured relative input and restore it afterward. Display resizing updates
  the tablet bounds live so host and guest coordinates do not drift.
- Public multitexture now keeps each stage's reciprocal-W and texture
  coordinates independent, and perspective depth no longer changes ordinary
  RAVE Z-comparison semantics.
- Resource replacement, detach/delete, mip updates, palette alpha, Alpha1 bit
  order, packed CL4 nibbles, RGB8_332 channel expansion, and scaled bitmap
  clipping now match the documented RAVE behavior and tested software oracle.
- Tools CD builds reject stale GXMetal component bundles before packaging.
- A clean Mac OS 9.2.1 installation now completes from the selected startup
  image instead of stalling during its file copy. The verified path continued
  through first boot, the automatic Tools mount, GXMetal installation and
  restart, and a successful GXMetal Test run.

## 2.0.2 — 2026-08-19

### Changed

- ClassicMac's Shut Down command now uses the emulated ADB Power key and
  confirms Mac OS's native shutdown dialog. HFS/HFS+ volumes are cleanly
  unmounted, avoiding the recovery work that added roughly 5–6 seconds to the
  next tested Mac OS 9 startup. A 15-second forced-off fallback remains for an
  unresponsive guest.

### Performance

- PowerPC BAT updates now use QEMU's range-aware TLB invalidation instead of
  flushing hundreds of pages one at a time. On a clean controlled Mac OS 9.2
  image this reduced mean time to Finder from 28.82 to 26.78 seconds (-7.1%).
- Added a disposable-disk boot benchmark and documented an optional
  emulator-specific Extension Manager profile. The full profile saved another
  0.67 seconds in the tested universal Mac OS 9.2 install, so it is not applied
  automatically.

## 2.0.1 — 2026-08-19

### Added

- Added `GXMetal Input`, an InputSprocket bridge that exposes ClassicMac's
  seamless Virtio pointer to classic games as a standard relative mouse.
  Quake III's menu cursor now renders, tracks the host pointer one-for-one,
  and accepts clicks without requiring a separate USB mouse device.
- The GXMetal installer and Tools CD now install the RAVE driver, startup
  companion, and InputSprocket bridge as one versioned, rollback-safe set.

### Fixed

- Suppressed clear-only ATI private frames so Quake III's intro no longer
  alternates between the rendered movie and a solid orange frame.
- Implemented ATI private convex-fan draw method 54, restoring transparent
  overlays and other screen-space polygons instead of exposing the orange
  clear color through their missing geometry.
- Relaxed the GXMetal Test fog sample tolerance to account for the measured
  host gamma ramp while continuing to reject incorrect fog colors.

## 2.0.0 — 2026-08-19

### Added

- **Quake III Arena Demo now runs through GXMetal on Mac OS 9.** The RAVE
  compatibility layer implements the ATI private draw paths used by the game,
  including multitextured world geometry and lightmaps, while retaining the
  general QuickDraw 3D RAVE interface used by existing titles.
- Added opt-in host profiling for draw encoding, presentation, primitive mix,
  and ATI texture-coordinate work so future games can be optimized from
  measured bottlenecks instead of game-specific guesses.

### Changed

- The GXMetal guest library is now compiled with `-O2`, and its transport
  batches up to 32 packets or 256 KiB while preserving immediate flushes at
  synchronization boundaries. Quake III retains cross-call triangle batching
  at roughly 176 host draws per heavy frame.
- The Power Mac launcher gives QEMU's translated-code cache 512 MiB for large
  PowerPC games. Metal presentation is coalesced to a trailing 60 Hz console
  refresh without dropping the final completed frame.
- Completed Metal frames are copied into QEMU VGA memory, marked dirty, and
  published through the normal QEMU console. VNC captures, the ClassicMac
  manager preview, and the Cocoa machine window therefore receive the same
  framebuffer rather than separate renderer-specific overlays.

### Fixed

- Corrected Quake III blend modes, texture filtering, scissoring, gamma,
  multitexture coordinates, lightmap composition, and synchronization across
  its menu and in-game rendering paths.
- Moved the legacy ATI ARGB4444 texture-coordinate transform to the host behind
  a negotiated protocol feature, preserving compatibility with older GXMetal
  hosts while reducing work in supported Carmageddon II draws.
- Coalesced guest-to-host command traffic and QEMU display refreshes without
  weakening validation, queue bounds, dirty tracking, or software fallback.

### Performance

- A release-style `-O2` QEMU and GXMetal build sustained approximately 64–65
  FPS in the validated heavy `q3dm1` scene, with the observed long-run low
  remaining above 60 FPS. The same scene rendered correctly through VNC, and
  the production ClassicMac Cocoa path was separately validated through the
  Quake III main menu.

## 1.9.3 — 2026-08-18

### Changed

- GXMetal and its installer, startup companion, and conformance application
  now report version 1.9.3 throughout the bundled Tools volume.

### Fixed

- **Power Mac hard-drive starts no longer pause at a question-mark disk while
  ClassicMac Tools is mounted.** The PowerPC NDRV loader now redirects Mac
  OS's startup path to a Virtio block device only when the user explicitly
  selected a Virtio startup CD. A normal launch keeps the IDE hard drive as
  the startup disk while still mounting ClassicMac Tools through Virtio.
- Corrected combined Mac OS 9 resolution and color-depth switches by
  validating framebuffer pages against the requested display mode rather
  than the previous desktop mode. Games can enter their supported GXMetal
  mode from any supported ClassicMac launcher resolution.
- Corrected ATI-compatible 320x200 bitmap overlay scaling. Carmageddon II's
  minimap now appears at the software renderer's expected lower-right
  position and size while full-screen menu backgrounds remain unchanged.

## 1.9.0 — 2026-08-18

### Added

- **GXMetal Test now reports the installed GXMetal driver version.** The test
  reads version 1.9.0 from the RAVE engine that Mac OS actually loaded, includes
  it in both the PASS dialog and machine-readable result, and rejects a
  mismatched driver with explicit reinstall-and-restart instructions.
- Added a dependency-free headless VNC control and screenshot path for
  repeatable Mac OS 9 game validation without keeping the ClassicMac window in
  the foreground.

### Changed

- **Carmageddon II gameplay now remains above 60 FPS in the validated OS 9
  scenes.** Active-driving samples measured 73–81 FPS; a sustained 20-sample
  window measured 78.54 FPS minimum, 84.35 FPS average, and 100.75 FPS maximum.
- Adjacent compatible RAVE triangles are submitted in order-preserving batches
  that flush at every state, texture, orientation, ATI coordinate-mode, and
  synchronization boundary. Carmageddon II draw traffic fell from roughly
  1,300–2,500 packets per frame to 220–380.
- Metal texture resources now use a collision-safe hash table instead of a
  linear scan, reducing measured lookup work from roughly 170–195 probes to
  one in dense gameplay. Small host vertex arrays use stack storage rather
  than thousands of short-lived heap allocations.

### Fixed

- Added the ATI-compatible RAVE identity, private clear methods, texture
  formats, per-primitive coordinate handling, unbound-texture lighting, and
  dynamic `NoCopy` texture refresh behavior required by Carmageddon II.
- Corrected texture lifetime and resource reuse under long-running games,
  preventing stale textures and resource-table exhaustion.
- Preserved Carmageddon II menus, HUDs, videos, transparent geometry, and
  in-game texture orientation through the accelerated path.

## 1.8.1 — 2026-08-17

### Changed

- **ClassicMac Tools now mounts automatically and reliably on Mac OS 9.**
  Power Macs receive the bundled HFS image as a read-only Virtio block volume
  at cold start, using the proven classicvirtio loader instead of depending on
  an IDE tray media-change notification that Mac OS 9 accepted but never
  surfaced to Finder.
- Existing Power Mac configurations are migrated once to enable the new Tools
  volume, including 1.8.0 machines whose switch remained off after a failed
  live insertion. A later explicit off choice remains respected.
- The Power Mac machine window no longer offers the misleading live Tools CD
  action. Its settings explain that Tools mounts at startup and that installer
  CD boots defer it until the next normal hard-disk start. Quadra SCSI Tools CD
  behavior is unchanged.

### Fixed

- The PowerPC Virtio block NDRV now retries its bounded `diskEvt` notification
  until File Manager has mounted an auxiliary read-only volume. Previously its
  single early event could arrive before Finder was ready, leaving an otherwise
  valid Tools disk invisible.

## 1.8.0 — 2026-08-17

### Added

- **GXMetal brings host-accelerated QuickDraw 3D RAVE to Mac OS 9.** Its
  PowerPC CFM `tnsl` engine batches validated, versioned drawing commands to a
  paravirtual QEMU device, which renders them with Metal. The matching driver,
  one-click installer, and conformance/benchmark application ship together on
  the ClassicMac Tools CD.
- GXMetal now has a simple classic extension puzzle-piece icon with a metal M.
  A small 68K startup companion shows it in Mac OS 9's extension row while the
  RAVE driver retains the `shlb`/`tnsl` identity required for discovery.
- GXMetal accelerates Gouraud and textured geometry, Z buffering, alpha test
  and blending, depth fog, rectangular clipping/scissoring, bitmap uploads,
  double buffering, and the texture formats and sampling modes used by the
  initial Nanosaur validation target. Unsupported contexts remain available to
  Apple's software RAVE engine instead of being partially claimed.

### Changed

- GXMetal presents Metal output directly into page-aligned QEMU VRAM with a
  compute conversion for RGB555, ARGB8888, and RGB8888. A bounded CPU readback
  path remains automatic for embeddings that cannot share the framebuffer.
- A full 640x480 RGB555 present now marks 614,400 bytes of VRAM dirty instead
  of the complete 64 MiB aperture, a 109.23x reduction. Partial presents mark
  only their clipped contiguous span.
- Six runs of the 120-frame Mac OS 9 mixed texture/Gouraud conformance
  workload from exact Developer ID-signed applications measured 8.19x–14.95x
  versus Apple Software RAVE. The stable 1.8.0 candidate completed in 54,144
  microseconds through GXMetal versus 709,314 microseconds through software,
  a 13.10x speedup.

### Fixed

- Corrected the guest-framebuffer stride and visible-mode relationship that
  produced horizontal bands from Happy Mac through early startup after the
  1.7 graphics fast path.
- Preserved RAVE's submitted per-triangle orientation flag as metadata rather
  than incorrectly discarding flagged triangles, which had removed most of
  Nanosaur's submitted geometry.
- Reconstructed RAVE fog distance from `1 / invW` instead of using the
  normalized Z-buffer coordinate. This fixes Nanosaur scenes that were blended
  almost entirely to the pale fog color even though their HUD rendered
  correctly.
- Made GXMetal updates safe when the previous RAVE library is still open. The
  installer stages both files, converts old copies to hidden inert rollback
  files, and the new startup companion removes them on restart, preventing a
  duplicate engine from remaining in the active Extensions folder.

## 1.7.0 beta 3 — 2026-08-16

### Changed

- **QuickDraw graphics are 56% faster in the controlled MacBench 5
  comparison.** The signed-beta Graphics baseline of 9,078 increased to a
  two-run mean of 14,125, with the warm validation run reaching 14,765. The
  exact notarized release bundle scored 14,457, a 59% gain over the baseline.
- Power Mac graphics workloads now avoid unnecessary translated-code page
  walks when framebuffer writes cannot overlap generated code. Ambiguous and
  cross-page writes continue through QEMU's original locked invalidation path.
- The small victim-TLB scan retains its existing order and behavior while
  avoiding a compiler expansion that produced hundreds of outlined helper
  calls on Apple Silicon.

## 1.7.0 beta 2 — 2026-08-16

### Added

- **PowerPC G4 acceleration for Mac OS 8.6 and 9.** New Power Mac machines use
  QEMU's 7400 CPU model by default, while a per-machine compatibility switch
  retains the G3 model required by Mac OS 8.5.
- **Faster Power Mac display modes.** Power Macs can now start in Thousands or
  Millions of colors; Thousands is the recommended high-performance mode.

### Changed

- **QuickDraw graphics are 52% faster in the controlled MacBench 5 comparison.**
  The paired Graphics mean increased from 9,054 on the G3 control to 13,768
  with the beta's G4 and display acceleration enabled.
- Power Mac framebuffers scan out directly to the native macOS display path,
  avoiding repeated TCG dirty-page traps and redundant pixel conversion.
- The classic Mac cursor is composited by the host instead of being redrawn
  into guest video memory on every pointer movement.

## 1.6.1 — 2026-08-15

### Fixed

- **Mac OS 8.5 and 8.6 installation discs now boot on Power Macs instead of
  stopping at a black screen.** The Power Mac profile now matches the
  CUDA-based original iMac hardware those releases support, corrects CUDA
  timer and one-second-message behavior, and supplies the classic NVRAM and
  RTAS firmware interfaces expected by their Mac OS ROMs.
- **Older installation discs can reach their startup volume before Mac OS has
  initialized IDE.** ClassicMac exposes the selected CD through a read-only
  Virtio startup mirror, with fixes for OpenBIOS HFS loading, Apple partition
  maps on CD media, and Mac OS 8.5's PCI capability reads. The same disc
  remains available through the normal CD drive after Finder starts.

## 1.6.0 — 2026-08-05

### Added

- **A guided new-machine setup.** Creating a Mac is now a focused three-step
  flow for the model and file location, hardware and display, then installation
  media and folder sharing. A final summary makes the important choices easy to
  verify before the machine is created.
- **Safer immediate shutdown.** ClassicMac now warns that forcing off a running
  machine can lose unsaved guest work and recommends shutting down from inside
  Mac OS when possible.

### Changed

- **The machine library opens directly to an existing Mac.** ClassicMac selects
  the first available machine at launch and keeps a nearby machine selected
  after one is removed, instead of falling back to first-run messaging.
- **Machine details are easier to scan.** The header now summarizes memory,
  disk size, and display resolution, while the saved-screen preview is more
  compact so display settings remain visible in a standard-size window.
- **Display and configuration validation is consistent.** Names, resolutions,
  color depths, and model-specific limits are normalized in one place for both
  newly created and older machine files.

### Fixed

- Copying an installation disc whose filename matches existing media now
  creates a unique copy instead of silently reusing the older file.
- Turning off custom or enhanced video now keeps the stored resolution and the
  visible preset in sync.
- The new-machine window remains open when disk creation fails so the selected
  settings are not lost.

## 1.5.0 — 2026-07-18

### Added

- **Writable floppy disk images on the Quadra 800.** Attach a raw `.img`,
  `.dsk`, `.ima`, or `.raw` image in a machine's Media settings, or insert one
  while the Mac is running from **Mac → Floppy**. The bundled classicvirtio
  driver mounts the image as a removable disk and writes guest changes back to
  the host file.
- **Guest-coordinated floppy ejection.** Ejecting from the Mac menu asks classic
  Mac OS to flush and unmount the disk before QEMU detaches its image. Ejecting
  the floppy from Finder follows the same path, avoiding stale desktop volumes
  and lost writes.

## 1.4.0 — 2026-07-11

### Added

- **Mac-native menus for running machines.** The Mac menu now provides Pause,
  Resume, Restart, Shut Down, a state-aware ClassicMac Tools command, a Disc
  submenu, and Secondary Click and Scrolling. The View menu focuses on Full
  Screen, title-bar presentation, matching the Mac screen to the window, and
  smooth scaling. Raw QEMU consoles, backend identifiers, developer speed
  controls, and QEMU documentation are no longer exposed.
- **A permanent user-facing disc tray.** Both machine families keep a Disc
  drive available even when it starts empty, so a disc image can be inserted
  or ejected from the Mac menu without restarting the machine.

### Fixed

- **ClassicMac Tools now inserts and ejects reliably while a Mac is running.**
  The Power Mac's Tools drive previously landed beside the hard disk instead
  of on its optical IDE channel, so QEMU accepted a media change but Mac OS 9
  never saw the disc. Power Macs now use dedicated optical positions, while
  Quadras keep separate fixed SCSI positions for the Disc and Tools trays.
- **Power Macs still start correctly from a selected installation disc.**
  OpenBIOS probes only the primary optical position for CD startup. During a
  disc startup, ClassicMac temporarily gives that position to the selected
  disc and leaves Tools empty in the secondary position; normal hard-disk
  startups continue to put Tools first.
- **Disc errors are clearer and safer.** Menu actions target the exact Disc
  tray, keep implementation details in the log, and distinguish a busy drive
  from an image that cannot be opened or read.

## 1.3.0 — 2026-07-10

### Added

- **Borderless machine windows.** Choose View → Hide Title Bar or press
  Control-Option-T to remove the title, traffic-light controls, and titlebar
  separator for a clean guest-only window. The command becomes Show Title Bar
  while active and is also available from the machine's Dock menu.

### Fixed

- **Fullscreen now uses the complete drawable resolution.** The Cocoa display
  previously subtracted the screen's safe-area inset and could request a guest
  mode one titlebar-height shorter than the actual fullscreen content. It now
  reports the final content frame after the transition, opts the machine helper
  out of camera-housing compatibility mode, and leaves fullscreen sizing to
  AppKit.

## 1.2.1 — 2026-07-10

### Fixed

- **The Mac OS 9.2.1 install CD no longer stops at an Apple Audio Extension
  address error.** The failure requires a loaded second Tools CD and Sungem
  networking at the same time; either device alone boots normally. Networked
  Power Mac CD boots now leave the dedicated Tools tray empty at startup while
  keeping the drive available for manual insertion from the Machine menu after
  Finder appears.
- **Turning networking off now removes the emulated NIC.** QEMU creates each
  machine's default network adapter when no `-nic` option is supplied, so the
  old off path silently left networking enabled. ClassicMac now passes
  `-nic none` explicitly.
- **Power Mac tablet input now stays seamless while booting installer CDs.**
  The CD path used to ignore the enabled tablet setting, omit the classicvirtio
  driver loader, and fall back to a relative USB mouse that captured the host
  pointer. The loader installs the tablet NDRV and then resumes Open Firmware's
  selected boot device, so it can safely remain active while `-boot d` starts
  Mac OS 9.2.1 or 9.2.2 from CD.

## 1.2.0 — 2026-07-10

### Fixed

- **Mac OS 9.2.1 and 9.2.2 installations no longer freeze during file
  copy.** QEMU's MacIO IDE model could complete cached DBDMA I/O before
  classic Mac OS armed the synchronous wait for it, losing the wakeup and
  leaving Installer stuck forever around “About 4 minutes remaining.” The
  custom QEMU build now keeps the final DMA descriptor active for 1 ms before
  publishing IDE and DBDMA completion, matching the non-zero latency of real
  hardware. The same patch fixes a long-standing QEMU typo that sent ordinary
  hard-disk DMA reads through the ATAPI completion callback, latches the
  originating IDE unit for asynchronous completion, and adds focused trace
  events plus a headless install regression harness.

## 1.1.1 — 2026-07-09

### Fixed

- **Power Mac G4: tablet input no longer falls back to capturing the
  mouse.** QEMU treats whichever pointing device the guest touched last as
  "the mouse", and the `via=pmu` machine configuration includes a built-in
  USB mouse that steals pointer priority back from the virtio tablet as
  soon as Mac OS starts polling USB — so the window quietly returned to
  capture mode moments into every boot. With tablet input on, the Power
  Mac now runs as `via=pmu-adb` instead: the ADB mouse never re-asserts
  itself, so the tablet keeps priority once its driver loads, and ADB
  remains a working (captured) fallback for guests that can't run the
  classicvirtio driver, such as OS X or CD boots. With tablet input off,
  the machine keeps `via=pmu` and its USB mouse exactly as before.
- **No more "Press ⌃⌥G to release the mouse" window title in tablet
  mode.** With an absolute pointer nothing is captured, so the machine
  window now keeps its plain title while the mouse is inside it.

## 1.1 — 2026-07-05

### Added

- **Tablet input: the mouse moves seamlessly in and out of the machine
  window, no capture needed.** Both machines now present an absolute
  pointing device — classicvirtio's `virtio-tablet-device` on the Quadra
  800 and `virtio-tablet-pci` on the Power Mac G4 — so the host cursor maps
  directly onto the guest screen instead of being grabbed by the window.
  The bundled `declrom` and `ndrvloader` already carry the tablet driver,
  so nothing needs to be installed in the guest. Tablet input is on by
  default; a per-VM "Tablet input" toggle in the Hardware section falls
  back to traditional mouse capture if needed.

## 1.0.5 — 2026-07-04

### Added

- **Experimental macOS 15 (Sequoia) support.** The app no longer requires
  macOS 26 Tahoe: the Swift package targets macOS 15, the two Tahoe-only
  SwiftUI APIs are behind availability checks (the Start button falls back
  from Liquid Glass to `.borderedProminent`, and the toolbar spacer is
  skipped), `LSMinimumSystemVersion` is 15.0 for the app and helper bundles,
  and `build-qemu.sh` compiles QEMU with `MACOSX_DEPLOYMENT_TARGET=15.0`.
  Tahoe is unaffected — it keeps the same glass UI and the deployment target
  only lowers the minimum OS stamp. Support is experimental because the
  bundled Homebrew libraries (glib, pixman, libslirp, ...) are bottles built
  for the host OS; a bundle built on Tahoe is stamped for 15.0 but is only
  guaranteed Sequoia-clean when built on a macOS 15 machine.

## 1.0.4 — 2026-07-04

### Fixed

- **App icon out of "icon jail" on macOS 26 Tahoe.** macOS 26 draws any app
  that ships only a legacy `.icns` shrunken on a synthesized squircle
  backdrop, no matter how the icon artwork is shaped — 1.0.3's full-bleed
  `.icns` alone could not escape that. The app now also ships the icon in
  the Liquid Glass format: an Icon Composer document
  (`Resources/AppIcon.icon`, with the artwork extended to a full square
  layer) that `bundle-qemu.sh` compiles with `actool` into `Assets.car`,
  referenced from `CFBundleIconName`. macOS 26 renders that natively —
  single squircle, edge to edge — while older macOS keeps using the legacy
  `.icns`. Building the Liquid Glass icon needs Xcode 26; without it the
  bundle still builds and just falls back to the legacy icon.

## 1.0.3 — 2026-07-03

### Fixed

- **App icon no longer shrinks inside a grey border.** The checked-in
  `AppIcon.icns` had drifted to a version whose artwork only covered ~87% of
  the canvas; macOS renders undersized icon art on its own synthesized
  backdrop, producing a growing grey border around the icon with each
  regeneration. The original full-bleed master `AppIcon.png` is restored, the
  separate `.icns` copy is gone from the repo, and `bundle-qemu.sh` now
  generates the `.icns` fresh from the master PNG on every build so the two
  can never drift apart again.

## 1.0.2 — 2026-07-03

### Fixed

- **Power Mac drag-to-resize works again once a resolution has been saved in
  the guest.** Live window resizing on the Power Mac G4 stopped working as
  soon as a resolution was ever picked in the Monitors control panel: the
  Display Manager's re-probe (which the video driver triggers through a
  connect-change interrupt when the window is dragged) would revalidate the
  guest's *saved* display preference and stop, never adopting the
  window-sized mode. The bundled `qemu_vga.ndrv` now reports every other
  mode as invalid while a host window resize is pending, so the re-probe
  falls through to the driver's preferred configuration and the switch
  lands. Mode reporting returns to normal the moment the switch completes
  (or after ~10 seconds if no Display Manager is running), so the Monitors
  panel and boot-time resolution restore behave exactly as before. This
  also makes Resend Screen Resolution (Control-Option-R) reliable on the
  Power Mac.

## 1.0.1 — 2026-07-03

Quality-of-life update for the machine window: fullscreen is easy to leave and
the guest resolution can be re-requested on demand.

### Added

- **Fullscreen keyboard shortcut: Control-Option-F.** Toggles fullscreen from
  anywhere — including while the emulator has grabbed the keyboard and mouse,
  which is exactly what happens when a machine enters fullscreen. Previously
  the only shortcut was Command-F, which the guest swallowed in fullscreen,
  making it hard to get back out.
- **Resend Screen Resolution: Control-Option-R.** Pushes the current window
  size to the guest a second time, for the occasional case where the Mac
  misses the resolution-change request that accompanies a window resize (for
  example while the Display Manager is still starting up). Works on both the
  Quadra 800 and the Power Mac G4.
- **Both commands in the View menu** with their keyboard shortcuts shown, so
  they are easy to discover. The fullscreen item now reads "Enter Fullscreen"
  or "Exit Fullscreen" to match the window's current state.
- **Dock icon menu.** Right-click the running machine's Dock icon for
  Enter/Exit Fullscreen and Resend Screen Resolution — an always-reachable
  escape hatch even when the emulator window has captured all input.

### Changed

- The View menu's fullscreen shortcut changed from Command-F to
  Control-Option-F so that one combo works in every situation (Command
  shortcuts are delivered to the guest while input is grabbed).

## 1.0 — 2026-07-03

The first release of ClassicMac — the whole classic Mac OS era on Apple
Silicon.

- Emulates a **Macintosh Quadra 800** (68040) for System 7.1 – Mac OS 8.1 and
  a **Power Mac G4** (`mac99`) for Mac OS 8.5 – 9.2.2, on a custom QEMU 11.0.2
  build.
- Live window resizing on both machines: drag the window and the guest
  switches to that exact resolution.
- Enhanced Quadra framebuffer: any resolution up to 3840x2160, all QuickDraw
  depths including Thousands.
- Host folder sharing on both machines (classicvirtio + virtio-9p), with
  resource forks preserved.
- Working, clean sound: patched Apple Sound Chip (no idle buzz) and screamer
  (AWACS) on the Power Mac.
- Self-contained `.classic` machine documents — double-click to boot.
- Bundled guest-additions **Tools CD** (StuffIt Expander, Disk Copy, USB
  Overdrive, Transmit, ...), insertable at runtime.
