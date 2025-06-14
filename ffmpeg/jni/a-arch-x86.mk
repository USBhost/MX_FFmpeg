
# common flags

LOCAL_MODULE := $(NAME)

LOCAL_CFLAGS += \
-D_FILE_OFFSET_BITS=64 \
-DANDROID \
-march=$(ARCH) \
-mfpmath=sse \
-finline-functions


ifeq ($(findstring clang, $(NDK_TOOLCHAIN_VERSION)),)
	# GCC only.
	LOCAL_CFLAGS += \
	-mandroid \
	-fpredictive-commoning \
	-fipa-cp-clone \
	-funswitch-loops \
	-fgcse-after-reload \
	-ftree-vectorize \
	-ffunction-sections \
	-Wno-psabi
else
	# Clang only.
	LOCAL_CFLAGS += \
	-Wno-deprecated-register
endif


ifeq ($(ARCH),atom)
	LOCAL_CFLAGS += -msse3 -mssse3
else
	LOCAL_CFLAGS += -msse2 -mno-sse3 -mno-ssse3
endif


ifeq ($(APP_OPTIM),debug)
	LOCAL_CFLAGS += -DDEBUG
endif

LOCAL_CPPFLAGS += \
-Werror=return-type

# Since NDK r11, warning looks like related to `text relocation` prevents building ffmpeg_x86.
# @see http://stackoverflow.com/questions/19986523/shared-library-text-segment-is-not-shareable
#LOCAL_LDLIBS += -Wl,--no-warn-shared-textrel

