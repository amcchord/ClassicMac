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
| `bugdom` | Bugdom 1.2.1 | QuickDraw 3D 1.6 / RAVE, ATI behavior | Highest quality; load The Lawn; verify terrain, fog, foliage alpha, and HUD for five minutes; quit and relaunch | Post-2.2.1 current-driver smoke: coherent Lawn gameplay and a 0.690459 movement delta; a separate 185-second route cleanly quits from the main menu and relaunches |
| `cro-mag-rally` | Cro-Mag Rally Demo | Apple OpenGL 1.1.2 | Confirm hardware/OpenGL; complete a lap with terrain, particles, HUD, transparency, and camera transitions | Post-2.2.1 640x480 Practice/Desert race pass: coherent terrain/vehicle/HUD, steering deltas 0.857201 and 0.780189, 35-second soak, and clean demo exit |
| `weekend-warrior` | Weekend Warrior | QuickDraw 3D / RAVE | Load the first arena; verify camera clipping, textured characters, UI, depth ordering, and transitions for five minutes | Post-2.2.1 current-driver smoke: coherent Center Stage character, arena, UI, and a 0.819740 action delta; a separate title-state recovery returns to Finder and reaches a second launch |
| `future-cop` | Future Cop: LAPD Demo | Selectable QuickDraw 3D RAVE | Select RAVE; enter Crime War; verify weapon blending, transparent HUD, depth, and explosions | Post-2.2.1 current-driver smoke: coherent Crime War mech, vehicles, buildings, radar/HUD, and a 0.672201 movement/fire delta; a separate gameplay recovery reaches Finder and a second rendered launch |
| `dark-vengeance` | Dark Vengeance Demo | Direct RAVE | Reach first combat and scripted sequence; inspect lighting, translucent effects, animated geometry, and camera motion | Post-2.2.1 gamma/notice candidate passes an in-game semantic pixel, renders coherent textured player/enemy/room geometry, changes 0.418249 after `w`, and survives a 15-second animated soak. The mostly-black selection/loading transition is not claimed as a drawn menu |
| `myth-ii` | Myth II: Soulblighter 1.5.1 Demo | RAVE | Select RAVE; load a solo map; pan, zoom, rotate, issue orders, and verify terrain, water, units, decals, projectiles, and explosions | Post-2.2.1 60-step route reaches a coherent Into the Breach battlefield, selects/orders units, exercises stop and ten paired camera directions/zoom/orbit actions, and completes a 30-second gameplay soak |
| `unreal-tournament` | Unreal Tournament 348m3 Demo | ATI renderer through RAVE | Confirm RAVE; render intro flyby; run a five-minute bot match checking lightmaps, fog, weapon alpha, HUD, and texture cycling | Post-2.2.1 exact-base process one reaches coherent Tempest and cleanly exits; process two renders a different map and accepts held Control. The exact read-only `CheckButtonKeys` probe observes Control in both `LMKeyMap` and UT's stack-local modifier field, hits UT's transition-emission instruction, and the reviewed final frame shows weapon fire, lower ammunition, and shell casings. A five-minute bot match remains the longer release gate |
| `combat-mission` | Combat Mission: Beyond Overlord 1.02 Demo | RAVE hardware probe | Complete detection; load Chance Encounter; move through the map and execute a turn with terrain, markers, smoke, and animation | Post-2.2.1 full-turn route selects Rifle 45 Sqd, plots Move/Disembark, executes GO, and reaches DONE after visible movement. A separate Force Quit recovery returns to Finder and reaches a second coherent scenario selector |
| `oni` | Oni Demo | Classic Apple OpenGL | Reach training and first fight; verify animation, lightmaps, transparency, HUD, and an indoor/outdoor transition | Post-2.2.1 full lifecycle passes warehouse gameplay/input/F1, clean Quit/Yes, exact `-nosound` second launch, renderer upload reset, active Bink frame change, Escape, and a stable four-pixel second-menu signature |
| `havoc` | Havoc Demo | First shipping QuickDraw 3D RAVE game | Select accelerated rendering; enter the demo arena; verify terrain, fog, textured objects, transparency, HUD, and camera motion for five minutes | Post-2.2.1 gamma candidate passes first-run help, main-screen, and cockpit semantic gates; steering changes 0.171768 of the frame and the coherent terrain/HUD soak contains only 0.000221 of the rejected solid-red range |

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

