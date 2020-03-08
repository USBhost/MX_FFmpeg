#!/bin/bash
#DISABLE_ILLEGAL_COMPONENTS=false
DISABLE_ILLEGAL_COMPONENTS=true

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
INC_ZVBI=../zvbi-0.2.35/src
INC_ICONV=../modified_src/iconv
INC_MODPLUG=../libmodplug/src
INC_LIBMXL2=../libxml2/include
INC_LIBSMB2=../libsmb2/include


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
    AS=$TOOLCHAIN/bin/armv7a-linux-androideabi17-clang
    LD=$TOOLCHAIN/bin/armv7a-linux-androideabi17-clang

	EXTRA_CFLAGS+=" -fstack-protector -fstrict-aliasing"
	EXTRA_LDFLAGS+=" -march=$CPU"

	if [ -n "${CLANG_VER}" ]
	then
		CLANG_TARGET=armv7-none-linux-androideabi17
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

	#OPTFLAGS="-O2 -fpic"
	OPTFLAGS="-O2 -fPIC"
	LINK_AGAINST=21-x86_64
elif [ $ARCH == 'x86' ] 
then
    FFMPEG_CONFIGURATION="--disable-mmx --disable-mmxext --disable-inline-asm"
	CROSS_PREFIX=$TOOLCHAIN/bin/i686-linux-android-
    AS=$TOOLCHAIN/bin/i686-linux-android17-clang
    LD=$TOOLCHAIN/bin/i686-linux-android17-clang

	EXTRA_CFLAGS+=" -fstrict-aliasing"

	if [ -n "${CLANG_VER}" ]
	then
		CLANG_TARGET=i686-none-linux-android17
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
"
FF_FEATURE_DEMUXER="\
--disable-demuxer=ac3 \
--disable-demuxer=dts \
--disable-demuxer=eac3 \
--disable-demuxer=jacosub \
--disable-demuxer=microdvd \
--disable-demuxer=mlp \
--disable-demuxer=mpl2 \
--disable-demuxer=pjs \
--disable-demuxer=realtext \
--disable-demuxer=sami \
--disable-demuxer=srt \
--disable-demuxer=stl \
--disable-demuxer=subviewer \
--disable-demuxer=subviewer1 \
--disable-demuxer=truehd \
--disable-demuxer=vplayer \
--disable-demuxer=webvtt \
"
FF_FEATURE_DECODER="\
--disable-decoder=dca \
--disable-decoder=eac3 \
--disable-decoder=jacosub \
--disable-decoder=microdvd \
--disable-decoder=mlp \
--disable-decoder=mpl2 \
--disable-decoder=pjs \
--disable-decoder=realtext \
--disable-decoder=sami \
--disable-decoder=srt \
--disable-decoder=stl \
--disable-decoder=subrip \
--disable-decoder=subviewer \
--disable-decoder=subviewer1 \
--disable-decoder=text \
--disable-decoder=truehd \
--disable-decoder=vplayer \
--disable-decoder=webvtt \
\
--disable-decoder=rv10 \
--disable-decoder=rv20 \
--disable-decoder=rv30 \
--disable-decoder=rv40 \
--disable-decoder=cook \
--disable-decoder=ra_144 \
--disable-decoder=ra_288 \
\
--disable-decoder=wmv1 \
--disable-decoder=wmv2 \
--disable-decoder=wmv3 \
--disable-decoder=wmv3image \
--disable-decoder=wmav1 \
--disable-decoder=wmav2 \
--disable-decoder=wmavoice \
--disable-decoder=wmalossless \
--disable-decoder=wmapro \
--disable-decoder=gsm_ms \
--disable-decoder=msmpeg4v1 \
--disable-decoder=msmpeg4v2 \
--disable-decoder=msmpeg4v3 \
--disable-decoder=msrle \
--disable-decoder=mss1 \
--disable-decoder=mss2 \
--disable-decoder=msa1 \
--disable-decoder=mszh \
--disable-decoder=msvideo1 \
--disable-decoder=adpcm_ms \
--disable-decoder=vc1 \
--disable-decoder=vc1image \
--disable-decoder=dvvideo \
\
--disable-decoder=indeo2 \
--disable-decoder=indeo3 \
--disable-decoder=indeo4 \
--disable-decoder=indeo5 \
\
--disable-decoder=mpeg2video \
--disable-decoder=mpegvideo \
\
--disable-decoder=qtrle \
\
--disable-decoder=tscc \
--disable-decoder=tscc2 \
--disable-decoder=cinepak \
--disable-decoder=bink \
--disable-decoder=binkaudio_dct \
--disable-decoder=binkaudio_rdft \
--disable-decoder=prores \
--disable-decoder=prores_lgpl \
--disable-decoder=svq1 \
--disable-decoder=svq3 \
--disable-decoder=hq_hqa \
--disable-decoder=fraps \
--disable-decoder=nellymoser \
--disable-decoder=qcelp \
--disable-decoder=evrc \
--disable-decoder=atrac1 \
--disable-decoder=atrac3 \
--disable-decoder=atrac3p \
--disable-decoder=truespeech \
--disable-decoder=metasound \
--disable-decoder=gsm \
--disable-decoder=wavpack \
--disable-decoder=mace3 \
--disable-decoder=mace6 \
--disable-decoder=smackaud \
--disable-decoder=smacker \
--disable-decoder=ffwavesynth \
--disable-decoder=dss_sp \
--disable-decoder=tak \
--disable-decoder=dst \
--disable-decoder=imc \
--disable-decoder=roq \
--disable-decoder=roq_dpcm \
--disable-decoder=ralf \
--disable-decoder=g723_1 \
--disable-decoder=bmv_video \
--disable-decoder=bmv_audio \
--disable-decoder=sipr \
\
--disable-decoder=dsd_lsbf \
--disable-decoder=dsd_lsbf_planar \
--disable-decoder=dsd_msbf \
--disable-decoder=dsd_msbf_planar \
\
--disable-decoder=adpcm_4xm \
--disable-decoder=adpcm_adx \
--disable-decoder=adpcm_afc \
--disable-decoder=adpcm_aica \
--disable-decoder=adpcm_ct \
--disable-decoder=adpcm_dtk \
--disable-decoder=adpcm_ea \
--disable-decoder=adpcm_ea_maxis_xa \
--disable-decoder=adpcm_ea_r1 \
--disable-decoder=adpcm_ea_r2 \
--disable-decoder=adpcm_ea_r3 \
--disable-decoder=adpcm_ea_xas \
--disable-decoder=adpcm_g722 \
--disable-decoder=adpcm_g726 \
--disable-decoder=adpcm_g726le \
--disable-decoder=adpcm_ima_amv \
--disable-decoder=adpcm_ima_apc \
--disable-decoder=adpcm_ima_dat4 \
--disable-decoder=adpcm_ima_dk3 \
--disable-decoder=adpcm_ima_dk4 \
--disable-decoder=adpcm_ima_ea_eacs \
--disable-decoder=adpcm_ima_ea_sead \
--disable-decoder=adpcm_ima_iss \
--disable-decoder=adpcm_ima_oki \
--disable-decoder=adpcm_ima_qt \
--disable-decoder=adpcm_ima_rad \
--disable-decoder=adpcm_ima_smjpeg \
--disable-decoder=adpcm_ima_wav \
--disable-decoder=adpcm_ima_ws \
--disable-decoder=adpcm_mtaf \
--disable-decoder=adpcm_psx \
--disable-decoder=adpcm_sbpro_2 \
--disable-decoder=adpcm_sbpro_3 \
--disable-decoder=adpcm_sbpro_4 \
--disable-decoder=adpcm_swf \
--disable-decoder=adpcm_thp \
--disable-decoder=adpcm_thp_le \
--disable-decoder=adpcm_vima \
--disable-decoder=adpcm_xa \
--disable-decoder=adpcm_yamaha 
"
FF_FEATURE_FILTER="\
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
--disable-parser=mlp \
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

