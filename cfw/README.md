# R36S CFW

Private-development firmware for the exact handheld identified by the hardware probe as:

`G80CA-MB V1.2-20250422 Panel 8`

This is no longer treated as a generic R36S/Panel-4 target.

## Locked baseline

- Architecture: AArch64
- SoC: Rockchip RK3326
- GPU: Mali-G31
- Kernel: Linux 6.12.111 LTS
- GPU kernel driver: Panfrost
- Mesa: 26.2.3
- Root filesystem builder: Buildroot 2026.08
- C library: glibc
- Display path: Rockchip DRM/KMS -> GBM/EGL
- Initial userspace: BusyBox + hardware diagnostics
- Initial bootloader policy: keep the known-good installed R36S SPL/U-Boot until the new kernel/rootfs/display stack is proven

No X11 desktop, Wayland compositor, EmulationStation, RetroArch, PortMaster or GL4ES is required for the bring-up image.

## Current milestone status

### Kernel / board layer — compiled and validated

The exact-board CI build passes for:

- Linux 6.12.111;
- exact G80CA Panel-8 board verifier;
- RK3326 device tree;
- captured KD35T133-compatible Panel-8 DSI program;
- Rockchip DRM/KMS;
- Panfrost;
- DSI PHY;
- RK817 PMIC/regulators;
- dual SD slots;
- GPIO buttons;
- ADC volume keys;
- single-ADC analogue-stick mux;
- RK817 audio infrastructure;
- thermal/cpufreq;
- serial bring-up console.

Validated workflow head:

`ab340be4eb48bbe070fbaded111f898d46ab3b0f`

Workflow run:

`36204659613`

### Base userspace — compiled and validated

Buildroot 2026.08/glibc rootfs builds successfully as a 256 MiB ext4 filesystem labeled `ROOTFS`.

It includes the first-boot hardware report service and DRM/input diagnostics.

### First-device bundle — ready

A non-destructive first-test bundle has been produced from the successful kernel artifact and successful base rootfs.

It intentionally keeps the currently working raw SPL/U-Boot sectors. The first device test therefore changes only the boot partition payload and root filesystem.

### Mesa/Panfrost userspace — in progress

The graphics image builds Mesa 26.2.3 with the Panfrost Gallium driver, GBM, EGL, GLES/OpenGL, `kmscube`, and our `r36s-gpu-probe`.

Target LLVM is explicitly rejected. Required Panfrost compiler tooling is built for the build host instead.

## Hardware contract

See `docs/G80CA_PANEL8.md`.

The Panel-8 command stream is checked byte-for-byte against the known-good DTB for the exact board revision before every kernel build.

## First hardware success criteria

1. Existing SPL/U-Boot loads our `Image` and exact G80CA DTB.
2. Linux 6.12.111 reaches userspace.
3. The 640x480 Panel-8 display initializes.
4. The Buildroot console becomes visible.
5. Both physical SD interfaces enumerate correctly.
6. Rockchip DRM creates a card device.
7. Panfrost creates a render node.
8. The bring-up log is captured.

Mesa hardware rendering is the next gate after this base boot succeeds.

## Tree

- `board/r36s/` — exact G80CA board and panel support
- `config/` — kernel and Buildroot configurations
- `scripts/` — reproducible source/build/verification tooling
- `rootfs-overlay/` — first-boot userspace additions
- `package/r36s-gpu-probe/` — direct DRM/GBM/EGL hardware renderer probe
- `boot/` — first-stage boot payload
- `docs/` — hardware contract, graphics and device-test procedures

All CFW work remains isolated under `cfw/` on the `r36s-cfw` branch while the firmware is under bring-up.
