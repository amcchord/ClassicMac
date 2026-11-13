# ClassicMac UI fidelity audit — November 12, 2026

The Windows/Linux frontend had visible differences from native Platinum in
fonts, menus, dialog frames, focus rings, scrollbars and progress animation.
This pass corrects those components and several configuration/running-guest
behaviors. The covered components now compare with zero differing pixels
against independent guest captures. The whole application is not yet
certified pixel-perfect across every platform/state. The November 12 pass ran
the shared C++ renderer on macOS. The [November 13 Windows follow-up](windows-proxmox-validation-2026-11-13.md)
then built both engines and the portable app on local Proxmox and passed the
actual five-DPI desktop/input matrix; physical host-device and mixed-monitor
qualification remain open.

The target for ClassicMac's host interface remains Mac OS 8.1 Platinum,
default Lavender, unsmoothed bitmap text. Native Mac OS 9.2.1 at 32-bit depth
is the separately identified raw-RGB control/font reference. Mac OS 7's
guest interface is supplied by the guest itself; the host does not replace
its menus, windows or fonts with Platinum. Different OS versions, themes,
depths and smoothing preferences must not be combined into one oracle.

## Method and independence

The reference utility in [`platinum/reference`](../platinum/reference/README.md)
calls the guest's Appearance Manager and QuickDraw. It does not use either
ClassicMac's or RetroBoot's renderer. It displays the actual Gestalt version
and depth; new reference pages report `Mac OS 921, depth 32`. Raw framebuffer
captures avoid host scaling and color-profile conversion. Each committed
crop has a source hash and rectangle in the adjacent manifest.

New references cover active/inactive movable dialogs, menu panels and selected
rows/separators, normal/focused fields, focused push buttons/check/radio
indicators, vertical scrollbars in normal/held/disabled states, a horizontal
scrollbar and all 16 indeterminate phase inputs (four repeating patterns).
Shipping UI uses measured drawing tables, not pasted reference screenshots.

For fonts, QuickDraw draws 224 Mac Roman characters per font and encodes each
CharWidth result in eight binary pixels beside the glyph. The measurement
tool reconstructs bitmap bounds/advances from those screenshots. The tests
compare against the independently captured atlases, including the advance
bits. They do not render the shipping font to create its own expected image.
No Apple font program was imported.

The existing archive-based OS8 title-box references remain distinct. Only those
three explicitly documented pattern fixtures normalize quantized archive
colors; new OS9 controls/font fixtures compare raw RGB. Pixel comparisons
have no percentage or perceptual tolerance.

## Findings and changes

