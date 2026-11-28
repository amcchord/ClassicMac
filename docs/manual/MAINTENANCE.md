# ClassicMac User Manual Maintenance Guide

This guide governs how **ClassicMac** keeps its documentation accurate, visual, private, and automatically verified across releases. Both human contributors and AI coding agents must follow this contract whenever changes affect ClassicMac's UI, configuration schema, emulation flags, catalogs, packaging scripts, or subsystems.

## Mandatory Update Triggers (Code-to-Manual Impact Matrix)

| Source Area Changed | Primary Manual Chapters to Update | Secondary Checks |
| :--- | :--- | :--- |
| `VERSION`, `README.md`, host OS support (`app/Package.swift`, `native/CMakeLists.txt`, `ios/`, `desktop/`) | [01-system-requirements.md](01-system-requirements.md), [README.md](README.md), root `README.md` | Check release download table & supported targets |
| `app/Sources/ClassicMac/AppPaths.swift`, `QEMURunner.swift`, `OSXNVRAM.swift`, `OSXShutdown.swift`, `g5/`, `native/src/engine.cpp` | [02-architecture-how-it-works.md](02-architecture-how-it-works.md), [11-checkpoints-updates-and-troubleshooting.md](11-checkpoints-updates-and-troubleshooting.md), [12-configuration-and-cli-reference.md](12-configuration-and-cli-reference.md) | Update Mermaid boot/socket/NVRAM diagrams if flow changed |
| `FirstMacSetupView.swift`, `HomeView.swift`, `AppMain.swift`, WindowShade / Platinum styling | [03-installation-and-first-run.md](03-installation-and-first-run.md) | Re-capture `first-run-welcome.png`, `main-home-*.png`, `windowshade.png` |
| `MachineCatalog.swift`, `LibraryEditions.swift`, `MachineDownloadModel.swift`, `MachineTemplateInstaller.swift`, `sparse_tar.c`, `native/src/catalog.cpp` | [04-mac-os-library-and-editions.md](04-mac-os-library-and-editions.md), [10-creating-and-serving-images.md](10-creating-and-serving-images.md) | Re-capture `library-410.png` if edition cards or filters changed |
| `NewMacDialog.swift`, `SettingsDialog.swift` (General / Delete alert) | [05-installing-your-own-mac-os.md](05-installing-your-own-mac-os.md) | Re-capture `new-mac-dialog.png`, `get-info-general.png`, `delete-mac-dialog.png` |
| `GuestScreenView.swift`, `GuestWindow.swift`, `PasteTextController.swift`, `docs/paste-text.md` | [06-everyday-use-and-input.md](06-everyday-use-and-input.md) | Re-capture `paste-text-dialog.png` / `shutdown-dialog.png` if dialog UI changed |
| `ati/`, `display/`, `browser/`, `BrowserDisplayServer.swift`, Display pane in `SettingsDialog.swift` | [07-display-3d-graphics-and-sound.md](07-display-3d-graphics-and-sound.md) | Re-capture `get-info-display.png`; verify Browser Display token/port docs |
| `docs/media-drawer.md`, `guestcd/manifest.tsv`, Media drawer / Sharing pane (`SettingsDialog.swift`) | [08-media-tools-and-sharing.md](08-media-tools-and-sharing.md) | Re-capture `media-*.png` or `get-info-sharing.png` |
| `PrintServer.swift`, `HostPrinterManager.swift`, `GuestPrinterProvisioner.swift`, `native/src/printer.cpp`, SLIRP args | [09-networking-and-virtual-printing.md](09-networking-and-virtual-printing.md) | Re-capture `get-info-printing.png`; verify protocols/ports (`515`/`631`/`9100`) |
| `scripts/prepare-os9-template.py`, `scripts/package-machine-template.py`, `docs/downloadable-machines.md`, `docs/operations/classicmac-downloads.md` | [10-creating-and-serving-images.md](10-creating-and-serving-images.md), [12-configuration-and-cli-reference.md](12-configuration-and-cli-reference.md) | Verify `catalog-v2.json` and `ThinArchive` field names match Swift & C++ |
| `VMCheckpoint.swift`, `CheckpointsDialog.swift`, `AppUpdater.swift`, `UpdateDialog.swift`, `native/src/checkpoint.cpp`, `native/src/updater.cpp` | [11-checkpoints-updates-and-troubleshooting.md](11-checkpoints-updates-and-troubleshooting.md) | Re-capture `checkpoints-dialog.png` / `software-update.png` |
| `VMConfig.swift` (`CodingKeys`, defaults, `sanitize()`, `relaunchReasons`), `native/src/machine.cpp`, `DebugSnapshots.swift` | [12-configuration-and-cli-reference.md](12-configuration-and-cli-reference.md) | `check_docs.py` fails automatically if any `CodingKeys` key is missing from Ch. 12 |

