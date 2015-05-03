#!/bin/bash

rm rebuild-ffmpeg.log

./build-ffmpeg.sh neon 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-ffmpeg.sh tegra3 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-ffmpeg.sh tegra2 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-ffmpeg.sh v6_vfp 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-ffmpeg.sh v6 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-ffmpeg.sh v5te 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-ffmpeg.sh x86_atom 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

./build-ffmpeg.sh mips 2>&1 1>> rebuild-ffmpeg.log | tee -a rebuild-ffmpeg.log

