# First-device test bundle

The bring-up bundle is intentionally separate from a full disk image.

During the first hardware validation we retain the R36S bootloader that is already proven on the user's hardware. This isolates failures to the pieces we own:

- Linux 6.12 kernel;
- R36S device tree and panel path;
- Buildroot userspace;
- DRM/KMS;
- Panfrost;
- Mesa.

A full blank-card image will only be produced after this payload boots reliably. At that point the bootloader can be brought under the CFW build as its own tested milestone rather than changing every layer at once.

Use:

```sh
sh cfw/scripts/make-test-bundle.sh \
  r36s-cfw-kernel-6.12.111.zip \
  rootfs-r36s-mesa-26.2.3.ext4
```

The resulting ZIP contains normalized BOOT files, the ROOTFS image and SHA-256 checksums.
