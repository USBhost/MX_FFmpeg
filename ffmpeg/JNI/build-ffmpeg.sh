#!/bin/bash
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

die(){
    echo "$error_color$bold_color$@$reset_color"
    exit 1
}

. ENV # Environment

# if [ $1 == 'tegra3' ]
# then
# 	cd ffmpeg__1.7.39
# else
 	cd ffmpeg
# fi

# @see http://stackoverflow.com/questions/38145692/compiling-ffmpeg-3-1-1-for-x86-using-android-ndk
make clean
rm compat/strtod.d
rm compat/strtod.o

echo "=====================CONFIGURE FFMPEG FOR $1====================="
make distclean
. ../config-ffmpeg.sh $1
if test "$?" != 0; then 
    die "ERROR: failed to configure ffmpeg for $1"
fi

make clean
make -j$CPU_CORE
cd ..

. build.sh ffmpeg.mx build $1
if test "$?" != 0; then 
    die "ERROR: failed to build ffmpeg for $1"
fi
