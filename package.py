#!/usr/bin/env python3
"""Assemble the PortMaster zip from the 007-r36s tree.

Usage:
    python package.py [--game-bin PATH] [--out DIR] [--zip-name NAME]

Layout inside the zip:

    ge007/
        ge007.aarch64               the game (default: build/arm64/)
        data/ge007.ini              R36S-safe video defaults
        data/.place-user-rom-and-sidecars-here
        prepare-assets/
            prepare-assets.py d43_emit.py d69_emit.py d88_emit.py d88_propdefs.py
            ge007-convert           shell wrapper: runs the scripts on device python3
            vendor/scripts/filelist.u.csv
            vendor/assets/**        file_resource_table + model headers
        build-info.txt
    GoldenEye 007.sh                launcher (runs the converter on first boot)

Refuses to include any ROM (.z64/.n64/.v64) or sidecar (pcmodels.bin/pccg.bin).
"""
import argparse
import datetime
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "work" / "goldeneye-pc-port"
BUNDLE = ROOT / "bundle" / "prepare-assets"
LAUNCHER = ROOT / "port" / "GoldenEye 007.sh"
PORT_JSON = ROOT / "port" / "port.json"
GAMEINFO = ROOT / "port" / "gameinfo.xml"
README = ROOT / "port" / "README.md"
DEFAULT_BIN = ROOT / "build" / "arm64" / "ge007.aarch64"

CONVERTER_SCRIPTS = [
    "prepare-assets.py",
    "d43_emit.py",
    "d69_emit.py",
    "d88_emit.py",
    "d88_propdefs.py",
]

WRAPPER = """#!/bin/sh
# ge007-convert: runs the bundled converter on the device's python3.
exec /usr/bin/env python3 "$(dirname "$0")/prepare-assets.py" "$@"
"""

INI = """# GoldenEye 007 R36S defaults (shipped in zip; edit freely).
# Conservative handheld baseline; Port Control can change these in-game.
[Video]
MSAA = 1
RenderScale = 100
TAA = 0
Anisotropy = 1

# Optional per-game tuning. Non-zero values are applied by the PortMaster
# launcher and the original kernel settings are restored when the game exits.
[System]
CpuGovernor = 0
GpuGovernor = 0
RamProfile = 0
"""

ROM_SUFFIXES = (".z64", ".n64", ".v64")
SIDECAR_BINS = ("pcmodels.bin", "pccg.bin")


def die(msg):
    print(f"package.py: error: {msg}", file=sys.stderr)
    sys.exit(1)


def check_elf_aarch64(path):
    with open(path, "rb") as f:
        head = f.read(20)
    if len(head) < 20 or head[:4] != b"\x7fELF":
        return False
    return struct.unpack("<H", head[18:20])[0] == 183  # EM_AARCH64


def stage_converter(stage):
    prep = stage / "ge007" / "prepare-assets"
    prep.mkdir(parents=True)
    for name in CONVERTER_SCRIPTS:
        src = BUNDLE / name
        if not src.is_file():
            die(f"missing converter script: {src}")
        shutil.copy2(src, prep / name)

    vendor = BUNDLE / "vendor"
    if not (vendor / "scripts" / "filelist.u.csv").is_file():
        die("missing vendor/scripts/filelist.u.csv")
    if not (vendor / "assets" / "obseg" / "file_resource_table.inc.c").is_file():
        die("missing vendor/assets/obseg/file_resource_table.inc.c")
    shutil.copytree(vendor, prep / "vendor")
    headers = [p for p in (prep / "vendor" / "assets").rglob("*")
               if p.name.lower().endswith("modelfileheader.inc.c")]
    if not headers:
        die("no *modelFileHeader.inc.c under vendor/assets")

    wrapper = prep / "ge007-convert"
    # LF only: exec'd by shebang on device.
    wrapper.write_text(WRAPPER, encoding="ascii", newline="\n")
    print(f"package.py: staged converter ({len(headers)} model headers)")
    return wrapper


