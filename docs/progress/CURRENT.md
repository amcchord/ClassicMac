# Windows DPI and guest-resize refinement in progress

Owned worktree `worktrees/windows-dpi-resize`, branch `codex/windows-dpi-resize`,
from main d1a69b1. Stable Latest remains v4.6.0. User requested a detailed
Windows robustness pass covering real OS scaling, sharp graphics and live
guest resolution changes when the window is resized.

Confirmed gaps: RoundPreferFloor ignores fractional Windows DPI; GuestView
only stretches a fixed framebuffer and never sends the driver's existing
host-resize request; fixed initial windows/dialogs can exceed a scaled work
area. Prior 1x/2x tests did not cover these cases. Use Windows VM171, isolated
QA paths, and real Windows DPI measurements as well as render tests. Preserve
existing guests, root gxmetal and unrelated worktrees.

Next: implement pass-through DPI, bounded asynchronous RFB desktop resizing,
sharp device-pixel rendering and work-area fitting, then qualify the Windows
portable executable at fractional scales and with real guest mode changes.
