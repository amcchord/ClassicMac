# ClassicMac 3.0.1 candidate ready

## Current state

New Machine recommends downloading ready-to-run Mac OS 9 and retains manual
setup in the same wizard. New Power Mac disks default to sparse 32 GB disks;
existing machines keep their current capacities.

The new 32 GiB OS 9 image is live on mcchord.net: 82.8 MB to download and about
125 MB initially on an APFS host. Mac OS 9 reports 31.99 GB capacity and about
31.28 GB free. The prior archive/catalog are retained for rollback.

The final macOS candidate is signed, Apple-notarized, stapled, and verified:
`artifacts/ClassicMac-3.0.1.dmg` (44.2 MB). A ZIP and checksums are retained
alongside it. Runtime source ends at `fb8d629`; later changes record validation
and hosting. All completed source and documentation are now pushed to GitHub
`main`, `codex/3.0-integration`, and `codex/3.0.1-onboarding`. The root checkout
is on `main`.

## Verification and limits

102 app tests, three packaging tests, three template helper tests, real public
wizard download/import, exact disk identity/allocation, Finder startup and
clean shutdown passed. The template passed Tools-on/off boot/input and full
GXMetal conformance. Emulator/guest sources are unchanged; the 3.0 full game
sweep was not repeated. Physical macOS 15 runtime remains untested.

All test emulators are stopped, temporary library entries removed, and the
three original entries preserved. See [validation](../3.0.1-validation.md),
[release notes](../releases/3.0.1.md), and
[hosting/rollback](../operations/classicmac-downloads.md).

## Next safe action

Use the signed 3.0.1 candidate. GitHub source is synchronized, while 3.0.0
remains the public release. Publish the candidate as a new release if
requested. Do not alter 3.0.0's published assets.
