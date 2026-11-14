# GitHub4.6.2 release in progress — 2026-11-13

User explicitly authorizes committing/pushing all RetroBoot/UI/Windows changes
and publishing an easily downloadable GitHub release. Owner:codex/github-4.6.2-release,
contained worktree worktrees/github-4.6.2-release. Product baselinee3e4133,
validated Windows binary source52b61d8. Root's unrelated gxmetal remains untouched.

Previous stable/latest isv4.6.1; v4.6.2 does not exist. The retained Windows EXE
has full five-DPI/default-config qualification and SHA256
b2f60d9fbb3536828121385d09dab76789c3a10321fc093a0d6e82e70647763a.
Mac packaging uses the qualified RetroBoot task engines and fresh Swift source.
Copland is unchanged and reused from the checksum-verified prior release.
Linux build will run from the exact release source in GitHub Actions.
CI Tools inputs now use the same verified4.6.1 Tools CD as Windows qualification.

Release order: push reviewed source; build/sign/notarize Mac; collect Linux and
corresponding sources; stage draft with notes/checksums; verify all assets; publish
stable/latest; anonymously download and compare hashes. Keep4.6.1 intact.
Rollback: before publication leave draft; after publication restore latest to
4.6.1 if needed, preserving tags/assets for traceability. No catalog/site deployment.

Next: finish Mac build/notary authentication, push frozen source and run Linux CI.
Keep VM401 stopped except if needed to archive its corresponding Windows source.
