# Windows validation complete — 2026-11-13

Local main includes the RetroBoot 2.0.1/UI import 88ca9e4 and Windows fixes
through 52b61d8. Task branch codex/windows-proxmox-qa and contained worktree
worktrees/windows-proxmox-qa are retained. Root untracked gxmetal is preserved.

Windows 11 Pro was built/tested on dedicated VM 401 on local Proxmox n5
(192.168.1.100). Final portable package passes 59 steps at all five actual
Windows DPI settings 100/125/150/175/200%, with native accented text input,
guest mouse input, full screen, restart process replacement and shutdown.
Sound/networking/Tools CD/tablet are enabled. DirectSound initializes.
75 desktop comparisons and 30 raw guest captures/19,201,136 visible pixels
compare exactly; one caret-blink transition uses a documented static recapture.
122 independent component comparisons and 672 glyphs/advances pass on Windows.

A preexisting ATI/Sungem/Virtio interrupt conflict was reproduced with the old
Windows engine and fixed by separating the affected tablet interrupt. Quadra
built-in/enhanced and Standard Mac OS 8.6 model flows also pass; 17 extra desktop
captures and 15,979,812 raw guest pixels pass. Code, package/source hashes,
remaining limits and sanitized evidence are in the [Windows report](../windows-proxmox-validation-2026-11-13.md).

Final integration checks pass: 138 Swift tests, 3 optional skips, no failures;
native self-test; 11 Python unit tests; deterministic exports/syntax; 7 Screamer
Unix checks. Windows TCP Screamer 7 checks and Mac TCP checks also pass.

VM 401 is retained STOPPED; existing RetroBoot container 300 remains running.
Its last Windows address is 192.168.1.131. Administrator credentials are in
AustinLand entry ClassicMac-Windows-QA-n5-401. Autologon/password caching,
owned scheduled jobs, setup HTTP/RDP processes and unattended credential
XML/ISO files are cleared. Original Mac disks and older lab VMs are untouched.
The existing Windows key failed activation with 0xC004C020; activation remains
unresolved. No secret values are committed.

Local test EXE: artifacts/windows-proxmox-2026-11-13/ClassicMac-4.6.2-windows-x64.exe
SHA256 b2f60d9fbb3536828121385d09dab76789c3a10321fc093a0d6e82e70647763a.
Earlier package and all full/failed capture sets remain in contained artifacts/output.
No push, tag, release or public upload was performed or authorized.

Next safe action: qualify a physical Windows GPU/audio endpoint, mixed-DPI
monitor moves and wider keyboard/IME cases. The macOS layout/scaling gaps
and remaining appearance variants in the [UI audit](../ui-fidelity-audit-2026-11.md)
remain open. This evidence certifies measured/captured states, not every UI
state, audible sound, accelerated 3D, guest internet transfer or performance.
