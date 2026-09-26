## Launch directly from EmulationStation

After the port files are installed, **you do not need to open the PortMaster UI to launch GoldenEye**. EmulationStation sees the root launcher:

```text
GoldenEye 007.sh
```

The launcher locates the adjacent `ge007/` directory from its own path, loads PortMaster's existing device/runtime support, and starts the native AArch64 executable.

For a direct/offline install, with PortMaster support already present on the firmware:

1. Extract `ge007.zip` directly into the active ROM volume's `ports/` directory.
2. Confirm these sit beside each other:

   ```text
   ports/GoldenEye 007.sh
   ports/ge007/
   ```

3. Put the verified ROM at `ports/ge007/data/ge007.ntsc-final.z64`.
4. Refresh/restart EmulationStation's game list.
5. Launch **GoldenEye 007** from the Ports system.

This bypasses opening the PortMaster application; it does not remove the dependency on PortMaster's installed `control.txt`/device helpers.

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


## Port Control overlay

Press **F10** to open ARM-GE's scalable Glass Control Deck.

The VIDEO page exposes output resolution, internal render scale, live MSAA, TXAA-style temporal AA, texture/mipmap filtering, anisotropy, FOV, draw distance and LOD controls.

The SYSTEM page exposes live CPU/FPS/RAM telemetry plus conservative CPU/GPU governor and RAM profiles. Performance-profile changes are saved to `ge007.ini`, applied by this PortMaster launcher on the next run when the firmware exposes writable endpoints, and restored when the game exits.

The temporal AA option is ARM-GE's own temporal accumulation implementation; it is not NVIDIA TXAA.

For debugging, check `ge007/log.txt`. A healthy overlay startup includes a `control map ... wired (0 missing)` line.
