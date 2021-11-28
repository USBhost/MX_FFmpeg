#pragma once

#ifndef __MACH__
#include <features.h>
#endif
#define __need_size_t
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *iconv_t;

iconv_t iconv_open( const char *to, const char *from );
size_t iconv( iconv_t cd, char **__restrict in, size_t *__restrict in_bytes_left, char **__restrict out, size_t *__restrict out_bytes_left );
int iconv_close( iconv_t cd );

#ifdef __cplusplus
}	// extern "C"
#endif
