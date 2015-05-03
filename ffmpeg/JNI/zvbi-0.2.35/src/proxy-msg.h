/*
 *  libzvbi -- Messages and basic I/O functions between
 *             VBI proxy client & server
 *
 *  Copyright (C) 2003, 2004 Tom Zoerner
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the 
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 *  Boston, MA  02110-1301  USA.
 */
 
/*
 *  $Id: proxy-msg.h,v 1.13 2008/02/19 00:35:21 mschimek Exp $
 *
 *  $Log: proxy-msg.h,v $
 *  Revision 1.13  2008/02/19 00:35:21  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.12  2007/11/27 18:31:07  mschimek
 *  Updated the FSF address in the copyright notice.
 *
 *  Revision 1.11  2007/11/20 21:43:46  tomzo
 *  Improvements and corrections in the proxy API documentation.
 *
 *  Revision 1.10  2007/07/23 20:01:18  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.9  2004/10/24 18:33:47  tomzo
 *  - cleaned up socket I/O interface functions
 *  - added defines for norm change events
 *
 *  Revision 1.8  2004/10/04 20:50:24  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.6  2003/06/07 09:43:08  tomzo
 *  - added new message types MSG_TYPE_DAEMON_PID_REQ,CNF
 *  - added new struct VBIPROXY_MSG: holds message header and body structs
 *
 *  Revision 1.5  2003/06/01 19:36:23  tomzo
 *  Implemented server-side TV channel switching
 *  - implemented messages MSG_TYPE_CHN_CHANGE_REQ/CNF/REJ; IND is still TODO
 *  - removed obsolete PROFILE messages: profile is included with CHN_CHANGE_REQ
 *  Also: added VBI API identifier and device path to CONNECT_CNF (for future use)
 *
 *  Revision 1.4  2003/05/24 12:19:29  tomzo
 *  - added VBIPROXY_SERVICE_REQ/_CNF/_REJ messages
 *  - prepared channel change request and profile request
 *  - renamed MSG_TYPE_DATA_IND into _SLICED_IND in preparation for raw data
 *
 *  Revision 1.3  2003/05/03 12:04:52  tomzo
 *  - added new macro VBIPROXY_ENDIAN_MISMATCH to replace use of swap32()
 *  - added declaration for new func vbi_proxy_msg_set_debug_level()
 *  - fixed copyright headers, added description to file headers
 *
 */

#ifndef PROXY_MSG_H
#define PROXY_MSG_H

#include <sys/syslog.h>

/* Public */

/**
 * @ingroup Proxy
 * @brief Priority levels for channel switching (equivalent to enum v4l2_priority)
 *
 * These priorities are used to cooperativly resolve conflicts between
 * channel requests of multiple capture applications.  While a capture
 * application with a higher priority has opened a device, channel change
 * requests of applications with lower priority will fail with error "EBUSY".
 */
typedef enum
{
        /**
         * Priority level to be used for non-interactive, background data
         * harvesting, i.e. applications which permanently run in the
         * background (e.g. teletext cache, EPG data acquisition)
         */
	VBI_CHN_PRIO_BACKGROUND  = 1,
        /**
         * Interactive (default): should be used when channels are changed
         * on request of the user (e.g. TV viewer, Radio, teletext reader)
         */
	VBI_CHN_PRIO_INTERACTIVE = 2,
        /**
         * Default priority for client which have not (yet) set a priority.
         */
	VBI_CHN_PRIO_DEFAULT     = VBI_CHN_PRIO_INTERACTIVE,
        /**
         * Scheduled recording (e.g. PVR): usually only one application
         * should run at this level (although this is not enforced by
         * the proxy daemon, must be checked by the user or applications)
         */
	VBI_CHN_PRIO_RECORD      = 3

} VBI_CHN_PRIO;

/**
 * @ingroup Proxy
 * @brief Sub-priorities for channel scheduling at "background" priority
 *
 * This enum describes recommended sub-priority levels for channel profiles.
 * They're intended for channel switching through a VBI proxy at background
 * priority level.  The daemon uses this priority to decide which request
 * to grant first if there are multiple outstanding requests.  To the daemon
 * these are just numbers (highest wins) but for successful cooperation
 * clients need to use agree on values for similar tasks. Hence the following
 * values are recommended:
 */
