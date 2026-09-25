################################################################################
#
# r36s-gpu-probe
#
################################################################################

R36S_GPU_PROBE_VERSION = 1
R36S_GPU_PROBE_SITE = $(BR2_EXTERNAL_R36S_CFW_PATH)/package/r36s-gpu-probe/src
R36S_GPU_PROBE_SITE_METHOD = local
R36S_GPU_PROBE_DEPENDENCIES = libdrm libgbm libegl libgles

define R36S_GPU_PROBE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) 		-I$(STAGING_DIR)/usr/include/libdrm 		$(@D)/r36s-gpu-probe.c 		-o $(@D)/r36s-gpu-probe 		$(TARGET_LDFLAGS) -ldrm -lgbm -lEGL -lGLESv2
endef

define R36S_GPU_PROBE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/r36s-gpu-probe 		$(TARGET_DIR)/usr/bin/r36s-gpu-probe
endef

$(eval $(generic-package))
