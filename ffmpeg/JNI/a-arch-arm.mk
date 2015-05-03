
# common flags

LOCAL_ARM_MODE := arm
LOCAL_MODULE := $(NAME)

LOCAL_CFLAGS += \
-D_FILE_OFFSET_BITS=64 \
-DANDROID \
-march=$(ARCH) \
-mandroid \
-finline-functions \
-fpredictive-commoning \
-fgcse-after-reload \
-ftree-vectorize \
-fipa-cp-clone \
-Wno-psabi

ifeq ($(APP_OPTIM),debug)
LOCAL_CFLAGS += -DDEBUG
endif

LOCAL_CPPFLAGS += -Werror=return-type

LOCAL_LDFLAGS += -march=$(ARCH)

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
	LOCAL_CFLAGS += -mvectorize-with-neon-quad -mtune=cortex-a8 -D__NEON__
else
	LOCAL_ARM_NEON := false
	LOCAL_CFLAGS += 
endif

ifeq ($(UNALIGNED_ACCESS),0)
	LOCAL_CFLAGS += -mno-unaligned-access
endif
