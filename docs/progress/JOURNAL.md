# Session journal

## September 2026 — 3.0 kickoff

- Saved the complete numbered polish review and the user's five-item selection
  in `docs/polish-roadmap.md`.
- Created the `codex/3.0-integration` branch from `f763917` and contained
  feature worktrees. Existing uncommitted iPad work is preserved.
- Read the workspace agreement and AustinLand infrastructure guide.
- Validation so far: repository baseline and remotes inspected; implementation
  and runtime qualification remain in progress.
- Production changes: none yet. Next: implement features independently,
  inspect the authorized static hosting target, then combine and validate.

## September 10 — feature integration and clean template

- Integrated compact GXMetal status, explicit reviewed paste text, and the
  resumable/download-verified machine importer from their feature branches.
- Connected native menu requests to machine identities. Fixed shutdown and
  restart to retain settings/media staged during a run; installer auto-handoff
  now preserves current settings and respects an explicitly changed disc.
- Focused validation passed: six graphics status tests, fourteen paste tests,
  sixteen download/import tests, and the disk/startup tests. Full host GXMetal
  `make test` passed, including real Metal execution and 83 Python assertions
  grouped as test cases. Full integrated app validation is still pending.
- The fresh public-template filesystem is assembled from selected Apple
  system/apps and current GXMetal guest files, with source preferences,
  documents, host paths, and source filesystem/free space excluded. Its first
  boot checks failed before Finder; diagnosis remains active, so no unproven
  archive has been published.
- Confirmed existing Apache static hosting under mcchord.net on the authorized
  50DayEdge host, HTTPS/range support, and adequate free storage. No DNS or
  web-server configuration changes are needed. Production writes remain pending.

## September 10 — runtime qualification and first template publication

- All five selected features are integrated. The initial combined Swift suite
  passed 97 tests. Actual native paste reproduced ASCII and accented text
  (`café ÑçÅü`) in the guest. Native and library GXMetal menus correctly showed
  a guest with no graphics commands as waiting, rather than healthy/active.
- Exact signed GXMetal guest conformance passed against a complete OS 9 test
  base and then the clean initialized public template. The initial minimal
  source lacked Apple QuickDraw 3D libraries; those audited Apple graphics
  dependencies were added to the fresh template.
- Fixed a missing alternate HFS+ header in template construction. Completed
  first startup and actual clean shutdown to generate guest defaults; the
  initialized clean image booted with both Tools enabled and removed.
- Quadra live media checks proved a guest-visible floppy mount, persisted
  guest write, and asynchronous safe eject. CD backend changes were verified,
  but Finder did not mount the changed CD in the OS 8.1 fixture. Updated UI
  guidance to explain that a restart may be needed; no live CD mount claim.
- Packaging exposed Homebrew libraries with macOS 26 minimums. Added verified
  exact-version official Sequoia bottles, genuine minimum-version validation,
  dependency closure checks, and bundled provenance to preserve macOS 15
  support. Eight focused failure-boundary tests passed. Final packaging and
  the exact-bundle guest regression rerun remain underway.
- Published the qualified OS 9 archive and catalog under the existing
  mcchord.net document root. Server hash/size, HTTPS, and byte-range checks
  passed. Paths, hash, and rollback instructions are in the hosting record.
  No existing site files, server configuration, or DNS records changed.

## September 10 — 3.0 build and validation complete

- All five selected features are implemented on `codex/3.0-integration`.
  Runtime source ends at `b24244d`; subsequent commits record publication and
  validation. The roadmap preserves its original numbering and now marks the
  five selected items implemented.
- Final integrated Swift suite: 97 tests, zero failures/skips. GXMetal host
  suite: 13 native executables, including actual Metal execution, and 83 Python
  test cases passed. The earlier journal's "assertions grouped as test cases"
  wording refers to these 83 test cases. Both QEMU engines rebuilt with their
  transport checks; packaging/template helper tests and syntax checks passed.
- Actual public HTTPS download, checksum, Swift import, sparse allocation,
  identity/path sanitization, Finder startup, idle/input, clean guest shutdown,
  and full GXMetal 2.3.0 conformance passed with the final bundle. Archived and
  imported disk payloads remained identical. Final media repeat: 18 checks,
  zero failures, including actual OS 8.1 floppy writes and safe acknowledged
  eject; original source hashes were unchanged.
- Final native/browser UI checks verified the live download flow, machine
  home, provenance, settings, media/paste entry points, accented and multiline
  text in SimpleText, cancellation, paused controls, and neutral paused/stopped
  GXMetal status. Disposable UI machines were shut down and removed using
  Remove from Library; evidence files and the three original entries remain.
- Game qualification completed 13 bounded scenarios across 10 games, with
  nonzero direct frames and no fallback, queue, or transport faults. Historical
  HAVOC setup and input-timing assumptions required a controlled saved-hardware
  fixture and settle delay; failed attempts and unchanged assertions remain in
  evidence. Menu-only, existing visual, audio/network, and lifecycle limits
  are explicit in `docs/3.0-validation.md`. No product source changed for the
  final game qualification.
- Apple accepted app and DMG notarization; stapling, Gatekeeper, mounted DMG
  release checks, and genuine macOS 15 library minimums passed. Runtime tests
  used this macOS 26 host; no physical macOS 15 execution claim. Retained
  installer: `artifacts/ClassicMac-3.0.0.dmg`, SHA-256
  `c86e40e20c6643f17d09fce0a7c7158228eb1bb246f5ee98a170ac1ee6ba8978`.
- Production state: the configured OS 9 archive/catalog are live on mcchord.net;
  no further server changes. GitHub main remains `f763917` (2.3.2), and 3.0 has
  not been pushed or released there. The installed 2.3.1 app and pre-existing
  iPad work were preserved. Next: try the notarized candidate and decide on
  publication or the next roadmap items.

## September 10 — 3.0 GitHub publication authorized

- Austin explicitly requested a fully signed/notarized app, committing all
  source to Git/GitHub, and a new 3.0 release. Added the remaining iPad beta
  source/scripts and its existing README/changelog/notices in `46188af`.
  iPad shell syntax, export plist, and patch structure checks passed; no iPad
  binary is being built or uploaded by this macOS release action.
- Rechecked the exact tested macOS artifacts: all six recorded identities
  match, mounted DMG release verification passed, and the app extracted from
  the ZIP passed strict signature, stapled ticket, and Gatekeeper checks.
  Current host reports macOS 27.0 (26A5425a), correcting earlier journal wording
  that called the development host macOS 26. macOS 15 runtime limits remain
  unchanged. There are no new macOS runtime changes requiring a rebuild.
- Added 3.0 README setup instructions, changelog, and release notes. Preflight
  found GitHub main at `f763917`, with no existing `v3.0.0` tag/release. Planned
  publication is a fast-forward of main and integration, an annotated tag,
  draft upload, digest verification, then publication as the latest release.
- Rollback reference: retain `v2.3.2` and its existing artifacts. If a release
  problem is found, withdraw 3.0 from latest status and restore 2.3.2 as latest;
  repair through a new version instead of rewriting published commits/tags or
  replacing immutable release bytes. The mcchord.net template is unchanged.

## September 10 — ClassicMac 3.0.0 published

- Release preparation commit: `2dc6e459d6f58716e839018441d64a0b946096cd`.
  Main and `codex/3.0-integration` were fast-forwarded and pushed, together with
  annotated tag `v3.0.0`, using one atomic push. The release uses the exact
  previously built and tested macOS binaries; no runtime source changed.
- Created a draft with the notarized DMG, stapled-app ZIP, and SHA256SUMS.txt;
  verified all GitHub-reported sizes and SHA-256 digests before publishing as
  the latest public release: <https://github.com/amcchord/ClassicMac/releases/tag/v3.0.0>.
- An unauthenticated latest-release request confirmed v3.0.0 is public and
  stable. Downloaded all three published assets again and verified their
  bytes against the qualified originals. Remote main/integration and the
  dereferenced tag matched the release commit. Evidence and rollback baseline
  are retained under `output/3.0/publication/`.
- DMG: `c86e40e20c6643f17d09fce0a7c7158228eb1bb246f5ee98a170ac1ee6ba8978`.
  ZIP: `fe843a18e3318bc44614d9342cdb4b03494fd00da9888543dbaf9a1d0c401074`.
- The iPad beta source is now committed/pushed; this release distributes
  macOS binaries only. No further changes were made to mcchord.net or the
  existing local Applications installation. This final publication record
  is a documentation-only follow-up; the release tag remains immutable.
- Result: requested release work complete. Next: use 3.0 and select further
  roadmap work as needed. Prior v2.3.2 and its assets remain available.

## September 11 — easier setup and 32 GB sparse image follow-up

- Austin requested the working-image download inside New Machine, highlighted
  as easiest, and sparse new disks with a 32 GB default OS 9 installation.
- Created root branch `codex/3.0.1-onboarding` from clean main `b0828f0`.
  Two existing agents have separate contained branches for template geometry
  and catalog/import storage accounting; root owns wizard integration.
- Inspection confirmed blank raw disks and downloaded raw disks already use
  sparse writes. The existing download space check still reserves the full
  logical size, which needs a separate, enforced initial-allocation budget.
- The next image needs a real 32 GB partition and filesystem, rather than
  simply increasing the file length. Existing machines will not be resized.
- Verification and publication are pending. Published 3.0.0 and the current
  8 GB archive remain unchanged while the follow-up is prepared.

## September 11 — sparse 32 GB wizard follow-up complete

- Integrated recommended download/manual setup in one New Machine sheet,
  a 32 GB new-Power-Mac default, sparse catalog/import accounting, and a fresh
  32 GiB OS 9 filesystem. Root branch: `codex/3.0.1-onboarding`; app runtime
  source ends at `fb8d629`. Existing machines were not resized.
- Agent work was reviewed and integrated: storage `f0ff6f5`/`6cc1b9a`,
  template builder `eb30967`. APFS close-time allocation required explicit
  hole punching; a regression protects zero sector tails. Review also caught
  unowned temporary-file cleanup in the template builder, which is fixed.
- Final Swift suite passed 102 tests, plus three packaging and three template
  helper tests. Actual public wizard download/import, exact disk hash, native
  Finder startup, and app-requested clean shutdown passed. Both HFS+ headers
  confirm clean unmount. The template also passed Tools-on/off boot/input and
  full GXMetal conformance with the released engine/resources.
- A vectorized empty-buffer comparison reduced the same local import-plus-
  hash check from 124.20 to 30.63 seconds. The imported 32 GiB disk uses
  124,780,544 host bytes, with 31.99 GB guest capacity and about 31.28 GB free.
- Published the immutable `mac-os-9.2.1-gxmetal-2.3.0-32gb-v2.tar.gz` on the
  already-authorized mcchord.net host and promoted its catalog atomically.
  Archive: 82,844,731 bytes; SHA-256
  `bc0290edf6e0eccba26738d14723d76d9b3acd9018463ce0c427f9152f77959d`.
  HTTPS, HTTP 206, server checksum/size, catalog comparison, and app retrieval
  passed. The v1 archive and private catalog backup are retained for rollback;
  no DNS, Apache, or other site changes occurred.
