#
MY_DIR := $(call my-dir)

#
# Fribidi
#

include $(CLEAR_VARS)
LOCAL_PATH := $(MY_DIR)
SRC_PATH := $(LOCAL_PATH)/fribidi-0.19.7/lib

LOCAL_C_INCLUDES := \
$(LOCAL_PATH)/fribidi-0.19.7 \
$(LOCAL_PATH)/fribidi-0.19.7/lib

LOCAL_SRC_FILES := \
$(SRC_PATH)/fribidi.c \
$(SRC_PATH)/fribidi-arabic.c \
$(SRC_PATH)/fribidi-bidi.c \
$(SRC_PATH)/fribidi-bidi-types.c \
$(SRC_PATH)/fribidi-deprecated.c \
$(SRC_PATH)/fribidi-joining.c \
$(SRC_PATH)/fribidi-joining-types.c \
$(SRC_PATH)/fribidi-mem.c \
$(SRC_PATH)/fribidi-mirroring.c \
$(SRC_PATH)/fribidi-run.c \
$(SRC_PATH)/fribidi-shape.c

LOCAL_CFLAGS := \
-DHAVE_CONFIG_H \
-DPIC

# Clang only.
ifneq ($(findstring clang, $(NDK_TOOLCHAIN_VERSION)),)
	LOCAL_CFLAGS += \
	-Wno-switch \
	-Wno-enum-conversion
endif



include $(LOCAL_PATH)/a-arch-$(TARGET_ARCH).mk

LOCAL_ASFLAGS := $(LOCAL_CFLAGS)

LOCAL_MODULE := fribidi
include $(BUILD_SHARED_LIBRARY)