Alternate-media controls also retain Dark Vengeance Demo 1.2 from
`MacintoshSharewareGamesD` (SHA-256
`44ccadc23f344f4e0b388598f516d6f32f495906069aa6ccae6e97b8da5eccf1`)
and the Tucows HAVOC demo (SHA-256
`27957a5ea672ecdb746dfcd499376812a407d4881f8f75f37e81a5e3e9d4fa21`).
The latter differs in application and resource data from the original tested
package but still fails before RAVE.

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
- The clean Weekend Warrior A/B then reproduced that gap deterministically on
  the immutable base. At the same title-menu checkpoint software RAVE renders
  the rotating runner, floppy disk, and other foreground models and responds to
  Left/Right/Space, while GXMetal displays only the animated money background
  and later advances to the Bugdom promo. A read-only focused trace recorded
  11/11 successful texture creates, 51,210/51,210 valid binds, 111,972 draws,
  active indexed textured meshes, and zero rejects, fallbacks, or faults. The
  cause was GXMetal's PerspectiveZ conversion: clamping
  `1 - invW` aliases every vertex with `invW >= 1` at depth zero, where strict
  LT testing rejects later foreground surfaces. Evidence is under
  `context/gxmetal-games/evidence/weekend-warrior-ab-20260825/` and
  `context/gxmetal-games/evidence/weekend-warrior-title-trace-20260825/`.
- The focused PerspectiveZ fix maps every finite positive reciprocal-W value
  monotonically with `1 / (1 + invW)`, preserving LT's nearer/larger-invW
  ordering without changing fog's `1 / invW` semantics. A native textured
  competitor at `invW = 2` and `invW = 4` reproduces the old rejection and
  passes with the fix; the real guest test exercises the same values through
  RAVE. A source-built QEMU run from the immutable nine-game base restored the
  complete title emblem at the same 30-second A/B checkpoint, accepted menu
  input through the runner, character, face, and single-player selections, and
  rendered the complete 3D center stage. The 819.501-second soak retained 72
  screenshots and 72 profiles totaling 38,386 direct frames, zero fallback
  frames, no error/fault/assert/unsupported log lines, QEMU exit zero, and an
  unchanged base SHA-256. Auxiliary VNC input stopped affecting the game after
  an action beside a shell, although rendering and presentation continued, so
  first-arena gameplay, in-guest quit, and relaunch remain unqualified. Evidence
  is under
  `context/gxmetal-games/evidence/weekend-warrior-perspectivez-fixed-probe-20260825/`
  and
  `context/gxmetal-games/evidence/weekend-warrior-perspectivez-fixed-longrun-20260825/`.
- Dark Vengeance consistently stopped before creating a RAVE context with
  `FATAL ERROR: Memory allocation failed (size = -49)`. The same result occurred
  at 512 MB and 128 MB, with GXMetal and software RAVE, and after applying the
  vendor-documented `DISABLE_DRAWSPROCKET=TRUE` workaround. The workaround took
  effect but did not change the error, and no rolling GXMetal profile appeared.
  Later static PEF analysis proves that `-49` is the literal allocation input
  printed by `RBNewPtr`, not a returned OS error. The earliest concrete callers
  allocate `cached_file_size + 1` for three preference INI files, which makes a
  bad file-position output or corruption the leading pre-RAVE hypothesis.
  Evidence and the completed review record are under
  `context/gxmetal-games/evidence/dark-vengeance-live-final-drawsprocket-off-2/`.
- Cro-Mag Rally's clean Option-launch retained its default 800x600, 16-bit
  selection and reached the Pangea logo, title, and animated main menu through
  classic Apple OpenGL. GXMetal recorded thousands of direct frames, zero
  fallback, and roughly 99 draws per frame in the menu. This is the first sweep
  evidence that the OpenGL-to-RAVE path works beyond the earlier direct/public
  RAVE cases. Gameplay and a complete lap remain pending; evidence is under
  `context/gxmetal-games/evidence/cromag-force-640-20260825/` (the retained
  directory name predates confirmation that the game stayed at 800x600).
