## Notes

GoldenEye 007 AArch64/GLES alpha for R36S / dArkOSRE.

After installation, place a legally obtained US NTSC big-endian GoldenEye 007 ROM at:

`ge007/data/ge007.ntsc-final.z64`

Expected SHA-1:

`abe01e4aeb033b6c0836819f549c791b26cfde83`

The first launch generates the required host-format sidecars locally from that ROM and then starts the game. No ROM, extracted assets, or generated sidecars are included in the package.

## Port Control

Open the ARM-GE Port Control overlay with **Select/Back** on controller or **F10** on keyboard.

Port Control exposes display resolution, render resolution scaling, MSAA, experimental TAA-lite, filtering, FOV/draw distance, audio/input/gameplay options, live CPU/FPS/RAM telemetry and guarded CPU/GPU/RAM performance profiles.

The shipped R36S defaults are conservative: 1x MSAA, 100% render scale, temporal AA off and system-default governors.

CPU/GPU/RAM profile changes apply on the next launch. The launcher records the original kernel settings and restores them when the game exits. The game executable itself is not launched with root privileges.

`Trim allocator now` is an unprivileged in-process memory trim and can be used immediately.

Runtime logs are written to:

`ge007/log.txt`

Select+Start exits back to EmulationStation.

Thanks to the n64decomp/007 contributors and the GoldenEye PC-port contributors. ARM64/R36S work and PortMaster packaging by bitflipunix and Tomobobo710.
