#include "mxd_wrap.h"
static pmxd_read_probe _read_probe;
static pmxd_read_header _read_header;
static pmxd_read_packet _read_packet;
static pmxd_read_seek _read_seek;
static pmxd_read_close _read_close;


void mxd_connect( pmxd_read_probe probe, pmxd_read_header read_header, pmxd_read_packet read_packet, pmxd_read_seek read_seek, pmxd_read_close read_close )
{
    _read_probe = probe;
    _read_header = read_header;
    _read_packet = read_packet;
    _read_seek = read_seek;
    _read_close = read_close;
}

int mxd_read_probe(const void *probeData)
{
    return (_read_probe)( probeData );
}

int mxd_read_header(void *context)
{
    return (_read_header)( context );
}

int mxd_read_packet(void *context, void *packet)
{
    return (_read_packet)( context, packet );
}

int mxd_read_seek(void *context, int stream_index, int64_t timestamp, int flags)
{
    return (_read_seek)( context, stream_index, timestamp, flags);
}

int mxd_read_close(void *context)
{
    return (_read_close)( context );
}

