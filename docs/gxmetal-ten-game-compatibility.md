# GXMetal ten-game compatibility snapshot

This is the evidence-backed state of the Mac OS 9 ten-game corpus on
2026-08-27. It is intentionally stricter than “the application opened.” A
gameplay pass requires coherent accelerated rendering, live input, sustained
presentation, and no fallback/error evidence. Clean quit/relaunch and audio
are tracked separately.

## Bottom line

GXMetal is now likely to work with more RAVE games than the original test set,
but the evidence still does not support a universal-compatibility claim.

- Bugdom, Future Cop, and Weekend Warrior all pass exact signed beta-3
  candidate routes through menus and coherent representative 3D gameplay with
  verified input. Their extracted final guest traces total more than 11.4
  million draws with zero rejected textured draws, invalid resources, texture
  rejects, geometry anomalies, or context fallbacks.
- Cro-Mag Rally now has a fully scripted 640x480 Practice/Desert route with
  acceleration and steering input, a gameplay soak, and a clean in-game exit.
  The current Q3-fixed driver sustained roughly 24-28 fps and about 204 direct
  draws per frame with zero fallback.
- Combat Mission now passes an exact signed beta-3 full-turn semantic route.
  Sustained mouse input selects Rifle 45 Sqd, plots a visible cyan
  `Move / Disembark` command, and presses GO. Fixed-camera captures prove the
  squad mounted before playback, disembarked by +40 seconds, crossed farther
  into open ground by +65, and reached the plotted endpoint with `DONE` by
  +100; the +100 and +145 captures are byte-identical. The trace records
  10,357,060 draws, balanced 5,312/5,312 render calls, and zero fallback or
  draw/resource rejects. Scripted quit/relaunch remains unproven.
- All four reviewed runs end with direct GXMetal presentation and zero
  fallback frames. The rc23 semantic routes ran concurrently from four
  independent clones and completed in 166 seconds wall-clock time.
- An accelerated Apple AGL/OpenGL 1.1 probe also passes context creation,
  clear, triangles, triangle strips, triangle fans, quads, quad strips, and
  polygon rendering, RGBA texture upload/sampling,
  source-alpha blending, depth ordering, pixel readback, texture deletion,
  display preservation, and teardown. This is broad core-path qualification,
  not a claim that every OpenGL feature or game is covered.
- Cro-Mag Rally renders its title/menu/loading path correctly on the signed
  2.1.3 candidate. Oni's previously black ATI/OpenGL path now renders its
  complete main menu, new-game UI, and coherent Combat Training gameplay on
  the exact signed beta-3 candidate through the formerly corrupt +45-second
  transition and about 176 seconds of designated live rendering. Static GLD audit
  identified private slots 49/50 as center-plus-contiguous-rim triangle fans,
  not independent pointer triangles; expanding each four-vertex call as two
  adjacent-rim triangles removes the giant wedge. The reviewed +45 capture's
  pale-corruption fraction fell from rc21's failing 0.106921 to 0.005498.
  The exact-final Oni trace records 10,126,893 ATI-private calls, 11,238,479
  queued triangles, 7,475 direct frames, and zero fallback, rejected
  triangles, geometry anomalies, or transport errors. The delivered gameplay
  controls and Command-Q produced no defensible visual change, so input and
  in-game quit/relaunch are not counted as passes. A later exact-final route
  does prove Oni's explicit main-menu Quit→Yes return to Finder and immediate
  clean relaunch; Load Game also reaches the stored warehouse checkpoint, but
  its first input gates overlap an active portrait-dialogue sequence and are
  not counted as gameplay control. A corrected warehouse route on the rebuilt
  signed candidate closes the modal first, then proves relative look
  (0.724367 world change), forward movement (0.874583), F1 Data Comlink
  open/resume (0.942946), Escape main menu, Quit/Yes, and a clean Finder
  return. All 9,186 frames are direct across 68 profiles with zero fallback,
  corruption, reject, or transport fault. A same-boot second process renders
  its animated Bungie intro coherently, but VNC Escape does not advance that
  intro; second-process exit is therefore still unproven.
