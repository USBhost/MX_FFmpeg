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
