# ClassicMac Claude / Agent Project Instructions

Refer to [`AGENTS.md`](AGENTS.md) and [`docs/manual/MAINTENANCE.md`](docs/manual/MAINTENANCE.md) for full project rules. Key non-negotiable rules:

1. **Keep the User Manual synchronized:** Whenever you change UI views, QEMU launch arguments, `VMConfig.swift` / `native/src/machine.cpp` config keys, virtual printing, shared folders, Library/catalog schemas, checkpoints, updater logic, or template packaging scripts, update the affected chapters in [`docs/manual/`](docs/manual/README.md).
2. **Run automated doc checks & tests:** Before completing work that changes user-facing code or docs, verify:
   ```bash
   python3 docs/manual/scripts/check_docs.py
   swift test --package-path app
   ```
3. **Never commit binary VM images, ROMs, or scratch directories (`output/`), and never leak real local user paths into docs or screenshots.**