- Dark Vengeance and HAVOC reproduce deterministic failures before either app
  submits accelerated work. The genuine Dark Vengeance Demo 1.2 VISE install
  completes, but its unchanged application stops at `Memory allocation failed
  (size = -49)` with zero contexts, draws, textures, or private calls. A
  second, independently preserved Tucows HAVOC demo (`27957a5e…dfa21`) also
  stops at the same resource-allocation dialog on exact beta-3; the log
  contains no RAVE context or GXMetal profile traffic. Raising that
  application's classic Mac preferred partition from 5.76 MB to 32 MB still
  produces the identical dialog before RAVE. A separately preserved full
  retail DiskDup image (`ba90fbd9…adebd`) mounts correctly, its installer
  completes, and its CD-backed application is distinct from both demos, but
  both its original 5.64 MB partition and a data-fork-identical 32 MiB
  partition reproduce the same dialog before any GXMetal profile. The
  original builds' Apple Software controls match the pre-RAVE classification.
  Unreal Tournament's default startup also remains black on its software
  control. Controlled GXMetal runs isolate `UseSound=True` as the blocker:
  sound-disabled starts render the same correct main menu in windowed and
  fullscreen modes, while a sound-enabled/windowed start submits no RAVE work.
  The final sound-disabled route now advances through the Tempest ready state
  into live Practice gameplay with working movement, turning, firing, damage,
  death, and respawn rendering. On the rebuilt candidate, a focus-corrected
  route again reaches coherent Tempest and proves initial live transition
  (0.289531), forward movement (0.737702), and turn/strafe change (0.901147).
  Its minute-one frame has health 100 and ammo 21 rather than the starting 30;
  minutes two through five remain coherent but byte-identical despite further
  inputs, so they count as stable retained rendering rather than a dynamic
  five-minute action soak. All 15,084 frames across 63 profiles are direct,
  with zero fallback, writebacks, rejects, out-of-range draws, or Metal/queue
  faults. The focused viewport consumes the later menu click, so clean exit
  and same-boot relaunch remain unproven.
  Myth II now reaches its fully rendered “Into the Breach” battlefield after
  the animated map and journal. GXMetal implements the proven flags-zero,
  single-level ATI private slot-2 base-image replacement and slot 4's
  synchronous one-shot draw-buffer readback, and accepts finite legacy
  eye-space Z in textured draws when both depth and fog are disabled. The
  final-candidate silent run records 775,456 draws, balanced 896/896 frames,
  28,247 private slot-2 calls and 496 slot-4 calls with successful final
  observed results, direct presentation, and zero transport or resource
  rejects. A second exact-candidate route visibly proves single-unit selection,
  camera forward/back and left/right motion, left/right rotation, and zoom-in.
  A refreshed signed-candidate replay records 3,589,155 draws, balanced
  3,584/3,584 frames, 120,923 slot-2 calls, 668 slot-4 calls, and zero
  fallback, rejects, or geometry anomalies. Band/Select All, group orders,
  Stop, zoom-out, and orbit remain unproven; its final 38.37 seconds were
  byte-identical despite live direct presentation and therefore are not
  counted as input evidence. An effects-film replay then exposed a release
  regression: treating every ATI-private, non-perspective-Z draw as OpenGL
  homogeneous geometry clipped Myth's terrain at eye-space Z near -20,000
  while leaving its HUD, units, foliage, and effects visible. GXMetal now
  requires negative reciprocal-W, active depth, or active fog before selecting
  that OpenGL path. On the rebuilt signed candidate, the effects film's
  playfield is only 0.001222 near-black initially and 0.010420 at +25 seconds
  (the bad build measured 0.605066 and 0.804112), with 0.951820 frame change,
  3,075 sampled draws free of out-of-range Z, 2,882 direct frames, and zero
  fallback, writebacks, rejects, or transport faults.
- Quake III's 2.2 beta visual regression was localized to valid ATI-private
  slot-60 triangle fans whose fourth argument is zero. Rendering those calls
  again restores the missing world surfaces. A refreshed silent Q3DM1 route
  retains five useful human-reviewed views covering the spawn courtyard,
  ornate and lit passage arches, walls, floor, statues, pedestals, and steps.
  On the rebuilt Myth-depth-fix candidate, its two explicit near-black
  missing-surface crops measure 0.002068 and 0.006214 against a 0.05 limit,
  while four independent viewpoint changes measure 0.590449 through 0.741227
  against a 0.05 minimum. All 32 GXMetal profiles report zero fallback, and
  the signed candidate records no queue/context fault or rejected draw.
