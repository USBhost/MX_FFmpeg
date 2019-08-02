#!/bin/bash

. ENV # Environment

ROOT=$(cd "$(dirname "$0")"; pwd)
source ${ROOT}/util.sh

cd ${ROOT}/openssl-1.0.2s

if test -z ${NDK}
then
    die "ndk not found.Please set NDK environment variable properly."
fi

HOST_PLATFORM=$(probe_host_platform)
GCC_VERSION=4.9
FLAVOR=$1

if test -z ${FLAVOR}
then
    die "No flavor selected.Valid architecture:arm arm64 x86 x86_64"
fi

if [ ${FLAVOR} == 'neon' ] || [ ${FLAVOR} == 'tegra3' ] || [ ${FLAVOR} == 'tegra2' ]
then
    TOOLCHAIN=${NDK}/toolchains/arm-linux-androideabi-${GCC_VERSION}/prebuilt/${HOST_PLATFORM}
    CROSS_PREFIX=${TOOLCHAIN}/bin/arm-linux-androideabi-
    APP_PLATFORM=android-19
    TARGET_OS='android-armv7'
    ARCH=arm

elif [ ${FLAVOR} == 'arm64' ] 
then
    TOOLCHAIN=${NDK}/toolchains/aarch64-linux-android-${GCC_VERSION}/prebuilt/${HOST_PLATFORM}
    CROSS_PREFIX=${TOOLCHAIN}/bin/aarch64-linux-android-
    APP_PLATFORM=android-21
    TARGET_OS=android
    ARCH=arm64

elif [ ${FLAVOR} == 'x86' ] 
then
    TOOLCHAIN=${NDK}/toolchains/x86-${GCC_VERSION}/prebuilt/${HOST_PLATFORM}
    CROSS_PREFIX=${TOOLCHAIN}/bin/i686-linux-android-
    APP_PLATFORM=android-19
    TARGET_OS=android-x86
    ARCH=x86

elif [ ${FLAVOR} == 'x86_64' ] 
then
    TOOLCHAIN=${NDK}/toolchains/x86_64-${GCC_VERSION}/prebuilt/${HOST_PLATFORM}
    CROSS_PREFIX=${TOOLCHAIN}/bin/x86_64-linux-android-
    APP_PLATFORM=android-21
    TARGET_OS=android
    ARCH=x86_64
    #OPTIONS=no-asm

else
    die "Unsupported architecture."
fi

export ANDROID_DEV=${NDK}/platforms/${APP_PLATFORM}/arch-${ARCH}/usr
export CC=${CROSS_PREFIX}gcc
export CXX=${CROSS_PREFIX}g++
export RANLIB=${CROSS_PREFIX}ranlib
export AR=${CROSS_PREFIX}ar

./Configure ${OPTIONS} ${TARGET_OS}
if test "$?" != 0; then
    die "ERROR: failed to configure openssl.${FLAVOR}"
fi

echo "BUILDING openssl.${FLAVOR}"
make clean; make build_libs -j 4
if test "$?" != 0; then
    die "ERROR: failed to build openssl.${FLAVOR}"
fi
<<!
DST_DIR=$(get_dst_dir ${FLAVOR})
echo "COPYING openssl.${FLAVOR} to ${DST_DIR}"
if ! test -d  ${DST_DIR}
then
    mkdir -p ${ROOT}/${DST_DIR}
fi
cp libssl.a ${ROOT}/${DST_DIR}
cp libcrypto.a ${ROOT}/${DST_DIR}
!
