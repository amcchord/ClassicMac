# ClassicMac 4.6.0 — published and verified

Stable Latest: [v4.6.0](https://github.com/amcchord/ClassicMac/releases/tag/v4.6.0),
tag/source `585b4189844703c4a7406f9953edb7e184118ba3`. PRs #36/#37 are merged.
Windows/Linux use native C++/Qt with measured Platinum pixels, single EXE/
AppImage packaging and all three classic base images. Electron is removed.

Exact final packages pass the Proxmox setup/UI/three-base lifecycle matrix
and pixel/inventory checks. Combined Swift/native/packaging validation passes.
All nine public assets and fresh anonymous downloads verify; the downloaded
Mac DMG passes full signature, staple, Gatekeeper and contents verification.
See docs/4.6.0-validation.md and docs/evidence/4.6.0.

Own QA scheduled task/RDP are gone; VMs170/171 are stopped. Task artifacts
remain in ignored worktree output. Existing user data, gxmetal and unrelated
worktrees are preserved. No catalog/DNS/hosting changes. v4.5 remains available
for rollback. No outstanding release action.
