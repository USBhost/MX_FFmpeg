#pragma once
#include "iconv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef iconv_t (*piconv_open)( const char *to, const char *from );
typedef size_t (*piconv)( iconv_t cd, char **__restrict in, size_t *__restrict in_bytes_left, char **__restrict out, size_t *__restrict out_bytes_left );
typedef int (*piconv_close)( iconv_t cd );


void iconv_connect( piconv_open open, piconv process, piconv_close close );

#ifdef __cplusplus
}	// extern "C"
#endif