typedef enum
{
        /**
         * Minimal priority level. Client will get channel control only
         * after all other clients.
         */
	VBI_CHN_SUBPRIO_MINIMAL  = 0x00,
        /**
         * After phases "initial" or "check" are completed, clients can use
         * this level to continuously check for change marks.
         */
	VBI_CHN_SUBPRIO_CHECK    = 0x10,
        /**
         * A change in the data transmission has been detected or a long
         * time has passed since the initial reading, so data needs to be
         * read newly.
         */
	VBI_CHN_SUBPRIO_UPDATE   = 0x20,
        /**
         * Initial reading of data after program start (and long pause since
         * last start); once all data is read the client should lower it's
         * priority.
         */
	VBI_CHN_SUBPRIO_INITIAL  = 0x30,
        /**
         * Scanning for VPS/PDC labels to wait for the start of a recording.
         */
	VBI_CHN_SUBPRIO_VPS_PDC  = 0x40

} VBI_CHN_SUBPRIO;

/**
 * @ingroup Proxy
 * @brief Proxy scheduler parameters for background channel switching
 *
 * This structure is passed along with channel change requests for
 * clients with priority @c VBI_CHN_PRIO_BACKGROUND.  The parameters
 * are used by the proxy daemon to share channel control between
 * multiple clients with background priority.
 */
typedef struct
{
        /**
         * Boolean: Ignore contents of this struct unless TRUE
         */
	uint8_t			is_valid;
        /**
         * Sub-priority for channel scheduling at "background" priority.
         * You can use aribtrary values in the range 0 ... 256, but as
         * this value is only meaningful in relation to priorities used
         * by other clients, you should stick to the scale defined by
         * @ref VBI_CHN_SUBPRIO
         */
	uint8_t			sub_prio;
        /**
         * Boolean: Set to FALSE if your capture client needs an
         * atomic time slice (i.e. would need to restart capturing
         * from the beginning it it was interrupted.)
         */
	uint8_t			allow_suspend;

	uint8_t			reserved0;
        /**
         * Minimum time slice your capture client requires. This value
         * is used when multiple clients have the same sub-priority
         * to give all clients channel control in a round-robin manner.
         */
	time_t			min_duration;
        /**
         * Expected duration of use of that channel
         */
	time_t			exp_duration;

	uint8_t			reserved1[16];
} vbi_channel_profile;

/**
 * @ingroup Proxy
 * @brief General flags sent by the proxy daemon to clients during connect
 */
typedef enum
{
        /**
         * Don't drop connection upon timeouts in socket I/O or message response;
         * Intended for debugging, i.e. when remote party runs in a debugger
         */
        VBI_PROXY_DAEMON_NO_TIMEOUTS   = 1<<0

} VBI_PROXY_DAEMON_FLAGS;

/**
 * @ingroup Proxy
 * @brief General flags sent by clients to the proxy daemon during connect
 */
typedef enum
{
        /**
         * Don't drop connection upon timeouts in socket I/O or message response
         * (e.g. when waiting for connect confirm)
         * Intended for debugging, i.e. when remote party runs in a debugger
         */
        VBI_PROXY_CLIENT_NO_TIMEOUTS   = 1<<0,
        /**
         * Suppress sending of channel change and similar indications, i.e. limit
         * messages to slicer data forward and synchronous messages (i.e. RPC reply).
         * Used to make sure that the proxy client socket only becomes readable
         * when data is available for applications which are not proxy-aware.
         */
        VBI_PROXY_CLIENT_NO_STATUS_IND = 1<<1

} VBI_PROXY_CLIENT_FLAGS;

/**
 * @ingroup Proxy
 * @brief Channel notification flags
 */
