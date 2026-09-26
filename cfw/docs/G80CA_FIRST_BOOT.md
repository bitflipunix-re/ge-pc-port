# G80CA Panel-8 first boot

Target:

`G80CA-MB V1.2-20250422 Panel 8`

The first hardware test does **not** replace the installed raw U-Boot/SPL sectors.

## Before changing the card

Make a byte-for-byte backup of the working OS card or, at minimum, save:

- every file from the FAT BOOT partition;
- the current Linux DTB;
- the current kernel image;
- the partition table.

Do not overwrite the raw sectors before the first partition during this milestone.

## BOOT partition payload

The CFW test bundle supplies:

- `Image` — Linux 6.12.111;
- `rk3326-r36s.dtb` — exact G80CA Panel-8 board definition;
- `boot.ini` — loads the new kernel/DTB while retaining the current bootloader.

The boot script tries the common R36S U-Boot BOOT partition enumeration and falls back between `mmc 1:1` and `mmc 0:1`.

## ROOTFS

Write `rootfs.ext4` into an ext4 partition.

The source rootfs is labeled `ROOTFS`, but the bundle packager reads the filesystem UUID and pins that exact UUID into the bundled `boot.ini` when `blkid` is available.

This avoids both a fixed Linux `mmcblkXpY` number and ambiguity if an older card happens to contain another partition with the same filesystem label.

## Expected first successful boot

1. U-Boot loads the CFW `Image` and `rk3326-r36s.dtb`.
2. Linux 6.12.111 starts.
3. The G80CA KD35T133-compatible Panel-8 sequence runs.
4. The 640x480 framebuffer console becomes visible.
5. Buildroot reaches BusyBox init/getty.
6. `/var/log/r36s-bringup.log` is generated.
7. `/dev/dri/card0` and `/dev/dri/renderD128` should appear when Rockchip DRM/Panfrost probe successfully.

## First commands

```sh
cat /proc/device-tree/model
uname -a
cat /var/log/r36s-bringup.log
ls -l /dev/dri
dmesg | grep -Ei 'panfrost|drm|dsi|panel|rk817|mmc|saradc'
modetest -c
```

On the Mesa image:

```sh
r36s-gpu-probe
kmscube
```

A valid GPU pass must report the Mali/Panfrost hardware renderer. llvmpipe, softpipe or swrast is a failure.

## If the LCD stays black

Do not assume the kernel failed.

Use the serial console if attached:

`ttyS2 @ 1500000 8N1`

The exact vendor DTB identifies `serial2` as UART2 at `0xff160000`, and the CFW makes that alias explicit.

Check specifically for:

- panel regulator/GPIO failures;
- DSI attach failures;
- Rockchip VOP/DSI bind failures;
- deferred probes;
- DTB load failures.

## Rollback

Restore the saved BOOT partition files and the previous root filesystem/partition contents.

Because SPL/U-Boot is intentionally untouched in milestone 0, a kernel/rootfs failure does not require rebuilding the bootloader.
