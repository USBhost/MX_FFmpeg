
# common flags

LOCAL_ARM_MODE := arm
LOCAL_MODULE := $(NAME)

# @note -O3를 주더라도 ndk-build에서 -O2로 override하는 듯 싶다. -O3에 해당하는 flag를 직접 설정.
# @note mxvp (혹은 C++ project)는 -finline-functions을 주면 다운되므로 ffmpeg는 프로젝트에서 별도 설정
# @note --coverage seems not working on 4.4.3
# @note -Wl,--relax			<-- 이 옵션은 오히려 느려지는 느낌이다.
# @note mxvp(c++)는 profile시 hang된다.

# 하지만 NDK에서 -O2를 주므로, 위험성이 있으므로 수정하지 않는다.

LOCAL_CFLAGS += \
-D_FILE_OFFSET_BITS=64 \
-DANDROID \
-march=$(ARCH) \
-fgcse-after-reload \
-ftree-vectorize

# Tegra3
ifeq ($(UNALIGNED_ACCESS),0)
	LOCAL_CFLAGS += -mno-unaligned-access

	# Since NDK r11, warning looks like related to `text relocation` prevents building ffmpeg_x86.
	# @see http://stackoverflow.com/questions/19986523/shared-library-text-segment-is-not-shareable
	ifeq ($(OLD_FFMPEG),1)
		LOCAL_LDLIBS += -Wl,--no-warn-shared-textrel
	endif

#	LOCAL_CFLAGS += -Wcast-align
endif

# LOCAL_CFLAGS += -v

ifeq ($(findstring clang, $(NDK_TOOLCHAIN_VERSION)),)
	# GCC only.
	LOCAL_CFLAGS += \
	-mandroid \
	-fpredictive-commoning \
	-fipa-cp-clone \
    -finline-functions
else
	# Clang only.
	LOCAL_CFLAGS += \
	-Wno-deprecated-register \

    ifeq ($(APP_OPTIM),debug)
        LOCAL_CFLAGS += -fno-inline-functions
        LOCAL_CFLAGS += -fstandalone-debug 
    else
        LOCAL_CFLAGS += -finline-functions
    endif

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

ifeq ($(ARCH),armv7-a)
 	TARGET_ARCH_ABI := armeabi-v7a
	LOCAL_CFLAGS += -D__TARGET_ARCH_ARM=7
	LOCAL_LDFLAGS += -Wl,--fix-cortex-a8
else ifeq ($(ARCH),armv6)
	LOCAL_CFLAGS += -D__TARGET_ARCH_ARM=6
else
	LOCAL_CFLAGS += -mtune=xscale -D__TARGET_ARCH_ARM=5
endif


ifdef VFP
	LOCAL_CFLAGS += -mfpu=$(VFP) -mfloat-abi=softfp 
else
	LOCAL_CFLAGS += -msoft-float
endif


ifeq ($(VFP),neon)
	LOCAL_ARM_NEON := true
	LOCAL_CFLAGS += -mtune=cortex-a8 -D__NEON__

	# GCC only.
	ifeq ($(findstring clang, $(NDK_TOOLCHAIN_VERSION)),)
		LOCAL_CFLAGS += -mvectorize-with-neon-quad
	endif
else
	LOCAL_ARM_NEON := false
endif