typedef enum
{
        /**
         * Revoke a previous channel request and return the channel switch
         * token to the daemon.
         */
        VBI_PROXY_CHN_RELEASE = 1<<0,
        /**
         * Return the channel token to the daemon without releasing the
         * channel; This should always be done when the channel switch has
         * been completed to allow faster scheduling in the daemon (i.e. the
         * daemon can grant the token to a different client without having
         * to reclaim it first.)
         */
        VBI_PROXY_CHN_TOKEN   = 1<<1,
        /**
         * Indicate that the channel was changed and VBI buffer queue
         * must be flushed; Should be called as fast as possible after
         * the channel and/or norm was changed.  Note this affects other
         * clients' capturing too, so use with care.  Other clients will
         * be informed about this change by a channel change indication.
         */
        VBI_PROXY_CHN_FLUSH   = 1<<2,
        /**
         * Indicate a norm change.  The new norm should be supplied in
         * the scanning parameter in cae the daemon is not able to
         * determine it from the device directly.
         */
        VBI_PROXY_CHN_NORM    = 1<<3,
        /**
         * Indicate that the client failed to switch the channel because
         * the device was busy. Used to notify the channel scheduler that
         * the current time slice cannot be used by the client.  If the
         * client isn't able to schedule periodic re-attempts it should
         * also return the token.
         */
        VBI_PROXY_CHN_FAIL    = 1<<4,

        VBI_PROXY_CHN_NONE    = 0

} VBI_PROXY_CHN_FLAGS;

/**
 * @ingroup Proxy
 * @brief Identification of the VBI device driver type
 */
typedef enum
{
        /**
         * Unknown device API - only used in error cases. Normally
         * the proxy will always be aware of the driver API as it's
         * determined by the type of capture context creation function
         * used when the device is opened.
         */
        VBI_API_UNKNOWN,
        /**
         * Video4Linux version 1 (i.e. Linux kernels 2.4 or older
         * or old device drivers which have not been ported yet)
         */
        VBI_API_V4L1,
        /**
         * Video4Linux version 2 (i.e. Linux kernels 2.6 and later)
         */
        VBI_API_V4L2,
        /**
         * BSD Brooktree capture driver.
         */
        VBI_API_BKTR
} VBI_DRIVER_API_REV;

/**
 * @ingroup Proxy
 * @brief Proxy protocol version: major, minor and patchlevel
 */
#define VBIPROXY_VERSION                   0x00000100
#define VBIPROXY_COMPAT_VERSION            0x00000100

/* Private */

/* ----------------------------------------------------------------------------
** Declaration of message IDs and the common header struct
*/
typedef enum
{
   MSG_TYPE_CONNECT_REQ,
   MSG_TYPE_CONNECT_CNF,
   MSG_TYPE_CONNECT_REJ,
   MSG_TYPE_CLOSE_REQ,

   MSG_TYPE_SLICED_IND,

   MSG_TYPE_SERVICE_REQ,
   MSG_TYPE_SERVICE_CNF,
   MSG_TYPE_SERVICE_REJ,

   MSG_TYPE_CHN_TOKEN_REQ,
   MSG_TYPE_CHN_TOKEN_CNF,
   MSG_TYPE_CHN_TOKEN_IND,
   MSG_TYPE_CHN_NOTIFY_REQ,
   MSG_TYPE_CHN_NOTIFY_CNF,
   MSG_TYPE_CHN_RECLAIM_REQ,
   MSG_TYPE_CHN_RECLAIM_CNF,
   MSG_TYPE_CHN_SUSPEND_REQ,
   MSG_TYPE_CHN_SUSPEND_CNF,
   MSG_TYPE_CHN_SUSPEND_REJ,
   MSG_TYPE_CHN_IOCTL_REQ,
   MSG_TYPE_CHN_IOCTL_CNF,
   MSG_TYPE_CHN_IOCTL_REJ,
   MSG_TYPE_CHN_CHANGE_IND,

   MSG_TYPE_DAEMON_PID_REQ,
   MSG_TYPE_DAEMON_PID_CNF,

   MSG_TYPE_COUNT

} VBIPROXY_MSG_TYPE;