- Force Quit recovery now uses an owner-only rendering-generation reset. An
  intermediate packet-reset design exposed the exact two-client race: the
  InputSprocket-side connection could overwrite a RAVE context header reserved
  but not yet published by the game. The final generic connector is read-only;
  the first `GXMetalDrawPrivateNew` resets queue/context/resource state through
  MMIO, while QEMU preserves independent cursor/input state. A deterministic
  two-transport pipeline test and the live Weekend Warrior relaunch gate retain
  the failure.
- Unreal Tournament v348 exposed a guest CFM code-layout regression in the
  first combined Myth slot-2/slot-4 build: engine discovery stopped after the
  VendorID, EngineID, and Revision Gestalts even though neither private method
  had executed. Builds with either method alone passed. Size-optimizing only
  the cold slot-2 update path retains both implementations and restores the
  complete accelerated menu with 21,367 direct frames and zero presentation
  fallback. The guest build now rejects later, unqualified CFM layouts before
  they can silently reintroduce the game-only failure. Live gameplay then
  exposed finite ATI-private fans outside the normalized clip volume. GXMetal
  now reconstructs their signed homogeneous coordinates for Metal clipping
  while keeping public RAVE validation strict. The retained Tempest route has
  9,472 direct frames, zero fallback or rejects, 80.36-131.59 fps, and distinct
  movement, turn, combat, damage, death, and respawn frames.

## Current candidate matrix

| Game | Classification | Furthest qualified state | Important open item |
|---|---|---|---|
| Bugdom | Gameplay pass on exact beta-3 | Correct full-color Lawn rendering, fog, foliage, HUD, and scripted movement/turning; 67-69 fps, 1,069,012 traced draws, direct frames with zero fallback/rejects | Retain the separately proven long soak and quit/relaunch gate; audio |
| Future Cop: LAPD | Gameplay/lifecycle-soak pass on exact beta-3 | Coherent Crime War world, vehicles, HUD and effects; forward/turn input visibly repositions the mech/camera, fire produces impact sparks and ammo 7500→7481→7463, and ~87 seconds live plus a 65-second soak remain coherent; 8,772,136 draws, zero fallback/rejects | Clean quit/relaunch and audio |
| Combat Mission | Full-turn gameplay/rendering pass on exact beta-3 | Rifle 45 Sqd is mounted before GO, visibly disembarks by +40, crosses open ground by +65, and reaches the plotted endpoint with DONE by +100; stable through +145; 10,357,060 traced draws, zero fallback/rejects | Clean scripted quit/relaunch and audio |
| Weekend Warrior | Gameplay smoke + recovery lifecycle pass on exact beta-3 | Fully scripted selection, Center Stage 3D play, strong movement/camera change, 1,585,966 traced draws with zero fallback/rejects; separate title → OS Force Quit → Finder → same-boot relaunch and second-title soak pass | Longer arena soak and audio |
| Cro-Mag Rally | Gameplay smoke pass on current Q3 fix | Correct accelerated title/menu/loading and Practice/Desert gameplay at 640x480; acceleration and steering visibly alter position/heading; 24-28 fps, ~204 direct draws/frame, zero fallback; expected demo exit screen | Complete-lap/longer lifecycle soak and audio |
| Dark Vengeance | Pre-RAVE app/runtime blocker across three routes | Demo 1.0.2 fails identically in accelerated/software modes; the retail updater requires its legitimate CD; the genuine 1.2 demo installs but exits with memory allocation size -49 and zero contexts/draws | Determine supported 1.2 engine/setup path and why its default HARDWARE field is blank, then rerun a software A/B |
| HAVOC | Matched pre-RAVE app/runtime blocker | Two independently preserved demos and a separately installed full retail/CD build stop at the same resource-allocation dialog before renderer creation; both demo and retail 32 MiB application-partition controls remain pre-RAVE | Investigate individual resource files, screen/runtime assumptions, and the allocation call site; the known-different retail-build route is exhausted |
| Myth II | Battlefield/effects rendering and partial-input pass on rebuilt beta-3 | Correct main UI, campaign map, journal, coherent battlefield, and an effects-heavy film with intact terrain through +25; visibly proven single selection, camera movement/rotation, and zoom-in; fixed effects replay has 2,882 direct frames and zero fallback/rejects/out-of-range-Z draws | Prove group selection/orders, Stop, zoom-out/orbit, and clean film exit/relaunch; audio |
| Oni | Gameplay/input + first-process lifecycle pass on rebuilt beta-3 | Correct ATI/OpenGL menus and coherent warehouse gameplay; relative look, forward movement, F1 Comlink/resume, Escape menu, Quit/Yes, and Finder return are visibly proven; 9,186 direct frames with zero fallback/rejects; same-boot second process renders its animated intro | Make VNC input advance/exit the second-process intro, then prove second quit; audio |
| Unreal Tournament | Live Practice gameplay/rendering pass with startup workaround | Sound-disabled rebuilt driver reaches coherent Tempest; initial transition, forward, and turn/strafe changes are proven; minute one shows ammo 21, while minutes 2–5 retain a coherent but byte-identical frame; 15,084 direct frames, zero fallback/rejects | Diagnose the post-minute-one lack of visible advance and make Game-menu Quit work after viewport capture; keep sound disabled under the current VM audio path |