- Final 3.0.1 app/DMG are signed, Apple-notarized, and stapled. Gatekeeper and
  mounted-DMG verification passed. Retained installer:
  `artifacts/ClassicMac-3.0.1.dmg`, SHA-256
  `a5dc24e43277bfa023ca925fbc4d6bede1d2d3cd4a155e31bdb2254ee209bd1c`.
  Details and limits are in `docs/3.0.1-validation.md`; evidence is under
  `output/3.0.1/`.
- All test emulators stopped and temporary library entries were removed;
  evidence machines remain on disk. Original entries and the Applications
  installation are preserved. GitHub 3.0.0 remains the public release; no
  GitHub push, tag, or new release was made in this follow-up.
- Result: requested wizard and sparse-image work is complete. Next: use the
  signed 3.0.1 candidate; publish it as a new release if requested, without
  changing 3.0.0 assets or tag.

## September 11 — 3.0.1 source pushed to GitHub

- Austin explicitly requested pushing all completed changes to GitHub.
  Preflight found the root and relevant feature worktrees clean, and remote
  main at `b0828f0` with no conflicting commits. All agent changes were already
  integrated in the seven reviewed local follow-up commits.
- Fast-forwarded local main and integration to `f5a7960`, then atomically
  pushed main, integration, and `codex/3.0.1-onboarding` to
  `https://github.com/amcchord/ClassicMac`. A fresh remote-ref query confirmed
  all three branches at `f5a7960af30ce8dc8b7fc03bafc990cef4b1b141`.
- This documentation-only synchronization record follows the source push.
  No runtime code changed; the existing 102-test and signed-build validation
  remains applicable, so tests were not repeated for pushing the same commits.
  The signed installer and template evidence remain local artifacts, with
  the configured OS 9 image already hosted on mcchord.net.
- No new release or tag was created, and no further hosting changes occurred.
  GitHub 3.0.0 remains the public release. Next: use the 3.0.1 candidate or
  publish it as a new release when requested.

## September 21 — Copland native engine and release qualification

- Created contained `worktrees/copland` from GitHub main, preserving the root
  iPad branch. Implemented a Power Mac 7500 family with a pinned DingusPPC engine,
  native lifecycle controls/previews, verified downloadable firmware and disk,
  and manual debugger Continue for Copland assertions. Details: [Copland record](COPLAND.md).
- Native startup required the browser port's bounded sound DMA cadence; host
  sound is deliberately unavailable. Upstream's eleven device fixes are credited.
- 107 Swift tests passed (two opt-in asset tests skipped in the ordinary suite);
  actual public-image import and signed-helper lifecycle tests passed separately,
  including two manual Continue operations after hard restart. Three packaging,
  eight release-library, and native serial tests passed. Exported corresponding
  source rebuilt successfully. A locked host prevented final manual window QA;
  real guest input was exercised through the emulator hardware/control API.
- Source pushed in PR #20. The immutable Copland archive is on mcchord.net and
  verified by a fresh download; catalog promotion and final release publication
  follow artifact notarization and mounted-DMG validation. Prior release assets
  and OS 9 downloads are retained for rollback.

## September 21 — Copland 3.2.0 published

- PR #20 merged as `4ac4d62`; tag `v3.2.0` and the public GitHub release now
  provide the notarized/stapled DMG and ZIP, exact engine source and checksums.
- Final notary submissions: app `7dca76c4-dce4-4f55-8668-c694f3275191`,
  DMG `6fda5760-ef90-40d4-96d8-fb721b7945b5`, both Accepted. A fresh GitHub DMG
  download matched its checksum and passed the complete mounted-release check.
  GitHub's asset digests match all three locally recorded artifact hashes.
- Catalog promotion verified old/new hashes, saved the 0600 pre-Copland backup,
  and atomically appended Copland while retaining the exact OS 9 entry. Public
  retrieval matches; production import using the public combined catalog passes.
- Runtime tests and source rebuild passed; limitations remain documented in
  `docs/3.2.0-validation.md`. All task-owned emulators are stopped. Root checkout
  remains unchanged. Task complete; no release action remains.

## September 21 — Copland Activities qualification

- Continued in `codex/copland-apps` from published main `d0d0ef3`, preserving the
  root iPad checkout. Inspected WinWorld D7E1 and B5 contents: D7 has different
  Appearance/MacBrowser pieces but its control panel requires an incompatible
  FileSystems fragment; B5 appears to be a 1997 conventional Mac OS 8 installer.
  No more complete working Copland system was established.
- Added Anarcho 1.6, MineSweeper, a notebook and original reading/experiment
  guides; surfaced the existing Solitaire/GX demos on a new 512 MiB disk.
  Preserved both forks, original catalog IDs/blessings and author documentation.
  Full developer manuals and unsuccessful app experiments remain local only.
- Fixed empty clipboard writes with a narrowly checked guest PEF patch; normal
  positive transfers and negative error handling remain. Persisted emulated RTC
  state and migrated old disks from actual catalog dates. Released startup Caps
  Lock on first real input after timer-based release selected the wrong OS.
- 110 Swift tests passed; production import and signed-helper manager lifecycle
  passed separately, with zero Continue operations across restart. Final text
  save/reopen after restart and Solitaire card movement passed. Native tests,
  source rebuild and mounted/notarized DMG checks passed. Details and limitations
  are recorded in `docs/3.2.1-validation.md`; all evidence is under `output/3.2.1`.
- Uploaded immutable Activities candidates v2 and v3 after server hash/size
  verification. v3 corrects two guide sentences; it is the intended catalog
  artifact. Fresh HTTPS downloads match and support HTTP 206. Current catalog
  remains v1 and the OS 9 entry is unchanged. GitHub publication and catalog
  promotion follow final public-archive qualification. Existing user machines,
  Apache and DNS are unchanged.

## September 21 — Copland Activities 3.2.1 published

- PR #21 merged as `cf39faf566b0325e036acaed45ff365a9f9dfd25` from runtime commit
  `f3627cd`; tag v3.2.1 and the public GitHub release contain the notarized DMG,
  ZIP, corresponding source and checksums. GitHub's asset digests match local
  evidence. A fresh release DMG download passed the full mounted-release check.
- Final app notary `b8993ccb-19a5-4b4c-900e-b59a7f8ccbc6` and DMG notary
  `0d8d5f92-d8f4-45e0-a726-0163ef3ebb55` are Accepted, with valid staples.
- The final v3 disk's fresh public download passed strict production import and
  signed-helper lifecycle, including zero debugger continuations after restart.
  Card movement, mine reveal/flag/new game, graphics-slide navigation and native
  GX app quit were verified; text saved before restart reopened intact.
- Promoted the catalog atomically after release publication, preserving OS 9
  exactly and retaining prior archives. Final catalog SHA-256 is
  `eaaa1b3a4bd9bd44f14765f498cd900e160fbd10af3ce86c6139e91cce50949d`.
  The verified 0600 rollback backup is recorded in the hosting runbook. Fresh
  public catalog retrieval and production import passed. No DNS/Apache changes.
- All task-owned emulators are stopped. User library and root iPad checkout are
  unchanged. Follow-up complete; further OS/control-panel compatibility remains
  experimental, with D7 full boot not yet qualified.

## October 17 — ClassicMac 4.0 Preview 1 (ATI Radeon, Library, Platinum)

- Scope: port RetroBoot's ATI Radeon R100 and drop GXMetal, bring over the
  Library of OS images, and rebuild the UI in Apple Platinum. Two worktrees:
  `claude/4.0-engine` (engine agent: GXMetal removal `0004bcd`, ATI with a
  CGL/OpenGL 4.1 back end and OpenBIOS node `273ebc7`, script overrides
  `5f2c378`, ClassicMac Display `c5701c9`, faster PPC `74275dd`, harness
  `90a7e4c`, report `ab0d314`) and `claude/4.0-platinum` (PlatinumKit
  `f813b2d`, Platinum app/Library/graphics card `8c946a2`, tests `d5b3d4d`,
  iPad build `f9b09a1`). Integrated on `claude/4.0` (`6b29ac7` onward): ATI
  launch arguments and 3D status `e143884`, OpenBIOS sound-objects fix
  `88e9c39` (firmware `f1cd3599…`, built under a temporary Rosetta colima
  profile that was deleted; the 3.x firmware still reproduces), Iconfactory
  support `4c220c3`.
- The user asked to use The Iconfactory's freeware icons. Their license is
  personal desktop use only, no redistribution, so ClassicMac reads the
  person's own downloaded collections from Application Support and keeps
  its MIT icons as the default; nothing Iconfactory is committed.
- Verification: 117 Swift tests; build gates incl. host GL; Deluxe v6 ATI
  boots (harness ×2 and bundled-app smoke test with the Cocoa window, 3D on
  Apple M5 Max, clean app Shut Down); engine evidence in
  `worktrees/4.0-engine/output/4.0-engine/` and `docs/engine-4.0-ati.md`.
- Release: 4.0.0 Preview 1 bundled, notarized (app `31625b34…`, DMG
  `b208a142…`), stapled, `verify-release.sh` passed; DMG SHA-256 `afb6b04a…`.
  Not published. No production, catalog or GitHub changes; the user's
  library and machines were not touched (UI runs used a scratch home).
- Next: user testing of the Preview DMG, then the CPU-commit and publication
  decisions.

## October 17 — ClassicMac 4.0 Preview 2 (Platinum guest window)

- The user found QEMU's Cocoa guest window (black at boot, OpenBIOS yellow)
  unacceptable and asked for a Platinum window with a lower toolbar.
- `claude/4.0-display` (display agent): `-display classicmac`, RetroBoot's
  presenter backend ported to macOS (stream socket, mmap'd shared buffer,
  damage tiles, CURSOR/MOUSE) `c00bbc4`, ARGB hardware-cursor fix `efa8f35`.
  Yellow came only from unpatched firmware; the staged firmware is black on
  both cards. Merged as `335cc9e`.
- `claude/4.0`: GuestDisplaySession/GuestScreenView/GuestWindows and the
  startup curtain `8f55c43`, safe shutdown on Quit `05fe98b`, window-size
  re-assertion and display refresh `1690b06`, dither margins `485a447`;
  `verify-release.sh` requires `classicmac` in 4.x.
- Preview 2 bundled, notarized (app `c39a82ca…`, DMG `c50c13be…`), stapled,
  verified; DMG SHA-256 `80eabae0…`. Smoke test of the notarized app:
  Deluxe v6 ATI Finder in the Platinum window, clean shutdown.
- A parallel VIA-timer session's stress soak briefly saturated the host
  (36 busy loops); it finished on its own.
- No pushes, publication or catalog changes; user machines untouched.

## October 18 — startup-race fixes merged; 4.0 to main

- Merged `claude/4.0-via-timers` (VIA timer-2 calibration fixes, `d199dcd`)
  and the stacked Cuda sync / IDE soft-reset fixes (`392cf73`, branch
  `claude/charming-blackburn-59de84`) into `claude/4.0` (`a6d171c`;
  `build-qemu.sh` conflicts resolved by keeping both display and timer
  sections). All build gates pass, including the new VIA, Cuda and IDE
  gates; both Macs boot to the Finder through the app's Platinum window.
- The fix sessions' soaks: 12/12 responsive Finders each for Deluxe ATI,
  9.2.1 VGA and Deluxe ATI under 36 busy host processes.
- Screen snapshots were blank because the display slept (display link
  paused); guest screens now draw their latest frame when visible again.