typedef struct
{
        uint32_t                len;
        uint32_t                type;
} VBIPROXY_MSG_HEADER;

#define VBIPROXY_MAGIC_STR                 "LIBZVBI VBIPROXY"
#define VBIPROXY_MAGIC_LEN                 16
#define VBIPROXY_ENDIAN_MAGIC              0x11223344
#define VBIPROXY_ENDIAN_MISMATCH           0x44332211
#define VBIPROXY_CLIENT_NAME_MAX_LENGTH    64
#define VBIPROXY_DEV_NAME_MAX_LENGTH      128
#define VBIPROXY_ERROR_STR_MAX_LENGTH     128

typedef struct
{
        uint8_t                 protocol_magic[VBIPROXY_MAGIC_LEN];
        uint32_t                protocol_compat_version;
        uint32_t                protocol_version;
        uint32_t                endian_magic;
} VBIPROXY_MAGICS;

typedef struct
{
        VBIPROXY_MAGICS         magics;
        uint8_t                 client_name[VBIPROXY_CLIENT_NAME_MAX_LENGTH];
        int32_t                 pid;
        uint32_t                client_flags;

        uint32_t                scanning;
        uint8_t                 buffer_count;

        uint32_t                services;
        int8_t                  strict;

        uint32_t                reserved[32];   /* set to zero */
} VBIPROXY_CONNECT_REQ;

typedef struct
{
        VBIPROXY_MAGICS         magics;
        uint8_t                 dev_vbi_name[VBIPROXY_DEV_NAME_MAX_LENGTH];
        int32_t                 pid;
        uint32_t                vbi_api_revision;
        uint32_t                daemon_flags;
        uint32_t                services;       /* all services, including raw */
        vbi_raw_decoder         dec;            /* VBI format, e.g. VBI line counts */
        uint32_t                reserved[32];   /* set to zero */
} VBIPROXY_CONNECT_CNF;

typedef struct
{
        VBIPROXY_MAGICS         magics;
        uint8_t                 errorstr[VBIPROXY_ERROR_STR_MAX_LENGTH];
} VBIPROXY_CONNECT_REJ;

typedef struct
{
        double                  timestamp;
        uint32_t                sliced_lines;
        uint32_t                raw_lines;
        union
        {
           vbi_sliced           sliced[1];
           int8_t               raw[1];
        } u;
} VBIPROXY_SLICED_IND;

#define VBIPROXY_RAW_LINE_SIZE        2048
#define VBIPROXY_SLICED_IND_SIZE(S,R) ( sizeof(VBIPROXY_SLICED_IND) \
                                        - sizeof(vbi_sliced) \
                                        + ((S) * sizeof(vbi_sliced)) \
                                        + ((R) * VBIPROXY_RAW_LINE_SIZE) )

typedef struct
{
        uint8_t                 reset;
        uint8_t                 commit;
        int8_t                  strict;
        uint32_t                services;
} VBIPROXY_SERVICE_REQ;

typedef struct
{
        uint32_t                services;       /* all services, including raw */
        vbi_raw_decoder         dec;            /* VBI format, e.g. VBI line counts */
} VBIPROXY_SERVICE_CNF;

typedef struct
{
        uint8_t                 errorstr[VBIPROXY_ERROR_STR_MAX_LENGTH];
} VBIPROXY_SERVICE_REJ;

typedef struct
{
        uint32_t                chn_prio;
        vbi_channel_profile     chn_profile;
} VBIPROXY_CHN_TOKEN_REQ;

typedef struct
{
        vbi_bool                token_ind;      /* piggy-back TOKEN_IND (bg. prio only) */
        vbi_bool                permitted;      /* change allowed by prio (non-bg prio) */
        vbi_bool                non_excl;       /* there are other clients at the same prio */
} VBIPROXY_CHN_TOKEN_CNF;

typedef struct
{
} VBIPROXY_CHN_TOKEN_IND;

