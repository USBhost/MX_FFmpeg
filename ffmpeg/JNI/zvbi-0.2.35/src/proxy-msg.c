/*
 *  libvbi -- Basic I/O between VBI proxy client & server
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
 *  Description:
 *
 *    This module contains a collection of functions for lower-level
 *    socket I/O which are shared between proxy daemon and clients.
 *    Error output is different for daemon and clients: daemon logs
 *    to a file or syslog facility, while the client returns error
 *    strings to the caller, which can be passed to the upper levels
 *    (e.g. the user interface)
 *
 *    Both UNIX domain and IPv4 and IPv6 sockets are implemented, but
 *    the latter ones are currently not officially supported.
 */
 
/*
 *  $Id: proxy-msg.c,v 1.20 2008/02/19 00:35:21 mschimek Exp $
 *
 *  $Log: proxy-msg.c,v $
 *  Revision 1.20  2008/02/19 00:35:21  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.19  2007/11/27 18:31:07  mschimek
 *  Updated the FSF address in the copyright notice.
 *
 *  Revision 1.18  2007/08/27 10:17:50  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.17  2007/08/27 06:44:40  mschimek
 *  vbi_proxy_msg_get_local_socket_addr, vbi_proxy_msg_accept_connection,
 *  vbi_proxy_msg_resolve_symlinks: Replaced strncpy() by the faster a
 *  safer strlcpy().
 *  vbi_proxy_msg_logger, vbi_proxy_msg_accept_connection: Replaced
 *  sprintf() by the safer snprintf().
 *
 *  Revision 1.16  2007/07/23 20:01:18  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.15  2006/05/22 09:08:46  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.14  2006/02/10 06:25:37  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.13  2004/12/30 02:25:29  mschimek
 *  printf ptrdiff_t fixes.
 *
 *  Revision 1.12  2004/12/13 07:17:09  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.11  2004/10/25 16:56:29  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.10  2004/10/24 18:33:47  tomzo
 *  - cleaned up socket I/O interface functions
 *  - added defines for norm change events
 *
 *  Revision 1.7  2003/06/07 09:42:53  tomzo
 *  Optimized message writing to socket in vbi_proxy_msg_handle_io():
 *  - keep message header and body in one struct VBIPROXY_MSG (for both read and
 *    write) to be able to write it in complete to the pipe in one syscall
 *  - before, the client usually was woken up after only the header was sent, i.e.
 *    only a partial message was available for reading; this was problematic for
 *    clients which polled the socket with a zero timeout, and is solved now.
 *
 *  Revision 1.6  2003/06/01 19:36:09  tomzo
 *  Optimization of read message handling:
 *  - use static buffer to read messages instead of dynamic malloc()
 *  - added pointer to read buffer as parameters to _handle_io()
 *
 *  Revision 1.5  2003/05/24 12:19:11  tomzo
 *  - renamed MSG_TYPE_DATA_IND into _SLICED_IND in preparation for raw data
 *
 *  Revision 1.4  2003/05/10 13:30:51  tomzo
 *  Reduced default debug level of proxy_msg_trace from 1 to 0
 *
 *  Revision 1.3  2003/05/03 12:05:26  tomzo
 *  - use new function vbi_proxy_msg_resolve_symlinks() to get unique device path,
 *    e.g. allow both /dev/vbi and /dev/vbi0 to work as proxy device args
 *  - added new func vbi_proxy_msg_set_debug_level()
 *  - fixed debug output level in various dprintf statements
 *  - fixed copyright headers, added description to file headers
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef ENABLE_PROXY

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <syslog.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "bcd.h"
#include "vbi.h"
#include "io.h"
#include "misc.h"
#include "proxy-msg.h"

#ifdef ENABLE_V4L2
#include <asm/types.h>		/* __u8 and friends for videodev2k.h */
#ifndef HAVE_S64_U64
#  include <inttypes.h>
  /* Linux 2.6.x asm/types.h defines __s64 and __u64 only
     if __GNUC__ is defined. */
typedef int64_t __s64;
typedef uint64_t __u64;
#endif
#include "videodev2k.h"
#endif
#ifdef ENABLE_V4L
#include "videodev.h"
#endif

#define dprintf1(fmt, arg...)    do {if (proxy_msg_trace >= 1) fprintf(stderr, "proxy_msg: " fmt, ## arg);} while(0)
#define dprintf2(fmt, arg...)    do {if (proxy_msg_trace >= 2) fprintf(stderr, "proxy_msg: " fmt, ## arg);} while(0)
static int proxy_msg_trace = 0;


/* settings for log output - only used by the daemon */
static struct
{
   vbi_bool  do_logtty;
   int       sysloglev;
   int       fileloglev;
   char    * pLogfileName;
} proxy_msg_logcf =
{
   FALSE, 0, 0, NULL
};

/* ----------------------------------------------------------------------------
** Local settings
*/
#define SRV_IO_TIMEOUT             60
#define SRV_LISTEN_BACKLOG_LEN     10
#define SRV_CLNT_SOCK_BASE_PATH    "/tmp/vbiproxy"