## Driver gates on the exact candidate

The immutable test base was prepared directly from the signed app, with the
guest artifacts extracted from its Tools CD and General Controls' improper-
shutdown disk check disabled. The source base was never attached writable.

The two driver gates ran concurrently from clean per-test clones:

- Expanded rc23 `GXMetal AGL Probe` reported `PASS_ACCELERATED`, renderer ID
  `0x00021000`, vendor `ATI Technologies Inc.`, renderer `Rage 128 OpenGL
  Engine`, OpenGL `1.1.ATI-5.131331`, correct triangle/background, RGBA
  texture, source-alpha blend, and depth-order readbacks; all six filled-mode
  samples were `0x0010F51B`, the disjoint-list guard remained background blue
  at `0x001B2442`, and a textured polygon crossing the viewport produced the
  expected clipped yellow remnant `0x00FFF417`. Texture deletion, displayed-
  pixel preservation, and teardown also succeeded with zero GL/AGL errors.
- `GXMetal Test` passed the full advertised RAVE contract, including discovery,
  capability negotiation, depth, blending, alpha/chromakey, clipping, texture
  formats, public multitexture, dynamic resources, ATI private no-copy data,
  large mesh batches, bitmap/scaling, dirty presentation, double buffering,
  and framebuffer access. Its comparison workload measured a 10.34× speedup
  over Apple Software RAVE in this VM run.

Accepted evidence:

