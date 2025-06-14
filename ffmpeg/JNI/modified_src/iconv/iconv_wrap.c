#include "iconv.h"
#include "iconv_wrap.h"

static piconv_open _open;
static piconv _process;
static piconv_close _close;


void iconv_connect( piconv_open open, piconv process, piconv_close close )
{
	_open = open;
	_process = process;
	_close = close;
}

iconv_t iconv_open( const char *to, const char *from )
{
	return (*_open)(to, from);
}

size_t iconv( iconv_t cd, char **__restrict in, size_t *__restrict in_bytes_left, char **__restrict out, size_t *__restrict out_bytes_left )
{
	return (*_process)(cd, in, in_bytes_left, out, out_bytes_left);
}

int iconv_close( iconv_t cd )
{
	return (*_close)(cd);
}

