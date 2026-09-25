#!/bin/sh
set -eu

CFW=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$CFW/versions.env"

BRSRC=${BRSRC:-"$CFW/.cache/src/buildroot-$BUILDROOT_VERSION"}
OUT=${OUT:-"$CFW/out/graphics"}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

[ -d "$BRSRC" ] || {
    echo "Buildroot source not found: $BRSRC" >&2
    echo "run: sh cfw/scripts/fetch-sources.sh buildroot" >&2
    exit 1
}

sh "$CFW/scripts/prepare-buildroot-mesa.sh" "$BRSRC"
mkdir -p "$OUT"

make -C "$BRSRC" O="$OUT" BR2_EXTERNAL="$CFW"     BR2_DEFCONFIG="$CFW/config/buildroot-r36s-mesa_defconfig" defconfig
make -C "$BRSRC" O="$OUT" BR2_EXTERNAL="$CFW" olddefconfig

for sym in     BR2_aarch64     BR2_cortex_a35     BR2_PACKAGE_MESA3D     BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_PANFROST     BR2_PACKAGE_MESA3D_OPENGL_EGL     BR2_PACKAGE_MESA3D_OPENGL_ES     BR2_PACKAGE_MESA3D_GBM     BR2_PACKAGE_KMSCUBE \
    BR2_PACKAGE_R36S_GPU_PROBE; do
    if ! grep -q "^${sym}=y$" "$OUT/.config"; then
        echo "required graphics config ${sym}=y was not resolved" >&2
        exit 1
    fi
done

if grep -q '^BR2_PACKAGE_MESA3D_LLVM=y$' "$OUT/.config"; then
    echo "target LLVM unexpectedly enabled; refusing bloated image" >&2
    exit 1
fi

make -C "$BRSRC" O="$OUT" BR2_EXTERNAL="$CFW" -j"$JOBS"

test -x "$OUT/target/usr/bin/kmscube"
test -x "$OUT/target/usr/bin/r36s-gpu-probe"
test -d "$OUT/target/usr/lib/dri"
find "$OUT/target/usr/lib/dri" -maxdepth 1 -type f -o -type l | sort

ART="$CFW/out/artifacts"
mkdir -p "$ART"
cp "$OUT/images/rootfs.ext4" "$ART/rootfs-r36s-mesa-$MESA_VERSION.ext4"
cp "$OUT/.config" "$ART/buildroot-r36s-mesa-$MESA_VERSION.config"

printf 'built graphics rootfs:\n  %s\n'     "$ART/rootfs-r36s-mesa-$MESA_VERSION.ext4"
