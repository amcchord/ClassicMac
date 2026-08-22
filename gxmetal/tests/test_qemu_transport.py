#!/usr/bin/env python3
"""Black-box realization checks for the QEMU GXMetal PCI transport."""

import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time


VNC_ENCODING_POINTER_TYPE_CHANGE = -257
GXMETAL_BAR2_REGISTER_OFFSET = 0x0B00
GXMETAL_RELATIVE_INPUT_REGISTER = 0x40


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


def receive_exact(connection, length):
    chunks = []
    received = 0
    while received < length:
        chunk = connection.recv(length - received)
        if not chunk:
            raise AssertionError("QEMU closed a test socket unexpectedly")
        chunks.append(chunk)
        received += len(chunk)
    return b"".join(chunks)


def wait_for_socket(path, process, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.exists(path):
            return
        if process.poll() is not None:
            stderr = process.stderr.read().decode("utf-8", "replace")
            raise AssertionError("QEMU exited during setup: %s" % stderr)
        time.sleep(0.05)
    raise AssertionError("QEMU did not create %s" % path)


def qmp_execute(stream, command, arguments=None):
    request = {"execute": command}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write((json.dumps(request) + "\n").encode("ascii"))
    stream.flush()
    while True:
        reply = json.loads(stream.readline())
        if "error" in reply:
            raise AssertionError("QMP %s failed: %s" % (command, reply))
        if "return" in reply:
            return reply["return"]


def connect_vnc_pointer_mode(path):
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.settimeout(5)
    connection.connect(path)
    version = receive_exact(connection, 12)
    assert version.startswith(b"RFB 003.")
    connection.sendall(b"RFB 003.008\n")
    security_count = receive_exact(connection, 1)[0]
    security_types = receive_exact(connection, security_count)
    assert 1 in security_types
    connection.sendall(b"\x01")
    assert receive_exact(connection, 4) == b"\0\0\0\0"
    connection.sendall(b"\x01")
    receive_exact(connection, 4)
    receive_exact(connection, 16)
    name_length = struct.unpack(">I", receive_exact(connection, 4))[0]
    receive_exact(connection, name_length)
    connection.sendall(struct.pack(">BBHi", 2, 0, 1,
                                   VNC_ENCODING_POINTER_TYPE_CHANGE))
    return connection


def receive_vnc_pointer_mode(connection):
    while True:
        message_type = receive_exact(connection, 1)[0]
        if message_type == 0:
            receive_exact(connection, 1)
            rectangles = struct.unpack(">H", receive_exact(connection, 2))[0]
            for _ in range(rectangles):
                x, _y, width, height, encoding = struct.unpack(
                    ">HHHHi", receive_exact(connection, 12))
                if encoding == VNC_ENCODING_POINTER_TYPE_CHANGE:
                    return x != 0
                if encoding == 0:
                    receive_exact(connection, width * height * 4)
                    continue
                raise AssertionError("unexpected VNC encoding %d" % encoding)
        elif message_type == 2:
            continue
        elif message_type == 3:
            receive_exact(connection, 3)
            length = struct.unpack(">I", receive_exact(connection, 4))[0]
            receive_exact(connection, length)
        else:
            raise AssertionError("unexpected VNC message %d" % message_type)


def qtest_command(connection, command):
    connection.sendall((command + "\n").encode("ascii"))
    reply = bytearray()
    while not reply.endswith(b"\n"):
        reply.extend(receive_exact(connection, 1))
    assert reply == b"OK\n", reply


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


def test_relative_input_handoff(qemu):
    with tempfile.TemporaryDirectory(prefix="gxmetal-input-") as temporary:
        vnc_path = os.path.join(temporary, "vnc.sock")
        qmp_path = os.path.join(temporary, "qmp.sock")
        qtest_path = os.path.join(temporary, "qtest.sock")
        process = subprocess.Popen(
            base_args(qemu) + [
                "-device", "VGA,gxmetal=on",
                "-device", "virtio-tablet-pci",
                "-vnc", "unix:%s" % vnc_path,
                "-qmp", "unix:%s,server=on,wait=off" % qmp_path,
                "-qtest", "unix:%s,server=on,wait=off" % qtest_path,
            ],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        qmp = None
        qtest = None
        vnc = None
        try:
            for path in (qmp_path, qtest_path, vnc_path):
                wait_for_socket(path, process)

            qmp = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            qmp.connect(qmp_path)
            qmp_stream = qmp.makefile("rwb", buffering=0)
            greeting = json.loads(qmp_stream.readline())
            assert "QMP" in greeting
            qmp_execute(qmp_stream, "qmp_capabilities")
            qmp_execute(qmp_stream, "cont")

            register_address = None
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                device = find_qemu_vga(qmp_execute(qmp_stream, "query-pci"))
                for region in device["regions"]:
                    if region["bar"] == 2 and region["address"] >= 0:
                        register_address = region["address"]
                        break
                if register_address is not None:
                    break
                time.sleep(0.05)
            assert register_address is not None, "OpenBIOS did not map GXMetal"

            mice = qmp_execute(qmp_stream, "query-mice")
            tablet = next(mouse for mouse in mice if mouse["absolute"])
            qmp_execute(qmp_stream, "human-monitor-command", {
                "command-line": "mouse_set %d" % tablet["index"]})

            vnc = connect_vnc_pointer_mode(vnc_path)
            assert receive_vnc_pointer_mode(vnc) is True
            qtest = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            qtest.connect(qtest_path)

            relative_register = (register_address +
                                 GXMETAL_BAR2_REGISTER_OFFSET +
                                 GXMETAL_RELATIVE_INPUT_REGISTER)
            qtest_command(qtest, "writel 0x%x 0x1" % relative_register)
            assert receive_vnc_pointer_mode(vnc) is False
            qtest_command(qtest, "writel 0x%x 0x0" % relative_register)
            assert receive_vnc_pointer_mode(vnc) is True
            qmp_execute(qmp_stream, "quit")
        finally:
            for connection in (vnc, qtest, qmp):
                if connection is not None:
                    connection.close()
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: test_qemu_transport.py /path/to/qemu-system-ppc")
    test_bar_layout(sys.argv[1])
    test_requires_mmio(sys.argv[1])
    test_relative_input_handoff(sys.argv[1])
    print("GXMetal QEMU transport: realization and input handoff tests passed")


if __name__ == "__main__":
    main()
