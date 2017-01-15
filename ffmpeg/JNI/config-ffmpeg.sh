#!/bin/bash

HOST_PLATFORM=linux-x86_64

GCC_VER=4.9
CLANG_VER=3.8

case $1 in
	arm64)
		;;

	neon)
		ARCH=arm
		CPU=armv7-a
		LIB_MX="../libs/armeabi-v7a/neon"
		EXTRA_CFLAGS="-mfloat-abi=softfp -mfpu=neon -mtune=cortex-a8"
		EXTRA_LDFLAGS="-Wl,--fix-cortex-a8"

		if [ -z "${CLANG_VER}" ]
		then
			# GCC only.
			EXTRA_CFLAGS+=" -mvectorize-with-neon-quad"
		fi

		;;

	tegra3)
		ARCH=arm
		CPU=armv7-a
		LIB_MX="../libs/armeabi-v7a/neon"
		EXTRA_CFLAGS="-mfloat-abi=softfp -mfpu=neon -mtune=cortex-a8 -mno-unaligned-access"
		EXTRA_LDFLAGS="-Wl,--fix-cortex-a8"
		EXTRA_PARAMETERS="--disable-fast-unaligned"

		if [ -z "${CLANG_VER}" ]
		then
			# GCC only.
			EXTRA_CFLAGS+=" -mvectorize-with-neon-quad"
		fi

		;;

	tegra2)
		ARCH=arm
		CPU=armv7-a
		LIB_MX="../libs/armeabi-v7a/vfpv3-d16"
		EXTRA_CFLAGS="-mfloat-abi=softfp -mfpu=vfpv3-d16"
		EXTRA_LDFLAGS="-Wl,--fix-cortex-a8"
		EXTRA_PARAMETERS="--disable-neon"
		;;

#	v6_vfp)
#		ARCH=arm
#		CPU=armv6
#		LIB_MX="../libs/armeabi-v6/vfp"
#		EXTRA_CFLAGS="-mfloat-abi=softfp -mfpu=vfp"
#		EXTRA_PARAMETERS="--disable-neon"
#		;;

#	v6)
#		ARCH=arm
#		CPU=armv6
#		LIB_MX="../libs/armeabi-v6"
#		EXTRA_CFLAGS="-msoft-float"
#		EXTRA_PARAMETERS="--disable-neon --disable-vfp"
#		;;

#	v5te)
#		ARCH=arm
#		CPU=armv5te
#		LIB_MX="../libs/armeabi-v5"
#		EXTRA_CFLAGS="-msoft-float -mtune=xscale"
#		EXTRA_PARAMETERS="--disable-neon --disable-armv6"
#		;;

#	x86_64)
#		ARCH=x86_64
#		LIB_MX="../libs/x86_64"
#		EXTRA_CFLAGS="-mtune=atom -msse3 -mssse3 -mfpmath=sse"
#		;;

	x86)
		ARCH=x86
		CPU=atom
		LIB_MX="../libs/x86"
		EXTRA_CFLAGS="-mtune=atom -msse3 -mssse3 -mfpmath=sse"
		;;

#	x86_sse2)
#		ARCH=x86
#		CPU=i686
#		LIB_MX="../libs/x86-sse2"
#		EXTRA_CFLAGS="-msse2 -mno-sse3 -mno-ssse3 -mfpmath=sse"
#		EXTRA_PARAMETERS="--disable-sse3"
#		;;

#	mips)
#		ARCH=mips
#		CPU=mips32r2
#		LIB_MX="../libs/mips"
#		EXTRA_CFLAGS=
#		EXTRA_PARAMETERS="--disable-mipsfpu --disable-mipsdspr1 --disable-mipsdspr2"
#		;;

	*)
		echo Unknown target: $1
		exit
esac

INC_OPENSSL=../openssl-1.0.1g/include
INC_OPUS=../opus-1.1/include
INC_SPEEX=../speex-1.2rc1/include
INC_ZVBI=../zvbi-0.2.35/src
INC_ICONV=../modified_src/iconv
INC_MODPLUG=../libmodplug/src


if [ $ARCH == 'arm' ] 
then
	TOOLCHAIN=$NDK/toolchains/arm-linux-androideabi-$GCC_VER/prebuilt/$HOST_PLATFORM
	CROSS_PREFIX=$TOOLCHAIN/bin/arm-linux-androideabi-

	EXTRA_CFLAGS+=" -fstack-protector -fstrict-aliasing"
	EXTRA_LDFLAGS+=" -march=$CPU"

	if [ -n "${CLANG_VER}" ]
	then
		CLANG_TARGET=armv7-none-linux-androideabi
	fi

	OPTFLAGS="-O2"
	APP_PLATFORM=android-19
	LINK_AGAINST=16-arm
