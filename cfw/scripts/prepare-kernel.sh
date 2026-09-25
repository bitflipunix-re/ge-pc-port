#!/bin/sh
set -eu

CFW=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$CFW/versions.env"

KSRC=${1:-"$CFW/.cache/src/linux-$KERNEL_VERSION"}
DTS_DIR="$KSRC/arch/arm64/boot/dts/rockchip"
PANEL_DIR="$KSRC/drivers/gpu/drm/panel"

[ -d "$KSRC" ] || {
    echo "kernel source not found: $KSRC" >&2
    echo "run cfw/scripts/fetch-sources.sh first" >&2
    exit 1
}

install -m 0644 "$CFW/board/r36s/rk3326-r36s.dts" "$DTS_DIR/rk3326-r36s.dts"
install -m 0644 "$CFW/board/r36s/panel-generic-dsi.c" "$PANEL_DIR/panel-generic-dsi.c"

DTS_MAKEFILE="$DTS_DIR/Makefile"
if ! grep -q 'rk3326-r36s.dtb' "$DTS_MAKEFILE"; then
    printf '\ndtb-$(CONFIG_ARCH_ROCKCHIP) += rk3326-r36s.dtb\n' >> "$DTS_MAKEFILE"
fi

PANEL_MAKEFILE="$PANEL_DIR/Makefile"
if ! grep -q 'panel-generic-dsi.o' "$PANEL_MAKEFILE"; then
    printf '\nobj-$(CONFIG_DRM_PANEL_GENERIC_DSI) += panel-generic-dsi.o\n' >> "$PANEL_MAKEFILE"
fi

PANEL_KCONFIG="$PANEL_DIR/Kconfig"
if ! grep -q '^config DRM_PANEL_GENERIC_DSI$' "$PANEL_KCONFIG"; then
    cat >> "$PANEL_KCONFIG" <<'EOF'

config DRM_PANEL_GENERIC_DSI
	tristate "Generic description-driven MIPI-DSI panel"
	depends on OF
	depends on DRM_MIPI_DSI
	depends on BACKLIGHT_CLASS_DEVICE
	depends on REGULATOR
	help
	  Description-driven MIPI-DSI panel support used for R36S
	  bring-up. The panel timing and initialization sequence are
	  supplied by the device tree.
EOF
fi

echo "R36S board support installed into $KSRC"
