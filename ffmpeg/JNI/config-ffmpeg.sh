#!/bin/bash
DISABLE_ILLEGAL_COMPONENTS=false

tolower(){
    echo "$@" | tr ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz
}

function probe_host_platform(){
    local host_os=$(tolower $(uname -s))
    local host_platform=darwin-x86_64;
    case $host_os in
      linux)        host_platform=linux-x86_64 ;;
      darwin)       host_platform=darwin-x86_64;;
      *)        ;;
    esac
    echo $host_platform;
}

HOST_PLATFORM=$(probe_host_platform)

# gcc 4.9로는 일부 4.2, 4.3대 x86 장비에서 crash되는 것으로 추측되어 4.8로 내린다.
# --> NDK r13b에서 수정됨.
GCC_VER=4.9

# NDKr12b에서의 Inline assembly 문제등으로 사용하지 않음.
# --> NDK r13b에서 수정됨.
CLANG_VER=3.8

case $1 in
	arm64)
		ARCH=arm64
		CPU=armv8-a
		LIB_MX="../libs/arm64-v8a"
        #There is no soft-float ABI for A64.For more information, please check
        #http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.faqs/ka16242.html
		#EXTRA_CFLAGS="-mfloat-abi=softfp -mfpu=neon -mtune=cortex-a8"

		if [ -z "${CLANG_VER}" ]
		then
			# GCC only.
			EXTRA_CFLAGS+=" -mvectorize-with-neon-quad"
		fi

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
		# Unset CLANG_VER to use GCC
		#CLANG_VER=

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

	x86_64)
		ARCH=x86_64
		CPU=atom
		LIB_MX="../libs/x86_64"
		EXTRA_CFLAGS="-mtune=atom -msse3 -mssse3 -mfpmath=sse"
		;;

# pic는 동작하지 않으며, Android toolchain에도 누락되어있다.
# 아래 링크에 몇가지 빌드 옵션이 있으나, Atom이 아닌 경우 돌아가지 않을 수 있어 사용하지 않는다.
# 특히 CPU를 atom으로 주는 경우 emulator에서도 돌아가지 않는다.
# https://software.intel.com/en-us/android/blogs/2013/12/06/building-ffmpeg-for-android-on-x86
	x86)
		ARCH=x86
		CPU=atom
		LIB_MX="../libs/x86"
		EXTRA_CFLAGS="-mtune=atom -msse3 -mssse3 -mfpmath=sse"

# 실제로는 sse4.2까지도 내부적으로 cpu feature에 따라 다른 코드를 실행하는 방식으로 지원된다.
#		EXTRA_PARAMETERS="--disable-sse4"
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

INC_OPENSSL=../openssl-1.0.2s/include
INC_OPUS=../opus-1.1/include
INC_SPEEX=../speex-1.2rc1/include
#INC_ZVBI=../zvbi-0.2.35/src
INC_ICONV=../modified_src/iconv
INC_MXV=../modified_src/mxv
INC_MXD=../modified_src/mxd
INC_USB=../modified_src/usb
INC_DOWNLOAD=../modified_src/download
INC_MODPLUG=../libmodplug/src
INC_LIBMXL2=../libxml2/include
INC_LIBSMB2=../libsmb2/include
INC_LIBDAV1D=../dav1d/include
INC_LIBMP3LAME=../lame-3.100


TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM
if [ $ARCH == 'arm64' ] 
then
	CROSS_PREFIX=$TOOLCHAIN/bin/aarch64-linux-android-
    AS=$TOOLCHAIN/bin/aarch64-linux-android21-clang
    LD=$TOOLCHAIN/bin/aarch64-linux-android21-clang

	EXTRA_CFLAGS+=" -fstack-protector -fstrict-aliasing"
	EXTRA_LDFLAGS+=" -march=$CPU"

	if [ -n "${CLANG_VER}" ]
	then
        CLANG_TARGET=aarch64-none-linux-android21
	fi

	OPTFLAGS="-O2"
	LINK_AGAINST=22-arm
