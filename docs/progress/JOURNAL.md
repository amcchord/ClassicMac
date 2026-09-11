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
