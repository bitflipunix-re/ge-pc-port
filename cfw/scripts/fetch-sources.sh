#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/versions.env"

DL="$HERE/.cache/downloads"
SRC="$HERE/.cache/src"
mkdir -p "$DL" "$SRC"

want=${1:-all}

case "$want" in
    all|kernel|mesa|buildroot) ;;
    *)
        echo "usage: sh cfw/scripts/fetch-sources.sh [all|kernel|mesa|buildroot]" >&2
        exit 2
        ;;
esac

fetch() {
    url=$1
    out=$2
    if [ -f "$out" ]; then
        return
    fi
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

fetch_kernel() {
    tarball="$DL/linux-$KERNEL_VERSION.tar.xz"
    fetch "$KERNEL_URL" "$tarball"
    verify "$KERNEL_SHA256" "$tarball"
    rm -rf "$SRC/linux-$KERNEL_VERSION"
    tar -C "$SRC" -xf "$tarball"
    printf 'kernel: %s\n' "$SRC/linux-$KERNEL_VERSION"
}

fetch_mesa() {
    tarball="$DL/mesa-$MESA_VERSION.tar.xz"
    fetch "$MESA_URL" "$tarball"
    verify "$MESA_SHA256" "$tarball"
    rm -rf "$SRC/mesa-$MESA_VERSION"
    tar -C "$SRC" -xf "$tarball"
    printf 'mesa: %s\n' "$SRC/mesa-$MESA_VERSION"
}

fetch_buildroot() {
    tarball="$DL/buildroot-$BUILDROOT_VERSION.tar.xz"
    fetch "$BUILDROOT_URL" "$tarball"
    verify "$BUILDROOT_SHA256" "$tarball"
    rm -rf "$SRC/buildroot-$BUILDROOT_VERSION"
    tar -C "$SRC" -xf "$tarball"
    printf 'buildroot: %s\n' "$SRC/buildroot-$BUILDROOT_VERSION"
}

case "$want" in
    kernel) fetch_kernel ;;
    mesa) fetch_mesa ;;
    buildroot) fetch_buildroot ;;
    all)
        fetch_kernel
        fetch_mesa
        fetch_buildroot
        ;;
esac