/* ----------------------------------------------------------------------------
** Append entry to logfile
*/
void vbi_proxy_msg_logger( int level, int clnt_fd, int errCode, const char * pText, ... )
{
   va_list argl;
   char timestamp[32], fdstr[20];
   const char *argv[10];
   uint32_t argc, idx;
   int fd;
   time_t now = time(NULL);

   if (pText != NULL)
   {
      /* open the logfile, if one is configured */
      if ( (level <= proxy_msg_logcf.fileloglev) &&
           (proxy_msg_logcf.pLogfileName != NULL) )
      {
         fd = open(proxy_msg_logcf.pLogfileName, O_WRONLY|O_CREAT|O_APPEND, 0666);
         if (fd >= 0)
         {  /* each line in the file starts with a timestamp */
            strftime(timestamp, sizeof(timestamp) - 1, "[%d/%b/%Y:%H:%M:%S +0000] ", gmtime(&now));
            write(fd, timestamp, strlen(timestamp));
         }
      }
      else
         fd = -1;

      if (proxy_msg_logcf.do_logtty && (level <= LOG_WARNING))
         fprintf(stderr, "vbiproxy: ");

      argc = 0;
      memset(argv, 0, sizeof(argv));
      /* add pointer to file descriptor (for client requests) or pid (for general infos) */
      if (clnt_fd != -1)
         snprintf(fdstr, sizeof (fdstr), "fd %d: ", clnt_fd);
      else
      {
         snprintf(fdstr, sizeof (fdstr), "pid %d: ", (int)getpid());
      }
      argv[argc++] = fdstr;

      /* add pointer to first log output string */
      argv[argc++] = pText;

      /* append pointers to the rest of the log strings */
      va_start(argl, pText);
      while ((argc < 5) && ((pText = va_arg(argl, char *)) != NULL))
      {
         argv[argc++] = pText;
      }
      va_end(argl);

      /* add system error message */
      if (errCode != 0)
      {
         argv[argc++] = strerror(errCode);
      }

      /* print the strings to the file and/or stderr */
      for (idx=0; idx < argc; idx++)
      {
         if (fd >= 0)
            write(fd, argv[idx], strlen(argv[idx]));
         if (proxy_msg_logcf.do_logtty && (level <= LOG_WARNING))
            fprintf(stderr, "%s", argv[idx]);
      }

      /* terminate the line with a newline character and close the file */
      if (fd >= 0)
      {
         write(fd, "\n", 1);
         close(fd);
      }
      if (proxy_msg_logcf.do_logtty && (level <= LOG_WARNING))
      {
         fprintf(stderr, "\n");
         fflush(stderr);
      }

      /* syslog output */
      if (level <= proxy_msg_logcf.sysloglev)
      {
         switch (argc)
         {
            case 1: syslog(level, "%s", argv[0]); break;
            case 2: syslog(level, "%s%s", argv[0], argv[1]); break;
            case 3: syslog(level, "%s%s%s", argv[0], argv[1],argv[2]); break;
            case 4: syslog(level, "%s%s%s%s", argv[0], argv[1],argv[2],argv[3]); break;
         }
      }
   }
}

/* ----------------------------------------------------------------------------
** Set parameters for event logging
** - loglevel usage
**   ERR    : fatal errors (which lead to program termination)
**   WARNING: this shouldn't happen error (OS failure or internal errors)
**   NOTICE : start/stop of the daemon
**   INFO   : connection establishment and shutdown
*/
void vbi_proxy_msg_set_logging( vbi_bool do_logtty, int sysloglev,
                                int fileloglev, const char * pLogfileName )
{
   /* free the memory allocated for the old config strings */
   if (proxy_msg_logcf.pLogfileName != NULL)
   {
      free(proxy_msg_logcf.pLogfileName);
      proxy_msg_logcf.pLogfileName = NULL;
   }

   proxy_msg_logcf.do_logtty = do_logtty;

   /* make a copy of the new config strings */
   if (pLogfileName != NULL)
   {
      proxy_msg_logcf.pLogfileName = malloc(strlen(pLogfileName) + 1);
      strcpy(proxy_msg_logcf.pLogfileName, pLogfileName);
      proxy_msg_logcf.fileloglev = ((fileloglev > 0) ? (fileloglev + LOG_ERR) : -1);
   }
   else
      proxy_msg_logcf.fileloglev = -1;

   if (sysloglev && !proxy_msg_logcf.sysloglev)
   {
      openlog("vbiproxy", LOG_PID, LOG_DAEMON);
   }
   else if (!sysloglev && proxy_msg_logcf.sysloglev)
   {
   }

   /* convert GUI log-level setting to syslog enum value */
   proxy_msg_logcf.sysloglev = ((sysloglev > 0) ? (sysloglev + LOG_ERR) : -1);
}

/* ----------------------------------------------------------------------------
** Enable debug output
*/
void vbi_proxy_msg_set_debug_level( int level )
{
   proxy_msg_trace = level;
}

/* ----------------------------------------------------------------------------
** Print mesage type name
** - must be kept in sync with message type enum
*/
const char * vbi_proxy_msg_debug_get_type_str( VBIPROXY_MSG_TYPE type )
{
   const char * pTypeStr;
   static const struct
   {
      VBIPROXY_MSG_TYPE  type;
      const char * const name;
   } names[] =
   {
#define DEBUG_STR_MSG_TYPE(TYPE)  { TYPE, #TYPE + 9},
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CONNECT_REQ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CONNECT_CNF)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CONNECT_REJ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CLOSE_REQ)

      DEBUG_STR_MSG_TYPE(MSG_TYPE_SLICED_IND)

      DEBUG_STR_MSG_TYPE(MSG_TYPE_SERVICE_REQ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_SERVICE_CNF)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_SERVICE_REJ)

      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_TOKEN_REQ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_TOKEN_CNF)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_TOKEN_IND)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_NOTIFY_REQ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_NOTIFY_CNF)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_RECLAIM_REQ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_RECLAIM_CNF)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_SUSPEND_REQ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_SUSPEND_CNF)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_SUSPEND_REJ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_IOCTL_REQ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_IOCTL_CNF)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_IOCTL_REJ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_CHN_CHANGE_IND)

      DEBUG_STR_MSG_TYPE(MSG_TYPE_DAEMON_PID_REQ)
      DEBUG_STR_MSG_TYPE(MSG_TYPE_DAEMON_PID_CNF)
#undef DEBUG_STR_MSG_TYPE
   };
   assert(MSG_TYPE_COUNT == (sizeof(names)/sizeof(names[0])));

   if (type < MSG_TYPE_COUNT)
   {
      assert(names[type].type == type);
      pTypeStr = names[type].name;
   }
   else
      pTypeStr = "*INVALID*";

   return pTypeStr;
}

/* ----------------------------------------------------------------------------
** Check for incomplete read or write buffer
*/
vbi_bool vbi_proxy_msg_read_idle( VBIPROXY_MSG_STATE * pIO )
{
   assert((pIO->readOff == 0) || (pIO->readOff == pIO->readLen));

   return (pIO->readOff == 0);
}

vbi_bool vbi_proxy_msg_write_idle( VBIPROXY_MSG_STATE * pIO )
{
   return (pIO->writeLen == 0);
}

vbi_bool vbi_proxy_msg_is_idle( VBIPROXY_MSG_STATE * pIO )
{
   assert((pIO->readOff == 0) || (pIO->readOff == pIO->readLen));

   return ((pIO->writeLen == 0) && (pIO->readOff == 0));
}

void vbi_proxy_msg_close_read( VBIPROXY_MSG_STATE * pIO )
{
   assert((pIO->readOff == 0) || (pIO->readOff == pIO->readLen));

   pIO->readOff = 0;
   pIO->readLen = 0;
}

