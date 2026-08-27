#!/usr/bin/env python3
"""Read-only live probe for Unreal Tournament's CheckButtonKeys state.

The probe attaches QEMU's GDB stub to an already-running audio-disabled sweep.
It finds the loaded UT PEF code fragment by matching sampled live PC/LR bytes
against the immutable application data fork, then observes the Control-specific
transition call. If no transition is emitted shortly after the sweep records a
Control key-down action, it breaks after UT masks GetKeys byte 7 and captures
the current modifier, prior modifier, and low-memory KeyMap bytes.
"""

import argparse
import hashlib
import json
from pathlib import Path
import socket
import struct
import time
from typing import Optional


PEF_HEADER_SIZE = 40
PEF_SECTION_HEADER_SIZE = 28
CHECK_BUTTON_KEYS_OFFSET = 0x14FB4C
CONTROL_MASKED_OFFSET = 0x14FB80
CONTROL_EMIT_OFFSET = 0x14FBF0
PRIOR_MODIFIERS_TOC_OFFSET = -11988
CONTROL_MODIFIER_INDEX = 1
LM_KEY_MAP_ADDRESS = 0x0174
LM_KEY_MAP_BYTES = 16


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def pef_code(path: Path) -> tuple[bytes, int]:
    data = path.read_bytes()
    if len(data) < PEF_HEADER_SIZE or data[:12] != b"Joy!peffpwpc":
        raise ValueError("UT data fork is not a PowerPC PEF container")
    section_count = struct.unpack_from(">H", data, 32)[0]
    for index in range(section_count):
        offset = PEF_HEADER_SIZE + index * PEF_SECTION_HEADER_SIZE
        if offset + PEF_SECTION_HEADER_SIZE > len(data):
            raise ValueError("truncated PEF section table")
        fields = struct.unpack_from(">iIIIII4B", data, offset)
        container_length = fields[4]
        container_offset = fields[5]
        section_kind = fields[6]
        if section_kind != 0:
            continue
        end = container_offset + container_length
        if end > len(data):
            raise ValueError("PEF code section exceeds the data fork")
        return data[container_offset:end], container_offset
    raise ValueError("PowerPC PEF has no executable code section")


def receive_hmp_prompt(connection: socket.socket) -> bytes:
    response = bytearray()
    while not response.endswith(b"(qemu) "):
        chunk = connection.recv(4096)
        if not chunk:
            raise RuntimeError("QEMU monitor closed unexpectedly")
        response.extend(chunk)
    return bytes(response)


def hmp_command(path: Path, command: str) -> bytes:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as monitor:
        monitor.settimeout(5)
        monitor.connect(str(path))
        receive_hmp_prompt(monitor)
        monitor.sendall(command.encode("ascii") + b"\n")
        return receive_hmp_prompt(monitor)


