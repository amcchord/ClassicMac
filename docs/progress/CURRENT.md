# ClassicMac 4.6.3 — published, 2026-11-15

Stable/latest: https://github.com/amcchord/ClassicMac/releases/tag/v4.6.3
PR #44 merged; main and annotated v4.6.3 pushed. Tag:
c3cc9126d681174254550b5e72c3f252c95a47bc. Shipped product source: 36c1084;
subsequent changes are QA scripts, documentation and evidence.
All nine release assets match uploaded size/SHA-256 and independent anonymous
full downloads. Latest page/API resolve to 4.6.3. Prior 4.6.2 retained for rollback.

Ubuntu on requested Proxmox 192.168.1.100: 150/150/40 UI/network steps,
72 exact desktop matches, 8,582,784 exact guest pixels, nine clean runtime runs.
Linux default ATI/tablet/network/Tools startup stall fixed. Median Finder:
System 7 8.309 s vs old 8.447 s; OS 9 29.678 s. Interface 59–65 MiB,
0.17–0.5% of one core; paused engine median 0%. Final Windows EXE passes
150-step flows at 1x/2x; 50/54 exact desktop captures, four OneDrive-obstructed
frames retained. Mac signing/notarization/stapling/full verification and actual
Quadra smoke pass. Consolidated 143 Swift tests (3 optional skips), 122 native
references/672 glyphs and 11 Python tests pass. Final package CI:
Linux 43782609513, Windows 43782612504.

Report: docs/ubuntu-validation-2026-11-15.md; evidence: docs/evidence/4.6.3.
Software graphics/Xorg do not qualify physical GPU/3D or every UI state.
The earlier UI audit's remaining appearance/layout/physical-device gaps remain.
QA VMs 401/402 stopped; Ubuntu auto-login disabled; Windows task/RDP removed;
Mac guests stopped; private temporary keys removed. Container 300 still running.
Root gxmetal untouched; no guest catalog/site/DNS or unrelated VM changes.

Requested release work is complete. Next action: the next requested change.
Retain stopped QA systems and qualified 4.6.2 rollback.