/* ----------------------------------------------------------------------------
** Check for I/O timeout
** - returns TRUE in case of timeout
*/
vbi_bool vbi_proxy_msg_check_timeout( VBIPROXY_MSG_STATE * pIO, time_t now )
{
   return ( (now > pIO->lastIoTime + SRV_IO_TIMEOUT) &&
            (vbi_proxy_msg_is_idle(pIO) == FALSE) );
}

/* ----------------------------------------------------------------------------
** Write a message to the socket
** - write(2) is called only once per call to this function
** - after errors the I/O state (indicated by FALSE result) is not reset, because
**   the caller is expected to close the connection.
*/
vbi_bool vbi_proxy_msg_handle_write( VBIPROXY_MSG_STATE * pIO, vbi_bool * pBlocked )
{
   ssize_t  len;
   vbi_bool result = TRUE;

   assert(pIO->writeLen >= sizeof(VBIPROXY_MSG_HEADER));
   assert(pIO->writeOff < pIO->writeLen);

   *pBlocked = FALSE;

   len = send(pIO->sock_fd, ((char *)pIO->pWriteBuf) + pIO->writeOff,
                            pIO->writeLen - pIO->writeOff, 0);
   if (len > 0)
   {
      pIO->lastIoTime = time(NULL);
      pIO->writeOff  += len;

      if (pIO->writeOff >= pIO->writeLen)
      {  /* all data has been written -> free the buffer; reset write state */
         if (pIO->freeWriteBuf)
            free(pIO->pWriteBuf);
         pIO->freeWriteBuf = FALSE;
         pIO->pWriteBuf = NULL;
         pIO->writeLen = 0;
      }
      else
      {  /* not all data could be written */
         *pBlocked = TRUE;
      }
   }
   else if (len < 0)
   {
      if ((errno != EAGAIN) && (errno != EINTR))
      {  /* network error -> close the connection */
         dprintf1("handle_io: write error on fd %d: %s\n", pIO->sock_fd, strerror(errno));
         result = FALSE;
      }
      else if (errno == EAGAIN)
      {
         *pBlocked = TRUE;
      }
   }
   else /* if (len == 0) */
   {  /* no data was written (actually in this case -1/EAGAIN should be returned) */
      *pBlocked = TRUE;
   }

   return result;
}

/* ----------------------------------------------------------------------------
** Read a message from the network socket
** - read(2) is called only once per call to this function
** - reading is done in 2 phases: first the length of the message is read into
**   a small buffer; then a buffer is allocated for the complete message and the
**   length variable copied into it and the rest of the message read afterwords.
** - a closed network connection is indicated by a 0 read from a readable socket.
**   Readability is indicated by the select syscall and passed here via
**   parameter closeOnZeroRead.
** - after errors the I/O state (indicated by FALSE result) is not reset, because
**   the caller is expected to close the connection.
*/
vbi_bool vbi_proxy_msg_handle_read( VBIPROXY_MSG_STATE * pIO,
                                    vbi_bool * pBlocked, vbi_bool closeOnZeroRead,
                                    VBIPROXY_MSG * pReadBuf, int max_read_len )
{
   ssize_t  len;
   vbi_bool err;
   time_t   now = time(NULL);
   vbi_bool result = TRUE;

   assert(pIO->writeLen == 0);

   if (pReadBuf != NULL)
   {
      err = FALSE;
      len = 0;  /* compiler dummy */

      if (pIO->readOff < sizeof(VBIPROXY_MSG_HEADER))
      {  /* in read phase one: read the message length */
         assert (pIO->readLen == 0);

         len = recv(pIO->sock_fd, (char *)pReadBuf + pIO->readOff,
                                  sizeof(VBIPROXY_MSG_HEADER) - pIO->readOff, 0);
         if (len > 0)
         {
            closeOnZeroRead = FALSE;
            pIO->lastIoTime = now;
            pIO->readOff += len;
            if (pIO->readOff >= sizeof(VBIPROXY_MSG_HEADER))
            {  /* message length variable has been read completely */
               /* convert from network byte order (big endian) to host byte order */
               pIO->readLen = ntohl(pReadBuf->head.len);
               pReadBuf->head.len  = pIO->readLen;
               pReadBuf->head.type = ntohl(pReadBuf->head.type);
               /* dprintf1("handle_io: fd %d: new block: size %d\n", pIO->sock_fd, pIO->readLen); */
               if ((pIO->readLen > (size_t) max_read_len) ||
                   (pIO->readLen < sizeof(VBIPROXY_MSG_HEADER)))
               {
                  /* illegal message size -> protocol error */
                  dprintf1("handle_io: fd %d: illegal block size %d: "
		           "outside limits [%ld..%ld]\n",
			   pIO->sock_fd, pIO->readLen,
			   (long) sizeof(VBIPROXY_MSG_HEADER),
			   max_read_len + (long) sizeof(VBIPROXY_MSG_HEADER));
                  result = FALSE;
               }
            }
            else
               *pBlocked = TRUE;
         }
         else
            err = TRUE;
      }

      if ((err == FALSE) && (pIO->readOff >= sizeof(VBIPROXY_MSG_HEADER)))
      {  /* in read phase two: read the complete message into the allocated buffer */
         assert (pIO->readLen <= (size_t) max_read_len);

         len = recv(pIO->sock_fd, (char*)pReadBuf + pIO->readOff,
                                  pIO->readLen - pIO->readOff, 0);
         if (len > 0)
         {
            pIO->lastIoTime = now;
            pIO->readOff += len;
         }
         else
            err = TRUE;
      }

      if (err == FALSE)
      {
         if (pIO->readOff < pIO->readLen)
         {  /* not all data has been read yet */
            *pBlocked = TRUE;
         }
      }
      else
      {
         if ((len == 0) && closeOnZeroRead)
         {  /* zero bytes read after select returned readability -> network error or connection closed by peer */
            dprintf1("handle_io: zero len read on fd %d\n", (int) pIO->sock_fd);
            errno  = ECONNRESET;
            result = FALSE;
         }
         else if ((len < 0) && (errno != EAGAIN) && (errno != EINTR))
         {  /* network error -> close the connection */
   	    dprintf1("handle_io: read error on fd %d: len=%ld, %s\n",
		     pIO->sock_fd, (long) len, strerror(errno));
            result = FALSE;
         }
         else if (errno == EAGAIN)
         {
            *pBlocked = TRUE;
         }
      }
   }

   return result;
}