- A second Cro-Mag run committed 640x480 through a recorded popup drag and
  reached a correctly rendered Pangea splash, but did not advance during 225
  seconds or after Space. GXMetal remained live at roughly 1,040-1,080 fps,
  75 draws per frame, all direct, with 136/136 successful texture creates and
  no context, bind, bitmap, packet, or transport errors. The earlier 800x600
  `Not enough VRAM` dialog is therefore a game-side OpenGL preflight rather
  than a GXMetal allocation failure: that trace had a valid 800x600 context,
  317/317 successful texture creates, and the advertised 64 MB framebuffer.
  The matched 640x480 software-RAVE control is worse: 86/88 captures are the
  same black frame, including every launch-to-final checkpoint, and click,
  Return, and Escape do not advance it. GXMetal therefore creates and draws
  more of the startup path than software at 640x480, but neither result
  qualifies gameplay and the control does not isolate a GXMetal-only stall.
  Evidence is under
  `context/gxmetal-games/evidence/cromag-manual-drag-640-gameplay-20260825/`
  and `context/gxmetal-games/evidence/cromag-640-software-ab-20260825/`.
- Future Cop installed without an EULA prompt and exposes `ATG2 (software
  based)` and `GXMetal` as its two 3D engines. The ATG2 control rendered live,
  correctly textured Crime War gameplay for more than two minutes and visibly
  responded to movement and combat input. GXMetal originally stopped
  presenting at the 3D transition after the ATI bridge requested the vendor
  blend pair `GL_SRC_COLOR/GL_ONE`; implementing the two ATI source-color
  factors restored live presentation. A controlled trace then established two
  real ATI-private texture-coordinate conventions: Carmageddon II supplies
  negative top-origin V, while Future Cop supplies nonnegative RAVE V. The
  production path now classifies the complete primitive before applying the
  legacy V translation, so mixed vertex order cannot produce a partial
  transform. Clamp-sensitive native gradients cover both conventions. The
  exact production rerun on QEMU SHA-256
  `363ce5db9e4f778df633c8defed946f4a81753a1abf768a7ed2f8a67308302b5`
  produced 2,610 direct frames with zero fallback, averaged 29.39 fps, showed
  coherent world, road, vehicle, HUD, and effect textures against ATG2, and
  visibly responded to movement and combat input. No relevant warning, error,
  fatal, or assertion appeared; QEMU exited zero and the immutable source hash
  remained unchanged. Audio was not exercised. Evidence is under
  `context/gxmetal-games/evidence/future-cop-software-gameplay-probe/` and
  `context/gxmetal-games/evidence/future-cop-gxmetal-input-adaptive-final/`.
- Unreal Tournament 348m3 presented no EULA prompt. Its first-run detector
  offered RAVE and selected `RAVE (Rage 128) Renderer`, proving that the
  expected accelerated device is visible. After acknowledging the renderer
  notice, the demo completed its `Loading objects...` progress screen and
  changed to a 640x480 black framebuffer. Captures from 226.38 through 416.43
  seconds are byte-identical, Escape and Command-Option-Escape do not alter the
  display, and the host log contains only initial gamma programming—no GXMetal
  context, profile, fallback, unsupported command, or error. A clean control
  explicitly selecting `Software Rendering` reaches the same black PNG hash
  and remains there from 172.63 through 437.66 seconds. Both QEMU runs exit
  zero and preserve their source hashes. This is retained as a demo/runtime
  startup blocker before the graphics driver, not a GXMetal rendering failure.
  Evidence and the exact hash comparison are under
  `context/gxmetal-games/evidence/unreal-tournament-rave-gameplay-20260825/`
  and
  `context/gxmetal-games/evidence/unreal-tournament-software-control-20260825/`.
- Oni presented no EULA and played its live Bink intro twice, but never reached
  the menu, training, or first fight. In GXMetal mode it reported `Unable to
  initialize hardware accelerated OpenGL on your system with the current
  settings; Oni will now exit.` The retained clone's `startup.txt` reaches
  `creating new OpenGL context`, reports both the original and requested
  display as `640x480x32 : SUCCESS`, enters `OpenGL platform initialization`,
  and stops there. The host log contains only initial gamma programming: no
  GXMetal context, rolling profile, fallback, or command error. Clicking OK
  returned cleanly to Finder and a relaunch reproduced the live intro and same
  fatal dialog. Sixty-one screenshots from 228.01 through 528.05 seconds are
  byte-identical at SHA-256
  `5aef23120e5c07e9e32140d4a097df52de28fab82a738e1816c0519727aca501`.
  With GXMetal disabled, Oni first offered to continue with lower performance,
  but accepting that path produced the same fatal dialog and identical
  `startup.txt` stopping point. This blocks classic Apple OpenGL/AGL context
  integration before GXMetal's in-context rendering path, so gameplay remains
  unqualified. The production QEMU SHA-256 was
  `363ce5db9e4f778df633c8defed946f4a81753a1abf768a7ed2f8a67308302b5`;
  both runs preserved the immutable source hash. Evidence is under
  `context/gxmetal-games/evidence/oni-gxmetal-fatal-relaunch-20260825/` and
  `context/gxmetal-games/evidence/oni-software-continue-20260825/`.
