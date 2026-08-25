# GXMetal ten-game compatibility sweep

This campaign qualifies GXMetal against ten additional classic Mac games. It
does not count the existing Nanosaur, Carmageddon II, or Quake III validation
runs. The matrix deliberately spans public QuickDraw 3D/RAVE, direct RAVE,
ATI-specific behavior, classic Apple OpenGL, and games that choose among
multiple renderers.

Compatibility is never inferred from a title screen alone. A result requires
the renderer to be identified, representative gameplay to run, screenshots and
GXMetal profile output to be retained, and a second clean launch to succeed.
Downloaded game binaries and installed disks stay outside Git; this repository
records sources, hashes, recipes, evidence descriptions, and driver fixes.

## Qualification matrix

| ID | Game | Primary path under test | Minimum qualification route | Media and current state |
| --- | --- | --- | --- | --- |
| `bugdom` | Bugdom 1.2.1 | QuickDraw 3D 1.6 / RAVE, ATI behavior | Highest quality; load The Lawn; verify terrain, fog, foliage alpha, and HUD for five minutes; quit and relaunch | Installed; launcher and 3D intro pass on candidate; gameplay qualification pending |
| `cro-mag-rally` | Cro-Mag Rally Demo | Apple OpenGL 1.1.2 | Confirm hardware/OpenGL; complete a lap with terrain, particles, HUD, transparency, and camera transitions | Installed; test in progress |
| `weekend-warrior` | Weekend Warrior | QuickDraw 3D / RAVE | Load the first arena; verify camera clipping, textured characters, UI, depth ordering, and transitions for five minutes | Direct intro/title and five-minute stability pass; title-menu models missing; clean A/B rerun pending |
| `future-cop` | Future Cop: LAPD Demo | Selectable QuickDraw 3D RAVE | Select RAVE; enter Crime War; verify weapon blending, transparent HUD, depth, and explosions | Acquired; in-guest installer pending |
| `dark-vengeance` | Dark Vengeance Demo | Direct RAVE | Reach first combat and scripted sequence; inspect lighting, translucent effects, animated geometry, and camera motion | Blocked before 3D by deterministic game-level error in GXMetal and software controls |
| `myth-ii` | Myth II: Soulblighter 1.5.1 Demo | RAVE | Select RAVE; load a solo map; pan, zoom, rotate, issue orders, and verify terrain, water, units, decals, projectiles, and explosions | Installed; route pending |
| `unreal-tournament` | Unreal Tournament 348m3 Demo | ATI renderer through RAVE | Confirm RAVE; render intro flyby; run a five-minute bot match checking lightmaps, fog, weapon alpha, HUD, and texture cycling | Installed; route pending |
| `combat-mission` | Combat Mission: Beyond Overlord 1.02 Demo | RAVE hardware probe | Complete detection; load Chance Encounter; move through the map and execute a turn with terrain, markers, smoke, and animation | Installed on nine-game candidate; route pending |
| `oni` | Oni Demo | Classic Apple OpenGL | Reach training and first fight; verify animation, lightmaps, transparency, HUD, and an indoor/outdoor transition | Installed with Bink library on nine-game candidate; route pending |
| `havoc` | Havoc Demo | First shipping QuickDraw 3D RAVE game | Select accelerated rendering; enter the demo arena; verify terrain, fog, textured objects, transparency, HUD, and camera motion for five minutes | Acquired, fork-verified, and installed on immutable ten-game base; route pending |

OpenGL titles only count as GXMetal tests when the host log contains GXMetal
presentation traffic. If a game cannot launch or render, run the matched
software variant from a fresh clone to distinguish driver faults from media,
OS, or installer faults.

## Media provenance

