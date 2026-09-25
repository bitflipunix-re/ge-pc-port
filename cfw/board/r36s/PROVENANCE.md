# R36S board-support provenance

The initial hardware description and generic DSI panel driver in this directory are imported as bring-up references from `icefirex/nixos-r36s`, commit `5d964492f2f5e830c8a6842094e71be58b6c9b26`.

- `rk3326-r36s.dts`: SPDX `GPL-2.0+ OR MIT`; original copyright remains in the file.
- `panel-generic-dsi.c`: SPDX `GPL-2.0`; original ROCKNIX attribution remains in the file.

They are vendored here so the CFW build does not depend on a moving external branch. Our integration targets Linux 6.12.111.
