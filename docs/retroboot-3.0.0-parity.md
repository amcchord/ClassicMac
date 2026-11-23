# RetroBoot 3.0.0 compatibility

The engine audit compares ClassicMac 4.7.0 with the RetroBoot 3.0.0 source
at [37664e17](https://github.com/amcchord/RetroBoot/tree/v3.0.0).
The public repository snapshot contains the same engine patches as the
original release. RetroBoot's 3.0.0 changes after that snapshot are catalog
configuration and appliance setup.

| Area | ClassicMac implementation |
| --- | --- |
| CPU and display | The 2.0.1 import already carries PowerPC fast paths, dirty-store backoff, 128-bit splitting, packed VGA rows and damage tracking. The ClassicMac display backend already reduces refresh work at idle. |
| Power Mac storage | PIO status polling completes an outstanding disk read during hard-disk startup. ATAPI signals a zero-byte-count abort so the guest can finish its CD probe. Installer startup keeps its existing DMA delay. |
| USB idle | OHCI batches idle frames on empty buses and on buses with idle HID devices. An input event resumes normal frame scheduling. |
| Quadra | The display keeps the mode and surface consistent with each dirty snapshot. Timer 2 allows up to 20 ms for slow translation while retaining its early release condition. |
| Shared folders | The guest driver caches directory details and refreshes on host changes. Both the Quadra ROM and Power Mac loader were rebuilt from the pinned classicvirtio revision with RetroBoot's folder date and cache patches. |
| Mac OS X | The 4.7.0 QEMU 10 patch matches the RetroBoot 3.0.0 engine patch byte for byte. The desktop profile selects standard VGA. RetroBoot's separate AMD core placement, ALSA device tuning and hardware presenter are appliance policies. Packed 1555 acceleration is an experimental branch change. |

## Checks

The live RetroBoot catalog listed 13 current editions. All returned the
advertised length in HTTPS byte-range responses. The combined desktop Library
listed 18 unique editions. Production Swift and Qt import paths verified
and installed System 7.5.5, Mac OS 9.2.1 and Tiger 10.4.11. The Tiger import
preserved its startup memory. On APFS, the Swift installer releases blocks
that the filesystem can allocate between neighboring sparse writes.

The Apple Silicon QEMU build passed its device, firmware, CGL and display
checks, including 598,030 exact Radeon cursor pixels. The Quadra snapshot
regression passed nine mode and surface cases. Both classicvirtio artifacts
built with the existing Retro68 toolchain.

Three alternating pairs on an Apple Silicon host used disposable copies of
the same RetroBoot Mac OS 9.2.1 image, the same devices and real-time
emulation. Median time to Finder was 14.543 s with the signed 4.7.0 engine
and 9.440 s with the rebuilt engine and PIO status drain enabled. Every
boot shut down normally. This measures the combined engine changes on
this host.

The imported System 7.5.5 disk reached Finder on the built-in and enhanced
Quadra framebuffers. Both accepted the ADB power key and displayed the
guest's shutdown dialog. The updated Power Mac engine also booted an
existing Mac OS 9.2.1 installer disc. Tiger reached the expected Finder pixels using
its imported startup memory and opened Home after a keyboard command.
The final macOS suite ran 147 Swift tests with three optional fixture
skips and zero failures. Native self-tests and 11 Python tests passed.

Three further alternating pairs used ClassicMac's normal accounted startup
clock and its Finder handoff to real time. Median time to Finder went from
9.806 s to 8.952 s. Each boot returned to real time and shut down normally.

[Linux package and engine checks](https://github.com/amcchord/ClassicMac/actions/runs/45094919620) ·
[Windows package and engine checks](https://github.com/amcchord/ClassicMac/actions/runs/45094922315)
