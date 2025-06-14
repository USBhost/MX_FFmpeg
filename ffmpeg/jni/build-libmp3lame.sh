#!/bin/bash

FLAVOR=$1

if test -z ${FLAVOR}
then
    echo "flavor in [neon arm64 x84 x86_64] using default arm64"
    FLAVOR=arm64
fi


if [ ${FLAVOR} == 'arm' ] || [ ${FLAVOR} == 'neon' ] || [ ${FLAVOR} == 'tegra2' ] || [ ${FLAVOR} == 'tegra3' ]
then
    #TARGET=android-armv7
    TARGET_ABI=armeabi-v7a
    TARGET_ARCH=armv7-a
    TARGET_PATH=armeabi-v7a/neon
elif [ ${FLAVOR} == 'arm64' ]
then
#    CROSS_PREFIX=${TOOLCHAIN}/bin/aarch64-linux-android-
#    TARGET=android64-aarch64
#    TRIPLE=aarch64-linux-android
#    CLANG_TARGET="aarch64-none-linux-android21"
    TARGET_ABI=arm64-v8a
    TARGET_ARCH=armv8-a
    TARGET_PATH=arm64-v8a
elif [ ${FLAVOR} == 'x86' ]
then
#    CROSS_PREFIX=${TOOLCHAIN}/bin/i686-linux-android-
#    TARGET=android-x86
#    CLANG_TARGET="i686-none-linux-android17"
    TARGET_ABI=x86
    TARGET_ARCH=atom
    TARGET_PATH=x86
elif [ ${FLAVOR} == 'x86_64' ]
then
    TARGET_ABI=x86_64
    TARGET_ARCH=atom
    TARGET_PATH=x86_64
#    CROSS_PREFIX=${TOOLCHAIN}/bin/x86_64-linux-android-
#    TARGET=android
#    CLANG_TARGET="x86_64-none-linux-android21"
else
    die "Unsupported architecture."
fi

/bin/rm -f ../obj/local/${TARGET_ABI}/libmp3lame.so

${NDK}/ndk-build NDK_DEBUG=0 \
				  -e APP_ABI=$TARGET_ABI \
				  -e APP_BUILD_SCRIPT=libmp3lame.mk \
				  -e ARCH=$TARGET_ARCH \
				  -e NDK_APP_DST_DIR=lame-build

mkdir -p ./libs/${TARGET_PATH}/
cp -f ../obj/local/${TARGET_ABI}/libmp3lame.so ./libs/${TARGET_PATH}/
cp -f ../obj/local/${TARGET_ABI}/libmp3lame.so ./lame-3.100/