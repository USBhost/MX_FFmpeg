
# common flags

LOCAL_MODULE := $(NAME)

LOCAL_CFLAGS += \
-D_FILE_OFFSET_BITS=64 \
-DANDROID \
-march=$(ARCH) \
-mfpmath=sse \
-mandroid \
-finline-functions \
-funswitch-loops \
-fpredictive-commoning \
-fgcse-after-reload \
-ftree-vectorize \
-fipa-cp-clone \
-ffunction-sections \
-Wno-psabi

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

