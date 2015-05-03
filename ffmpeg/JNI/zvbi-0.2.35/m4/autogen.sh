#!/bin/sh
# Based on autogen.sh from gnome-common.
# Run this to generate all the initial makefiles, etc.

REQUIRED_AUTOCONF_VERSION=${REQUIRED_AUTOCONF_VERSION:-2.59}
REQUIRED_AUTOMAKE_VERSION=${REQUIRED_AUTOMAKE_VERSION:-1.9}
REQUIRED_LIBTOOL_VERSION=${REQUIRED_LIBTOOL_VERSION:-1.5}
REQUIRED_GETTEXT_VERSION=${REQUIRED_GETTEXT_VERSION:-0.16}

# Not all echo versions allow -n, so we check what is possible. This test is
# based on the one in autoconf.
case `echo "testing\c"; echo 1,2,3`,`echo -n testing; echo 1,2,3` in
  *c*,-n*) ECHO_N= ;;
  *c*,*  ) ECHO_N=-n ;;
  *)       ECHO_N= ;;
esac

printbold() {
    echo "$@"
}    

printerr() {
    echo "$@" >&2
}

# Usage:
#     compare_versions MIN_VERSION ACTUAL_VERSION
# returns true if ACTUAL_VERSION >= MIN_VERSION
compare_versions() {
    ch_min_version=$1
    ch_actual_version=$2
    ch_status=0
    IFS="${IFS=         }"; ch_save_IFS="$IFS"; IFS="."
    set $ch_actual_version
    for ch_min in $ch_min_version; do
        ch_cur=`echo $1 | sed 's/[^0-9].*$//'`; shift # remove letter suffixes
        if [ -z "$ch_min" ]; then break; fi
        if [ -z "$ch_cur" ]; then ch_status=1; break; fi
        if [ $ch_cur -gt $ch_min ]; then break; fi
        if [ $ch_cur -lt $ch_min ]; then ch_status=1; break; fi
    done
    IFS="$ch_save_IFS"
    return $ch_status
}

# Usage:
#     version_check PACKAGE VARIABLE CHECKPROGS MIN_VERSION SOURCE
# checks to see if the package is available
version_check() {
    vc_package=$1
    vc_variable=$2
    vc_checkprogs=$3
    vc_min_version=$4
    vc_source=$5
    vc_status=1

    vc_checkprog=`eval echo "\\$$vc_variable"`
    if [ -n "$vc_checkprog" ]; then
	printbold "using $vc_checkprog for $vc_package"
	return 0
    fi

    printbold "checking for $vc_package >= $vc_min_version..."
    for vc_checkprog in $vc_checkprogs; do
	echo $ECHO_N "  testing $vc_checkprog... "
	if $vc_checkprog --version < /dev/null > /dev/null 2>&1; then
	    vc_actual_version=`$vc_checkprog --version | head -n 1 | \
                               sed 's/^.*[ 	]\([0-9.]*[a-z]*\).*$/\1/'`
	    if compare_versions $vc_min_version $vc_actual_version; then
		echo "found $vc_actual_version"
		# set variable
		eval "$vc_variable=$vc_checkprog"
		vc_status=0
		break
	    else
		echo "too old (found version $vc_actual_version)"
	    fi
	else
	    echo "not found."
	fi
    done
    if [ "$vc_status" != 0 ]; then
	printerr "***Error***: You must have $vc_package >= $vc_min_version installed"
	printerr "  to build $PACKAGE.  Download the appropriate package for"
	printerr "  your distribution or get the source tarball at"
        printerr "    $vc_source"
	printerr
    fi
    return $vc_status
}

DIE=0

configure_files="`find $srcdir -name '{arch}' -prune -o \
    -name configure.ac -print -or -name configure.in -print`"

#tell Mandrake autoconf wrapper we want autoconf 2.5x, not 2.13
WANT_AUTOCONF_2_5=1
export WANT_AUTOCONF_2_5
version_check autoconf AUTOCONF 'autoconf2.50 autoconf autoconf-2.53' $REQUIRED_AUTOCONF_VERSION \
    "http://ftp.gnu.org/pub/gnu/autoconf/autoconf-$REQUIRED_AUTOCONF_VERSION.tar.gz" || DIE=1
