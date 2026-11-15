# Windows desktop UX pass — November 15, 2026

The Library now has a searchable edition list, system filters and a separate
details pane. It shows the newest revision of each edition, prioritizes usable
systems, explains compatibility restrictions and displays download and disk
sizes. Users can name their Mac before downloading, pause and resume with
visible byte counts, and start a newly installed Mac directly.

Home keeps Library, New Mac and Open Mac in one toolbar. Narrow windows stack
the machine list and a smaller preview so settings, media, management and
startup remain visible. The machine list preserves its position when status
changes and supports right-click actions and keyboard shortcuts.

Manage makes removal explicit: Remove from List preserves the machine folder;
Delete Files moves a validated `.classic` folder to Recycle Bin after a second
confirmation. Both choices are blocked while the Mac is running. Destructive
confirmations give Cancel the keyboard focus and default action.

Power is available on Home, the guest toolbar, Manage, and when closing a
running guest. It distinguishes graceful shutdown/restart from Force Stop.
Force Stop kills only the owned engine process without a QMP timeout, including
while an earlier shutdown request is waiting. Quit provides the same recovery
for multiple running Macs.

## Screenshots

All images are application captures from this Windows desktop. The `1x` and
`2x` galleries use isolated qualification fixtures; their mock guest is visibly
labeled. The network gallery uses the public Library. The real-guest gallery
shows the downloaded System 7.5.5 image running with the existing 4.6.2 engine.

| Flow | Before | After |
| --- | --- | --- |
| Home | [Original home](screenshots/windows-ux/before/home.png) | [Home](screenshots/windows-ux/1x/09-home-downloaded.png), [narrow home](screenshots/windows-ux/1x/09b-home-narrow.png) |
| Library | [Original Library](screenshots/windows-ux/before/library.png) | [Public Library](screenshots/windows-ux/network/01-public-library.png), [narrow Library](screenshots/windows-ux/1x/02b-library-narrow.png) |
| Download | — | [Progress](screenshots/windows-ux/network/02-download-progress.png), [paused](screenshots/windows-ux/network/03-download-paused.png), [resumed](screenshots/windows-ux/network/04-download-resumed.png), [ready to start](screenshots/windows-ux/1x/08-library-download-complete.png) |
| Machine removal | — | [Manage](screenshots/windows-ux/1x/10-machine-management.png), [keep or delete](screenshots/windows-ux/1x/11-machine-remove-options.png), [Recycle Bin confirmation](screenshots/windows-ux/1x/20-recycle-machine-warning.png) |
| Hung Mac | — | [Power](screenshots/windows-ux/1x/15-paused-mac-power.png), [force-stop confirmation](screenshots/windows-ux/1x/16-force-stop-warning.png), [shutdown still waiting](screenshots/windows-ux/1x/19b-shutdown-waiting-force-available.png) |
| Real guest | — | [System 7.5.5 Finder](screenshots/windows-ux/real-guest/system7-running.png), [close opens Power](screenshots/windows-ux/real-guest/system7-close-power.png) |

The complete [1× gallery](screenshots/windows-ux/1x) and
[2× gallery](screenshots/windows-ux/2x) contain 27 captures each.

## Validation

Local Windows desktop: 144 DPI / 150% OS display scale, Per-Monitor V2 aware.
The test harness separately forces exact 1× and 2× interface zoom, asserting the
resulting device-pixel ratio. It does not change Windows display settings.

- Release C++17 / Qt build completed with UCRT64 GCC and Qt 6.
- Both isolated UI flows passed all 150 steps, including narrow Home/Library,
  New Mac, Get Info and Media, corrupted-cache rejection, real SHA-256 and
  archive extraction, keep-files removal/reopen, Recycle Bin, and process kill.
- Hung-process termination passed the 1.5-second interaction bound. Shutdown
  followed by Force Stop also passed while QMP remained unanswered.
- Independent self-tests passed 122 pixel fixture comparisons, 672 font glyphs,
  display/input protocol checks, configuration, sparse disk and archive checks.
  Asset export consistency passed.
- All 27 1× captures matched their Windows desktop captures exactly. All 54
  1×/2× rendered captures passed title-bar pixel-grid checks, with full UI pixel
  checks where applicable. The 2× desktop comparison was obstructed only by a
  retained computer-use pointer/halo overlay; it is not counted as an exact
  desktop match.
- A separate 32-step public network flow loaded the live catalog, paused and
  resumed Deluxe from saved bytes, and downloaded and installed System 7.5.5.
- A separate 14-step engine flow booted that System 7.5.5 Mac to Finder,
  paused/resumed it, closed its window into Power and shut it down. Final state
  had no running engine.

See [validation.json](screenshots/windows-ux/validation.json) and the reusable
[UI regression instructions](../native/tests/ui-ux.md). Full qualification
reports, matching desktop captures and disposable machines remain in ignored
`output/` directories. Mixed-monitor moves, the full OS DPI matrix and every
guest OS are outside this pass; this is not a new packaged release.