- Myth II reached its correctly rendered main menu and new-game dialog, then
  accepted the demo mission and difficulty. It changed to a larger black
  display that remained byte-identical for the final minute; the host log only
  contains initial gamma programming and never records a GXMetal context or
  rolling profile. This route therefore does not count as an accelerated game
  result and remains a game/runtime or route investigation. Evidence is under
  `context/gxmetal-games/evidence/mythii-gameplay-20260825/`.
- Havoc stops before creating a RAVE context with `Ran out of memory allocating
  resources`. The published-hash demo and its HFS+ resource forks are intact,
  but the error repeats with 512 MB and 128 MB guest RAM, GXMetal and software
  graphics, the documented 256-color startup mode, and a period-correct test
  that raises its `SIZE` resource from about 5.5 MB to 16 MB. The latter was
  isolated on a locked derivative at SHA-256
  `4642aa51eabd221acfe5e07d9cf22686da0e0c2070518028e017435cb06135c2`.
  No GXMetal rolling profile or rendered frame was reached, so this is retained
  as a demo/runtime incompatibility rather than a driver failure. Evidence is
  under `context/gxmetal-games/evidence/havoc-*20260825/`.
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
- Hardened the sweep runner's QEMU display-name serialization after a matrix
  label containing commas was parsed as invalid `-name` sub-options. Commas
  and whitespace are now normalized before launch, with a unit regression and
  a successful real-QEMU folder probe.
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
- A longer Bugdom trace reached its Pangea logo, title, and expected main-menu
  interval through four sequential renderer contexts. GXMetal accepted 221,158
  draws with no fallback or invalid resource, but the menu was mostly black
  with blue fragments and presentation stopped after the game's 20-second menu
  timeout. A disabled-GXMetal control changed to 640x480 but never exposed a
  useful software-rendered frame, so the visual defect remains open rather than
  attributed to either side. A follow-up fixed-delay recipe launched only as
  far as the mode change before its menu input landed, demonstrating that
  variable guest latency can invalidate otherwise correct coordinates.
  Evidence is under
  `context/gxmetal-games/evidence/bugdom-lawn-qualification-20260825/`,
  `context/gxmetal-games/evidence/bugdom-software-control-20260825/`, and
  `context/gxmetal-games/evidence/bugdom-menu-profiler-20260825/`.
- The sweep harness now provides `wait_for_frame_change`, which records a
  baseline, polls with configurable RGB noise and changed-pixel thresholds,
  captures the detected transition, and fails explicitly on timeout. This
  lets new game recipes synchronize on visible guest progress instead of
  silently sending input after an assumed fixed launch delay.
- Combat Mission's hardware path originally stalled at `Loading 3-D Graphics`
  because three of four `kQAPixel_Alpha1` bitmaps were rejected. RAVE Alpha1
  bitmaps are packed one-bit, MSB-first rows, unlike Alpha1 textures, which are
  byte-addressed. GXMetal now validates padded packed rows and expands full and
  dirty subregions to deterministic 0/255 host Alpha8. Native and guest tests
  cover odd widths, byte crossings, row padding, and the distinct texture
  layout. The production rerun rendered the complete Chance Encounter setup
  battlefield immediately and held it visually stable for 246.6 seconds.
  Thirty-three profiles reported 49.75-53.64 fps (51.48 mean), roughly 1,553
  draws/frame, direct-only presentation, and zero fallback. The trace records
  459/459 Alpha1 bitmap successes, 6,065,523 draws, balanced 3,392/3,392 frame
  boundaries, and no rejects, invalid resources, aborts, or errors. VNC input
  produced no visible camera or command change, so rendering/stability is
  verified while input, turn execution, clean quit, and relaunch remain open.
  Evidence is under
  `context/gxmetal-games/evidence/combat-mission-alpha1-fixed-production-20260825/`.

### 2026-08-26 and 2026-08-27

- Localized the Quake III regression introduced in the 2.2 betas to valid
  ATI-private slot-60 pointer fans whose fourth argument is zero. Restoring
  those draws removes the missing world surfaces. The retained silent Q3DM1
  regression now captures six viewpoints, including both ornate and small
  arches, the courtyard, passage lighting, pedestals, portal effects, and HUD.
