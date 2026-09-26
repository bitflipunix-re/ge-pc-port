# ARM-GE — GoldenEye 007, Native on ARM64 Linux

> **GoldenEye 007's reconstructed Nintendo 64 codebase running natively on ARM64 Linux handheld hardware — AArch64 + SDL2 + OpenGL ES, not N64 emulation.**

ARM-GE is an open-source engineering effort to make the reconstructed GoldenEye 007 Nintendo 64 codebase run as native software on low-power ARM64 Linux handhelds.

The current reference target is **R36S / dArkOSRE / PortMaster** using **AArch64 + SDL2 + OpenGL ES 3**.

## Current showcase

[![Watch the current ARM64/R36S showcase](https://img.youtube.com/vi/lcN9C9waB6I/hqdefault.jpg)](https://www.youtube.com/shorts/lcN9C9waB6I)

**Latest video state:** https://www.youtube.com/shorts/lcN9C9waB6I

This is the single current showcase clip for the project. It represents the latest public video state and supersedes older showcase footage.

[Full showcase notes →](SHOWCASE.md)

## See it running

- **Alpha testing / real-device reports:**  
  https://www.reddit.com/r/R36S/comments/1wos5ea/goldeneye_007_n64_arm64_call_for_alpha_testers/
- **Current alpha release links:**  
  https://www.reddit.com/r/u_Appropriate_Comb1486/comments/1woso74/goldeneye_007_n64_arm64_port_alpha_release_links/

The interesting part is not simply that GoldenEye runs on another device. The engineering problem is moving reconstructed software that still carries a 1990s console's ABI, address model, graphics assumptions and runtime invariants onto a modern 64-bit ARM Linux host.

## Current alpha

The latest verified public R36S PortMaster prerelease was produced on **26 September 2026** after consolidating the active ARM64 completion branches into `main`:

- source revision: `09ce89bfa07fc2e9de37e14e1726a7739e813477`
- build run: `36231558781`
- release tag: `r36s-alpha-2026-09-26-completion-pass`
- release: https://github.com/bitflipunix-re/ge-pc-port/releases/tag/r36s-alpha-2026-09-26-completion-pass
- build artifact digest: `sha256:9e4f7f2ff290937871c69e3b752339d177f2b7bb4e32fa402a3a7bd020faf4dd`
- installer: `ge007.zip` (4,611,208 bytes)
- native executable: `ge007.aarch64` (9,740,864 bytes)
- architecture: AArch64
- graphics: SDL2 + OpenGL ES
- ROM target: GoldenEye 007 NTSC-U, big-endian

This completion-pass build folds the currently verified ARM64 work into the mainline: host-width stage/setup rebasing, animation/model/title/language/background pointer fixes, teardown hardening, stage-boundary SFX cleanup, fresh-save safety, displacement-stable mouse aim, batched-tick crosshair stabilization, scripted-camera ownership, watch-menu setting persistence, and the ARM64 semantic regression gate.

The CI build passed the semantic audit, AArch64 cross-configuration and compile, ELF architecture verification, PortMaster package construction, `unzip -t`, package metadata checks, and the release workflow's second checksum/content verification. ROM files and generated ROM-derived sidecars are explicitly rejected from the published archive.

This is still an alpha: the completion pass is source/CI verified, while full-campaign, long-session audio, level-state, rendering and broader handheld behavior still require continued real-device testing.

## What works on real hardware

The port has demonstrated:

- native AArch64 execution on R36S-class hardware;
- SDL2/GLES rendering;
- boot, menus and intro;
- in-mission rendering and gameplay;
- controller integration;
- PortMaster install and launch;
- first-run generation of required ROM-derived sidecars from the user's own ROM;
- runtime logging and on-device diagnostics.

Active work is now concentrated on real-device correctness and polish rather than broad 32→64 conversion: full-campaign stage behavior, spawn/state transitions, AI/objectives/props, collision/navigation edge cases, GLES rendering defects, long-session audio behavior and broader handheld compatibility. Older Dam/PortMaster/Tom experiment branches are behind the consolidated mainline; the R36S CFW branch remains a separate firmware effort and is intentionally not merged into the game port.

## The portability work

Porting reconstructed N64 software to a modern host is not a mechanical 32-bit-to-64-bit conversion.

A recurring rule in this project is to classify values before changing them:

1. **native host pointer**
2. **N64 / ROM / segmented-address token**
3. **ordinary integer or game state**

That distinction drives whole-class fixes for LP64 correctness rather than one-crash-at-a-time patches.

Other recurring problem areas include:

- MIPS-era signedness and pointer-width assumptions;
- binary structure layout and ABI dependencies;
- segmented and ROM address translation;
- desktop OpenGL behavior that is unavailable in OpenGL ES;
- gameplay/runtime behavior that depended on N64-era invariants;
- generated sidecar and asset formats crossing host architectures.

The longer-term goal is to turn those lessons into reusable N64-to-modern-host portability tooling and documentation.

## Install model

This repository and its release package do **not** include a GoldenEye ROM or generated ROM-derived sidecars.

After installing the PortMaster package, provide your own legally obtained US NTSC big-endian ROM at:

`ge007/data/ge007.ntsc-final.z64`

Expected SHA-1:

`abe01e4aeb033b6c0836819f549c791b26cfde83`

The first launch generates the required host-format data locally and then starts the game.

## Repository layout

- `work/goldeneye-pc-port/` — source-port tree used for the ARM64 build
- `port/` — PortMaster launcher and package metadata
- `bundle/prepare-assets/` — local ROM-to-sidecar conversion tooling
- `package.py` — builds and validates the distributable PortMaster ZIP
- `watch/` — PortMaster exit-hotkey helper
- `.github/workflows/build-r36s.yml` — reference AArch64/GLES build and packaging proof
- `PRESS.md` — concise media / creator briefing and verified public links

## Building

The reference build is the GitHub Actions workflow in `.github/workflows/build-r36s.yml`. Every ARM64 build first runs `work/goldeneye-pc-port/tools_pc/arm64_semantic_audit.py` to catch known pointer-width and token/pointer regression classes before compilation.

For local package assembly after producing `ge007.aarch64`:

```sh
python3 package.py --game-bin path/to/ge007.aarch64 --out dist
```

The packager refuses to include `.z64`, `.n64`, `.v64`, `pcmodels.bin` or `pccg.bin` files.

## Contributing

Help is welcome, particularly with real-device testing, GLES rendering, ARM64/LP64 semantics, gameplay correctness, PortMaster compatibility and documentation.

See [CONTRIBUTING.md](CONTRIBUTING.md).

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
