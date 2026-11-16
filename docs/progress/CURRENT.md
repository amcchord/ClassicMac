# ClassicMac 4.6.3 — ready to publish, 2026-11-15

PR #44 is incorporated on codex/github-4.6.3-ubuntu. Shipped product source is
36c1084; subsequent changes are QA runners/docs/evidence. All three final
packages are staged with hashes; Mac is signed/notarized/stapled and verified.

Ubuntu VM 402 on requested Proxmox 192.168.1.100 passes 150/150/40 UI/network
steps, 72 exact desktop matches, 8,582,784 exact guest pixels and nine repeated
performance runs. Linux ATI default startup is fixed by separating the tablet
interrupt. Median Finder: System 7 8.309 s vs old 8.447 s; OS 9 29.678 s.
Detailed method, raw samples and limits: docs/ubuntu-validation-2026-11-15.md.
Final Windows EXE passes 150-step flows at 1x/2x on VM 401; 50/54 desktop
comparisons exact, four blocked by OneDrive. No masking. Both QA VMs are now
stopped; temporary Ubuntu auto-login disabled and Windows task removed.

Root main integrated through 6203100 before evidence. Consolidated validation:
143 Swift tests (3 optional skips), 122 native comparisons/672 glyphs and
11 Python tests pass. Final Linux CI 43782609513/Windows CI 43782612504 green.
The UI audit's remaining physical-device/appearance/layout gaps stay explicit.

Next: commit evidence, fast-forward/push main and annotated v4.6.3, upload all
nine draft assets, verify upload hashes, publish stable/latest and redownload
all anonymously. Verified git backup bundle in contained output; retain v4.6.2
for rollback. Root gxmetal untouched; no public catalog/site or other VM changes.