- Promoted Cro-Mag Rally from a menu/rendering result to a current-driver
  gameplay smoke pass. The scripted 640x480 Practice/Desert route visibly
  accelerates and steers in both directions, sustains roughly 24-28 fps at
  about 204 direct draws/frame with zero fallback, and reaches the expected
  demo exit screen.
- Replayed Combat Mission from two fresh clones. Both reach Chance Encounter,
  visibly advance Setup to Orders, and sustain coherent 3D for 207-265
  seconds. The combined traces contain 16.7 million draws with zero host
  fallback or guest rejects. Camera, unit-order execution, clean quit, and
  relaunch remain open because strengthened post-transition input frames were
  byte-identical.
- A later fixed-camera exact-beta-3 route completes the semantic proof that the
  ordered unit moves: Rifle 45 Sqd is mounted before GO, disembarks by +40,
  moves farther into open ground by +65, and reaches its plotted endpoint with
  DONE by +100. The +100 and +145 frames are byte-identical. Its trace records
  10,357,060 draws, balanced 5,312/5,312 render calls, and zero fallback,
  resource rejects, or render aborts. Command-Q did not visibly exit, so
  relaunch remains an app-control gap rather than a driver pass.
- Replayed Unreal Tournament on the final homogeneous-clipping host from an
  immutable configured base. With audio disabled it advances through the
  Tempest ready state into live Practice gameplay. Scripted forward, left,
  right, and fire input produces distinct coherent world, combat, damage,
  death, and respawn frames. Forty profiles contain 9,472 direct frames, zero
  fallback, 80.36-131.59 fps, and no queue fault, reject, or texture error.
  The required host fix accepts finite ATI-private reciprocal-W and eye-space Z
  outside the normalized clip volume and lets Metal clip reconstructed signed
  homogeneous coordinates; public RAVE remains strict and zero/nonfinite W is
  still rejected. Evidence is under
  `context/gxmetal-games/evidence/gxmetal-ut-beta3-homogeneous-clip-live-gameplay-20260827/`.
- A later protocol-1.25 discriminator corrects two UT lifecycle
  interpretations. The viewport remains live through movement and firing until
  the visible `You are dead. Hit [Fire] to respawn` state; only that death-wait
  frame then becomes byte-identical. Its 61 profiles contain 14,591/14,591
  direct frames with zero fallback, rejects, out-of-range draws, queue faults,
  or Metal errors. Post-death fire/refocus remains an input-capture gap, not
  evidence of a GXMetal freeze. For clean exit, the earlier Game-menu route
  used Down five, which selects `Return to Current Game`; deterministic `Quit`
  is Down six. A corrected quit/relaunch replay remains required. Evidence is
  under `context/gxmetal-games/evidence/gxmetal-ut-protocol125-freeze-discriminator-20260827/`.
- Replayed Bugdom, Future Cop, Combat Mission, and Weekend Warrior using the
  exact published 2.2.1 QEMU, loader, Tools CD, and immutable installed guest
  base. Human review passes all four within their short-smoke scope. The 110
  profiles total 33,382 direct frames, zero fallback frames, and 8,709,954
  draws. Bugdom, Future Cop, and Weekend Warrior pass semantic input gates;
  Combat Mission's retained recipe reaches coherent Chance Encounter setup but
  sends no battle input. Evidence is under
  `context/gxmetal-games/evidence/gxmetal-2.2.1-four-game-smoke-20260827/`.
- A fork-level audit disproved the older configured UT image as an exact stable
  guest, so a new immutable image was prepared from the published 2.2.1 Tools
  CD (`da6d81f5…451ad`). On that exact stack, the first process reaches live
  Tempest and Command-Q returns to Finder. A same-boot relaunch restarts texture
  upload IDs and reaches a coherent second live HUD/weapon. The focused second
  process does not visibly react to Up or Control, isolating the remaining gap
  to InputSprocket/gameplay input rather than renderer generation or quit-menu
  automation. Evidence is under
  `context/gxmetal-games/evidence/gxmetal-ut-2.2.1-cleanquit-relaunch-corrected-20260827/`.
- An input-CFM reference-release candidate preserves Oni's first-process
  warehouse look, movement, action, F1/resume, Quit/Yes, and Finder-return
  gates. It is not sufficient: process two stops changing before the focus
  click, and first-upload IDs remain one uninterrupted 1…213 sequence. The
  next discriminator must cover the full process/InputSprocket startup boundary
  as well as input bridge lifetime. Evidence is under
  `context/gxmetal-games/evidence/gxmetal-oni-input-lifecycle-candidate-20260827/`.