class RSPClient:
    """Minimal acknowledged-mode GDB Remote Serial Protocol client."""

    def __init__(self, connection: socket.socket):
        self.connection = connection
        self.buffer = bytearray()
        self.running = False

    @staticmethod
    def framed(payload: str) -> bytes:
        encoded = payload.encode("ascii")
        checksum = sum(encoded) & 0xFF
        return b"$" + encoded + (b"#%02x" % checksum)

    def _receive_more(self, timeout: float) -> None:
        self.connection.settimeout(timeout)
        chunk = self.connection.recv(4096)
        if not chunk:
            raise RuntimeError("QEMU GDB stub closed unexpectedly")
        self.buffer.extend(chunk)

    def _wait_for_ack(self, timeout: float = 5) -> None:
        deadline = time.monotonic() + timeout
        while True:
            if self.buffer:
                marker = self.buffer.pop(0)
                if marker == ord("+"):
                    return
                if marker == ord("-"):
                    raise RuntimeError("QEMU GDB stub rejected packet checksum")
                raise RuntimeError(
                    f"unexpected byte before GDB acknowledgement: {marker:#x}")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("timed out waiting for GDB acknowledgement")
            self._receive_more(remaining)

    def send(self, payload: str) -> None:
        self.connection.sendall(self.framed(payload))
        self._wait_for_ack()

    def receive(self, timeout: float = 5) -> str:
        deadline = time.monotonic() + timeout
        while True:
            while self.buffer and self.buffer[0] in (ord("+"), ord("-")):
                marker = self.buffer.pop(0)
                if marker == ord("-"):
                    raise RuntimeError("QEMU requested an unsupported resend")
            if self.buffer and self.buffer[0] != ord("$"):
                raise RuntimeError(
                    f"unexpected GDB response byte: {self.buffer[0]:#x}")
            if self.buffer:
                delimiter = self.buffer.find(b"#", 1)
                if delimiter >= 0 and len(self.buffer) >= delimiter + 3:
                    payload = bytes(self.buffer[1:delimiter])
                    checksum = bytes(
                        self.buffer[delimiter + 1:delimiter + 3])
                    del self.buffer[:delimiter + 3]
                    if int(checksum, 16) != (sum(payload) & 0xFF):
                        self.connection.sendall(b"-")
                        raise RuntimeError("invalid GDB response checksum")
                    self.connection.sendall(b"+")
                    return payload.decode("ascii")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("timed out waiting for GDB response")
            self._receive_more(remaining)

    def command(self, payload: str, timeout: float = 5) -> str:
        if self.running:
            raise RuntimeError("cannot send a GDB command while guest runs")
        self.send(payload)
        return self.receive(timeout)

    def interrupt(self, timeout: float = 5) -> str:
        if not self.running:
            return "already-stopped"
        self.connection.sendall(b"\x03")
        response = self.receive(timeout)
        self.running = False
        return response

    def continue_guest(self) -> None:
        if self.running:
            raise RuntimeError("guest is already running")
        self.send("c")
        self.running = True

    def wait_for_stop(self, timeout: float) -> str:
        if not self.running:
            raise RuntimeError("guest is not running")
        response = self.receive(timeout)
        self.running = False
        return response

    def register(self, number: int) -> int:
        response = self.command(f"p{number:x}")
        if response.startswith("E"):
            raise RuntimeError(
                f"failed to read GDB register {number}: {response}")
        return int(response, 16)

    def memory(self, address: int, length: int) -> bytes:
        response = self.command(f"m{address:x},{length:x}")
        if response.startswith("E"):
            raise RuntimeError(
                f"failed to read guest memory {address:#x}: {response}")
        data = bytes.fromhex(response)
        if len(data) != length:
            raise RuntimeError(
                f"short GDB memory read at {address:#x}: "
                f"expected {length}, received {len(data)}")
        return data

    def breakpoint(self, address: int, insert: bool) -> None:
        command = "Z" if insert else "z"
        response = self.command(f"{command}0,{address:x},4")
        if response != "OK":
            raise RuntimeError(
                f"failed to {'insert' if insert else 'remove'} breakpoint "
                f"at {address:#x}: {response!r}")


def connect_rsp(host: str, port: int, timeout: float) -> RSPClient:
    deadline = time.monotonic() + timeout
    while True:
        try:
            connection = socket.create_connection((host, port), timeout=1)
            client = RSPClient(connection)
            supported = client.command("qSupported:multiprocess+;swbreak+")
            if supported.startswith("E"):
                raise RuntimeError(f"GDB feature negotiation failed: {supported}")
            return client
        except (ConnectionRefusedError, FileNotFoundError, socket.timeout):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError("timed out connecting to QEMU GDB stub")
            time.sleep(min(0.1, remaining))


def locate_code_fragment(
    client: RSPClient, code: bytes, samples: int = 80,
) -> tuple[int, list[dict[str, object]]]:
    observations = []
    if client.running:
        client.interrupt()
    for index in range(samples):
        candidates = {
            "pc": client.register(64),
            "lr": client.register(67),
        }
        sample = {"index": index, **{
            name: f"0x{address:x}" for name, address in candidates.items()
        }}
        observations.append(sample)
        for name, address in candidates.items():
            if address < 8 or address > 0xFFFFFFE0:
                continue
            try:
                live = client.memory(address, 24)
            except RuntimeError:
                continue
            positions = []
            start = 0
            while True:
                position = code.find(live, start)
                if position < 0:
                    break
                positions.append(position)
                start = position + 1
            if len(positions) == 1:
                base = address - positions[0]
                sample["matched_register"] = name
                sample["code_offset"] = f"0x{positions[0]:x}"
                sample["runtime_base"] = f"0x{base:x}"
                return base, observations
        client.continue_guest()
        time.sleep(0.02)
        client.interrupt()
    raise RuntimeError(
        f"could not identify UT code fragment in {samples} PC/LR samples")


