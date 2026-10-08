include $(sort $(wildcard $(BR2_EXTERNAL_OSCILL_PATH)/package/*/*.mk))
# Direct framebuffer/DRM backends use SDL events, without GBM/EGL.
SDL2_CONF_OPTS += --enable-video-dummy