- `context/gxmetal-games/evidence/gxmetal-driver-smoke-rc23-20260826`
- `context/gxmetal-games/evidence/gxmetal-oni-fan-topology-rc23-20260826`
- `context/gxmetal-games/evidence/gxmetal-quake3-multiview-current2-20260826`
- `context/gxmetal-games/evidence/gxmetal-quake3-final-upload-order-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-ut-layout-fixed-candidate-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-homogeneous-clip-regression-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-render-owner-reset-candidate-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-five-view-regression-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-myth-depth-fix-five-view-20260827`
- `context/gxmetal-games/evidence/gxmetal-ut-beta3-combinedreal-slot2compact-menu-control-20260827`
- `context/gxmetal-games/evidence/gxmetal-ut-beta3-homogeneous-clip-live-gameplay-20260827`
- `context/gxmetal-games/evidence/gxmetal-ut-render-owner-reset-dedicated-retry-20260827`
- `context/gxmetal-games/evidence/gxmetal-slot4-noz-conformance-20260827`
- `context/gxmetal-games/evidence/gxmetal-mythii-final-upload-order-20260827`
- `context/gxmetal-games/evidence/gxmetal-mythii-beta3-input-lifecycle-retry2-20260827`
- `context/gxmetal-games/evidence/gxmetal-mythii-ut-layout-fixed-candidate-20260827`
- `context/gxmetal-games/evidence/gxmetal-three-game-beta3-final-20260827`
- `context/gxmetal-games/evidence/gxmetal-future-cop-beta3-final-lifecycle-20260827`
- `context/gxmetal-games/evidence/gxmetal-weekend-warrior-render-owner-reset-candidate-20260827`
- `context/gxmetal-games/evidence/cromag-current-q3fix-20260826-route1`
- `context/gxmetal-games/evidence/combat-mission-current-q3fix-20260826-battle-v3`
- `context/gxmetal-games/evidence/combat-mission-current-q3fix-20260826-input-v4`
- `context/gxmetal-games/evidence/combat-mission-beta3-held-input-v3-20260827`
- `context/gxmetal-games/evidence/combat-mission-beta3-full-turn-lifecycle-20260827`
- `context/gxmetal-games/evidence/gxmetal-oni-beta3-final-lifecycle-input-20260827`
- `context/gxmetal-games/evidence/gxmetal-oni-beta3-final-menu-quit-load-input-retry2-20260827`
- `context/gxmetal-games/evidence/gxmetal-havoc-tucows-beta3-20260827`
- `context/gxmetal-games/evidence/gxmetal-havoc-tucows-size32-beta3-20260827`
- `context/gxmetal-games/evidence/gxmetal-havoc-full-install-20260827`
- `context/gxmetal-games/evidence/gxmetal-havoc-full-installed-launch-20260827`
- `context/gxmetal-games/evidence/gxmetal-havoc-full-size32-launch-20260827`
- `context/gxmetal-games/evidence/gxmetal-mythii-ati-depth-fix-effects-20260827`
- `context/gxmetal-games/evidence/gxmetal-oni-render-owner-reset-lifecycle-retry4-20260827`
- `context/gxmetal-games/evidence/gxmetal-ut-release-candidate-five-minute-lifecycle-retry-20260827`
- `context/gxmetal-games/evidence/gxmetal-dark-vengeance-demo12-beta3-exact-final-20260827`
- `context/gxmetal-games/evidence/ut-current-q3fix-20260826-exact-rc6`
- `context/gxmetal-games/evidence/dark-vengeance12-current-q3fix-20260826-qualification`
- `context/gxmetal-games/evidence/software-mythii-prologue-early-escape-current-20260827`
- `context/gxmetal-games/evidence/gxmetal-mythii-prologue-early-escape-current-20260827`
- `context/gxmetal-games/evidence/gxmetal-four-game-fan-topology-rc23-20260826`
- `context/gxmetal-games/evidence/gxmetal-driver-smoke-readback-signed-final-20260826`
- `context/gxmetal-games/evidence/gxmetal-four-game-readback-signed-final-20260826`
- `context/gxmetal-games/evidence/gxmetal-weekend-warrior-readback-signed-retry2-20260826`
- `context/gxmetal-games/evidence/gxmetal-driver-smoke-rc6-20260826`
- `context/gxmetal-games/evidence/gxmetal-cromag-regression-rc6-20260826`
- `context/gxmetal-games/evidence/gxmetal-oni-depth-state-rc6-20260826`
- `context/gxmetal-games/evidence/gxmetal-ut-startup-isolation-rc6-20260826`
- `context/gxmetal-games/evidence/gxmetal-mythii-gameplay-focused-rc6-20260826`
- `context/gxmetal-games/evidence/gxmetal-oni-start-direct-click-rc6-20260826/retry`
- `context/gxmetal-games/evidence/gxmetal-agl-expanded-rc8-20260826`
- `context/gxmetal-games/evidence/gxmetal-agl-filled-modes-rc15-20260826`
- `context/gxmetal-games/evidence/gxmetal-agl-clipped-rc18-20260826`
- `context/gxmetal-games/evidence/gxmetal-os9-conformance-rc18-final-20260826`
- `context/gxmetal-games/evidence/gxmetal-oni-gameplay-rc18-20260826`
- `context/gxmetal-games/evidence/gxmetal-driver-smoke-rc20-20260826`
- `context/gxmetal-games/evidence/gxmetal-oni-transition-gate-rc20-20260826`
- `context/gxmetal-games/evidence/gxmetal-driver-smoke-rc21-20260826`
- `context/gxmetal-games/evidence/gxmetal-oni-transition-gate-rc21-20260826`

One isolated Weekend Warrior retry stopped in OpenBIOS before Finder and left
its clone byte-identical to the base. A fresh isolated retry booted normally
and passed the complete corrected recipe. That retained failure is classified
as a VM-start transient, not a game or GXMetal failure, because no guest or
driver work occurred.

## Final signed beta-3 candidate identity

