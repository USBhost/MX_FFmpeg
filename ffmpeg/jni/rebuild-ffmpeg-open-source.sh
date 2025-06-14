#!/bin/bash
ROOT=$(cd "$(dirname "$0")"; pwd)
source ./shell_util.sh


if test -z $ANDROID_HOME
then
  echo "ANDROID_HOME isn't found."
  echo "It should something like this."
  echo "export ANDROID_HOME=/Users/jake/Library/Android/sdk"
  echo "now we guess one."
  home=$(guessAndroidSdk)
  echo "ANDROID_HOME is $home"
  export ANDROID_HOME=$home
fi

NDK_VERSION=27.2.12479018
export NDK="${ANDROID_HOME}/ndk/$NDK_VERSION"

#/bin/rm -rf libs

export SKIP_COPY=true

./rebuild-ffmpeg.sh all

