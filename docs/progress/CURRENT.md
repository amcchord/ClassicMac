# ClassicMac 4.6.3 / Ubuntu validation — 2026-11-15

User requests incorporation of recent PR improvements, a new GitHub release,
and actual Ubuntu Linux performance testing on Proxmox 192.168.1.100.
The intended recent change is ClassicMac PR #44 (dbf3c9f), Library and machine
management UX shared by Windows/Linux. RetroBoot's previous import is current.

Owner: codex/github-4.6.3-ubuntu in worktrees/github-4.6.3-ubuntu. Root/main
baseline 5e68920; previous stable v4.6.2 retained. Root gxmetal untouched.
PR #44 fast-forwarded into the task branch; reviewing before release integration.

Provisioning dedicated Ubuntu 24.04 VM 402 (6 vCPU, 8 GiB, sparse NVMe disk)
on the requested host. Official image SHA256 verified. Other VMs/containers
untouched. Test actual AppImage/GNOME, new 150-step UX flow at 1x/2x, public
Library/download/real guests, and repeatable 4.6.2 vs 4.6.3 performance measures.
Record host load, CPU/memory, boot and input/UI timing; do not claim physical GPU
results from a virtual display. Fix discovered regressions before publication.

Next: finish Ubuntu provisioning and PR review, bump shipping versions/build
all packages, test final Linux package and compare baseline, consolidate checks,
commit/push/tag, publish release and verify anonymous downloads. Stop owned
QA guests and VM afterward; retain qualified v4.6.2 rollback. No catalog/site deploy.