- Preview 3 bundle signed; notarization waits for an unlocked session
  (notary keychain profile unreadable while locked).
- At the user's request `claude/4.0` was pushed and merged into `main`
  through a PR; local `main` fast-forwarded. No release published.

## October 18 — 4.0 Preview 3 published as a pre-release

- PR #22 merged into `main` as `6dab4a6` at the user's request; root `main`
  fast-forwarded (an untracked, ignored-era `gxmetal/` build folder remains
  locally and was left alone).
- After the user unlocked the Mac, Preview 3 was notarized (app `4926df73…`,
  DMG `72576009…`), stapled and verified; the notarized app booted Deluxe v6
  on the ATI card to the Finder in its Platinum window and shut down cleanly.
- Published GitHub pre-release `v4.0.0-preview.3` (DMG, ZIP, Copland source,
  SHA256SUMS). Fresh download: checksum OK, staple valid, Gatekeeper
  accepted. v3.2.1 stays the latest release; catalog unchanged.


## October 18 — stable 4.0.0 artwork and packaging

- User confirmed Iconfactory permission and explicitly authorized a stable
  release, then a Windows version. Created `codex/4.0-stable-release` under
  `worktrees/4.0-stable-release`, preserving root untracked gxmetal files.
- Extracted unmodified icns resources from read-only Downloads DMGs; bundled
  G4 Side List View, 800/8000 and 7200+series/G3 Desktop, original attribution,
  licenses/readmes and hashes. Built-ins are default; imports still override.
- 118 Swift tests pass with three fixture-dependent skips; packaged signature,
  runtime, artwork, ATI and display checks pass. Disposable Deluxe ATI boot
  reaches Finder and displays included icons. Evidence `output/4.0.0`.
- Stable notarization/publication pending existing notary keychain access;
  user asked to unlock the session/profile. No release/catalog changes yet.

## October 18 — Platinum screenshot audit

- User requested pixel accuracy before wrapping the release. Added a Retro68
  guest reference utility calling native Appearance Manager; collected raw
  OS9.2.1 control captures and ten original OS8.1 archive screenshots with
  source hashes. All VM work used disposable disk copies.
- `codex/platinum-pixel-audit` corrects system ASCII glyphs/advances, raw gray
  levels, button/default/checkbox/radio states, paired popup arrows, title-box
  gradient/widgets, determinate progress and frame details. Window titles now
  draw through non-antialiased Core Text.
- Added zero-tolerance image comparisons for 32 states/patterns at both 1×/2×
  and a 95-glyph atlas/advance test. Whole-app fidelity is not certified;
  menus, scrollbars, other fonts, SwiftUI text and window states need more
  coverage. See the audit report and reference lab.
- Stable release not yet published. Release API credentials verified without
  exposing or persisting them; the earlier keychain question is obsolete.
  Windows native build reached packaging, then the real desktop smoke found
  missing DES cipher support for authenticated VNC; libgcrypt rebuild running.

## October 18 — stable 4.0.0 shipped; native Windows prototype passes

- Merged artwork PR #23 (`7960abd`) and screenshot audit PR #25 (`cee04d0`).
  Final release source/tag is `cee04d02994314e5345d2f1c6608e4721f580687`.
  Consolidated integration suite: 122 tests, two fixture-dependent skips,
  zero failures. Signed package, macOS 15 runtime, ATI/OpenGL and native
  display reconnect gates pass. Final disposable ATI/Standard Macs boot to
  Finder with the included icons and finish normal shutdown.
- App notary `1998d59e-e3e3-4983-8ae5-30ef3bd23b12` and DMG notary
  `28c34264-d0df-4883-84a1-14a0f2a5d61f` accepted; both stapled. Published
  v4.0.0 as stable Latest with DMG, ZIP, Copland corresponding source and
  SHA256SUMS. Fresh public DMG checksum/staple/Gatekeeper/package checks pass.
  DMG SHA256 `c367d659dc5d412c31550a97d3831426dc33b51a7ecea2aff055dbbe376e18eb`.
  Previous stable/Preview 3 remain available for rollback; catalog unchanged.
- Native Windows CI 39143255474 at `3b2d435` passes the packaged-desktop
  boot/display/pause/resume/reset/reconnect/clean-shutdown smoke. Reviewed the
  Windows screenshot: OS9 Finder is visible. Explicit libgcrypt fixes VNC
  authentication; reset-exit handling fixes the Windows lifecycle race.
  Six JS tests pass; native QEMU devices/optional WGL realization pass.
- Windows unsigned prototype ZIP retained at `output/windows/2026-10-18/`,
  SHA256 `ae7b408dbd47472b0e11409a2fed87077e76887d65c13fb61b9a1e4619980db7`.
  PR #24 remains draft: Copland/catalog/paste/sharing and hardware/audio/game
  qualification remain. No Windows release was published. Pixel-perfect
  certification is explicitly limited to the covered Mac components.
- Release API credentials existed only in a protected temporary file, removed
  after notarization. Task-mounted source DMGs ejected. User machines and
  unrelated gxmetal files untouched. Worktrees/evidence preserved.

## October 18 — 4.1.0 cross-platform work authorized

- User asked to use AustinLand/Proxmox for feature parity testing, add Linux,
  update GitHub screenshots and publish all results as a new release.
- Reused the contained Windows worktree/PR #24. Renamed the reusable frontend
  to desktop/, added native Linux QEMU/Electron tar.gz/deb packaging, and
  started native Linux/Windows CI. Linux compiled both engines and realized
  EGL/GLES 3.2 with Mesa; a Mac-specific renderer-string assertion needs a
  Linux API expectation. Windows full build runs.
- Added the Library downloader (range validation, SHA256, bounded flat-tar
  extraction and safe import), US/MacRoman paste, Linux 9p sharing and missing
  media/settings controls. 12 focused tests pass; real-VM tests still pending.
- Created dedicated Proxmox test VMs 170 (Debian13) and 171 (Win11) on SazProx
  via AustinLand. Windows creation returned 500 for a start warning despite
  the VM running; inspected before retrying, completed post-setup through
  the existing AustinLand helper, avoiding a duplicate VM. Secrets are never
  logged or committed. Unrelated VMs/templates and user machines untouched.

## October 18 — 4.1 real-host qualification and restart correction

- Dedicated AustinLand/Proxmox Debian13 and Windows11 hosts imported the real
  current 32GB Library archive with interrupted/resumed download, exact size,
  SHA256 and bounded extraction. Final native OS9 input probe shows ASCII and
  accented MacRoman text; Linux reads a host file and writes a verified reply.
  Both hosts boot Quadra/MacOS8.1 CD and complete normal shutdown.
- Real tests found QEMU's System32/en-US keyboard-map collision, early QMP
  commands during restart, and hard resets leaving HFS dirty. Fixed runtime
  CWD and readiness; desktop Restart now completes guest shutdown before
  relaunch. Final runs require actual menu-title pixels and finish without
  dismissing a disk-check dialog. Strict CI caught an empty startup menu being
  misclassified; its detector now excludes borders, Apple icon and clock.
- Mac 4.1 metadata/notices build signed/notarized; app notary
  19512f97-9c95-425a-8f82-8e24ea2d6bda and DMG notary
  70419b9f-a438-4773-b705-42dd68036c67 accepted. Package, Gatekeeper, ATI and
  native display reconnect gates pass. Refreshed direct Mac screenshots.
- Final packaged CI and integration/publication pending. No new stable tag
  published in this entry. VM secrets remain in protected ignored output;
  no unrelated VM, template or user machine changed.

## October 18 — 4.1.0 stable release published and verified

- Merged PR #24 as a6922db and tagged that exact source v4.1.0. Main-checkout
  consolidation passes 122 Swift tests (two Copland fixture skips) and all
  12 Node tests. Native Windows 39158984879 and Linux 39158930105 pass strict
  Finder/clean-restart/shutdown package gates; captures reviewed.
- The exact CI Windows ZIP and Linux DEB were installed/run again on the
  dedicated Proxmox hosts. Final extended checks pass, including native
  accented input and Linux host-read/guest-write 9p. Reports retained in
  docs/evidence/4.1.0; fresh direct screenshots are on main.
- Published GitHub v4.1.0 as stable Latest with all three host platforms,
  corresponding QEMU/Copland source and SHA256SUMS (ten assets). Fresh public
  downloads match all nine archive hashes. The public Mac DMG again passes
  staple/Gatekeeper/release verification. SHA256
  3d2b266b66d57c0b467c788e320cf2d0248a335c5927c56fac8ba3fe9eacef8c.
- Windows test auto-login disabled and its registry password removed. QA
  host shutdown requested; VMs retained, not deleted. Notary API key existed
  only in a protected temporary file and was removed after use. Prior release
  assets and Library catalog remain unchanged; no unrelated systems touched.
- Remaining measured limits: unsigned Windows, Mac-only Copland, no Windows
  9p backend, physical GPU/audio/game and Internet tests outside VM proof;
  whole-app/CSS pixel fidelity remains outside the component audit. No pending
  release action. See docs/4.1.0-validation.md for source, gates and rollback.


## 2026-10-19 — Quake III input and cinematic fixes, release qualification

Task branch `codex/q3-input-cinematic`, PR #28, implementation `2d01ad2`,
Windows source-line-ending/test follow-ups `b240a16`, `892b1d0`. User confirmed
macOS and authorized a new stable release. Baseline is main `11a4c61` and stable
v4.1.0. Root's unrelated gxmetal work and the user's running guest are preserved.

A verified fresh public SuperDeluxe v2 fixture reproduced missing mouse input.
Standard USB HID fixes InputSprocket without guest changes. ATI's driver parks
its enabled cursor offscreen, so routing includes cursor position and restores
Finder's tablet on exit. Native trackpad motion preserves fractional deltas;
Mac/browser/desktop game buttons bypass Finder helpers and capture is exposed.

The intro's ~3 fps was isolated to the Mac OpenGL glFinish retry loop, not host
GPU throughput. GDB-timed finishes take ~150 ms with WAIT_UNTIL zero. Changing
only its idle-clean mask to 0x30000 restores 30 uploads/s in the same guest;
clearing it restores the slowdown. Radeon reset/handoff now initializes that
mask. Sound restart was a confounded diagnostic because it reinitializes video.
No guest code or preferences are patched. Unsuccessful CPU nap, input heartbeat,
Cuda, sample-clock-only and WAIT-zero experiments were removed.

Pinned upstream Screamer patches separately correct codec volume order, paced
DMA/continuous clock, buffer lead and interrupt separation. Actual-device
synthetic checks pass: clock-paused halves remain pending, stamps differ by
512 frames, half completions are separate, sample rate stays within 0.5%, and
unserviced interrupts apply bounded backpressure. Independent QEMU preparation
and native display/GL gates pass. Signed Mac package passes fresh first intro
(203 uploads/8s), replay (183/6s), menu click, camera, fire and seamless Finder
return; WAV qualification has 481 active 10 ms soundtrack bins. Detailed local
artifacts stay under ignored output/4.1.1; guest media/audio are not release assets.

