# Native UI regression flow

Run the built Windows app through the Library and machine-management flows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File native/scripts/run-ui-ux.ps1 -Executable output/native-build/classicmac.exe
```

Use `-PixelScale 2` to exercise the same controls at twice their physical pixel
size. This models interface zoom; it does not change Windows display scaling or
qualify mixed-monitor DPI transitions. Each run creates a new ignored output
directory with isolated preferences and disposable machines. An explicit
`-Output` must name an empty folder. The test never opens the user's library.

The 150-step recipe saves 27 window screenshots, matching desktop captures,
control geometry/accessibility evidence, and a final `result.json`. It checks:

- Library revision deduplication, search combined with OS filters, empty
  results, unavailable platforms, and minimum-version messages.
- Rejection of an intentionally damaged cached archive, followed by creation
  of a Mac from a valid cache using the real SHA-256 and archive extraction.
- Name validation and the completed-download path back to My Macs.
- Narrow Home and Library layouts, plus New Mac, Get Info and Media dialogs
  with their action buttons anchored outside scrolling content.
- Canceling removal, removing only the list entry, reopening the unchanged
  machine, and canceling or confirming a move to the Recycle Bin.
- Blocking removal while paused, safe defaults on destructive confirmations,
  canceling Force Stop, and stopping an unresponsive process within 1.5 seconds.
- Closing a running guest window, reaching its power controls, and force
  stopping while an unanswered Shut Down command is still waiting.

Catalog and cache fixtures are injected only by the isolated qualification
runner. Their archive contains a valid configuration and a 512-byte blank disk,
not an operating system. The hung-engine fixture is another copy of the app
process which deliberately never answers QMP; it has a two-minute lifetime
limit. Its guest screenshot is explicitly labeled as a qualification fixture.
These checks cover real Qt widgets, disk handling, and process termination;
they do not establish that a guest OS boots, that Internet downloads work, or
that mouse/keyboard input reaches Windows. Use the separate
[Windows desktop flow](windows/README.md) and real Library downloads for those
checks.
