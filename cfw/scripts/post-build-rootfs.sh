#!/bin/sh
set -eu

TARGET_DIR=$1

chmod 0755 "$TARGET_DIR/etc/init.d/S99r36s-bringup"
mkdir -p "$TARGET_DIR/var/log"