| Area | Finding | Change and evidence |
| --- | --- | --- |
| System font | Printable ASCII advances matched the prior M5Tab oracle, but its visible glyphs were one pixel left of native QuickDraw. | Correct native bearing; retain `m5-system-ascii-legacy.png` for provenance. The ASCII oracle is intentionally corrected, not loosened. |
| Regular/small fonts | Only 2/95 and 21/95 printable ASCII shape/advance combinations matched the native Geneva captures. | Rebuild System, Regular and Small from Charcoal 12, Geneva 12 and Geneva 9 across all 224 Mac Roman characters each. Small keeps its existing nominal role name. 672 glyphs and advances pass at 1×/2×. |
| Font export | TrueType timestamp generation depended on the host time zone; editable-control TTFs could drift from the C++ bitmap export. | Use UTC; one exporter builds/checks the JSON and editable-control TTFs. Checks pass in UTC and Pacific/Honolulu. |
| Popup menus | Stock Qt menu geometry, cosmetic border, host text and selection colors differed from the guest. | Custom menu delegate/panel: native 16-pixel rows, six-pixel separators, selected bevel/colors, bitmap text and checked item. Popup selection aligns with button text. Actual delegate and full 2× screenshot checks catch cosmetic host pixels. |
| Movable dialogs | Qt dialogs inherited document-window borders and controls. | Native active/inactive movable-dialog frame and 3/24/3 content insets; no document title widgets. Escape/Control-period cancels where closing is allowed. Swift modal frame/insets use the same measurements. |
| Focus | One-pixel perimeter strokes substituted for native focus artwork. | Measured two-pixel field focus, button focus and check/radio indicator focus. Actual focused native widgets are tested. |
| Fields | Long text could draw over borders; caret stayed visible with selections/read-only fields. | Clip text/selection to the content area, use native system strike, blink the caret and hide it for selections/read-only fields. Pointer selection and input-method cursor geometry follow bitmap widths. |
| Scrollbars | Arrows were at opposite ends; thumb length, channel, held states and horizontal light direction differed. | Native paired lower/right arrows, 17-pixel thumb, correct gradients/disabled/pressed states. Horizontal artwork transposes rather than rotates the lighting. Hit regions and drag span match the new geometry. |
| Indeterminate progress | Static diagonal approximation. | Native four-frame pattern with measured caps/grays. All phase inputs wrap correctly; timer stops while hidden/determinate. Animation timing itself is not a native timing certificate. |
| Document frame | Opaque content could cover the grow artwork; held title buttons stayed pressed outside their hit region. | Raise a transparent grow overlay, track held-inside state and elide long captions before drawing. Composed opaque-content regression passes. |
| Oversized guest screen | Sharp Pixels silently reduced the image below one device pixel per guest pixel. | Preserve integral minimum zoom; add native horizontal/vertical scrollbars and correct input offsets. Every visible device pixel is compared against raw RFB captures; tests also reach the bottom-right source pixel. Fit Window remains the explicit fractional mode. |
| Full screen | Host frame/toolbar could remain visible in the guest window. | Content-only full screen; restore controls on exit. Escape/Alt+Enter behavior retained. Actual guest captures and raw-pixel checks exercise transitions. |
| Narrow guest window | One-row action bar overflowed and omitted Media. | Wrap the action bar; expose Pause, Restart, Shut Down, Paste, Media and Full Screen. |
| Close behavior | Closing a running guest could hide it while its disk remained mounted. | Request orderly shutdown; keep the guest visible while it stops. |
| Configuration | Saving an unrelated setting could reset a custom resolution absent from the preset list. Model-specific options could appear for the wrong family. | Preserve custom size in the list, hide inapplicable CPU/enhanced-video controls and disable the Power Mac graphics selector for Quadra. |
| Preview/copy | Saved preview could stretch; empty/about text included unnecessary prose. | Preserve aspect ratio with nearest-neighbor rendering. Use functional labels/states. |

## Configuration and lifecycle review

The actual native application was exercised through Home, New Mac, the model
popup, Get Info, graphics popup, Media, the live Mac OS Library, setup/license,
guest startup, constrained guest sizing, full screen, pause/resume, Paste
and clean shutdown. Modal action buttons remain visible below scrolling
content. Library/network/process operations retain the existing implementation;
the audit did not replace them with a screenshot mockup.

The review checked the modified fields and model choices, selection colors,
bitmap text, menu geometry, active/inactive frames and default/cancel controls.
Home preview keeps the guest's aspect ratio. Long titles are elided. Narrow
guest windows preserve the display's whole pixels and keep actions reachable.
Full screen removes host chrome. Pointer coordinates account for the guest
viewport and scrollbar offsets. Existing relative game mouse input, RFB
authentication, clipboard mapping and framebuffer protocol gates still pass.

The imported Quadra RESET behavior is compatible with the existing engine
lifecycle: host Restart and guest Control-Command-Power each terminate the
old engine and create a new process. Captures show a healthy System 7.5.5
Finder afterward, followed by clean shutdown. This is process replacement
evidence, not merely a successful click on Restart.

The shared Swift renderer received the new movable-dialog, menu, scrollbar
and progress measurements and corrected fonts. The native C++ frontend
received the larger integration pass because that is the Windows/Linux UI.
The optional browser/iPad UI and experimental Copland frontend are outside
this desktop fidelity certificate.

## Validation and evidence

The committed [evidence summary](evidence/2026-11-ui-audit/summary.json)
and [native pixel report](evidence/2026-11-ui-audit/native-pixels.json)
record final counts. Disposable disks, full raw captures and build logs stay
under `worktrees/retroboot-ui-audit/output/`; original user machines are untouched.

