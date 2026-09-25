# R36S graphics stack

Target stack:

`Rockchip DRM/KMS -> Panfrost kernel driver -> libdrm/GBM -> Mesa 26.2.3 Panfrost -> EGL/GLES/OpenGL`

There is deliberately no X11, Wayland compositor or GL4ES in the bring-up image.

## Build-time compiler split

Mesa's Panfrost compiler requires LLVM at build time. For cross compilation, Mesa supports building the required compiler tools on the host and using them during the target build.

The CFW patches Buildroot 2026.08 accordingly:

- host Mesa/LLVM builds `mesa-clc` and the Panfrost precompiler;
- target Mesa is built with LLVM disabled;
- target OpenCL is not enabled;
- the R36S root filesystem does not carry LLVM merely to run Panfrost.

## First graphics tests

On device:

```
modetest -c
kmscube
```

The first success condition is hardware-rendered `kmscube` directly through DRM/KMS + GBM/EGL with Mesa reporting Panfrost on the Mali-G31.

Software rendering is not considered a pass.
