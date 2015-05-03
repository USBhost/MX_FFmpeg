
# common flags

LOCAL_MODULE := $(NAME)

LOCAL_CFLAGS += \
-D_FILE_OFFSET_BITS=64 \
-DANDROID \
-march=$(ARCH) \
-mandroid \
-finline-functions \
-funswitch-loops \
-fpredictive-commoning \
-fgcse-after-reload \
-ftree-vectorize \
-fipa-cp-clone \
-ffunction-sections \
-Wno-psabi

ifeq ($(APP_OPTIM),debug)
LOCAL_CFLAGS += -DDEBUG
endif

LOCAL_CPPFLAGS += \
-Werror=return-type

