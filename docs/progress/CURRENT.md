# ClassicMac 4.6.4 — qualified for publication, 2026-11-15

User requests RetroBoot's new GPU fix. Source 88d074a (2.0.2 cursor clipping)
is ported in 65b0c53; 918ae1d also corrects Mac default tablet/network IRQ
allocation and qualification pixel-center bounds. Final product source:
918ae1dbf85dfebdddd7bced7247b959918215cf. Later edits are recipes/docs/evidence.
Owner: codex/retroboot-gpu-cursor in worktrees/retroboot-gpu-cursor.

Final Linux CI 43790212460 and Windows CI 43790215360 pass. Each platform's
cursor gate checks 144 cases and 598,030 exact RGB pixels. The published
4.6.3 engine fails the old clipped-left case. Source pairs match across all
three corresponding QEMU archives. Mac physical CGL smoke passes; final app
signed/notarized/stapled/full verification and actual complete Finder/stop pass.
Mac default IRQ control: old automatic slot18 stalls65s; slot19 boots24.44s.

Ubuntu: 150+150 UI steps, 44 guest edge/power steps, 67 exact desktop matches,
10,773,120 exact guest pixels. Windows: 150+44 steps, normal process/task exit0,
39 exact desktop matches and 8,957,952 exact guest pixels. All selected devices
remain on. Final quiet follow-up: 4.6.3/4.6.4 Finder28.40/28.83s, input46.17/45.67ms,
paused0%; one pair, not a speedup claim. Initial deadline/viewport/caret probes
retained separately. Full report: docs/4.6.4-validation.md and evidence/4.6.4.
Consolidated144Swift tests (3 optional skips),122native refs/672glyphs,11Python pass.

Windows VM401 stopped and task/RDP removed. Ubuntu VM 402 stopped and
temporary auto-login disabled. Mac guests stopped.
All eight binary/source assets plus SHA256SUMS staged in contained output;
complete Git backup verified. Stable/latest is still4.6.3 until publication.
Next: commit evidence, push source/tag, upload/verify draft, fast-forward main,
publish4.6.4/latest and verify nine anonymous downloads. Root gxmetal preserved;
no public catalog/site, unrelated VM or cleanup work.