def step_seen(
    path: Path, step_index: int, action: Optional[str] = None,
    value: object = None,
) -> bool:
    if not path.exists():
        return False
    for line in path.read_text(encoding="utf-8").splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if (event.get("event") == "step" and
                event.get("index") == step_index and
                (action is None or event.get("action") == action) and
                (value is None or event.get("value") == value)):
            return True
    return False


def key_down_seen(path: Path, step_index: int) -> bool:
    return step_seen(path, step_index, "key_down", "Control_L")


def snapshot_state(client: RSPClient) -> dict[str, object]:
    registers = {
        name: client.register(number)
        for name, number in (("r1", 1), ("r2", 2), ("r3", 3),
                             ("r4", 4), ("pc", 64), ("lr", 67))
    }
    stack_modifiers = client.memory(registers["r1"] + 56, 4)
    prior_address = registers["r2"] + PRIOR_MODIFIERS_TOC_OFFSET
    prior_modifiers = client.memory(prior_address, 4)
    key_map = client.memory(LM_KEY_MAP_ADDRESS, LM_KEY_MAP_BYTES)
    return {
        "registers": {
            name: f"0x{value:x}" for name, value in registers.items()
        },
        "stack_modifiers_hex": stack_modifiers.hex(),
        "stack_control": stack_modifiers[CONTROL_MODIFIER_INDEX],
        "prior_modifiers_address": f"0x{prior_address:x}",
        "prior_modifiers_hex": prior_modifiers.hex(),
        "prior_control": prior_modifiers[CONTROL_MODIFIER_INDEX],
        "lm_key_map_hex": key_map.hex(),
        "lm_control": key_map[7] & 0x08,
    }


