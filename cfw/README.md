# R36S CFW

Private-development firmware work for the RK3326/Mali-G31 R36S target.

## Locked baseline

- Architecture: AArch64
- SoC: Rockchip RK3326
- GPU: Mali-G31 MP2
- Kernel: Linux 6.12.111 LTS
- GPU kernel driver: Panfrost
- Mesa: 26.2.3
- Root filesystem builder: Buildroot 2026.08
- Display path: Rockchip DRM/KMS -> GBM/EGL
- Initial userspace: BusyBox + diagnostics
- Initial bootloader policy: keep the known-good R36S boot chain until the kernel/rootfs/display stack is proven

No X11, Wayland compositor, EmulationStation, RetroArch, PortMaster, GL4ES, or game runtime is part of milestone 0.

## Milestone 0: first controlled boot

The first image is successful when it:

1. boots Linux 6.12.111 on the R36S;
2. mounts the minimal root filesystem;
3. exposes the eMMC/SD, input, thermal and power interfaces needed for bring-up;
4. initializes Rockchip DRM/KMS and the 640x480 panel;
5. binds Panfrost to the Mali-G31 and creates /dev/dri/card0 plus a render node;
6. records a complete boot log and hardware report.

Mesa is milestone 1. A launcher and game ports are later milestones.

## Tree

- `versions.env` - pinned upstream versions and hashes
- `scripts/fetch-sources.sh` - reproducible source fetch
- `config/kernel-r36s.fragment` - kernel options required for bring-up
- `docs/BRINGUP.md` - staged validation order

All firmware work stays under `cfw/` until it is split into its own repository.
