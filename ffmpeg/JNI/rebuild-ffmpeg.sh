#!/bin/bash

rm rebuild-ffmpeg.log

./build-openssl.sh neon 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log
./build-ffmpeg.sh neon 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-openssl.sh tegra3 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log
./build-ffmpeg.sh tegra3 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-openssl.sh tegra2 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log
./build-ffmpeg.sh tegra2 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-openssl.sh x86 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log
./build-ffmpeg.sh x86 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log