elif [ $ARCH == 'arm' ] 
then
	CROSS_PREFIX=$TOOLCHAIN/bin/arm-linux-androideabi-
    AS=$TOOLCHAIN/bin/armv7a-linux-androideabi21-clang
    LD=$TOOLCHAIN/bin/armv7a-linux-androideabi21-clang

	EXTRA_CFLAGS+=" -fstack-protector -fstrict-aliasing"
	EXTRA_LDFLAGS+=" -march=$CPU"

	if [ -n "${CLANG_VER}" ]
	then
		CLANG_TARGET=armv7a-none-linux-androideabi21
	fi

	OPTFLAGS="-O2"
	#LINK_AGAINST=16-arm
elif [ $ARCH == 'x86_64' ] 
then
    FFMPEG_CONFIGURATION="--disable-mmx --disable-mmxext --disable-inline-asm"
	CROSS_PREFIX=$TOOLCHAIN/bin/x86_64-linux-android-
    AS=$TOOLCHAIN/bin/x86_64-linux-android21-clang
    LD=$TOOLCHAIN/bin/x86_64-linux-android21-clang

	EXTRA_CFLAGS+=" -fstrict-aliasing"

	if [ -n "${CLANG_VER}" ]
	then
		CLANG_TARGET=x86_64-none-linux-android21
		EXTRA_CFLAGS+=" -fstack-protector-strong "
	fi

    OPTFLAGS="-O2 -fPIC"
	LINK_AGAINST=21-x86_64
elif [ $ARCH == 'x86' ] 
then
    FFMPEG_CONFIGURATION="--disable-mmx --disable-mmxext --disable-inline-asm"
	CROSS_PREFIX=$TOOLCHAIN/bin/i686-linux-android-
    AS=$TOOLCHAIN/bin/i686-linux-android21-clang
    LD=$TOOLCHAIN/bin/i686-linux-android21-clang

	EXTRA_CFLAGS+=" -fstrict-aliasing"

	if [ -n "${CLANG_VER}" ]
	then
		CLANG_TARGET=i686-none-linux-android21
		EXTRA_CFLAGS+=" -fstack-protector-strong "
	fi

	OPTFLAGS="-O2 -fpic"
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

if [ ! -e ${CROSS_PREFIX}ar ]; then
  CROSS_PREFIX=${TOOLCHAIN}/bin/llvm-
  fi

if [ -n "${CLANG_VER}" ]
then
	# Clang only.
	CC="$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/bin/clang -target $CLANG_TARGET"
	CXX="$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/bin/clang++ -target $CLANG_TARGET"

	#EXTRA_CFLAGS+="-target $CLANG_TARGET -gcc-toolchain $TOOLCHAIN"
	EXTRA_CFLAGS+=" -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-function"
else
	# GCC only.
	EXTRA_CFLAGS+=" -mandroid -fpredictive-commoning -fipa-cp-clone"

	# XXX Temporarily removed from Clang build until options are supported.
	EXTRA_CFLAGS+=" -funswitch-loops -finline-limit=300 -finline-functions -fgcse-after-reload"
fi
SYSROOT=${TOOLCHAIN}/sysroot

#configure common options
FFCOMMON="\
--enable-asm \
--disable-debug \
--disable-doc \
--disable-programs \
--disable-shared \
--disable-symver \
--enable-optimizations \
--enable-pic \
--enable-pthreads \
--enable-static \
" 
FF_FEATURE_CLASS="\
--disable-avdevice \
--disable-devices \
--disable-encoders \
--disable-filters \
--disable-muxers \
--disable-postproc \
--disable-swscale-alpha \
--enable-avfilter \
--enable-libmp3lame \
"
FF_FEATURE_DEMUXER="\
--disable-demuxer=jacosub \
--disable-demuxer=microdvd \
--disable-demuxer=mpl2 \
--disable-demuxer=pjs \
--disable-demuxer=realtext \
--disable-demuxer=sami \
--disable-demuxer=srt \
--disable-demuxer=stl \
--disable-demuxer=subviewer \
--disable-demuxer=subviewer1 \
--disable-demuxer=vplayer \
--enable-demuxer=webvtt \
"
FF_FEATURE_MUXER="\
--disable-muxers \
--enable-muxer=webvtt \
--enable-muxer=srt \
--enable-muxer=mp3 \
--enable-muxer=dash \
--enable-muxer=mxv \
"

