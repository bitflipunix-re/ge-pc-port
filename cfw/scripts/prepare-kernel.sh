#!/bin/sh
set -eu

CFW=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$CFW/versions.env"

KSRC=${1:-"$CFW/.cache/src/linux-$KERNEL_VERSION"}
DTS_DIR="$KSRC/arch/arm64/boot/dts/rockchip"
PANEL_DIR="$KSRC/drivers/gpu/drm/panel"
LOGO_DIR="$KSRC/drivers/video/logo"
LOGO_SVG="$CFW/board/r36s/bitflipunix-logo.svg"

[ -d "$KSRC" ] || {
    echo "kernel source not found: $KSRC" >&2
    echo "run cfw/scripts/fetch-sources.sh first" >&2
    exit 1
}

install -m 0644 "$CFW/board/r36s/rk3326-r36s.dts" "$DTS_DIR/rk3326-r36s.dts"
install -m 0644 "$CFW/board/r36s/panel-generic-dsi.c" "$PANEL_DIR/panel-generic-dsi.c"

# Match the lower Rockchip DSI lane-rate margin used by downstream/vendor
# code. Linux 6.12 uses 1/0.8 (25% margin); this exact panel shows loss of
# sync/vertical-line decay when Linux takes ownership from U-Boot. Use 1/0.9
# (~11% margin), matching the current upstream Rockchip correction.
DSI_DRV="$KSRC/drivers/gpu/drm/rockchip/dw-mipi-dsi-rockchip.c"
python3 - "$DSI_DRV" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
old1 = "tmp = mpclk * (bpp / lanes) * 10 / 8;"
new1 = "tmp = DIV_ROUND_UP(mpclk * bpp * 10, lanes * 9);"
old2 = "phy_mipi_dphy_get_default_config(mode->clock * 1000 * 10 / 8,"
new2 = "phy_mipi_dphy_get_default_config((u32)mode->clock * 1000 * 10 / 9,"
if old1 not in s:
    raise SystemExit("Rockchip DSI lane-rate expression not found")
if old2 not in s:
    raise SystemExit("Rockchip external-DPHY rate expression not found")
s = s.replace(old1, new1, 1).replace(old2, new2, 1)
p.write_text(s)
print("patched Rockchip DSI lane margin: 1/0.8 -> 1/0.9")
PY

# PX30/RK3326 uses the newer INNO D-PHY family. Linux 6.12 still feeds
# it the RK3288/RK3399 variable HSTT table. Use the fixed PX30 timings from
# the current Rockchip upstream correction for this exact-target kernel.
python3 - "$DSI_DRV" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
old = """static int
dw_mipi_dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
                           struct dw_mipi_dsi_dphy_timing *timing)
{
        int i;

        for (i = 0; i < ARRAY_SIZE(hstt_table); i++)
                if (lane_mbps < hstt_table[i].maxfreq)
                        break;

        if (i == ARRAY_SIZE(hstt_table))
                i--;

        *timing = hstt_table[i].timing;

        return 0;
}"""
# Kernel source uses tabs; normalize just this function for a robust match.
start = s.find("static int\ndw_mipi_dsi_phy_get_timing(")
end = s.find("\n\nstatic const struct dw_mipi_dsi_phy_ops", start)
if start < 0 or end < 0:
    raise SystemExit("Rockchip DSI timing function not found")
new = """static int
dw_mipi_dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
                           struct dw_mipi_dsi_dphy_timing *timing)
{
        /*
         * PX30/RK3326 INNO D-PHY fixed timing.
         * Match the downstream Rockchip DSI transition timing used by the\n         * known-good vendor-era path: 0x40 for all four transitions.
         */
        timing->clk_lp2hs = 0x40;
        timing->clk_hs2lp = 0x40;
        timing->data_lp2hs = 0x40;
        timing->data_hs2lp = 0x40;

        return 0;
}"""
s = s[:start] + new + s[end:]
p.write_text(s)
print("patched Rockchip PX30 fixed D-PHY timings")
PY

# Convert BitflipUnix artwork into Linux's 224-colour boot-logo format.
if command -v magick >/dev/null 2>&1; then
    magick "$LOGO_SVG" -alpha off -colors 224 -compress none \
        "$LOGO_DIR/logo_linux_clut224.ppm"
elif command -v convert >/dev/null 2>&1; then
    convert "$LOGO_SVG" -alpha off -colors 224 -compress none \
        "$LOGO_DIR/logo_linux_clut224.ppm"
else
    echo "ImageMagick is required to prepare the BitflipUnix boot logo" >&2
    exit 1
fi

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

echo "R36S board support and BitflipUnix boot logo installed into $KSRC"
