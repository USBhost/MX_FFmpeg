#!/bin/bash 
ROOT=$(cd "$(dirname "$0")"; pwd)
source ${ROOT}/util.sh

cd ${ROOT}/libsmb2

if test -z ${NDK}
then
    die "ndk not found.Please set NDK environment variable properly."
fi

HOST_PLATFORM=$(probe_host_platform)
FLAVOR=$1

if test -z ${FLAVOR}
then
    die "No flavor selected.Valid architecture:neon tegra2 tegra3 arm64 x86 x86_64"
fi

TOOLCHAIN=${NDK}/toolchains/llvm/prebuilt/${HOST_PLATFORM}

if [ ${FLAVOR} == 'neon' ] || [ ${FLAVOR} == 'tegra2' ] || [ ${FLAVOR} == 'tegra3' ]
then
    CROSS_PREFIX=${TOOLCHAIN}/bin/arm-linux-androideabi-
    CLANG_TARGET="armv7a-none-linux-androideabi21"
    HOST="arm-linux-android"

elif [ ${FLAVOR} == 'arm64' ] 
then
    CROSS_PREFIX=${TOOLCHAIN}/bin/aarch64-linux-android-
    TRIPLE=aarch64-linux-android
    CLANG_TARGET="aarch64-none-linux-android21" 
    HOST="aarch64-linux-android"

elif [ ${FLAVOR} == 'x86' ] 
then
    CROSS_PREFIX=${TOOLCHAIN}/bin/i686-linux-android-
    CLANG_TARGET="i686-none-linux-android21"
    HOST="x86-linux-android"

elif [ ${FLAVOR} == 'x86_64' ] 
then
    CROSS_PREFIX=${TOOLCHAIN}/bin/x86_64-linux-android-
    CLANG_TARGET="x86_64-none-linux-android21"
    HOST="x86_64-linux-android"
else
    die "Unsupported architecture."
fi

if [ ! -e ${CROSS_PREFIX}ar ]; then
  CROSS_PREFIX=${TOOLCHAIN}/bin/llvm-
  fi

export SYSROOT=$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/sysroot
export CC="$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/bin/clang -target $CLANG_TARGET"
export CXX="$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/bin/clang++ -target $CLANG_TARGET"
export RANLIB=${CROSS_PREFIX}ranlib
export AR=${CROSS_PREFIX}ar
export CPPFLAGS="-DMXTECHS"
export LDFLAGS="-Wl,-z,max-page-size=16384"

./bootstrap
./configure --enable-static=no      \
            --disable-werror        \
            --host=$HOST            \
            --without-libkrb5       \
            --with-pic              \
            --with-sysroot=$SYSROOT

if test "$?" != 0; then
    die "ERROR: failed to configure libsmb2.${FLAVOR}"
fi

echo "BUILDING libsmb2.${FLAVOR}"
make clean; make -j 4
if test "$?" != 0; then
    die "ERROR: failed to build libsmb2.${FLAVOR}"
fi

DST_DIR=$(get_dst_dir ${FLAVOR})
echo "COPYING openssl.${FLAVOR} to ${DST_DIR}"
if ! test -d  ${DST_DIR}
then
    mkdir -p ${ROOT}/${DST_DIR}
fi
cp lib/.libs/libsmb2.so ${ROOT}/${DST_DIR}