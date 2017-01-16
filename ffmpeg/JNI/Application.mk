
ifeq ($(APP_ABI),x86)
	NDK_TOOLCHAIN_VERSION := 4.9
else
	NDK_TOOLCHAIN_VERSION := clang
endif


APP_STL := gnustl_static

APP_OPTIM := release