/* ----------------------------------------------------------------------------
** Free resources allocated for IO
*/
void vbi_proxy_msg_close_io( VBIPROXY_MSG_STATE * pIO )
{
   if (pIO->sock_fd != -1)
   {
      close(pIO->sock_fd);
      pIO->sock_fd = -1;
   }

   if (pIO->pWriteBuf != NULL)
   {
      if (pIO->freeWriteBuf)
         free(pIO->pWriteBuf);
      pIO->pWriteBuf = NULL;
   }
}

/* ----------------------------------------------------------------------------
** Fill a magic header struct with protocol constants
*/
void vbi_proxy_msg_fill_magics( VBIPROXY_MAGICS * p_magic )
{
   memcpy(p_magic->protocol_magic, VBIPROXY_MAGIC_STR, VBIPROXY_MAGIC_LEN);
   p_magic->protocol_compat_version = VBIPROXY_COMPAT_VERSION;
   p_magic->protocol_version = VBIPROXY_VERSION;
   p_magic->endian_magic = VBIPROXY_ENDIAN_MAGIC;
}

/* ----------------------------------------------------------------------------
** Create a new message and prepare the I/O state for writing
** - length and pointer of the body may be zero (no payload)
*/
void vbi_proxy_msg_write( VBIPROXY_MSG_STATE * p_io, VBIPROXY_MSG_TYPE type,
                          uint32_t msgLen, VBIPROXY_MSG * pMsg, vbi_bool freeBuf )
{
   assert((p_io->readOff == 0) && (p_io->readLen == 0));  /* I/O must be idle */
   assert(p_io->writeLen == 0);
   assert((msgLen == 0) || (pMsg != NULL));

   dprintf2("write: len %ld, msg type %d (%s)\n",
            (long) sizeof(VBIPROXY_MSG_HEADER) + msgLen, type,
	    vbi_proxy_msg_debug_get_type_str(type));

   p_io->pWriteBuf    = pMsg;
   p_io->freeWriteBuf = freeBuf;
   p_io->writeLen     = sizeof(VBIPROXY_MSG_HEADER) + msgLen;
   p_io->writeOff     = 0;
   p_io->lastIoTime   = time(NULL);

   /* message header: length is coded in network byte order (i.e. big endian) */
   pMsg->head.len     = htonl(p_io->writeLen);
   pMsg->head.type    = htonl(type);
}

/* ----------------------------------------------------------------------------
** Implementation of the C library address handling functions
** - for platforms which to not have them in libc
** - documentation see the manpages
*/
#ifndef HAVE_GETADDRINFO

#ifndef AI_PASSIVE
# define AI_PASSIVE 1
#endif

struct addrinfo
{
   int  ai_flags;
   int  ai_family;
   int  ai_socktype;
   int  ai_protocol;
   struct sockaddr * ai_addr;
   int  ai_addrlen;
};

enum
{
   GAI_UNSUP_FAM       = -1,
   GAI_NO_SERVICE_NAME = -2,
   GAI_UNKNOWN_SERVICE = -3,
   GAI_UNKNOWN_HOST    = -4,
};

static int getaddrinfo( const char * pHostName, const char * pServiceName, const struct addrinfo * pInParams, struct addrinfo ** ppResult )
{
   struct servent  * pServiceEntry;
   struct hostent  * pHostEntry;
   struct addrinfo * res;
   char  * pServiceNumEnd;
   uint32_t  port;
   int   result;

   res = malloc(sizeof(struct addrinfo));
   *ppResult = res;

   memset(res, 0, sizeof(*res));
   res->ai_socktype  = pInParams->ai_socktype;
   res->ai_family    = pInParams->ai_family;
   res->ai_protocol  = pInParams->ai_protocol;

   if (pInParams->ai_family == PF_INET)
   {
      if ((pServiceName != NULL) || (*pServiceName == 0))
      {
         port = strtol(pServiceName, &pServiceNumEnd, 0);
         if (*pServiceNumEnd != 0)
         {
            pServiceEntry = getservbyname(pServiceName, "tcp");
            if (pServiceEntry != NULL)
               port = ntohs(pServiceEntry->s_port);
            else
               port = 0;
         }

         if (port != 0)
         {
            if (pHostName != NULL)
               pHostEntry = gethostbyname(pHostName);
            else
               pHostEntry = NULL;

            if ((pHostName == NULL) || (pHostEntry != NULL))
            {
               struct sockaddr_in * iad;

               iad = malloc(sizeof(struct sockaddr_in));
               res->ai_addr    = (struct sockaddr *) iad;
               res->ai_addrlen = sizeof(struct sockaddr_in);

               iad->sin_family      = AF_INET;
               iad->sin_port        = htons(port);
               if (pHostName != NULL)
                  memcpy(&iad->sin_addr, (char *) pHostEntry->h_addr, pHostEntry->h_length);
               else
                  iad->sin_addr.s_addr = INADDR_ANY;
               result = 0;
            }
            else
               result = GAI_UNKNOWN_HOST;
         }
         else
            result = GAI_UNKNOWN_SERVICE;
      }
      else
         result = GAI_NO_SERVICE_NAME;
   }
   else
      result = GAI_UNSUP_FAM;

   if (result != 0)
   {
      free(res);
      *ppResult = NULL;
   }
   return result;
}

static void freeaddrinfo( struct addrinfo * res )
{
   if (res->ai_addr != NULL)
      free(res->ai_addr);
   free(res);
}

static char * gai_strerror( int errCode )
{
   switch (errCode)
   {
      case GAI_UNSUP_FAM:       return "unsupported protocol family";
      case GAI_NO_SERVICE_NAME: return "missing service name or port number for TCP/IP";
      case GAI_UNKNOWN_SERVICE: return "unknown service name";
      case GAI_UNKNOWN_HOST:    return "unknown host";
      default:                  return "internal or unknown error";
   }
}
#endif  /* HAVE_GETADDRINFO */

