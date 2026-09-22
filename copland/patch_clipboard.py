"""Exact D11E4 clipboard compatibility patch; contains no Apple binary assets.

Two zero-byte operations become successful no-ops. Positive lengths keep the
original implementation; negative PutClassicScrap sizes retain the original
error path. No general assertion/debugger suppression is performed.
"""
import hashlib
import struct

ORIGINAL_SHA256 = 'bb2038d07df5e837be5f074f8216fc70836602eec0f5989215c1e7c6031bd964'
PATCHED_SHA256 = '6dfbe6f6e35efc2c329ac04d635e34cfa6ea0abb3b1f89e3ee16c3a90b531ad4'


def patch(data):
    if hashlib.sha256(data).hexdigest() != ORIGINAL_SHA256:
        raise ValueError('Clipboard patch requires the exact unmodified D11E4 library')
    b = bytearray(data)
    def word(at, value):
        struct.pack_into('>I', b, at, value)
    def branch(at, to, base=0x48000000):
        mask = 0x03fffffc if base == 0x48000000 else 0xfffc
        return base | ((to-at) & mask)
    # Code occupies [0x870,0x3d80). Append aligned trampolines so every existing
    # instruction address, export, TOC entry and code relocation stays valid.
    # PutClassicScrap's existing `ble` routes zero/negative sizes here. For a
    # negative size, branch back to its original assertion/paramErr path.
    words = [branch(0x3d80, 0x18a0, 0x40820000), 0x38600000, 0x90610038,
             branch(0x3d8c, 0x1afc),
             # UnloadClassicScrap: FSWrite(count=0) needs no buffer or I/O.
             # Tail-call the original FSWrite thunk for every nonzero count;
             # the caller's LR and its normal return/error handling survive.
             0x80040000, 0x2c000000, branch(0x3d98, 0x3bb8, 0x40820000),
             0x38600000, 0x4e800020, 0x60000000, 0x60000000, 0x60000000]
    word(0x1898, branch(0x1898, 0x3d80, 0x40810000))
    word(0x11f4, branch(0x11f4, 0x3d90) | 1)
    for offset in (48, 52, 56):  # PEF code total/unpacked/packed lengths
        word(offset, struct.unpack_from('>I', b, offset)[0] + 48)
    word(88, struct.unpack_from('>I', b, 88)[0] + 48)  # Data container offset
    b[0x3d80:0x3d80] = struct.pack('>12I', *words)
    if hashlib.sha256(b).hexdigest() != PATCHED_SHA256:
        raise ValueError('Clipboard patch result did not match its qualified hash')
    return bytes(b)
