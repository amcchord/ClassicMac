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
GXMETAL_RING_OFFSET = 0x1000
GXMETAL_PRODUCER_REGISTER = 0x20
GXMETAL_CONSUMER_REGISTER = 0x24
GXMETAL_DOORBELL_REGISTER = 0x28
GXMETAL_STATUS_REGISTER = 0x2C
GXMETAL_ERROR_REGISTER = 0x30
GXMETAL_RELATIVE_INPUT_REGISTER = 0x40
GXMETAL_INPUT_BUTTONS_REGISTER = 0x44
GXMETAL_INPUT_RELATIVE_X_REGISTER = 0x48
GXMETAL_INPUT_RELATIVE_Y_REGISTER = 0x4C
GXMETAL_INPUT_BUTTONS_DOWN_REGISTER = 0x50
GXMETAL_INPUT_BUTTONS_UP_REGISTER = 0x54
GXMETAL_OP_CONTEXT_CREATE = 0x0100
GXMETAL_OP_CONTEXT_DESTROY = 0x0101
QEXT_BAR2_OFFSET = 0x600
QEXT_CURSOR_VISIBLE_REGISTER = 12 * 4
QEXT_CURSOR_COMMAND_REGISTER = 13 * 4
QEXT_CURSOR_MOVE = 0x2


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


def current_mouse(stream):
    return next(mouse for mouse in qmp_execute(stream, "query-mice")
                if mouse["current"])


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


def qtest_readl(connection, address):
    connection.sendall(("readl 0x%x\n" % address).encode("ascii"))
    reply = bytearray()
    while not reply.endswith(b"\n"):
        reply.extend(receive_exact(connection, 1))
    fields = reply.decode("ascii").split()
    assert fields[0] == "OK", reply
    return int(fields[1], 0)


def qtest_write(connection, address, data):
    qtest_command(connection, "write 0x%x %d 0x%s" % (
        address, len(data), data.hex()))


def qtest_readl_le(connection, address):
    value = qtest_readl(connection, address)
    return int.from_bytes(value.to_bytes(4, "big"), "little")


def qtest_writel_le(connection, address, value):
    qtest_write(connection, address, struct.pack("<I", value))


