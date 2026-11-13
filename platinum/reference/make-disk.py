#!/usr/bin/env python3
"""Wrap Retro68's MacBinary application in an HFS reference disk.

Requires machfs. This avoids ConvertDiskImage/hfsutils mount state and is
useful when a Retro68 build emits a blank .dsk. Only the named output is written.
"""
import argparse
from pathlib import Path
import machfs

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('application', type=Path, help='PlatinumReference.bin')
parser.add_argument('output', type=Path, help='new HFS .dsk')
args = parser.parse_args()
raw = args.application.read_bytes()
if len(raw) < 128 or raw[0] != 0 or not 1 <= raw[1] <= 63:
    raise ValueError('Expected a MacBinary application')
data_size = int.from_bytes(raw[83:87], 'big')
resource_size = int.from_bytes(raw[87:91], 'big')
resource_start = 128 + (data_size + 127) // 128 * 128
if not resource_size or len(raw) < resource_start + resource_size:
    raise ValueError('Missing or truncated application resource fork')
application = machfs.File()
application.type, application.creator = raw[65:69], raw[69:73]
application.data = raw[128:128 + data_size]
application.rsrc = raw[resource_start:resource_start + resource_size]
volume = machfs.Volume()
volume.name = 'Platinum Reference'
volume['PlatinumReference'] = application
with args.output.open('xb') as output:
    output.write(volume.write())
