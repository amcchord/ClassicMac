#!/usr/bin/env python3
"""Capture fresh ClassicMac 4.10.0 screenshots in an isolated synthetic home.

This script never touches the user's real ~/Documents/ClassicMac, Application
Support library, preferences, or clipboard. All tours run with HOME and
CFFIXED_USER_HOME pointed at /tmp/cm-manual-home.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request

REPO = Path(__file__).resolve().parents[3]
DEFAULT_APP = REPO / "output/manual-shots-work/ClassicMac.app"
FALLBACK_APP = REPO / "dist/ClassicMac.app"
CACHE_DIR = REPO / "output/manual-shots-work/cache"
MANUAL_SHOTS_DIR = REPO / "docs/screenshots/manual"
PLATINUM_SHOTS_DIR = REPO / "docs/screenshots/platinum"
ROOT_SHOTS_DIR = REPO / "docs/screenshots"
DEFAULT_SOURCE_OS9 = Path.home() / "Documents/ClassicMac/Mac OS 9.2.2 Mega Deluxe.classic"

SYNTH_HOME = Path("/tmp/cm-manual-home")

ARCHIVES = {
    "sys755": (
        "https://mcchord.net/classicmac/system-7.5.5-base-v1.tar.gz",
        "system-7.5.5-base-v1.tar.gz",
    ),
    "os81": (
        "https://mcchord.net/classicmac/mac-os-8.1-base-v1.tar.gz",
        "mac-os-8.1-base-v1.tar.gz",
    ),
    "copland": (
        "https://mcchord.net/classicmac/copland-d11e4-activities-v3.tar.gz",
        "copland-d11e4-activities-v3.tar.gz",
    ),
    "tiger": (
        "https://mcchord.net/retroboot-downloads/files/mac-os-x-10-4-11-tiger-period-v1.thin.tar.gz",
        "mac-os-x-10-4-11-tiger-period-v1.thin.tar.gz",
    ),
}


def download_if_needed(url: str, dest: Path) -> Path:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 0:
        print(f"Using cached {dest.name} ({dest.stat().st_size:,} bytes)")
        return dest
    tmp = dest.with_suffix(dest.suffix + ".part")
    print(f"Downloading {url} -> {dest.name}...")
    req = urllib.request.Request(url, headers={"User-Agent": "ClassicMac-Manual-Capturer/4.10.0"})
    with urllib.request.urlopen(req, timeout=120) as resp, tmp.open("wb") as out:
        total = int(resp.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = resp.read(1024 * 1024)
            if not chunk:
                break
            out.write(chunk)
            done += len(chunk)
            if total > 0 and done % (64 * 1024 * 1024) < len(chunk):
                print(f"  {done / 1048576:.1f} / {total / 1048576:.1f} MB")
    tmp.replace(dest)
    print(f"Downloaded {dest.name} ({dest.stat().st_size:,} bytes)")
    return dest


def extract_tar_gz_sparse(archive_path: Path, dest_dir: Path) -> None:
    """Extract a ClassicMac or GNU sparse 1.0 tar.gz into dest_dir using bsdtar."""
    dest_dir.mkdir(parents=True, exist_ok=True)
    # Validate flat regular-file listing first, then extract sparsely with system tar.
    listing = subprocess.run(
        ["tar", "-tzf", str(archive_path)],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    ).stdout.splitlines()
    for entry in listing:
        clean = entry.strip()
        if not clean or "/" in clean or clean.startswith(".") or clean == "..":
            raise ValueError(f"Unsafe archive member: {entry!r}")
    subprocess.run(
        ["tar", "-xzf", str(archive_path), "-C", str(dest_dir)],
        check=True,
    )


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def prepare_synth_home(source_os9: Path, include_tiger: bool = True) -> dict[str, Path]:
    if SYNTH_HOME.exists():
        shutil.rmtree(SYNTH_HOME)
    docs_dir = SYNTH_HOME / "Documents/ClassicMac"
    shared_dir = SYNTH_HOME / "Shared Mac Files"
    support_dir = SYNTH_HOME / "Library/Application Support/ClassicMac"
    prefs_dir = SYNTH_HOME / "Library/Preferences"
    for d in (docs_dir, shared_dir, support_dir, prefs_dir):
        d.mkdir(parents=True, exist_ok=True)

    bundles: list[Path] = []

    # 1. Mac OS 9.2.2 Mega Deluxe (APFS clone)
    os9_dst = docs_dir / "Mac OS 9.2.2 Mega Deluxe.classic"
    if not source_os9.exists():
        raise FileNotFoundError(f"Source OS 9 bundle not found: {source_os9}")
    subprocess.run(["cp", "-c", "-R", str(source_os9), str(os9_dst)], check=True)
    os9_cfg = json.loads((os9_dst / "config.json").read_text(encoding="utf-8"))
    os9_cfg.update(
        {
            "id": "11111111-1111-4111-8111-000000000009",
            "name": "Mac OS 9.2.2 Mega Deluxe",
            "sharedFolderPath": str(shared_dir),
            "printingEnabled": True,
            "printerName": "pdf",
            "graphicsCard": "atiRadeon",
            "width": 1024,
            "height": 768,
            "depth": 24,
            "toolsCDInserted": True,
        }
    )
    write_json(os9_dst / "config.json", os9_cfg)

    # Seed two APFS-cloned Checkpoints so CheckpointsDialog is richly populated
    cp_root = os9_dst / "Checkpoints"
    cp_root.mkdir(exist_ok=True)
    disk_bytes = (os9_dst / "disk.img").stat().st_size
    for cp_id, label, kind, created_at in [
        ("20261124T220000Z", "Fresh install", "fresh-install", "2026-11-24T22:00:00Z"),
        ("20261125T143000Z", "Before Quake III mods", "manual", "2026-11-25T14:30:00Z"),
    ]:
        cp_dir = cp_root / cp_id
        cp_dir.mkdir(exist_ok=True)
        subprocess.run(["cp", "-c", str(os9_dst / "disk.img"), str(cp_dir / "disk.img")], check=True)
        write_json(
            cp_dir / "checkpoint.json",
            {
                "id": cp_id,
                "label": label,
                "kind": kind,
                "createdAt": created_at,
                "diskBytes": disk_bytes,
                "allocatedBytes": 6_530_000_000,
                "method": "clonefile",
            },
        )
    bundles.append(os9_dst)

    # 2. Mac OS 8.1 (Quadra 800)
    os81_tar = download_if_needed(* (ARCHIVES["os81"][0], CACHE_DIR / ARCHIVES["os81"][1]))
    os81_dst = docs_dir / "Mac OS 8.1.classic"
    extract_tar_gz_sparse(os81_tar, os81_dst)
    os81_cfg = json.loads((os81_dst / "config.json").read_text(encoding="utf-8"))
    os81_cfg.update(
        {
            "id": "11111111-1111-4111-8111-000000000008",
            "name": "Mac OS 8.1",
            "machineFamily": "quadra800",
            "useEnhancedFramebuffer": True,
            "width": 1024,
            "height": 768,
            "depth": 16,
            "toolsCDInserted": True,
        }
    )
    write_json(os81_dst / "config.json", os81_cfg)
    write_json(
        os81_dst / "template-info.json",
        {
            "schemaVersion": 1,
            "templateID": "mac-os-8.1-base-v1",
            "osVersion": "Mac OS 8.1",
            "archiveSHA256": "779cb74e50720f263e15d9a79011d1aa1f2a8ef25c83ae55e2c1a4ffadca3d09",
            "importedAt": 817273700.0,
        },
    )
    bundles.append(os81_dst)

    # 3. System 7.5.5 (Quadra 800)
    s755_tar = download_if_needed(* (ARCHIVES["sys755"][0], CACHE_DIR / ARCHIVES["sys755"][1]))
    s755_dst = docs_dir / "System 7.5.5.classic"
    extract_tar_gz_sparse(s755_tar, s755_dst)
    s755_cfg = json.loads((s755_dst / "config.json").read_text(encoding="utf-8"))
    s755_cfg.update(
        {
            "id": "11111111-1111-4111-8111-000000000007",
            "name": "System 7.5.5",
            "machineFamily": "quadra800",
            "useEnhancedFramebuffer": True,
            "width": 800,
            "height": 600,
            "depth": 8,
            "toolsCDInserted": False,
        }
    )
    write_json(s755_dst / "config.json", s755_cfg)
    write_json(
        s755_dst / "template-info.json",
        {
            "schemaVersion": 1,
            "templateID": "system-7.5.5-base-v1",
            "osVersion": "System 7.5.5",
            "archiveSHA256": "68b8ffb1993664644435e7c9bb4f4d58737a7da6c63e6cb794b7c48830c38246",
            "importedAt": 817273600.0,
        },
    )
    bundles.append(s755_dst)

    # 4. Copland D11E4 (Power Mac 7500)
    cop_tar = download_if_needed(* (ARCHIVES["copland"][0], CACHE_DIR / ARCHIVES["copland"][1]))
    cop_dst = docs_dir / "Copland D11E4.classic"
    extract_tar_gz_sparse(cop_tar, cop_dst)
    cop_cfg = json.loads((cop_dst / "config.json").read_text(encoding="utf-8"))
    cop_cfg.update(
        {
            "id": "11111111-1111-4111-8111-000000000075",
            "name": "Copland D11E4",
            "machineFamily": "powerMac7500",
        }
    )
    write_json(cop_dst / "config.json", cop_cfg)
    write_json(
        cop_dst / "template-info.json",
        {
            "schemaVersion": 1,
            "templateID": "copland-d11e4-activities-v3",
            "osVersion": "Copland D11E4",
            "archiveSHA256": "9edff56ec94e3dcd5a0804f327705e3f38ffca73ac701ac5a5ee3d581267af19",
            "importedAt": 817273500.0,
        },
    )
    bundles.append(cop_dst)

    # 5. Mac OS X 10.4.11 Tiger (Power Mac G4 — Mac OS X)
    osx_dst = docs_dir / "Mac OS X Tiger.classic"
    if include_tiger:
        tiger_tar = download_if_needed(* (ARCHIVES["tiger"][0], CACHE_DIR / ARCHIVES["tiger"][1]))
        extract_tar_gz_sparse(tiger_tar, osx_dst)
        osx_cfg = json.loads((osx_dst / "config.json").read_text(encoding="utf-8"))
        osx_cfg.update(
            {
                "id": "11111111-1111-4111-8111-000000000010",
                "name": "Mac OS X Tiger",
                "machineFamily": "powerMacOSX",
                "ramMB": 1024,
                "width": 1024,
                "height": 768,
                "depth": 24,
            }
        )
        write_json(osx_dst / "config.json", osx_cfg)
        write_json(
            osx_dst / "template-info.json",
            {
                "schemaVersion": 1,
                "templateID": "mac-os-x-10-4-11-tiger-period-v1",
                "osVersion": "10.4.11",
                "archiveSHA256": "fe8f6d247f174717a4d5594c052b18d76ef8948dd4e2c15d5cd4961a5b7b6376",
                "importedAt": 817273800.0,
            },
        )
        bundles.append(osx_dst)

    write_json(support_dir / "library.json", [str(b) for b in bundles])
    return {
        "os9": os9_dst,
        "os81": os81_dst,
        "sys755": s755_dst,
        "copland": cop_dst,
        "tiger": osx_dst,
    }


def run_tour(
    app_path: Path,
    tour: str,
    shot_dir: Path,
    extra_env: dict[str, str] | None = None,
    timeout: int = 180,
) -> list[Path]:
    if shot_dir.exists():
        shutil.rmtree(shot_dir)
    shot_dir.mkdir(parents=True, exist_ok=True)
    binary = app_path / "Contents/MacOS/ClassicMac"
    env = os.environ.copy()
    env["HOME"] = str(SYNTH_HOME)
    env["CFFIXED_USER_HOME"] = str(SYNTH_HOME)
    env["CLASSICMAC_UI_SNAPSHOTS"] = str(shot_dir)
    env["CLASSICMAC_UI_TOUR"] = tour
    env["CLASSICMAC_DISABLE_UPDATE_CHECK"] = "1"
    if extra_env:
        env.update(extra_env)
    print(f"Running tour [{tour}] -> {shot_dir.name}")
    subprocess.run([str(binary)], env=env, timeout=timeout, check=True)
    return sorted(shot_dir.glob("*.png"))


def find_shot(shot_dir: Path, prefix: str, title_substr: str) -> Path:
    candidates = sorted(shot_dir.glob(f"{prefix}-*{title_substr}*.png"))
    if not candidates:
        all_pngs = [p.name for p in sorted(shot_dir.glob("*.png"))]
        raise FileNotFoundError(f"Missing shot {prefix}/*{title_substr}* in {shot_dir}: {all_pngs}")
    return candidates[-1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=DEFAULT_APP if DEFAULT_APP.exists() else FALLBACK_APP)
    parser.add_argument("--out", type=Path, default=MANUAL_SHOTS_DIR)
    parser.add_argument("--source-os9", type=Path, default=DEFAULT_SOURCE_OS9)
    parser.add_argument("--skip-tiger", action="store_true")
    parser.add_argument("--only-os9", action="store_true")
    parser.add_argument("--no-refresh-readme", action="store_true")
    parser.add_argument("--pippin-rom", type=Path, default=None, help="Optional 4 MB Pippin boot ROM for Pippin captures")
    parser.add_argument("--pippin-iso", type=Path, default=None, help="Optional Pippin CD image for Super Marathon capture")
    parser.add_argument("--pippin-nav-iso", type=Path, default=None, help="Optional Pippin Navigator CD image")
    args = parser.parse_args()

    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    raw_dir = REPO / "output/manual-shots-work/raw"

    if not args.only_os9:
        # Tour 1: Empty home -> First-run welcome wizard
        if SYNTH_HOME.exists():
            shutil.rmtree(SYNTH_HOME)
        for d in (
            SYNTH_HOME / "Documents/ClassicMac",
            SYNTH_HOME / "Library/Application Support/ClassicMac",
            SYNTH_HOME / "Library/Preferences",
        ):
            d.mkdir(parents=True, exist_ok=True)
        run_tour(args.app, "wait:1,shot:welcome", raw_dir / "t1-welcome")
        shutil.copy2(find_shot(raw_dir / "t1-welcome", "welcome", "ClassicMac"), out / "first-run-welcome.png")

        # Prepare populated isolated home with real machines
        prepare_synth_home(args.source_os9, include_tiger=not args.skip_tiger)

        # Tour 2: Library, New Mac, About, Checkpoints, Icons, Media (Quadra), Delete
        # Sorted indices: 0=Copland D11E4, 1=Mac OS 8.1, 2=Mac OS 9.2.2 Mega Deluxe, 3=Mac OS X Tiger, 4=System 7.5.5
        run_tour(
            args.app,
            "select:2,"
            "library,wait:4,shot:lib,"
            "newmac,wait:1,shot:newmac,"
            "about,wait:1,shot:about,"
            "icons,wait:1,shot:icons,"
            "checkpoints,wait:1,shot:cp,"
            "delete,wait:1,shot:del,"
            "select:1,media,wait:1,shot:qmedia",
            raw_dir / "t2-dialogs",
            timeout=120,
        )
        shutil.copy2(find_shot(raw_dir / "t2-dialogs", "lib", "Mac OS Library"), out / "library-410.png")
        shutil.copy2(find_shot(raw_dir / "t2-dialogs", "newmac", "New Mac"), out / "new-mac-dialog.png")
        shutil.copy2(find_shot(raw_dir / "t2-dialogs", "about", "About ClassicMac"), out / "about-410.png")
        shutil.copy2(find_shot(raw_dir / "t2-dialogs", "icons", "Iconfactory Icons"), out / "icon-packs.png")
        shutil.copy2(find_shot(raw_dir / "t2-dialogs", "cp", "Checkpoints"), out / "checkpoints-dialog.png")
        shutil.copy2(find_shot(raw_dir / "t2-dialogs", "del", "window"), out / "delete-mac-dialog.png")
        shutil.copy2(find_shot(raw_dir / "t2-dialogs", "qmedia", "Media"), out / "media-quadra.png")

        # Tour 3: Get Info panes (general, display, sharing, printing) + Power Mac Media + WindowShade + Update
        for pane, fname in [
            ("general", "get-info-general.png"),
            ("display", "get-info-display.png"),
            ("sharing", "get-info-sharing.png"),
            ("printing", "get-info-printing.png"),
        ]:
            run_tour(
                args.app,
                "select:2,settings,wait:1,shot:pane",
                raw_dir / f"t3-{pane}",
                extra_env={"CLASSICMAC_UI_SETTINGS_PANE": pane},
            )
            shutil.copy2(find_shot(raw_dir / f"t3-{pane}", "pane", "Info"), out / fname)

        run_tour(
            args.app,
            "select:2,media,wait:1,shot:pmmedia",
            raw_dir / "t3-pmmedia",
        )
        shutil.copy2(find_shot(raw_dir / "t3-pmmedia", "pmmedia", "Media"), out / "media-powermac.png")

        run_tour(
            args.app,
            "select:2,collapse,wait:1,shot:shade",
            raw_dir / "t3-shade",
        )
        shutil.copy2(find_shot(raw_dir / "t3-shade", "shade", "ClassicMac"), out / "windowshade.png")

        run_tour(
            args.app,
            "select:2,update,wait:4,shot:upd",
            raw_dir / "t3-update",
            extra_env={"CLASSICMAC_DISABLE_UPDATE_CHECK": "0"},
        )
        shutil.copy2(find_shot(raw_dir / "t3-update", "upd", "Software Update"), out / "software-update.png")

    # Tour 4: Live Mac OS 9.2.2 Mega Deluxe boot -> guest window, running home, paste text, shutdown alert, clean stop
    # Use default clean Sharing/Printing off for Tour 4 so Finder desktop & Launcher boot rapidly in headless automation.
    os9_bundle = SYNTH_HOME / "Documents/ClassicMac/Mac OS 9.2.2 Mega Deluxe.classic"
    os9_cfg_path = os9_bundle / "config.json"
    os9_cfg = json.loads(os9_cfg_path.read_text(encoding="utf-8"))
    os9_cfg["printingEnabled"] = False
    os9_cfg.pop("sharedFolderPath", None)
    os9_cfg.pop("printerName", None)
    write_json(os9_cfg_path, os9_cfg)
    ss_path = os9_bundle / ".session-state.json"
    if ss_path.exists():
        ss_path.unlink()

    run_tour(
        args.app,
        "select:2,start,wait:32,guest-activate,wait:1,shot:os9boot,"
        "paste,wait:1,shot:paste,"
        "shutdown,wait:1,shot:shut,"
        "stop",
        raw_dir / "t4-os9-live",
        extra_env={
            "CLASSICMAC_UI_PASTE_SAMPLE": "Hello from ClassicMac 4.10.0!\nPaste Text types safely via the US keyboard layout (café, 1999)."
        },
        timeout=240,
    )
    shutil.copy2(find_shot(raw_dir / "t4-os9-live", "os9boot", "Mac OS 9.2.2 Mega Deluxe"), out / "guest-os9-mega-deluxe.png")
    shutil.copy2(find_shot(raw_dir / "t4-os9-live", "os9boot", "ClassicMac"), out / "main-home-running.png")
    shutil.copy2(find_shot(raw_dir / "t4-os9-live", "paste", "Paste Text"), out / "paste-text-dialog.png")
    shutil.copy2(find_shot(raw_dir / "t4-os9-live", "shut", "window"), out / "shutdown-dialog.png")

    # Capture Home after clean OS 9 shutdown so the saved preview.png appears in ScreenPreview!
    run_tour(args.app, "select:2,wait:1,shot:home", raw_dir / "t4b-home", timeout=60)
    shutil.copy2(find_shot(raw_dir / "t4b-home", "home", "ClassicMac"), out / "main-home-410.png")

    if not args.only_os9:
        # Tour 5: Live Mac OS 8.1 (Quadra 800) boot
        run_tour(
            args.app,
            "select:1,start,wait:22,guest-activate,wait:1,shot:os81,stop",
            raw_dir / "t5-os81-live",
            timeout=180,
        )
        shutil.copy2(find_shot(raw_dir / "t5-os81-live", "os81", "Mac OS 8.1"), out / "guest-os81-quadra800.png")

        # Tour 6: Live System 7.5.5 (Quadra 800) boot
        s7_idx = 3 if args.skip_tiger else 4
        run_tour(
            args.app,
            f"select:{s7_idx},start,wait:20,guest-activate,wait:1,shot:s755,stop",
            raw_dir / "t6-s755-live",
            timeout=180,
        )
        shutil.copy2(find_shot(raw_dir / "t6-s755-live", "s755", "System 7.5.5"), out / "guest-sys755-quadra800.png")

        # Tour 7: Live Mac OS X 10.4.11 Tiger boot (use CLASSICMAC_UI_STAY=1 and clean process termination after capture)
        if not args.skip_tiger:
            t7_dir = raw_dir / "t7-tiger-live"
            if t7_dir.exists():
                shutil.rmtree(t7_dir)
            t7_dir.mkdir(parents=True, exist_ok=True)
            binary = args.app / "Contents/MacOS/ClassicMac"
            env = os.environ.copy()
            env["HOME"] = str(SYNTH_HOME)
            env["CFFIXED_USER_HOME"] = str(SYNTH_HOME)
            env["CLASSICMAC_UI_SNAPSHOTS"] = str(t7_dir)
            env["CLASSICMAC_UI_TOUR"] = "select:3,start,wait:48,guest-activate,wait:1,shot:tiger"
            env["CLASSICMAC_UI_STAY"] = "1"
            env["CLASSICMAC_DISABLE_UPDATE_CHECK"] = "1"
            print(f"Running tour [{env['CLASSICMAC_UI_TOUR']}] -> t7-tiger-live")
            proc = subprocess.Popen([str(binary)], env=env)
            try:
                deadline = time.time() + 120
                captured_path: Path | None = None
                while time.time() < deadline:
                    matches = sorted(t7_dir.glob("tiger-*Mac OS X Tiger*.png"))
                    if matches and matches[-1].stat().st_size > 0:
                        captured_path = matches[-1]
                        break
                    time.sleep(0.5)
                if captured_path is None:
                    raise TimeoutError("Timed out waiting for Mac OS X Tiger screenshot")
                shutil.copy2(captured_path, out / "guest-osx-tiger.png")
            finally:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                subprocess.run(["pkill", "-f", str(SYNTH_HOME)], check=False)

    # Refresh outdated README / Platinum screenshots
    if not args.no_refresh_readme:
        shutil.copy2(out / "guest-os9-mega-deluxe.png", PLATINUM_SHOTS_DIR / "running-ati.png")
        shutil.copy2(out / "guest-os9-mega-deluxe.png", PLATINUM_SHOTS_DIR / "guest-window.png")
        shutil.copy2(out / "main-home-410.png", PLATINUM_SHOTS_DIR / "main-window.png")
        shutil.copy2(out / "library-410.png", PLATINUM_SHOTS_DIR / "library.png")
        shutil.copy2(out / "get-info-display.png", PLATINUM_SHOTS_DIR / "get-info-display.png")
        shutil.copy2(out / "media-quadra.png", PLATINUM_SHOTS_DIR / "media.png")
        shutil.copy2(out / "windowshade.png", PLATINUM_SHOTS_DIR / "windowshade.png")
        shutil.copy2(out / "guest-os81-quadra800.png", ROOT_SHOTS_DIR / "quadra800-macos81.png")
        shutil.copy2(out / "guest-os9-mega-deluxe.png", ROOT_SHOTS_DIR / "powermacg4-macos92.png")

    if args.pippin_rom and args.pippin_rom.is_file():
        pip_docs = SYNTH_HOME / "Documents/ClassicMac"
        sup_dir = SYNTH_HOME / "Library/Application Support/ClassicMac"
        roms_dir = sup_dir / "ROMs"
        roms_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.pippin_rom, roms_dir / "pippin.rom")

        def seed_pippin_bundle(name: str, uid: str, iso_path: Path | None) -> Path:
            dst = pip_docs / f"{name}.classic"
            dst.mkdir(parents=True, exist_ok=True)
            for fname, size in [("disk.img", 1024 * 1024), ("pram.img", 256), ("nvram.img", 8192)]:
                with open(dst / fname, "wb") as fh:
                    fh.truncate(size)
            (dst / "flash.img").write_bytes(b"\xff" * 131072)
            cfg = {
                "id": uid,
                "name": name,
                "machineFamily": "pippin",
                "ramMB": 13,
                "diskSizeGB": 1,
                "diskImageName": "disk.img",
                "pramImageName": "pram.img",
                "flashImageName": "flash.img",
                "pippinROMPath": str(roms_dir / "pippin.rom"),
                "bootFromCD": iso_path is not None,
                "pippinVideoOut": "ntsc",
                "width": 640,
                "height": 480,
                "depth": 16,
                "networking": False,
                "sound": True,
                "useG4CPU": False,
                "classicInputHelpers": False,
                "legacyMouseTracking": True,
                "tabletInput": False,
            }
            if iso_path is not None:
                cfg["cdImagePath"] = str(iso_path)
            write_json(dst / "config.json", cfg)
            return dst

        pip_bundles = [seed_pippin_bundle("Apple Pippin", "33333333-3333-4333-8333-000000000001", args.pippin_iso)]
        if args.pippin_nav_iso and args.pippin_nav_iso.is_file():
            pip_bundles.append(seed_pippin_bundle("Pippin Navigator", "33333333-3333-4333-8333-000000000002", args.pippin_nav_iso))
        write_json(sup_dir / "library.json", [str(b) for b in pip_bundles])

        run_tour(args.app, "newmac,wait:1,shot:pnew", raw_dir / "tp-newmac", extra_env={"CLASSICMAC_UI_NEWMAC_FAMILY": "pippin"})
        shutil.copy2(find_shot(raw_dir / "tp-newmac", "pnew", "New Mac"), out / "pippin-new-mac-dialog.png")
        run_tour(args.app, "select:0,settings,wait:1,shot:pgen", raw_dir / "tp-gen", extra_env={"CLASSICMAC_UI_SETTINGS_PANE": "general"})
        shutil.copy2(find_shot(raw_dir / "tp-gen", "pgen", "Info"), out / "pippin-get-info-general.png")
        run_tour(args.app, "select:0,settings,wait:1,shot:pctl", raw_dir / "tp-ctl", extra_env={"CLASSICMAC_UI_SETTINGS_PANE": "controls"})
        shutil.copy2(find_shot(raw_dir / "tp-ctl", "pctl", "Info"), out / "pippin-get-info-controls.png")
        if args.pippin_iso and args.pippin_iso.is_file():
            run_tour(args.app, "select:0,start,wait:23,guest-activate,wait:2,shot:pmarathon,stop", raw_dir / "tp-marathon", timeout=120)
            shutil.copy2(find_shot(raw_dir / "tp-marathon", "pmarathon", "Apple Pippin"), out / "guest-pippin-marathon.png")
            if not args.no_refresh_readme:
                shutil.copy2(out / "guest-pippin-marathon.png", ROOT_SHOTS_DIR / "pippin-super-marathon.png")
        if args.pippin_nav_iso and args.pippin_nav_iso.is_file():
            run_tour(args.app, "select:1,start,wait:24,guest-activate,wait:2,shot:pnav,stop", raw_dir / "tp-nav", timeout=120)
            shutil.copy2(find_shot(raw_dir / "tp-nav", "pnav", "Pippin Navigator"), out / "guest-pippin-navigator.png")

    print(f"\nCaptured screenshots in {out}:")
    for p in sorted(out.glob("*.png")):
        print(f"  {p.name} ({p.stat().st_size:,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
