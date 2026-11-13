# Windows validation on local Proxmox — November 13, 2026

The current RetroBoot import and native UI build and run on Windows 11 Pro.
All five actual Windows desktop scales pass the configuration/running-guest
flow. Component references, desktop compositor captures and visible raw guest
pixels compare exactly. This qualifies the measured states, rather than every
classic Mac OS version, application layout, input method or host device.

## Environment and scope

The user requested the local Proxmox host `192.168.1.100`. Its trusted SSH alias
is `n5` (`pve-n5`). Initial inventory had no QEMU VMs and one running RetroBoot
build container, 300. A dedicated VM 401, `classicmac-win11-qa`, was created for
this work: 8 virtual CPUs, 16 GiB RAM, 128 GiB sparse disk, OVMF/TPM and Windows 11
Pro 24H2, build 26100. Existing VMs/templates on the older lab cluster were not
changed. The original container 300 and user Mac disk images were preserved.

Windows installation media came from the existing lab. AustinLand supplied
the existing Windows key; it was never printed or committed. Microsoft
activation returned `0xC004C020`, with license status 5. Administrator credentials
for this new test VM are stored separately in AustinLand under
`ClassicMac-Windows-QA-n5-401`. Activation needs attention before ongoing use.

Native Windows OpenSSH and the Proxmox guest agent provide the command
channels. Interactive testing uses the Administrator's actual RDP desktop at
1920×1440. The RDP certificate and SSH host key were verified through the
trusted Proxmox connection. These are Windows compositor/input tests; no
`QT_SCALE_FACTOR` or macOS modeled DPI substitutes for Windows DPI.

## Build and package

Source baseline is `88ca9e4`, which already contains RetroBoot 2.0.1 revision
`432a43d4` and the native font/control audit. Qualification fixes `2d467e7`
and `7aa1fe0`, plus the Windows startup fix `52b61d8`, are included in the final app. Both patched
QEMU 11.0.2 engines
and the native Qt application were built on Windows using MSYS2 UCRT64,
GCC 16.2.0, Python 3.14 and CMake 4.4. The normal packager produced the portable
EXE; GUI tests launched that EXE, including its temporary extraction and normal
runtime discovery. No development-runtime override was used.

The retained portable package is
`artifacts/windows-proxmox-2026-11-13/ClassicMac-4.6.2-windows-x64.exe`.
It is 42,643,673 bytes, SHA-256
`b2f60d9fbb3536828121385d09dab76789c3a10321fc093a0d6e82e70647763a`.
The copied artifact's hash matches the file built and exercised on Windows.
It is a local test artifact; no release or upload was performed.

The [build manifest](evidence/2026-11-windows/build-manifest.json) records all
418 payload files. Important engine/firmware hashes are:

| File | SHA-256 |
| --- | --- |
| qemu-system-ppc.exe | `57a0b30c7617b5801ba91da3a18fba087eebef67e5c497c2582fbbbc9e71469a` |
| qemu-system-m68k.exe | `cec97dbd13d7cbd138ad8d0bae87f4655ae5e34e8c203a25a47d3c70aa02852a` |
| qemu_vga.ndrv | `7983d4d736d22528a81f5120a7b04bf61b619438d27af6f85acedb3de0ad4c13` |
| mac_qfb.rom | `5f40220ec35e12fcdf1ad9715a8aa1cf0893fb2b15aa19c1f3973c6ef1bf35b4` |

The package includes the verified Tools CD from the published 4.6.1 bundle,
SHA-256 `23a42184811c7e1ad8718b6746e061c3a1c3e3dd43af25c9c828b3fa326fca22`.
Three fresh test machines were downloaded through the actual Windows Library:
OS 9.2.1 ATI, System 7.5.5 Quadra and Mac OS 8.6. Download/hash/extraction and
sparse NTFS disk creation therefore ran in the shipping host code.

## Configuration and running-guest review

Each DPI case ran 59 steps with a fresh isolated host profile. Native Win32
`SendInput` typed and saved `Windows QA é ñ` in Get Info, then clicked the
guest Apple menu using real Windows mouse input. Those assertions are separate
from qualification actions that invoke native Qt widgets directly.

