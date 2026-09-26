# R36S board-support provenance

The initial mainline-oriented R36S device-tree and generic DSI panel driver were imported from `icefirex/nixos-r36s`, commit `5d964492f2f5e830c8a6842094e71be58b6c9b26`.

The board definition has since been specialized for the actual test unit identified by the user's hardware probe as:

`G80CA-MB V1.2-20250422 Panel 8`

Hardware-specific facts for that clone are derived from the known-good dArkOSRE DTB set for the same exact board string. The reference Linux DTB blob is:

- repository: `southoz/dArkOSRE-R36`
- path: `files/BOOT/dtb/clone/G80CA-MB V1.2-20250422 Panel 8/rk3326-r36s-linux.dtb`
- Git blob SHA: `6897c5ff81d7947f3ad933cf8806e105ca9524b4`

We decode hardware facts from that DTB, then express them using Linux 6.12 interfaces rather than carrying the 4.4 vendor driver stack forward. Those facts include the Panel-8 KD35T133-compatible DSI sequence/timing, reset/power GPIOs, GPIO button map, ADC volume ladder and single-ADC stick mux wiring.

- `rk3326-r36s.dts`: SPDX `GPL-2.0+ OR MIT`; original copyright remains in the file.
- `panel-generic-dsi.c`: SPDX `GPL-2.0`; original ROCKNIX attribution remains in the file.

The CFW integration target is Linux 6.12.111.