| Check | Coverage/result |
| --- | --- |
| Native C++ reference gate | 61 independent fixtures at 1×/2×: 122 comparisons, zero differing pixels. All 672 native Mac Roman glyphs/advances; actual focused widgets/menu delegate; opaque content/grow-box compositing. |
| Native input/display gate | Authenticated RFB, framebuffer/input/model contracts, oversized-screen scrolling, bottom-right reachability, explicit fractional Fit Window and seven modeled backing scales. |
| Swift reference gate | Direct Core Text glyphs/advances at 1×/2×, native control/window/menu/scroll/progress fixtures, all 16 indeterminate phase values. |
| Combined source validation | From the integration checkout: 138 Swift tests, three optional fixture skips, zero failures; all 13 Platinum render tests pass. Native build/self-test passes. Eleven Python unit tests, UTC/Honolulu export checks and shell syntax checks pass. |
| Whole UI | 48-step configuration/running-guest recipe passes at both 1× and modeled 2×. All 26 captured surfaces pass the pixel-grid checker, including all 18 pure UI captures. Guest pixels are checked separately. |
| Raw guest display | 11 captures, 6,843,569 visible Sharp Pixels device pixels, zero differences against raw RFB frames. This includes the enhanced Quadra. Fit Window is explicitly reported as fractional and excluded from that claim. |
| Engines | Both v11.0.2 engines build; two consecutive source preparations; ATI/CGL realization/off switch, display transport/reconnect, CPU/VIA/startup/firmware gates pass. |
| Drivers and audio device | Host GL shader/FBO/draw/readback smoke passes. 19,974 bounded gray scanlines across six depths, Power Mac mode/gamma contracts and seven Screamer DMA pacing/interrupt checks pass. Device-level pacing does not establish audible guest sound. |
| Real guests | Actual Power Mac OS 9.2.1 at 1×/2×. A 16-step built-in Quadra System 7.5.5 recipe proves engine replacement for both restart paths. Another 16-step enhanced-video recipe boots at 800×600, resizes to 779×701, resets into a healthy Finder and shuts down. |
| ATI game | Quake III drawing, menu click, relative mouse look/firing and return to Finder exercised. Audio assertion fails on both old and updated ATI fixtures because captured sound is silent. No sound/performance certification. |
| Actual Windows desktop | November 13 follow-up: Windows 11 Pro portable package, actual 100/125/150/175/200% OS DPI, 59-step flows with native accented input and guest mouse input, 75 exact desktop captures and 19,201,136 raw guest pixels pass. See the [Windows report](windows-proxmox-validation-2026-11-13.md) for model checks, the ATI/Sungem/tablet interrupt fix, limitations and dedicated VM 401 state. |

Selected actual application captures: [Get Info at 2×](evidence/2026-11-ui-audit/settings-2x.png),
[model menu at 2×](evidence/2026-11-ui-audit/model-menu-2x.png),
[narrow guest](evidence/2026-11-ui-audit/guest-narrow.png) and
[resized enhanced Quadra](evidence/2026-11-ui-audit/quadra-enhanced-resized.png).
These are captured from the running C++ application on macOS, not Windows
desktop screenshots or original classic-application layout references.

## Remaining work, in priority order

| Priority | Gap | Required evidence / acceptance |
| --- | --- | --- |
| P1 | Remaining Windows interaction and host devices | The actual five-DPI package/compositor/native input/full-screen flows now pass. Still require mixed-DPI monitor moves, other work areas, native drag/snapping, IME/alternate layouts and real accelerated GPU/audio output. One RDP display does not qualify those cases. |
| P1 | macOS whole-layout text and constrained guest scaling | SwiftUI Text can still place bitmap outlines on fractional baselines. The macOS guest view also retains smooth downscaling when its screen exceeds the window. Add whole-window native screenshot/physical-pixel checks and an explicit sharp/fit policy before claiming parity with the updated C++ display. |
| P2 | Unmeasured appearance variants | Utility/floating/nonmovable modal frames, inactive/disabled menu variants, combined focus+pressed controls and disabled indeterminate bars need their own native references. Existing controls' passing states do not certify these combinations. |
| P2 | Entire form layout | No original Mac OS application has ClassicMac's configuration content. Control artwork is measurable; every label baseline, wrapped paragraph, row spacing and focus path still needs screenshot review on each shipping platform and constrained work area. |
| P2 | Font styles outside the measured set | Non-Mac-Roman fallback artwork and synthetic bold/italic styles are not native-reference certified. Smoothing is intentionally off for measured bitmap strikes. |
| P2 | Timing and long-running behavior | Animation cadence, prolonged VBL/game stability, sound and all supported OS editions need separate runtime qualification. The new relative VBL is exercised by boot/UI checks, not a long-duration soak. |
| P3 | Host file/folder dialogs | These intentionally use platform services. Their contents/chrome do not reproduce classic Standard File; decide separately whether to build and qualify a classic picker. |

The next platform actions are the remaining physical Windows device/input
cases and the macOS layout/scaling work above. Current Mac and Windows
evidence supports the measured component and captured-state claims; it does
not establish that every app state is a pixel-perfect classic Mac OS reproduction.