def choose_local_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def run(args: argparse.Namespace) -> dict[str, object]:
    code, code_container_offset = pef_code(args.ut_pef)
    for offset in (CHECK_BUTTON_KEYS_OFFSET, CONTROL_MASKED_OFFSET,
                   CONTROL_EMIT_OFFSET):
        if offset + 4 > len(code):
            raise ValueError(f"UT PEF is too short for code offset {offset:#x}")

    if args.attach_step_index is not None:
        deadline = time.monotonic() + args.attach_timeout
        while not step_seen(args.events, args.attach_step_index):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError(
                    "timed out waiting for sweep debugger-attach step "
                    f"{args.attach_step_index}")
            time.sleep(min(0.1, remaining))

    port = choose_local_port()
    hmp_response = hmp_command(
        args.monitor_socket, f"gdbserver tcp:127.0.0.1:{port}")
    if b"Waiting for gdb connection" not in hmp_response:
        raise RuntimeError(
            "QEMU did not start the requested GDB server: " +
            hmp_response.decode("utf-8", "replace"))

    result: dict[str, object] = {
        "schema": 1,
        "ut_pef": str(args.ut_pef.resolve()),
        "ut_pef_sha256": sha256(args.ut_pef),
        "pef_code_container_offset": f"0x{code_container_offset:x}",
        "pef_code_bytes": len(code),
        "check_button_keys_offset": f"0x{CHECK_BUTTON_KEYS_OFFSET:x}",
        "control_masked_offset": f"0x{CONTROL_MASKED_OFFSET:x}",
        "control_emit_offset": f"0x{CONTROL_EMIT_OFFSET:x}",
        "events": str(args.events.resolve()),
        "key_step_index": args.key_step_index,
        "attach_step_index": args.attach_step_index,
    }
    client: Optional[RSPClient] = None
    inserted = set()
    try:
        client = connect_rsp("127.0.0.1", port, 5)
        # Attaching QEMU's system-mode GDB stub stops the guest before command
        # processing. Query that initial stop instead of sending Ctrl-C; QEMU
        # does not send a second stop reply for an already-stopped CPU.
        result["initial_stop_reply"] = client.command("?")
        runtime_base, samples = locate_code_fragment(client, code)
        result["runtime_base"] = f"0x{runtime_base:x}"
        result["locator_samples"] = samples

        for offset in (CHECK_BUTTON_KEYS_OFFSET, CONTROL_MASKED_OFFSET,
                       CONTROL_EMIT_OFFSET):
            live = client.memory(runtime_base + offset, 4)
            expected = code[offset:offset + 4]
            if live != expected:
                raise RuntimeError(
                    f"live instruction mismatch at UT offset {offset:#x}: "
                    f"{live.hex()} != {expected.hex()}")

        emit_address = runtime_base + CONTROL_EMIT_OFFSET
        client.breakpoint(emit_address, True)
        inserted.add(emit_address)
        client.continue_guest()

        marker_time = None
        overall_deadline = time.monotonic() + args.marker_timeout
        stop_reply = None
        while time.monotonic() < overall_deadline:
            if marker_time is None and key_down_seen(
                    args.events, args.key_step_index):
                marker_time = time.monotonic()
            try:
                stop_reply = client.wait_for_stop(0.1)
                break
            except TimeoutError:
                pass
            if (marker_time is not None and
                    time.monotonic() - marker_time >=
                    args.transition_timeout):
                break

        if stop_reply is not None:
            state = snapshot_state(client)
            result.update({
                "outcome": "control-transition-emitted",
                "stop_reply": stop_reply,
                "key_marker_seen_before_stop": marker_time is not None,
                "state": state,
            })
            client.breakpoint(emit_address, False)
            inserted.remove(emit_address)
        else:
            if marker_time is None:
                raise RuntimeError("timed out before sweep Control key-down")
            client.interrupt()
            client.breakpoint(emit_address, False)
            inserted.remove(emit_address)
            masked_address = runtime_base + CONTROL_MASKED_OFFSET
            client.breakpoint(masked_address, True)
            inserted.add(masked_address)
            client.continue_guest()
            stop_reply = client.wait_for_stop(3)
            state = snapshot_state(client)
            if state["lm_control"] != 0 and state["stack_control"] == 0:
                diagnosis = "stale-getkeys-return"
            elif (state["stack_control"] != 0 and
                  state["prior_control"] != 0):
                diagnosis = "stuck-prior-modifier-state"
            elif (state["stack_control"] != 0 and
                  state["prior_control"] == 0):
                diagnosis = "transition-branch-or-breakpoint-mismatch"
            else:
                diagnosis = "control-not-held-at-snapshot"
            result.update({
                "outcome": "control-transition-not-emitted",
                "diagnosis": diagnosis,
                "stop_reply": stop_reply,
                "key_marker_seen_before_stop": True,
                "state": state,
            })
            client.breakpoint(masked_address, False)
            inserted.remove(masked_address)
        client.continue_guest()
        return result
    finally:
        if client is not None:
            try:
                if client.running:
                    client.interrupt()
                for address in tuple(inserted):
                    try:
                        client.breakpoint(address, False)
                    except Exception:
                        pass
                client.continue_guest()
            except Exception:
                pass
            client.connection.close()
        try:
            hmp_command(args.monitor_socket, "gdbserver none")
            hmp_command(args.monitor_socket, "cont")
        except Exception:
            pass


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("monitor_socket", type=Path)
    parser.add_argument("events", type=Path)
    parser.add_argument("ut_pef", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--key-step-index", type=int, required=True)
    parser.add_argument("--attach-step-index", type=int)
    parser.add_argument("--attach-timeout", type=float, default=180)
    parser.add_argument("--marker-timeout", type=float, default=30)
    parser.add_argument("--transition-timeout", type=float, default=2)
    args = parser.parse_args()
    if args.key_step_index < 0:
        parser.error("--key-step-index must be nonnegative")
    if args.attach_step_index is not None and args.attach_step_index < 0:
        parser.error("--attach-step-index must be nonnegative")
    if (args.attach_timeout <= 0 or args.marker_timeout <= 0 or
            args.transition_timeout <= 0):
        parser.error("timeouts must be positive")

    result = run(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
