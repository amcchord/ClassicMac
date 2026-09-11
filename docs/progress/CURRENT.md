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

Integration and runtime qualification. Download, GXMetal status, and paste
text are committed on the integration branch. Home and media are finishing
their independent checks. The existing static hosting location is verified.

## Next action

Connect the final UI hooks, rebuild both emulation engines, and validate the
exact 3.0 app. Finish the fresh OS 9 filesystem's boot qualification before
publishing its immutable archive and catalog. Run the app, host graphics,
transport, guest conformance, media/paste/status, and packaging checks, then
record any remaining limits.
