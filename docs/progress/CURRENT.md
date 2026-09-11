# Current work: ClassicMac 3.0

## Objective

Publish the completed, signed and notarized ClassicMac 3.0 build to GitHub,
including all remaining project source changes, as explicitly authorized by
Austin after candidate validation.

## Baseline and ownership

- Root integration checkout: `~/Development/ClassicMac`, branch
  `codex/3.0-integration`, starting at `f763917`.
- Feature agents work in contained `worktrees/3.0-*` checkouts and commit only
  their own scoped work. The coordinator owns shared records and final build.
- Remaining iPad beta source and build scripts are committed in `46188af`
  under the user's instruction to commit everything. The macOS release
  artifacts are the previously qualified, byte-identical 3.0 build.
- Hosting target: existing 50day.io server, under mcchord.net, via AustinLand.

## Current phase

All five features are integrated. The OS 9.2.1/GXMetal 2.3.0 archive and catalog
are live on mcchord.net. The 3.0.0 app and DMG are signed, notarized, stapled,
and release-verified. App tests, host graphics/transport checks, public
download/import/boot/conformance, final media tests, and native/browser UI
qualification passed. Temporary UI machines were shut down and removed from
the library while retaining their evidence files.

The final game sweep completed 13 scenarios across 10 games with nonzero
accelerated direct frames, zero fallback, and zero queue/transport faults.
Historical HAVOC fixture/input-timing failures and corrected reruns are
retained. See [validation record](../3.0-validation.md) for artifact identities,
evidence, and precise scenario limits. There are no pending runtime changes.

Retained installer: `artifacts/ClassicMac-3.0.0.dmg` (44.2 MB). Feature work is
integrated; contained worktrees and test evidence remain for reference.

## Next action

Fast-forward GitHub main, tag `v3.0.0`, upload the verified DMG/ZIP/checksums,
and publish the GitHub release. Prior release `v2.3.2` remains the rollback
reference. Confirm remote artifact digests and latest-release selection, then
record the final publication state.
