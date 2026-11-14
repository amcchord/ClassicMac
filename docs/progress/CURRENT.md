# GitHub 4.6.2 release — 2026-11-13

User authorizes pushing all RetroBoot/UI/Windows work and publishing downloads.
Owner: codex/github-4.6.2-release in worktrees/github-4.6.2-release. Root main
and GitHub include the earlier work through 733be36. Unrelated gxmetal is untouched.
Previous stable/latest v4.6.1 is retained; no v4.6.2 tag/release yet.

Mac release smoke reproduced the user's booted-but-blank Quadra window. The
built-in DAFB draws into a same-size 640×480 QEMU placeholder without retiring
its flag; the shared backend suppresses those pixels. A focused device fix
replaces that surface. The new ROM-free qtest fails on the old engine and
passes 3,148,800 exact RGB pixels across palette changes, reconnect and reset
at 640×480/800×600. The rebuilt signed app visibly reaches System 7.5.5 Finder.

User then observed a brief white startup flash. The curtain now waits for
boot content rather than a uniform white/gray frame or empty ROM checkerboard,
and draws the latest guest frame before starting the fade. Five focused tests
pass. Final app smoke and full validation remain.

Apple agreement is resolved; notarization and full release verification pass
for the display-fix candidate. Rebuild/notarize again with the curtain fix.
Linux CI 43557050723 passed before the DAFB change; rebuild from final inputs.
Windows VM 401 is running for the incremental DAFB rebuild and Quadra retest;
all earlier five-DPI qualification is retained. Stop it after collecting results.

Next: finish final Mac/Windows checks, build Linux, commit evidence, stage the
draft with source archives/checksums, publish stable/latest, verify anonymous
downloads. Rollback: retain v4.6.1 and restore it as latest if verification fails.
GitHub repository/release only; no catalog/site deployment.
