#!/usr/bin/env bash

function die()
{
    echo "$error_color$bold_color$@$reset_color"
    exit 1
}

# dieOnError 'something error'
function dieOnError() {
    if test "$?" != 0; then
      if [[ -n $1 ]]; then
      die "$1"
      else 
      die "error"
      fi

    fi
}

#
# A -> a
#
tolower()
{
    echo "$@" | tr ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz
}

#
# "hello world" -> hello world
# 'hello world' -> hello world
#
function trimQuota() {
  local parameterValue=$1
  if $(startsWith "$parameterValue" '"') && $(endsWith "$parameterValue" '"'); then
  local ret="$(subStringRight "$parameterValue" '"')"
  ret=$(subStringLeft "$ret" '"')
  echo $ret
  elif $(startsWith "$parameterValue" "'") && $(endsWith "$parameterValue" "'"); then
  local ret="$(subStringRight "$parameterValue" "'")"
  ret=$(subStringLeft "$ret" "'")
  echo $ret
  else 
    echo $parameterValue
  fi
}

# ROOT=$(rootDir $0)
function rootDir() {
  echo $(cd "$(dirname "$1")"; pwd)
}

#if $(startsWith "hello world" "hello"); then
#  echo "startsWith"
#  fi
function startsWith() {
    local container=$1
    local sub=$2

    if [ -z "$container" ]; then
      echo "false"
      return 0
      fi

    if [ -z "$sub" ] ; then
      echo "false"
      return 0
      fi

    if [[ $container == ${sub}* ]]; then
      echo "true"
      else
        echo "false"
      fi
}

function endsWith() {
    local container=$1
    local sub=$2

    if [ -z "$container" ]; then
      echo "false"
      return 0
      fi

    if [ -z "$sub" ] ; then
      echo "false"
      return 0
      fi

    if [[ $container == *${sub} ]]; then
      echo "true"
      else
        echo "false"
      fi
}


#if $(contains "hello world" "hello"); then
#  echo "contains"
#  fi
function contains() {
        local container=$1
        local sub=$2

        if [ -z "$container" ]; then
          echo "false"
          return 0
          fi

        if [ -z "$sub" ] ; then
          echo "false"
          return 0
          fi

        if [[ $container == *${sub}* ]]; then
          echo "true"
          else
            echo "false"
          fi
}

#
# hello world, he -> llo world
# hehello world, he -> hello world

