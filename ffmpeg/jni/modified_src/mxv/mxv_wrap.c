#include "mxv_wrap.h"
static pmxv_probe _probe;
static pmxv_read_header _read_header;
static pmxv_read_packet _read_packet;
static pmxv_read_seek _read_seek;
static pmxv_read_close _read_close;

void mxv_demuxer_connect(
        pmxv_probe probe, pmxv_read_header read_header, pmxv_read_packet read_packet,
        pmxv_read_seek read_seek, pmxv_read_close read_close )
{
    _probe = probe;
    _read_header = read_header;
    _read_packet = read_packet;
    _read_seek = read_seek;
    _read_close = read_close;
}

int mxv_probe(const void *probeData)
{
    return (_probe)( probeData );
}

int mxv_read_header(void *context)
{
    return (_read_header)( context );
}

int mxv_read_packet(void *context, void *packet)
{
    return (_read_packet)( context, packet );
}

int mxv_read_seek(void *context, int stream_index, int64_t timestamp, int flags)
{
    return (_read_seek)( context, stream_index, timestamp, flags);
}

int mxv_read_close(void *context)
{
    return (_read_close)( context );
}


static pmxv_init _init;
static pmxv_write_header _write_header;
static pmxv_write_flush_packet _write_flush_packet;
static pmxv_write_trailer _write_trailer;
static pmxv_query_codec _query_codec;
static pmxv_check_bitstream _check_bitstream;

void mxv_muxer_connect(
        pmxv_init init, pmxv_write_header write_header,
        pmxv_write_flush_packet write_flush_packet, pmxv_write_trailer write_trailer,
        pmxv_query_codec query_codec, pmxv_check_bitstream check_bitstream )
{
    _init = init;
    _write_header = write_header;
    _write_flush_packet = write_flush_packet;
    _write_trailer = write_trailer;
    _query_codec = query_codec;
    _check_bitstream = check_bitstream;
}

int mxv_init( void *context )
{
    return (_init)( context );
}

int mxv_write_header( void *context )
{
    return (_write_header)( context );
}

int mxv_write_flush_packet( void *context, void *pkt )
{
    return (_write_flush_packet)( context, pkt );
}

int mxv_write_trailer( void *context )
{
    return (_write_trailer)( context );
}

int mxv_query_codec( int codec_id, int std_compliance )
{
    return (_query_codec)( codec_id, std_compliance );
}

int mxv_check_bitstream( void *context, const void *pkt )
{
    return (_check_bitstream)( context, pkt );
}

