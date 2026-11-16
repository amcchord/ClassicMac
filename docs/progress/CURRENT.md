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

Ubuntu VM is at 192.168.1.104 after switching to NetworkManager. GNOME/Xorg
1920x1440, software llvmpipe, idle lock disabled only for QA. 150-step real
visible UI flows pass at 1x/2x; 54 exact desktop matches. Offscreen 122 reference
gate passes; graphical focus wait was fixed and the actual desktop gate passes
with that source. New Linux CI 43780087685 from b4ef7a9 includes the focus fix.
Windows line-ending failure was fixed with source attributes plus checkout
configuration, preserving Copland byte exceptions. Initial corrected Windows
run 43778499815 is being superseded after its engine cache was populated.
Mac 4.6.3 is signed/notarized and full release verification passes. Engines
and firmware are unchanged from 4.6.2. Public System 7 and Mac OS 9 clean images
are imported under the VM's ClassicMacQA/public-library. Original images stay
stopped; every benchmark clones them. Initial System 7 proof passes (~7.4s,
45–56ms menus, ~59MiB frontend); final paired runs still required. Note busy
unpaused classic System 7 consumes one emulation core, as did 4.6.2.


Actual default ATI/Mac OS 9 on 4.6.2 Linux stalled at startup with Net, Tools,
sound and tablet ON (retained baseline capture at 60s). Reusing the Windows
slot-19 tablet fix on Linux boots the identical public image to Finder in
29.37s; real XTest menus 46–57ms. Source/Golden Linux-specific expectation added;
local Ubuntu build and all 122 desktop reference checks pass. No engine or
firmware change. New final CI packages must include this configuration fix.
The 150/150/40 final UI/public flows and 54 exact desktop matches already pass;
repeat against rebuilt AppImage, then measure three final OS9 boots and three
paired System 7 runs. Keep old failed OS9 run as evidence; baseline has no
valid OS9 performance value. Mac 4.6.3 remains signed/notarized and verified.
