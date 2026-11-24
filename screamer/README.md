# Power Mac sound timing

The base Screamer port remains in `screamer.c` and `screamer.h`. The build
applies these upstream patches in order after `integration.patch`: volume,
idle-stop, paced-dma, realtime, irq-hold, and later lead-strict (from RetroBoot
2.0.1). They come from RetroBoot (`emulator/patches/`) under
GPL-2.0-or-later. Existing source copyright and license notices are retained.

Instant completion of every queued DMA output merged the guest's two
half-buffer interrupts and left its frame counter unchanged. The new output
runs on the codec sample timeline, exposes advancing frame stamps, and keeps
interrupts separate while the guest services them. Host buffering tolerates
jitter without slowing the guest clock. Volume/mute changes apply at the
queued stream position and use the newly written codec value.

`tests/qtest_screamer.py` exercises the actual QEMU device without guest media:
paused-clock behavior, two distinct half completions and stamps, continuous
sample rate, and interrupt service/backpressure. Run it with a fresh output
folder and the prepared QEMU binary/firmware:

```sh
python3 screamer/tests/qtest_screamer.py --qemu vendor/qemu/build/qemu-system-ppc \
  --share vendor/qemu/pc-bios --run output/screamer-test
```

The device properties retain upstream defaults and diagnostic off switches:
`paced-dma`, `frame-count`, `irq-hold-ms`, `dma-lead-us`, `resync-ms`,
`host-lead-ms`, `host-lead-max-ms`, `host-lead-strict`, and `idle-stop-ms`.
Only Screamer consults the new DBDMA interrupt-service record; other DMA
clients retain their behavior.

| Patch | SHA-256 |
| --- | --- |
| `retroboot-screamer-volume.patch` | `44d2922fb693f0283797e582c82645cca0f0a87b2307bd2ebe35ca9417f482f6` |
| `retroboot-screamer-idle-stop.patch` | `a332feb7a2531d9d95b8bed3f46689f05a3fdbd61807a79efc749f41bb061529` |
| `retroboot-screamer-paced-dma.patch` | `a9f99c44910ef58a9c9019b8791826aae037b60c93db177fbcae723295f1b261` |
| `retroboot-screamer-realtime.patch` | updated locally (rate-change host clock reset) |
| `retroboot-screamer-irq-hold.patch` | updated locally (reset `holding` flag) |
| `retroboot-screamer-lead-strict.patch` | `fb609f4eb35e612c79a92e47e0edf0b9a789a93115052303f1093c3a93d5a893` |
