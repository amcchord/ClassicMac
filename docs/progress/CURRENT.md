# ClassicMac 3.0 released

## Current state

[ClassicMac 3.0.0](https://github.com/amcchord/ClassicMac/releases/tag/v3.0.0)
is public and selected as the latest GitHub release. The annotated `v3.0.0`
tag points to `2dc6e459d6f58716e839018441d64a0b946096cd`. All macOS feature
work and the remaining iPad beta source/scripts are committed and pushed.
The root checkout is on `main`; the integration branch is kept in sync.

Release assets are `ClassicMac.dmg`, `ClassicMac.zip`, and `SHA256SUMS.txt`.
The app and DMG are Developer ID signed, Apple-notarized, stapled, and accepted
by Gatekeeper. GitHub asset digests and a fresh download of all three files
match the qualified local originals. The retained local installer is
`artifacts/ClassicMac-3.0.0.dmg` (44.2 MB).

The configured Mac OS 9.2.1/GXMetal 2.3.0 download remains live on mcchord.net.
The existing Applications installation is unchanged. The iPad source is
included in Git; this release's downloadable binaries are macOS only.

## Validation and records

97 app tests, GXMetal host/guest conformance, 18 final media checks, public
OS 9 download/import/boot, native/browser UI, and 13 bounded game scenarios
passed. Detailed coverage, source/artifact identities, prior failed fixture
attempts, and known limits are in the [validation record](../3.0-validation.md).
Publication evidence is retained in `output/3.0/publication/`.

All test machines stopped; temporary UI entries were removed while retaining
evidence. Contained feature worktrees remain for reference.

## Next safe action

Use the published release and choose further work from the
[numbered roadmap](../polish-roadmap.md). No release work is pending.
`v2.3.2` and its assets remain the rollback reference; fix later issues through
new commits and a new release rather than changing the published 3.0 tag/assets.