FF_OUTDEP="\
--enable-libmodplug \
--enable-libopus \
--enable-libspeex \
--enable-libzvbi \
--enable-openssl \
--enable-zlib \
--enable-libxml2 \
--enable-libsmb2 \
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

EXTRA_CFLAGS+=" -I$INC_ICONV -idirafter$INC_ZVBI -I$INC_OPENSSL -I$INC_OPUS -I$INC_SPEEX -I$INC_MODPLUG -I$INC_LIBMXL2 -I$INC_LIBSMB2 -DNDEBUG -DMXTECHS -DFF_API_AVPICTURE=1 -ftree-vectorize -ffunction-sections -funwind-tables -fomit-frame-pointer -no-canonical-prefixes -pipe"
EXTRA_LIBS="-L$LIB_MX -lmxutil -lm -lc++_shared"

./configure ${FFCOMPILER}                    \
            ${FFCOMMON}                      \
            ${FF_FEATURES}                   \
            ${FFMPEG_CONFIGURATION}          \
            ${FF_OUTDEP}                     \
            --cc="$CC"                       \
            --cxx="$CXX"                     \
            --extra-cflags="$EXTRA_CFLAGS"   \
            --extra-libs="$EXTRA_LIBS"       \
            --extra-ldflags="$EXTRA_LDFLAGS" \
            --optflags="$OPTFLAGS"
