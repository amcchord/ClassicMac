# Mac OS 9 boot performance

ClassicMac shortens a measured Mac OS 9.2 startup at two layers: QEMU now
invalidates PowerPC BAT mappings as ranges, and the app asks Mac OS to shut
down natively so the boot volume remains clean. An optional Extension Manager
trim is possible, but its measured benefit is much smaller.

## Controlled result

The test used the same Mac OS 9.2 disk, PowerPC 7400, 512 MiB of RAM,
1024 x 768 Thousands-color display, GXMetal transport, ClassicMac Tools,
Virtio tablet, shared folder, and Sungem network device. Every run cloned the
source image before QEMU opened it. Finder readiness was detected from QEMU's
normal framebuffer through HMP `screendump`; the test never booted the source
image itself.

| Configuration | Finder runs (seconds) | Mean | Change |
| --- | --- | ---: | ---: |
| Installed ClassicMac 2.0.1, clean HFS+ volume | 29.15, 28.66, 28.64 | 28.82 | baseline |
| BAT range invalidation, clean volume | 26.61, 26.63, 27.09 | 26.78 | -2.04 s (-7.1%) |
| BAT range invalidation plus hardware-extension trim | 26.11, 26.13, 26.09 | 26.11 | -2.71 s (-9.4%) |

These comparison runs used QEMU's `cache=none` mode to prevent a preceding
host mount from warming macOS's file cache. Normal cached runs produced the
same ordering. The reproducible harness is `scripts/benchmark-os9-boot.py`.

## QEMU PowerPC BAT invalidation

During Mac OS startup, the PowerPC guest changes block-address-translation
(BAT) registers frequently. QEMU 11.0.2 invalidated every 4 KiB page in the
old and new BAT regions independently. A 20-second Apple Silicon sample found
3,037 samples inside page-at-a-time TLB invalidation, making it the dominant
named host-side boot hotspot.

`powermac/tcg-ppc-bat-range-flush.patch` sends the same contiguous virtual
address region through QEMU's existing `tlb_flush_range_by_mmuidx()` API. The
API takes the TLB lock once, chooses a bounded scan or full flush based on the
range and TLB size, and clears the jump cache efficiently. The optimized
profile recorded 94 samples in range invalidation instead of 3,037 page-flush
samples. Guest mappings and invalidation coverage are unchanged.

## Clean app-controlled shutdown

The former ClassicMac **Shut Down** action sent SIGTERM to QEMU. That behaved
like pulling the plug: HFS+ left its `kHFSVolumeUnmountedBit` clear, so Mac OS
performed extra recovery work during the next startup. On the test image this
added roughly 5–6 seconds and about 2,300 IDE DMA completions.

ClassicMac now sends QEMU's emulated ADB Power key, waits for Mac OS's native
Shut Down dialog, and confirms its default button. The guest flushes the disk
and exits through CUDA/PMU normally. The retained 15-second forced-off fallback
only runs if the guest is crashed or refuses to shut down. A guest-produced
test changed the HFS+ attributes from `0x80000000` to `0x80000100`; its next
boot reached Finder in 26.63 seconds without any host mount or repair.

Shutting down from the Special menu inside Mac OS remains equally safe.

## Optional Extension Manager profile

The universal Mac OS 9.2 install contains drivers for hardware ClassicMac does
not expose. Disabling the following complete set reduced a clean optimized
boot by another 0.67 seconds (about 2.5%) and roughly 250 IDE DMA completions
in this image:

- NVIDIA: `NVIDIA 2D Acceleration`, `NVIDIA DVD Accelerator`, `NVIDIA Driver`,
  `NVIDIA Engine`, `NVIDIA OpenGL`, and `NVIDIA Video Accelerator`.
- DVD and external video: `DVD AutoLauncher`, `DVD Navigation Manager ATI`,
  `DVD Navigation Manager NV`, `DVD Region Manager`, `DVD Video Interface`,
  `DVDRuntimeLib`, `Theater Mode`, `QuickTime FireWire DV Enabler`, and
  `QuickTime FireWire DV Support`.
- Wireless and telecom: `AirPort AP`, `AirPort AP Support`, `AirPort Driver`,
  `Internal V.90 Modem`, `PowerMac G3 Modem`, `iMac Modem Extension`,
  `IrDA Tool`, and `IrDALib`.
- Unavailable removable buses: `Iomega Driver`, `USB Device Extension`,
  `USB Mass Storage Support`, `USB Software Locator`, `USB Support`, and
  `USBAppleMonitorModule`.

This remains optional because the gain is modest and a user's System Folder
may be portable to real hardware or another emulator. Use Extensions Manager
to create a duplicate set before disabling anything, which makes rollback
straightforward.

Keep the networking extensions (`Apple Enet`, Open Transport, DNS), core sound
and QuickTime components, CarbonLib, InputSprocket/DrawSprocket, QuickDraw 3D,
RAVE, and every GXMetal component. They support devices or games ClassicMac
actually runs.
