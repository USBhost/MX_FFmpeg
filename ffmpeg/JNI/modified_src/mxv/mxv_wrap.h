#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pmxv_probe)( const void *probeData );
typedef int (*pmxv_read_header)( void *context );
typedef int (*pmxv_read_packet)( void *context, void *packet );
typedef int (*pmxv_read_seek)( void *context, int stream_index, int64_t timestamp, int flags );
typedef int (*pmxv_read_close)( void *context );

void mxv_demuxer_connect(
                  pmxv_probe probe, pmxv_read_header read_header, pmxv_read_packet read_packet,
                  pmxv_read_seek read_seek, pmxv_read_close read_close );

int mxv_probe( const void *probeData);
int mxv_read_header(void *context);
int mxv_read_packet(void *context, void *packet);
int mxv_read_seek(void *context, int stream_index, int64_t timestamp, int flags);
int mxv_read_close(void *context);

typedef int (*pmxv_init)( void *context );
typedef int (*pmxv_write_header)( void *context );
typedef int (*pmxv_write_flush_packet)( void *context, void *pkt );
typedef int (*pmxv_write_trailer)( void *context );
typedef int (*pmxv_query_codec)( int codec_id, int std_compliance );
typedef int (*pmxv_check_bitstream)( void *context, const void *pkt );

void mxv_muxer_connect(
                  pmxv_init init, pmxv_write_header write_header,
                  pmxv_write_flush_packet write_flush_packet, pmxv_write_trailer write_trailer,
                  pmxv_query_codec query_codec, pmxv_check_bitstream check_bitstream );


int mxv_init( void *context );
int mxv_write_header( void *context );
int mxv_write_flush_packet( void *context, void *pkt );
int mxv_write_trailer( void *context );
int mxv_query_codec( int codec_id, int std_compliance );
int mxv_check_bitstream( void *context, const void *pkt );

#ifdef __cplusplus
}   // extern "C"
#endif
