# First R36S CFW boot

The first hardware test deliberately keeps the currently working R36S bootloader.

## BOOT partition

Place these files in the first FAT boot partition:

- `Image` — our Linux 6.12.111 kernel
- `rk3326-r36s.dtb` — our R36S DTB
- `boot.ini` — the CFW boot script

Back up the existing BOOT partition before replacing anything.

## ROOTFS partition

Create or replace an ext4 partition labeled exactly `ROOTFS` with the contents of the generated Buildroot filesystem.

The CFW boot arguments locate this partition by filesystem label rather than a fixed `mmcblkXpY` device name.

## Expected first boot

A successful first boot reaches a BusyBox login on tty1 and writes:

`/var/log/r36s-bringup.log`

The report captures the kernel, memory, DRM connectors, Panfrost-related dmesg lines, inputs, thermal state and CPU frequency.

Before Mesa is introduced, `modetest -c` is the primary display validation command.
