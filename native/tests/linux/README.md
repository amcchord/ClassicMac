# Ubuntu desktop qualification

Use an isolated Ubuntu desktop VM with Python 3, Pillow, psutil and python-xlib.
Keep the desktop unlocked, set a fixed resolution and close unrelated windows.
The qualification runners always use new preference and machine directories.
Never supply an original user machine as a benchmark source.

Run the exact portable AppImage, without a runtime override:

```sh
python3 native/scripts/run-ui-ux.py ./ClassicMac-4.6.3-linux-x64.AppImage --output /absolute/qa/ui-1x
python3 native/scripts/run-ui-ux.py ./ClassicMac-4.6.3-linux-x64.AppImage --output /absolute/qa/ui-2x --pixel-scale 2
```

Each run checks all 150 Library/machine-management steps. Compare the window
and real desktop captures with `native/scripts/check-ui-pixel-grid.py`.
These are interface zoom tests; they do not certify monitor DPI transitions.
The fixture archive and unresponsive child process contain no guest OS.

The opt-in public network flow downloads from the real Library. It pauses and
resumes Deluxe, then installs System 7.5.5, starts it through the Library,
checks pause/resume and closing into Power, and requests a clean shutdown:

```sh
python3 native/scripts/run-ui-ux.py ./ClassicMac-4.6.3-linux-x64.AppImage --output /absolute/qa/network --recipe native/tests/linux/public-network-flow.json
```

## Performance

Use an otherwise idle desktop, identical stopped source images and the same
virtual CPU/memory/display. The benchmark makes sparse disposable clones and
alternates baseline/candidate order. It measures actual desktop-root pixels:
engine start to visible Finder, XTest mouse-down to visible Apple menu, six
seconds of idle CPU/RSS, and five seconds of paused-engine CPU. A ready RFB
connection or successful QMP reply does not count as Finder or input response.
Each run also pauses, resumes and shuts the guest down normally.

```sh
python3 native/scripts/benchmark-linux.py --baseline ./ClassicMac-4.6.2-linux-x64.AppImage --candidate ./ClassicMac-4.6.3-linux-x64.AppImage --machine '/absolute/qa/System 7.classic' --output /absolute/qa/system7-performance --runs 3 --hold-seconds 30
python3 native/scripts/benchmark-linux.py --candidate-only --candidate ./ClassicMac-4.6.3-linux-x64.AppImage --machine '/absolute/qa/Mac OS 9.classic' --output /absolute/qa/os9-performance --runs 3 --hold-seconds 60
```

Mac OS 9 uses candidate-only measurements because 4.6.2 Linux stalls with the
default ATI/network/Tools/tablet combination. 4.6.3 assigns the tablet a separate
PCI interrupt, as Windows already did. Retain the old failed run and matching
new-default boot evidence; do not invent old-version input/boot timings.

CPU is percent of one logical core. RSS includes shared libraries. Classic
System 7 may use a full emulation core while idle; Pause should reduce that
work. VM software graphics do not establish physical GPU/3D performance.
Retain package hashes, source configuration, repeated samples, desktop images,
host load and virtualization details alongside the summary. Page caches warm
across repetitions; this is a process/guest boot test, not a cold-storage test.
