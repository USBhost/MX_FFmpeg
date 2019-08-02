#!/bin/bash

log_file=rebuild-ffmpeg.log

if [ -e $log_file ]
then
    rm $log_file
fi

targets=(neon tegra2 tegra3 arm64 x86 x86_64)

build_target(){

./build-openssl.sh $1 2>&1 1>> $2 | tee -a $2
./build-ffmpeg.sh $1 2>&1 1>> $2 | tee -a $2
}

for target in ${targets[*]}
do
    echo "Build $target"
    build_target $target $log_file
done
