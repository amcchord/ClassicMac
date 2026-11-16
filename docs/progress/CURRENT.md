# ClassicMac 4.6.4 — published, 2026-11-15

[ClassicMac 4.6.4](https://github.com/amcchord/ClassicMac/releases/tag/v4.6.4)
is the stable/latest GitHub release. Release ID 446550995, published
2026-11-16 02:47:31 UTC. All nine uploaded files and full anonymous downloads
match the tested sizes and SHA-256 hashes; latest API/page both resolve 4.6.4.

RetroBoot 2.0.2 Radeon cursor correction is included, along with the Mac
startup fix for the default tablet/network/Tools configuration. Product source
918ae1dbf85dfebdddd7bced7247b959918215cf; release tag v4.6.4 points to
c0ebc1439c13d38573ed1daa00dc45fd105a5288. Later commits record publication only.
Source is integrated into main. Task branch: codex/retroboot-gpu-cursor.

All three platforms pass 598,030 exact cursor pixel comparisons each. The
previous published engine reproduces the corrected failure. Signed/notarized
Mac app boots to complete Finder and shuts down; physical Apple GPU GL smoke
passes. Windows and Ubuntu pass UI and actual Mac OS 9 edge/power flows on
Proxmox 192.168.1.100. Ubuntu paired performance remains comparable to 4.6.3;
one pair does not establish a speedup. Consolidated Swift, native and Python
checks pass. Details and limitations: [validation report](../4.6.4-validation.md).

Owned QA VMs 401/402 and Mac guests are stopped. Temporary Ubuntu auto-login
is disabled, Windows release task/RDP removed and private temporary keys
removed. Container 300 remains running. Root gxmetal, other VMs, guest catalog
and website are unchanged. Verified backup is retained; 4.6.3 is the rollback.

No blocker or remaining release work. Next: the next user-requested change.
