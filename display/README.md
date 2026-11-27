# ClassicMac display backend (`-display classicmac`)

A QEMU display backend that hands the guest screen to the ClassicMac app
instead of QEMU's own Cocoa window. The guest frame lives in a shared memory
buffer; a local socket carries the buffer, damage rectangles, the std-VGA
hardware cursor and guest events to the app, and the app's keyboard and
pointer input back to QEMU.

```
-display classicmac,socket=PATH[,idle-refresh=MS]
```

- `socket=PATH` (required): the app's listening `AF_UNIX` `SOCK_STREAM`
  socket. QEMU connects as the client and retries every 100 ms without
  blocking the guest. `PATH` must fit `sun_path` (under 104 bytes on macOS).
- `idle-refresh=MS` (optional, default 100, at most 1000): refresh interval
  once the guest has drawn nothing and the app has sent no input other than
  `REFRESH_RATE` for one second; any drawing or input restores the app's
  rate at once. `0` always refreshes at the app's rate.
- `-display help` lists `classicmac`. Default serial/parallel/monitor
  consoles go nowhere (`vc=null`), as with RetroBoot.
- Built when the host is macOS or Linux and pixman is found
  (`CONFIG_CLASSICMAC_DISPLAY`); only the macOS build is exercised here.

## Files and provenance

| File | What |
| --- | --- |
| `classicmac-display.patch` | `ui/classicmac-display.c` (new), `qapi/ui.json` (`DisplayType` `classicmac`, struct `DisplayClassicMac`), `meson.build` (`CONFIG_CLASSICMAC_DISPLAY`), `ui/meson.build`. Applied last by `scripts/build-qemu.sh` (section 3f), on top of every other ClassicMac patch. |
| `tests/cmdclient.py` | the app's side of the protocol in Python (stdlib only); used by the tests |
| `tests/test_display_gate.py` | build gate: paused mac99, app listening after QEMU starts, HELLO / SURFACE (+ fd, mapped) / full DAMAGE / events, app reconnect |
| `tests/test_cursor_plumbing.py` | std-VGA hardware cursor to `CURSOR`/`MOUSE` without a guest disk: Forth typed at the OpenBIOS prompt through `KEY` messages programs the QEXT cursor registers |
| `tests/guest_display_test.py` | boots a Mac OS 9 disk through the backend (std-VGA with icount or the ATI R100) and exercises resize, menus, keys, cursor, idle, motion rates, a stalled app and reconnects |
| `tests/firmware_yellow_probe.py` | per-frame check for OpenBIOS's pale-yellow console background during boot, with the staged or any other `openbios-ppc` |

