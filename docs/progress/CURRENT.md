# Quake III fixes — 4.1.1 qualification

Task checkout: worktrees/q3-input-cinematic; branch codex/q3-input-cinematic.
Integration main remains 11a4c61. Root gxmetal work is preserved. User confirmed
macOS and authorized fixes and publication of a stable release.

Verified standard USB HID input fixes the unchanged SuperDeluxe v2 Quake III
menu, camera and fire controls. Position-aware ATI cursor detection handles
the driver's offscreen cursor and restores the Finder tablet on exit.

Intro diagnosis is now isolated: WAIT_UNTIL's zero initial idle-clean mask
makes the Mac OpenGL driver's glFinish retry for ~150 ms. In one running
unchanged guest, 0 -> 0x30000 restores ~30 texture uploads/s; writing 0 again
restores ~3/s. Sound/video reset experiments were diagnostic, not the fix.
A Radeon reset/handoff initialization is undergoing fresh-launch testing.

Screamer's upstream paced DMA, continuous sample clock, interrupt separation,
and queued volume fixes pass the synthetic double-buffer timing test. Source
patches are pinned verbatim from RetroBoot 1860881. No guest files are changed.
All runtime tests use private disposable clones of the SHA-verified public
SuperDeluxe v2 fixture; the source remains read-only.

Next: require first-launch and replay cadence/audio plus menu/aim/fire/Finder
return against fresh prepared/package engines; freeze source; run consolidated
checks, native host builds, sign/notarize, and publish stable 4.1.1. Nothing has
been pushed or published yet.