#elif [ $ARCH == 'x86_64' ] 
#then
#	CROSS_PREFIX=$NDK/toolchains/x86_64-$GCC_VER/prebuilt/$HOST_PLATFORM/bin/x86_64-linux-android-
#	EXTRA_CFLAGS+=" -fstrict-aliasing"
#	OPTFLAGS="-O2 -fpic"
#	APP_PLATFORM=android-21
#	LINK_AGAINST=21-x86_64
elif [ $ARCH == 'x86' ] 
then
	TOOLCHAIN=$NDK/toolchains/x86-$GCC_VER/prebuilt/$HOST_PLATFORM
	CROSS_PREFIX=$TOOLCHAIN/bin/i686-linux-android-

	EXTRA_CFLAGS+=" -fstrict-aliasing"

	if [ -n "${CLANG_VER}" ]
	then
		CLANG_TARGET=i686-none-linux-android
		EXTRA_CFLAGS+=" -fstack-protector-strong "
	fi

	OPTFLAGS="-O2 -fno-pic"
	APP_PLATFORM=android-19
	LINK_AGAINST=16-x86
#elif [ $ARCH == 'mips' ] 
#then
#	CROSS_PREFIX=$NDK/toolchains/mipsel-linux-android-$GCC_VER/prebuilt/$HOST_PLATFORM/bin/mipsel-linux-android-
#	EXTRA_CFLAGS+=" -fno-strict-aliasing -fmessage-length=0 -fno-inline-functions-called-once -frerun-cse-after-loop -frename-registers"
#	OPTFLAGS="-O2"
#	APP_PLATFORM=android-19
#	# Due to reference to missing symbol __fixdfdi on Novo 7 Paladin.
#	LINK_AGAINST=15-mips
fi


if [ -n "${CLANG_VER}" ]
then
	# Clang only.
	CC="$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/bin/clang -target $CLANG_TARGET -gcc-toolchain $TOOLCHAIN"
	CXX="$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/bin/clang++ -target $CLANG_TARGET -gcc-toolchain $TOOLCHAIN"
	LD=${CROSS_PREFIX}gcc
	AS=${CROSS_PREFIX}gcc

	EXTRA_CFLAGS+=" -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-function"
else
	# GCC only.
	EXTRA_CFLAGS+=" -mandroid -fpredictive-commoning -fipa-cp-clone"

	# XXX Temporarily removed from Clang build until options are supported.
	EXTRA_CFLAGS+=" -funswitch-loops -finline-limit=300 -finline-functions -fgcse-after-reload"
fi


./configure \
--enable-static \
--disable-shared \
--enable-pic \
--enable-optimizations \
--enable-pthreads \
--disable-debug \
--disable-doc \
--disable-programs \
--disable-symver \
--disable-avdevice \
--disable-postproc \
--enable-avfilter \
--disable-swscale-alpha \
--disable-encoders \
--disable-muxers \
--disable-devices \
--disable-filters \
--enable-filter=yadif \
--enable-filter=w3fdif \
--disable-protocol=bluray \
--disable-protocol=crypto \
--disable-protocol=data \
--disable-protocol=gopher \
--disable-protocol=md5 \
--disable-protocol=pipe \
--disable-protocol=udplite \
--disable-protocol=unix \
--disable-demuxer=srt \
--disable-demuxer=microdvd \
--disable-demuxer=jacosub \
--disable-demuxer=sami \
--disable-demuxer=realtext \
--disable-demuxer=stl \
--disable-demuxer=subviewer \
--disable-demuxer=subviewer1 \
--disable-demuxer=pjs \
--disable-demuxer=vplayer \
--disable-demuxer=mpl2 \
--disable-demuxer=webvtt \
--disable-decoder=text \
--disable-decoder=srt \
--disable-decoder=subrip \
--disable-decoder=microdvd \
--disable-decoder=jacosub \
--disable-decoder=sami \
--disable-decoder=realtext \
--disable-decoder=stl \
--disable-decoder=subviewer \
--disable-decoder=subviewer1 \
--disable-decoder=pjs \
--disable-decoder=vplayer \
--disable-decoder=mpl2 \
--disable-decoder=webvtt \
--enable-openssl \
--enable-zlib \
--enable-libopus \
--enable-libspeex \
--enable-libzvbi \
--enable-libmodplug \
--arch=$ARCH \
--cpu=$CPU \
--cross-prefix=$CROSS_PREFIX \
--cc="$CC" \
--cxx="$CXX" \
--ld=$LD \
--nm=$NM \
--ar=$AR \
--as=$AS \
--enable-cross-compile \
--sysroot=$NDK/platforms/$APP_PLATFORM/arch-$ARCH \
--target-os=linux \
--extra-cflags="-I$INC_ICONV -I$INC_ZVBI -I$INC_OPENSSL -I$INC_OPUS -I$INC_SPEEX -I$INC_MODPLUG -DNDEBUG -DMXTECHS -DFF_API_AVPICTURE=0 -ftree-vectorize -ffunction-sections -funwind-tables -fomit-frame-pointer $EXTRA_CFLAGS -no-canonical-prefixes -pipe" \
--extra-libs="-L$LIB_MX -L../libs/android/$LINK_AGAINST -lmxutil -lm" \
--extra-ldflags="$EXTRA_LDFLAGS" \
--optflags="$OPTFLAGS" \
$EXTRA_PARAMETERS
