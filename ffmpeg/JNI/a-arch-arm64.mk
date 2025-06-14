
# common flags

LOCAL_ARM_MODE := arm
LOCAL_MODULE := $(NAME)

# @note Even if you give -O3, it seems to override -O2 in ndk-build. Set the flag corresponding to -O3 directly.
# @note mxvp(or C ++ project) will crash if you give -finline-functions, so ffmpeg will be set separately in the project
# @note --coverage seems not working on 4.4.3
# @note -Wl,--relax	<-- This option feels rather slow.
# @note mxvp(c++) is called during the profile.

# However, since NDK gives -O2, there is a risk, so do not modify it.

LOCAL_CFLAGS += \
-D_FILE_OFFSET_BITS=64 \
-DANDROID \
-march=$(ARCH) \
-finline-functions \
-fgcse-after-reload \
-ftree-vectorize

ifeq ($(findstring clang, $(NDK_TOOLCHAIN_VERSION)),)
	# GCC only.
	LOCAL_CFLAGS += \
	-mandroid \
	-fpredictive-commoning \
	-fipa-cp-clone
else
	# Clang only.
	LOCAL_CFLAGS += \
	-Wno-deprecated-register
endif


ifeq ($(APP_OPTIM),debug)
	LOCAL_CFLAGS += -DDEBUG
endif

LOCAL_CPPFLAGS += -Werror=return-type
LOCAL_LDFLAGS += -march=$(ARCH)

#ifeq ($(PROFILE),1)
#	LOCAL_CFLAGS += -fprofile-generate=/mnt/sdcard/profile
#	LOCAL_LDFLAGS += -fprofile-generate=/mnt/sdcard/profile
#else
#	LOCAL_CFLAGS += -fprofile-use
#endif

# architecture specific flags.

TARGET_ARCH_ABI := arm64-v8a
LOCAL_CFLAGS += -D__TARGET_ARCH_ARM=8 -D__AARCH64__
#LOCAL_LDFLAGS += -Wl,--fix-cortex-a53
#LOCAL_CFLAGS += -mfpu=$(VFP) -mfloat-abi=softfp

ifeq ($(VFP),neon)
	LOCAL_ARM_NEON := true
	LOCAL_CFLAGS += -D__NEON__
	LOCAL_CFLAGS += -mtune=generic -D__NEON__
	# GCC only.
	ifeq ($(findstring clang, $(NDK_TOOLCHAIN_VERSION)),)
		LOCAL_CFLAGS += -mvectorize-with-neon-quad
	endif
else
	LOCAL_ARM_NEON := false
endif