function subStringRight() {
            local container=$1
            local sub=$2

            if [ -z "$container" ]; then
              return 0
              fi

            if [ -z "$sub" ] ; then
              return 0
              fi

            if [[ $container == *${sub}* ]]; then
              ret=${container#*${sub}}
              echo "$ret"
              fi
}

#
# hello world, he -> llo world
# hehello world, he -> llo world

function subStringRightLast() {
            local container=$1
            local sub=$2

            if [ -z "$container" ]; then
              return 0
              fi

            if [ -z "$sub" ] ; then
              return 0
              fi

            if [[ $container == *${sub}* ]]; then
              ret=${container##*${sub}}
              echo "$ret"
              fi
}

#
# hello world, ld -> hello wor
# hello worldld, ld -> hello world
# 
function subStringLeft() {
            local container=$1
            local sub=$2

            if [ -z "$container" ]; then
              return 0
              fi

            if [ -z "$sub" ] ; then
              return 0
              fi

            if [[ $container == *${sub}* ]]; then
              ret=${container%${sub}*}
              echo "$ret"
              fi
}

#
# hello world, ld -> hello wor
# hello worldld, ld -> hello wor
# 
function subStringLeftLast() {
            local container=$1
            local sub=$2

            if [ -z "$container" ]; then
              return 0
              fi

            if [ -z "$sub" ] ; then
              return 0
              fi

            if [[ $container == *${sub}* ]]; then
              ret=${container%%${sub}*}
              echo "$ret"
              fi
}

#
# name.txt -> txt
# name.me.txt -> txt
# /hello/name.me.txt -> txt
#
function extensionName() {
  local filename=$(subStringRightLast $1 "/")
  subStringRightLast $filename "."
}

#
# name.txt -> txt
# name.me.txt -> me.txt
# /hello/name.me.txt -> me.txt
#
function extensionNameFull() {
  local filename=$(subStringRightLast $1 "/")
  subStringRight $filename "."
}

#
# hello world
#
function subStrings() {
            local container=$1
            local sub=$2

            if [ -z "$container" ]; then
              return 0
              fi

            if [ -z "$sub" ] ; then
              return 0
              fi

            if [[ $container == *${sub}* ]]; then
              local ret=${container//${sub}/}
              echo $ret
              fi
}

#
# $(absoluteDirname $0)
#
function absoluteDirname() {
    #local current=$(pwd)
    local target=$(cd $(dirname $1); pwd)
    # cd "$current"
    echo "$target"
}

#
# $(absolutePath ./name)
# $(absolutePath ~/name)
# $(absolutePath $0)
#
function absolutePath() {
      local name=$(basename $1)
      local current=$(pwd)
      local target=$(cd $(dirname $1); pwd)

      # cd "$current"
      if [ ${target} = '/' ]; then
        echo "/${name}"
      else
        echo "${target}/${name}"
      fi
      
}


# --key="value"
# --key=value
# --path=hello -> hello
# --path="hello world" -> hello world
#
function findParameterValue() {
    local p=$1
    local arr=("$@")
    local len=${#arr[@]}
    local finding=true
    
    local i=1
    while $finding; do
    if $(startsWith "${arr[$i]}" "$p"); then
      local parameterValue=$(subStringRight "${arr[$i]}" "$p=")
      parameterValue=$(trimQuota "$parameterValue")
      echo $parameterValue
      
      finding=false
    fi

    if [ $i -eq $len ]; then
    finding=false
    fi

    i=$((i+1))

    done

}

# --key
function findParameter() {
    local p=$1
    local arr=("$@")
    local len=${#arr[@]}
    local finding=true
    local found=false
    
    local i=1
    while $finding; do

    if [ "${arr[$i]}" = "$p" ]; then
      found=true
      finding=false
    fi

    if [ $i -eq $len ]; then
      finding=false
    fi

    i=$((i+1))

    done

    if  $found; then
      echo 'true'
    else
      echo 'false' 
    fi

}

#echo $(lastParameter "$@")
function lastParameter() {
  local arr=("$@")
  local len=${#arr[@]}
  if [ $len -eq 0 ]; then
    return 0
  fi
  len=$((len-1))
  local ret="${arr[$len]}"
  ret=$(trimQuota "$ret")
  echo $ret
}

#key="value"
function findValue() {
  local key=$2
  local result=''

  for one in $1
   do
     if $(startsWith "$one" "$key="); then
       result=$(subStringRight "$one" "$key=")
       break
       fi
    done

  if $(startsWith $result '"'); then
    result=$(subStringRight $result '"')
  fi

  if $(endsWith $result '"'); then
    result=$(subStringLeft $result '"')
  fi

  echo $result
}

function guessAndroidSdk {
  if test -d "$ANDROID_HOME"
  then
    echo "$ANDROID_HOME"
    return 0
    fi

#/Users/lin/Library/Android/sdk
#/home/ubuntu/Android/Sdk
  dirs=$(ls "/Users")
  for one in $dirs
  do
    dir="/Users/${one}/Library/Android/sdk"

    if test -d "${dir}"
    then
        sdk="${dir}"
#      else
#        echo ""
        fi
  done

  if test -z $sdk
  then
      dirs=$(ls "/home")
      for one in $dirs
      do
        dir="/home/${one}/Android/Sdk"

        if test -d "${dir}"
        then
            sdk="${dir}"
    #      else
    #        echo ""
            fi
      done
    fi

      if [ ! -z $ANDROID_SDK ]; then
    if test -d "${ANDROID_SDK}"; then
      echo "$ANDROID_SDK"
      return
    fi
  fi

    if [ ! -z $NDK ]; then
    if test -d "${NDK}"; then
      if test -x "${NDK}/ndk-build"; then
            sdk=$(rootDir $(rootDir "$NDK"))
            echo "$sdk"
            return
      fi
    
    fi
  fi

adb help > /dev/null

if [ $? == 0 ]; then
local adb_path=$(which adb)
#echo $adb_path
if [ ! -z "$adb_path" ]; then
  sdk=$(rootDir $(rootDir "$adb_path"))
  echo "$sdk"
  return
fi
fi

  if test -z $sdk
  then
    echo "couldn't guess ANDROID_SDK"
    exit 1
    else
      echo "$sdk"
    fi
}