def context_packet(opcode, context_id, sequence):
    if opcode == GXMETAL_OP_CONTEXT_DESTROY:
        return struct.pack("<HHIII", opcode, 16, 16, context_id, sequence)
    assert opcode == GXMETAL_OP_CONTEXT_CREATE
    return struct.pack("<HHIII8I", opcode, 16, 48, context_id, sequence,
                       32, 32, 64, 1, 0, 0, 0, 0)


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
    assert regions[4]["size"] == 0x800000
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
            shared_address = None
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                device = find_qemu_vga(qmp_execute(qmp_stream, "query-pci"))
                for region in device["regions"]:
                    if region["bar"] == 2 and region["address"] >= 0:
                        register_address = region["address"]
                    elif region["bar"] == 4 and region["address"] >= 0:
                        shared_address = region["address"]
                if register_address is not None and shared_address is not None:
                    break
                time.sleep(0.05)
            assert register_address is not None, "OpenBIOS did not map GXMetal"
            assert shared_address is not None, "OpenBIOS did not map shared VRAM"

            mice = qmp_execute(qmp_stream, "query-mice")
            tablet = next(mouse for mouse in mice if mouse["absolute"])
            qmp_execute(qmp_stream, "human-monitor-command", {
                "command-line": "mouse_set %d" % tablet["index"]})
            assert current_mouse(qmp_stream)["absolute"] is True

            vnc = connect_vnc_pointer_mode(vnc_path)
            assert receive_vnc_pointer_mode(vnc) is True
            qtest = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            qtest.connect(qtest_path)

            gxmetal_registers = (register_address +
                                 GXMETAL_BAR2_REGISTER_OFFSET)
            button_register = (gxmetal_registers +
                               GXMETAL_INPUT_BUTTONS_REGISTER)
            qmp_execute(qmp_stream, "input-send-event", {
                "events": [{"type": "btn", "data": {
                    "button": "left", "down": True}}]})
            assert qtest_readl_le(qtest, button_register) == 0x1
            qmp_execute(qmp_stream, "input-send-event", {
                "events": [{"type": "btn", "data": {
                    "button": "right", "down": True}}]})
            assert qtest_readl_le(qtest, button_register) == 0x3
            qmp_execute(qmp_stream, "input-send-event", {
                "events": [
                    {"type": "btn", "data": {
                        "button": "left", "down": False}},
                    {"type": "btn", "data": {
                        "button": "right", "down": False}},
                    {"type": "btn", "data": {
                        "button": "middle", "down": True}},
                ]})
            assert qtest_readl_le(qtest, button_register) == 0x4
            qmp_execute(qmp_stream, "input-send-event", {
                "events": [{"type": "btn", "data": {
                    "button": "middle", "down": False}}]})
            assert qtest_readl_le(qtest, button_register) == 0

            create = context_packet(GXMETAL_OP_CONTEXT_CREATE, 1, 1)
            qtest_write(qtest, shared_address + GXMETAL_RING_OFFSET, create)
            qtest_writel_le(qtest,
                            gxmetal_registers + GXMETAL_PRODUCER_REGISTER,
                            len(create))
            qtest_writel_le(qtest,
                            gxmetal_registers + GXMETAL_DOORBELL_REGISTER, 1)
            consumer = qtest_readl_le(qtest, gxmetal_registers +
                                      GXMETAL_CONSUMER_REGISTER)
            status = qtest_readl_le(qtest, gxmetal_registers +
                                    GXMETAL_STATUS_REGISTER)
            error = qtest_readl_le(qtest, gxmetal_registers +
                                   GXMETAL_ERROR_REGISTER)
            assert (consumer, status, error) == (len(create), 1, 0), (
                consumer, status, error)

            # A live accelerated context alone remains seamless. Hiding the
            # guest cursor is the game signal that switches host motion to
            # captured relative deltas.
            qtest_writel_le(qtest,
                            register_address + QEXT_BAR2_OFFSET +
                            QEXT_CURSOR_VISIBLE_REGISTER, 0)
            qtest_writel_le(qtest,
                            register_address + QEXT_BAR2_OFFSET +
                            QEXT_CURSOR_COMMAND_REGISTER,
                            QEXT_CURSOR_MOVE)
            assert receive_vnc_pointer_mode(vnc) is False
            # The tablet stays selected while captured. REL motion bypasses
            # the emulated Mac cursor and is consumed once through GXMetal.
            assert current_mouse(qmp_stream)["absolute"] is True
            button_down_register = (gxmetal_registers +
                                    GXMETAL_INPUT_BUTTONS_DOWN_REGISTER)
            button_up_register = (gxmetal_registers +
                                  GXMETAL_INPUT_BUTTONS_UP_REGISTER)
            # A complete click between guest polls remains observable as two
            # read-and-clear edges even though the final held state is up.
            qmp_execute(qmp_stream, "input-send-event", {
                "events": [
                    {"type": "btn", "data": {
                        "button": "left", "down": True}},
                    {"type": "btn", "data": {
                        "button": "left", "down": False}},
                ]})
            assert qtest_readl_le(qtest, button_register) == 0
            assert qtest_readl_le(qtest, button_down_register) == 0x1
            assert qtest_readl_le(qtest, button_up_register) == 0x1
            assert qtest_readl_le(qtest, button_down_register) == 0
            assert qtest_readl_le(qtest, button_up_register) == 0
            relative_x_register = (gxmetal_registers +
                                   GXMETAL_INPUT_RELATIVE_X_REGISTER)
            relative_y_register = (gxmetal_registers +
                                   GXMETAL_INPUT_RELATIVE_Y_REGISTER)
            qmp_execute(qmp_stream, "input-send-event", {
                "events": [
                    {"type": "rel", "data": {"axis": "x", "value": 17}},
                    {"type": "rel", "data": {"axis": "y", "value": -9}},
                ]})
            assert qtest_readl_le(qtest, relative_x_register) == 17
            assert qtest_readl_le(qtest, relative_y_register) == 0xFFFFFFF7
            # Relative-axis registers are read-and-clear accumulators.
            assert qtest_readl_le(qtest, relative_x_register) == 0
            assert qtest_readl_le(qtest, relative_y_register) == 0
            qtest_writel_le(qtest,
                            register_address + QEXT_BAR2_OFFSET +
                            QEXT_CURSOR_VISIBLE_REGISTER, 1)
            qtest_writel_le(qtest,
                            register_address + QEXT_BAR2_OFFSET +
                            QEXT_CURSOR_COMMAND_REGISTER,
                            QEXT_CURSOR_MOVE)
            assert receive_vnc_pointer_mode(vnc) is True
            assert current_mouse(qmp_stream)["absolute"] is True

            # Context teardown must restore absolute input even when an
            # exiting or crashing game leaves the cursor hidden.
            qtest_writel_le(qtest,
                            register_address + QEXT_BAR2_OFFSET +
                            QEXT_CURSOR_VISIBLE_REGISTER, 0)
            qtest_writel_le(qtest,
                            register_address + QEXT_BAR2_OFFSET +
                            QEXT_CURSOR_COMMAND_REGISTER,
                            QEXT_CURSOR_MOVE)
            assert receive_vnc_pointer_mode(vnc) is False
            assert current_mouse(qmp_stream)["absolute"] is True
            destroy = context_packet(GXMETAL_OP_CONTEXT_DESTROY, 1, 2)
            qtest_write(qtest, shared_address + GXMETAL_RING_OFFSET +
                        len(create), destroy)
            qtest_writel_le(qtest,
                            gxmetal_registers + GXMETAL_PRODUCER_REGISTER,
                            len(create) + len(destroy))
            qtest_writel_le(qtest,
                            gxmetal_registers + GXMETAL_DOORBELL_REGISTER, 1)
            assert receive_vnc_pointer_mode(vnc) is True
            assert current_mouse(qmp_stream)["absolute"] is True

            # Keep the explicit InputSprocket bridge request as an override
            # for games that activate the GXMetal mouse device.
            relative_register = (register_address +
                                 GXMETAL_BAR2_REGISTER_OFFSET +
                                 GXMETAL_RELATIVE_INPUT_REGISTER)
            qtest_writel_le(qtest, relative_register, 1)
            assert receive_vnc_pointer_mode(vnc) is False
            assert current_mouse(qmp_stream)["absolute"] is True
            qtest_writel_le(qtest, relative_register, 0)
            assert receive_vnc_pointer_mode(vnc) is True
            assert current_mouse(qmp_stream)["absolute"] is True
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