- A combined lifecycle candidate tracks InputSprocket device ownership by
  Process Serial Number, clears stale active/timer/CFM state, re-enumerates
  after a confirmed dead owner, and safely retires System-heap renderer context
  records only after their owning process is `procNotFound`. Native lifecycle
  tests and the PowerPC build pass, but two parallel, audio-disabled VM gates
  remain negative. Oni's first process again passes warehouse input/F1/clean
  quit, while process two becomes byte-identical at Bungie before focus or
  GXMetal re-entry (66 direct profiles, zero fallback, upload IDs 1…210). UT
  cleanly relaunches and resets uploads 244→1, but its five second-process
  live/action/final captures remain byte-identical after Up and Control. The UT
  keyboard failure cannot be attributed only to GXMetalInput's mouse elements;
  the next candidate requires direct InputSprocket system-device lifecycle
  instrumentation. Evidence is under
  `context/gxmetal-games/evidence/gxmetal-oni-process-owner-v2-candidate-20260827/`
  and
  `context/gxmetal-games/evidence/gxmetal-ut-process-owner-v2-candidate-20260827/`.
- A tickle-only diagnostic candidate decisively tests the stale self-rearming
  timer hypothesis without adding per-tickle disk I/O. Oni reaches coherent
  first-process warehouse gameplay, but InputSprocket invokes neither tickle
  callback and relative look fails at 0.008250 changed fraction; the timer is
  necessary for normal mouse delivery and cannot simply be removed. UT still
  reaches coherent first and second Tempest generations. Its persisted trace
  shows the first process calls Stop and Dispose, then the second process uses
  a different PSN/CFM connection and creates a fresh custom device plus five
  elements. Signed QEMU independently records every second-process Control and
  Up transition, while four reviewed live/action frames remain byte-identical.
  This rules out the stale custom timer and VNC/QEMU key transport; the next UT
  discriminator is the built-in keyboard/UT reacquisition layer. Evidence is
  under
  `context/gxmetal-games/evidence/gxmetal-oni-input-tickle-diagnostic-20260827/`
  and
  `context/gxmetal-games/evidence/gxmetal-ut-input-tickle-diagnostic-20260827/`.
- A fixed-address read-only discriminator closes the next UT boundary. In both
  first and second processes, the 16-byte low-memory `LMKeyMap` at `0x0174` is
  zero at rest and byte 7 becomes `0x08` while Control is held; QEMU qcode and
  ADB traffic are identically `0x36`/`0xb6`. Mac OS `GetKeys` backing state is
  therefore healthy in process two. A reproducible GDB helper now relocates the
  exact immutable UT PEF (`51cc73dc…ae28d`) and targets current Control at
  `r1+57`, prior Control at `r2-11987`, and the transition call at code offset
  `0x14fbf0`; no valid live breakpoint sample was captured yet. During PEF
  extraction, macOS changed a writable retained run image despite read-only
  attach/mount flags. The harness caught its digest change, the disk was
  quarantined, and the unchanged upstream preparation base remains available.
  Future extraction must use a host-read-only disposable clone. Evidence is
  under
  `context/gxmetal-games/evidence/gxmetal-ut-first-second-keymap-20260827/`
  and
  `context/gxmetal-games/evidence/gxmetal-ut-second-process-keyboard-discriminator-20260827/`.
- Replayed Future Cop on exact beta-3 through a longer semantic lifecycle.
  Forward and turn controls visibly reposition the mech/camera, fire produces
  impact sparks while ammunition falls from 7500 to 7481 and then 7463, and
  the scene remains coherent for about 87 seconds of live play plus a
  65-second post-input soak. The trace records 8,772,136 draws, 72/72 private
  texture creations, 3,735 direct frames, and zero resource/draw rejects or
  fallback. The promotional exit page ignored Command-Q, so relaunch remains
  an application-control gap.
- Exhausted the available Dark Vengeance retail-1.2-updater route. The updater
  installs in a disposable clone, but GXMetal, Apple Software, a reconstructed
  read-only data disc, and explicit SOFT8 controls all stop at the same retail
  game-CD validation before creating RAVE. The separate 1.2 demo or a
  legitimate retail Mac CD/install is still required.
- Retrieved and fork-preserved a genuine, separate Dark Vengeance Demo 1.2
  installer plus an independently archived Tucows HAVOC demo. The HAVOC build
  differs from the first package but reproduces the same resource-allocation
  dialog on exact beta-3 before any RAVE context/profile traffic, strengthening
  its classification as a pre-driver application/runtime blocker.