/* ----------------------------------------------------------------------------
** Get socket address for PF_UNIX aka PF_LOCAL address family
** - result is in the same format as from getaddrinfo
** - note: Linux getaddrinfo currently supports PF_UNIX queries too, however
**   this feature is not standardized and hence not portable (e.g. to NetBSD)
*/
static int vbi_proxy_msg_get_local_socket_addr( const char * pPathName, const struct addrinfo * pInParams, struct addrinfo ** ppResult )
{
   struct addrinfo * res;
   struct sockaddr_un * saddr;

   if ((pInParams->ai_family == PF_UNIX) && (pPathName != NULL))
   {
      /* note: use regular malloc instead of malloc in case memory is freed by the libc internal freeaddrinfo */
      res = malloc(sizeof(struct addrinfo));
      *ppResult = res;

      memset(res, 0, sizeof(*res));
      res->ai_socktype  = pInParams->ai_socktype;
      res->ai_family    = pInParams->ai_family;
      res->ai_protocol  = pInParams->ai_protocol;

      saddr = malloc(sizeof(struct sockaddr_un));
      res->ai_addr      = (struct sockaddr *) saddr;
      res->ai_addrlen   = sizeof(struct sockaddr_un);

      strlcpy(saddr->sun_path, pPathName, sizeof(saddr->sun_path) - 1);
      saddr->sun_path[sizeof(saddr->sun_path) - 1] = 0;
      saddr->sun_family = AF_UNIX;

      return 0;
   }
   else
      return -1;
}

/* ----------------------------------------------------------------------------
** Open socket for listening
*/
int vbi_proxy_msg_listen_socket( vbi_bool is_tcp_ip, const char * listen_ip, const char * listen_port )
{
   struct addrinfo    ask, *res;
   int  opt, rc;
   int  sock_fd;
   vbi_bool result = FALSE;

   memset(&ask, 0, sizeof(ask));
   ask.ai_flags    = AI_PASSIVE;
   ask.ai_socktype = SOCK_STREAM;
   sock_fd = -1;
   res = NULL;

   #ifdef PF_INET6
   if (is_tcp_ip)
   {  /* try IP-v6: not supported everywhere yet, so errors must be silently ignored */
      ask.ai_family = PF_INET6;
      rc = getaddrinfo(listen_ip, listen_port, &ask, &res);
      if (rc == 0)
      {
         sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
         if (sock_fd == -1)
         {
            dprintf2("listen_socket: socket (ipv6)\n");
            freeaddrinfo(res);
            res = NULL;
         }
      }
      else
         dprintf2("listen_socket: getaddrinfo (ipv6): %s\n", gai_strerror(rc));
   }
   #endif

   if (sock_fd == -1)
   {
      if (is_tcp_ip)
      {  /* IP-v4 (IP-address is optional, defaults to localhost) */
         ask.ai_family = PF_INET;
         rc = getaddrinfo(listen_ip, listen_port, &ask, &res);
      }
      else
      {  /* UNIX domain socket: named pipe located in /tmp directory */
         ask.ai_family = PF_UNIX;
         rc = vbi_proxy_msg_get_local_socket_addr(listen_port, &ask, &res);
      }
      if (rc == 0)
      {
         sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
         if (sock_fd == -1)
         {
            vbi_proxy_msg_logger(LOG_ERR, -1, errno, "socket create failed: ", NULL);
         }
      }
      else
         vbi_proxy_msg_logger(LOG_ERR, -1, 0, "Invalid hostname or service/port: ", gai_strerror(rc), NULL);
   }

   if (sock_fd != -1)
   {
      /* allow immediate reuse of the port (e.g. after server stop and restart) */
      opt = 1;
      if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt)) == 0)
      {
         /* make the socket non-blocking */
         if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) == 0)
         {
            /* bind the socket */
            if (bind(sock_fd, res->ai_addr, res->ai_addrlen) == 0)
            {
               #ifdef linux
               /* set socket permissions: r/w allowed to everyone */
               if ( (is_tcp_ip == FALSE) &&
                    (chmod(listen_port, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) != 0) )
                  vbi_proxy_msg_logger(LOG_WARNING, -1, errno, "chmod failed for named socket: ", NULL);
               #endif

               /* enable listening for new connections */
               if (listen(sock_fd, SRV_LISTEN_BACKLOG_LEN) == 0)
               {  /* finished without errors */
                  result = TRUE;
               }
               else
               {
                  vbi_proxy_msg_logger(LOG_ERR, -1, errno, "socket listen failed: ", NULL);
                  if ((is_tcp_ip == FALSE) && (listen_port != NULL))
                     unlink(listen_port);
               }
            }
            else
               vbi_proxy_msg_logger(LOG_ERR, -1, errno, "socket bind failed: ", NULL);
         }
         else
            vbi_proxy_msg_logger(LOG_ERR, -1, errno, "failed to set socket non-blocking: ", NULL);
      }
      else
         vbi_proxy_msg_logger(LOG_ERR, -1, errno, "socket setsockopt(SOL_SOCKET=SO_REUSEADDR) failed: ", NULL);
   }

   if (res != NULL)
      freeaddrinfo(res);

   if ((result == FALSE) && (sock_fd != -1))
   {
      close(sock_fd);
      sock_fd = -1;
   }

   return sock_fd;
}

/* ----------------------------------------------------------------------------
** Stop listening a socket
*/
void vbi_proxy_msg_stop_listen( vbi_bool is_tcp_ip, int sock_fd, char * pSrvPort )
{
   if (sock_fd != -1)
   {
      if (is_tcp_ip == FALSE)
         unlink(pSrvPort);

      close(sock_fd);
      sock_fd = -1;
   }
}