- Immutable four-game evidence base with the UT layout fix SHA-256:
  `61541dac3a483cd7ce71edf6a820649fe9ea5651465f5b338765859d6f716cfe`
- Immutable dedicated Quake III evidence base SHA-256:
  `35c1b3d34b6dc74830548259d3f94535ec4bbf303466fb6395f252c41890083b`
- Prepared render-owner-reset Quake III base SHA-256:
  `4686402815fa15daae7c82724c915f66f05dac2c73d6563378864802f219a117`
- Prepared render-owner-reset four-game base SHA-256:
  `035b0905a09a3b75917d957d05e767828ed63d880c63b106231f9cebaf9cb284`
- Prepared render-owner-reset Unreal Tournament base SHA-256:
  `321cacd4950e38886f3546d7dd34072ffe5702bb0f48519a2c7b8bee926767d0`
- Prepared unlimited-score Unreal Tournament soak derivative SHA-256:
  `431033669630bb7929c5dba3234b32b3b482c53ec9fadef779af70b54c94a9d9`
- Prepared render-owner-reset Myth II base SHA-256:
  `19fc30b1df7b0a21d1d2476d643e06d6af765f4f3143dacc460bdeaabd8a6261`
- Immutable dedicated Myth II evidence base SHA-256:
  `48efe79ecca390bda19fe52e06c9664216401406c015da184fd2df3f6f1c2ac9`
- Immutable exact-final Oni evidence base SHA-256:
  `c5646c8fcf9d499d9ec3498c99a50baf0bfe0b7a5967c5fefe444142f24c387d`
- Prepared render-owner-reset Oni base SHA-256:
  `58948903a2efe6cc15b893dc11d23739244d003c1562704e293ae50c426616e7`
- Immutable alternate-media staging base SHA-256:
  `66d134bde978624864b2ac126a941f2518cebc3c3a58ce7527a802f8ac3748e8`
- Prepared full-retail HAVOC installed base SHA-256:
  `b2ff47a759be6b4848f2195a29c97dbef48b62964bc56d12103380bcd868ab13`
- Signed Power Mac QEMU SHA-256:
  `e62f95486b7d8526fbfa0ca6d299f9f88cf9f2edcbcbee01d84529202fdfc72e`
- Packaged Tools CD SHA-256:
  `fb2bb086d28fd840cb6284dd64c6e6cd9aab2153cb91e185ba3a706c17df2250`
- Packaged Power Mac video NDRV SHA-256:
  `c5e63db45de7b58ea286f785e56811209b090673635da81fea07f9871312b27d`
- NDRV loader SHA-256:
  `a5b441048916fdb58291e34ab029cfd06d7e6da9380b6035bd4d8740f040f27d`
- Packaged GXMetal PEF data-fork SHA-256:
  `b50b4abe5d672be78aafd43d04673b68ab7e0f2a3b168dc231c3fcaae46c0f70`

The final candidate defaults to the native Cocoa VM window; VNC remains an
optional display/automation mode. Every retained qualification run in this
snapshot explicitly used the `none` audio backend. The prepared evidence bases
also have Mac OS 9's improper-shutdown disk check disabled and were verified
unchanged after each sweep.

## 2.1.4 qualification rc23 identity

- Immutable rc23 game-evidence base SHA-256:
  `cb9946f10dd438ea2c12c240907352664fb2c168cb1a40a13a6badaf07e2d736`
- Packaged Tools CD SHA-256:
  `dbc7f44cf66dfe036a44c3cd1f5b42abf431bc56d02d69505a984b7b3fc1710b`
- Packaged Power Mac QEMU SHA-256:
  `7ba4648bcae8fe50c3dc349486e53b8bf3c70af926c3a11b8a920e26d0384f94`
- Power Mac NDRV SHA-256:
  `c5e63db45de7b58ea286f785e56811209b090673635da81fea07f9871312b27d`
- NDRV loader SHA-256:
  `a5b441048916fdb58291e34ab029cfd06d7e6da9380b6035bd4d8740f040f27d`
- Guest GXMetal driver MacBinary SHA-256:
  `c96bc113ad85467d295b539c20a49d79a7e34e362b9c02ff99fa72106b3af3a8`

