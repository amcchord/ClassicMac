# GXMetal status integration

The library uses the compact `GXMetalStatusMenu(config:isRunning:isPaused:showTools:)`
view. `showTools` is an optional action that opens the selected Mac's media
controls. The view reads status while visible, checks every two seconds, cancels
when the selected machine changes or stops, and clears failed readings.

The native guest window has **View → GXMetal**, including in full screen.
Its snapshot refreshes whenever View or the GXMetal submenu opens. Both menus
include instructions for running GXMetal Test and reinstalling the guest driver.

## Build integration

Apply `cocoaui/gxmetal-status-menu.patch` after the existing Cocoa patches in
`scripts/build-qemu.sh`. The normal source-copy stage already installs the changed
`gxmetal/qemu/gxmetal_qemu.c` and `.h`; no additional transport source or protocol
version change is required. Rebuild QEMU before bundling the app.

## Runtime interface and meaning

HMP command: `qom-get / gxmetal-status`.

QMP equivalent:

```json
{"execute":"qom-get","arguments":{"path":"/","property":"gxmetal-status"}}
```

The value is a JSON **string** containing:

```json
{"schema":1,"protocol":65561,"renderer":"metal","completedCommands":0,"activeContexts":0,"successfulDraws":0,"lastDrawAgeMs":-1,"faulted":false,"errorCode":0}
```

- `renderer` reports the actual initialized host backend: `metal` or `software`.
- `completedCommands` counts validated queue commands in the current connection
  generation, including fences. Nonzero proves guest contact, not the installed
  extension's identity or version.
- `successfulDraws` counts successful draw dispatches. `lastDrawAgeMs` is the
  monotonic age of the last successful draw, or `-1` when there have been none.
  Active rendering requires a live context and a draw less than two seconds ago.
- Errors override activity. A paused or stopped machine never displays current
  acceleration. No telemetry is reported as unavailable, rather than healthy.
- A transport reset clears the activity evidence along with the existing queue.
- `protocol` identifies the host protocol, with major/minor in its high/low words.
  Guest extension version and GXMetal Test results are not available here.

Apple Software RAVE renders outside this transport. An idle GXMetal connection
cannot distinguish a desktop, an idle game, or a game using Apple Software RAVE.
The menus deliberately do not claim that absence of activity proves fallback.

## Verification

The feature checkout passed six Swift parser/state tests and a Swift build.
The updated transport and patched Cocoa file passed compiler syntax checks with
the configured QEMU flags plus `-Werror`. The new patch applies cleanly after
the existing patches. Vendor sources were only read during these checks.

After integration, verify the actual bundled binary reports zero guest activity
before boot, rejects attempts to write this property, and reports guest contact
and recent draws during GXMetal Test or a compatible game. Confirm the icon
returns to idle after rendering stops, stays neutral while paused/stopped, and
the native menu remains reachable in full screen. Inspect the library menu with
VoiceOver and the native test/repair help before accepting the 3.0 candidate.
