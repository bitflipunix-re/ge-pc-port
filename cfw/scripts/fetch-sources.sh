#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/versions.env"

DL="$HERE/.cache/downloads"
SRC="$HERE/.cache/src"
mkdir -p "$DL" "$SRC"

fetch() {
    url=$1
    out=$2
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 -o "$out.part" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$out.part" "$url"
    else
        echo "curl or wget is required" >&2
        exit 1
    fi
    mv "$out.part" "$out"
}

verify() {
    hash=$1
    file=$2
    printf '%s  %s\n' "$hash" "$file" | sha256sum -c -
}

kernel_tar="$DL/linux-$KERNEL_VERSION.tar.xz"
mesa_tar="$DL/mesa-$MESA_VERSION.tar.xz"
buildroot_tar="$DL/buildroot-$BUILDROOT_VERSION.tar.xz"

[ -f "$kernel_tar" ] || fetch "$KERNEL_URL" "$kernel_tar"
[ -f "$mesa_tar" ] || fetch "$MESA_URL" "$mesa_tar"
[ -f "$buildroot_tar" ] || fetch "$BUILDROOT_URL" "$buildroot_tar"

verify "$KERNEL_SHA256" "$kernel_tar"
verify "$MESA_SHA256" "$mesa_tar"

rm -rf "$SRC/linux-$KERNEL_VERSION" "$SRC/mesa-$MESA_VERSION" "$SRC/buildroot-$BUILDROOT_VERSION"
tar -C "$SRC" -xf "$kernel_tar"
tar -C "$SRC" -xf "$mesa_tar"
tar -C "$SRC" -xf "$buildroot_tar"

printf 'kernel:    %s\n' "$SRC/linux-$KERNEL_VERSION"
printf 'mesa:      %s\n' "$SRC/mesa-$MESA_VERSION"
printf 'buildroot: %s\n' "$SRC/buildroot-$BUILDROOT_VERSION"
