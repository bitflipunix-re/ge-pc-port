#!/bin/sh
set -eu

CFW=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$CFW/versions.env"

BRSRC=${1:-"$CFW/.cache/src/buildroot-$BUILDROOT_VERSION"}

[ -d "$BRSRC/package/mesa3d" ] || {
    echo "Buildroot source not found: $BRSRC" >&2
    exit 1
}

python3 - "$BRSRC" "$MESA_VERSION" "$MESA_SHA256" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
version = sys.argv[2]
sha256 = sys.argv[3]
sha512 = "ec621735ae4679762ea27b979a5314c7aecfdd4165afea19c8c55f933d79b40f01fa041a95874fc4347e732ac565786d9b616a9aa8856a422204d9ecc9a12ec4"

def replace(path, old, new):
    p = root / path
    s = p.read_text()
    if old not in s:
        if new in s:
            return
        raise SystemExit(f"expected text not found in {p}: {old!r}")
    p.write_text(s.replace(old, new))

# Pin Buildroot's Mesa package and matching headers to our chosen release.
replace("package/mesa3d/mesa3d.mk",
        "MESA3D_VERSION = 26.1.8",
        f"MESA3D_VERSION = {version}")
replace("package/mesa3d-headers/mesa3d-headers.mk",
        "MESA3D_HEADERS_VERSION = 26.1.8",
        f"MESA3D_HEADERS_VERSION = {version}")

for rel in ("package/mesa3d/mesa3d.hash", "package/mesa3d-headers/mesa3d-headers.hash"):
    p = root / rel
    s = p.read_text()
    s = s.replace(
        "sha256  b320f65874fd9653ac6c0bd1616605387344e1247411a50c797b5f3fb9dc0b55  mesa-26.1.8.tar.xz",
        f"sha256  {sha256}  mesa-{version}.tar.xz")
    s = s.replace(
        "sha512  eb3d46ae9aaba70fdd908486cef24041fec9264ddf586eaebbba65ce55e716d306ef9e91be83e742dfaec4dfc94105a5cd3a36b29bf56614febcbcf6c1f8b516  mesa-26.1.8.tar.xz",
        f"sha512  {sha512}  mesa-{version}.tar.xz")
    p.write_text(s)

# Mesa documents that cross builds can keep LLVM on the host:
# build/install mesa-clc + Panfrost precomp tools on the host, then use
# -Dmesa-clc=system -Dprecomp-compiler=system for the target.
config = root / "package/mesa3d/Config.in"
s = config.read_text()
s = s.replace(
"""config BR2_PACKAGE_MESA3D_NEEDS_PRECOMP_COMPILER
	bool
	select BR2_PACKAGE_MESA3D_OPENCL
	select BR2_PACKAGE_SPIRV_LLVM_TRANSLATOR
	select BR2_PACKAGE_SPIRV_TOOLS
""",
"""config BR2_PACKAGE_MESA3D_NEEDS_PRECOMP_COMPILER
	bool
""")
s = s.replace(
"""config BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_PANFROST
	bool "Gallium panfrost driver"
	depends on BR2_TOOLCHAIN_HAS_SYNC_4 || !BR2_PACKAGE_XORG7 # libxshmfence
	depends on BR2_PACKAGE_MESA3D_LLVM
""",
"""config BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_PANFROST
	bool "Gallium panfrost driver"
	depends on BR2_TOOLCHAIN_HAS_SYNC_4 || !BR2_PACKAGE_XORG7 # libxshmfence
""")
config.write_text(s)

mk = root / "package/mesa3d/mesa3d.mk"
s = mk.read_text()
s = s.replace(
"MESA3D_DEPENDENCIES += host-mesa3d spirv-llvm-translator spirv-tools",
"MESA3D_DEPENDENCIES += host-mesa3d")
mk.write_text(s)

print(f"Buildroot Mesa recipe prepared for Mesa {version}")
PY

grep -q "^MESA3D_VERSION = $MESA_VERSION$" "$BRSRC/package/mesa3d/mesa3d.mk"
grep -q "$MESA_SHA256.*mesa-$MESA_VERSION.tar.xz" "$BRSRC/package/mesa3d/mesa3d.hash"