/* ----------------------------------------------------------------------------
** Accept a new connection
*/
int vbi_proxy_msg_accept_connection( int listen_fd )
{
   struct hostent * hent;
   char  hname_buf[129];
   uint32_t  length, maxLength;
   struct {  /* allocate enough room for all possible types of socket address structs */
      struct sockaddr  sa;
      char             padding[64];
   } peerAddr;
   int   sock_fd;
   vbi_bool  result = FALSE;

   maxLength = length = sizeof(peerAddr);
   sock_fd = accept(listen_fd, &peerAddr.sa, &length);
   if (sock_fd != -1)
   {
      if (length <= maxLength)
      {
         if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) == 0)
         {
            if (peerAddr.sa.sa_family == AF_INET)
            {
               hent = gethostbyaddr((void *) &peerAddr.sa, maxLength, AF_INET);
               if (hent != NULL)
               {
                  strlcpy(hname_buf, hent->h_name, sizeof(hname_buf) -1);
                  hname_buf[sizeof(hname_buf) - 1] = 0;
               }
               else
	       {
		  struct sockaddr_in *sa;

		  sa = (struct sockaddr_in *) &peerAddr.sa;
                  snprintf(hname_buf, sizeof (hname_buf), "%s, port %d", inet_ntoa(sa->sin_addr), sa->sin_port);
	       }

               vbi_proxy_msg_logger(LOG_INFO, sock_fd, 0, "new connection from ", hname_buf, NULL);
               result = TRUE;
            }
            #ifdef HAVE_GETADDRINFO
            else if (peerAddr.sa.sa_family == AF_INET6)
            {
               if (getnameinfo(&peerAddr.sa, length, hname_buf, sizeof(hname_buf) - 1, NULL, 0, 0) == 0)
               {  /* address could be resolved to hostname */
                  vbi_proxy_msg_logger(LOG_INFO, sock_fd, 0, "new connection from ", hname_buf, NULL);
                  result = TRUE;
               }
               else if (getnameinfo(&peerAddr.sa, length, hname_buf, sizeof(hname_buf) - 1, NULL, 0,
                                    NI_NUMERICHOST | NI_NUMERICSERV) == 0)
               {  /* resolver failed - but numeric conversion was successful */
                  dprintf2("accept_connection: IPv6 resolver failed for %s\n", hname_buf);
                  vbi_proxy_msg_logger(LOG_INFO, sock_fd, 0, "new connection from ", hname_buf, NULL);
                  result = TRUE;
               }
               else
               {  /* neither name looup nor numeric name output succeeded -> fatal error */
                  vbi_proxy_msg_logger(LOG_INFO, sock_fd, errno, "new connection: failed to get IPv6 peer name or IP-addr: ", NULL);
                  result = FALSE;
               }
            }
            #endif
            else if (peerAddr.sa.sa_family == AF_UNIX)
            {
               vbi_proxy_msg_logger(LOG_INFO, sock_fd, 0, "new connection from localhost via named socket", NULL);
               result = TRUE;
            }
            else
            {  /* neither INET nor named socket -> internal error */
               snprintf(hname_buf, sizeof (hname_buf), "%d", peerAddr.sa.sa_family);
               vbi_proxy_msg_logger(LOG_WARNING, -1, 0, "new connection via unexpected protocol family ", hname_buf, NULL);
            }
         }
         else
         {  /* fcntl failed: OS error (should never happen) */
            vbi_proxy_msg_logger(LOG_WARNING, -1, errno, "new connection: failed to set socket to non-blocking: ", NULL);
         }
      }
      else
      {  /* socket address buffer too small: internal error */
         snprintf(hname_buf, sizeof (hname_buf), "need %d, have %d", length, maxLength);
         vbi_proxy_msg_logger(LOG_WARNING, -1, 0, "new connection: saddr buffer too small: ", hname_buf, NULL);
      }

      if (result == FALSE)
      {  /* error -> drop the connection */
         close(sock_fd);
         sock_fd = -1;
      }
   }
   else
   {  /* connect accept failed: remote host may already have closed again */
      if (errno == EAGAIN)
         vbi_proxy_msg_logger(LOG_INFO, -1, errno, "accept failed: ", NULL);
   }

   return sock_fd;
}

/* ----------------------------------------------------------------------------
** Follow path through symlinks (in an attempt to get a unique path)
** - note: "." and ".." in relative symlinks appear to be resolved by Linux
**   already when creating the symlink
*/
static char * vbi_proxy_msg_resolve_symlinks( const char * p_dev_name )
{
   struct stat stbuf;
   char   link_name[MAXPATHLEN + 1];
   char * p_path;
   char * p_tmp;
   char * p_tmp2;
   int    name_len;
   int    res;
   int    slink_idx;

   p_path = strdup(p_dev_name);

   for (slink_idx = 0; slink_idx < 100; slink_idx++)
   {
      res = lstat(p_path, &stbuf);
      if ((res == 0) && S_ISLNK(stbuf.st_mode))
      {
         name_len = readlink(p_path, link_name, sizeof(link_name));
         if ((name_len > 0) && (name_len < (int) sizeof(link_name)))
         {
            link_name[name_len] = 0;
            dprintf2("resolve_symlinks: following symlink %s to: %s\n", p_path, link_name);
            if (link_name[0] != '/')
            {  /* relative path -> replace only last path element */
               p_tmp = malloc(strlen(p_path) + name_len + 1 + 1);
               p_tmp2 = strrchr(p_path, '/');
               if (p_tmp2 != NULL)
               {  /* copy former path up to and including the separator character */
                  p_tmp2 += 1;
                  strlcpy(p_tmp, p_path, p_tmp2 - p_path);
               }
               else
               {  /* no path separator in the former path -> replace completely */
                  p_tmp2 = p_path;
               }
               /* append the path read from the symlink file */
               strcpy(p_tmp + (p_tmp2 - p_path), link_name);
            }
            else
            {  /* absolute path -> replace symlink completely */
               p_tmp = strdup(link_name);
            }

            free((void *) p_path);
            p_path = p_tmp;
         }
         else
         {  /* symlink string too long for the buffer */
            if (name_len > 0)
            {
               link_name[sizeof(link_name) - 1] = 0;
               dprintf1("resolve_symlinks: abort: symlink too long: %s\n", link_name);
            }
            else
               dprintf1("resolve_symlinks: zero length symlink - abort\n");
            break;
         }
      }
      else
         break;
   }

   if (slink_idx >= 100)
      dprintf1("resolve_symlinks: symlink level too deep: abort after %d\n", slink_idx);

   return p_path;
}

/* ----------------------------------------------------------------------------
** Derive file name for socket from device path
*/
char * vbi_proxy_msg_get_socket_name( const char * p_dev_name )
{
   char * p_real_dev_name;
   char * p_sock_path;
   char * po;
   const char * ps;
   char   c;
   int    name_len;

   if (p_dev_name != NULL)
   {
      p_real_dev_name = vbi_proxy_msg_resolve_symlinks(p_dev_name);

      name_len = strlen(SRV_CLNT_SOCK_BASE_PATH) + strlen(p_real_dev_name) + 1;
      p_sock_path = malloc(name_len);
      if (p_sock_path != NULL)
      {
         strcpy(p_sock_path, SRV_CLNT_SOCK_BASE_PATH);
         po = p_sock_path + strlen(SRV_CLNT_SOCK_BASE_PATH);
         ps = p_real_dev_name;
         while ((c = *(ps++)) != 0)
         {
            if (c == '/')
               *(po++) = '-';
            else
               *(po++) = c;
         }
         *po = 0;
      }

      free(p_real_dev_name);
   }
   else
      p_sock_path = NULL;

   return p_sock_path;
}