- The unchanged Dark Vengeance 1.2 VISE installer accepted the preauthorized
  EULA and completed its default install. The installed demo then reproducibly
  displayed `FATAL ERROR: Memory allocation failed (size = -49)`. Its decoded
  trace has NDRV discovery but zero game contexts, draws, textures, flushes, or
  ATI-private calls. Static inspection of the failure wrapper and startup
  callers proves that the displayed `-49` is allocation bits `0xffffffcf`, most
  likely produced when `ReadPrefs` adds one to a cached size of `-50` for
  `DARKVENG.INI`, `CUSTOM.INI`, or `NETLAUNCH.INI`. The next bounded experiment
  is a conditional `RBNewPtr` return-PC/cached-size capture during one silent
  launch—not a GXMetal workaround.
- A controlled HAVOC application-memory route changed only its `SIZE` resource:
  the preferred classic Mac partition rose from 5,760,000 bytes to 32 MiB and
  the minimum from 3,760,000 bytes to 8 MiB, while its data fork stayed
  byte-identical. It reproduced the exact same resource-allocation dialog with
  no RAVE/profile traffic. The generic Finder partition size is therefore not
  the missing resource. The separately installed full retail application's
  460,600-byte resource fork and its external `RB Geometry`, `RB Objects`, and
  `RB Sounds` forks (1,027,602, 1,442,614, and 1,261,043 bytes) are also intact.
  HAVOC exposes only the generic resource-memory message, not Dark Vengeance's
  `-49`. Static analysis narrows that alert to exactly five failed `NewHandle`
  sites: two dynamic signed-16-bit screen/resource sizes and fixed requests of
  7,562, 4,096, or 131,072 bytes. The external resource opens occur later and
  use different alerts. The two shared-engine failures must not be collapsed
  into one exact cause without the one-launch return-PC traces.
- The return-PC traces overturn that provisional classification and identify a
  shared, exact driver cause. The old PPC video NDRV returned `noErr` for
  synchronous `cscGetGamma` while setting no table pointer. Dark Vengeance's
  `SetupGammaAdjustment__14GGraphicDeviceFv` and HAVOC both dereference that
  null result, interpret low memory as `GammaTbl` fields, and issue a signed
  `-49` allocation. Dark's independent file trace records a successful
  `DARKVENG.INI` open with refnum `0x0552`, cached size 2,807, and position
  zero, so preference-file sizing was not responsible. The rebuilt NDRV returns
  a persistent normalized 3-channel, 256-entry, 8-bit gamma table initialized
  to identity and refreshed by `SetGamma`; native layout/identity tests pass.
  Candidate SHA-256 is
  `3b687b15b4ed321eaee6822438be5bba55a5ab1d8a3ef948fe8f63996a7c5722`.
  HAVOC then reaches coherent cockpit gameplay and passes a 0.603298 named
  frame-change gate. Dark clears the allocation alert and exposes a narrower
  period-RAVE ABI mismatch: it calls `QAGetNoticeMethod` with a callback output
  but a null refCon output. GXMetal now permits either output independently and
  invokes the registered selector-4 buffer notice synchronously from
  `RenderEnd` using a readback memory device. The broad
  `kQAOptional_BufferComposite` bit remains unadvertised. The final route
  reaches the Reality Bytes splash, animated title, and coherent textured
  player/enemy/portal gameplay; `w` changes the frame and a 15-second soak
  remains animated. Its intermediate selection/loading capture is mostly black,
  so a fully drawn interactive menu is not claimed. Evidence is retained under
  `context/gxmetal-games/evidence/reality-bytes-runtime-trace-20260827/`,
  `context/gxmetal-games/evidence/gxmetal-havoc-getgamma-gameplay-input-v3-20260827/`,
  `context/gxmetal-games/evidence/gxmetal-dark-buffer-notice-callback-hit-final-20260827/`,
  and
  `context/gxmetal-games/evidence/gxmetal-dark-title-menu-gameplay-final-20260827/`.
- Replayed Oni on an immutable exact-final beta-3 disk. It remains coherent
  through the former +45 corruption boundary and roughly 176 seconds of live
  rendering. The trace records 10,126,893 ATI-private calls, 11,238,479 queued
  triangles, 7,475 direct frames, and zero rejects, anomalies, context
  fallbacks, or host fallbacks. Delivered movement/combat keys and Command-Q
  did not create defensible visual changes, so input and in-game relaunch
  remain open.
