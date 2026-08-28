# GXMetal ten-game compatibility snapshot

This is the evidence-backed state of the Mac OS 9 ten-game corpus on
2026-08-27. It is intentionally stricter than “the application opened.” A
gameplay pass requires coherent accelerated rendering, live input, sustained
presentation, and no fallback/error evidence. Clean quit/relaunch and audio
are tracked separately.

## Bottom line

GXMetal is now likely to work with more RAVE games than the original test set,
but the evidence still does not support a universal-compatibility claim.

- The current post-2.2.1 source candidate was replayed across all ten installed
  games from one immutable base, with each run using an independent clone,
  host audio disabled, and guest networking disabled. Bugdom, Future Cop,
  Weekend Warrior, Cro-Mag Rally, Dark Vengeance, Myth II, Oni, HAVOC, and
  Unreal Tournament pass reviewed gameplay or lifecycle scopes. Combat Mission
  now also passes its full mounted/disembark/GO turn and a separate
  gameplay-to-Force-Quit recovery/relaunch route. The candidate uses NDRV SHA-256
  `3b687b15…c5722` and Tools CD SHA-256 `ed6ba9cf…d1d`; the signed v2.2.1 QEMU
  and loader remain exact. A separate current-driver Quake III route passes
  five reviewed Q3DM1 arch/statue/passage views, four motion gates, and both
  missing-world rejection regions.
- A post-2.2.2 Carmageddon II replay exposed one narrower public-RAVE
  regression: a valid unflagged nine-vertex Gouraud triangle packet with depth
  enabled and an unused zero reciprocal-W field was misclassified as legacy
  homogeneous OpenGL, permanently faulting the queue and leaving the race
  black. The corrected heuristic keeps unusable reciprocal-W data on the
  public path while retaining explicit protocol-1.25 provenance and the
  mixed-sign old-guest fallback. A fresh-clone recipe mounts the original
  Toast image through Virtual DVD-ROM/CD Utility and reaches a coherent
  640×480 race; its two delayed near-black fractions are both 0.016491 and the
  QEMU log contains no queue or transport fault. The same patched host then
  passes the five-view Quake III regression plus parallel Bugdom, Future Cop,
  Combat Mission, and Weekend Warrior smokes without a queue/transport fault;
  every immutable source digest remains unchanged. The exact signed and
  notarized 2.2.3 Power Mac executable repeats the Carmageddon route at a
  0.015029 near-black fraction for both race gates, again with no fault and an
  unchanged source.
- Bugdom, Future Cop, and Weekend Warrior all pass short semantic routes on
  the exact published 2.2.1 QEMU, loader, Tools CD, and installed guest stack.
  Their movement/action frame-change gates pass at 0.653301, 0.725452, and
  0.798568. Combat Mission also passes launch, menu, and Chance Encounter setup
  rendering on that exact release, but the short stable recipe sends no battle
  input and is not counted as an input pass. Across the four reviewed runs,
  GXMetal presents 33,382 direct frames, zero fallback frames, and 8,709,954
  draws. The deeper beta-3 routes below remain useful for long/full-turn
  coverage and are not substituted for exact-release lifecycle evidence.
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
  2.1.3 candidate. Earlier Oni candidates established how its previously black
  ATI/OpenGL path renders its
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
  corruption, reject, or transport fault. Two later lifecycle candidates added
  explicit CFM-reference release, InputSprocket process ownership, and safe
  retirement of renderer contexts owned by exited processes. Neither changed
  the live result: after the clean first quit, process two renders several
  distinct intro frames, becomes byte-identical at the Bungie logo before the
  focus/input probe, never creates a second GXMetal context, and never reaches
  the menu. The latest combined run records 66 direct profiles, zero fallback,
  monotonic upload IDs 1…210, QEMU exit zero, and an unchanged source base.
  This remains a pre-renderer second-process lifecycle stall rather than a
  proven VNC keyboard or Metal rendering failure. A later tickle-only
  diagnostic suppressed the driver's self-rearming 8 ms timer. Oni still
  reached coherent first-process warehouse gameplay, but InputSprocket never
  invoked either tickle callback and relative-look changed only 0.008250 of
  the qualified region, below the accepted 0.05 gate. The timer is therefore
  required for normal relative-mouse delivery and cannot simply be removed as
  the process-lifecycle fix. The latest matched evidence narrows the stall to
  Oni's movie path: a menu-only first process followed by early second-process
  Escape recreates InputSprocket, resets GXMetal upload IDs, creates a second
  context, and reaches the menu; the full warehouse→clean-quit route instead
  remains static in Bink/level-0 startup with uploads ending at the first
  process's IDs 1…210. Universal InputSprocket construction and renderer
  restart are therefore ruled out. A short launch-arguments qualifier visibly
  proves that Oni accepts literal `-nosound`, but every full-route attempt was
  rejected because relative gameplay warped the guest cursor away from the
  long-lived VNC client's model. The harness then added an explicit motion-only
  pointer-rehome action. The current post-2.2.1 replay closes that lifecycle
  gap: the first process completes warehouse gameplay/input/F1 and clean
  Quit/Yes, literal `-nosound` is visible on the second launch, GXMetal texture
  upload IDs reset, Bink frames remain active, Escape reaches a stable
  four-pixel second-menu signature, and the full 279.334-second route completes
  without fallback or corruption.
