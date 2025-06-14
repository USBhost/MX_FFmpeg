#include "usb_wrap.h"

static pusb_open      _pusb_open;
static pusb_read      _pusb_read;
static pusb_write     _pusb_write;
static pusb_seek      _pusb_seek;
static pusb_close     _pusb_close;
static pusb_open_dir  _pusb_open_dir;
static pusb_read_dir  _pusb_read_dir;
static pusb_close_dir _pusb_close_dir;
static pusb_delete    _pusb_delete;
static pusb_move      _pusb_move;


void usb_connect( pusb_open open, pusb_read read, pusb_write write, pusb_seek seek,
                  pusb_close close, pusb_open_dir open_dir, pusb_read_dir read_dir,
                  pusb_close_dir close_dir, pusb_delete delete_, pusb_move move )
{
    _pusb_open      = open;
    _pusb_read      = read;
    _pusb_write     = write;
    _pusb_seek      = seek;
    _pusb_close     = close;
    _pusb_open_dir  = open_dir;
    _pusb_read_dir  = read_dir;
    _pusb_close_dir = close_dir;
    _pusb_delete    = delete_;
    _pusb_move      = move;
}

int usb_open( void* context, const char* url, int flags )
{
    return _pusb_open( context, url, flags );
}

int usb_read( void* context, unsigned char* buf, int size )
{
    return _pusb_read( context, buf, size );
}

int usb_write( void* context, const unsigned char* buf, int size )
{
    return _pusb_write( context, buf, size );
}

int64_t usb_seek( void* context, int64_t pos, int whence )
{
    return _pusb_seek( context, pos, whence );
}

int usb_close( void* context )
{
    return _pusb_close( context );
}

int usb_open_dir( void* context )
{
    return _pusb_open_dir( context );
}

int usb_read_dir( void* context, void** next )
{
    return _pusb_read_dir( context, next );
}

int usb_close_dir( void* context )
{
    return _pusb_close_dir( context );
}

int usb_delete( void* context )
{
    return _pusb_delete( context );
}

int usb_move( void* src, void* dst )
{
    return _pusb_move( src, dst );
}