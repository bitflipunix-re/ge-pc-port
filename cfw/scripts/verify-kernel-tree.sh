#!/bin/sh
set -eu

CFW=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$CFW/versions.env"
KSRC=${1:-"$CFW/.cache/src/linux-$KERNEL_VERSION"}

fail=0
check() {
    if "$@"; then
        printf 'PASS: %s\n' "$*"
    else
        printf 'FAIL: %s\n' "$*" >&2
        fail=1
    fi
}

check test -f "$KSRC/arch/arm64/boot/dts/rockchip/rk3326-r36s.dts"
check test -f "$KSRC/drivers/gpu/drm/panel/panel-generic-dsi.c"
check grep -q 'rk3326-r36s.dtb' "$KSRC/arch/arm64/boot/dts/rockchip/Makefile"
check grep -q 'DRM_PANEL_GENERIC_DSI' "$KSRC/drivers/gpu/drm/panel/Kconfig"
check grep -q 'panel-generic-dsi.o' "$KSRC/drivers/gpu/drm/panel/Makefile"

exit "$fail"
