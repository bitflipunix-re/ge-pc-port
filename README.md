# ARM-GE — GoldenEye 007, Native on ARM64 Linux

> **GoldenEye 007's reconstructed Nintendo 64 codebase running natively on ARM64 Linux handheld hardware — AArch64 + SDL2 + OpenGL ES, not N64 emulation.**

ARM-GE is an open-source engineering effort to make the reconstructed GoldenEye 007 Nintendo 64 codebase run as native software on low-power ARM64 Linux handhelds.

The current reference target is **R36S / dArkOSRE / PortMaster** using **AArch64 + SDL2 + OpenGL ES 3**.

## Current release

ARM-GE has now moved from **alpha** to **beta** for the R36S / PortMaster target.

The first player-focused beta prerelease was published on **26 September 2026** from the validated main branch:

- Release publication revision: `88e15245756e82074bf63b9312e7197122b1cd03`
- Build run: `36271139262`
- Release tag: `r36s-beta-2026-09-26`
- Release: https://github.com/bitflipunix-re/ge-pc-port/releases/tag/r36s-beta-2026-09-26
- Standalone executable: `ge007.aarch64` — 1,798,512 bytes
- Executable asset SHA-256: `0ae0578d9260c9d3ff4930fb933a96dad53692e5b9b417a5bd51e3c31f07a458`
- PortMaster installer: `ge007-r36s-portmaster-beta.zip` — 1,102,377 bytes
- Installer asset SHA-256: `e12d8fb37deb670f4f47629393657be60f65ba30b7fe54cb976adfa6b8ee546b`
- Compatibility installer filename: `ge007.zip`
- Architecture: AArch64
- Graphics: SDL2 + OpenGL ES
- Target: R36S / dArkOSRE / PortMaster
- ROM target: GoldenEye 007 NTSC-U, big-endian

This beta is intentionally different from the development builds used during the ARM64 bring-up. It is built in **Release** mode and stripped, with development-only benchmarking, render probes, frame-dump capture paths and the standalone DAM-lab HUD excluded from the player binary/package.

The beta keeps the features needed for normal use: the ARM-GE Glass Control Deck, normal runtime error logging, production crash screen, controller support, clean Start+Select exit, ROM verification, first-run sidecar generation and the lightweight CPU/FPS/RAM telemetry backend used by Port Control.

The release workflow verifies the AArch64 executable and installer, checks that the player package contains no benchmark launcher or development probe markers, refuses to publish ROM images or generated ROM-derived sidecars, and publishes both the standalone `.aarch64` executable and an explicitly named PortMaster installer package (while retaining `ge007.zip` for compatibility).

## September 26 completion pass

The current mainline includes the verified ARM64 work from the active development branches:

- host-width stage/setup rebasing for campaign and multiplayer data;
- animation/model/title/language/background pointer-width fixes;
- cached animation pointer zero-extension and host-width comparison fixes;
- character-action pointer truth/width fixes;
- player-body teardown hardening for stale or missing props;
- fresh-save NULL-pointer safety;
- stage-boundary SFX cleanup for long-session audio stability;
- displacement-stable absolute/direct mouse look;
- batched-tick crosshair damping stabilization;
- scripted/death/pause camera ownership protection;
- watch-menu toggle and slider persistence;
- CPU/FPS/RAM diagnostics retained while the intrusive HUD defaults off;
- mission-save/title-handoff/stage-unload breadcrumbs for remaining transition failures;
- an ARM64 semantic regression gate that runs before every reference build.

Older Dam, PortMaster integration and Tom experiment branches are behind the consolidated game-port mainline. The `r36s-cfw` branch is a separate firmware project and is intentionally not merged into the GoldenEye game port.

## Current showcase

