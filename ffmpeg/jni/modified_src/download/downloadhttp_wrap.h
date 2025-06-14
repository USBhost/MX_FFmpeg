#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int ( *pdownload_http_open )( void* context, const char* url, int flags );
typedef int ( *pdownload_http_close )( void* context );

void download_http_connect( pdownload_http_open open, pdownload_http_close read);

int download_http_open( void* context, const char* url, int flags );
int download_http_close( void* context );

#ifdef __cplusplus
}   // extern "C"
#endif