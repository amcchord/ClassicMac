# Paste Text into Mac

ClassicMac 3.0 provides an explicit one-way text entry action in the app's
Machine menu, the native guest window's Machine menu, and the browser toolbar.
The app shortcut is Command-Shift-V. The native guest menu has no shortcut, so
the guest's Command-V and Command-Shift-V keep their existing behavior.

The action opens a review window containing the host clipboard's plain text.
The user can edit it and must click **Paste Text** to send it. Clipboard data is
read only when this window is requested; it is never returned over HTTP,
written to a file, or automatically synchronized with the guest.

Text is typed with acknowledged, paced QEMU keyboard commands because the
guest does not currently provide a clipboard agent. The guest must use the
US keyboard layout with Caps Lock off. Supported text includes all printable
ASCII, tabs, line breaks, and common MacRoman accented letters. The complete
mapping lives in `PasteTextPlan.swift`. Unsupported Unicode or control
characters reject the whole paste for editing; nothing is silently replaced or
dropped. CR, LF, and CRLF become one Return per line break.

The limit is 8,192 characters. Normal speed is approximately 20 key chords per
second; Slow typing uses about eight. Accented letters that require a dead key
use two chords. Cancel, closing the review window, switching applications,
pausing, or stopping the machine ends the transfer. A started accent pair is
finished before cancellation so the next manually typed letter does not
inherit an accent. Already entered text remains in the guest.

## Integration

- Set `CLASSICMAC_VM_ID` to `config.id.uuidString` in the QEMU process environment.
- Apply `cocoaui/paste-text.patch` after the existing Cocoa patches. This patch
  adds only a native menu command and enabled-state updates.
- `AppDelegate.applicationDidFinishLaunching` calls
  `PasteTextController.shared.startListening()` once.
- The native menu releases the grabbed mouse, pressed keys, and mouse buttons,
  then posts `com.classicmac.paste-text` through
  `NSDistributedNotificationCenter`, with the UUID string as `object` and no
  `userInfo`. The Swift listener validates the UUID, resolves the machine from
  the library, checks it is running and unpaused, and opens the review window.
- `PasteTextController.shared.present(for: UUID)` is the shared app entry point.
  Notifications and browser requests only open the review window; neither can
  directly send guest input.
- Browser POST actions `/actions/paste-text` and `/actions/paste-text-state`
  require the exact `http://127.0.0.1:<server-port>` Origin and a per-run random
  token in `X-ClassicMac-Action`. The token is embedded in the served page's
  `classicmac-action-token` meta element. The first action opens the window;
  the second returns `canPaste` for the toolbar's enabled state. Both reject
  missing or mismatched credentials with HTTP 403. A paste request while
  stopped or paused returns HTTP 409.
- `HMPClient.command` and `send` now serialize socket connections with one
  shared lock. Their signatures and result semantics are unchanged. Text entry
  releases the lock between chords so status, previews and lifecycle commands
  can interleave.

## Verification

The focused Swift tests cover printable ASCII, command metacharacters, newline
normalization, common accents, strict rejection, size limits, cancellation,
pause/unavailable handling, monitor failures, HTTP token/origin protection,
per-run token changes, and the stopped-machine HTTP state. Accented sequences
are independently checked against the installed Mac US keyboard layout using
Carbon's keyboard translator, including the following ordinary letter.

Before release, check the integrated native menu patch and paste into SimpleText
on both an OS 9 Power Mac and an OS 8 Quadra. Include mixed case, punctuation,
tabs, multiple lines, `café Ñ ç Å ü`, a long paragraph, Cancel, Pause, switching
apps, and ordinary guest Command-V. Check the native and browser entry points,
including disabled controls while paused. This worktree's automated tests do
not substitute for that combined guest/UI pass.
