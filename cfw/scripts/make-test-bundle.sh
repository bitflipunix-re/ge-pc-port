#!/bin/sh
set -eu

CFW=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "usage: sh cfw/scripts/make-test-bundle.sh KERNEL_ZIP ROOTFS_EXT4 [OUTPUT_ZIP]" >&2
    exit 2
fi

KERNEL_ZIP=$1
ROOTFS=$2
OUTPUT=${3:-r36s-cfw-first-boot.zip}

[ -f "$KERNEL_ZIP" ] || { echo "missing kernel zip: $KERNEL_ZIP" >&2; exit 1; }
[ -f "$ROOTFS" ] || { echo "missing rootfs: $ROOTFS" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

mkdir -p "$TMP/kernel" "$TMP/bundle/BOOT" "$TMP/bundle/ROOTFS"
unzip -q "$KERNEL_ZIP" -d "$TMP/kernel"

IMAGE=$(find "$TMP/kernel" -maxdepth 2 -type f -name 'Image-*r36s' | head -n 1)
DTB=$(find "$TMP/kernel" -maxdepth 2 -type f -name 'rk3326-r36s.dtb' | head -n 1)

[ -n "$IMAGE" ] && [ -f "$IMAGE" ] || { echo "kernel Image not found in $KERNEL_ZIP" >&2; exit 1; }
[ -n "$DTB" ] && [ -f "$DTB" ] || { echo "R36S DTB not found in $KERNEL_ZIP" >&2; exit 1; }

cp "$IMAGE" "$TMP/bundle/BOOT/Image"
cp "$DTB" "$TMP/bundle/BOOT/rk3326-r36s.dtb"
cp "$CFW/boot/boot.ini" "$TMP/bundle/BOOT/boot.ini"
cp "$ROOTFS" "$TMP/bundle/ROOTFS/rootfs.ext4"

cat >"$TMP/bundle/README.txt" <<'EOF'
R36S CFW — FIRST DEVICE TEST

This is intentionally NOT a blank-SD flash image.

It preserves the known-good R36S bootloader already present on the test card.

BOOT/
  Image
  rk3326-r36s.dtb
  boot.ini

ROOTFS/
  rootfs.ext4

Before testing:
1. Make a complete backup of the existing SD card.
2. Keep the existing raw bootloader sectors intact.
3. Replace the files on the first FAT BOOT partition with the contents of BOOT/.
4. Write ROOTFS/rootfs.ext4 to an ext4 partition whose filesystem label is ROOTFS.
5. Boot and inspect /var/log/r36s-bringup.log.

A successful graphics image should also run:
  modetest -c
  kmscube

Do not treat a software-rendered result as a Panfrost pass.
EOF

(
    cd "$TMP/bundle"
    sha256sum BOOT/Image BOOT/rk3326-r36s.dtb BOOT/boot.ini ROOTFS/rootfs.ext4 > SHA256SUMS
    zip -q -r "$OLDPWD/$OUTPUT" .
)

echo "$OUTPUT"
