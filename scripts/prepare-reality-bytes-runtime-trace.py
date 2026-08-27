#!/usr/bin/env python3
"""Prepare a disposable Reality Bytes runtime discriminator disk.

The original application and source disk are never changed.  The script makes
an APFS clone (or a full copy when cloning is unavailable), mounts only that
copy, validates the exact PEF data fork, and applies one fail-closed diagnostic
patch set for an allocation, call-path, or callback-site trace.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


GAMES = {
    "dark-vengeance": {
        "application": "Dark Vengeance Demo/Dark Vengeance\N{TRADE MARK SIGN} DEMO",
        "application_sha256":
            "91e703911071247e590fb00f9cccc1d5c7b2ff87bd0bf41e4586f9712cb6e2c9",
        "patches": (
            # Each request remains negative when the observed cached size is
            # -50, so the diagnostic cannot proceed with an undersized buffer.
            (0x9FA4,  "38630001", "38630001", "DARKVENG.INI", 1),
            (0xA094,  "38630001", "38630011", "CUSTOM.INI", 17),
            (0xA184,  "38630001", "38630021", "NETLAUNCH.INI", 33),
        ),
        "interpretation": (
            "The displayed allocation value minus the selected addend is the "
            "live cached GDataFile size. Values -49, -33, and -17 identify "
            "DARKVENG.INI, CUSTOM.INI, and NETLAUNCH.INI respectively when "
            "the cached size is -50."
        ),
    },
    "dark-getfpos-return": {
        "application": "Dark Vengeance Demo/Dark Vengeance\N{TRADE MARK SIGN} DEMO",
        "application_sha256":
            "91e703911071247e590fb00f9cccc1d5c7b2ff87bd0bf41e4586f9712cb6e2c9",
        "patches": (
            # Stop immediately after GDataFile::GetPos has copied the
            # InterfaceLib GetFPos result into r30. At this point r27 is the
            # GDataFile object and r28 is the caller's output pointer.
            (0x1506B4, "7fc00734", "48000000", "post-GetFPos hold", "b ."),
        ),
        "interpretation": (
            "At the hold PC, r30 is the InterfaceLib GetFPos OSErr, r27 is "
            "the GDataFile object, and r28 is the output pointer. The object "
            "dump exposes cached size +296, current position +300, and the "
            "file refnum +278 without changing any File Manager inputs."
        ),
    },
    "dark-darkveng-open-return": {
        "application": "Dark Vengeance Demo/Dark Vengeance\N{TRADE MARK SIGN} DEMO",
        "application_sha256":
            "91e703911071247e590fb00f9cccc1d5c7b2ff87bd0bf41e4586f9712cb6e2c9",
        "patches": (
            # ReadPrefs has just returned from DARKVENG.INI Open. Preserve the
            # returned GError pointer in r31, load the exact GDataFile pointer
            # from its local, then hold before the success/error branch.
            (0x9EF8, "281f0000", "83c101b0", "load DARKVENG.INI object", "r30"),
            (0x9EFC, "41820090", "48000000", "post-Open hold", "b ."),
        ),
        "interpretation": (
            "At the hold PC, r31 is the DARKVENG.INI Open/CacheFileSize "
            "GError pointer and r30 is that exact GDataFile object. Offsets "
            "+296 and +300 are the cached size and last position; +278 is "
            "the live File Manager refnum."
        ),
    },
    "dark-rbnewptr-failure-return": {
        "application": "Dark Vengeance Demo/Dark Vengeance\N{TRADE MARK SIGN} DEMO",
        "application_sha256":
            "91e703911071247e590fb00f9cccc1d5c7b2ff87bd0bf41e4586f9712cb6e2c9",
        "patches": (
            # This path runs only after NewPtr returned NULL. Recover the
            # caller's saved LR from the wrapper frame into r29 and hold before
            # formatting the fatal dialog. r30 remains the exact request.
            (0x51D60, "806284d8", "83a10048", "load failed caller LR", "r29"),
            (0x51D64, "7fc5f378", "48000000", "failed RBNewPtr hold", "b ."),
        ),
        "interpretation": (
            "At the hold PC, r29 is the relocated caller return PC, r30 is "
            "the exact failed NewPtr request, and r31 is the returned pointer. "
            "Subtract NIP-0x51d64 from r29 to recover the static PEF address."
        ),
    },
    "dark-rbnewptrclear-failure-lr": {
        "application": "Dark Vengeance Demo/Dark Vengeance\N{TRADE MARK SIGN} DEMO",
        "application_sha256":
            "91e703911071247e590fb00f9cccc1d5c7b2ff87bd0bf41e4586f9712cb6e2c9",
        "patches": (
            # This instruction is reached only when RBNewPtrClear's NewPtrClear
            # call returned NULL. Reuse the existing fatal dialog's signed
            # decimal value field to expose the wrapper's saved caller LR.
            (0x51DF4, "7fc5f378", "80a10048", "display failed caller LR", "LR"),
        ),
        "interpretation": (
            "The fatal dialog's displayed value is the exact relocated caller "
            "return PC loaded from RBNewPtr's saved LR. The unmodified request "
            "for this same path is retained by the control run and can be "
            "recovered independently without permitting an unsafe allocation."
        ),
    },
    "dark-rbnewhandle-failure-lr": {
        "application": "Dark Vengeance Demo/Dark Vengeance\N{TRADE MARK SIGN} DEMO",
        "application_sha256":
            "91e703911071247e590fb00f9cccc1d5c7b2ff87bd0bf41e4586f9712cb6e2c9",
        "patches": (
            # This instruction is reached only when RBNewHandle's NewHandle
            # call returned NULL. The saved caller LR is at old-SP+8, or
            # current-SP+72 after this wrapper's 64-byte frame allocation.
            (0x51E8C, "7fc5f378", "80a10048", "display failed caller LR", "LR"),
        ),
        "interpretation": (
            "The fatal dialog's displayed value is the exact relocated caller "
            "return PC loaded from RBNewHandle's saved LR. The unmodified "
            "request is retained by the control run and can be recovered "
            "independently without permitting an unsafe allocation."
        ),
    },
    "dark-compositing-caller-lr": {
        "application": "Dark Vengeance Demo/Dark Vengeance\N{TRADE MARK SIGN} DEMO",
        "application_sha256":
            "91e703911071247e590fb00f9cccc1d5c7b2ff87bd0bf41e4586f9712cb6e2c9",
        "patches": (
            # Preserve the generic formatter's freshly captured LR in its
            # first vararg slot, then add one %d field only to Dark's exact
            # compositing fatal string. No success path or RAVE input changes.
            (0x157230, "90a10280", "90010280", "format caller LR", "LR"),
            (0x20F955,
             "726571756972656420436f6d706f736974696e672066756e6374696f6e616c697479",
             "63616c6c657220256420202020202020202020202020202020202020202020202020",
             "display compositing caller LR", "%d"),
        ),
        "interpretation": (
            "At the unchanged compositing-fatal path, the inserted decimal "
            "value is the generic formatter caller's exact relocated return "
            "PC. The application still follows its original fatal branch."
        ),
    },
    "dark-buffer-notice-callback-hold": {
        "application": "Dark Vengeance Demo/Dark Vengeance\N{TRADE MARK SIGN} DEMO",
        "application_sha256":
            "91e703911071247e590fb00f9cccc1d5c7b2ff87bd0bf41e4586f9712cb6e2c9",
        "patches": (
            # Hold at the exact selector-4 callback entry. Reaching this PC
            # proves the RAVE engine invoked the registered buffer notice;
            # r3-r6 retain drawContext, TQADevice, dirtyRect, and refCon.
            (0x129990, "7c0802a6", "48000000",
             "RenderCompositeCallback entry hold", "b ."),
        ),
        "interpretation": (
            "At the hold PC, r3 is the TQADrawContext, r4 is the supplied "
            "TQADevice, r5 is the dirty rectangle, and r6 is Dark's refCon. "
            "The independently established relocation maps static 0x129990 "
            "to runtime 0x3e96a990."
        ),
    },
    "havoc": {
        "application": "HAVOC/HAVOC\N{TRADE MARK SIGN}",
        "application_sha256":
            "b31256a407aae0dce111ebd0ef522d05fb72854ea7ef1e6d40213b3506bb4207",
        "patches": (
            (0x39018, "3860000e", "3860000f", "NewHandle dynamic #1", 15),
            (0x39080, "3860000e", "38600010", "NewHandle dynamic #2", 16),
            (0x51B2C, "3860000e", "38600011", "NewHandle 7562", 17),
            (0x72D70, "3860000e", "38600012", "NewHandle 4096", 18),
            (0x72E8C, "3860000e", "38600013", "NewHandle 131072", 19),
        ),
        "interpretation": (
            "The retained alert text now selects one exact NewHandle caller: "
            "graphic=0x39000, key mappings=0x39068, critical offscreen="
            "0x51b14, purgeable picture=0x72d58, or drawing purgeable picture="
            "0x72e74."
        ),
    },
    "havoc-size-low6": {
        "application": "HAVOC/HAVOC\N{TRADE MARK SIGN}",
        "application_sha256":
            "b31256a407aae0dce111ebd0ef522d05fb72854ea7ef1e6d40213b3506bb4207",
        "patches": (
            (0x3900C, "807d000e", "a87d0016", "load saved request", "lha"),
            (0x39010, "28030000", "5463069f", "extract bits 0..5", "low6"),
            (0x39014, "40820024", "60000000", "diagnostic fallthrough", "nop"),
            (0x39018, "3860000e", "38630001", "one-based STR# index", "+1"),
        ),
        "interpretation":
            "Displayed STR# index minus one is request bits 0 through 5.",
    },
    "havoc-size-mid6": {
        "application": "HAVOC/HAVOC\N{TRADE MARK SIGN}",
        "application_sha256":
            "b31256a407aae0dce111ebd0ef522d05fb72854ea7ef1e6d40213b3506bb4207",
        "patches": (
            (0x3900C, "807d000e", "a87d0016", "load saved request", "lha"),
            (0x39010, "28030000", "5463d69f", "extract bits 6..11", "mid6"),
            (0x39014, "40820024", "60000000", "diagnostic fallthrough", "nop"),
            (0x39018, "3860000e", "38630001", "one-based STR# index", "+1"),
        ),
        "interpretation":
            "Displayed STR# index minus one is request bits 6 through 11.",
    },
    "havoc-size-high4": {
        "application": "HAVOC/HAVOC\N{TRADE MARK SIGN}",
        "application_sha256":
            "b31256a407aae0dce111ebd0ef522d05fb72854ea7ef1e6d40213b3506bb4207",
        "patches": (
            (0x3900C, "807d000e", "a87d0016", "load saved request", "lha"),
            (0x39010, "28030000", "5463a71f", "extract bits 12..15", "high4"),
            (0x39014, "40820024", "60000000", "diagnostic fallthrough", "nop"),
            (0x39018, "3860000e", "38630001", "one-based STR# index", "+1"),
        ),
        "interpretation":
            "Displayed STR# index minus one is request bits 12 through 15.",
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def clone_disk(source: Path, destination: Path) -> str:
    result = subprocess.run(
        ["cp", "-c", str(source), str(destination)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if result.returncode == 0:
        return "clonefile"
    destination.unlink(missing_ok=True)
    shutil.copy2(source, destination)
    return "full-copy"


def code_file_offset(data: bytes, vma: int) -> int:
    if data[:8] != b"Joy!peff":
        raise ValueError("application data fork is not a PEF container")
    section_count = struct.unpack_from(">H", data, 32)[0]
    for index in range(section_count):
        header = 40 + index * 28
        _default_address, total_size, unpacked_size, container_size, container_offset = \
            struct.unpack_from(">IIIII", data, header + 4)
        section_kind = data[header + 24]
        if section_kind != 0:
            continue
        # Retro68's PEF BFD backend reports the code-section container offset
        # as its disassembly VMA (the first Dark Vengeance instruction is at
        # both file offset and objdump address 0x3270; HAVOC starts at 0x10c0).
        # Accept those recorded addresses directly while still bounding them
        # to the validated executable section.
        relative = vma - container_offset
        if relative < 0 or relative + 4 > total_size:
            continue
        if unpacked_size != container_size:
            raise ValueError("packed executable code sections are unsupported")
        return container_offset + relative
    raise ValueError(f"no executable PEF section contains VMA {vma:#x}")


def patch_application(path: Path, patches: tuple[tuple, ...]) -> list[dict]:
    data = bytearray(path.read_bytes())
    records = []
    for vma, expected_hex, replacement_hex, label, marker in patches:
        offset = code_file_offset(data, vma)
        expected = bytes.fromhex(expected_hex)
        replacement = bytes.fromhex(replacement_hex)
        actual = bytes(data[offset:offset + len(expected)])
        if actual != expected:
            raise ValueError(
                f"{label} at VMA {vma:#x}: expected {expected.hex()}, "
                f"found {actual.hex()}")
        data[offset:offset + len(expected)] = replacement
        records.append({
            "label": label,
            "vma": f"0x{vma:x}",
            "file_offset": f"0x{offset:x}",
            "before": expected.hex(),
            "after": replacement.hex(),
            "marker": marker,
        })
    path.write_bytes(data)
    return records


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", choices=sorted(GAMES))
    parser.add_argument("source_disk", type=Path)
    parser.add_argument("output_disk", type=Path)
    args = parser.parse_args()

    source = args.source_disk.resolve()
    output = args.output_disk.resolve()
    if not source.is_file():
        parser.error(f"source disk does not exist: {source}")
    if source.stat().st_mode & 0o222:
        parser.error(
            "source disk must be host-read-only (chmod a-w) before "
            f"preparation: {source}")
    if output.exists():
        parser.error(f"refusing to overwrite output: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)

    source_hash = sha256(source)
    config = GAMES[args.game]
    clone_method = None
    records = []
    app_hash_before = None
    app_hash_after = None

    source_hash_after = None
    attached = False
    temporary = None
    mountpoint = None
    try:
        clone_method = clone_disk(source, output)
        output.chmod(output.stat().st_mode | 0o200)
        temporary = Path(tempfile.mkdtemp(prefix="reality-bytes-trace-"))
        mountpoint = temporary / "volume"
        mountpoint.mkdir()
        subprocess.run([
            "hdiutil", "attach", "-nobrowse", "-noverify",
            "-mountpoint", str(mountpoint), str(output),
        ], check=True, stdout=subprocess.PIPE,
           stderr=subprocess.STDOUT, text=True)
        attached = True
        application = mountpoint / config["application"]
        if not application.is_file():
            raise FileNotFoundError(
                "expected application data fork is absent: "
                f"{application}")
        app_hash_before = sha256(application)
        if app_hash_before != config["application_sha256"]:
            raise ValueError(
                "application hash mismatch: expected "
                f"{config['application_sha256']}, "
                f"found {app_hash_before}")
        records = patch_application(application, config["patches"])
        app_hash_after = sha256(application)
    finally:
        active_error = sys.exc_info()[1]
        cleanup_errors = []
        if attached and mountpoint is not None:
            detach_failures = []
            for force in (False, True):
                command = ["hdiutil", "detach"]
                if force:
                    command.append("-force")
                command.append(str(mountpoint))
                try:
                    result = subprocess.run(
                        command, check=False, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True)
                    if result.returncode == 0:
                        attached = False
                        break
                    detach_failures.append(
                        f"{'force ' if force else ''}detach exited "
                        f"{result.returncode}: {result.stdout.strip()}")
                except BaseException as error:
                    detach_failures.append(
                        f"{'force ' if force else ''}detach raised {error}")
            if attached:
                cleanup_errors.append(RuntimeError(
                    "could not detach disposable trace output at "
                    f"{mountpoint}: {'; '.join(detach_failures)}"))
        if temporary is not None and not attached:
            try:
                shutil.rmtree(temporary)
            except BaseException as error:
                cleanup_errors.append(error)
        try:
            source_hash_after = sha256(source)
            if source_hash_after != source_hash:
                cleanup_errors.append(RuntimeError(
                    "source disk changed while preparing the disposable "
                    f"trace: {source_hash} -> {source_hash_after}"))
        except BaseException as error:
            cleanup_errors.append(error)
        if output.exists():
            try:
                output.chmod(output.stat().st_mode & ~0o222)
            except BaseException as error:
                cleanup_errors.append(error)
        if active_error is not None or cleanup_errors:
            if output.exists() and not attached:
                try:
                    output.unlink()
                except BaseException as error:
                    cleanup_errors.append(error)
            if active_error is not None:
                for cleanup_error in cleanup_errors:
                    if hasattr(active_error, "add_note"):
                        active_error.add_note(
                            "trace cleanup/integrity failure: "
                            f"{cleanup_error}")
            elif cleanup_errors:
                raise cleanup_errors[0]

    assert source_hash_after is not None
    assert clone_method is not None
    output_hash = sha256(output)
    sidecar = output.with_suffix(output.suffix + ".trace.json")
    sidecar.write_text(json.dumps({
        "schema": 1,
        "game": args.game,
        "source_disk": str(source),
        "source_disk_sha256": source_hash,
        "source_disk_sha256_after": source_hash_after,
        "output_disk": str(output),
        "output_disk_sha256": output_hash,
        "clone_method": clone_method,
        "application": config["application"],
        "application_sha256_before": app_hash_before,
        "application_sha256_after": app_hash_after,
        "patches": records,
        "interpretation": config["interpretation"],
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"prepared {output}")
    print(f"source sha256 {source_hash}")
    print(f"output sha256 {output_hash}")
    print(f"application sha256 {app_hash_before} -> {app_hash_after}")
    print(f"trace manifest {sidecar}")


if __name__ == "__main__":
    main()