/* ----------------------------------------------------------------------------
** Attempt to connect to an already running server
*/
vbi_bool vbi_proxy_msg_check_connect( const char * p_sock_path )
{
   VBIPROXY_MSG_HEADER msgCloseInd;
   struct sockaddr_un saddr;
   int  fd;
   vbi_bool result = FALSE;

   fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (fd != -1)
   {
      saddr.sun_family = AF_UNIX;
      strcpy(saddr.sun_path, p_sock_path);
      if (connect(fd, (struct sockaddr *) &saddr, sizeof(saddr)) != -1)
      {
         msgCloseInd.len  = htonl(sizeof(VBIPROXY_MSG_HEADER));
         msgCloseInd.type = htonl(MSG_TYPE_CLOSE_REQ);
         if (write(fd, &msgCloseInd, sizeof(msgCloseInd)) == sizeof(msgCloseInd))
         {
            result = TRUE;
         }
      }
      close(fd);
   }

   /* if no server is listening, remove the socket from the file system */
   if (result == FALSE)
      unlink(p_sock_path);

   return result;
}

/* ----------------------------------------------------------------------------
** Open client connection
** - since the socket is made non-blocking, the result of the connect is not
**   yet available when the function finishes; the caller has to wait for
**   completion with select() and then query the socket error status
*/
int vbi_proxy_msg_connect_to_server( vbi_bool use_tcp_ip, const char * pSrvHost, const char * pSrvPort, char ** ppErrorText )
{
   struct addrinfo    ask, *res;
   int  sock_fd;
   int  rc;

   rc = 0;
   res = NULL;
   sock_fd = -1;
   memset(&ask, 0, sizeof(ask));
   ask.ai_flags = 0;
   ask.ai_socktype = SOCK_STREAM;

   #ifdef PF_INET6
   if (use_tcp_ip)
   {  /* try IP-v6: not supported everywhere yet, so errors must be silently ignored */
      ask.ai_family = PF_INET6;
      rc = getaddrinfo(pSrvHost, pSrvPort, &ask, &res);
      if (rc == 0)
      {
         sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
         if (sock_fd == -1)
         {
            freeaddrinfo(res);
            res = NULL;
            /*dprintf2("socket (ipv6)\n"); */
         }
      }
      else
         dprintf2("getaddrinfo (ipv6): %s\n", gai_strerror(rc));
   }
   #endif

   if (sock_fd == -1)
   {
      if (use_tcp_ip)
      {
         ask.ai_family = PF_INET;
         rc = getaddrinfo(pSrvHost, pSrvPort, &ask, &res);
      }
      else
      {
         ask.ai_family = PF_UNIX;
         rc = vbi_proxy_msg_get_local_socket_addr(pSrvPort, &ask, &res);
      }
      if (rc == 0)
      {
         sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
         if (sock_fd == -1)
         {
            dprintf1("socket (ipv4): error %d, %s\n", errno, strerror(errno));
            asprintf(ppErrorText, _("Cannot create socket: %s."),
			 strerror(errno));
         }
      }
      else
      {
         dprintf1("getaddrinfo (ipv4): %s\n", gai_strerror(rc));
         asprintf(ppErrorText,
		      _("Invalid hostname or port: %s."),
		      gai_strerror(rc));
      }
   }

   if (sock_fd != -1)
   {
      if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) == 0)
      {
         /* connect to the server socket */
         if ( (connect(sock_fd, res->ai_addr, res->ai_addrlen) == 0)
              || (errno == EINPROGRESS)
              )
         {
            /* all ok: result is in sock_fd */
         }
         else
         {
            dprintf1("connect: error %d, %s\n", errno, strerror(errno));
            if (use_tcp_ip)
               asprintf(ppErrorText, _("Connection via TCP/IP failed, server not running or unreachable."));
            else
               asprintf(ppErrorText, _("Connection via socket failed, server not running."));
            close(sock_fd);
            sock_fd = -1;
         }
      }
      else
      {
         dprintf1("fcntl (F_SETFL=O_NONBLOCK): error %d, %s\n",
		  errno, strerror(errno));
         asprintf(ppErrorText, _("Socket I/O error: %s."),
		      strerror(errno));
         close(sock_fd);
         sock_fd = -1;
      }
   }

   if (res != NULL)
      freeaddrinfo(res);

   return sock_fd;
}

/* ----------------------------------------------------------------------------
** Check for the result of the connect syscall
** - UNIX: called when select() indicates writability
** - Win32: called when select() indicates writablility (successful connected)
**   or an exception (connect failed)
*/
vbi_bool vbi_proxy_msg_finish_connect( int sock_fd, char ** ppErrorText )
{
   vbi_bool result = FALSE;
   int sockerr;
   socklen_t sockerrlen;

   sockerrlen = sizeof(sockerr);
   if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, (void *)&sockerr, &sockerrlen) == 0)
   {
      if (sockerr == 0)
      {  /* success -> send the first message of the startup protocol to the server */
         dprintf2("finish_connect: socket connect succeeded\n");
         result = TRUE;
      }
      else
      {  /* failed to establish a connection to the server */
         dprintf1("finish_connect: socket connect failed: %s\n", strerror(sockerr));
         asprintf(ppErrorText, _("Cannot connect to server: %s."),
		      strerror(sockerr));
      }
   }
   else
   {
      dprintf1("finish_connect: getsockopt: %s\n", strerror(errno));
      asprintf(ppErrorText, _("Socket I/O error: %s."),
		   strerror(errno));
   }

   return result;
}

