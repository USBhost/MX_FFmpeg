LOCAL_PATH := $(call my-dir)
LAME_ROOT=${LOCAL_PATH}/lame-3.100/libmp3lame
include $(CLEAR_VARS)

LOCAL_MODULE := mp3lame

LOCAL_CFLAGS += -Dieee754_float32_t=float -DSTDC_HEADERS

LOCAL_C_INCLUDES := \
$(LAME_ROOT) \
${LOCAL_PATH}/lame-3.100/include


LOCAL_SRC_FILES := \
${LAME_ROOT}/vector/xmm_quantize_sub.c \
${LAME_ROOT}/bitstream.c \
${LAME_ROOT}/encoder.c \
${LAME_ROOT}/fft.c \
${LAME_ROOT}/gain_analysis.c \
${LAME_ROOT}/id3tag.c \
${LAME_ROOT}/lame.c \
${LAME_ROOT}/mpglib_interface.c \
${LAME_ROOT}/newmdct.c \
${LAME_ROOT}/presets.c \
${LAME_ROOT}/psymodel.c \
${LAME_ROOT}/quantize.c \
${LAME_ROOT}/quantize_pvt.c \
${LAME_ROOT}/reservoir.c \
${LAME_ROOT}/set_get.c \
${LAME_ROOT}/tables.c \
${LAME_ROOT}/takehiro.c \
${LAME_ROOT}/util.c \
${LAME_ROOT}/vbrquantize.c \
${LAME_ROOT}/VbrTag.c \
${LAME_ROOT}/version.c

#LOCAL_LDLIBS :=
LOCAL_LDFLAGS := -Wl,--version-script=$(LOCAL_PATH)/version_scripts/lame


include $(BUILD_SHARED_LIBRARY)