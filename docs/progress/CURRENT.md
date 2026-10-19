# ClassicMac 4.2.0 — final release checks

User authorized native classic Mac OS icons, running-VM Platinum fidelity,
merge and stable release; specifically requested Mac OS 9 Icon Services/alpha.
Task worktree native-platinum-icons, branch codex/native-platinum-icons, PR #31.
Baseline main ecdda03 / stable v4.1.1; unrelated root gxmetal remains untouched.

Native Mac OS 9 Icon Services supplies the original colour/alpha families.
32 native 16/32-pixel strikes export reproducibly; independent decode/alpha
checks pass. Native Appearance Manager full document frames, toolbar button
states, held widgets and grow box compare with zero differing pixels at 1×/2×.
The composed view preserves the grow corner above guest content. Real signed
Mac screenshots cover SuperDeluxe boot, narrow controls and WindowShade.

Mac app/DMG signed, notarized, stapled and fully verified. Linux packaged CI
39263262485 passes and all 54 packaged source/assets match. Windows native CI
39263257232 is compiling; its final package checks are required before release.
A private draft stages verified files; no stable v4.2.0 tag/publication yet.

Reference guest and screenshot guests stopped normally; task source mounts
detached. Notary private key existed only in a mode-600 temporary file, removed
after completion. User guests, VMs, worktrees, Library catalog and infrastructure
remain unchanged.

Next: finish Windows package/source freshness, merge PR #31, consolidated root
checks once, tag exact source and publish stable Latest with all ten assets.
Verify fresh public downloads and merge final release evidence. Retain 4.1.1
for rollback. See docs/4.2.0-validation.md for current results and scope.
