# R36S bring-up order

Do not debug every subsystem at once.

## Stage A - kernel boots

Use the existing known-good R36S bootloader and replace only the kernel, DTB and rootfs.

Pass:
- kernel reaches userspace;
- root filesystem mounts read-write or read-only as intended;
- boot log is retained.

## Stage B - board identity and storage

Pass:
- RK3326 CPUs enumerate;
- RAM size is sane;
- SD/MMC devices enumerate without repeated errors;
- thermal zones and cpufreq nodes exist.

## Stage C - display

Pass:
- Rockchip DRM binds;
- R36S DSI panel probes;
- 640x480 mode is exposed;
- a DRM dumb-buffer/page-flip test can fill the screen.

Do not introduce SDL yet.

## Stage D - Panfrost

Pass:
- panfrost probes the Mali-G31;
- /dev/dri/card0 exists;
- /dev/dri/renderD128 exists;
- dmesg contains no GPU MMU fault/reset loop at idle.

## Stage E - Mesa 26.2.3

Only after stages A-D pass.

Pass:
- libdrm + GBM + EGL initialize;
- Mesa reports Panfrost/Mali-G31 rather than software rendering;
- GLES diagnostic clears/swaps at native panel resolution;
- frame timing is stable.

## Stage F - input and audio

Validate all buttons/sticks through evdev/IIO, then RK817 audio.

## Stage G - runtime

Add SDL2/SDL3, the custom launcher and native ports. GoldenEye returns here, against the known graphics stack rather than the old firmware environment.