The base-preparation automation installed the packaged driver, persistently
disabled Mac OS 9's improper-shutdown disk check, rebooted, verified the
General Controls preference resource, and made the resulting image read-only.
The Oni pair, driver-gate pair, and four-game sweep all used independent clones
of this image and verified that the immutable source hash did not change.

## Published 2.1.4 artifact identity

- Notarized and stapled `ClassicMac.dmg` SHA-256:
  `e3517aefa8536c391c61d55b09e3b4846ccfcdd0b048a805e6258abd67ab367b`
- Stapled `ClassicMac.zip` SHA-256:
  `3fc6cedd185cfceb23313dacf1b89070cb4599a9394ecf8d023aafc6a4df1df2`
- Signed Power Mac QEMU SHA-256:
  `5f2227484ff1f8e2096c96586b11ec134389b4d5dccec6af4dcf8ad4e11e2062`
- Bundled Tools CD SHA-256:
  `8d02934f343900f9a3b2b22b462f86950661df50fdf5a46c63fcca597bc0e61f`
- Bundled Power Mac NDRV SHA-256:
  `c5e63db45de7b58ea286f785e56811209b090673635da81fea07f9871312b27d`
- Bundled NDRV loader SHA-256:
  `a5b441048916fdb58291e34ab029cfd06d7e6da9380b6035bd4d8740f040f27d`
- GXMetal protocol-header SHA-256:
  `76b7a344eb832fd8195d17fd6baf255e50a3e5ce4d7a5c0d55b0e00691263ef2`
- Guest GXMetal driver MacBinary SHA-256:
  `814763ea8339bc91e5f1cf17cd793141de3b056470a747cf70bd5e3e210fe4a6`
- Guest GXMetal driver data-fork SHA-256:
  `f9c1df5e6e1052c136c41ff9d4ae416502f7b91fb42e9d9802deef3abeb9052b`

Apple accepted the application notarization submission
`146a247e-ee46-4d98-a195-678302c6133c` and DMG submission
`5e95d69b-30be-41d4-8f77-2d5620c65544`. Both tickets are stapled, and
Gatekeeper accepts the app and disk image. Release binaries were rebuilt with
2.1.4 version metadata after the rc23 VM gates; the topology implementation
and protocol are unchanged.

## Published 2.1.3 artifact identity

- Notarized and stapled `ClassicMac.dmg` SHA-256:
  `06e31f0a5126e3bcb0a64a2364987e880c1d9636877465d4d84bdaf36e796414`
- Signed Power Mac QEMU SHA-256:
  `0aa249562d49d2f4b548c5869e3c3319fdb087407ac0d880cc955f43b1936a39`
- Bundled Tools CD SHA-256:
  `12df3effb8196a364b69330ce5ec18a5fda0bbf168a86fb2e6b97520af6f6643`
- Bundled Power Mac NDRV SHA-256:
  `c5e63db45de7b58ea286f785e56811209b090673635da81fea07f9871312b27d`
- Bundled NDRV loader SHA-256:
  `a5b441048916fdb58291e34ab029cfd06d7e6da9380b6035bd4d8740f040f27d`
- GXMetal protocol-header SHA-256:
  `76b7a344eb832fd8195d17fd6baf255e50a3e5ce4d7a5c0d55b0e00691263ef2`
- Guest GXMetal driver MacBinary SHA-256:
  `9442fbfd53ae6f6a13d7e119216790ac34a37000d3fab7cd71716e64cae0b5c0`
- Guest GXMetal driver data-fork SHA-256:
  `8371b7d8b14b9d6c7b31dc39cdecb2471b77040bdc515291ed9599c84f5499a7`
- Immutable rc21 game-evidence base SHA-256:
  `2b5cca2052baa25b5145ed7163745f79de7ef88806a45bbd05dd0db8dee53ed4`

The final packaged binaries were rebuilt from the same source revision after
the rc21 VM gates. Signing and classic Mac artifact timestamps therefore make
the final QEMU, Tools CD, and whole MacBinary hashes differ from the rc21
candidate, while the compatibility-critical NDRV, protocol header, NDRV
loader, and GXMetal data fork remain identical. The immutable rc21 base hash
above identifies the exact disk used for the retained automated evidence; it
is not the downloadable release artifact.

The NDRV hash is a compatibility-critical part of this identity. An earlier
build had a 4 MiB transport contract while protocol 1.18 required 8 MiB; Mac OS
therefore withheld the GXMetal transport property and both AGL and RAVE fell
back. The build now stamps the NDRV with the protocol-header hash and refuses
to package a mismatch.