Ported from RetroBoot's display backend (`ui/retroboot.c`) at RetroBoot
commit `1860881` ("Merge RetroBoot 1.6.0"):
`~/RetroBoot/emulator/patches/retroboot-display.patch`,
`retroboot-display-idle-refresh.patch` and `retroboot-relative-pointer.patch`;
protocol from `~/RetroBoot/docs/ARCHITECTURE.md` section 3 ("Display
socket"). License GPL-2.0-or-later (as QEMU and the RetroBoot original); the
source keeps the RetroBoot copyright line and adds ClassicMac's.

### Changes from RetroBoot

- **Byte stream instead of `SOCK_SEQPACKET`** (macOS has none): every
  message is framed by its header's `len`. QEMU keeps an output queue,
  sends non-blocking (`sendmsg`, gathering queued messages) and flushes when
  the socket is writable. A message is never split across a dropped
  boundary: only messages that have not started to go out are ever dropped
  or replaced.
- **Coalesced damage**: while a `DAMAGE` is still queued in QEMU, newer
  damage is merged into the next one instead of queueing more (the pixels
  are copied into the shared buffer anyway). If the queue exceeds 4 MB,
  queued `DAMAGE` messages are dropped and one full-surface `DAMAGE` is sent
  once the queue has drained. The socket keeps the default kernel buffer
  (8 KB on macOS) so a stalled app does not wade through seconds of stale
  messages afterwards.
- **Shared memory**: no memfd on macOS. A POSIX shared memory object
  (`shm_open` with a random name, unlinked at once; an unlinked temporary
  file if that fails), `ftruncate` to the size, `mmap(MAP_SHARED)`. Linux
  keeps a sealed memfd. Size = stride x height rounded up to the host page
  size (16 KB on Apple silicon).
- **`SCM_RIGHTS`** travels on the `sendmsg` that carries the first byte of
  the `SURFACE` header (a message with a descriptor always starts its own
  `sendmsg`).
- **New messages `CURSOR` (5) and `MOUSE` (6)** from `dpy_cursor_define` /
  `dpy_mouse_set`.
- **Damage reduced to what changed**: damaged areas are compared with the
  shared buffer in 64 x 16 tiles and only differing tiles are copied and
  announced. On little-endian hosts, 32-bit big-endian surfaces
  (`PIXMAN_b8g8r8x8` / `PIXMAN_b8g8r8a8`) use a direct byte-swapped `bswap32`
  compare/copy fast path straight into the shared buffer, skipping
  `pixman_image_composite32` and the staging buffer. std-VGA's untracked
  shared scanout (`VGA.untracked-vram=on`) reports the whole frame on every
  refresh; without this the app would re-upload every frame and the idle
  refresh would never engage.
- A full-surface `DAMAGE` follows every `SURFACE`.
- Env `CLASSICMAC_SESSION` (instead of `RETROBOOT_SESSION`) fills HELLO.

## Protocol (version 1)

All integers are host-endian (little-endian on Apple silicon). Every
message is:

```c
struct { uint32_t type; uint32_t len; };   /* then len payload bytes */
```

### QEMU to app

| Type | Name | Payload |
| --- | --- | --- |
| 1 | `HELLO` | `u32 version (=1); u32 qemu_pid; char session[40]` (NUL-padded, from env `CLASSICMAC_SESSION`, else empty). 48 bytes. |
| 2 | `SURFACE` | `u32 width; u32 height; u32 stride; u32 format; u64 size` (24 bytes) + one descriptor via `SCM_RIGHTS`. `format` is always `0x34325258` (DRM XRGB8888: bytes B, G, R, X in memory), `stride = width * 4`, `size` is the mapping size (page multiple). The buffer already holds the complete current frame. A new buffer comes with every geometry change; a reconnect gets the current one again. |
| 3 | `DAMAGE` | `u32 seq; u32 count; {i32 x, y, w, h}[count]`: rectangles already written into the shared buffer. At most 64 rects (their bounding rect beyond that). `seq` increments per message sent; do not rely on it being contiguous. |
| 4 | `GUEST_EVENT` | UTF-8 JSON, no NUL. `{"event":"clock","clock":"icount"\|"real-time"}` after every HELLO (plus `"handoff_uptime_ms":n` once handed off); `{"event":"boot-handoff","clock":"real-time","uptime_ms":n,"virtual_ms":n}` when ClassicMac's accelerated boot hands off; `{"event":"resize-request","width":w,"height":h,"method":"ui-info"\|"re-request"\|"unchanged"\|"unsupported"}` after each RESIZE; `{"event":"pointer-mode","relative":true\|false}` after HELLO and when QEMU's active mouse mode changes. |
| 5 | `CURSOR` | `i32 hot_x; i32 hot_y; u32 width; u32 height` then `width * height` u32 pixels exactly as QEMUCursor stores them: `0xAARRGGBB`, not premultiplied, rows top to bottom. |
| 6 | `MOUSE` | `i32 x; i32 y; u32 visible`: the hardware cursor position in surface pixels as the device reports it (QEMU's convention: the hot spot's position; draw the image at `x - hot_x, y - hot_y`). ClassicMac's std-VGA always reports hot spot (0, 0). |

### App to QEMU

| Type | Name | Payload |
| --- | --- | --- |
| 16 | `KEY` | `u32 qcode` (QAPI `QKeyCode` value); `u32 down` |
| 17 | `POINTER_ABS` | `u32 x; u32 y` in current surface pixels (QEMU maps each to the centre of its span on the absolute axis; needs the virtio tablet) |
| 18 | `BUTTON` | `u32 button` (0 left, 1 middle, 2 right, 3 wheel-up, 4 wheel-down); `u32 down` |
| 19 | `RESIZE` | `u32 width; u32 height` (64..16384) to `dpy_set_ui_info`, the std-VGA host-resize request. Repeating a size the guest has since left re-arms the request through `host-resize-request` (`"method":"re-request"`). A new `SURFACE` confirms the switch. The ATI card has no host-resize channel (`"unsupported"`). |
| 20 | `RELEASE_ALL` | none; releases every key and button QEMU believes held |
| 21 | `REFRESH_RATE` | `u32 interval_ms` (clamped 4..1000; 16 by default while connected, 500 while disconnected) |
| 22 | `POINTER_REL` | `i32 dx; i32 dy` (clamped to +-32767), for the USB/ADB relative mouse |

Messages from the app with more payload than listed are accepted (extra
bytes ignored); shorter ones are ignored; a payload over 256 bytes ends the
connection.

### Behaviour

- **Connect**: HELLO, then (as soon as the console has a surface) SURFACE
  with its descriptor, a full-surface DAMAGE, the `clock` and `pointer-mode`
  events, the current CURSOR (if the device defined one) and MOUSE.
- **Disconnect** (EOF or error): QEMU releases every key and button the app
  held, drops its queue and reconnects every 100 ms; the next connection
  gets the full greeting again. One app connection at a time.
- **Frames**: QEMU refreshes every `REFRESH_RATE` ms; each refresh copies
  the changed tiles into the buffer and sends one DAMAGE (or none). The app
  may see tearing within one refresh. QEMU's placeholder surface ("display
  output is not active") is delivered as black.
- **Cursor**: std-VGA with `-global VGA.hardware-cursor=on` sends CURSOR
  when the guest defines its image and MOUSE on every move and
  show/hide. The ATI card (`guest_hwcursor=on`) draws its cursor into the
  frame: no CURSOR/MOUSE. Mac OS 9.2.1 with ClassicMac's NDRV does not use
  the QEXT hardware cursor today (see Known limitations), so in practice
  the std-VGA cursor is drawn into the frame as well.

### Reading it from the app

- Receive with `recvmsg` and keep every descriptor that arrives in a FIFO;
  a SURFACE takes the oldest one when its 24-byte payload is complete.
  `mmap(size, PROT_READ, MAP_SHARED)`; unmap the previous buffer.
- Upload only DAMAGE rectangles; when several DAMAGEs are pending, their
  union is enough (the buffer always holds the newest pixels).

## Testing

```sh
PY=/opt/homebrew/opt/python@3.12/bin/python3.12
Q=vendor/qemu/build/qemu-system-ppc; B=vendor/qemu/pc-bios
$PY display/tests/test_display_gate.py $Q $B            # also run by build-qemu.sh
$PY display/tests/test_cursor_plumbing.py $Q $B output/display
$PY display/tests/guest_display_test.py --qemu $Q --bios $B \
    --disk scratch/machines/os921.img --out output/display/vga \
    --scenario vga --depth 15 --drag-x 300 --drag-y 35
$PY display/tests/guest_display_test.py --qemu $Q --bios $B \
    --disk scratch/machines/deluxe-v6.img --out output/display/ati \
    --scenario ati --size 1024x768 --quake
```

Disks are APFS-cloned into the output directory and deleted afterwards;
every QEMU started is stopped. Socket paths live under a short `/tmp`
directory because of the `sun_path` limit.

## Known limitations

- **Mac OS does not engage the std-VGA hardware cursor.** With
  `VGA.hardware-cursor=on` the NDRV advertises the QEXT cursor, but on
  Mac OS 9.2.1 no cursor definition or move ever reaches QEMU (no
  `dpy_cursor_define`/`dpy_mouse_set` after the initial registration, at
  Thousands and Millions); QuickDraw draws a software cursor into the frame.
  The guest's choice does not depend on the frontend, so this predates the
  backend (Cocoa's cursor layer never gets an image either). A likely cause
  is the NDRV's `HardwareCursorDescriptorRec` (32-bit, no colour encodings,
  no transparent/invert special encodings), which
  `VSLPrepareCursorForHardwareCursor` may refuse; not investigated further
  (needs a debug NDRV). The backend path itself is verified by
  `tests/test_cursor_plumbing.py`.
- The 4 MB drop-and-repaint backstop is not exercised by the tests (damage
  coalescing keeps the queue small; it needs megabytes of queued cursor
  images or events).
- The Linux build of the file (sealed memfd, `MSG_NOSIGNAL`) is not
  compiled here.
