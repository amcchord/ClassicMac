#!/usr/bin/env python3
"""Prepare a disposable Tiger test disk using the Internet Archive fixture.

Never add the disk, archive or guest screenshots to a release artifact.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--qemu-img", type=Path, required=True)
    parser.add_argument("--seven-zip", default="7z")
    args = parser.parse_args()
    folder = args.folder.resolve()
    folder.mkdir(parents=True, exist_ok=False)
    archive = folder / "tiger.7z"
    subprocess.run([sys.executable, str(Path(__file__).with_name("fetch_ranges.py")), str(archive)], check=True)
    print("Checking the Tiger fixture archive.", flush=True)
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != "cb37f45254a12542314593267e8215416a74b6d68eb37b642840d9c665e98bf1":
        raise SystemExit("Tiger test archive differs from the reviewed fixture")
    image = "osx-tiger_10.4.11_installed.qcow2"
    subprocess.run([args.seven_zip, "e", "-y", "-o" + str(folder), str(archive), image],
                   check=True)
    archive.unlink()
    print("Converting the disposable disk.", flush=True)
    subprocess.run([str(args.qemu_img.resolve()), "convert", "-f", "qcow2", "-O", "raw",
                    str(folder / image), str(folder / "disk.img")], check=True)
    (folder / image).unlink()
    config = {"id": str(uuid.uuid4()), "name": "ClassicMac Tiger qualification",
              "machineFamily": "powerMacOSX", "networking": False, "sound": False}
    (folder / "config.json").write_text(json.dumps(config) + "\n")


if __name__ == "__main__":
    main()