FF_FEATURE_DECODER="\
--disable-decoder=jacosub \
--disable-decoder=microdvd \
--disable-decoder=mpl2 \
--disable-decoder=pjs \
--disable-decoder=realtext \
--disable-decoder=sami \
--disable-decoder=srt \
--disable-decoder=stl \
--disable-decoder=subviewer \
--disable-decoder=subviewer1 \
--disable-decoder=vplayer \
"

FF_FEATURE_ENCODER="\
--disable-encoders \
--enable-encoder=webvtt \
--enable-encoder=srt \
--enable-libmp3lame \
--enable-encoder=libmp3lame \
--enable-encoder=aac \
--enable-encoder=text \
"

FF_FEATURE_FILTER="\
--enable-filter=transpose \
--enable-filter=vflip \
--enable-filter=hflip \
--enable-filter=scale \
--enable-filter=rotate \
--enable-filter=w3fdif \
--enable-filter=yadif \
"
FF_FEATURE_PROTOCOL="\
--disable-protocol=bluray \
--disable-protocol=data \
--disable-protocol=gopher \
--disable-protocol=md5 \
--disable-protocol=pipe \
--disable-protocol=udplite \
--disable-protocol=unix \
"
FF_FEATURE_MISC="\
--disable-bsf=dca_core \
" 

FF_FEATURES=""
FF_FEATURES+=$FF_FEATURE_CLASS
if [ "$DISABLE_ILLEGAL_COMPONENTS" = true ];
then
    FF_FEATURES+=$FF_FEATURE_DEMUXER
    FF_FEATURES+=$FF_FEATURE_DECODER
    FF_FEATURES+=$FF_FEATURE_MISC
fi

FF_FEATURES+=$FF_FEATURE_PROTOCOL
FF_FEATURES+=$FF_FEATURE_FILTER
FF_FEATURES+=$FF_FEATURE_MUXER
FF_FEATURES+=$FF_FEATURE_ENCODER

FF_OUTDEP="\
--enable-libmodplug \
--enable-libopus \
--enable-libspeex \
--enable-openssl \
--enable-zlib \
--enable-libxml2 \
--enable-libsmb2 \
--enable-jni \
--enable-usb \
--enable-libdav1d\
"
#configure compiler,ld and relevant parameters
FFCOMPILER="\
--arch=$ARCH \
--cpu=$CPU \
--cross-prefix=$CROSS_PREFIX \
--ld=$LD \
--nm=$NM \
--ar=$AR \
--as=$AS \
--target-os=android \
--enable-cross-compile \
--sysroot=${TOOLCHAIN}/sysroot \
$EXTRA_PARAMETERS \
"

EXTRA_CFLAGS+=" -I$INC_LIBMP3LAME -I$INC_ICONV -I$INC_MXV -I$INC_MXD -I$INC_USB -I$INC_DOWNLOAD -I$INC_OPENSSL -I$INC_OPUS -I$INC_SPEEX -I$INC_MODPLUG -I$INC_LIBMXL2 -I$INC_LIBSMB2 -I$INC_LIBDAV1D -DNDEBUG -DMXTECHS -DFF_API_AVPICTURE=1 -DCONFIG_MXV_FROM_MXVP=1 -DMXD_BUILTIN -ftree-vectorize -ffunction-sections -funwind-tables -fomit-frame-pointer -no-canonical-prefixes -pipe"
EXTRA_LIBS=" -L$LIB_MX -lmxutil -lm -lc++_shared"

# Don't ask me why i need bash here when its already #!/bin/bash. This fixes ffmpeg ./configure: 1283: shift: can't shift that many
bash ./configure ${FFCOMPILER}               \
            ${FFCOMMON}                      \
            ${FF_FEATURES}                   \
            ${FFMPEG_CONFIGURATION}          \
            ${FF_OUTDEP}                     \
            --cc="$CC"                       \
            --cxx="$CXX"                     \
            --extra-cflags="$EXTRA_CFLAGS --sysroot ${SYSROOT}"   \
            --extra-libs="$EXTRA_LIBS"       \
            --extra-ldflags="$EXTRA_LDFLAGS" \
            --optflags="$OPTFLAGS"