- Dark Vengeance and HAVOC exposed the same PPC video-driver gamma-contract
  defect. The old NDRV returned `noErr` from `cscGetGamma` while leaving
  `csGTable` null. Both games dereference the returned `GammaTbl`, read low
  memory as its header, derive the signed allocation size `-49`, and report a
  misleading resource/memory failure. Dark's exact live trace reaches
  `SetupGammaAdjustment__14GGraphicDeviceFv` and its `RBNewHandle` wrapper;
  an independent file trace proves `DARKVENG.INI` opened successfully with its
  correct 2,807-byte cached size, disproving the earlier INI hypothesis.
  HAVOC's exact dynamic `NewHandle` request is likewise `-49` immediately after
  its synchronous `cscGetGamma` query. The rebuilt NDRV now returns a valid,
  persistent, normalized 3x256x8 table, initializes it to the identity ramp,
  and updates it whenever the guest sets gamma. HAVOC consequently reaches a
  coherent first-person cockpit, terrain, pyramid, HUD, and radar; its input
  probe changes 0.603298 of the frame and remains visually intact. Dark also
  clears the `-49` boundary. Its next failure was an exact period-RAVE ABI
  mismatch: Dark asks `QAGetNoticeMethod` for the selector-4 callback while
  passing a null refCon output, but GXMetal incorrectly required both outputs.
  GXMetal now treats those outputs independently and synchronously invokes the
  registered buffer notice from `RenderEnd` with a readback memory device. The
  broader `kQAOptional_BufferComposite` bit remains unadvertised. The final
  silent route renders the Reality Bytes splash, animated Dark title, and
  textured first-person gameplay with a player, enemy, portal, and environment;
  `w` changes the frame and all frames in a 15-second soak differ. The
  intermediate selection/loading capture is mostly black, so this does not yet
  claim a fully drawn interactive menu.
