#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int ( *pusb_open )( void* context, const char* url, int flags );
typedef int ( *pusb_read )( void* context, unsigned char* buf, int size );
typedef int ( *pusb_write )( void* context, const unsigned char* buf, int size );
typedef int64_t ( *pusb_seek )( void* context, int64_t pos, int whence );
typedef int ( *pusb_close )( void* context );
typedef int ( *pusb_open_dir )( void* context);
typedef int ( *pusb_read_dir )( void* context, void** next );
typedef int ( *pusb_close_dir )( void* context );
typedef int ( *pusb_delete )( void* context );
typedef int ( *pusb_move )( void* src, void* dst );

void usb_connect( pusb_open open, pusb_read read, pusb_write write, pusb_seek seek,
                  pusb_close close, pusb_open_dir open_dir, pusb_read_dir read_dir,
                  pusb_close_dir close_dir, pusb_delete delete_, pusb_move move );

int usb_open( void* context, const char* url, int flags );
int usb_read( void* context, unsigned char* buf, int size );
int usb_write( void* context, const unsigned char* buf, int size );
int64_t usb_seek( void* context, int64_t pos, int whence );
int usb_close( void* context );
int usb_open_dir( void* context);
int usb_read_dir( void* context, void** next );
int usb_close_dir( void* context );
int usb_delete( void* context );
int usb_move( void* src, void* dst );

#ifdef __cplusplus
}   // extern "C"
#endif