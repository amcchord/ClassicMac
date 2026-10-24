# Windows display follow-up complete — ClassicMac 4.6.1

Published stable Latest [v4.6.1](https://github.com/amcchord/ClassicMac/releases/tag/v4.6.1)
on October 24, 2026. Tagged source431e087; qualified native functional source
bf9a749. PR #38 merged. All nine fresh anonymous downloads match their checksums;
the downloaded Mac DMG passes full release verification.

Windows honors fractional DPI with Per-Monitor V2. Standard Power Macs change
resolution with the window; ATI and Quadra displays default to automatic fitting,
preserving the OS video mode and saved scale choices. Integral physical painting
preserves complete guest pixels. Dialog actions stay reachable and input releases
survive focus changes. The repeated-mode Power Mac NDRV fix ships on all hosts.

Final Windows tests pass49/32/16/18 steps and Linux46. All23 complete integral
guest frames and four Windows desktop captures have zero differing pixels. Both
package inventories pass. Root consolidated tests pass136 Swift cases (two
optional Copland skips), native rendering/model/RFB/input/export,15 packaging
regressions, mode/gamma contracts and syntax. Signed/notarized/stapled Mac passes.
See [validation](../4.6.1-validation.md) and [evidence](../evidence/4.6.1).

Restored task-only QA configs, removed own scheduled task, ended own RDP/Xvfb,
and verified VMs170/171 stopped. No temporary notary keys remain. User files,
root gxmetal and unrelated worktrees are preserved. No catalog/DNS/hosting change.
v4.6.0 remains available for rollback by promotion to Latest. No required work remains.