| Game | Archive page | Selected file | Archive digest | Local SHA-256 |
| --- | --- | --- | --- | --- |
| Bugdom | [Macintosh Repository 5064](https://www.macintoshrepository.org/5064-bugdom) | `Bugdom_1.2.1.sit`, download id 14992 | SHA-1 `23c6e2cc46d2bcd42e989183017e326f9d6388d9` | `6ebfcb987c66622ec0c70c9fc8e1a5b9103dff7bb58b7de289de61c7a3dadd51` |
| Cro-Mag Rally | [Internet Archive tucows_205724_Cro-Mag_Rally_Demo](https://archive.org/details/tucows_205724_Cro-Mag_Rally_Demo) | `cromagrallydemo.sit` | SHA-1 `80bee13de42e47cd092f730ef7bc0e1aa79ea6d7` | `3ef0b8af0f4825d873a5eb91a0e27e751a3f41ff124ea377215cef4e61a95fa6` |
| Weekend Warrior | [Internet Archive tucows_205672_Weekend_Warrior](https://archive.org/details/tucows_205672_Weekend_Warrior) | `weekendwarrior.sit` | SHA-1 `07522e498736b8b073abade0334f468dc9203f69` | `c475a7f2f2f04a8e03ab789836a60216911041c8f352a179a7980c07501cddd5` |
| Future Cop | [Macintosh Repository 2863](https://www.macintoshrepository.org/2863-future-cop-lapd) | `futurecop_demo.bin`, download id 4702 | SHA-1 `30cbc4034c50ab92179f7baa4d2444580c16fb99` | `f5b8901fce95c432a46d31fd55c8423b8ef41ccb39d2d0426ab86973dc2b8a64` |
| Dark Vengeance | [Internet Archive tucows_205575_Dark_Vengeance](https://archive.org/details/tucows_205575_Dark_Vengeance) | `darkvengeance.sit` | SHA-1 `bdaf88528a32535acd32fbcbfbba72999e646798` | `9f366bce8c0a4286cc5fe87e3f61cbce48ff09a0620360b767fec4e167441f04` |
| Myth II | [Project Magma demo mirror](https://www.moddb.com/games/myth2/downloads/myth-2-151-demo-macosxclassic) | `Myth_II_Demo.zip` | MD5 `baacee4d64a6848580fe1c7883ac1172` | `0eda6b3620891080a2dc04cb88de3d6620c05f2a4f6c1bebcb724bdb97afc020` |
| Unreal Tournament | [Internet Archive tucows_205662_Unreal_Tournament](https://archive.org/details/tucows_205662_Unreal_Tournament) | `unrealtournament.hqx`, 348m3 demo | SHA-1 `76107e9de4bf6c22db3f6338f27edd6312de193a` | `1155ff75573893220fbbc211fa41b45ee92f597650f7f6cc918f5ab473d11117` |
| Combat Mission | [Macintosh Repository 24664](https://www.macintoshrepository.org/24664-combat-mission-beyond-overlord) | `cm-bo-demo102.sit.bin`, download id 24627 | SHA-1 `eab48f4a228bc151eef7629c6b3576cd6930deea` | `56f46fae2993fbce317f33a71411f959525de5c3481fdca7a6cb6f3750b19967` |
| Oni | [Macintosh Repository 3445](https://www.macintoshrepository.org/3445-oni) | `OniDemo.sit`, download id 60155 | SHA-1 `0433e60b5987a144e57ccec154f003c710c04a81` | `295f8b2ba3b84bb601194f73fbc026265a2b287b09761453c7134200faf31739` |
| Havoc | [Macintosh Repository 4456](https://www.macintoshrepository.org/4456-havoc) | `HAVOC_Demo.sit`, download id 59775 | SHA-1 `8e4e2e8d300c9441d03eedb3954f3ba657f6bd28` | `4cf7f7d8d49fa46b54abf7f6977d20bed856cbd236d6d2c95b952f57183956d8` |

The classic Bugdom and Weekend Warrior releases are freeware according to
Pangea's official pages. Pangea also publishes the classic Bugdom serial. The
Cro-Mag classic release is now free, while this sweep uses the preserved demo.
The remaining selected packages are evaluation demos/shareware. EULA screens
are captured before acceptance, and acceptance is noted in the automation
timeline. No game package is redistributed with ClassicMac.

## Evidence contract

For every game retain:

1. Source URL, exact filename, byte count, cryptographic digest, and license or
   EULA action.
2. Finder state before launch, renderer selection, first rendered frame,
   representative gameplay, an effects-heavy scene, clean exit, and second
   launch.
3. The exact QEMU command and hashes for QEMU, NDRV loader, Tools CD, manifest,
   base disk, and attached media.
4. `GXMETAL_PROFILE=1` output and any queue fault or diagnostic state.
5. A filled review record separating launch, menus, gameplay, visual
   correctness, input, audio, and sustained stability.

Performance runs use one VM at a time. Correctness runs may use two concurrent
VMs, each with an independent clone and unique local VNC and monitor sockets.

## Campaign log

### 2026-08-25

- Selected ten games covering nine additional engines and five renderer/API
  patterns. Existing Nanosaur, Carmageddon II, and Quake III results are kept as
  regression anchors rather than counted toward the ten.
- Replaced the planned Diablo II shareware case after its 121 MB Macintosh
  Repository package proved member-gated and no digest-matched public mirror
  was available. The replacement is Havoc, which
  [Apple Directions, July 1996](https://www.savagetaylor.com/wp-content/uploads/documents/Apple_Directions/Apple_Directions_1996/Apple_Directions_07-96.pdf)
  identified as the first game on the market to use QuickDraw 3D RAVE. That
  makes its small public demo a more direct historical test of the baseline
  RAVE contract while avoiding an undocumented account-creation step.
- Added `scripts/gxmetal-game-sweep.py`, an evidence-producing parallel runner
  with isolated APFS/full-copy disks, Unix-socket VNC automation, screenshots,
  profile and serial logs, media/tool hashes, and before/after base-image
  integrity checks. A two-instance real-QEMU smoke run completed with isolated
  sockets and evidence.
- Confirmed `context/issue9-test.img` is an empty sparse placeholder, not a
  usable Mac OS system. Installed a new Mac OS 9.2.1 disk from the local install
  CD and recorded clean-base SHA-256
  `741ce07a67f05ca86a84719d5db2f06dd6c5e26086f503b814779f8903e25020`.
- Found that the signed-bundle conformance harness inherited read-only mode
  from an immutable source image. Updated it to grant write permission only to
  the disposable clone before mounting it.
- Downloaded and verified Bugdom 1.2.1, Cro-Mag Rally, Weekend Warrior, Dark
  Vengeance, Myth II, Unreal Tournament, and Future Cop. Host-side extraction
  of the first six produced directly runnable PowerPC applications; Future Cop
  is a resource-fork-bearing Mac installer and must be run inside Mac OS 9.
- Downloaded and verified the Combat Mission 1.02 demo (30,113,536 bytes). Its
  MacBinary-wrapped StuffIt archive expands to a directly runnable application
  folder; `unar` retained its application resource fork and the resource-only
  Graphics and Sound data files. Installation into the shared candidate disk
  is deferred until the concurrent game runs finish their source-integrity
  checks.
- Downloaded and verified the Oni demo (79,260,591 bytes) against the archive's
  full SHA-1. Host extraction retained its application resource fork and the
  resource forks of CarbonLib, InputSprocket, and the Bink shared library.
  This supplied the self-contained game folder and candidate extension set for
  an isolated Mac OS 9 staging pass.
- Cloned the shared six-game candidate without altering it, then staged Combat
  Mission and Oni on the new nine-game disk. Mac OS 9.2.1 already supplied
  newer CarbonLib 1.4 and the same InputSprocket 1.7.3, so only Oni's missing
  Bink 1.0k shared library was installed. Data and resource-fork byte counts
  were verified after `ditto`; the staged disk SHA-256 is
  `c32273882c6f3582b1699a0e05dddf0848fa702eb86a959760df1169761af81d`.
- Downloaded the 5,367,480-byte Havoc demo after the archive's serialized
  transfer interval and matched its complete published SHA-1. `unar` preserved
  its PEF application's 351,278-byte resource fork and the resource-only game
  data files (including the 795,380-byte `RB Objects` fork). A full-copy
  derivative of the immutable nine-game disk was mounted for this staging only;
  `ditto` preserved the verified forks, the disk was ejected, and the resulting
  ten-game base was locked read-only at SHA-256
  `66ddbb9898aec3128d8e8cbf33ae3774cb39bba64483936b45432245ffaa83aa`.
- A concurrent Cro-Mag probe detected that the older shared source
  `/tmp/gxmetal-bugdom-fix.img` changed at 00:29:24 while the run was active.
  Audit confirmed every recorded QEMU command targeted a distinct retained
  clone, not that source, but the exact external writer was not recoverable
  after the fact. The harness correctly rejected the run on its before/after
  SHA-256 mismatch. All overlapping results are marked tainted, the old source
  is retired, and subsequent runs use the unmounted nine-game image above in
  read-only mode (`0444`) with its hash checked on every run.
- Weekend Warrior reached its Pangea 3D intro and animated textured money-field
  title scene and remained stable for more than five minutes. The host recorded
  49,184 direct frames, zero fallback frames, and no crash or queue fault.
  However, the rotating `MenuInterface.3dmf` selection models never appeared,
  so input could not advance to gameplay. Because this long run overlapped the
  retired source-image change, it is retained as diagnostic evidence rather
  than qualification; a clean GXMetal/software comparison on the immutable
  base is required to isolate a likely public RAVE state/rendering gap.
- Dark Vengeance consistently stopped before creating a RAVE context with
  `FATAL ERROR: Memory allocation failed (size = -49)`. The same result occurred
  at 512 MB and 128 MB, with GXMetal and software RAVE, and after applying the
  vendor-documented `DISABLE_DRAWSPROCKET=TRUE` workaround. The workaround took
  effect but did not change the error, and no rolling GXMetal profile appeared.
  Because classic Mac OS error -49 is `opWrErr` (file already open for writing),
  the observed failure is classified as a media/runtime compatibility problem,
  not a GXMetal rendering failure. Evidence and the completed review record are
  under
  `context/gxmetal-games/evidence/dark-vengeance-live-final-drawsprocket-off-2/`.
- Confirmed `unar` preserved classic Finder metadata and resource forks for the
  extracted applications before copying them to HFS+ with `ditto`.
- Macintosh Repository rejected simultaneous follow-up transfers with HTTP
  418. Retrieval from that archive is therefore serialized at its enforced
  ten-minute interval; Internet Archive mirrors are used where their file
  digests match the preserved package.
- Hardened the dependency-free VNC client for the Open Firmware-to-Mac OS
  resolution change. It now negotiates standard and extended DesktopSize,
  resets pointer state after a resize, requests a coherent full frame, and has
  protocol tests for both resize forms.
- Proved the harness against a signed application build using one persistent
  VNC connection across `640x480 -> 1024x768 -> 640x480`, ten screenshots, a
  zero QEMU exit, and an unchanged base-image SHA-256. Short temporary Unix
  socket directories avoid macOS's socket-path length limit while logs and
  evidence keep their descriptive paths.
- Reverse-engineered Bugdom 1.2.1's launcher checks from its PEF executable.
  It enumerates ATI vendor engines, requires engine generation 5 (or generation
  4 revision 30+), reads private chip tag 1011, and selects its Rage 128 path
  at value `0x0500`. GXMetal now exposes that compatible identity while keeping
  the real product revision in the public revision gestalt. Native and guest
  conformance tests cover the exact conditions.
- The next Bugdom failure initially appeared at `QADrawTriMeshGouraud`, but the
  persisted trace showed the host had already rejected an invalid fog-mode
  value sent through a void RAVE setter. Legacy state input is now validated
  in the guest; invalid fog modes are ignored and counted instead of faulting
  the shared transport. The successful run recorded 1,048 rejected tag-17
  writes, confirming this is a recurring Bugdom/QuickDraw 3D behavior rather
  than a one-time corrupted packet.
- Clearing the copyright screen then exposed a host abort in Metal's
  `setVertexBytes`: real RAVE mesh batches exceed that API's 4 KiB limit.
  Larger Gouraud and textured batches now use command-buffer-retained
  `MTLBuffer`s. A native regression submits both oversized forms. All native,
  Metal, VNC, PEF, and guest-build checks pass.
- Expanded the real Mac OS 9 `GXMetal Test` with 256-triangle indexed Gouraud
  and textured batches plus pixel verification. As a negative control, the
  new guest test reliably aborts the signed v2.1.1 host in Metal's
  `setVertexBytes`. The fixed source host passes the identical immutable-disk
  run, reports the new `large-mesh-batches` capability, and renders the full
  suite 10.03x faster than the software comparison. Compact evidence is under
  `context/gxmetal-games/evidence/source-candidate-large-mesh-conformance/`.
- Bugdom's candidate run completed 164 seconds with 37 screenshots, QEMU exit
  zero, an unchanged source-disk SHA-256, and visible animated 3D intro scenes.
  Rolling profiles reported about 43-145 presentations/s, direct presentation
  only, and roughly 300-490 vertices per draw. Evidence is under
  `context/gxmetal-games/evidence/bugdom-candidate-large-batch/`; a five-minute
  playable Lawn route, clean quit, and second launch are still required before
  marking the game qualified.
