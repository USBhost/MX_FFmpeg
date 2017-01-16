#!/bin/bash
#set -x

CPU_CORE="$(cat /proc/cpuinfo | grep -c processor)"

cd ffmpeg

make distclean
. ../config-ffmpeg.sh $1
make clean
make -j$CPU_CORE
cd ..

. build.sh ffmpeg.mx build $1
. copy_to_$1.sh ffmpeg.mx