| Desktop scale | Measured window DPI | UI physical-pixel zoom | Steps/input |
| --- | --- | --- | --- |
| 100% | 96 | 1× | 59/pass |
| 125% | 120 | 1× | 59/pass |
| 150% | 144 | 2× | 59/pass |
| 175% | 168 | 2× | 59/pass |
| 200% | 192 | 2× | 59/pass |

All cases report Per-Monitor V2 awareness. Pixel artwork deliberately uses
whole physical-pixel zoom, rather than fractional enlargement of glyphs and
bevels. The screenshots show consistent square pixels at fractional OS scales.

| Surface/behavior | Evidence and result |
| --- | --- |
| Home and New Mac | Bitmap labels, selection, document frame, model popup and cancel path captured at all five scales. |
| Get Info | Native field/focus artwork, saved accented characters, screen/depth/graphics popups, checkboxes and visible actions reviewed. |
| Media, Library and setup | Real dialogs and current license screen captured; Library downloads also exercised separately. |
| Running OS 9.2.1 | Healthy Finder after startup and restart; native mouse input visibly opens the Apple menu. |
| Narrow window | Actions wrap and stay reachable; oversized sharp guest pixels scroll rather than shrink below their minimum zoom. |
| Full screen | Only the guest display remains; the host title/action bars return after exit. |
| Pause/resume | Guest state and raw framebuffer captured across the transition. |
| Host Restart | New engine PID required, then a working Finder; recorded PID pairs appear in the flow evidence. |
| Paste and shutdown | Paste dialog opens/cancels; clean shutdown ends with no live engine/display. |

The final five-DPI matrix uses sound, networking, Tools CD and tablet input
enabled. An earlier successful muted matrix remains separate in task output.
RDP remote audio mode 1 exposes the VM's High Definition Audio endpoint;
the actual packaged QEMU process initializes DirectSound without silent
fallback. This is an initialization check, not an audible sound certificate.

## Default ATI startup failure and fix

Testing only the muted configuration missed a real startup problem. With the
ATI card, Sungem network adapter, Tools disk and tablet all enabled, Mac OS 9
remained on its startup screen even after 110 seconds. Host Restart then timed
out because the guest was not processing its shutdown keys.

Snapshot-only probes use the same clean disk without writing to it. Networking
alone boots; Tools alone boots and mounts the Tools volume. Their combination
fails in both the imported engine and the earlier successful Windows build,
GitHub Actions run 40146326685, commit `c5184eadda364e5b95428709095bb811f3cc965e`
(October 25). This predates the RetroBoot import.

The normal ATI PCI layout places Sungem in slot 14 and the tablet in slot 18
when Tools is present. Both use interrupt 29. A controlled reproduction with
the original device order stalls; pinning the tablet to slot 19 gives it
interrupt 30 and boots to a working Finder with Tools mounted. Moving an
auxiliary Virtio disk off the same Sungem line also boots; keeping it on that
line still stalls. This isolates interrupt sharing as the cause, rather than
an individual disk, networking feature, or the new Screamer patch.

`52b61d8` pins the Windows native tablet to slot 19 only for the affected
nonlegacy ATI+Sungem+Tools combination. Other guest layouts and the macOS
frontend keep their existing arguments. The native argument oracle includes
an explicit Windows expectation for this case. No guest disk or driver binary
was changed to obtain the fix. The final portable package is rebuilt and the
full DPI/configuration/input/restart/shutdown matrix repeated with all default
options enabled.

Retained regression evidence: [startup before](evidence/2026-11-windows/startup-before.png),
[startup after](evidence/2026-11-windows/startup-after.png),
[PCI before](evidence/2026-11-windows/pci-before.txt),
[PCI after](evidence/2026-11-windows/pci-after.txt), and
[controlled probe results](evidence/2026-11-windows/startup-isolation.json).
The [native source hashes](evidence/2026-11-windows/native-source-hashes.json)
match the files compiled on Windows to the integrated source.

## Exact comparisons

The Windows [native component report](evidence/2026-11-windows/native-pixels.json)
passes 122 independent classic-Mac reference comparisons, all 672 Mac Roman
glyphs/advances, seven actual native widget groups, protocol/input contracts
and the modeled scale gate. The reference artwork was captured independently
from the classic guest's Appearance Manager/QuickDraw, as documented in the
[original audit](ui-fidelity-audit-2026-11.md).