- A follow-up exact-final route proves the explicit main-menu Quit→Yes path,
  Finder return, and immediate clean relaunch. Load Game also reaches the
  stored warehouse checkpoint. Its early visual-change gate correctly caught
  that portrait dialogue was still advancing while movement input arrived;
  those changing portraits are not counted as player control, and the F1
  in-game lifecycle remains to be reached after the dialogue settles.
- The later matched lifecycle discriminator narrows process two to Oni's movie
  path. A menu-only first process followed by early Escape in process two
  recreates InputSprocket, resets GXMetal upload IDs, creates a second context,
  and reaches the menu. The full warehouse→clean-quit route instead remains
  static in Bink/level-0 startup with upload IDs ending at the first process's
  1…210 sequence. A short launch-arguments qualifier visibly proves literal
  `-nosound`; the full-route discriminator remains open because relative
  gameplay desynchronized the VNC pointer model. The harness now provides an
  explicit corner-to-corner motion-only rehome action for a direct-frame-gated
  replay. Evidence is retained under
  `context/gxmetal-games/evidence/gxmetal-oni-fullgame-first-second-nosound-rehomed-20260827/`.
- Corrected Myth II's driver-specific transition failure. Proven ATI private
  slot-2 calls now replace flags-zero, single-level base images with the
  existing texture's exact format and dimensions without changing its resource
  identity; slot 4 returns the synchronous 24-byte `TQADevice` draw-buffer
  view used by Myth; and textured rendering with depth and fog disabled accepts
  finite signed eye depth while still rejecting non-finite input. The retained
  final-candidate silent route reaches a coherent battlefield at 25.74-26.59
  fps, records 775,456 draws, balanced 896/896 frames, 28,247 slot-2 calls and
  496 slot-4 calls with successful final observed results, and zero
  transport/resource rejects. Evidence is retained under
  `context/gxmetal-games/evidence/gxmetal-mythii-final-upload-order-20260827`.
  Focused guest and Metal tests retain the exact update, readback, upload
  ordering, and 224-byte textured-fan contracts.
- Built an immutable unified post-2.2.1 candidate from NDRV
  `3b687b15…c5722` and Tools CD `ed6ba9cf…d1d`, then replayed all ten installed
  games with host audio and guest networking disabled. Bugdom, Future Cop,
  Weekend Warrior, Cro-Mag's full race, Dark Vengeance, Myth II, Oni's full
  quit/relaunch lifecycle, and HAVOC's first-run-to-cockpit route pass semantic
  gameplay gates and visual review. Combat Mission passes its coherent setup
  scope. A later 409.892-second route also completes the full
  mounted/disembark/GO turn, and a separate recovery route reaches the real
  Force Quit confirmation, Finder, and a coherent second scenario selector.
  UT renders both
  processes, cleanly quits the first, and accepts held Control in the second.
  The exact read-only `CheckButtonKeys` probe relocates the live PEF from an
  invariant instruction window, observes Control value `0x08` in `LMKeyMap`
  and UT's stack-local modifier field, and stops at UT's transition-emission
  instruction; the reviewed final frame visibly fires the weapon. The earlier
  static post-death frame was therefore not a GXMetal, QEMU, ADB, or stale-key
  failure. Timing-only Dark and coordinate-only HAVOC routes
  produced false automation completions during this replay; both are replaced
  by reusable path, pixel, frame-change, and color-range oracles in
  `gxmetal/compatibility/`. A separate current-driver Quake III Q3DM1 run
  passes five reviewed courtyard/arch/statue/passage views, four motion gates,
  and dark-world fractions 0.001389 and 0.005012 against the 0.05 limit.
  Evidence is retained under the `gxmetal-post221-wave1-*`,
  `gxmetal-post221-wave2-*`, `gxmetal-post221-wave3-*`, and
  `gxmetal-post221-quake3-five-view-20260827` directories.
- Added independent post-release lifecycle routes rather than extending every
  game serially in one VM. Bugdom cleanly quits from its main menu and
  relaunches. Future Cop and Weekend Warrior each prove Finder recovery and a
  second rendered launch, while remaining explicitly classified as recovery
  rather than clean application exits. A matched Bugdom run with GXMetal
  Input enabled and disabled reaches the same late collision, still opens the
  pause menu in both configurations, and records 20,399 successful host reads
  out of 20,399 with no stale callback or handoff faults. This rules out the
  custom input extension as the cause of the collision-static frames.
