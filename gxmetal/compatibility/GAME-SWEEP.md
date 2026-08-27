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
| `bugdom` | Bugdom 1.2.1 | QuickDraw 3D 1.6 / RAVE, ATI behavior | Highest quality; load The Lawn; verify terrain, fog, foliage alpha, and HUD for five minutes; quit and relaunch | Exact signed beta-3 gameplay pass: coherent Lawn rendering, scripted movement/turning, 1,069,012 traced draws, direct presentation, and zero fallback/rejects |
| `cro-mag-rally` | Cro-Mag Rally Demo | Apple OpenGL 1.1.2 | Confirm hardware/OpenGL; complete a lap with terrain, particles, HUD, transparency, and camera transitions | Gameplay smoke pass at 640x480: Practice/Desert renders coherently, acceleration and steering visibly respond, 24-28 fps, ~204 direct draws/frame, zero fallback, and expected demo exit screen |
| `weekend-warrior` | Weekend Warrior | QuickDraw 3D / RAVE | Load the first arena; verify camera clipping, textured characters, UI, depth ordering, and transitions for five minutes | Exact signed beta-3 gameplay smoke plus recovery-lifecycle pass: Center Stage play has strong movement/camera change, 1,585,966 traced draws, and zero fallback/rejects; title → OS Force Quit → Finder → same-boot relaunch and second-title soak also pass |
| `future-cop` | Future Cop: LAPD Demo | Selectable QuickDraw 3D RAVE | Select RAVE; enter Crime War; verify weapon blending, transparent HUD, depth, and explosions | Exact signed beta-3 gameplay/lifecycle-soak pass: movement and turning reposition the mech/camera, fire shows impact sparks and ammo consumption, ~87 seconds live plus 65-second soak remain coherent, and 8,772,136 draws have zero fallback/rejects; scripted relaunch remains open |
| `dark-vengeance` | Dark Vengeance Demo | Direct RAVE | Reach first combat and scripted sequence; inspect lighting, translucent effects, animated geometry, and camera motion | Demo 1.0.2 fails identically before RAVE in GXMetal/software; the retail updater requires its legitimate CD; the genuine 1.2 demo installs but stops at memory allocation size -49 with zero contexts/draws |
| `myth-ii` | Myth II: Soulblighter 1.5.1 Demo | RAVE | Select RAVE; load a solo map; pan, zoom, rotate, issue orders, and verify terrain, water, units, decals, projectiles, and explosions | Exact signed beta-3 battlefield/partial-input pass: coherent rendering plus visibly proven single selection, forward/back and left/right camera motion, rotation, and zoom-in; refreshed replay has 3,589,155 traced draws and zero fallback/rejects; group orders and the remaining controls stay open |
| `unreal-tournament` | Unreal Tournament 348m3 Demo | ATI renderer through RAVE | Confirm RAVE; render intro flyby; run a five-minute bot match checking lightmaps, fog, weapon alpha, HUD, and texture cycling | Sound-disabled live Practice pass: coherent Tempest world, weapon and HUD; movement, turning, firing, damage, death and respawn visually proven; 9,472 direct frames at 80.36-131.59 fps with zero fallback/rejects |
| `combat-mission` | Combat Mission: Beyond Overlord 1.02 Demo | RAVE hardware probe | Complete detection; load Chance Encounter; move through the map and execute a turn with terrain, markers, smoke, and animation | Exact signed beta-3 full-turn pass: Rifle 45 Sqd visibly disembarks, crosses open ground, reaches the plotted endpoint with DONE, and remains stable through +145 seconds; 10,357,060 traced draws, zero fallback/rejects; scripted relaunch remains open |
| `oni` | Oni Demo | Classic Apple OpenGL | Reach training and first fight; verify animation, lightmaps, transparency, HUD, and an indoor/outdoor transition | Exact signed beta-3 long-rendering/menu-lifecycle partial pass through Combat Training, the formerly corrupt +45-second transition, and ~176 seconds live; 11,238,479 queued triangles and 7,475 direct frames with zero rejects/fallback; explicit menu quit and relaunch plus saved-checkpoint load pass, while gameplay input and in-game F1 lifecycle stay open |
| `havoc` | Havoc Demo | First shipping QuickDraw 3D RAVE game | Select accelerated rendering; enter the demo arena; verify terrain, fog, textured objects, transparency, HUD, and camera motion for five minutes | Two independently preserved demos stop at the same resource-allocation dialog before renderer creation; the alternate Tucows build produces no RAVE/profile traffic on exact beta-3, and raising its classic Mac partition from 5.76 to 32 MB does not alter the failure |

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
  Because classic Mac OS error -49 is `opWrErr` (file already open for writing),
  the observed failure is classified as a media/runtime compatibility problem,
  not a GXMetal rendering failure. Evidence and the completed review record are
  under
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
  ATI-private calls. The next bounded investigation is its supported setup/
  engine path and blank default `HARDWARE` field, not a GXMetal workaround.
- A controlled HAVOC application-memory route changed only its `SIZE` resource:
  the preferred classic Mac partition rose from 5,760,000 bytes to 32 MiB and
  the minimum from 3,760,000 bytes to 8 MiB, while its data fork stayed
  byte-identical. It reproduced the exact same resource-allocation dialog with
  no RAVE/profile traffic. The generic Finder partition size is therefore not
  the missing resource.
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