typedef struct
{
        VBI_PROXY_CHN_FLAGS     notify_flags;
        uint32_t                scanning;       /* new norm after flush; zero if unknown */
        uint32_t                cause;          /* currently always zero */
        uint8_t                 reserved[32];
} VBIPROXY_CHN_NOTIFY_REQ;

typedef struct
{
        uint32_t                scanning;
        uint8_t                 reserved[32];
} VBIPROXY_CHN_NOTIFY_CNF;

typedef struct
{
        vbi_bool                enable;
        uint32_t                cause;
} VBIPROXY_CHN_SUSPEND_REQ;

typedef struct
{
} VBIPROXY_CHN_SUSPEND_CNF;

typedef struct
{
} VBIPROXY_CHN_SUSPEND_REJ;

typedef struct
{
        uint32_t                request;
        uint32_t                reserved_0;
        uint32_t                reserved_1;
        uint32_t                arg_size;
        uint8_t                 arg_data[0];    /* warning: must have same offset as in CNF message */
} VBIPROXY_CHN_IOCTL_REQ;

#define VBIPROXY_CHN_IOCTL_REQ_SIZE(SIZE) (sizeof(VBIPROXY_CHN_IOCTL_REQ) + (SIZE) - 1)

typedef struct
{
        uint32_t                reserved_0;
        int32_t                 result;
        int32_t                 errcode;
        uint32_t                arg_size;
        uint8_t                 arg_data[0];
} VBIPROXY_CHN_IOCTL_CNF;

#define VBIPROXY_CHN_IOCTL_CNF_SIZE(SIZE) (sizeof(VBIPROXY_CHN_IOCTL_CNF) + (SIZE) - 1)

typedef struct
{
} VBIPROXY_CHN_IOCTL_REJ;

typedef struct
{
} VBIPROXY_CHN_RECLAIM_REQ;

typedef struct
{
} VBIPROXY_CHN_RECLAIM_CNF;

typedef struct
{
        VBI_PROXY_CHN_FLAGS     notify_flags;
        uint32_t                scanning;
        uint8_t                 reserved[32];   /* always zero */
} VBIPROXY_CHN_CHANGE_IND;

typedef struct
{
        VBIPROXY_MAGICS         magics;
} VBIPROXY_DAEMON_PID_REQ;

typedef struct
{
        VBIPROXY_MAGICS         magics;
        int32_t                 pid;
} VBIPROXY_DAEMON_PID_CNF;

typedef union
{
        VBIPROXY_CONNECT_REQ            connect_req;
        VBIPROXY_CONNECT_CNF            connect_cnf;
        VBIPROXY_CONNECT_REJ            connect_rej;

        VBIPROXY_SLICED_IND             sliced_ind;

        VBIPROXY_SERVICE_REQ            service_req;
        VBIPROXY_SERVICE_CNF            service_cnf;
        VBIPROXY_SERVICE_REJ            service_rej;

        VBIPROXY_CHN_TOKEN_REQ          chn_token_req;
        VBIPROXY_CHN_TOKEN_CNF          chn_token_cnf;
        VBIPROXY_CHN_TOKEN_IND          chn_token_ind;
        VBIPROXY_CHN_RECLAIM_REQ        chn_reclaim_req;
        VBIPROXY_CHN_RECLAIM_CNF        chn_reclaim_cnf;
        VBIPROXY_CHN_NOTIFY_REQ         chn_notify_req;
        VBIPROXY_CHN_NOTIFY_CNF         chn_notify_cnf;
        VBIPROXY_CHN_SUSPEND_REQ        chn_suspend_req;
        VBIPROXY_CHN_SUSPEND_CNF        chn_suspend_cnf;
        VBIPROXY_CHN_SUSPEND_REJ        chn_suspend_rej;
        VBIPROXY_CHN_IOCTL_REQ          chn_ioctl_req;
        VBIPROXY_CHN_IOCTL_CNF          chn_ioctl_cnf;
        VBIPROXY_CHN_IOCTL_REJ          chn_ioctl_rej;
        VBIPROXY_CHN_CHANGE_IND         chn_change_ind;

        VBIPROXY_DAEMON_PID_REQ         daemon_pid_req;
        VBIPROXY_DAEMON_PID_CNF         daemon_pid_cnf;

} VBIPROXY_MSG_BODY;

