#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pmxv_probe)( const void *probeData );
typedef int (*pmxv_read_header)( void *context );
typedef int (*pmxv_read_packet)( void *context, void *packet );
typedef int (*pmxv_read_seek)( void *context, int stream_index, int64_t timestamp, int flags );
typedef int (*pmxv_read_close)( void *context );

void mxv_connect( pmxv_probe probe, pmxv_read_header read_header, pmxv_read_packet read_packet, pmxv_read_seek read_seek, pmxv_read_close read_close );

int mxv_probe( const void *probeData); 
int mxv_read_header(void *context); 
int mxv_read_packet(void *context, void *packet);
int mxv_read_seek(void *context, int stream_index, int64_t timestamp, int flags);
int mxv_read_close(void *context);

#ifdef __cplusplus
}   // extern "C"
#endif
