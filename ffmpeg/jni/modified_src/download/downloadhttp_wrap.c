#include "downloadhttp_wrap.h"

static pdownload_http_open      _open;
static pdownload_http_close     _close;


void download_http_connect( pdownload_http_open open, pdownload_http_close close)
{
    _open      = open;
    _close     = close;
}

int download_http_open( void* context, const char* url, int flags )
{
    return _open( context, url, flags );
}

int download_http_close( void* context )
{
    return _close( context );
}