- Unreal Tournament's default startup also remains black on its software
  control. Controlled GXMetal runs isolate `UseSound=True` as the blocker:
  sound-disabled starts render the same correct main menu in windowed and
  fullscreen modes, while a sound-enabled/windowed start submits no RAVE work.
  The final sound-disabled route now advances through the Tempest ready state
  into live Practice gameplay with working movement, turning, firing, damage,
  death, and respawn rendering. On the rebuilt candidate, a focus-corrected
  route again reaches coherent Tempest and proves initial live transition
  (0.289531), forward movement (0.737702), and turn/strafe change (0.901147).
  Its minute-one frame has health 100 and ammo 21 rather than the starting 30.
  A focused discriminator now proves live movement and firing until the player
  dies; the terminal frame visibly says `You are dead. Hit [Fire] to respawn`
  and records one death. It becomes byte-identical only in that death-wait
  state, so the prior “post-minute-one renderer freeze” classification was
  incorrect. Post-death Control, refocus, Escape, and application switching did
  not recover input, leaving a capture/input problem rather than a GXMetal
  presentation failure. The diagnostic records 14,591/14,591 direct frames
  across 61 profiles with zero fallback, writebacks, rejects, out-of-range
  draws, or Metal/queue faults. A later run installed the exact published
  2.2.1 guest stack, used Command-Q after returning to the UT frontend, and
  proved a clean Finder return plus a same-boot second rendering generation.
  The second process reaches a coherent live Tempest HUD and weapon after the
  correct Control-to-ready input, but its subsequent focused Up and Control
  actions do not change the framebuffer. A later process-owner candidate
  re-enumerates GXMetalInput devices after a confirmed dead owner, clears its
  stale timer/active/CFM state, and passes native lifecycle tests, but the real
  second-process live/action/final captures remain byte-identical. Because Up
  and Control are supplied by InputSprocket's keyboard device rather than
  GXMetalInput's mouse-only elements, the remaining gap is broader
  second-process InputSprocket gameplay delivery—not menu indexing, renderer
  restart, or a proved defect in the custom mouse device alone. A persistent
  lifecycle trace now sharpens that result: the first UT process calls Stop,
  deactivates, disposes all five custom elements and its device, and exits.
  Process two has a different PSN and CFM connection, reruns configuration and
  discovery, and successfully creates a fresh device plus five elements.
  QEMU separately records all second-process Control and Up transitions, yet
  the four reviewed live/action frames are byte-identical. Suppressing the
  custom driver's timer does not restore the keyboard path, so neither a
  stale GXMetalInput timer nor lost VNC/QEMU key transport explains this UT
  failure. A fixed-address low-memory discriminator now proves more: the
  16-byte `LMKeyMap` at `0x0174` is all zero at rest in both generations and
  byte 7 becomes `0x08` while Control is held in both, with identical QEMU
  qcode and ADB `0x36`/`0xb6` transitions. Mac OS `GetKeys` backing state is
  therefore correct. The completed read-only probe uses the exact UT PEF
  (`51cc73dc…ae28d`) at `CheckButtonKeys`: current Control at `r1+57`, prior
  Control at `r2-11987`, and the transition call at code offset `0x14fbf0`.
  It relocates the live PEF on its first PC/LR sample, sees Control value
  `0x08` in both `LMKeyMap` and UT's stack-local modifier field while the prior
  value is zero, and stops at the exact transition-emission instruction. After
  the read-only breakpoint is removed, the reviewed frame visibly shows
  firing, reduced ammunition, and shell casings. The earlier static death-state
  frame was therefore not a GXMetal presentation freeze, stale UT
  prior-modifier state, or failed QEMU/ADB/GetKeys delivery. A five-minute bot
  match remains the longer gate.
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
- Protocol 1.25 replaces the final per-vertex ATI/OpenGL coordinate inference
  with negotiated whole-draw provenance. The guest marks transformed private
  callback triangles only when the host advertises support; the host then
  preserves signed reciprocal-W and clip-space Z consistently for textured and
  Gouraud draws. Older guests retain a bounded whole-draw compatibility path,
  while public RAVE depth behavior remains unchanged.
- The exact packaged protocol-1.25 guest and signed QEMU complete the five-view
  Q3DM1 gate. Human review confirms intact spawn and passage arches, walls,
  floor, statues, pedestals, pickups, lighting, and HUD across all five frames.
  Both cropped missing-surface thresholds pass, every camera transition is
  distinct, presentation stays direct, and fallback remains zero.

## Current 2.2.1 through 2.2.3 candidate matrix

