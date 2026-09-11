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
