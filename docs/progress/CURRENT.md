# Current work: ClassicMac 3.0

## Objective

Implement roadmap items 1 (downloadable configured OS 9), 4 (machine home),
7 (compact GXMetal status), 13 (paste text), and 10 (media drawer), then build
and validate the integrated macOS 3.0 candidate.

## Baseline and ownership

- Root integration checkout: `~/Development/ClassicMac`, branch
  `codex/3.0-integration`, starting at `f763917`.
- Feature agents work in contained `worktrees/3.0-*` checkouts and commit only
  their own scoped work. The coordinator owns shared records and final build.
- Existing uncommitted iPad work remains unrelated and must be preserved.
- Hosting target: existing 50day.io server, under mcchord.net, via AustinLand.

## Current phase

Feature implementation and hosting investigation. Three features can run
alongside the coordinator; the remaining two will start as slots free up.

## Next action

Dispatch download, GXMetal status, and paste-text agents; confirm hosting
paths and a suitable clean guest template while preparing integration.
