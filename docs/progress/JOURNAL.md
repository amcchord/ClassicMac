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
