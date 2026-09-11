# ClassicMac polish roadmap and 3.0 selection

## Review baseline

The September 2026 review covered the installed macOS interface, current source,
release notes, and GitHub issues. The repository is
<https://github.com/amcchord/ClassicMac>, with the primary checkout at
`~/Development/ClassicMac`. At review time, local main and GitHub main both
pointed to `f763917` (2.3.2). The installed Applications copy was 2.3.1.
Uncommitted iPad files, two iPad scripts, and related edits to README,
CHANGELOG, and THIRD_PARTY_NOTICES are separate existing work.

## Ideas

Effort is relative, not a delivery estimate. Original numbering is retained
so conversation references remain useful.

| # | Improvement | Intended experience | Effort | 3.0 |
| --- | --- | --- | --- | --- |
| 1 | Ready-to-run OS 9 download | Download Mac OS 9 with GXMetal, choose a name, and start; skip guest installation. | Medium–large | Selected |
| 2 | Safer shutdown and restart | Show shutdown progress and offer Keep Waiting or Force Power Off. Replace the current automatic 15-second forced-stop fallback and clarify hardware reset. | Medium | Deferred |
| 3 | Keep missing machines visible | Offer Locate Machine when a drive is unplugged or a package moves, instead of silently dropping the library entry. | Small–medium | Deferred |
| 4 | Cleaner machine home | Lead with a desktop preview, Start, OS identity, and useful actions; move the long configuration form into Settings. | Medium | Selected |
| 5 | Easy file transfer | Ready-made Shared Files folder, Open Shared Folder, and file drops that preserve classic Mac metadata. | Medium | Deferred |
| 6 | Duplicate and restore points | Duplicate Mac and disk/settings restore points while shut down; consider running-state saves separately. | Medium | Deferred |
| 7 | Visible GXMetal status | Compact status indicator, installed/active/update/attention details, and access to guest testing or repair guidance. | Medium–large | Selected |
| 8 | Built-in update checking | Check for Updates, release notes, and eventually installation after guests shut down. | Small–medium | Deferred |
| 9 | OS-specific presets | Choose OS 9 gaming, OS 8.6, or System 7; apply sensible hardware/input settings and explain blank-machine setup. | Medium | Deferred |
| 10 | Consistent media drawer | Inserted and recent discs, startup selection, and clear live-change versus next-start behavior. | Medium | Selected |
| 11 | Better previews/screenshots | Retain useful desktop/game frames instead of shutdown-gray previews; Save/Copy Screenshot. | Small–medium | Deferred |
| 12 | Clear progress/recovery | Distinguish startup/restart/shutdown states; make transfers cancellable and retryable with actionable errors. | Medium | Deferred except as needed for #1 |
| 13 | Paste text into guest | An explicit Paste Text into Mac command; full two-way clipboard is a later feature. | Medium–large | Selected |
| 14 | Practical compatibility guide | Searchable game cards with tested versions, settings, and known quirks drawn from existing evidence. | Small–medium | Deferred |
| 15 | Useful storage information | Actual host consumption versus virtual capacity, low-space warnings, and unused copied installer discovery. | Medium | Deferred except as needed for #1 |

## Accepted 3.0 direction

- Implement **1, 4, 7, 13, and 10** with parallel feature agents, then integrate
  into one 3.0 candidate and run the combined validation suite.
- Host the OS 9 files on the existing **50day.io server under mcchord.net**;
  AustinLand is the infrastructure source of truth. This replaces the initial
  Cloudflare R2 suggestion.
- Keep the GXMetal indicator small: an icon in the window controls with a
  menu for details, rather than a large dashboard panel.
- Keep unrelated iPad work intact and outside the macOS feature commits.

## Download design

Start with one tested Mac OS 9 template, preferably 9.2.2, containing matching
GXMetal components and working display/input defaults. Deliver a compressed
portable `.classic` machine through a versioned HTTPS catalog.

Flow: **Download Mac OS 9 → Name your Mac → Start**.

Each import needs a fresh machine identity. Remove source-machine shared-folder
and installer paths, personal data, credentials, and development/test content.
Show transfer progress and disk-space requirements, permit cancellation/retry,
verify integrity, and avoid partially imported library entries. Template
updates create a new machine instead of replacing an existing user's disk.
Retain the manual-install path.

The original review identified redistribution rights as a prerequisite for
public OS/software distribution. Apple's published OS 9 license is at
<https://www.apple.com/shop/Catalog/US/Images/MacOS9.htm>. Building a configured
image locally from user-supplied media remains an alternative.

## Existing follow-up

GitHub issue #11 (<https://github.com/amcchord/ClassicMac/issues/11>) reports
OS 8.5/8.6 pointer-motion problems in 1.6.1. It was still open during review;
its applicability to the current release was not established by that review.

## Implementation record

See [current state](progress/CURRENT.md) and [session journal](progress/JOURNAL.md)
for ownership, integration, test evidence, and remaining work.