| Game | Classification | Furthest qualified state | Important open item |
|---|---|---|---|
| Bugdom | Exact-2.2.1 short gameplay pass; post-release clean lifecycle pass | Published build renders the Lawn coherently and passes movement at 0.653301; 5,723 direct/0 fallback frames and 1,040,070 draws. A 185-second post-release route cleanly quits from the main menu and relaunches to the menu | A collision-aware five-minute movement route and audio |
| Future Cop: LAPD | Exact-2.2.1 short gameplay pass; post-release recovery pass | Published build renders Crime War coherently, passes movement/fire at 0.725452, and changes ammo 7500→7482; 1,784 direct/0 fallback frames and 3,976,505 draws. A 258-second post-release route proves Finder recovery and a second rendered launch | Clean application quit after the promo page; audio |
| Combat Mission | Exact-2.2.1 setup pass; post-release full-turn and recovery passes | The 410-second current-driver route selects Rifle 45 Sqd, plots Move/Disembark, executes GO, and reaches DONE after the visible movement. A separate 242-second route proves the real Mac OS Force Quit dialog, Finder recovery, and a coherent second scenario selector | Clean application quit/relaunch and audio |
| Weekend Warrior | Exact-2.2.1 short gameplay pass; post-release recovery pass | Published build renders Center Stage coherently and passes movement/fire at 0.798568; 24,707 direct/0 fallback frames and 1,517,694 draws. A 131-second post-release title-state recovery returns to Finder and reaches the second launch | Clean in-game quit/relaunch, collision-aware five-minute route, and audio |
| Cro-Mag Rally | Gameplay smoke pass on current Q3 fix | Correct accelerated title/menu/loading and Practice/Desert gameplay at 640x480; acceleration and steering visibly alter position/heading; 24-28 fps, ~204 direct draws/frame, zero fallback; expected demo exit screen | Complete-lap/longer lifecycle soak and audio |
| Dark Vengeance | Gameplay rendering/input/short-soak pass on gamma + notice candidate | Exact tracing proves `cscGetGamma` success-with-null caused `-49`; `DARKVENG.INI` opens correctly. Accepting independently optional `QAGetNoticeMethod` outputs and invoking selector 4 synchronously reaches a textured player/enemy/portal scene, responds to `w`, and remains animated for 15 seconds without advertising `kQAOptional_BufferComposite` | Qualify the mostly-black selection/menu transition, longer gameplay/lifecycle, and audio |
| HAVOC | Gameplay rendering/input pass on gamma candidate | The `cscGetGamma` contract fix clears all prior memory dialogs. The reusable route separately gates first-run help, main screen, and cockpit; steering changes 0.171768, and the coherent terrain/HUD soak has only 0.000221 of the rejected solid-red range | Five-minute gameplay/lifecycle soak and audio |
| Myth II | Battlefield rendering and broad-input pass on post-2.2.1 candidate | Coherent Into the Breach battlefield; unit selection/orders, Stop, ten paired camera direction/zoom/orbit actions, and 30-second soak | Effects-heavy long film, clean exit/relaunch, and audio |
| Oni | Full two-process gameplay/input/lifecycle pass on post-2.2.1 candidate | Warehouse gameplay/input/F1, clean Quit/Yes, exact `-nosound` relaunch, upload-ID reset, active second Bink sequence, Escape, and stable four-pixel second-menu signature | Longer second-process gameplay and audio |
| Unreal Tournament | Two-process rendering/input short-route pass | Process one renders Tempest and cleanly exits; process two renders another map. The exact read-only `CheckButtonKeys` probe observes Control in `LMKeyMap` and UT's stack-local modifier, hits the transition-emission instruction, and the reviewed final frame shows visible fire and lower ammunition | Five-minute bot match and audio; keep sound disabled under the current VM audio path |

## Driver gates on the exact 2.2.1 release

The immutable test base was prepared directly from the signed, notarized 2.2.1
app, with the guest artifacts extracted from its Tools CD and General Controls'
improper-shutdown disk check disabled. The source base was never attached
writable.

