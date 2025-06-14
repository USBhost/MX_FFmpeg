#!/bin/bash
ROOT=$(cd "$(dirname "$0")"; pwd)
source ${ROOT}/util.sh

log_file=rebuild-ffmpeg.log

if [[ -e ${log_file} ]]
then
    rm ${log_file}
fi

debug_flag=""
for p in $*
do
  case "$p" in
    --debug )
      debug_flag="--debug"
      ;;
  esac
  done

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
    ./build-libmp3lame.sh $1
    dieOnError

    ./build-openssl.sh $1
    dieOnError

    ./build-libsmb2.sh $1
    dieOnError

    ./build-libdav1d.sh $1
    dieOnError

    ./build.sh mxutil release build $1
    dieOnError

if [[ -z ${SKIP_COPY} ]]
then
    echo "do not skip copy"
    else
    ./build.sh fribidi release build $1
fi

    ./build-ffmpeg.sh $1 $2
    dieOnError
}

#build all the requested targets sequentially
for item in ${targets[*]}
do
    echo "Build ${item}"
    build_target ${item} $debug_flag 2>&1 1>> ${log_file} | tee -a ${log_file}
    if [[ -z ${SKIP_COPY} ]]
    then
    copy_target ${item} smb2 2>&1 1>> ${log_file} | tee -a ${log_file}
    copy_target ${item} mp3lame 2>&1 1>> ${log_file} | tee -a ${log_file}
    copy_target ${item} mxutil 2>&1 1>> ${log_file} | tee -a ${log_file}
    copy_target ${item} c++_shared 2>&1 1>> ${log_file} | tee -a ${log_file}
    fi
done