typedef struct
{
        VBIPROXY_MSG_HEADER             head;
        VBIPROXY_MSG_BODY               body;
} VBIPROXY_MSG;

#define VBIPROXY_MSG_BODY_OFFSET   ((long)&(((VBIPROXY_MSG*)NULL)->body))

/* ----------------------------------------------------------------------------
** Declaration of the IO state struct
*/
typedef struct
{
        int                     sock_fd;        /* socket file handle or -1 if closed */
        time_t                  lastIoTime;     /* timestamp of last i/o (for timeouts) */

        uint32_t                writeLen;       /* number of bytes in write buffer, including header */
        uint32_t                writeOff;       /* number of already written bytes, including header */
        VBIPROXY_MSG          * pWriteBuf;      /* data to be written */
        vbi_bool                freeWriteBuf;   /* TRUE if the buffer shall be freed by the I/O handler */

        uint32_t                readLen;        /* length of incoming message (including itself) */
        uint32_t                readOff;        /* number of already read bytes */
} VBIPROXY_MSG_STATE;

/* ----------------------------------------------------------------------------
** Declaration of the service interface functions
*/
void     vbi_proxy_msg_logger( int level, int clnt_fd, int errCode, const char * pText, ... );
void     vbi_proxy_msg_set_logging( vbi_bool do_logtty, int sysloglev,
                                    int fileloglev, const char * pLogfileName );
void     vbi_proxy_msg_set_debug_level( int level );
const char * vbi_proxy_msg_debug_get_type_str( VBIPROXY_MSG_TYPE type );

vbi_bool vbi_proxy_msg_read_idle( VBIPROXY_MSG_STATE * pIO );
vbi_bool vbi_proxy_msg_write_idle( VBIPROXY_MSG_STATE * pIO );
vbi_bool vbi_proxy_msg_is_idle( VBIPROXY_MSG_STATE * pIO );
vbi_bool vbi_proxy_msg_check_timeout( VBIPROXY_MSG_STATE * pIO, time_t now );
vbi_bool vbi_proxy_msg_handle_write( VBIPROXY_MSG_STATE * pIO, vbi_bool * pBlocked );
void     vbi_proxy_msg_close_read( VBIPROXY_MSG_STATE * pIO );
vbi_bool vbi_proxy_msg_handle_read( VBIPROXY_MSG_STATE * pIO,
                                    vbi_bool * pBlocked, vbi_bool closeOnZeroRead,
                                    VBIPROXY_MSG * pReadBuf, int max_read_len );
void     vbi_proxy_msg_close_io( VBIPROXY_MSG_STATE * pIO );
void     vbi_proxy_msg_fill_magics( VBIPROXY_MAGICS * p_magic );
void     vbi_proxy_msg_write( VBIPROXY_MSG_STATE * p_io, VBIPROXY_MSG_TYPE type,
                              uint32_t msgLen, VBIPROXY_MSG * pMsg, vbi_bool freeBuf );

int      vbi_proxy_msg_listen_socket( vbi_bool is_tcp_ip, const char * listen_ip, const char * listen_port );
void     vbi_proxy_msg_stop_listen( vbi_bool is_tcp_ip, int sock_fd, char * pSrvPort );
int      vbi_proxy_msg_accept_connection( int listen_fd );
char   * vbi_proxy_msg_get_socket_name( const char * p_dev_name );
vbi_bool vbi_proxy_msg_check_connect( const char * p_sock_path );
int      vbi_proxy_msg_connect_to_server( vbi_bool use_tcp_ip, const char * pSrvHost, const char * pSrvPort, char ** ppErrorText );
vbi_bool vbi_proxy_msg_finish_connect( int sock_fd, char ** ppErrorText );

int      vbi_proxy_msg_check_ioctl( VBI_DRIVER_API_REV vbi_api,
                                    int request, void * p_arg, vbi_bool * req_perm );

#endif  /* PROXY_MSG_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
