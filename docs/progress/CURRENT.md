# Windows display refinement — final preflight

Functional source bf9a749, worktree `worktrees/windows-dpi-resize`, PR #38.
Windows CI40127036437 and Linux CI40127038152 pass. Latest remains4.6.0.

Exact final AppImage passes46 steps: Power Mac8.6 repeated resize/fullscreen/
pause/resume/pixel controls/shutdown, public System7.5.5 download/import,
enhanced Quadra boot-time resize with safe fitting and clean shutdown.
Exact final Windows EXE passes32 steps: enhanced7.5.5 early resize, all scale
controls, fullscreen, shutdown/reboot, remembered Fit Window. Final Windows Standard Power Mac live DPI (49 steps), ATI fallback (16) and
constrained 200% UI (18) pass. Twenty-three complete guest frames and four desktop
captures compare with zero differing pixels; both package inventories pass.

Native Windows uses exact fractional DPI/Per-Monitor V2. Physical painting
preserves every source pixel at integral zoom, including centered1024x768.
Standard Power Macs support asynchronous live resolution changes; NDRV0bb2f227
fixes repeated modes. ATI and all Quadras default to automatic fitting, keeping
their OS mode; saved choices are retained. Startup stress exposed unsafe QFB
hot-plug. All experimental firmware changes were reverted; native Quadra resize
requests are disabled. Shared Quadra firmware is unchanged. Mac4.6.1 with new
Power Mac NDRV is signed/notarized/stapled; full verification passes.

QA VMs170/171 and own RDP/task ClassicMac461QA remain running. Task-only
backed-up configs need restoration. No user disk/catalog/DNS/hosting change.
Integrated at670bb18; root consolidated validation and full Mac verifier pass.
All nine staged release asset checksums match. Next: publish4.6.1, verify fresh
public downloads, restore QA configs and stop own task/RDP/VM resources.
