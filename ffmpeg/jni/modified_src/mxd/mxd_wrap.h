#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pmxd_read_probe)( const void *probeData );
typedef int (*pmxd_read_header)( void *context );
typedef int (*pmxd_read_packet)( void *context, void *packet );
typedef int (*pmxd_read_seek)( void *context, int stream_index, int64_t timestamp, int flags );
typedef int (*pmxd_read_close)( void *context );

void mxd_connect( pmxd_read_probe probe, pmxd_read_header read_header, pmxd_read_packet read_packet, pmxd_read_seek read_seek, pmxd_read_close read_close );

int mxd_read_probe( const void *probeData);
int mxd_read_header(void *context);
int mxd_read_packet(void *context, void *packet);
int mxd_read_seek(void *context, int stream_index, int64_t timestamp, int flags);
int mxd_read_close(void *context);

#ifdef __cplusplus
}   // extern "C"
#endif