Packaging/notarization and native Windows/Linux CI are in progress. First
Windows attempt failed before compilation because copied Screamer sources had
CRLF; .gitattributes now preserves their LF bytes. Dedicated existing Proxmox
QA VMs were started for package tests; Windows QA auto-login must be removed
afterward. No release/tag has been published. Next: finish packages/QA, merge,
consolidated root checks, publish and verify stable 4.1.1. Retain 4.1.0 for rollback.

## 2026-10-19 — 4.1.1 stable release published and verified

- Merged Quake III fixes in PR #28 (`daf8c31`) and desktop capture/focus follow-up
  PR #29. Stable tag `v4.1.1` points to source
  `dfdf4c1ad2b7206b385c1b787d72aa9af5385c2b`. Final native packaged CI used
  `ff49110` with the identical source tree
  `8ed4cfc21d1aefef83bc78b6a5a66350dc2817d3`.
- Root integration validation passes 125 Swift tests (two optional Copland
  fixture skips) and 12 desktop tests. Capture focus passes syntax and desktop
  checks after integration. Native Linux 39247028650 and Windows 39247576540
  both pass packaged CI; exact final DEB/ZIP also pass dedicated Proxmox
  create/import/config, boot/display, pause/resume, restart/new-engine/reconnect,
  native paste and normal shutdown. Linux additionally passes 9p host read and
  guest write. Source freshness and engine hashes match the final archives.
- Signed Mac package passes fresh SuperDeluxe first launch (203 uploads/8 s),
  replay (183/6 s), menu motion/click, camera/fire and Finder restoration.
  Separate soundtrack WAV and actual-device synthetic Screamer DMA checks
  pass. Input was simulated through the native protocol; physical trackpad and
  speaker recording were not exercised. No guest disk/software changes ship.
- App notary `ad6f8966-8246-475d-a34d-55592f0bd221` and DMG notary
  `13701bca-7b81-46b6-b7a8-3fea5e7dc1c8` accepted. Signatures, staples,
  Gatekeeper, macOS 15 library deployment targets and full package verification
  pass. Protected temporary notarization credentials were removed after use.
- Published stable Latest v4.1.1 with all ten assets. Fresh anonymous downloads
  match all nine archive SHA256 values; public API confirms neither draft nor
  prerelease. DMG SHA256:
  `55149b6a260762596a338386a832d3726e4f0a14c3782275a5e392ecdad1404b`.
  Public DMG staple and Gatekeeper checks pass. Reports, source links, exact
  manifest and cleanup state are in `docs/4.1.1-validation.md` and
  `docs/evidence/4.1.1/`.
- QA VMs 170/171 are verified stopped and retained. Windows auto-login disabled,
  stored registry password removed, scheduled test task unregistered. Read-only
  SuperDeluxe source mount detached. User gxmetal work, running guest and
  unrelated worktrees preserved. No catalog, DNS or Apache changes.
- Stable v4.1.0 assets remain for rollback; promote it with
  `gh release edit v4.1.0 --latest` and record the reason in 4.1.1 notes.
  No pending release action. Retain task worktree and ignored qualification
  output for support; physical Windows/Linux GPU/audio testing remains outside
  VM qualification, Windows remains unsigned and Copland remains Mac-only.

## 2026-10-19 — native Mac OS 9 icons and VM frame qualification

- User authorized original system icons, running-VM Platinum fidelity, merge
  and a new stable release; then requested Mac OS 9 Icon Services/alpha.
  Contained branch codex/native-platinum-icons starts at ecdda03. Root gxmetal
  work, user machines and other worktrees are preserved.
- Native Icon Services in a disposable public OS9 fixture exports the original
  small/large families through GetIconRef/IconRefToIconFamily. This supplies
  32-bit colour and 8-bit alpha absent from flat System resources. Added
  original resources for remaining artwork. Source/family/member hashes and
  copyright distinguish Apple assets from MIT drawings and Iconfactory art.
  32 strikes export deterministically; independent ICNS decode/alpha checks pass.
- Extended the guest Appearance Manager lab to capture whole document frames,
  native small bevel buttons and held widgets. Corrected top/content offset,
  stripe/box positions, shadow ends, inactive palette, held feedback and the
  diagonal grow corner, including its notch and visibility above guest content.
  New full-frame/control comparisons pass at 1×/2× with zero differing pixels;
  original component/font gates still pass. Owning-window/application focus
  controls active state. Narrow VM controls use a compact icon layout.
- Signed Mac package boots a private SuperDeluxe clone to the Finder with
  native toolbar/Media icons. Native Windows/Linux builds run; Linux packaged
  CI 39263262485 passes. Mac notarization and additional narrow/WindowShade
  screenshots are in progress. No stable release/tag is published yet; a
  hidden draft stages unchanged matching engine sources. 4.1.1 is retained
  for rollback. No production catalog, DNS or Apache changes.
- The diagnostic reference guest was normally shut down; source mounts are
  detached. Temporary notarization credentials are obtained from AustinLand
  into a mode-600 temporary file and removed by the wrapper on completion.

## 2026-10-19 — 4.2 package and layout gates complete

- Final desktop source 2e86a5c; native Windows 39267099712 and Linux 39267103833
  pass packaged boot/display, pause/resume, restart and clean shutdown. New
  geometry checks require one-line labels, horizontal fit and toolbar visibility.
  They caught Windows clamping the window to a small host work area; reserving
  preview space and wrapping whole controls fixes the actual captured layout.
- Every packaged source/UI asset (54 per platform) matches the branch; native
  icon attribution/provenance and qualified Tools CD match too. Refreshed both
  desktop screenshots. Mac source tree 1ef81587d51e8beebac6ff06f9600e221b3feba6
  is unchanged after signing/notarization and real SuperDeluxe/WindowShade checks.
- Private draft 417348832 stages qualified assets. No public tag/release yet.
  Next: merge PR #31, consolidated integration tests, tag/publish stable Latest,
  fresh anonymous download verification and final release evidence.

## 2026-10-19 — 4.2.0 stable release published and verified

- Merged PR #31 as 9ca105f583ba9e220f4795790032901da56f5c14 and tagged that
  exact source v4.2.0. Consolidated root validation passes 129 Swift tests
  (two optional Copland fixture skips) and all 12 desktop tests; 32 native
  icon strike exports/hash checks pass. Mac source tree matches its qualified
  signed build; desktop source tree matches final native CI 2e86a5c.
- Final Windows 39267099712 / Linux 39267103833 pass packaged boot/display,
  restart/clean shutdown and all toolbar geometry assertions. Packaged source
  and assets match; fresh screenshots and exact checksums are on main.
- Published stable Latest v4.2.0 with ten assets. Every archive was freshly
  downloaded anonymously and matches the manifest; the public API confirms
  neither draft nor prerelease. Public DMG staple/Gatekeeper checks pass.
  DMG SHA256 2e59561c8ee5929207f5ae76006c8a13341b2baa318ba2dc0cc649ae9e2929b5.
- Task guests normally shut down, task mounts detached, mode-600 temporary
  notarization key removed. No user machine, gxmetal, unrelated worktree,
  Proxmox host, catalog, DNS or Apache changes. Worktree/evidence retained.
- Stable v4.1.1 assets remain for rollback by promotion to Latest; record the
  reason in 4.2 notes if used. No pending release work. The documented wider
  font/layout/desktop fidelity limits remain outside the measured frame gate.

## 2026-10-22 — first-run wizard and 4.3.0 qualification

- User requested a simple first-install-to-first-boot wizard, license
  responsibility notice, own-OS/pre-installed routes and release as 4.3.0.
  Contained branch codex/4.3-setup-wizard starts at 262f663; PR #33.
- Added a four-step native Mac/shared desktop flow, recommended hardware,
  copied installation media, current compatible editions, inline resumable
  progress and a ready screen retained across machine registration. Native
  Finder/File opens during setup also reach ready. New Deluxe catalog
  revisions keep ATI defaults while using current catalog descriptions.
- Disposable signed-Mac tests pass real clean Library download, verified
  import, ready, first Finder boot and normal shutdown; own-CD route copies
  a real OS9 installer, creates a sparse disk and boots the CD desktop, then
  shuts down normally. Guest OS installation itself is outside this check.
  130 Swift tests pass (three local engine/fixture skips), 15 desktop tests
  pass with the bundled image tool, and browser setup scenarios pass.
- Native packaged CI now exercises both first-run routes through actual
  renderer/IPC, real download/pause/resume/first boot/shutdown and disk/CD
  creation. Runs are in progress. Mac app uses public 4.2.0 engine code,
  independently matched without code signatures; notarization is in progress.
  No new public tag/release yet. Next: complete gates, merge, consolidate,
  publish and verify stable 4.3.0. Keep 4.2.0 for rollback.
- No user machine, gxmetal, catalog, DNS or production infrastructure changes.

## 2026-10-22 — native qualification corrections

- The first Linux restart hit a display-port bind collision. Added a maximum
  of three pre-boot retries with fresh ports and tests proving bounds and
  immediate failure for unrelated errors. Subsequent packaged restart,
  shutdown and toolbar checks pass on both Windows and Linux.
- New own-CD packaged tests caught unsupported `preallocation=off` in the
  Windows raw-file backend. Both wizard and existing New Mac now use the
  supported Windows arguments; regression coverage explicitly checks each
  platform. Windows prepackaging now makes unit-test success a hard gate,
  since PowerShell had continued to packaging after a failed npm test.
- Linux's real default-sound download reached ready but could not start in
  the headless runner without PulseAudio. CI now starts a virtual sink;
  product sound defaults remain exercised. Setup checks report errors
  promptly. The shared renderer/IPC routes both pass locally with real tools,
  including Library pause/resume, Finder and guest-shutdown.
- Mac app/DMG notarization accepted, staples/Gatekeeper/full release checks
  pass. Final notarized Mac app bypasses setup for the retained disposable
  library, boots Finder and shuts down normally. Both final native CI runs
  are in progress; no public 4.3.0 tag/release yet.

## 2026-10-22 — first boot without host audio

- Windows own-CD creation passes after correcting its raw-image arguments.
  Default-sound first boots on headless Windows/Linux revealed missing
  DirectSound/PulseAudio output could stop QEMU before any guest execution.
- Recognized host-audio startup errors now retry once with a silent backend,
  retaining guest sound hardware/clock and the saved preference. A status
  message explains sound is unavailable for this session. Unit checks prove
  actual fallback arguments, unchanged settings and bounded failure. The
  expanded desktop suite has 20 passing tests. The temporary Linux CI audio
  service workaround was removed; native CI exercises the real fallback.
- Final native runs use source 49dd7de; the Mac app source remains the exact
  notarized e892524 tree. No public 4.3.0 tag/release has been published.

## 2026-10-22 — final native package gates pass

- Final source 49dd7de passes all 20 desktop tests on native Windows/Linux.
  Runs 39806245692 / 39806241075 complete both setup routes, real Library
  pause/resume/hash/extraction, first Finder boot and clean shutdown. Existing
  packaged pause/restart/reconnect/shutdown and toolbar geometry also pass.
  Both hosts exercise unavailable-audio fallback without changing guest
  settings. Completed job logs and uploaded reports are authoritative;
  GitHub's current Actions/PR incident leaves some status projections stale.
- All 57 packaged source/UI assets match on each platform; Linux DEB/TAR
  application archives match. Nine release archives and SHA256SUMS are staged.
  Mac notarization/source checks remain valid; source app tree is unchanged.