[![Watch the current ARM64/R36S showcase](https://img.youtube.com/vi/lcN9C9waB6I/hqdefault.jpg)](https://www.youtube.com/shorts/lcN9C9waB6I)

Latest public video: https://www.youtube.com/shorts/lcN9C9waB6I

[Full showcase notes →](SHOWCASE.md)

## ARM-GE Glass Control Deck

The F10 overlay is a first-class port feature rather than a debug-only menu. Its layout is generated from the live GoldenEye VI coordinate space, so the same interface scales across the R36S target and larger desktop/handheld resolutions without fixed-resolution artwork.

Open it with **F10**. On a controller, use the D-pad/left stick to navigate, **LB/RB** to change pages, **A/X** to advance a value, **B/Y** to step backward, and **Start** to close. Mouse interaction is mapped through Fast3D's actual cropped UI viewport, so click targets remain aligned when gameplay safe-area or viewport cropping is active.

### Video controls

The VIDEO page exposes the renderer rather than presenting cosmetic placeholders:

- **Output resolution** — windowed output-size presets filtered against the current desktop. Fullscreen uses the panel/display mode supplied by SDL/PortMaster.
- **Internal resolution** — 50–200% render scale with full-output presentation. The game renders the 3D scene into the selected lower/higher-resolution framebuffer and then scales that image to the complete output surface; it does **not** shrink the viewport. On the 640x480 R36S panel, 75% therefore renders 480x360 internally and presents it across the full 640x480 panel. The overlay shows both dimensions.
- **MSAA** — OFF/2x/4x/8x through Fast3D's multisample framebuffer/resolve path, clamped to what the GLES driver reports.
- **Temporal AA (TXAA-style)** — optional low/high temporal accumulation on the final frame. This is an ARM-GE temporal AA implementation inspired by the same class of techniques; it is **not NVIDIA TXAA** and does not claim NVIDIA's proprietary implementation.
- **Graphics presets** — N64 / Crisp / Enhanced / R36S / Performance. Preset identity is derived from the live renderer settings, so a manual change immediately becomes Custom instead of leaving a stale preset label. Every preset writes a complete owned setting set. Performance renders at 75% internally and upscales to full output, uses 1x MSAA, disables temporal AA, keeps anisotropy low, and uses the game's own model-LOD machinery at 50% distance to reduce model/matrix/render work without changing AI or simulation timing.
- VSync, frame cap, texture filtering, mip filtering, anisotropic filtering, FOV, draw distance, LOD distance and framebuffer effects.
- Live video settings are routed through `video.c` into Fast3D. Settings that can safely rebuild or reconfigure at runtime apply live; settings explicitly marked for restart are persisted instead.

### System / performance controls

The SYSTEM page combines live telemetry with conservative per-game tuning:

- live FPS, CPU use, process RAM and available system memory;
- current CPU governor, GPU governor and VM swappiness;
- **CPU governor:** System / schedutil / performance / powersave;
- **GPU governor:** System / simple_ondemand / performance / powersave;
- **RAM profile:** System / Low Swap / Balanced / Game;
- allocator trim action;
- live overlay control-map verification count.

Governor/RAM selections are intentionally **not** written to privileged sysfs/procfs by the game executable. They are saved in `ge007.ini`; the PortMaster launcher applies supported values on the **next launch** using PortMaster's privilege helper when required, records the original kernel values, and restores them when the game exits. Unsupported governors/endpoints are logged and skipped.

The **Game** RAM profile currently uses a conservative temporary VM policy: swappiness 5 and `vm.vfs_cache_pressure=50`. It does not resize zram, drop caches, overclock hardware, pin clocks, or make persistent system changes.

The overlay performs a startup wiring audit and writes the result to `log.txt` as:

```text
optionsoverlay: control map <wired>/<total> wired (<missing> missing)
```

A public build should report zero missing registered controls before the overlay is considered fully wired.

## What works on real hardware

The port has demonstrated:

- native AArch64 execution on R36S-class hardware;
- SDL2/OpenGL ES rendering;
- boot, menus and intro;
- in-mission rendering and gameplay;
- controller integration;
- PortMaster installation and launch;
- first-run generation of required ROM-derived sidecars from the user's own ROM;
- runtime logging and on-device diagnostics;
- native **Start + Select** clean-exit chord back to EmulationStation, including while Port Control is open;
- direct EmulationStation Ports launcher after installation;

This is now a **beta-stage port**. Active work is concentrated on real-device correctness, campaign completion and polish rather than broad 32→64 conversion: full-campaign behavior, spawn/state transitions, AI/objectives/props, collision/navigation edge cases, GLES rendering defects, long-session audio behavior and broader handheld compatibility.

## Port Control overlay

**Port Control** is intended to be a major part of ARM-GE rather than a thin debug menu. Press **F10** on keyboard or **Select/Back** on a controller to open it. While open, gameplay input is captured by the overlay and mouse capture is released.

The interface uses a resolution-scalable translucent glass layout and currently exposes:

- output/window resolution and fullscreen state;
- **50–200% render resolution scaling**;
- VSync and frame cap;
- **real Fast3D/GLES MSAA** at 1x/2x/4x/8x, clamped to the GPU's supported sample count;
- experimental **Temporal AA / TAA-lite** LOW/HIGH modes;
- texture, mipmap and anisotropic filtering;
- framebuffer effects, FOV, draw distance and LOD distance;
- mouse, controller, aim and key-bind controls;
- audio latency/buffer/mixer controls;
- gameplay accessibility and original GoldenEye cheat controls;
- live CPU/FPS/RAM/audio/stage telemetry;
- live CPU and GPU governor reporting;
- optional per-game CPU governor, GPU governor and RAM/swappiness profiles;
- a safe in-process allocator trim action.

The R36S package intentionally ships conservative defaults: **1x MSAA, 100% render scale, TAA off and system-default governors**. More expensive graphics features are opt-in.

The AArch64 render path also uses larger same-state Fast3D triangle batches and builds the isolated Fast3D/RSP translation layer at `-O3` (without `-ffast-math`). These optimizations reduce host/GLES submission overhead without changing the reconstructed GoldenEye simulation.

CPU/GPU/RAM profiles do not run the game as root. Port Control stores small numeric profile selections in `ge007.ini`; on the next launch the PortMaster wrapper translates them into fixed known values, uses PortMaster's existing privileged helper only for the required sysfs/procfs writes, records the previous values, and restores them when the game exits.

The temporal option is deliberately described as **TAA/TAA-lite**, not NVIDIA TXAA. It is a lightweight experimental temporal accumulation pass suitable for testing on the GLES target and is disabled while Port Control is open so the overlay itself remains stable.

---

# Install on R36S / dArkOSRE / PortMaster

## Requirements

You need:

1. an **AArch64 R36S-class handheld** running dArkOSRE/ArkOS-compatible PortMaster;
2. a working **PortMaster** installation;
3. the release file **`ge007.zip`**;
4. **Python 3 available on the handheld** for the one-time ROM-to-sidecar conversion;
5. your own legally obtained **GoldenEye 007 US NTSC big-endian ROM**.

No GoldenEye ROM, extracted game assets, `pcmodels.bin` or `pccg.bin` are distributed by this project.

### Required ROM

Filename:

```text
ge007.ntsc-final.z64
```

Expected SHA-1:

```text
abe01e4aeb033b6c0836819f549c791b26cfde83
```

The launcher checks the SHA-1 when `sha1sum` is available and refuses a known-wrong ROM.

## Recommended installation

### Direct EmulationStation install

If PortMaster support is already installed on the firmware, **the PortMaster application does not need to be opened to launch or install this build manually**.

Extract `ge007.zip` directly into the active ROM volume's `ports/` directory. The resulting layout must include:

```text
/roms/ports/GoldenEye 007.sh
/roms/ports/ge007/
```

(or the equivalent `/roms2/ports/` path).

The root launcher is executable in the package and self-locates the adjacent `ge007/` directory. Put the ROM under `ge007/data/`, refresh/restart EmulationStation, and launch **GoldenEye 007** from the Ports system. The launcher still uses PortMaster's installed `control.txt` and device helpers; it simply does not require opening the PortMaster UI.

### PortMaster autoinstall

For ArkOS/dArkOSRE, copy `ge007.zip` into the PortMaster autoinstall directory:

```text
/roms/tools/PortMaster/autoinstall/
```

If your setup uses the second ROM volume, use the corresponding `roms2` PortMaster tree instead.

Then:

1. Start the **PortMaster** application.
2. Allow PortMaster to process the autoinstall ZIP.
3. After installation, the game directory on the current R36S target is normally:

   ```text
   /roms/ports/ge007/
   ```

4. Copy your ROM to:

   ```text
   /roms/ports/ge007/data/ge007.ntsc-final.z64
   ```

   If PortMaster is using a different ROM root, place it under the installed `ge007/data/` directory for that root.

5. Launch **GoldenEye 007** from EmulationStation.

PortMaster's current documentation also supports installing offline ports by placing their ZIP in the appropriate autoinstall directory and starting PortMaster.

## First launch

On first launch the wrapper:

1. loads PortMaster's device/CFW control environment;
2. verifies that `ge007.aarch64` exists;
3. checks for the required ROM;
4. verifies the ROM SHA-1 when possible;
5. checks whether the generated sidecars already exist;
6. runs the bundled Python converter if they do not;
7. verifies that these files were generated:

   ```text
   ge007/data/pcmodels-ntsc-final/pcmodels.bin
   ge007/data/pccg-ntsc-final/pccg.bin
   ```

8. launches `ge007.aarch64`.

The ROM and generated sidecars remain local to the user's device.

## Runtime files

Important installed paths:

```text
ge007/
├── ge007.aarch64
├── data/
│   ├── ge007.ntsc-final.z64        # user supplied
│   ├── ge007.ini
│   ├── pcmodels-ntsc-final/        # generated locally
│   └── pccg-ntsc-final/            # generated locally
├── prepare-assets/
├── conf/
├── build-info.txt
└── log.txt
```

`log.txt` is recreated on launch and is the first file to collect when reporting a crash or startup failure.

## Common installation failures

### ROM missing

The log will contain:

```text
[ROM] MISSING
```

Confirm the ROM is named exactly:

```text
ge007.ntsc-final.z64
```

and is inside the installed `ge007/data/` directory.

### Wrong ROM

If the SHA-1 does not match:

```text
abe01e4aeb033b6c0836819f549c791b26cfde83
```

the launcher exits instead of generating sidecars.

### Python 3 missing

First-run conversion requires `python3`. If it is unavailable the launcher reports:

```text
[Extract] python3 not available on this firmware
```

A PortMaster installation with the required runtime support or a firmware providing Python 3 is required for first-run conversion.

### Sidecar conversion failure

Check:

```text
ge007/log.txt
```

The launcher prints the converter return code and refuses to start the game if the required sidecars are still missing.

---

# Build from source

## Reference build environment

The reproducible reference build currently uses **Ubuntu 24.04 x86_64** and cross-compiles to **AArch64 Linux**.

The canonical build definition is:

```text
.github/workflows/build-r36s.yml
```

If local setup and CI ever disagree, the workflow is the source of truth.

The build uses **GCC**, not Clang, because the reconstructed codebase relies on structure/inheritance behavior supported by GCC extensions.

## Clone

```bash
git clone https://github.com/bitflipunix-re/ge-pc-port.git
cd ge-pc-port
```

## Build dependencies

Host/build tools:

```text
cmake
make
ccache
pkg-config
unzip
file
python3
git
gcc-aarch64-linux-gnu
g++-aarch64-linux-gnu
binutils-aarch64-linux-gnu
libc6-dev-arm64-cross
```

Graphics/runtime development dependencies:

```text
libsdl2-dev
libgles2-mesa-dev
libegl1-mesa-dev
zlib1g-dev
libsdl2-2.0-0:arm64
libgles2:arm64
libegl1:arm64
zlib1g:arm64
```

## Ubuntu 24.04 dependency setup

Enable the ARM64 package architecture:

```bash
sudo dpkg --add-architecture arm64
```

The GitHub Actions runner explicitly configures the Ubuntu amd64 archive and the Ubuntu Ports ARM64 archive before installation. On a normal Ubuntu 24.04 machine, make sure apt has valid sources for both architectures, then run:

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake make ccache pkg-config unzip file python3 git \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu libc6-dev-arm64-cross \
  libsdl2-dev libgles2-mesa-dev libegl1-mesa-dev zlib1g-dev \
  libsdl2-2.0-0:arm64 libgles2:arm64 libegl1:arm64 zlib1g:arm64
```

The CI build also extracts the ARM64 SDL2 development package so its target-specific headers are available:

```bash
mkdir -p /tmp/sdl2-arm64-dev
cd /tmp
apt-get download libsdl2-dev:arm64
dpkg-deb -x libsdl2-dev_*_arm64.deb /tmp/sdl2-arm64-dev
cd -
```

Ensure the target linker names exist:

```bash
sudo ln -sf libSDL2-2.0.so.0 /usr/lib/aarch64-linux-gnu/libSDL2.so
sudo ln -sf libGLESv2.so.2 /usr/lib/aarch64-linux-gnu/libGLESv2.so
sudo ln -sf libEGL.so.1 /usr/lib/aarch64-linux-gnu/libEGL.so
sudo ln -sf libz.so.1 /usr/lib/aarch64-linux-gnu/libz.so
```

## Run the ARM64 semantic regression gate

Before compiling:

```bash
python3 work/goldeneye-pc-port/tools_pc/arm64_semantic_audit.py
```

This catches known classes of accidental host-pointer truncation and token/pointer confusion before a build is published.

## Configure the R36S AArch64/GLES build

From the repository root:

```bash
export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/

cmake -S work/goldeneye-pc-port -B build/arm64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  -DCMAKE_ASM_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_C_COMPILER_TARGET=aarch64 \
  -DCMAKE_CXX_COMPILER_TARGET=aarch64 \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DROMID=ntsc-final \
  -DCMAKE_BUILD_TYPE=Release \
  -DGE_BETA_RELEASE=ON \
  -DGE_DEV_PROBES=OFF \
  -DSDL2_INCLUDE_DIR=/usr/include/SDL2 \
  -DSDL2_LIBRARY=/usr/lib/aarch64-linux-gnu/libSDL2.so \
  -DZLIB_INCLUDE_DIR=/usr/include \
  -DZLIB_LIBRARY=/usr/lib/aarch64-linux-gnu/libz.so \
  -DGL_LIBRARY=/usr/lib/aarch64-linux-gnu/libGLESv2.so \
  -DCMAKE_C_FLAGS="-DUSE_GLES=1 -I/tmp/sdl2-arm64-dev/usr/include/aarch64-linux-gnu" \
  -DCMAKE_CXX_FLAGS="-DUSE_GLES=1 -I/tmp/sdl2-arm64-dev/usr/include/aarch64-linux-gnu"
```

A correct configure must report an AArch64 target and the output binary `ge007.aarch64`.

Normal builds compile the Fast3D render path without the historical D-series diagnostic probes.

For a player-equivalent beta build, configure with:

```text
-DCMAKE_BUILD_TYPE=Release
-DGE_BETA_RELEASE=ON
-DGE_DEV_PROBES=OFF
```

`GE_BETA_RELEASE=ON` compiles the development benchmark hooks, standalone DAM-lab HUD, presentation probes and frame-dump/debug capture paths out of the player binary while retaining the telemetry backend used by Port Control.

To reproduce a render investigation that relies on environment variables such as `GE_D172`, `GE_D229`, `GE_D236*`, `GE_D288` or texture dumps, use a development build with `-DGE_DEV_PROBES=ON`. Do not use developer probes for performance testing or release packages.

## Compile

```bash
cmake --build build/arm64 -j"$(nproc)"
```

Expected output:

```text
build/arm64/ge007.aarch64
```

## Verify the binary

```bash
file build/arm64/ge007.aarch64
aarch64-linux-gnu-readelf -h build/arm64/ge007.aarch64
sha256sum build/arm64/ge007.aarch64
```

The ELF header must report:

```text
Machine: AArch64
```

## Build the PortMaster package

From the repository root:

```bash
python3 package.py \
  --game-bin build/arm64/ge007.aarch64 \
  --out dist
```

Expected output:

```text
dist/ge007.zip
```

The packager confirms the input executable is AArch64 and deliberately refuses to package:

- `*.z64`
- `*.n64`
- `*.v64`
- `pcmodels.bin`
- `pccg.bin`

## Verify the PortMaster package

```bash
unzip -t dist/ge007.zip
unzip -l dist/ge007.zip
unzip -p dist/ge007.zip port.json | python3 -m json.tool
sha256sum dist/ge007.zip
```

The archive must contain at least:

```text
port.json
gameinfo.xml
README.md
GoldenEye 007.sh
ge007/ge007.aarch64
ge007/prepare-assets/
```

and must not contain any ROM or generated ROM-derived sidecar binary.

## Build products

The CI artifact contains:

```text
dist/ge007.zip
dist/ge007.zip.sha256
dist/ge007.zip.list
build/arm64/ge007.aarch64
build/arm64/ge007.elf-header.txt
build/arm64/ge007.sha256
```

---

# Architecture and portability work

Porting reconstructed N64 software to a modern host is not a mechanical 32-bit-to-64-bit conversion.

A recurring rule in this project is to classify every address-like value as one of:

1. **native host pointer**
2. **N64 / ROM / segmented-address token**
3. **ordinary integer or game state**

That distinction drives whole-class LP64 fixes instead of one-crash-at-a-time patches.

Recurring problem areas include:

- MIPS-era signedness and pointer-width assumptions;
- binary structure layout and ABI dependencies;
- segmented and ROM address translation;
- desktop OpenGL behavior unavailable in OpenGL ES;
- gameplay/runtime behavior that depended on N64-era invariants;
- generated sidecar and asset formats crossing host architectures.

The longer-term goal is to turn these lessons into reusable N64-to-modern-host portability tooling and documentation.

## Repository layout

```text
.github/workflows/
    build-r36s.yml              reference AArch64/GLES build + packaging proof
    discord-main-builds.yml     green-main build/changelog notification flow
    publish-main-release.yml    verified release publisher

work/goldeneye-pc-port/         source-port tree used for the ARM64 build
    tools_pc/
        arm64_semantic_audit.py semantic regression gate

port/
    GoldenEye 007.sh            PortMaster launcher
    port.json                   PortMaster metadata
    gameinfo.xml
    README.md                   installed-port notes

bundle/prepare-assets/          ROM-to-sidecar converter and required tables
package.py                      verified PortMaster ZIP assembler
watch/                          PortMaster exit-hotkey helper
docs/                           project/development documentation
PRESS.md                        media / creator briefing
SHOWCASE.md                     public showcase notes
```

## CI and release policy

A release-worthy main build must:

1. pass `arm64_semantic_audit.py`;
2. configure as AArch64;
3. compile `ge007.aarch64`;
4. verify the ELF machine type;
5. package `ge007.zip`;
6. pass ZIP integrity and PortMaster metadata checks;
7. prove no ROM or generated sidecars are present;
8. upload the verified build artifact;
9. have release publication re-check the payload before publishing.

This keeps the public release tied to a known green source revision.

## Contributing

Help is welcome, particularly with:

- real-device R36S testing;
- GLES rendering;
- ARM64/LP64 semantics;
- stage/setup/model correctness;
- AI, objectives, props and collision/navigation;
- long-session audio;
- PortMaster compatibility;
- documentation and reproducible bug reports.

See [CONTRIBUTING.md](CONTRIBUTING.md).

When reporting a device problem, include `ge007/log.txt`, the release/source revision, device/CFW, and the exact stage/action that triggered the problem.

For journalists, video creators and technical writers, see [PRESS.md](PRESS.md).

## Lineage and attribution

This project builds on the work of:

- [n64decomp/007](https://github.com/n64decomp/007) — GoldenEye 007 reconstruction/decompilation;
- [jkdansereau/goldeneye-pc-port](https://github.com/jkdansereau/goldeneye-pc-port) — PC/source-port foundation;
- [fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark) and related Fast3D lineage used by the renderer.

ARM64/R36S work and PortMaster packaging are maintained by **bitflipunix** and **Tomobobo710**, with contributions welcomed from the wider community.

Existing copyright and license notices in inherited and third-party code are preserved. See [NOTICE.md](NOTICE.md) and the license files within the source tree.

## Legal

No ROM is distributed by this project. No generated ROM-derived sidecar binaries are included in the PortMaster package.

GoldenEye 007 and associated names and trademarks belong to their respective rights holders. This is a **non-commercial fan preservation/porting effort** and is not affiliated with or endorsed by Nintendo, Rare, MGM, EON Productions, Danjaq, or other rights holders.

Any future commercial activity around this work is intended to concern original tooling, engineering services, educational material or creator content—not distribution or sale of GoldenEye game data.
