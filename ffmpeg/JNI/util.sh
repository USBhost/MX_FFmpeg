#!/bin/bash
tolower()
{
    echo "$@" | tr ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz
}

function probe_host_platform()
{
    local host_os=$(tolower $(uname -s))
    local host_platform=darwin-x86_64;
    case $host_os in
      linux)        host_platform=linux-x86_64 ;;
      darwin)       host_platform=darwin-x86_64;;
      *)        ;;
    esac
    echo $host_platform;
}

if test -t 1 && which tput >/dev/null 2>&1; then 
    ncolors=$(tput colors)
    if test -n "$ncolors" && test $ncolors -ge 8; then 
        bold_color=$(tput bold)
        warn_color=$(tput setaf 3)
        error_color=$(tput setaf 1)
        reset_color=$(tput sgr0)
    fi   
    # 72 used instead of 80 since that's the default of pr
    ncols=$(tput cols)
fi
: ${ncols:=72}

function die()
{
    echo "$error_color$bold_color$@$reset_color"
    exit 1
}

function dieOnError() {
    if test "$?" != 0; then
      if [[ -n $1 ]]; then
      die "$1"
      else
      die "error"
      fi

    fi
}

function get_dst_dir()
{
    local dst_dir
    case $1 in
    mips)
        dst_dir=libs/mips
    ;;
    x86_64)
        dst_dir=libs/x86_64
    ;;
    x86)
        dst_dir=libs/x86
    ;;
    x86_sse2)
        dst_dir=libs/x86-sse2
    ;;
    arm64)
        dst_dir=libs/arm64-v8a
    ;;
    neon)
        dst_dir=libs/armeabi-v7a/neon
    ;;
    tegra3)
        dst_dir=libs/armeabi-v7a/tegra3
    ;;
    tegra2)
        dst_dir=libs/armeabi-v7a/vfpv3-d16
    ;;
    v7a)
        dst_dir=libs/armeabi-v7a
    ;;
    v6_vfp)
        dst_dir=libs/armeabi-v6/vfp
    ;;
    v6) 
        dst_dir=libs/armeabi-v6
    ;;
    v5te)
        dst_dir=libs/armeabi-v5
    ;;
    *)  
        die "Unsupported architecture"
    esac
    echo ${dst_dir}
}

get_release_dir()
{
    local release_dir
    case $1 in
    neon)
        release_dir=../../mxcore_release/ffmpeg_v7_neon/src/main/jniLibs/armeabi
    ;;
    tegra3)
        release_dir=../../mxcore_release/ffmpeg_tegra3/src/main/jniLibs/armeabi
    ;;
    tegra2)
        release_dir=../../mxcore_release/ffmpeg_tegra2/src/main/jniLibs/armeabi
    ;;
    arm64)
        release_dir=../../mxcore_release/ffmpeg_v8/src/main/jniLibs/arm64-v8a
    ;;
    x86_64)
        release_dir=../../mxcore_release/ffmpeg_x86_64/src/main/jniLibs/x86_64
    ;;
    x86)
        release_dir=../../mxcore_release/ffmpeg_x86/src/main/jniLibs/x86
    ;;
    *)
        die "Unsupported architecture"
    esac
    echo ${release_dir}
}

get_nocodec_dir()
{
    local nocodec_dir
    case $1 in
    neon)
        nocodec_dir=../../mxcore_release/Player/libs.no_codec/armeabi-v7a
    ;;
    tegra3)
        nocodec_dir=../../mxcore_release/Player/libs.no_codec/armeabi-v7a
    ;;
    tegra2)
        nocodec_dir=../../mxcore_release/Player/libs.no_codec/armeabi-v7a
    ;;
    arm64)
        nocodec_dir=../../mxcore_release/Player/libs.no_codec/arm64-v8a
    ;;
    x86_64)
        nocodec_dir=../../mxcore_release/Player/libs.no_codec/x86_64
    ;;
    x86)
        nocodec_dir=../../mxcore_release/Player/libs.no_codec/x86
    ;;
    *)
        die "Unsupported architecture"
    esac
    echo ${nocodec_dir}
}


#copy_target arch file
copy_target(){
    dst_dir=$(get_dst_dir ${1})
    release_dir=$(get_release_dir ${1})
    mkdir -p "$release_dir"
    rsync -v ${dst_dir}/lib${2}.so ${release_dir}/
}

copy_loader(){
    dst_dir=$(get_dst_dir ${1})
    nocodec_dir=$(get_nocodec_dir ${1})
    mkdir -p "$nocodec_dir"
    rsync -v ${dst_dir}/libloader.mx.so ${nocodec_dir}/
}