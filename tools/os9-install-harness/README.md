# Mac OS 9 install harness

This headless harness reproduces and verifies the Power Mac MacIO IDE/DBDMA
installer race. It requires a locally built QEMU and user-supplied Mac OS 9
installation media; neither disk images nor installation media are committed.

Start a fresh machine with the structured storage trace enabled:

```bash
ISO=/path/to/macos-9.iso tools/os9-install-harness/boot.sh
```

Use Drive Setup from the CD to initialize the destination. Quit Drive Setup,
return to the CD's top-level window, then run the fixed-coordinate driver:

```bash
python3 tools/os9-install-harness/install.py \
  --qmp /tmp/os9-harness/qmp.sock \
  --shots /tmp/os9-harness/shots
```

The driver opens Installer Options and turns off **Update Apple Hard Disk
Drivers** before starting the regular installation. Inspect progress through
QMP screenshots and block statistics. A successful run must reach the explicit
"installation process has finished" dialog and the installed disk must boot.

Validate trace invariants after QEMU exits:

```bash
python3 tools/os9-install-harness/analyze.py \
  --require-delay /tmp/os9-harness/trace.log
```

Useful environment overrides:

- `MACIO_DELAY_NS=0` reproduces the zero-latency race on affected media.
- `MACIO_DELAY_NS=1000000` selects the fixed 1 ms completion latency.
- `KEEP_DISK=1` preserves an already initialized/test disk.
- `CD_INDEX=2` places the installer CD on the second MacIO IDE channel.
- `DISK`, `QMP_SOCK`, `MON_SOCK`, and `TRACE` isolate parallel trials.