The [DPI flow evidence](evidence/2026-11-windows/dpi-flows.json) and
[pixel-grid report](evidence/2026-11-windows/dpi-grid.json) cover 75 actual desktop
captures. Every cropped Windows desktop capture equals the corresponding app
capture. Seventy title bars and all pure configuration UI surfaces preserve
uniform integer pixel blocks. Five full-screen captures correctly have no
host title bar to test.

One original 150% settings capture crossed the editable field's caret blink:
56 pixels differed in the caret's 2×28-pixel rectangle. The full 59-step flow
still passed. An additional 8-step configuration-only run uses real Windows
Control-A input to select the name, removing the blinking caret while keeping
the focused field. Its exact desktop capture replaces that transition in the
final pixel collection. Both originals and the selected-state recapture are
retained; no threshold or ignored pixel region was introduced.

The [raw guest report](evidence/2026-11-windows/dpi-guest-pixels.json) compares
30 Sharp Pixels captures, 19,201,136 visible device pixels, with zero differences
from raw RFB frames. This establishes that the host display does not blur,
recolor or interpolate those visible guest pixels. It does not compare the
guest's own OS artwork against a different OS edition or theme.

Selected actual desktop crops:
[Get Info 200%](evidence/2026-11-windows/settings-200.png),
[native guest input 125%](evidence/2026-11-windows/guest-native-input-125.png),
[restarted Finder 175%](evidence/2026-11-windows/guest-restarted-175.png),
[model popup 150%](evidence/2026-11-windows/model-menu-150.png), and
[Home 100%](evidence/2026-11-windows/home-100.png).

The [selected field at 150%](evidence/2026-11-windows/settings-selection-150.png)
is the stable recapture described above. The final compared collection is
`output/windows-dpi-pixels-final`; original full-flow captures remain in
`output/windows-dpi-final`.

## Other Mac configurations

The [model flows](evidence/2026-11-windows/platform-flows.json) run the same
portable package at actual 200% Windows DPI. Each ends with no running engine.
These model flows precede the final ATI PCI fix; that conditional does not
apply to Quadra or the standard G3 profile, and their engine/firmware hashes
are unchanged in the final package.

| Configuration | Behavior verified |
| --- | --- |
| Built-in Quadra, System 7.5.5 | Fixed 640×480; disabled/inapplicable graphics choice; sharp/fit behavior; full screen; pause/resume; host Restart and guest Control-Command-Power each replace the engine and return to Finder. |
| Enhanced Quadra, System 7.5.5 | Starts at 800×600/32-bit; live resize to 887×559, then 960×720 in full screen; host restart and guest reset each replace the engine and return to Finder. |
| Standard Power Mac G3, Mac OS 8.6 | Matched Virtio startup profile; starts at 640×480, resizes to 880×559 and 960×720 in full screen; pause/resume, replacement process on restart and clean shutdown. |

The [platform grid report](evidence/2026-11-windows/platform-grid.json) passes
17 exact desktop captures and 14 title bars. The
[guest comparison](evidence/2026-11-windows/platform-guest-pixels.json) passes
13 captures, 15,979,812 visible pixels. The built-in Quadra's initial fractional
Fit Window capture is excluded from the sharp-pixel claim.

One original enhanced-Quadra full-screen capture crossed the guest menu-clock
minute transition: 112 pixels in the time's last digit differed between the
application grab and desktop grab. The originals are retained. A focused
14-step boot/full-screen/pause/capture/resume/shutdown run captures a stable
frame and passes both exact comparisons. That paused capture replaces the
clock-transition attempt in the final platform collection; no tolerance or
masked region was added to either checker.

Selected desktop crops: [Quadra reset](evidence/2026-11-windows/quadra-reset-200.png),
[enhanced resize](evidence/2026-11-windows/quadra-enhanced-resized-200.png), and
[Mac OS 8.6 after restart](evidence/2026-11-windows/os86-restarted-200.png).

## Test fixes and rejected attempts