- Next: merge PR #33, consolidated root validation, exact-source tag, publish
  stable Latest 4.3.0, then verify every fresh anonymous download. 4.2.0 remains
  the rollback release. No public 4.3.0 tag/release yet.

## 2026-10-22 — 4.3.0 stable release published and verified

- Integrated the exact tested PR #33 head f9017eb through Git because GitHub's
  Actions/PR incident left mergeability and job status projections stale.
  Merge tree exactly matches the qualified branch. Source/tag v4.3.0:
  7467ddd02d03cc66360273de252b9db123124cfa. Consolidated root checks pass
  130 Swift tests (two optional Copland fixture skips) and all 20 desktop tests.
- Native Windows 39806245692 / Linux 39806241075 completed logs and artifacts
  prove both wizard routes, real Library pause/resume/verification/preparation,
  first Finder boot, audio-unavailable fallback and normal shutdown. Existing
  boot/pause/restart/reconnect/layout checks pass. All 57 packaged source/UI
  assets match, allowing Windows text line endings; Linux DEB/TAR ASARs match.
- Mac app notary 9cb3c72e-8ce7-4e4d-a835-2068451d1151 and DMG notary
  365824b9-dc88-4adc-870d-8eb5c110a621 accepted. Signatures, staples, Gatekeeper,
  exact package checks and final notarized Finder boot pass. Published stable
  Latest v4.3.0 with ten assets. Fresh anonymous downloads match all nine archive
  hashes; fresh public DMG passes full verification/staple/Gatekeeper again.
  DMG SHA256: d26f247bb7f0ab9ca983db068628330e36f196a0e445612fc3a25e13ea3ff6f5.
- Task guests normally shut down, temporary mounts detached, no temporary
  notary private key created. User gxmetal work, machines and unrelated
  worktrees preserved. No Library catalog, DNS, hosting or infrastructure
  changes. Task worktree/ignored output retained. Stable 4.2.0 remains for
  rollback by promotion to Latest. No pending release action.

## 2026-10-22 — retire remaining GXMetal surfaces for 4.4.0

- User authorized retirement from app, images and documentation, all task
  commits, main integration and stable release 4.4.0. Contained task branch
  codex/4.4-retire-gxmetal starts at 547448b. Existing local GXMetal build
  artifacts, user machines and unrelated worktrees are preserved.
- Removed required catalog/provenance version fields and dormant PowerPC NDRV
  transport/header. Rebuilt NDRV; mode/gamma contracts pass. Clean Library
  revisions select ATI hardware on Mac/Windows/Linux. Removed obsolete driver
  installation/status/game guides and updated current ATI documentation.
- Rebuilt clean 9.2.1 on a fresh filesystem, copying neither source free space
  nor preferences/documents and filtering retired files before writing. Added
  fourteen verified Apple/ATI graphics components plus ClassicMac Display.
  Finder at 1024x768 and normal shutdown pass. Read-only filesystem and full
  raw-disk audits find zero retired files/byte strings. Published immutable
  82,965,064-byte archive 9ec5b9e9…0e2d, then atomically promoted the catalog
  after saving its checked mode-600 backup. Other catalog editions are intact.
- Mac app uses verified public 4.3 runtime with fresh Swift binary and NDRV.
  App and DMG notarization/staples/Gatekeeper/exact verifier pass. Native
  packaging, final source checks and consolidated integration remain pending;
  no public 4.4 release/tag yet. Rollback retains 4.3 and the old catalog.

## 2026-10-22 — clean-image shutdown qualification correction

- Fresh native Mac import caught a disk-check dialog in the v3 candidate.
  Investigation showed the preparation guest had not exited after the menu
  click; the initial archive was made while it was still running. No public
  4.4 release existed and older apps reject that entry's minimum version.
- Performed native Shut Down and verified emulator exit, then cold-booted an
  independent clone to Finder without a disk-check dialog. That clone also
  exits normally after Shut Down, with no forced termination. Repeated the
  zero-retired-file/raw-byte and fourteen-component hash audits on the stopped
  source. Published immutable v4, withdrew v3 from the catalog, and preserved
  all other editions and the original rollback catalog. Qualified archive:
  82,965,347 bytes, SHA256 0e2449c5…75e48. Updated native fixtures and source.

## 2026-10-22 — final Mac and native Linux gates pass

- Final notarized Mac app freshly downloads public v4 through the native
  wizard and Swift importer, reaches ready and Finder without a disk-check
  dialog, reports ATI 3D on the host and mounts Tools. Verified host toolbar
  shutdown after Finder settles, emulator exit and the HFS+ clean flag.
  Mac retains the existing 15-second fallback for an unresponsive guest.
- All 131 Swift tests pass (three local optional engine/fixture skips), all
  21 desktop tests pass with the real signed image tool, and NDRV mode/gamma
  plus image preparation/packaging checks pass. All four Mac engine code
  images match public 4.3; matching source includes the rebuilt NDRV.
- Native Linux 39813549507 at 92b977d passes both setup routes, real public v4
  download/pause/resume/hash/import, first ATI Finder boot, normal shutdown,
  and existing standard-card boot/restart/display/toolbar checks. Logs and
  reports are authoritative while GitHub status projections remain stale.
- All 57 source/UI files match each native package; NDRV matches too. Linux
  DEB/TAR app archives match; all three QEMU source archives contain current
  NDRV and no retired transport files. Current public Deluxe/SuperDeluxe
  disks have no retired files. Removed five now-unused GXMetal screenshots.
  Final Windows 39814128066 checks remain pending; no public 4.4 tag/release.

## 2026-10-22 — native Windows gates pass; release scope frozen

- Final native Windows 39814128066 at 92b977d passes all 21 desktop tests,
  packaged standard-card boot/pause/restart/reconnect/normal shutdown/toolbar
  checks and both first-run routes. Real public ATI v4 download supports
  pause/resume, verifies/imports, reaches first Finder boot and shuts down
  normally. Completed logs and uploaded reports prove the results despite
  stale GitHub status projections. Native Linux gates also pass.
- Both packaged source/UI trees and NDRV match; all nine archives are staged
  with corresponding prepared source. Mac signatures/notarization/staples,
  final native wizard/import/Finder/clean shutdown and exact verifier pass.
  Next: PR #34 integration, consolidated root validation, tag/publish stable
  Latest 4.4.0, then fresh anonymous verification. No public 4.4 release yet.

## 2026-10-22 — ClassicMac 4.4.0 stable release published and verified

- Merged PR #34 as 15240410d2164a69f6af35765cf27ff102ae318d and tagged that
  exact source v4.4.0. The merge tree exactly matches the qualified branch.
  Consolidated root validation passes 131 Swift tests (two optional Copland
  fixture skips), all 21 desktop tests with signed qemu-img, 15 Python tests
  and both NDRV mode/gamma contracts. Sources are unchanged after these gates.
- Native Windows 39814128066 / Linux 39813549507 at 92b977d pass both setup
  routes, real public ATI v4 download/pause/resume/hash/import/first Finder
  boot/normal shutdown, and standard-card boot/restart/reconnect/layout checks.
  Completed logs/reports are authoritative during stale GitHub projections.
  All 57 source/UI assets and NDRV match; Linux DEB/TAR ASARs match. All three
  prepared QEMU source archives include current NDRV and no retired files.
- Final notarized Mac app natively downloads/imports public v4, reaches Finder
  without a disk-check dialog, reports host ATI 3D and mounts Tools. Normal
  toolbar shutdown after Finder settles exits with HFS+ marked clean.
  All four Mac engine code images match public 4.3.0. Final app notary
  e4fef133-0d11-4792-b1b6-a137980e6e97 and DMG notary
  7d290755-da73-449e-83ea-eed41b9cd515 are Accepted; signatures/staples,
  Gatekeeper and exact verification pass.
- Published stable Latest v4.4.0 with all ten assets. All nine fresh anonymous
  downloads match the public manifest; public API confirms neither draft nor
  prerelease. Downloaded DMG passes full verification, Gatekeeper and staples.
  DMG SHA256: 2c0ce8cae79f1d3c04e52ac639c953621aef4d0e822fb047b3da2dff267d7671.
- Qualified clean image v4, catalog hashes/backups, withdrawal of the initial
  v3 candidate and edition audits are recorded in docs/evidence/4.4.0. No
  existing user machine or historical immutable archive was overwritten.
  No DNS/configuration change or service restart. Task guests are stopped and
  mounts detached; no temporary private key was created. User local build
  artifacts and unrelated worktrees are preserved; task checkout/output are
  retained for support. No pending release action. Rollback retains v4.3.0
  plus the checked original catalog; promotion changes no installed machine.

## 2026-10-23 — portable Platinum desktop for 4.5

- Austin authorized improving Windows/Linux to match the Mac’s Platinum UI,
  single executable downloads, AustinLand/Proxmox testing and a new release.
  Contained branch `codex/4.5-desktop-platinum`, baseline main `14cbebd`.
- Exported measured Mac tables and BDF glyphs into a deterministic desktop
  renderer. Its independent framebuffer gate passes at 1×/2× with zero
  differing pixels, including the 95-character system-font atlas/advances.
  Semantic HTML retains accessibility and keyboard controls. Removed the host
  menu bar and host window decorations; title controls and WindowShade use
  sandboxed, sender-checked IPC. Home follows the native Mac layout.
- Portable Windows EXE and Linux AppImage replace multi-file ZIP/TAR downloads;
  DEB remains optional. Native CI builds/tests are in progress. UI checks on
  the Mac development host pass the wizard, popup, home at 940/780 widths,
  settings, media and reversible WindowShade. All 22 desktop tests pass with
  the real signed qemu-img. These are not yet native Windows/Linux results.
- Started only existing dedicated QA VMs 170/171 using AustinLand. The Windows
  start returned 500 despite starting; fresh detail confirms running/agent
  ready, so no duplicate VM was created. Existing fixtures/user data preserved.
  No public release, catalog, DNS or infrastructure configuration changes.
  Next: qualify exact portable packages, integrate, release and verify downloads.

## 2026-10-23 — portable packages and native pixel qualification complete

- Runtime/UI source is frozen at `956dd7d` on PR #35. Shared Mac measurement
  tables/BDF glyphs pass the independent native fixture suite; 21 actual DOM
  control states plus the composed title frame pass with zero differing pixels
  at both device scales. Final Windows/Linux 940×700 homes are pixel-identical.
- Real Windows testing found the OS’s 39-pixel minimum. WindowShade now clips
  the native drawing/input region to 23 pixels; GetWindowRgn/GetRgnBox verifies
  physical region height at 1×/2×. Errors expand the window before presentation.
  A final composed-header check exposed and removed legacy CSS overlays and
  corrected integer title/widget placement. Native UI and exact package checks
  pass after that correction. All 61 packaged source/UI files match source.
- Both dedicated Proxmox hosts pass fresh public ATI image download, pause/
  resume, SHA-256/import, both setup routes, Finder, restart and normal shutdown.
  Final titlebar-only packages repeat the UI and guest cycle using the stopped
  task-owned public fixture; main/engine/setup code is unchanged. Linux CI
  40037705241 passes; Windows CI 40037702912 is finishing the full matrix.
