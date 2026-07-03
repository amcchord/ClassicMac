#!/usr/bin/env python3
"""Copy a file or directory tree onto the mounted hfsutils volume, keeping
resource forks and Finder type/creator/flags intact.

Usage: hfs-copy.py <host-path> <mac-dest-path>

<mac-dest-path> is an hfsutils path like ":USB Overdrive 1.4". The volume
must already be hmount-ed (hfsutils keeps the current volume in ~/.hcwd,
which the hcopy/hmkdir child processes inherit).

macOS preserves classic Mac metadata natively: the resource fork is readable
at <file>/..namedfork/rsrc and the type/creator/flags live in the
com.apple.FinderInfo extended attribute (unar fills in both when expanding
StuffIt archives). hfsutils can only write a resource fork through its
MacBinary decoder, so each file is re-encoded as MacBinary II here and
copied with `hcopy -m`.
"""
import ctypes
import ctypes.util
import os
import struct
import subprocess
import sys
import tempfile
import unicodedata

MAC_EPOCH_OFFSET = 2082844800  # 1904-01-01 -> 1970-01-01 in seconds

libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)


def finder_info(path):
    """Return the 32-byte com.apple.FinderInfo xattr, or zeros."""
    buf = ctypes.create_string_buffer(32)
    n = libc.getxattr(path.encode("utf-8"), b"com.apple.FinderInfo",
                      buf, 32, 0, 0)
    if n == 32:
        return buf.raw
    return bytes(32)


def resource_fork(path):
    try:
        with open(path + "/..namedfork/rsrc", "rb") as f:
            return f.read()
    except (FileNotFoundError, OSError):
        return b""


def crc16_xmodem(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def mac_name(name):
    """Host file name -> Mac-Roman HFS name (max 31 bytes)."""
    # macOS stores a Finder-visible "/" as ":" on disk; HFS wants the "/".
    name = unicodedata.normalize("NFC", name).replace(":", "/")
    encoded = name.encode("mac_roman", errors="replace")
    if len(encoded) > 31:
        print("    warning: truncating long name: %s" % name, file=sys.stderr)
        encoded = encoded[:31]
    return encoded


def macbinary(path, name_bytes):
    """Encode the file at path as MacBinary II."""
    with open(path, "rb") as f:
        data = f.read()
    rsrc = resource_fork(path)
    info = finder_info(path)
    ftype, creator, flags = info[0:4], info[4:8], struct.unpack(">H", info[8:10])[0]
    # Clear the Finder "inited" bit so the guest Finder places the icon.
    flags &= ~0x0100

    st = os.stat(path)
    mac_mtime = int(st.st_mtime) + MAC_EPOCH_OFFSET

    header = bytearray(128)
    header[1] = len(name_bytes)
    header[2:2 + len(name_bytes)] = name_bytes
    header[65:69] = ftype
    header[69:73] = creator
    header[73] = (flags >> 8) & 0xFF
    struct.pack_into(">I", header, 83, len(data))
    struct.pack_into(">I", header, 87, len(rsrc))
    struct.pack_into(">I", header, 91, mac_mtime)
    struct.pack_into(">I", header, 95, mac_mtime)
    header[101] = flags & 0xFF
    header[122] = 129  # MacBinary II version
    header[123] = 129  # minimum version to decode
    struct.pack_into(">H", header, 124, crc16_xmodem(bytes(header[0:124])))

    def padded(chunk):
        if len(chunk) % 128 != 0:
            chunk += bytes(128 - len(chunk) % 128)
        return chunk

    return bytes(header) + padded(data) + padded(rsrc)


def run(args):
    result = subprocess.run(args)
    if result.returncode != 0:
        sys.exit("command failed: %r" % (args,))


def copy_node(src, dest_mac):
    """Copy src (file or dir) to the HFS path dest_mac (bytes)."""
    if os.path.isdir(src):
        run([b"hmkdir", dest_mac])
        for entry in sorted(os.listdir(src)):
            if entry.startswith("."):
                continue  # host/archiver metadata (.DS_Store etc.)
            copy_node(os.path.join(src, entry),
                      dest_mac + b":" + mac_name(entry))
    else:
        name = mac_name(dest_mac.split(b":")[-1].decode("mac_roman"))
        encoded = macbinary(src, name)
        with tempfile.NamedTemporaryFile(suffix=".macbin", delete=False) as tmp:
            tmp.write(encoded)
            tmp_path = tmp.name
        try:
            run([b"hcopy", b"-m", tmp_path.encode(), dest_mac])
        finally:
            os.unlink(tmp_path)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: hfs-copy.py <host-path> <mac-dest-path>")
    src = sys.argv[1]
    dest = sys.argv[2].encode("mac_roman", errors="replace")
    copy_node(src, dest)


main()
