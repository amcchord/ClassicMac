# ClassicMac 4.6.2 published — 2026-11-14

User-authorized GitHub release is complete. Stable/latest v4.6.2:
https://github.com/amcchord/ClassicMac/releases/tag/v4.6.2
Tag commit: a98eb9949b564d7e97834c144d885cfb0b7bd14d; product code: 451ce59.
All RetroBoot/UI/Windows/Mac fixes, audit and evidence are committed on main.

Nine release assets (Mac DMG/ZIP, Windows EXE, Linux AppImage, four corresponding
source archives/checksums) were uploaded and independently downloaded without
credentials. Every SHA-256 matches; public latest redirect resolves to v4.6.2.
Verification: docs/evidence/4.6.2/public-downloads.json. v4.6.1 remains available.

Mac Quadra blank-window bug and white startup flash fixed. Final signed/notarized
app reveals Happy Mac and reaches visible Finder on built-in/enhanced Quadra
and Power Mac. 143 Swift tests, three optional skips, zero failures; 11 Python
checks. Mac/Linux compare 3,148,800 shared RGB pixels exactly. Linux CI passes.
Final Windows Quadra 33-step flow, six desktop comparisons and 3,646,080 guest
pixels pass. Unchanged frontend/Power Mac payloads retain five-DPI qualification.
Both native reference gates pass 122 comparisons and 672 glyphs.

All task guests/RDP/jobs stopped; dedicated Proxmox VM 401 stopped, container
300 running. Original user machines and unrelated root gxmetal preserved.
Only GitHub source/tag/release published; no catalog/site deployment.
Remaining fidelity/hardware limits are documented in the audit and release
notes. No release work remains; continue with the next requested change.
