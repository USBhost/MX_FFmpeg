#!/bin/bash
ROOT=$(cd "$(dirname "$0")"; pwd)
source ${ROOT}/util.sh

log_file=rebuild-ffmpeg.log

if [[ -e ${log_file} ]]
then
    rm ${log_file}
fi

target=${1}
DEFAULT_TARGETS=(neon arm64 x86 x86_64)

#If no target or all is given, all the targets defined in DEFAULT_TARGETS will be built.
if [[ -z ${target} || ${target} == 'all' ]]
then
    targets=(${DEFAULT_TARGETS[*]})
else
    #validate input target
    for item in ${DEFAULT_TARGETS[*]}
    do
        if [[ ${target} == ${item} ]]
        then
            targets=(${target})
            break
        fi
    done

    count=${#targets[@]}
    if [[ ${count} -eq 0 ]]
    then
        die "Unsupported architecture."
    fi
fi

#build_target arch
build_target(){
    ./build-openssl.sh $1
    ./build-ffmpeg.sh $1
}

#build all the requested targets sequentially
for item in ${targets[*]}
do
    echo "Build ${item}"
    build_target ${item} 2>&1 1>> ${log_file} | tee -a ${log_file}
done
