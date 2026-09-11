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
