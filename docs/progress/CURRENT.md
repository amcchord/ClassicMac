# ClassicMac 3.0.1 — easier setup and sparse 32 GB disks

## Active objective

Make the configured OS 9 download the recommended path inside New Machine.
Use a sparse, guest-usable 32 GB disk for the next hosted template, with clear
download/capacity/storage figures. Existing machines keep their disk sizes.

## Baseline and owners

- Published 3.0.0 and its assets remain immutable. Main is `b0828f0`.
- Root integration: `codex/3.0.1-onboarding`, wizard, defaults, combined
  validation, packaging, and authorized hosting update.
- Download agent: `codex/os9-32gb-template`, fresh filesystem capacity and
  clean template qualification in `output/3.0.1/template/`.
- Home agent: `codex/sparse-template-storage`, catalog storage metadata,
  sparse import budget, download UI, and packaging metadata.
- Guest graphics and emulator binaries remain the qualified 3.0 versions.

## Next safe action

Integrate the bounded changes, run app/storage checks, inspect the wizard,
and qualify the new template before publishing a new immutable archive and
promoting the catalog. Retain the current catalog for rollback. New GitHub
release publication is not part of the completed 3.0.0 action.