/* ----------------------------------------------------------------------------
** Query size and character of an ioctl request for v4l1 drivers
*/
static int
vbi_proxy_msg_v4l_ioctl( unsigned int request, void * p_arg, vbi_bool * req_perm )
{
   p_arg = p_arg;

   switch (request)
   {
#ifdef ENABLE_V4L
      case VIDIOCGCAP:
         dprintf2("v4l_ioctl CGCAP, arg size %ld\n", (long) sizeof(struct video_capability));
         return sizeof(struct video_capability);
      case VIDIOCGCHAN:
         dprintf2("v4l_ioctl CGCHAN, arg size %ld\n", (long) sizeof(struct video_channel));
         return sizeof(struct video_channel);
      case VIDIOCSCHAN:
         dprintf2("v4l_ioctl CSCHAN, arg size %ld\n", (long) sizeof(struct video_channel));
         *req_perm = TRUE;
         return sizeof(struct video_channel);
      case VIDIOCGTUNER:
         dprintf2("v4l_ioctl CGTUNER, arg size %ld\n", (long) sizeof(struct video_tuner));
         return sizeof(struct video_tuner);
      case VIDIOCSTUNER:
         dprintf2("v4l_ioctl CSTUNER, arg size %ld\n", (long) sizeof(struct video_tuner));
         *req_perm = TRUE;
         return sizeof(struct video_tuner);
      case VIDIOCGFREQ:
         dprintf2("v4l_ioctl CGFREQ, arg size %ld\n", (long) sizeof(unsigned long));
         return sizeof(unsigned long);
      case VIDIOCSFREQ:
         dprintf2("v4l_ioctl CSFREQ, arg size %ld\n", (long) sizeof(unsigned long));
         *req_perm = TRUE;
         return sizeof(unsigned long);
      case VIDIOCGUNIT:
         dprintf2("v4l_ioctl CGUNIT, arg size %ld\n", (long) sizeof(struct video_unit));
         return sizeof(struct video_unit);
#endif
      default:
         return -1;
   }
}

/* ----------------------------------------------------------------------------
** Query size and character of an ioctl request for v4l2 drivers
*/
static int
vbi_proxy_msg_v4l2_ioctl( unsigned int request, void * p_arg, vbi_bool * req_perm )
{
   switch (request)
   {
#ifdef ENABLE_V4L2
      case VIDIOC_QUERYCAP:
         dprintf2("v4l2_ioctl QUERYCAP, arg size %ld\n", (long) sizeof(struct v4l2_capability));
         return sizeof(struct v4l2_capability);
      case VIDIOC_QUERYSTD:
         dprintf2("v4l2_ioctl QUERYSTD, arg size %ld\n", (long) sizeof(v4l2_std_id));
         return sizeof(v4l2_std_id);
      case VIDIOC_G_STD:
         dprintf2("v4l2_ioctl G_STD, arg size %ld\n", (long) sizeof(v4l2_std_id));
         return sizeof(v4l2_std_id);
      case VIDIOC_S_STD:
         dprintf2("v4l2_ioctl S_STD, arg size %ld\n", (long) sizeof(v4l2_std_id));
         *req_perm = TRUE;
         return sizeof(v4l2_std_id);
      case VIDIOC_ENUMSTD:
         dprintf2("v4l2_ioctl ENUMSTD, arg size %ld\n", (long) sizeof(struct v4l2_standard));
         return sizeof(struct v4l2_standard);
      case VIDIOC_ENUMINPUT:
         dprintf2("v4l2_ioctl ENUMINPUT, arg size %ld\n", (long) sizeof(struct v4l2_input));
         return sizeof(struct v4l2_input);
      case VIDIOC_G_CTRL:
         dprintf2("v4l2_ioctl G_CTRL, arg size %ld\n", (long) sizeof(struct v4l2_control));
         return sizeof(struct v4l2_control);
      case VIDIOC_S_CTRL:
         dprintf2("v4l2_ioctl S_CTRL, arg size %ld\n", (long) sizeof(struct v4l2_control));
         return sizeof(struct v4l2_control);
      case VIDIOC_G_TUNER:
         dprintf2("v4l2_ioctl G_TUNER, arg size %ld\n", (long) sizeof(struct v4l2_tuner));
         return sizeof(struct v4l2_tuner);
      case VIDIOC_S_TUNER:
         dprintf2("v4l2_ioctl S_TUNER, arg size %ld\n", (long) sizeof(struct v4l2_tuner));
         *req_perm = TRUE;
         return sizeof(struct v4l2_tuner);
      case VIDIOC_QUERYCTRL:
         dprintf2("v4l2_ioctl QUERYCTRL, arg size %ld\n", (long) sizeof(struct v4l2_queryctrl));
         return sizeof(struct v4l2_queryctrl);
      case VIDIOC_QUERYMENU:
         dprintf2("v4l2_ioctl QUERYMENU, arg size %ld\n", (long) sizeof(struct v4l2_querymenu));
         return sizeof(struct v4l2_querymenu);
      case VIDIOC_G_INPUT:
         dprintf2("v4l2_ioctl G_INPUT, arg size %ld\n", (long) sizeof(int));
         return sizeof(int);
      case VIDIOC_S_INPUT:
         dprintf2("v4l2_ioctl S_INPUT, arg size %ld\n", (long) sizeof(int));
         *req_perm = TRUE;
         return sizeof(int);
      case VIDIOC_G_MODULATOR:
         dprintf2("v4l2_ioctl G_MODULATOR, arg size %ld\n", (long) sizeof(struct v4l2_modulator));
         return sizeof(struct v4l2_modulator);
      case VIDIOC_S_MODULATOR:
         dprintf2("v4l2_ioctl S_MODULATOR, arg size %ld\n", (long) sizeof(struct v4l2_modulator));
         *req_perm = TRUE;
         return sizeof(struct v4l2_modulator);
      case VIDIOC_G_FREQUENCY:
         dprintf2("v4l2_ioctl G_FREQUENCY, arg size %ld\n", (long) sizeof(struct v4l2_frequency));
         return sizeof(struct v4l2_frequency);
      case VIDIOC_S_FREQUENCY:
         dprintf2("v4l2_ioctl S_FREQUENCY, arg size %ld\n", (long) sizeof(struct v4l2_frequency));
         *req_perm = TRUE;
         return sizeof(struct v4l2_frequency);
#endif
      default:
         return vbi_proxy_msg_v4l_ioctl(request, p_arg, req_perm);
   }
}

/* ----------------------------------------------------------------------------
** Query size and character of an ioctl request
*/
int vbi_proxy_msg_check_ioctl( VBI_DRIVER_API_REV vbi_api,
                               int request, void * p_arg, vbi_bool * req_perm )
{
   *req_perm = FALSE;

   switch (vbi_api)
   {
      case VBI_API_V4L1:
         return vbi_proxy_msg_v4l_ioctl(request, p_arg, req_perm);

      case VBI_API_V4L2:
         return vbi_proxy_msg_v4l2_ioctl(request, p_arg, req_perm);

      default:
         dprintf1("v4l2_ioctl: API #%d not supported\n", vbi_api);
         return -1;
   }
}

#endif  /* ENABLE_PROXY */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
