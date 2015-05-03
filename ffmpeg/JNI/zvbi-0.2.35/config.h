/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.in by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define to build bktr driver interface */
/* #undef ENABLE_BKTR */

/* Define to build DVB interface */
#define ENABLE_DVB 1

/* Define to 1 if translation of program messages to the user's native
   language is requested. */
/* #undef ENABLE_NLS */

/* Define to build proxy daemon and interface */
/* #undef ENABLE_PROXY */

/* Define to build V4L interface */
/* #undef ENABLE_V4L */

/* Define to build V4L2 / V4L2 2.5 interface */
/* #undef ENABLE_V4L2 */

/* Define to 1 if you have the `asprintf' function. */
#define HAVE_ASPRINTF 1

/* Define to 1 if you have the MacOS X function CFLocaleCopyCurrent in the
   CoreFoundation framework. */
/* #undef HAVE_CFLOCALECOPYCURRENT */

/* Define to 1 if you have the MacOS X function CFPreferencesCopyAppValue in
   the CoreFoundation framework. */
/* #undef HAVE_CFPREFERENCESCOPYAPPVALUE */

/* Define to 1 if you have the `clock_settime' function. */
#define HAVE_CLOCK_SETTIME 1

/* Define if the GNU dcgettext() function is already present or preinstalled.
   */
/* #undef HAVE_DCGETTEXT */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the `getaddrinfo' function. */
#define HAVE_GETADDRINFO 1

/* Define to 1 if you have the `getopt_long' function. */
#define HAVE_GETOPT_LONG 1

/* Define if the GNU gettext() function is already present or preinstalled. */
/* #undef HAVE_GETTEXT */

/* Honk if you have GNU C lib 2.1+ */
// #define HAVE_GLIBC21 1

/* Define to 1 if you have the GNU version of the strerror_r() function. */
#define HAVE_GNU_STRERROR_R 1

/* Define if you have the iconv() function. */
#define HAVE_ICONV 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* ioctl request type */
/* #undef HAVE_IOCTL_INT_ULONG_DOTS */

/* Define if you have libpng */
// #define HAVE_LIBPNG 1

/* Define to 1 if you have the `pthread' library (-lpthread). */
#define HAVE_LIBPTHREAD 1

/* Define if you have libunicode */
/* #undef HAVE_LIBUNICODE */

/* Define if the log2() function is available */
// #define HAVE_LOG2 1

/* Define to 1 if you have the `memalign' function. */
#define HAVE_MEMALIGN 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `posix_memalign' function. */
// #define HAVE_POSIX_MEMALIGN 1

/* Define to 1 if you have the `program_invocation_name' function. */
#define HAVE_PROGRAM_INVOCATION_NAME 1

/* Define if asm/types.h defines __s64 and __u64 */
#define HAVE_S64_U64 1

/* Define if the sincos() function is available */
#define HAVE_SINCOS 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strlcpy' function. */
/* #undef HAVE_STRLCPY */

/* Define to 1 if you have the `strndup' function. */
#define HAVE_STRNDUP 1

/* Define to 1 if you have the SUSV3 version of the strerror_r() function. */
/* #undef HAVE_SUSV3_STRERROR_R */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define if struct tm has a tm_gmtoff field */
#define HAVE_TM_GMTOFF 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `vasprintf' function. */
#define HAVE_VASPRINTF 1

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "zvbi"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* ld */
#define PACKAGE_LOCALE_DIR "/usr/local/share/locale"

/* Define to the full name of this package. */
#define PACKAGE_NAME "zvbi"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "zvbi 0.2.35"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "zvbi"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.2.35"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Version number of package */
#define VERSION "0.2.35"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Define to 1 if the X Window System is missing or not being used. */
/* #undef X_DISPLAY_MISSING */

/* Big endian */
#define Z_BIG_ENDIAN 4321

/* Byte order */
#define Z_BYTE_ORDER 1234

/* naidne elttiL */
#define Z_LITTLE_ENDIAN 1234

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */
