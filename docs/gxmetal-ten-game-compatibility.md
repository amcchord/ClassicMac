# GXMetal ten-game compatibility snapshot

This is the evidence-backed state of the Mac OS 9 ten-game corpus on
2026-08-26. It is intentionally stricter than “the application opened.” A
gameplay pass requires coherent accelerated rendering, live input, sustained
presentation, and no fallback/error evidence. Clean quit/relaunch and audio
are tracked separately.

## Bottom line

GXMetal is now likely to work with more RAVE games than the original test set,
but the evidence still does not support a universal-compatibility claim.

- Bugdom and Future Cop have coherent representative gameplay with verified
  input on the 2.1.4 qualification rc23 candidate.
- Weekend Warrior has a fully scripted route through menus, selection,
  textured 3D play, input, and a short soak.
- Combat Mission reaches and sustains its complete accelerated 3D scenario
  setup on that same candidate; representative battle input remains open.
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
  rc23 through the formerly corrupt +45-second transition. Static GLD audit
  identified private slots 49/50 as center-plus-contiguous-rim triangle fans,
  not independent pointer triangles; expanding each four-vertex call as two
  adjacent-rim triangles removes the giant wedge. The reviewed +45 capture's
  pale-corruption fraction fell from rc21's failing 0.106921 to 0.005498.
  Both shortened Oni routes have zero context fallbacks, rejected inputs,
  rejected triangles, geometry anomalies, or transport errors.
- Dark Vengeance and HAVOC reproduce the same deterministic failures on
  GXMetal and Apple Software, before either app submits accelerated work.
  Unreal Tournament's default startup also remains black on its software
  control. Controlled GXMetal runs isolate `UseSound=True` as the blocker:
  sound-disabled starts render the same correct main menu in windowed and
  fullscreen modes, while a sound-enabled/windowed start submits no RAVE work.
  Myth II's black display transition is avoided by disabling its startup
  resolution switch, but its demo then remains in the mission briefing without
  creating a draw context. These are kept separate from driver faults.

## Current signed-candidate matrix

| Game | Classification | Furthest qualified state | Important open item |
|---|---|---|---|
| Bugdom | Gameplay pass | Correct full-color Lawn rendering; sustained movement/turning; short automated soak; direct frames with zero fallback | Retain the separately proven long soak and quit/relaunch gate; audio |
| Future Cop: LAPD | Gameplay pass | Coherent Crime War world, vehicles, HUD, effects, and live input; direct frames with zero fallback | Clean quit/relaunch and audio |
| Combat Mission | Rendering pass | Complete Chance Encounter 3D setup scene; stable high-draw-count presentation with zero fallback | Camera/GO input, representative battle, lifecycle, and audio |
| Weekend Warrior | Gameplay smoke pass | Fully scripted selection, Center Stage 3D play, movement/turning/action input, and short soak; zero fallback | Longer arena/lifecycle soak and audio |
| Cro-Mag Rally | Signed rendering pass | Correct accelerated title/menu/loading rendering at 640x480; direct frames with zero fallback | Script representative race input and lifecycle |
| Dark Vengeance | Matched app/runtime blocker | Deterministic `Memory allocation failed (size = -49)` in accelerated and software modes | Test alternate media/build or application-memory patch |
| HAVOC | Matched app/runtime blocker | Deterministic resource-allocation failure in accelerated and software modes, including memory controls | Test alternate media/build or application-memory patch |
| Myth II | Pre-3D game-flow blocker | Correct main UI, new-game dialog, and mission briefing remain at 640x480 with “Switch resolutions” disabled; the unchanged briefing survives Return, Space, and click probes with no GXMetal context or draws | Test alternate demo/media or updated game build before further driver work |
| Oni | Gameplay smoke pass on rc23 | Correct ATI/OpenGL main menu, new-game confirmation, loading, and coherent Combat Training through the formerly corrupt +45-second transition; method-50 fan topology corrected with zero rejected geometry or fallback | Longer gameplay/input soak, clean lifecycle, and audio |
| Unreal Tournament | Signed accelerated rendering pass with startup workaround | Sound-disabled windowed and fullscreen starts reach the same correct main menu; the fullscreen control records 19,174 direct frames, zero fallback, and roughly 429–456 fps during its stable menu phase | Script representative match input and clean quit; keep sound disabled under the current VM audio path |

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
  `context/gxmetal-games/evidence/mythii-gameplay-20260825`
- Oni:
  `context/gxmetal-games/evidence/oni-gxmetal-fatal-relaunch-20260825`
- Unreal Tournament:
  `context/gxmetal-games/evidence/unreal-tournament-rave-gameplay-20260825`

## Next compatibility gates

1. Extend the now-passing shortened Oni route into a longer gameplay/input and
   clean-lifecycle soak. Keep the pale RGB-range oracle and method-50 fan
   counters as the fast regression gate.
2. Script Unreal Tournament match gameplay with sound disabled; try alternate
   Myth II demo/media or a later game build for its pre-context briefing stall.
3. Try alternate builds/media for Dark Vengeance and HAVOC rather than
   weakening driver or VM memory safety around deterministic app failures.
4. Require reviewed screenshots, direct presentation profiles with zero
   unexpected fallback, representative input, a longer soak, and clean
   lifecycle for every title advertised as supported.
5. Run correctness recipes concurrently, but keep performance comparisons at
   `--jobs 1` so host contention does not distort results.
6. Continue publishing exact hashes and retaining failed evidence; do not
   collapse partial, pre-context, or OpenGL-initialization results into a
   single pass/fail number.
