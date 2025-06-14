# APP_ABI := armeabi mips

#
# 4.6은 Galaxy Victory에서 SysAudioDecoder:decode함수의 optmize에 문제가 있어 보여 동작하지 않는다. 4.8로 대체.
#
# 단, 4.8에서 2.1, 2.2에서 Debug build library는 load되지 않는다.
#

#NDK_TOOLCHAIN_VERSION := 4.9


# Clang으로 mxvp등을 build하면 x86에서는 즉시 crash(SIGSEGV)되어 x86 build에서는 GCC로 간다. (NDK r11c, r12b) 
# (rtti를 삭제해도 마찬가지이다.)
# 단, ffmpeg은 Clang으로 build하여도 이상 없음.

#
# NDKr12b의 Clang에서 Assembly code가 삭제되는 현상이 있어 사용하지 않는다.
#
# YV12_10bits_Narrower 에서 발생했다. Inline을 사용하지 않아도 동일하며 line looping시 logging 함수를 호출하면 정상치리된다.
# Release build에서만 발생.
#

## ifeq ($(APP_ABI),x86)
#	NDK_TOOLCHAIN_VERSION := 4.9
##else
##	NDK_TOOLCHAIN_VERSION := clang
##endif

NDK_TOOLCHAIN_VERSION := clang

APP_SUPPORT_FLEXIBLE_PAGE_SIZES := true

#APP_STL := gnustl_static
#APP_STL := c++_static
APP_STL := c++_shared

ifeq ($(MX_DEBUG),1)
APP_OPTIM := debug
else
APP_OPTIM := release
endif
APP_STRIP_MODE := none