The two driver gates ran concurrently from clean per-test clones:

- The 2.2.1 `GXMetal AGL Probe` reported `PASS_ACCELERATED`, renderer ID
  `0x00021000`, vendor `ATI Technologies Inc.`, renderer `Rage 128 OpenGL
  Engine`, and OpenGL `1.1.ATI-5.131585`. Primitive, clipped-texture,
  base/mip/asymmetric sampler, ARB unit-1, alpha-blend, depth, display-readback,
  resource-deletion, and full-teardown checks matched with zero GL/AGL errors.
  The extracted diagnostic trace records 34 ATI-private calls, 17 accepted
  geometry calls, and zero geometry anomalies, input rejects, context
  fallbacks, or texture/bind errors.
- `GXMetal Test` passed the full advertised RAVE contract, including discovery,
  capability negotiation, depth, blending, alpha/chromakey, clipping, texture
  formats, public multitexture, dynamic resources, ATI private no-copy data,
  large mesh batches, bitmap/scaling, dirty presentation, double buffering,
  and framebuffer access. Its extracted trace records 6,000 draws and zero
  geometry anomalies, rejects, context fallbacks, unbound-texture fallbacks,
  or texture/bind errors. Its comparison workload measured 52,579 microseconds
  under GXMetal versus 511,402 under Apple Software RAVE, a 9.72× speedup in
  this isolated VM run.

Accepted evidence:

- `context/gxmetal-games/evidence/gxmetal-2.2.1-driver-smoke-20260827`
- `context/gxmetal-games/evidence/gxmetal-2.2.1-in-guest-conformance-20260827`
- `context/gxmetal-games/evidence/gxmetal-2.2.1-four-game-smoke-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-campaign-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-wave1-four-game-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-wave2-cromag-race-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-wave2-mythii-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-wave2-dark-retry-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-wave2-havoc-final2-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-wave2-oni-relaunch-final-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-wave3-ut-checkbuttonkeys-retry-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-quake3-five-view-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-long-combat-mission-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-combat-recovery-final-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-lifecycle-bugdom-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-lifecycle-future-cop-20260827`
- `context/gxmetal-games/evidence/gxmetal-post221-lifecycle-weekend-warrior-20260827`
- `context/gxmetal-games/evidence/gxmetal-ut-guest-identity-20260827`
- `context/gxmetal-games/evidence/gxmetal-ut-2.2.1-cleanquit-relaunch-corrected-20260827`
- `context/gxmetal-games/evidence/gxmetal-oni-input-lifecycle-candidate-20260827`
- `context/gxmetal-games/evidence/gxmetal-oni-process-owner-v2-candidate-20260827`
- `context/gxmetal-games/evidence/gxmetal-ut-process-owner-v2-candidate-20260827`
- `context/gxmetal-games/evidence/gxmetal-oni-input-tickle-diagnostic-20260827`
- `context/gxmetal-games/evidence/gxmetal-ut-input-tickle-diagnostic-20260827`
- `context/gxmetal-games/evidence/gxmetal-driver-smoke-rc23-20260826`
- `context/gxmetal-games/evidence/gxmetal-oni-fan-topology-rc23-20260826`
- `context/gxmetal-games/evidence/gxmetal-quake3-multiview-current2-20260826`
- `context/gxmetal-games/evidence/gxmetal-quake3-final-upload-order-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-ut-layout-fixed-candidate-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-homogeneous-clip-regression-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-render-owner-reset-candidate-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-five-view-regression-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-myth-depth-fix-five-view-20260827`
- `context/gxmetal-games/evidence/gxmetal-quake3-homogeneous-draw-five-view-20260827`
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

## Published 2.2.1 exact-release identity

