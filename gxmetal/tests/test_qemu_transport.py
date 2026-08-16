#!/usr/bin/env python3
"""Black-box realization checks for the QEMU GXMetal PCI transport."""

import json
import subprocess
import sys


def base_args(qemu):
    return [
        qemu,
        "-M", "mac99,via=cuda,audiodev=audio0",
        "-nodefaults",
        "-display", "none",
        "-audiodev", "none,id=audio0",
        "-S",
    ]


def find_qemu_vga(pci_buses):
    for bus in pci_buses:
        for device in bus.get("devices", []):
            ident = device.get("id", {})
            if ident.get("vendor") == 0x1234 and ident.get("device") == 0x1111:
                return device
    raise AssertionError("QEMU VGA device was not realized")


def test_bar_layout(qemu):
    commands = "\n".join([
        '{"execute":"qmp_capabilities"}',
        '{"execute":"query-pci"}',
        '{"execute":"quit"}',
        "",
    ])
    result = subprocess.run(
        base_args(qemu) + ["-device", "VGA,gxmetal=on", "-qmp", "stdio"],
        input=commands, text=True, capture_output=True, timeout=10,
        check=False)
    if result.returncode != 0:
        raise AssertionError("QEMU realization failed: %s" % result.stderr)

    replies = []
    for line in result.stdout.splitlines():
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(message.get("return"), list):
            replies = message["return"]
    device = find_qemu_vga(replies)
    regions = {region["bar"]: region for region in device["regions"]}
    assert regions[2]["size"] == 0x1000
    assert regions[4]["size"] == 0x400000
    assert regions[4]["prefetch"] is True


def test_requires_mmio(qemu):
    result = subprocess.run(
        base_args(qemu) + ["-device", "VGA,gxmetal=on,mmio=off"],
        text=True, capture_output=True, timeout=10, check=False)
    assert result.returncode != 0
    assert "GXMetal requires the VGA MMIO BAR" in result.stderr


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: test_qemu_transport.py /path/to/qemu-system-ppc")
    test_bar_layout(sys.argv[1])
    test_requires_mmio(sys.argv[1])
    print("GXMetal QEMU transport: realization tests passed")


if __name__ == "__main__":
    main()
