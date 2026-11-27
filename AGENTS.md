# ClassicMac Repository Agent Guide

ClassicMac (`VERSION`: `4.10.1`) runs classic 68k Macintosh (Quadra 800), PowerPC Macintosh (Power Mac G3/G4 with optional ATI 3D OpenGL acceleration and Copland D11E4), and early PowerPC Mac OS X (10.4.11 Tiger) inside a classic Apple Platinum desktop interface.

## Repository Architecture

- `app/` — macOS native SwiftUI application (`ClassicMac.app`, target macOS 13.0+) plus bundled C target `CMachineArchive` (`sparse_tar.c` for GNU sparse 1.0 extraction with APFS `F_PUNCHHOLE`).
- `native/` — Shared C++/Qt6 native host runtime for Windows and Linux (`desktop/` builds Windows `.exe` / portable and Linux `.AppImage` / `.tar.gz`).
- `ios/` — iPadOS / iOS beta host frontend.
- `ati/` — ATI Rage 128 Pro & Radeon 7500/9200 (`qemu-system-ppc-ati`) FCode ROMs, OpenGL pass-through, and QEMU integration.
- `browser/` & `display/` — Optional loopback HTTP + WebSocket noVNC Browser Display (`BrowserDisplayServer`) and enhanced framebuffer notes.
- `copland/` & `osx/` — Dedicated Copland D11E4 QEMU support (`qemu-system-ppc-copland`) and PowerPC Mac OS X (Tiger) NVRAM / shutdown / VirtIO docs.
- `guestcd/` — Source manifest (`manifest.tsv`) and builder assets for `ClassicMac Tools.iso`.
- `scripts/` — Template preparation (`prepare-os9-template.py`) and GNU sparse 1.0 archive packaging (`package-machine-template.py`).
- `docs/manual/` — Full ClassicMac User Manual (`README.md`, `01`–`12`, `MAINTENANCE.md`, `scripts/check_docs.py`, `scripts/capture_manual_shots.py`).

## Mandatory User Manual & Documentation Maintenance

Every code, UI, schema, catalog, or subsystem change MUST keep the User Manual and README accurate:

1. **Consult the impact matrix first:** Check [`docs/manual/MAINTENANCE.md`](docs/manual/MAINTENANCE.md) whenever modifying Swift sources in `app/`, C++ sources in `native/`, `scripts/`, `ati/`, `browser/`, `copland/`, `osx/`, or `guestcd/`.
2. **Update matching chapters:**
   - Any new, renamed, or changed `config.json` key (`app/Sources/ClassicMac/VMConfig.swift` `CodingKeys` or `native/src/machine.cpp`), `template.json` field, or `CLASSICMAC_*` environment variable MUST be documented in [`docs/manual/12-configuration-and-cli-reference.md`](docs/manual/12-configuration-and-cli-reference.md) and its relevant feature chapter.
   - UI changes (First-Run wizard, Home window, Library, New Mac, Get Info panes, Media drawer, Checkpoints, Paste Text, Software Update, Icon Packs) require updating the corresponding chapter and refreshing screenshots via `docs/manual/scripts/capture_manual_shots.py`.
3. **Respect privacy & isolation when capturing screenshots:**
   - NEVER capture live developer homes, real personal `.classic` libraries, or personal clipboards.
   - `capture_manual_shots.py` isolates execution inside `HOME=/tmp/cm-manual-home` (`CFFIXED_USER_HOME`) using synthetic configs and `CLASSICMAC_UI_PASTE_SAMPLE`.
4. **Keep Mermaid diagrams GitHub-valid:**
   - Use only `flowchart TD`, `flowchart LR`, `sequenceDiagram`, or `stateDiagram-v2`.
   - Always quote node labels containing spaces or special characters (`id["Label (detail)"]`) and never put raw HTML tags inside Mermaid blocks.
5. **Validate before finishing:** Run BOTH checks before committing or concluding a task that touches documentation or user-facing config/UI:
   ```bash
   python3 docs/manual/scripts/check_docs.py
   swift test --package-path app
   ```

## Privacy & Git Hygiene

- Never commit `.tar.gz`, `.qcow2`, `.img`, `.iso`, ROM binaries, credentials, temporary staging directories (`output/`, `worktrees/`), or test homes (`/tmp/cm-manual-home`).
- Never write local developer home paths into docs, scripts, or screenshots.
- Preserve pre-existing local user edits cleanly when preparing commits.