The immutable exact-release four-game base SHA-256 is
`a67291f8eff79015aa9e7d845b8f3097423de674c87cb065448f3c319dd6d105`.
It was prepared from the released app and packaged Tools CD while preserving
the disabled improper-shutdown disk check. The source remained unchanged after
the concurrent AGL and RAVE smoke pair.

The reviewed exact-release four-game sweep used that base and records 252
captures across four automation-complete, QEMU-exit-zero runs. Its 110 profiles
total 33,382 direct frames, zero fallback frames, and 8,709,954 draws. Bugdom,
Future Cop, and Weekend Warrior have semantic input evidence; Combat Mission's
short recipe is deliberately limited to setup rendering. The exact-release UT
base is `da6d81f55bdced0a549d3e5b2ceff2639dba23f697cf5c9337eeed6e832451ad`.
Fork-level inspection first disproved the older `43103366…` image as a stable
guest before the published 2.2.1 stack was installed into this replacement.

- Notarized and stapled `ClassicMac.dmg` SHA-256:
  `3ad3a497aefac40c7976a180c2b5805c532f423a905d46005981adfb57e7b94c`
- Stapled `ClassicMac.zip` SHA-256:
  `f36f41ea06549b270184bc76034537b2bde678e58cd1eaf07768c705e864475c`
- Signed Power Mac QEMU SHA-256:
  `0a7db4fcf860497bfcfc2011dd5012bae1ba5d935a53cecac156ad0202589aa1`
- Bundled Tools CD SHA-256:
  `4f8da127cc2d8418269b85cde0914bed9c0efb98b9b911b11742185219b04eef`
- Bundled Power Mac video NDRV SHA-256:
  `c5e63db45de7b58ea286f785e56811209b090673635da81fea07f9871312b27d`
- Bundled NDRV loader SHA-256:
  `a5b441048916fdb58291e34ab029cfd06d7e6da9380b6035bd4d8740f040f27d`
- GXMetal protocol-header SHA-256:
  `346a0ddb317bd4cadfc268a41bd0988614d41a9767c96c32d24d0355c92f84dc`
- Guest GXMetal driver MacBinary SHA-256:
  `53bd3fca2d1a106a42f0cfde887ae1a2f3852935772c7ac2bd674e78cf009327`
- Guest GXMetal driver data-fork SHA-256:
  `4861d6bf1b75b84c9e2d20c0ac9893ca93a5d0babb721f60248a87d24bf9cca3`

Apple accepted application submission
`84d0641c-613e-4e97-826c-baca224bcfeb` and DMG submission
`34faa462-f961-48bf-a49a-f4840241c673`. Both tickets are stapled, and
Gatekeeper accepts the app and disk image as Notarized Developer ID software.
The release defaults to the native Cocoa VM window; VNC remains an optional
display and automation mode. Every exact-release qualification command in this
snapshot explicitly used `-audiodev none,id=snd0`.

## Final signed beta-3 qualification identity (historical)

The final release rebuild uses GXMetal protocol 1.25 and the negotiated
whole-draw coordinate path. Its exact prepared Quake III base SHA-256 is
`e270325b4b3ce05a4cc4b4050717e9488df6ed80039bdff1d7d4569be7de81b3`.
That base was prepared from the prior render-owner-reset base without attaching
the source writable, contains the driver extracted from the packaged Tools CD,
retains the disabled improper-shutdown disk check, and remained unchanged
after the five-view sweep.

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
  `a02479931f290b0cf0dfdb6e21159b318fb0a38ecc84d4a4317eef4ac6a4b59a`
- Packaged Tools CD SHA-256:
  `20a640ae359cb4a31326e6b919f5767da04435c03cbfd26649d7c862f8446cc7`
- Packaged Power Mac video NDRV SHA-256:
  `c5e63db45de7b58ea286f785e56811209b090673635da81fea07f9871312b27d`
- NDRV loader SHA-256:
  `a5b441048916fdb58291e34ab029cfd06d7e6da9380b6035bd4d8740f040f27d`