- Windows QA VM 171’s existing 100 GiB disk lacked space for another 32 GiB
  fixture. Verified SazProx headroom, extended only its disk to 228 GiB via
  AustinLand, then extended C: online without deleting data. FUSE 3 is installed
  only on the minimal Linux QA VM. Earlier failed/partial test evidence remains.
- Mac 4.5 retains the exact qualified 4.4 native frontend and four engine code
  images, refreshed version metadata, original entitlements and final accepted
  app/DMG notarization. Full exact verifier, staples/Gatekeeper and isolated
  first-run launch pass. Source archives remain byte-identical to qualified
  4.4 with NDRV checked. All 131 Swift tests pass (three optional local skips),
  all 22 desktop tests pass with signed qemu-img, deterministic export passes.
- No public 4.5 tag/release yet. Next: integrate, consolidated root validation,
  publish stable Latest and verify every anonymous download. Rollback retains
  v4.4.0. User machines, root gxmetal and unrelated worktrees are preserved;
  no catalog, DNS, hosting configuration or production service changes.

## 2026-10-23 — final native CI gates pass

- Windows 40037702912 and Linux 40037705241 at `956dd7d` both pass all native
  unit, exact portable UI/pixel/title-frame, boot/restart/shutdown and fresh
  setup routes, including public ATI Library import and first Finder boot.
  Final Proxmox portable bytes independently pass the UI and guest cycle.
- Final Windows/Linux home captures are pixel-identical. Scale 2 captures,
  source/UI inventories, source archives, SHA256SUMS and notarized Mac parity
  are retained in evidence/4.5.0. PR #35 is ready for integration/publication.
  No public 4.5 tag/release yet; rollback retains immutable v4.4.0.

## 2026-10-23 — ClassicMac 4.5.0 published and independently verified

- Merged PR #35 as `69f3d52ed721599542afe231bd5820549ba07813`, then tagged that
  exact source v4.5.0. Consolidated root checks pass 131 Swift tests (two
  optional Copland skips), all 22 desktop tests with the signed image tool,
  deterministic exports and bundle syntax. Qualified desktop/app trees match.
- Native Windows 40037702912 and Linux 40037705241 at `956dd7d` pass all
  fresh gates. Dedicated Proxmox hosts independently pass setup/public ATI
  import/Finder/normal shutdown and exact final UI/pixel/title/guest cycles.
  All 61 source/UI assets match each package. Source archives retain exact
  qualified 4.4 bytes and NDRV; Mac native code remains unchanged.
- Verified all ten draft asset sizes/digests before publishing stable Latest
  v4.5.0. All nine fresh anonymous downloads match the qualified manifest.
  The fresh public Mac DMG passes full exact verification, signature/staple
  and Gatekeeper checks. DMG SHA256:
  `7a980ada9708276436f1596781f25fd1934ba409929362162f36d0406a2d179b`.
- Mac app notary `493abc3d-6396-449b-8b44-1fb02b14c510` and DMG notary
  `14fe17ea-6ed5-4eac-8108-7656613a5930` are Accepted. Original entitlements
  are preserved; temporary private-key files are removed. Public verification,
  integration, source/package parity and cleanup are in evidence/4.5.0.
- All task guests shut down normally. Task scheduled tasks and RDP are closed;
  dedicated VMs 170/171 are stopped. QA files and task worktree/output remain
  for support. Windows QA disk remains 228 GiB, with prior data preserved;
  Linux QA retains FUSE 3. No other VM or production system is changed.
- No user machine, root gxmetal, unrelated worktree, Library catalog, DNS or
  hosting configuration was modified. Stable v4.4.0 remains for rollback by
  promotion to Latest. Requested work and publication are complete.

## 2026-10-23 — classic base image build and delivery scope

- User explicitly requests upload/catalog registration in both ClassicMac and
  RetroBoot. No publication has occurred. Existing server catalogs were read
  without changes; server has about 65 GB free.
- Apple installer 7.5.5/8.1 guests pass cold Finder boots, HTTP, PASV FTP via
  Transmit 1.2 and StuffIt 5.5 extraction with byte-exact fixture comparison.
  Netscape 3.01 Java startup configuration remains under investigation.
- 8.6 native MacIO/CMD646 disk trials did not qualify. A private fixed Virtio
  driver and correct HFS boot blocks reach responsive Finder with ROM 1.4,
  G3, 128 MB, virtual memory off. Full reinstall/apps/network gates remain.
  Unqualified legacy IDE feature is being removed from the task branch.
- RetroBoot source inspection confirms its importer and launcher support PPC
  only; no Quadra emulator is shipped. Catalog compatibility must be delivered
  with runtime support, not by advertising unbootable downloads.
- Other work advanced root main while this contained task was in progress.
  Preserve its release work and preexisting untracked gxmetal directory.

## 2026-10-24 — 68k guest qualification and parallel runtime dependency

- System 7.5.5 and Mac OS 8.1 now have native Netscape 3.04 installations.
  Both pass warning-free cold launches, HTTP fixture loads, and earlier
  Transmit 1.2 PASV download/StuffIt 5.5 byte-exact expansion checks.
  Removed installers, test files, connection history and browser caches;
  the preserved browser preference opens about:blank.
- Allocation-aware HFS copies retain every allocated block and all partition,
  bitmap and alternate-header bytes while zeroing free allocation blocks.
  Private reports compact755.json/compact81.json retain geometry and matching
  retained-byte digests. Fresh Linux q800 starts reach Finder; input and full
  RetroBoot presenter qualification are still separate.
- Isolated build VM175 classic-base-images-build (McHost) was created through
  AustinLand, pinned through authenticated guest-agent SSH host-key readback.
  Private keys/state stay in output/classic-base-images/lab. Both PPC and m68k
  QEMU targets build from RetroBoot's pinned prepared sources on this VM.
  Native default floppy metadata is outside that early Linux smoke profile.
- The active RetroBoot D044 task now owns Quadra emulator/guest/hostd support
  in its three worktrees. Do not compete with those edits. Image delivery and
  ClassicMac support remain in this task. Permission to message its task was
  requested; no cross-task message has been sent.
- Corrected MacBinary transfer metadata: /usr/bin/macbinary drops the high
  Finder flag byte (engine 0x2140 became 0x0040). Private extraction now restores
  both flag bytes from the MacBinary header, preserving application bundles
  and aliases. HFS tools now have project-private mount state, avoiding the
  user's shared ~/.hcwd while other tasks prepare guests.
- Mac OS 8.6 still does not qualify: a self-hosted full reinstall stalls in
  synchronous File Manager SetEOF. A separate CD-to-blank-disk path uses two
  Virtio devices with explicit paths. Placing PCI devices below the onboard
  hardware changes MacIO BAR placement and prevents the retail ROM startup;
  slots f/10 preserve it and reach CD Finder. The stock WinWorld image's file
  data is not readable by this HFS/OpenBIOS path; the existing repaired local
  CD is used for this probe. No claim of payload identity is made.
- Focused validation: 48 config/argument tests pass; 25 download/import tests
  pass with one optional Copland skip. No image or catalog publication yet.

## 2026-10-24 — real archive import and 8.6 wakeup diagnosis

- Local System 7.5.5 v1 archive: 20,511,664 bytes, SHA256
  68b8ffb1993664644435e7c9bb4f4d58737a7da6c63e6cb794b7c48830c38246.
  Mac OS 8.1 v1: 36,423,160 bytes, SHA256
  779cb74e50720f263e15d9a79011d1aa1f2a8ef25c83ae55e2c1a4ffadca3d09.
  Both production Swift imports pass checksum, capacity, hardware and firmware
  checks and reach Finder from the imported disk with fresh PRAM.
- A retail 8.6 core boots standalone with corrected MacBinary flags. Realtek's
  original RTL8139x 3.20 driver (archive SHA1 a3bc1f1caa0bd27e53710312f5548f3974d41baa)
  exposes Ethernet; configured DHCP and passive FTP through native control panels.
- The apparent 8.6 I/O freeze is at least partly the known nanokernel wakeup
  race: a controlled incoming network interrupt immediately wakes it; keyboard,
  mouse and clock resume and Netscape Communicator loads the HTTP fixture.
  Ported RetroBoot's CUDA idle interrupt as an opt-in property (default zero)
  in a task-local QEMU build; testing 50 ms on the legacy profile. No changes
  to the ordinary OS 9 runtime defaults. Retrial of the native full installer
  and Transmit 1.2 PPC follows; 1.6 PPC login/LIST passes but RETR stalls.
- No production upload, catalog promotion, shared push or release occurred.

## 2026-10-24 — qualified 68k downloads published

- Published immutable system-7.5.5-base-v1.tar.gz and mac-os-8.1-base-v1.tar.gz
  under mcchord.net/classicmac, with the exact archive hashes above. Uploaded
  to private temporary filenames, verified remote sizes and SHA256, then linked
  to previously absent immutable final names. No existing archive replaced.
- Published https://mcchord.net/classicmac/catalog-v2.json with all six old
  entries retained byte-for-value, plus the two qualified Quadra entries.
  SHA256 4b6916f25f0e95d2349415101a400d50af56278e67d146fe849735e14b8d6025.
  Legacy catalog.json is byte-identical, SHA256
  21661864c256e8780c50b0fefa037a8f75a038bf8cc7a77a8850be6d35ffcf85.
  Rollback base is /root/classicmac-catalog-backups/pre-base-images-v2-20261024.json
  (0600); restoring its entries to v2 leaves the legacy endpoint unchanged.
- Full fresh public HTTPS downloads and HTTP 206 ranges pass. Both production
  Swift public imports pass and their newly imported copies reach Finder and
  respond to keyboard input. Private evidence in output/classic-base-images/hosting,
  swift-public-{755,81}-import.log, run-public{755,81}. No Apache, DNS or other
  site configuration change/restart. Client release/integration is still pending.
- 8.6 remains excluded: the idle IRQ fixes Finder/Netscape lost wakeups, but
  SetEOF during full installation still waits indefinitely after about 12 MB of
  writes. An explicit synchronous-driver Gestalt response does not fix it.
  SCC diagnostic builds log completed block I/O before the File Manager wait;
  native runtime command/completion tracing is the next isolated probe.

## 2026-10-24 — current-main client checks and 8.6 file-write probe

- Rebased the contained branch onto current main 6c5ff7b, preserving main's
  release history and all task journal entries. Current checkpoint dbc3d73;
  no shared push or release. Local 4.6.0 version preparation remains uncommitted.
- Swift focused suite: 75 tests, 2 optional real-archive skips, zero failures.
  Desktop suite after isolated npm dependency bootstrap: 22 pass, 2 optional
  qualification skips. Both real public 68k archives pass desktop production
  imports with the prepared Quadra family, 128 MB, 640x480x8 and Tools off.
- Apple DV35 explicitly disallows setting dNeedTime on native NDRV drivers.
  A private writable-PPC probe skips that flag. A small native Toolbox-based
  68k test app then grows a new file to 8 MiB, writes 4 MiB and closes it, all
  returning noErr. A prior RetroConsole-based test app exits with type 12;
  that test artifact is discarded and is never part of a template.
- Full self-hosted 8.6 installation still waits at SetEOF even with that
  change. It may involve the active System/resource fork or same-volume
  install source, rather than generic data writes. Do not publish 8.6 until
  browser, FTP, StuffIt and payload checks pass. Native Memory preferences
  and clean-install source/destination behavior are the next checks.

