
ifeq ($(APP_ABI),x86)
	NDK_TOOLCHAIN_VERSION := 4.9
else
	NDK_TOOLCHAIN_VERSION := clang
endif

APP_STL := c++_shared

APP_OPTIM := release