def main():
    ap = argparse.ArgumentParser(description="Assemble the PortMaster zip.")
    ap.add_argument("--game-bin", default=str(DEFAULT_BIN), help="path to ge007.aarch64")
    ap.add_argument("--out", default=str(ROOT / "dist"), help="output dir (default: dist/)")
    ap.add_argument("--zip-name", default=None, help="zip file name (default: port.json name)")
    args = ap.parse_args()

    game_bin = Path(args.game_bin)
    if not game_bin.is_file():
        die(f"--game-bin {game_bin}: not a file")
    if not check_elf_aarch64(game_bin):
        die(f"--game-bin {game_bin}: not an AArch64 ELF")
    if not LAUNCHER.is_file():
        die(f"missing launcher: {LAUNCHER}")
    if b"\r\n" in LAUNCHER.read_bytes():
        die(f"{LAUNCHER} has CRLF line endings")
    for f in (PORT_JSON, GAMEINFO, README):
        if not f.is_file():
            die(f"missing {f}")
    try:
        zip_name = args.zip_name or json.loads(PORT_JSON.read_text(encoding="utf-8"))["name"]
    except Exception as e:
        die(f"cannot read {PORT_JSON}: {e}")
    print(f"package.py: game binary OK ({game_bin.stat().st_size} bytes, AArch64)")

    stage = Path(tempfile.mkdtemp(prefix="ge007-pkg-"))
    try:
        wrapper = stage_converter(stage)

        shutil.copy2(game_bin, stage / "ge007" / "ge007.aarch64")
        data = stage / "ge007" / "data"
        data.mkdir(parents=True, exist_ok=True)
        (data / ".place-user-rom-and-sidecars-here").write_text(
            "Put your GoldenEye 007 US NTSC .z64 ROM in this folder as\n"
            "ge007.ntsc-final.z64, then launch. Sidecars generate on first boot.\n",
            encoding="ascii", newline="\n")
        (data / "ge007.ini").write_text(INI, encoding="ascii", newline="\n")

        launcher = stage / LAUNCHER.name
        shutil.copy2(LAUNCHER, launcher)
        shutil.copy2(PORT_JSON, stage / "port.json")
        shutil.copy2(GAMEINFO, stage / "gameinfo.xml")
        shutil.copy2(README, stage / "README.md")

        try:
            rev = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=SRC,
                                 capture_output=True, text=True, timeout=30).stdout.strip()
            dirty = subprocess.run(["git", "status", "--porcelain"], cwd=SRC,
                                   capture_output=True, text=True, timeout=30).stdout.strip()
            if rev and dirty:
                rev += "-dirty"
        except Exception:
            rev = ""
        (stage / "ge007" / "build-info.txt").write_text(
            f"source-rev: {rev or 'unknown'}\n"
            f"built-utc: {datetime.datetime.now(datetime.timezone.utc):%Y-%m-%dT%H:%M:%SZ}\n",
            encoding="ascii", newline="\n")

        bad = [p for p in stage.rglob("*")
               if p.is_file() and (p.suffix.lower() in ROM_SUFFIXES or p.name in SIDECAR_BINS)]
        if bad:
            die("refusing to ship ROM/sidecar data: " + ", ".join(str(p) for p in bad[:5]))

        outdir = Path(args.out)
        outdir.mkdir(parents=True, exist_ok=True)
        zippath = outdir / zip_name
        if zippath.exists():
            zippath.unlink()
        exec_files = {wrapper.resolve(), launcher.resolve(),
                      (stage / "ge007" / "ge007.aarch64").resolve()}
        with zipfile.ZipFile(zippath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for p in sorted(stage.rglob("*")):
                if not p.is_file():
                    continue
                zi = zipfile.ZipInfo(p.relative_to(stage).as_posix())
                zi.external_attr = (0o755 if p.resolve() in exec_files else 0o644) << 16
                zi.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(zi, p.read_bytes())

        with zipfile.ZipFile(zippath) as z:
            names = z.namelist()
        checks = {
            "ge007/ge007.aarch64 in zip": "ge007/ge007.aarch64" in names,
            "launcher in zip": LAUNCHER.name in names,
            "port.json at zip root": "port.json" in names,
            "gameinfo.xml at zip root": "gameinfo.xml" in names,
            "README.md at zip root": "README.md" in names,
            "no nested metadata": "ge007/port.json" not in names and "ge007/gameinfo.xml" not in names,
            "converter wrapper in zip": "ge007/prepare-assets/ge007-convert" in names,
            "converter scripts in zip": all(f"ge007/prepare-assets/{n}" in names
                                            for n in CONVERTER_SCRIPTS),
            "no ROM in zip": not any(n.lower().endswith(ROM_SUFFIXES) for n in names),
            "no sidecar bins in zip": not any(Path(n).name in SIDECAR_BINS for n in names),
        }
        for what, ok in checks.items():
            print(f"package.py: verify {'OK  ' if ok else 'FAIL'} {what}")
            if not ok:
                die("verification failed")
        print(f"package.py: wrote {zippath} ({zippath.stat().st_size} bytes, {len(names)} files)")
    finally:
        shutil.rmtree(stage, ignore_errors=True)


if __name__ == "__main__":
    main()