- GXMetal protocol-header SHA-256:
  `346a0ddb317bd4cadfc268a41bd0988614d41a9767c96c32d24d0355c92f84dc`
- Guest GXMetal driver MacBinary SHA-256:
  `be2d142973f718821c57102d8b667e5224fff5520ffbb4161b3a70af7c4ee5ed`
- Packaged GXMetal PEF data-fork SHA-256:
  `cbf3d8af8be8eefd6b67aa3ac00d736f46dd48953a110b4901000920b1c94194`
- Notarized and stapled `ClassicMac.dmg` SHA-256:
  `90954881b7fe379ee6776baf8af96ba4ca3af63450373e1de36e65762b1e65a5`
- Stapled `ClassicMac.zip` SHA-256:
  `4639fbdd1d62f90b168e33b1fffd8969e2bc1e37c6f03169e24dcd291e42ba30`

Apple accepted application submission
`8b13f9b5-d431-4d48-af6a-d357b9164eb2` and DMG submission
`643ddc2c-0c50-4253-a29a-d187fbc5456e`. Both tickets are stapled, and
Gatekeeper accepts the app and disk image as Notarized Developer ID software.

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
  `context/gxmetal-games/evidence/gxmetal-dark-title-menu-gameplay-final-20260827`
- HAVOC:
  `context/gxmetal-games/evidence/gxmetal-havoc-getgamma-gameplay-input-v3-20260827`
- Reality Bytes static startup trace:
  `context/gxmetal-games/evidence/reality-bytes-static-trace-20260827`
- Myth II:
  `context/gxmetal-games/evidence/gxmetal-mythii-final-upload-order-20260827`
- Oni:
  `context/gxmetal-games/evidence/gxmetal-oni-fullgame-first-second-nosound-rehomed-20260827`
- Unreal Tournament:
  `context/gxmetal-games/evidence/gxmetal-ut-first-second-keymap-20260827`

## Next compatibility gates

1. Preserve Oni's now-passing warehouse input, F1, in-game quit, pale-range,
   method-50 fan, pointer-rehome, literal `-nosound`, active second-cinematic,
   upload-reset, and four-pixel second-menu gates. Extend the second process
   into longer gameplay while retaining the normal 8 ms relative-input polling
   source.
2. Preserve Unreal Tournament's exact-release first-process gameplay,
   Command-Q/Finder return, second rendering reset, and the exact read-only
   `CheckButtonKeys` Control-transition result. Extend process two into a
   dynamic five-minute bot match. Do not classify either a visible death-wait
   state or an unready match as a renderer freeze. Extend Myth II through clean
   film quit/relaunch while retaining its now-passing group orders, Stop,
   camera, zoom/orbit, and effects/terrain regressions.
3. Preserve the shared Dark Vengeance/HAVOC gamma contract and its native table
   tests. Extend Dark beyond the mostly-black selection/loading transition,
   then run a longer gameplay and clean-relaunch gate while retaining the
   selector-4 callback trace and leaving `kQAOptional_BufferComposite`
   unadvertised. Preserve HAVOC's first-run/main/cockpit/steering/color-range
   assertions and add a longer cockpit/lifecycle soak.
4. Require reviewed screenshots, direct presentation profiles with zero
   unexpected fallback, representative input, a longer soak, and clean
   lifecycle for every title advertised as supported.
5. Run correctness recipes concurrently, but keep performance comparisons at
   `--jobs 1` so host contention does not distort results.
6. Continue publishing exact hashes and retaining failed evidence; do not
   collapse partial, pre-context, or OpenGL-initialization results into a
   single pass/fail number.
7. Preserve protocol-1.25's whole-draw provenance with both sides of its
   compatibility contract under test: an old negotiated host must receive
   legacy flags and may coalesce public/private triangles, while a supporting
   host must receive a provenance-flagged private batch separated from public
   RAVE geometry. Retain a Metal framebuffer assertion for mixed-sign
   homogeneous clipping, including byte-identical explicit and old-guest
   fallback output.
