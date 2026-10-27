include $(sort $(wildcard $(BR2_EXTERNAL_OSCILL_PATH)/package/*/*.mk))
# Direct framebuffer/DRM backends use SDL events, without GBM/EGL.
SDL2_CONF_OPTS += --enable-video-dummy

# Keep the receiver and its shared ABI in the device sources, not in a local
# kernel checkout. Refresh them on every kernel build, including incremental.
define OSCILL_INSTALL_CAPTURE_DRIVER
	$(INSTALL) -m 0644 $(BR2_EXTERNAL_OSCILL_PATH)/driver/oscill_capture.c $(LINUX_DIR)/drivers/misc/oscill_capture.c
	$(INSTALL) -m 0644 $(BR2_EXTERNAL_OSCILL_PATH)/driver/oscill_capture_internal.h $(LINUX_DIR)/drivers/misc/oscill_capture_internal.h
	$(INSTALL) -m 0644 $(BR2_EXTERNAL_OSCILL_PATH)/driver/oscill_analysis.c $(LINUX_DIR)/drivers/misc/oscill_analysis.c
	$(INSTALL) -m 0644 $(BR2_EXTERNAL_OSCILL_PATH)/../gui/src/analysis_uapi.h $(LINUX_DIR)/include/uapi/linux/oscill_analysis.h
	$(INSTALL) -m 0644 $(BR2_EXTERNAL_OSCILL_PATH)/../gui/src/capture_uapi.h $(LINUX_DIR)/include/uapi/linux/oscill_capture.h
	$(INSTALL) -m 0644 $(BR2_EXTERNAL_OSCILL_PATH)/../gui/src/capture_rates.h $(LINUX_DIR)/include/uapi/linux/capture_rates.h
	grep -q '^obj-y += oscill_capture.o$$' $(LINUX_DIR)/drivers/misc/Makefile || echo 'obj-y += oscill_capture.o' >> $(LINUX_DIR)/drivers/misc/Makefile
	grep -q '^obj-y += oscill_analysis.o$$' $(LINUX_DIR)/drivers/misc/Makefile || echo 'obj-y += oscill_analysis.o' >> $(LINUX_DIR)/drivers/misc/Makefile
endef
LINUX_PRE_BUILD_HOOKS += OSCILL_INSTALL_CAPTURE_DRIVER