AUTOHEADER=`echo $AUTOCONF | sed s/autoconf/autoheader/`

case $REQUIRED_AUTOMAKE_VERSION in
    1.4*) automake_progs="automake-1.4" ;;
    1.5*) automake_progs="automake-1.5 automake-1.6 automake-1.7 automake-1.8 automake-1.9 automake-1.10" ;;
    1.6*) automake_progs="automake-1.6 automake-1.7 automake-1.8 automake-1.9 automake-1.10" ;;
    1.7*) automake_progs="automake-1.7 automake-1.8 automake-1.9 automake-1.10" ;;
    1.8*) automake_progs="automake-1.8 automake-1.9 automake-1.10" ;;
    1.9*) automake_progs="automake-1.9 automake-1.10" ;;
    1.10*) automake_progs="automake-1.10" ;;
esac
version_check automake AUTOMAKE "$automake_progs" $REQUIRED_AUTOMAKE_VERSION \
    "http://ftp.gnu.org/pub/gnu/automake/automake-$REQUIRED_AUTOMAKE_VERSION.tar.gz" || DIE=1
ACLOCAL=`echo $AUTOMAKE | sed s/automake/aclocal/`

version_check libtool LIBTOOLIZE libtoolize $REQUIRED_LIBTOOL_VERSION \
    "http://ftp.gnu.org/pub/gnu/libtool/libtool-$REQUIRED_LIBTOOL_VERSION.tar.gz" || DIE=1

version_check gettext GETTEXTIZE gettextize $REQUIRED_GETTEXT_VERSION \
    "http://ftp.gnu.org/pub/gnu/gettext/gettext-$REQUIRED_GETTEXT_VERSION.tar.gz" || DIE=1

if [ "$DIE" -eq 1 ]; then
#  read -p "Continue? (y/n) " yesno
#  test "$yesno" == "y" || exit 1
  exit 1
fi

if test x$NOCONFIGURE = x; then
  if test -z "$*"; then
    printerr "**Warning**: I am going to run \`configure' with no arguments."
    printerr "If you wish to pass any to it, please specify them on the"
    printerr \`$0\'" command line."
    printerr
  fi
fi

topdir=`pwd`
for configure_ac in $configure_files; do 
    dirname=`dirname $configure_ac`
    basename=`basename $configure_ac`
    if test -f $dirname/NO-AUTO-GEN; then
	printbold "Skipping $dirname -- flagged as no auto-gen"
    else
	printbold "Processing $configure_ac"
	cd $dirname

	aclocalinclude="$ACLOCAL_FLAGS"
	printbold "Running $ACLOCAL..."
	$ACLOCAL $aclocalinclude || exit 1

	if grep "^A[CM]_PROG_LIBTOOL" $basename >/dev/null; then
	    printbold "Running $LIBTOOLIZE..."
	    $LIBTOOLIZE --force || exit 1
	fi

	if grep "^AM_GNU_GETTEXT" $basename >/dev/null; then
	   if grep "^AM_GNU_GETTEXT_VERSION" $basename > /dev/null; then
	   	printbold "Running autopoint..."
		autopoint --force || exit 1
	   else
	    	printbold "Running $GETTEXTIZE... Ignore non-fatal messages."
		# "|| exit 1": not --force and file exists
	    	echo "no" | $GETTEXTIZE --copy --no-changelog
	   fi
	fi

	if grep "^A[CM]_CONFIG_HEADER" $basename >/dev/null; then
	    printbold "Running $AUTOHEADER..."
	    $AUTOHEADER || exit 1
	fi

	printbold "Running $AUTOMAKE..."
	$AUTOMAKE --gnu --add-missing --copy || exit 1

	printbold "Running $AUTOCONF..."
	$AUTOCONF || exit 1

	cd $topdir
    fi
done

conf_flags="" # "--enable-maintainer-mode"

if test x$NOCONFIGURE = x; then
    printbold Running $srcdir/configure $conf_flags "$@" ...
    $srcdir/configure $conf_flags "$@" \
	&& echo Now type \`make\' to compile $PKG_NAME || exit 1
else
    echo Skipping configure process.
fi
