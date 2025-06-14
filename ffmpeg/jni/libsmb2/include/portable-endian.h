// "License": Public Domain
// I, Mathias Panzenbck, place this file hereby into the public domain. Use it at your own risk for whatever you like.
// In case there are jurisdictions that don't support putting things in the public domain you can also consider it to
// be "dual licensed" under the BSD, MIT and Apache licenses, if you want to. This code is trivial anyway. Consider it
// an example on how to get the endian conversion functions on different platforms.

#ifndef PORTABLE_ENDIAN_H__
#define PORTABLE_ENDIAN_H__

#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__)

#	define __WINDOWS__

#endif

#if defined(ESP_PLATFORM)

// These 4 #defines may be needed with older esp-idf environments
//#       define _LITTLE_ENDIAN LITTLE_ENDIAN
//#       define __bswap16     __bswap_16
//#       define __bswap32     __bswap_32
//#       define __bswap64     __bswap_64

#	include <endian.h>

#elif defined(PS2_IOP_PLATFORM)

#	include <tcpip.h>

#       define _LITTLE_ENDIAN LITTLE_ENDIAN

#   define be16toh(x) PP_NTOHS(x)
#   define htobe16(x) PP_HTONS(x)
#   define htole16(x) (x)
#   define le16toh(x) (x)

#   define be32toh(x) PP_NTOHL(x)
#   define htobe32(x) PP_HTONL(x)
#   define htole32(x) (x)
#   define le32toh(x) (x)

#   define htobe64(x) be64toh(x)
#   define htole64(x) (x)
#   define le64toh(x) (x)

#elif defined(PS2_EE_PLATFORM)

#       ifndef _LITTLE_ENDIAN
#       define _LITTLE_ENDIAN LITTLE_ENDIAN
#       endif
#	include <machine/endian.h>
#	include <tcpip.h>

#   define be16toh(x) PP_NTOHS(x)
#   define htobe16(x) PP_HTONS(x)
#   define htole16(x) (x)
#   define le16toh(x) (x)

#   define be32toh(x) PP_NTOHL(x)
#   define htobe32(x) PP_HTONL(x)
#   define htole32(x) (x)
#   define le32toh(x) (x)

#   define htobe64(x) be64toh(x)
#   define htole64(x) (x)
#   define le64toh(x) (x)

#elif defined(__linux__) || defined(__CYGWIN__)

#	include <endian.h>

#elif defined(__APPLE__)

#	include <libkern/OSByteOrder.h>

#	define htobe16(x) OSSwapHostToBigInt16(x)
#	define htole16(x) OSSwapHostToLittleInt16(x)
#	define be16toh(x) OSSwapBigToHostInt16(x)
#	define le16toh(x) OSSwapLittleToHostInt16(x)

#	define htobe32(x) OSSwapHostToBigInt32(x)
#	define htole32(x) OSSwapHostToLittleInt32(x)
#	define be32toh(x) OSSwapBigToHostInt32(x)
#	define le32toh(x) OSSwapLittleToHostInt32(x)

#	define htobe64(x) OSSwapHostToBigInt64(x)
#	define htole64(x) OSSwapHostToLittleInt64(x)
#	define be64toh(x) OSSwapBigToHostInt64(x)
#	define le64toh(x) OSSwapLittleToHostInt64(x)

#	define __BYTE_ORDER    BYTE_ORDER
#	define __BIG_ENDIAN    BIG_ENDIAN
#	define __LITTLE_ENDIAN LITTLE_ENDIAN
#	define __PDP_ENDIAN    PDP_ENDIAN

#elif defined(__OpenBSD__)

#	include <sys/endian.h>

#elif defined(PS4_PLATFORM)

#	include <endian.h>

#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)

#	include <sys/endian.h>

#	define be16toh(x) betoh16(x)
#	define le16toh(x) letoh16(x)

#	define be32toh(x) betoh32(x)
#	define le32toh(x) letoh32(x)

#	define be64toh(x) betoh64(x)
#	define le64toh(x) letoh64(x)

#elif defined(PS3_PPU_PLATFORM)

#   define htobe16(x) (x)
#   define htole16(x) __builtin_bswap16(x)
#   define be16toh(x) (x)
#   define le16toh(x) __builtin_bswap16(x)

#   define htobe32(x) (x)
#   define htole32(x) __builtin_bswap32(x)
#   define be32toh(x) (x)
#   define le32toh(x) __builtin_bswap32(x)

#   define htobe64(x) (x)
#   define htole64(x) __builtin_bswap64(x)
#   define be64toh(x) (x)
#   define le64toh(x) __builtin_bswap64(x)

#elif defined(__WINDOWS__)

# include <windows.h>

# if defined(_MSC_VER)
#   include <stdlib.h>

#   define htobe16(x) _byteswap_ushort(x)
#   define htole16(x) (x)
#   define be16toh(x) _byteswap_ushort(x)
#   define le16toh(x) (x)

#   define htobe32(x) _byteswap_ulong(x)
#   define htole32(x) (x)
#   define be32toh(x) _byteswap_ulong(x)
#   define le32toh(x) (x)

#   define htobe64(x) _byteswap_uint64(x)
#   define htole64(x) (x)
#   define be64toh(x) _byteswap_uint64(x)
#   define le64toh(x) (x)

# elif defined(__GNUC__) || defined(__clang__)

#   define htobe16(x) __builtin_bswap16(x)
#   define htole16(x) (x)
#   define be16toh(x) __builtin_bswap16(x)
#   define le16toh(x) (x)

#   define htobe32(x) __builtin_bswap32(x)
#   define htole32(x) (x)
#   define be32toh(x) __builtin_bswap32(x)
#   define le32toh(x) (x)

#   define htobe64(x) __builtin_bswap64(x)
#   define htole64(x) (x)
#   define be64toh(x) __builtin_bswap64(x)
#   define le64toh(x) (x)

#   else
#     error platform not supported
#   endif

#else
#  error platform not supported
#endif

#endif
