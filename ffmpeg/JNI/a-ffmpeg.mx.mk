MY_DIR := $(call my-dir)
LOCAL_PATH := $(MY_DIR)/ffmpeg

# avutil
include $(CLEAR_VARS)
LOCAL_MODULE    := avutil
LOCAL_SRC_FILES := libavutil/libavutil.a
include $(PREBUILT_STATIC_LIBRARY)

# avcodec
include $(CLEAR_VARS)
LOCAL_MODULE    := avcodec
LOCAL_SRC_FILES := libavcodec/libavcodec.a
include $(PREBUILT_STATIC_LIBRARY)

# avformat
include $(CLEAR_VARS)
LOCAL_MODULE    := avformat
LOCAL_SRC_FILES := libavformat/libavformat.a
include $(PREBUILT_STATIC_LIBRARY)

# swscale
include $(CLEAR_VARS)
LOCAL_MODULE    := swscale
LOCAL_SRC_FILES := libswscale/libswscale.a
include $(PREBUILT_STATIC_LIBRARY)

# swresample
include $(CLEAR_VARS)
LOCAL_MODULE    := swresample
LOCAL_SRC_FILES := libswresample/libswresample.a
include $(PREBUILT_STATIC_LIBRARY)

# Merge all static libraries.
include $(CLEAR_VARS)

LIBS := $(MY_DIR)/libs

ifeq ($(TARGET_ARCH),arm)
	LIB_OPENSSL := $(LIBS)/android/16-$(TARGET_ARCH)
endif

ifeq ($(TARGET_ARCH),x86)
	LIB_OPENSSL := $(LIBS)/android/16-$(TARGET_ARCH)
endif

ifeq ($(TARGET_ARCH),mips)
	LIB_OPENSSL := $(LIBS)/android/15-$(TARGET_ARCH)
endif

LOCAL_LDLIBS := \
-L$(NDK_APP_DST_DIR) \
-L$(LIB_OPENSSL) \
-lmxutil \
-lssl \
-lcrypto \
-lz

include $(MY_DIR)/a-arch-$(TARGET_ARCH).mk

LOCAL_WHOLE_STATIC_LIBRARIES := swresample swscale avformat avcodec avutil

include $(BUILD_SHARED_LIBRARY)