## 2026-10-24 — corrected 8.6 virtual-memory preference

- Native Memory control panel disproves the earlier VM-off assumption: VM was
  still on at 179 MB with 128 MB physical RAM. The edited vm resource was in
  the System file, but the effective preference is the vm resource in
  System Folder:Preferences:System Preferences (flags 1, size 0x0b300000).
- Turned VM and RAM Disk off in the native Memory panel, saved and cleanly
  shut down. The native preference now has flags 2 and size 0x08100000.
  Private capture apps/86-native-system-prefs retains that resource. The
  separate System-file vm resource is unchanged by the control panel.
- A fresh boot after the native setting is being checked, followed by the
  installer/application write gates. Prior statements that 8.6 VM was off
  refer to the ineffective earlier edit, not verified runtime state.

## 2026-10-24 — 8.6 application data path passes

- Cold boot retains VM Off when the correct native System Preferences vm
  resource is used (flags 2). RAM Disk UI state resets with non-persistent
  mac99 NVRAM; no RAM Disk volume is mounted. VM Off itself is disk-persistent.
- Transmit 1.2 PPC, after a fresh boot and with the private FTP fixture's
  sendfile path disabled, downloads 4,194,582 bytes in 7.675 seconds. StuffIt
  5.5 automatically expands it. Extracted 4,194,304-byte payload SHA256 is
  501e3235620a82d1d045ebad6e1bc34ace244170da0311ffa942a5e95107b121,
  exactly matching the host fixture. Guest shut down cleanly before extraction.
- Full clean install now reaches an explicit missing-source-folder error for
  Mac OS 8.5, so no full final installer success is claimed. The core came
  from the earlier Apple core installer stage under a 9.2.1 startup CD.
- A public mirror of the original G4 8.6 691-2460-A media is being downloaded.
  Archive.org identifier 691-2460-AZsawtooth_8-6-Install-or-Restore, ZIP
  395,012,040 bytes; expected SHA1 917c37ce752224ca376bd22231d5483dd3ef79ee
  matches Macintosh Repository's original-media manifest. Test it separately
  for native IDE/Sungem support before finalizing the retail Virtio profile.

## 2026-10-24 — public RetroBoot imports and 8.6 repeatability regression

- RetroBoot contained branch now builds on completed quadra-hostd dependency
  e473f4c without editing that worktree or root. Local commit 07aff57 switches
  its Internet catalog default to catalog-v2.json and records the delivery
  contract. Production hostd imports of both full public 68k archives pass,
  preserving family, hardware, capacity and pinned ROM. Seven supported rows
  are decoded; Copland is omitted. No shared push, integration or host release.
- A fresh 8.6 filesystem and clean NDRV build reach Finder, launch Netscape
  without a profile wizard after native profile repair, and pass HTTP. Native
  About This Computer confirms VM Off / 128 MB. The original System file was
  restored for one probe (only its default vm resource byte differed); this
  does not fix large transfers. Reapplying the old default does not fix them.
- The earlier 4 MiB FTP result is not repeatable: a new transfer stalls even
  on an untouched source-disk clone with the exact original diagnostic loader
  and argv. Host curl still fetches the full fixture with exact SHA256. An
  incoming network wakeup and the CUDA idle IRQ do not resolve this stall.
  The RTL receive ring is drained with no pending interrupt at inspection.
  Do not attribute the issue to filesystem cleanup or claim 8.6 qualified.
- Diagnostic SCC/runtime logging and unique driver-name experiments were
  removed from the tracked driver build. The candidate adds synchronous
  Driver Gestalt reporting and avoids native writable-driver dNeedTime,
  retaining the existing read-only Tools workaround (Apple DV35). The new
  loader builds; final behavioral qualification remains.
- Original G4 691-2460-A media ZIP was downloaded and verified at
  395,012,040 bytes / SHA1 917c37ce752224ca376bd22231d5483dd3ef79ee.
  Native IDE CD startup fails on both CUDA and PMU, 128 and 512 MB. OEM
  Apple Enet 2.3 has GMAC support but a retail-ROM probe still cannot connect
  after selecting the new Ethernet interface and DHCP in native TCP/IP.
  No OEM payload is in Git. No 8.6 image or catalog entry is public.

## 2026-10-24 — 8.6 fresh-download repetitions and native-controller probe

- The 68k Transmit 1.2 app (18 CODE resources, no cfrg, zero data fork)
  completed a fresh 4,194,582-byte passive FTP download in 7.38 seconds.
  StuffIt expanded it and the extracted payload exactly matches SHA256
  501e3235...07b121. But another fresh image with the same app also stalls.
  Do not claim an app-architecture or duplicate-file fix from one pass.
- Smaller 512 MiB classic HFS also reaches Finder; its transfer stalls. A
  plain HFS+ fresh filesystem with original 8.6 files remains in firmware
  before RTAS, so it is not a valid boot candidate. A CMD646 native HDD
  reaches the retail Mac OS ROM and ?-floppy, suggesting the source OS 9
  ATA driver prefix is unsuitable. Next probe initializes a blank native
  CMD646 disk from a read-only Virtio 8.6 startup CD, with only one Virtio
  block device. No diagnostic or failed filesystem is publishable.
- All production Swift config/arguments/download checks: 75 tests, 2 optional
  real-archive skips, zero failures. Desktop: 23 pass, 2 optional skips.
- RetroBoot own branch bec18c7 adds candidate 8.6 profile, import preservation
  and capability-gated rows, a verified startup-driver overlay and relative
  ADB input. It bypasses OS 9 guest-prep/driver injection. 26 pure
  hardware/catalog tests pass. Real appliance behavior is still unqualified.
  All seven old driver intermediate hashes reproduce exactly; final overlay
  SHA256 1aaa285a7377562210f8d9607c61b0d516d6a84b73854df0d9e412044edbcf45.
  No shared push, release, physical device or 8.6 publication occurred.

## 2026-10-24 — OEM restore inspection and 68k driver regression

- Rebuilt shared declrom passes fresh cold boot and Finder keyboard input on
  separate clones of both published 68k sources; both shut down natively.
- CMD646 native Drive Setup 1.7.2 lists no disks. No ATA initialization
  occurred. Both scratch native-controller and small-HFS probes stopped.
- Decoded Power Mac G4 HD.img from the verified 691-2460-A media using
  mhjacobson/ndif2raw; output is 670,998,528-byte plain HFS+. Read-only
  explicit raw image attachment works. Its full installed System differs
  from the CD startup System. ROM 2.5.1 matches the reference ROM data hash
  c18ffdc0...707ac. Vio/native IDE probes still fail early in that ROM.
- Reference ROM files have no FinderInfo, so a plain hfs-copy loses the
  tbxi/chrp type required for startup. A private 1.8.1 copy with correct
  type reaches ROM code, but no usable desktop yet. Do not generalize
  earlier missing-metadata ROM tests into ROM incompatibility claims.
- Current retail probe checks Apple KB60337's documented TCP/IP Advanced
  option to disable Load Only When Needed and restart. No pass claimed.

## 2026-10-24 — 8.6 OEM, queue and native ATA comparisons

- Disabling TCP/IP Load Only When Needed acquires DHCP on startup but does
  not fix large FTP. Retail ROM 1.6 (with tbxi/chrp type) boots but stalls.
- The full G4 ASR System boots using retail ROM 1.4; it includes Open
  Transport 2.5.1 and Apple Enet 2.3. Passive FTP is enabled via the existing
  stock Internet Preferences UsePassiveFTP key. Its large transfer still stalls.
  One script hit the obsolete signup prompt after a failed pointer operation;
  the attempt failed with no personal address. Escape cancels that prompt.
  Do not batch dependent typing after pointer errors.
- A private PPC QSend polling experiment passed two consecutive 4,194,582-byte
  downloads and StuffIt expansion. Both payloads exactly match SHA256
  501e3235620a82d1d045ebad6e1bc34ace244170da0311ffa942a5e95107b121.
  A second fresh cold image stalls again, so it is not a qualified fix.
  interruptmask.c was restored; no experimental polling in tracked binaries.
- Native About This Computer on that failed fresh image directly reports
  Mac OS 8.6, VM Off, 128 MB; screenshot kernel-memory-real.png.
- Correct CMD646 OF shape has an ata-4@0 child, compatible cmd646-ata. Native
  Drive Setup sees an uninitialized ATA bus 2 disk, then writes APM plus two
  Apple_Driver_ATA partitions. HFS remains zero and the device disappears
  after driver loading, so no successful native initialization is claimed.
- Two-Virtio CD/HD probes do not both mount; full installation remains
  unqualified. Current comparisons isolate the stock upstream video NDRV
  (18,752 bytes) and CPU optimization flags disabled, using separate clones.
- 68k public artifacts/catalog are unchanged. Local candidate source checkpoint
  4a5c077 contains profile/defaults/test changes and excludes unqualified 8.6
  publication. No shared push or release. OEM raw-image attachment ejected.

## 2026-10-24 — 8.6 network isolation and paced HTTP proof

- Disabling all three CPU optimizations (MMU contexts, fast FPU, fast branch)
  does not fix the stall. Stock upstream video NDRV passes two transfers on
  one image but stalls on another fresh image. Neither is a fix.
- Finder duplicates the 9,504,985-byte Netscape app after an FTP stall.
  Transmit 1.7 from Panic's official File Museum boots but still stalls.
- Fresh Netscape HTTP, before any FTP, also stalls. QEMU packet capture shows
  ACKs stop at 37,441 bytes with a nonzero 17,520-byte window; the backend
  retransmits while the RTL ring is drained, receiver enabled, no IRQ pending
  or missed-packet count. Do not classify this as just a File Manager issue.
- The identical 4,194,582-byte ZIP downloaded through a paced loopback HTTP
  fixture (1,440-byte chunks / 5 ms gap) completes at 100%. Clean native
  shutdown and extraction exactly match SHA256
  fbc4598f63f7511fadad32f65c912640278b38a9819d5312cb6e1c41776cdd84.
  Netscape's ZIP helper is ZipIt, absent in the candidate; fix that mapping
  to StuffIt or Save To Disk before any final image.
- A private QEMU experiment adds opt-in delayed RTL transmit completion;
  default zero retains the original path. It is being tested at 1 ms using
  a fresh image and unpaced FTP. No final driver/runtime claim yet; migration
  support is deliberately not in this experimental source.

## 2026-10-24 — delayed Ethernet completion trial

- Paced HTTP payload is extracted after native clean shutdown and matches its
  exact ZIP hash, not just a progress bar.
- New opt-in RTL transmit deferral keeps hardware descriptor ownership until
  a virtual-clock timer sends each queued packet and completes it in FIFO
  order. Default zero uses the unchanged upstream path. At 1 ms the first
  guest completes two unpaced 4 MiB passive downloads in 0.986 / 0.731 s,
  with native automatic StuffIt expansion. Both extracted payloads match
  501e3235620a82d1d045ebad6e1bc34ace244170da0311ffa942a5e95107b121.
  Fresh second-guest repetition is still pending; not yet a qualified fix.
- A separate receive pacing experiment exists only in task vendor source,
  default zero and not tested yet. Neither experiment is in a tracked patch
  or release. Pending migration support must be added if either is retained.