The published 2.1.3 candidate speaks protocol 1.23. It adds exact multi-rectangle
QuickDraw region clipping, accepts deep-Z contexts, corrects ATI multitexture
stage-zero discovery, preserves ATI private type-1006 RGBA byte order, covers
the vendor's filled primitive families and staged viewport-clipped polygon
path, and synchronizes ATI/OpenGL alpha, blend, depth, fog, channel-mask,
clear, and primary/secondary texture state from the vendor state block. The
initial full state snapshot prevents Apple's defaults from diverging from
GXMetal's; explicit `GL_NEVER` alpha-test behavior is preserved. The
strengthened AGL gate proves every common filled mode plus a clipped textured
polygon. rc23 retains that protocol while correcting the private fan ABI that
caused 2.1.3's later Oni geometry failure.

## Accepted historical evidence

The following runs came from earlier incremental candidates. They remain
useful for regression depth, but they are not substituted for a replay on the
signed candidate:

- Bugdom long gameplay/fog/lifecycle:
  `context/gxmetal-games/evidence/bugdom-lawn-fog-corrected-screen-skip-v14-20260825`
  and
  `context/gxmetal-games/evidence/bugdom-menu-lifecycle-automated-v14-20260825`
- Future Cop:
  `context/gxmetal-games/evidence/future-cop-gxmetal-input-adaptive-final`
- Combat Mission:
  `context/gxmetal-games/evidence/combat-mission-alpha1-fixed-production-20260825`
- Weekend Warrior:
  `context/gxmetal-games/evidence/weekend-warrior-primary-arena-final-20260825`
- Cro-Mag Rally:
  `context/gxmetal-games/evidence/cromag-manual-drag-640-gameplay-20260825`
- Dark Vengeance:
  `context/gxmetal-games/evidence/dark-vengeance-live-final-drawsprocket-off-2`
- HAVOC:
  `context/gxmetal-games/evidence/havoc-launch-128mb-20260825`
- Myth II:
  `context/gxmetal-games/evidence/gxmetal-mythii-final-upload-order-20260827`
- Oni:
  `context/gxmetal-games/evidence/oni-gxmetal-fatal-relaunch-20260825`
- Unreal Tournament:
  `context/gxmetal-games/evidence/gxmetal-ut-beta3-homogeneous-clip-live-gameplay-20260827`

## Next compatibility gates

1. Preserve Oni's now-passing warehouse input, F1, in-game quit, pale-range,
   and method-50 fan gates. Determine why VNC Escape does not advance the
   same-boot second process's coherently animated Bungie intro, then prove its
   second quit without weakening the first-process route.
2. Diagnose why Unreal Tournament's focus-corrected route retains a coherent
   but byte-identical frame after minute one, and make its Game-menu Quit work
   after viewport capture before claiming a longer bot match or clean relaunch.
   Extend Myth II through group selection/orders and clean film quit/relaunch
   while retaining the now-passing effects/terrain regression.
3. For Dark Vengeance 1.2, determine its supported engine/setup path and why
   the installed default leaves `HARDWARE` blank before running a software
   A/B. For HAVOC, both the bounded application-partition increase and the
   known-different full-retail/CD build are now exhausted; investigate its
   individual resource files, screen/runtime assumptions, and allocation call
   site. Do not weaken driver or VM memory safety around failures that occur
   before RAVE.
4. Require reviewed screenshots, direct presentation profiles with zero
   unexpected fallback, representative input, a longer soak, and clean
   lifecycle for every title advertised as supported.
5. Run correctness recipes concurrently, but keep performance comparisons at
   `--jobs 1` so host contention does not distort results.
6. Continue publishing exact hashes and retaining failed evidence; do not
   collapse partial, pre-context, or OpenGL-initialization results into a
   single pass/fail number.
7. Replace the bounded ATI/OpenGL depth/fog/W heuristic with an additive,
   feature-gated whole-draw homogeneous-coordinate flag emitted by the guest's
   private-geometry choke point. Until then, do not claim compatibility for
   the unobserved case of ATI private OpenGL geometry with depth and fog both
   disabled, all-positive reciprocal-W, and clip-range Z.
