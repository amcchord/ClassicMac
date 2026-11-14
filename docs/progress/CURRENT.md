# GitHub 4.6.2 release — 2026-11-13

All product fixes are on GitHub main through 451ce59. User authorizes publication.
Owner: codex/github-4.6.2-release; contained worktree; unrelated gxmetal untouched.

Mac blank Quadra and white startup flash fixed. Final signed app reaches visible
Finder on built-in/enhanced Quadra and Power Mac; first revealed frames contain
Happy Mac. Apple agreement resolved; final app/DMG notarized and full release
verification passes. Consolidated main: 143 Swift tests, 3 optional skips,
0 failures; 11 Python tests; Mac/Linux shared-display test 3,148,800 exact pixels.
Linux CI 43558821440 succeeds. Final Windows portable rebuild passes 33 Quadra
steps, 6 exact desktop captures and 3,646,080 guest pixels; frontend/Power Mac
payloads identical to prior five-DPI/default-device qualification. Both native
reference gates pass 122 comparisons and 672 glyphs.

Eight qualified downloads/source archives plus SHA256SUMS staged in task output.
Matching DAFB source verified in all three platform source archives. Evidence
and release notes prepared. Proxmox VM 401 stopped; container 300 running;
owned RDP/task/guest processes stopped. Original machines preserved.

Next: commit evidence, tag final source, create GitHub draft, verify all uploaded
assets, publish stable/latest, anonymously redownload and compare every hash.
Previous latest v4.6.1 retained. If publication verification fails, restore it
as latest while preserving tags/assets. No catalog/site deployment.