- Private VNC screenshot helper now requests two frames, avoiding stale
  display-cache captures immediately after guest actions. Native menu drags
  must move down before right to avoid switching from Apple to File menu.

## 2026-10-24 — repeated Ethernet qualification and matched Linux runtime

- Final opt-in RTL TX patch defers DMA/send/descriptor completion/TxOK using a
  virtual timer and four FIFO deadlines. Default zero preserves the old path.
  Reset/unrealize handle the timer; an optional migration subsection saves the
  queue, deadlines and timer. The receive experiment and CMD646 selection are
  removed. Swift and desktop enable 1 ms only for the 8.6 Virtio profile.
- Fresh second guest: both expanded FTP payloads match SHA256 501e3235...b121;
  unpaced Netscape HTTP ZIP matches fbc4598f...dd84 after native shutdown.
  Final PPC Transmit 1.2 source passes two more transfers. Native Netscape ZIP
  mapping is being changed from absent ZipIt to Save To Disk before cleanup.
- Swift focused consolidated checks: 75 tests, 2 optional skips, zero failures.
  Desktop 23 pass / 2 optional skips; package accounting 3 pass, Bash syntax and
  diff checks pass. RetroBoot Linux: 43 hardware/catalog tests pass, versus the
  expected macOS supervisor/platform failures. New Linux package build and
  capability verification pass on owned VM175; no appliance qualification yet.
- No new public archive/catalog or shared software release in this checkpoint.

## 2026-10-24 — all three immutable images published and imported

- Final 8.6 PPC source passes two FTP/StuffIt payloads and unpaced HTTP with exact
  hashes. After native shutdown, test files/caches/history are removed, Netscape
  ZIP handling saves to disk, and the HFS free allocation blocks are zeroed while
  preserving the allocated-content digest. Source frozen read-only.
- 8.6 archive: 56,692,342 bytes; expanded 2,147,503,280; 2 GiB disk; initial storage
  bound 191,889,408. SHA256 f7e61bc9c8a1a098b55006bf9619c011447def243c79a91eed03b8875a2f9a88.
  Immutable public URL https://mcchord.net/classicmac/mac-os-8.6-base-v1.tar.gz.
- VM175 builds the matched RetroBoot Linux runtime, verifies its package and passes
  43 hardware/catalog tests. Actual production import/argv cold boot the archive.
  Native HTTP ZIP and FTP expanded payload match their source hashes after native
  clean shutdown. FTP transferred 4,194,582 bytes in 0.089 s. This proves the Linux
  emulator/profile, not physical hardware or full appliance presentation.
- Public full HTTPS/hash/Range 206 pass before atomic 9-row catalog-v2 promotion.
  Catalog SHA256 942b2bb05f44302f2a36a24d9fa45e72876497e5a67f971cc80177b6c2b7c5bc.
  All 8 previous v2 rows preserved. Legacy catalog.json unchanged. Remote backup
  /root/classicmac-catalog-backups/pre-base86-v2-20261024.json (0600). No service/DNS
  changes. Publication evidence/rollback in private hosting/.
- Fresh public bytes pass production Swift, desktop and RetroBoot imports; all
  three base IDs survive the compatible RetroBoot capability filter. 8.6 imported
  native disk boots and accepts Finder keyboard input. Desktop real import test
  now covers prepared Power Mac hardware as well as the Quadra ROM contract.
- Source checkpoints: ClassicMac 36432ca, RetroBoot b475e3d. No shared Git push,
  application release or physical device change. Existing clients still need the
  companion updates. Next is explicit permission to publish client source and
  integration with D044; image/catalog upload was already authorized.

- Cleanup: every owned guest Mac stopped through native Shut Down. Local network
  fixtures and the VM175 SSH tunnel stopped after command/PID verification.
  AustinLand accepted graceful shutdown for owned VM175; private build and proof
  files remain on its disk. No unrelated VM, worktree or service was changed.

## 2026-10-24 — client branch publication approved

- User explicitly approved pushing both client branches and opening integration
  PRs. Inspected current origins; ClassicMac is based on current main 6c5ff7b.
  RetroBoot depends on D044 hostd e473f4c; its PR must remain draft until the
  coordinator integrates and qualifies the full Quadra runtime.
- Final build inspection found RTL8139 missing from the repeat-build reset list.
  Added the file and ran source preparation twice successfully in the owned QEMU
  worktree. Linux/Windows smoke paths now match desktop package version 4.6.0.
  Bash syntax, version/path consistency and diff checks pass. Guest image bytes
  and the public catalogs are unchanged.


## 2026-10-24 — approved client branches and integration PRs published

- Pushed ClassicMac codex/classic-base-images at 4e2124a and RetroBoot's matching
  branch at 44b781b using the user's explicit approval. Both remote refs verified.
- Opened ClassicMac PR #36, ready for review:
  https://github.com/amcchord/ClassicMac/pull/36.
- Opened RetroBoot PR #1 as draft:
  https://github.com/amcchord/RetroBoot/pull/1. Its description names unmerged D044
  hostd dependency e473f4c and the appliance presentation/lifecycle gates. It must
  be integrated with the coordinator's complete Quadra work.
- Both PRs are attached to this task. Images/catalog remain as previously
  published. No application release, merge or physical-device deployment occurred.
  Packaging workflows are manual/branch-filtered and were not dispatched here.
  The existing local/import/guest qualification and two repeat source-preparation
  passes are documented in the PR. Next: review and coordinated integration.

## 2026-10-24 — native 4.6 desktop release candidates qualified

- Worktree `worktrees/native-desktop`, branch `codex/native-desktop`, incorporates
  PR #36 at 52b775e and PR #37. Replaced Electron with compiled C++/Qt, integer
  Platinum/BDF rendering, native guest windows, direct authenticated RFB/QMP,
  complete machine/setup/Library/media/settings/lifecycle/paste operations,
  portable EXE and AppImage. Existing libraries remain compatible; Windows
  raw disks use NTFS sparse files. Runtime notices/inventories are refreshed.
- Both Proxmox hosts pass all three public base imports, Finder/native input,
  pause/resume and clean shutdown (42 steps per host). Exact candidate packages
  additionally pass both setup routes with pause/resume, 1x/2x UI, all three
  guest boots/restart/shutdown (24/14/14/31 steps per host). A last sizing check
  adds minimum-window coverage and preserves resize limits across WindowShade.
- Native fixtures pass 84 reference comparisons, 95 advances, four actual widget/
  frame composites, 20 startup profiles, bounded archive, sparse disk, paste and
  RFB checks. Final capture comparison caught inherited item-view smoothing at
  2x; it is disabled explicitly and a whole-home integer-scaling gate is added.
  Release candidates are being rebuilt for this final renderer correction.
- Real testing fixed a normal RFB disconnect/dialog lifetime crash (ASAN passes),
  silent NSIS launch-directory/error handling and quoted extraction paths, and
  a Debian 13 TLS mismatch by bundling matching libssl/libcrypto. No failures
  are hidden; earlier evidence remains under ignored output/4.6.0.
- Mac is freshly built with PR #36, Developer ID signed, accepted/stapled app
  notary 23e07921-8082-4ddb-9410-4ea75c807941 and DMG notary
  5c008529-b4d0-4fd5-8abb-2a17023617b7. Full DMG verification passes. Focused Swift
  checks pass 136 tests/3 optional skips; real public 7.5.5 import also passes.
- No public release or main merge yet. Next: check final UI pixels/package
  inventories, integrate both PRs, run consolidated root validation, publish
  v4.6 and verify fresh downloads. v4.5 stays Latest until then. QA VMs170/171
  and the task RDP session remain running; remove own scheduled task/RDP and
  gracefully stop the QA hosts after qualification. No production catalog,
  hosting or DNS changes; root gxmetal and unrelated worktrees are preserved.

## 2026-10-24 — native 4.6 integration and final preflight

- PR #36 merged at 8900b7a; PR #37 at 1a5cae8. Source matches final native
  CI ff11171 and the Mac build's 52b775e. Root gxmetal is preserved.
- Final Windows CI 40106495101 and Linux CI 40106493052 pass. Exact final
  EXE/AppImage repeat both setup routes, 1x/2x UI including compact sizing
  and WindowShade, and all three Finder/lifecycle checks (24/17/17/31 steps).
  Both pass native self-tests. Actual home scaling and active cross-platform
  comparison have zero differing pixels. Package inventories verify every
  file hash (Windows415/runtime238, Linux380/runtime128). Windows extraction
  into spaces/hyphens and Debian public HTTPS catalog/TLS pass.
- Root consolidated validation passes 136 Swift tests, two optional Copland
  skips, no failures, including live 7.5.5 import; native rendering/model,
  deterministic exporter, 15 packaging regressions and script syntax pass.
- Final artwork correction replaces Qt list borders with measured Platinum
  sunken frames. One QA recipe accidentally renamed an old installer path;
  corrected the harness path and repeated all gates successfully. Earlier
  failures remain in ignored output. No app regression was hidden.
- Durable reports, selected captures, source checks and checksums are saved
  under docs/evidence/4.6.0; current screenshots show the native apps. Nine
  release assets are assembled. Publication and fresh public verification
  remain next, followed by own QA task/RDP and VM cleanup. v4.5 is rollback.

## 2026-10-24 — native 4.6.0 published and publicly verified

- Tagged exact source 585b4189844703c4a7406f9953edb7e184118ba3 as v4.6.0.
  Published stable Latest at 15:31:58 UTC with nine assets. All uploaded
  sizes/SHA-256 metadata match the qualified application/source files.
  Release: https://github.com/amcchord/ClassicMac/releases/tag/v4.6.0.
- Fresh anonymous downloads verify all eight application/source files and
  SHA256SUMS. Latest API confirms v4.6.0 stable, non-draft, nine assets.
  Downloaded Mac DMG passes full exact contents/signature/staple/Gatekeeper
  verification. Initial root verifier referenced an older ignored dist Tools
  ISO; reran with the independent checksum-qualified CI Tools image used by
  this build, preserving every verifier gate. Product files are unchanged.
- No running QA guests remain. Removed own ClassicMac46QA scheduled task,
  ended own RDP, requested graceful shutdown and verified VMs170/171 stopped.
  No temporary notary keys remain. Existing fixtures/user data, root gxmetal,
  unrelated worktrees and task output are preserved. No catalog/DNS/hosting
  mutation. v4.5.0 is retained as rollback by promoting it to Latest.
- Public verification and cleanup evidence are committed in docs/evidence/
  4.6.0; CURRENT now records the completed release. No required work remains.

## 2026-10-24 — Windows DPI and resize follow-up started

- User reports scaling/graphics and guest resolution failures in v4.6.0 and
  requests a detailed robustness pass. New owned worktree windows-dpi-resize,
  branch codex/windows-dpi-resize from d1a69b1. No agents delegated.
- Confirmed forced DPI rounding and missing guest resize messages; prior
  1x/2x reference gates did not cover fractional OS DPI or guest mode changes.
  QEMU already forwards RFB SetDesktopSize to the Standard/QFB driver channel,
  so the fix can use the native async transport without changing engines.
- Started existing QA VM171; API returned a warning but read-back confirms
  running at 192.168.105.146. No VM created/resized or user data modified.
- Next: implement and test per-monitor/fractional scaling, real guest resize,
  bounded/fixed-resolution fallback and window usability on scaled work areas.
