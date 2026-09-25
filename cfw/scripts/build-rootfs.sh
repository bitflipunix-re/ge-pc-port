#!/bin/sh
set -eu

CFW=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$CFW/versions.env"

BRSRC=${BRSRC:-"$CFW/.cache/src/buildroot-$BUILDROOT_VERSION"}
OUT=${OUT:-"$CFW/out/buildroot"}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

[ -d "$BRSRC" ] || {
    echo "Buildroot source not found: $BRSRC" >&2
    echo "run: sh cfw/scripts/fetch-sources.sh buildroot" >&2
    exit 1
}

mkdir -p "$OUT"

make -C "$BRSRC" O="$OUT" BR2_EXTERNAL="$CFW"     BR2_DEFCONFIG="$CFW/config/buildroot-r36s_defconfig" defconfig

make -C "$BRSRC" O="$OUT" BR2_EXTERNAL="$CFW" olddefconfig

for sym in     BR2_aarch64     BR2_cortex_a35     BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_AARCH64_GLIBC_STABLE     BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV     BR2_PACKAGE_LIBDRM_INSTALL_TESTS     BR2_TARGET_ROOTFS_EXT2_4; do
    if ! grep -q "^${sym}=y$" "$OUT/.config"; then
        echo "required Buildroot config ${sym}=y was not resolved" >&2
        exit 1
    fi
done

make -C "$BRSRC" O="$OUT" BR2_EXTERNAL="$CFW" -j"$JOBS"

ART="$CFW/out/artifacts"
mkdir -p "$ART"
cp "$OUT/images/rootfs.ext4" "$ART/rootfs-r36s.ext4"
cp "$OUT/.config" "$ART/buildroot-r36s.config"

printf 'built:\n  %s\n' "$ART/rootfs-r36s.ext4"