## Documentation & Asset Standards

### 1. Privacy & Environment Isolation
- Never expose real local developer home directories, usernames, hostnames, clipboard contents, or private machine collections in text or screenshots.
- Always generate screenshots using `docs/manual/scripts/capture_manual_shots.py`, which runs ClassicMac under an isolated `HOME=/tmp/cm-manual-home` (`CFFIXED_USER_HOME`) with synthetic sample text (`CLASSICMAC_UI_PASTE_SAMPLE`) and update checks disabled (`CLASSICMAC_DISABLE_UPDATE_CHECK=1`).
- In Prose/Tables, write generic host paths such as `~/Documents/ClassicMac`, `/tmp/cm-manual-home`, or `%USERPROFILE%\Documents\ClassicMac`.

### 2. Mermaid Diagram Rules (GitHub Compatibility)
GitHub's Mermaid renderer strictly enforces syntax:
- Use only supported diagram types: `flowchart TD`, `flowchart LR`, `sequenceDiagram`, or `stateDiagram-v2`.
- Always quote node labels containing spaces, parentheses, brackets, colons, slashes, dots, ampersands, commas, or hyphens (for example `A["Print Server (LPD 515 / IPP 631)"]`).
- Never embed raw HTML tags (`<br>`, `<b>`, `<code>`, `<i>`, `<small>`) inside Mermaid labels; use concise multi-word labels or nested subgraphs instead.

### 3. Screenshot & Image Standards
- Store manual screenshots in `docs/screenshots/manual/` (`docs/screenshots/platinum/` and `docs/screenshots/` hold `README.md` showcase images).
- Always render images at readable width (`<= 840` px in `<img src="..." width="760">`, or `width="390"`–`410"` when pairing side-by-side).
- Whenever regenerating screenshots, inspect every modified PNG visually to verify active window title bars, populated UI controls, crisp guest desktops (not mid-boot watch cursors or blank frames), and clean proportions.
- Never commit `.tar.gz`, `.qcow2`, `.img`, `.iso`, ROM files, temporary build output (`output/`), or `/tmp/cm-manual-home`.

## Validation Workflow

Run the automated documentation verifier after any documentation or schema change:

```bash
python3 docs/manual/scripts/check_docs.py
```

`check_docs.py` verifies:
1. All Markdown links (`README.md`, `docs/manual/*.md`, and root `README.md` / `AGENTS.md` / `CLAUDE.md`) resolve to real files and valid `#heading` anchors.
2. Every `<img src="...">` and Markdown image link resolves to an existing file on disk, uses `width <= 840` when specified, and checks for leaked personal usernames/paths.
3. Every Mermaid code block uses supported diagram types, avoids raw HTML tags, and quotes special-character labels.
4. Every `CodingKeys` property in `app/Sources/ClassicMac/VMConfig.swift` (plus `swapAltCommand` in `native/src/machine.cpp`) is documented by name in [12-configuration-and-cli-reference.md](12-configuration-and-cli-reference.md).

## Regenerating Screenshots

When UI dialogs, Platinum chrome, or guest desktops change:

```bash
swift build --package-path app -c release
# Stage a disposable copy of ClassicMac.app with the updated binary, then:
python3 docs/manual/scripts/capture_manual_shots.py --app output/manual-shots-work/ClassicMac.app
# Re-validate after inspecting the captured PNGs:
python3 docs/manual/scripts/check_docs.py
# Remove temporary staging and isolated test home before committing:
rm -rf output/manual-shots-work /tmp/cm-manual-home
```
