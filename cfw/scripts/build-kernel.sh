#!/bin/sh
set -eu

CFW=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$CFW/versions.env"

KSRC=${KSRC:-"$CFW/.cache/src/linux-$KERNEL_VERSION"}
OUT=${OUT:-"$CFW/out/kernel"}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

[ -d "$KSRC" ] || {
    echo "kernel source not found: $KSRC" >&2
    echo "run: sh cfw/scripts/fetch-sources.sh" >&2
    exit 1
}

sh "$CFW/scripts/prepare-kernel.sh" "$KSRC"
mkdir -p "$OUT"

MAKE_ARGS="ARCH=arm64 O=$OUT"

if [ "$(uname -m)" = "aarch64" ] && command -v gcc >/dev/null 2>&1; then
    :
elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    MAKE_ARGS="$MAKE_ARGS CROSS_COMPILE=aarch64-linux-gnu-"
elif command -v clang >/dev/null 2>&1 && command -v ld.lld >/dev/null 2>&1; then
    MAKE_ARGS="$MAKE_ARGS LLVM=1"
else
    echo "need native AArch64 gcc, aarch64-linux-gnu-gcc, or clang+lld" >&2
    exit 1
fi

# shellcheck disable=SC2086
make -C "$KSRC" $MAKE_ARGS tinyconfig

"$KSRC/scripts/kconfig/merge_config.sh" -m -O "$OUT"     "$OUT/.config" "$CFW/config/kernel-r36s.fragment"

# shellcheck disable=SC2086
make -C "$KSRC" $MAKE_ARGS olddefconfig

for sym in DRM_ROCKCHIP DRM_PANFROST DRM_PANEL_GENERIC_DSI PHY_ROCKCHIP_INNO_DSIDPHY MMC_DW_ROCKCHIP MFD_RK8XX_I2C BLK_DEV_INITRD FRAMEBUFFER_CONSOLE; do
    if ! grep -q "^CONFIG_${sym}=y$" "$OUT/.config"; then
        echo "required config CONFIG_${sym}=y was not resolved" >&2
        exit 1
    fi
done

# shellcheck disable=SC2086
make -C "$KSRC" $MAKE_ARGS -j"$JOBS" Image rockchip/rk3326-r36s.dtb

ART="$CFW/out/artifacts"
mkdir -p "$ART"
cp "$OUT/arch/arm64/boot/Image" "$ART/Image-$KERNEL_VERSION-r36s"
cp "$OUT/arch/arm64/boot/dts/rockchip/rk3326-r36s.dtb" "$ART/rk3326-r36s.dtb"
cp "$OUT/.config" "$ART/kernel-$KERNEL_VERSION-r36s.config"

IMAGE_BYTES=$(wc -c < "$ART/Image-$KERNEL_VERSION-r36s")
echo "raw Image bytes: $IMAGE_BYTES"
if [ "$IMAGE_BYTES" -gt 16777216 ]; then
    echo "lean bring-up Image exceeds 16 MiB BOOT budget" >&2
    exit 1
fi

printf 'built:\n  %s\n  %s\n'     "$ART/Image-$KERNEL_VERSION-r36s"     "$ART/rk3326-r36s.dtb"