- The qualification pointer helper assumed every guest was fractionally fitted.
  It now uses the actual `screenRect`, pixel centers and visible viewport,
  including scroll offsets, and rejects off-screen targets.
- Full-screen captures now identify their content-only state. The pixel
  checker tests a title bar only when a host title bar exists; guest pixels
  are still checked separately.
- The Screamer regression test used Unix sockets and platform-default newline
  translation. Windows needs loopback TCP and LF protocol lines. Its seven
  checks now pass on Windows, including 44,136 frames/s pacing and 512-frame
  buffer separation. Both TCP and Unix variants are checked on macOS.
  [Device result](evidence/2026-11-windows/screamer.json).

Initial attempts are retained separately and excluded from passing counts.
The first desktop attempt had the Windows Start menu over the app; a detached
RDP session later stopped presenting fresh desktop frames. Persistent RDP and
real foreground input corrected the test setup. A native-input P/Invoke used
a string-null class argument instead of a null pointer; the corrected actor
types and clicks successfully. A completed PowerShell actor's null `.ExitCode`
was also unsuitable as a pass condition; the flow now requires its completion
marker, no input errors, exact persisted text and the package's successful exit.

An early 45-second boot/restart attempt with sound/network/CD enabled had not
reached Finder. It was excluded from passing counts. Longer waits and the
controlled probes above establish the interrupt-sharing failure; a boot logo
is never counted as a healthy Finder. A separate 110-second test-script wait
initially exceeded its default 60-second action deadline. The reusable runner
now supplies an explicit deadline for longer waits.

## Remaining qualification

The virtual display reports Microsoft Basic/Remote Display adapters. WGL
compiles and the ATI realization/off-switch gates pass, but the test VM
does not expose OpenGL 4.1. QEMU explicitly reports discarded 3D drawing. ATI 2D
Finder pixels work; accelerated Windows games remain unqualified.

Screamer DMA timing is synthetic device evidence, not audible Mac sound.
The original UI matrix had no Windows sound endpoint; final default runs use
the added virtual HDA endpoint and actual DirectSound initialization. Physical speakers,
loopback audio samples, latency and long-running sound still need their own
checks. The previous Mac ATI game fixture was silent on both old and new
renderers; it is not counted as an audio pass here.

Mixed-DPI monitor moves, Windows snapping/native drag, alternate work-area
sizes, IME composition, other keyboard layouts and accessibility interaction
remain outside this run. One RDP display at five real DPI settings does not
qualify multiple simultaneous physical monitors. Unmeasured classic frame/
control variants and the macOS SwiftUI layout/scaling gaps in the original
audit also remain open.

Networking is enabled in the final boot/restart flows; guest internet transfer
and network performance are not certified by a successful startup. Physical
download/SmartScreen and multi-hour stability checks are also separate from
the local portable-package run.

## Reproduction and retained state

[`native/tests/windows`](../native/tests/windows/README.md) contains the reusable
portable-package flow and Win32 input actor. Use disposable Library machines,
an active Windows desktop at the requested OS scale, new output folders, then
run both pixel checkers. Full captures, recipes, build logs and failed attempts
remain in `worktrees/windows-proxmox-qa/output/` and `C:\ClassicMacQA` in VM 401.
Committed evidence contains no credentials or original user disk content.

Final source checks ran from the integration checkout after `52b61d8`:
138 Swift tests with three optional fixture skips and zero failures, native
build/self-test with 122 reference comparisons, 11 Python unit tests, deterministic
exports in UTC/Pacific-Honolulu, syntax checks and seven Unix Screamer checks.
The Windows self-test and TCP Screamer results are separate actual Windows
runs. [Summary](evidence/2026-11-windows/summary.json).

VM 401 is retained and stopped. Its last assigned address is 192.168.1.131;
start it in Proxmox and use the AustinLand credential entry above for RDP.
Automatic Administrator logon is disabled, its stored logon password removed,
owned scheduled jobs/RDP/setup HTTP processes stopped, and owned unattended
credential XML/ISO files removed. The existing container 300 remains running.

No push, tag, release, public artifact upload or change to the existing lab
VMs/templates was made. Root's untracked `gxmetal/` remains untouched.
