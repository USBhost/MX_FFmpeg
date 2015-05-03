/*
 *  libzvbi -- VBI proxy client
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

/* $Id: proxy-client.c,v 1.18 2008/02/19 00:35:21 mschimek Exp $ */

static const char rcsid[] =
"$Id: proxy-client.c,v 1.18 2008/02/19 00:35:21 mschimek Exp $";

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>

#include "vbi.h"
#include "io.h"
#include "bcd.h"
#include "misc.h"

#include "proxy-msg.h"
#include "proxy-client.h"

#ifdef ENABLE_PROXY

#define dprintf1(fmt, arg...)    do {if (vpc->trace >= 1) fprintf(stderr, "proxy-client: " fmt, ## arg);} while(0)
#define dprintf2(fmt, arg...)    do {if (vpc->trace >= 2) fprintf(stderr, "proxy-client: " fmt, ## arg);} while(0)

/* ----------------------------------------------------------------------------
** Declaration of types of internal state variables
*/
typedef enum
{
   CLNT_STATE_NULL,
   CLNT_STATE_ERROR,
   CLNT_STATE_WAIT_CON_CNF,
   CLNT_STATE_WAIT_IDLE,
   CLNT_STATE_WAIT_SRV_CNF,
   CLNT_STATE_WAIT_RPC_REPLY,
   CLNT_STATE_CAPTURING,
} PROXY_CLIENT_STATE;

struct vbi_proxy_client
{
   unsigned int            services;
   int                     strict;
   int                     buffer_count;
   int                     scanning;
   unsigned int            trace;
   VBI_PROXY_CLIENT_FLAGS  client_flags;
   VBI_PROXY_DAEMON_FLAGS  daemon_flags;
   VBI_DRIVER_API_REV      vbi_api_revision;
   vbi_raw_decoder         dec;

   int                     chn_scanning;
   int                     chn_prio;
   vbi_bool                has_token;

   vbi_bool                sliced_ind;
   vbi_capture_buffer      raw_buf;
   vbi_capture_buffer      slice_buf;
   vbi_capture             capt_api;

   VBI_PROXY_EV_TYPE       ev_mask;

   PROXY_CLIENT_STATE      state;
   VBIPROXY_MSG_STATE      io;
   VBIPROXY_MSG          * p_client_msg;
   int                     max_client_msg_size;
   vbi_bool                endianSwap;
   unsigned long           rxTotal;
   unsigned long           rxStartTime;
   char                  * p_srv_host;
   char                  * p_srv_port;
   char                  * p_client_name;
   char                  * p_errorstr;

   VBI_PROXY_CLIENT_CALLBACK * p_callback_func;
   void                      * p_callback_data;
};

/* timeout for RPC to proxy daemon (for parameter changes) */
#define RPC_TIMEOUT_MSECS   5000
/* timeout for waiting until ongoing read is completed
** used to "free" the socket before sending parameter requests */
#define IDLE_TIMEOUT_MSECS  2000

/* helper macro */
#define VBI_RAW_SERVICES(SRV)           (((SRV) & (VBI_SLICED_VBI_625 | VBI_SLICED_VBI_525)) != 0)

/* ----------------------------------------------------------------------------
** Open client connection
** - automatically chooses the optimum transport: TCP/IP or pipe for local
** - since the socket is made non-blocking, the result of the connect is not
**   yet available when the function finishes; the caller has to wait for
**   completion with select() and then query the socket error status
*/
static vbi_bool proxy_client_connect_server( vbi_proxy_client * vpc )
{
   vbi_bool use_tcp_ip;
   int  sock_fd;
   vbi_bool result = FALSE;

   use_tcp_ip = FALSE;

   /* check if a server address has been configured */
   if ( ((vpc->p_srv_host != NULL) || (use_tcp_ip == FALSE)) &&
        (vpc->p_srv_port != NULL))
   {
      sock_fd = vbi_proxy_msg_connect_to_server(use_tcp_ip, vpc->p_srv_host, vpc->p_srv_port, &vpc->p_errorstr);
      if (sock_fd != -1)
      {
         /* initialize IO state */
         memset(&vpc->io, 0, sizeof(vpc->io));
         vpc->io.sock_fd    = sock_fd;
         vpc->io.lastIoTime = time(NULL);
         vpc->rxStartTime   = vpc->io.lastIoTime;
         vpc->rxTotal       = 0;

         result = TRUE;
      }
   }
   else
   {
      dprintf1("connect_server: hostname or port not configured\n");
      if (use_tcp_ip && (vpc->p_srv_host == NULL))
	 asprintf(&vpc->p_errorstr, _("Server hostname not configured."));
      else if (vpc->p_srv_port == NULL)
         asprintf(&vpc->p_errorstr, _("Server port not configured."));
   }
   return result;
}

/* ----------------------------------------------------------------------------
** Allocate buffer for client/servier message exchange
** - buffer is allocated statically, large enough for all expected messages
*/
static vbi_bool proxy_client_alloc_msg_buf( vbi_proxy_client * vpc )
{
   vbi_bool result;
   size_t msg_size;

   msg_size = sizeof(VBIPROXY_MSG_BODY);

   if ( (vpc->state == CLNT_STATE_CAPTURING) &&
        (vpc->services != 0) )
   {
      /* XXX TODO allow both raw and sliced */
      if (VBI_RAW_SERVICES(vpc->services))
         msg_size = VBIPROXY_SLICED_IND_SIZE(0, vpc->dec.count[0] + vpc->dec.count[1]);
      else
         msg_size = VBIPROXY_SLICED_IND_SIZE(vpc->dec.count[0] + vpc->dec.count[1], 0);

      if (msg_size < sizeof(VBIPROXY_MSG_BODY))
         msg_size = sizeof(VBIPROXY_MSG_BODY);
   }
   else
      msg_size = sizeof(VBIPROXY_MSG_BODY);

   msg_size += VBIPROXY_MSG_BODY_OFFSET;

   if (((int) msg_size != vpc->max_client_msg_size)
       || (vpc->p_client_msg == NULL))
   {
      if (vpc->p_client_msg != NULL)
         free(vpc->p_client_msg);

      dprintf2("alloc_msg_buf: allocate buffer for "
	       "max. %lu bytes\n", (unsigned long) msg_size);
      vpc->max_client_msg_size = msg_size;
      vpc->p_client_msg = malloc(msg_size);

      if (vpc->p_client_msg == NULL)
      {
         asprintf(&vpc->p_errorstr, _("Virtual memory exhausted."));
         result = FALSE;
      }
      else
         result = TRUE;
   }
   else
      result = TRUE;

   return result;
}

/* ----------------------------------------------------------------------------
** Checks the size of a message from server to client
*/
static vbi_bool proxy_client_check_msg( vbi_proxy_client * vpc, uint len,
                                        VBIPROXY_MSG * pMsg )
{
   VBIPROXY_MSG_HEADER * pHead = &pMsg->head;
   VBIPROXY_MSG_BODY * pBody = &pMsg->body;
   vbi_bool result = FALSE;

   /*if (vpc->p_client_msg->head.type != MSG_TYPE_SLICED_IND) */
   dprintf2("check_msg: recv msg type %d, len %d (%s)\n", pHead->type, pHead->len, vbi_proxy_msg_debug_get_type_str(pHead->type));

   switch (pHead->type)
   {
      case MSG_TYPE_CONNECT_CNF:
         if ( (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->connect_cnf)) &&
              (memcmp(pBody->connect_cnf.magics.protocol_magic, VBIPROXY_MAGIC_STR, VBIPROXY_MAGIC_LEN) == 0) )
         {
            if (pBody->connect_cnf.magics.endian_magic == VBIPROXY_ENDIAN_MAGIC)
            {  /* endian type matches -> no swapping required */
               vpc->endianSwap = FALSE;
            }
            else if (pBody->connect_cnf.magics.endian_magic == VBIPROXY_ENDIAN_MISMATCH)
            {  /* endian type does not match -> convert "endianess" of all msg elements > 1 byte */
               /* enable byte swapping for all following messages */
               vpc->endianSwap = TRUE;
            }
            result = TRUE;
         }
         break;

      case MSG_TYPE_CONNECT_REJ:
         result = ( (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->connect_rej)) &&
                    (memcmp(pBody->connect_rej.magics.protocol_magic, VBIPROXY_MAGIC_STR, VBIPROXY_MAGIC_LEN) == 0) );
         break;

      case MSG_TYPE_SLICED_IND:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) +
                          VBIPROXY_SLICED_IND_SIZE(pBody->sliced_ind.sliced_lines, pBody->sliced_ind.raw_lines));
         break;

      case MSG_TYPE_SERVICE_CNF:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->service_cnf));
         break;

      case MSG_TYPE_SERVICE_REJ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->service_rej));
         break;

      case MSG_TYPE_CLOSE_REQ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER));
         break;

      case MSG_TYPE_CHN_TOKEN_CNF:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_token_cnf));
         break;

      case MSG_TYPE_CHN_TOKEN_IND:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_token_ind));
         break;

      case MSG_TYPE_CHN_NOTIFY_CNF:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_notify_cnf));
         break;

      case MSG_TYPE_CHN_SUSPEND_CNF:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_suspend_cnf));
         break;

      case MSG_TYPE_CHN_SUSPEND_REJ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_suspend_rej));
         break;

      case MSG_TYPE_CHN_IOCTL_CNF:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) +
                          VBIPROXY_CHN_IOCTL_CNF_SIZE(pBody->chn_ioctl_cnf.arg_size));
         break;

      case MSG_TYPE_CHN_IOCTL_REJ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_ioctl_rej));
         break;

      case MSG_TYPE_CHN_RECLAIM_REQ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_reclaim_req));
         break;

      case MSG_TYPE_CHN_CHANGE_IND:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_change_ind));
         break;

      case MSG_TYPE_CONNECT_REQ:
      case MSG_TYPE_SERVICE_REQ:
      case MSG_TYPE_CHN_TOKEN_REQ:
      case MSG_TYPE_CHN_RECLAIM_CNF:
      case MSG_TYPE_CHN_NOTIFY_REQ:
      case MSG_TYPE_CHN_SUSPEND_REQ:
      case MSG_TYPE_CHN_IOCTL_REQ:
      case MSG_TYPE_DAEMON_PID_REQ:
      case MSG_TYPE_DAEMON_PID_CNF:
         dprintf1("check_msg: recv server msg type %d (%s)\n", pHead->type, vbi_proxy_msg_debug_get_type_str(pHead->type));
         result = FALSE;
         break;
      default:
         dprintf1("check_msg: unknown msg type %d\n", pHead->type);
         result = FALSE;
         break;
   }

   if (result == FALSE)
   {
      dprintf1("check_msg: illegal msg len %d for type %d (%s)\n", len, pHead->type, vbi_proxy_msg_debug_get_type_str(pHead->type));
      errno = EMSGSIZE;
   }

   return result;
}

/* ----------------------------------------------------------------------------
** Handle asynchronous messages from server
*/
static vbi_bool proxy_client_take_message( vbi_proxy_client * vpc )
{
   VBIPROXY_MSG_BODY * pMsg = &vpc->p_client_msg->body;
   vbi_bool result = FALSE;

   switch (vpc->p_client_msg->head.type)
   {
      case MSG_TYPE_SLICED_IND:
         if (vpc->state == CLNT_STATE_CAPTURING)
         {
            /* XXX TODO check raw */
            if ((int) pMsg->sliced_ind.sliced_lines > vpc->dec.count[0] + vpc->dec.count[1])
            {  /* more lines than req. for service -> would overflow the allocated slicer buffer
               ** -> discard extra lines (should never happen; proxy checks for line counts) */
               dprintf1("take_message: SLICED_IND: too many lines: %d > %d\n", pMsg->sliced_ind.sliced_lines, vpc->dec.count[0] + vpc->dec.count[1]);
               pMsg->sliced_ind.sliced_lines = vpc->dec.count[0] + vpc->dec.count[1];
            }
            /*assert(vpc->sliced_ind == FALSE);*/
            vpc->sliced_ind = TRUE;
            result = TRUE;
         }
         else if ( (vpc->state == CLNT_STATE_WAIT_IDLE) ||
                   (vpc->state == CLNT_STATE_WAIT_SRV_CNF) ||
                   (vpc->state == CLNT_STATE_WAIT_RPC_REPLY) )
         {
            /* discard incoming data during service changes */
            result = TRUE;
         }
         break;

      case MSG_TYPE_CHN_TOKEN_IND:
         if ( (vpc->state == CLNT_STATE_CAPTURING) ||
              (vpc->state == CLNT_STATE_WAIT_IDLE) ||
              (vpc->state == CLNT_STATE_WAIT_RPC_REPLY) )
         {
            /* XXX check if we're currently waiting for CNF for chn param change? */
            vpc->has_token  = TRUE;
            vpc->ev_mask   |= VBI_PROXY_EV_CHN_GRANTED;
            result = TRUE;
         }
         break;

      case MSG_TYPE_CHN_RECLAIM_REQ:
         if (vpc->state >= CLNT_STATE_WAIT_IDLE)
         {
            /* XXX FIXME: if no callback registered reply immediately */
            /* XXX FIXME? handle "has_token == FALSE": reply immediately? */
            vpc->ev_mask |= VBI_PROXY_EV_CHN_RECLAIMED;
            vpc->ev_mask &= ~VBI_PROXY_EV_CHN_GRANTED;
            result = TRUE;
         }
         break;

      case MSG_TYPE_CHN_CHANGE_IND:
         dprintf1("channel change indication: new scanning %d\n", pMsg->chn_change_ind.scanning);
         vpc->chn_scanning = pMsg->chn_change_ind.scanning;
         /* schedule callback to be invoked for this event */
         if ((pMsg->chn_change_ind.notify_flags & VBI_PROXY_CHN_FLUSH) != 0)
            vpc->ev_mask |= VBI_PROXY_EV_CHN_CHANGED;
         if ((pMsg->chn_change_ind.notify_flags & VBI_PROXY_CHN_NORM) != 0)
            vpc->ev_mask |= VBI_PROXY_EV_NORM_CHANGED;
         result = TRUE;
         break;

      case MSG_TYPE_CLOSE_REQ:
         result = FALSE;
         break;

      case MSG_TYPE_CONNECT_CNF:
      case MSG_TYPE_CONNECT_REJ:
      case MSG_TYPE_SERVICE_CNF:
      case MSG_TYPE_SERVICE_REJ:
      case MSG_TYPE_CHN_TOKEN_CNF:
      case MSG_TYPE_CHN_NOTIFY_CNF:
      case MSG_TYPE_CHN_SUSPEND_CNF:
      case MSG_TYPE_CHN_SUSPEND_REJ:
      case MSG_TYPE_CHN_IOCTL_CNF:
      case MSG_TYPE_CHN_IOCTL_REJ:
         /* synchronous message - internal error */
         dprintf1("take_message: error: handler called for RPC message reply %d (%s)\n", vpc->p_client_msg->head.type, vbi_proxy_msg_debug_get_type_str(vpc->p_client_msg->head.type));
         result = FALSE;
         break;

      default:
         break;
   }

   if ((result == FALSE) && (vpc->p_errorstr == NULL))
   {
      dprintf1("take_message: message type %d (len %d) not expected in state %d\n", vpc->p_client_msg->head.type, vpc->p_client_msg->head.len, vpc->state);
      asprintf(&vpc->p_errorstr, _("Protocol error (unexpected message)."));
   }

   return result;
}

/* ----------------------------------------------------------------------------
** Close client connection
*/
static void proxy_client_close( vbi_proxy_client * vpc )
{
   int save_errno;

   if (vpc != NULL)
   {
      save_errno = errno;
      vbi_proxy_msg_close_io(&vpc->io);

      memset(&vpc->io, 0, sizeof(vpc->io));
      vpc->io.sock_fd    = -1;
      vpc->io.lastIoTime = time(NULL);

      if (vpc->state != CLNT_STATE_NULL)
      {
         vpc->state = CLNT_STATE_ERROR;
      }
      errno = save_errno;
   }
   else
      dprintf1("proxy_client-close: illegal NULL ptr param");
}

/* ----------------------------------------------------------------------------
** Wait for I/O event on socket with the given timeout
*/
static int proxy_client_wait_select( vbi_proxy_client * vpc, struct timeval * timeout )
{
   struct timeval tv_start;
   struct timeval tv;
   fd_set fd_rd;
   fd_set fd_wr;
   int    ret;

   if (vpc->io.sock_fd != -1)
   {
      do
      {
#ifdef HAVE_LIBPTHREAD
         pthread_testcancel();
#endif

         FD_ZERO(&fd_rd);
         FD_ZERO(&fd_wr);

         if (vpc->io.writeLen > 0)
            FD_SET(vpc->io.sock_fd, &fd_wr);
         else
            FD_SET(vpc->io.sock_fd, &fd_rd);

         if ( ((vpc->client_flags & VBI_PROXY_CLIENT_NO_TIMEOUTS) == 0) &&
              ((vpc->daemon_flags & VBI_PROXY_DAEMON_NO_TIMEOUTS) == 0) )
         {
            tv = *timeout; /* Linux kernel overwrites this */
            gettimeofday(&tv_start, NULL);

            ret = select(vpc->io.sock_fd + 1, &fd_rd, &fd_wr, NULL, &tv);

            vbi_capture_io_update_timeout(timeout, &tv_start);
         }
         else
            ret = select(vpc->io.sock_fd + 1, &fd_rd, &fd_wr, NULL, NULL);

      } while ((ret < 0) && (errno == EINTR));

      if (ret > 0) {
	 dprintf2("wait_select: waited for %c -> sock r/w %d/%d\n",
		  (vpc->io.writeLen > 0) ? 'w':'r',
		  (int) FD_ISSET(vpc->io.sock_fd, &fd_rd),
		  (int) FD_ISSET(vpc->io.sock_fd, &fd_wr));
      } else if (ret == 0) {
	 dprintf1("wait_select: timeout\n");
      } else {
	 dprintf1("wait_select: error %d (%s)\n", errno, strerror(errno));
      }
   }
   else
   {
      dprintf1("wait_select: socket not open\n");
      ret = -1;
   }

   return ret;
}

/* ----------------------------------------------------------------------------
** Call remote procedure, i.e. write message then wait for reply
** - message must already have been written prior to calling this function
** - this is a synchronous message exchange with the daemon, i.e. the function
**   does not return until a reply is available or a timeout occured (in which
**   case the connection is dropped.)
*/
static vbi_bool proxy_client_rpc( vbi_proxy_client * vpc,
                                  VBIPROXY_MSG_TYPE reply1, VBIPROXY_MSG_TYPE reply2 )
{
   struct timeval tv;
   vbi_bool io_blocked;

   assert (vpc->state != CLNT_STATE_ERROR);
   assert (vpc->io.sock_fd != -1);

   tv.tv_sec  = RPC_TIMEOUT_MSECS / 1000;
   tv.tv_usec = (RPC_TIMEOUT_MSECS % 1000) * 1000;

   /* wait for write to finish */
   do
   {
      if (proxy_client_wait_select(vpc, &tv) <= 0)
         goto failure;

      if (vbi_proxy_msg_handle_write(&vpc->io, &io_blocked) == FALSE)
         goto failure;

   } while (vpc->io.writeLen > 0);

   /* wait for reply message */
   while (1)
   {
      assert (vbi_proxy_msg_is_idle(&vpc->io));

      do
      {
         if (proxy_client_wait_select(vpc, &tv) <= 0)
            goto failure;

         if (vbi_proxy_msg_handle_read(&vpc->io, &io_blocked, TRUE, vpc->p_client_msg, vpc->max_client_msg_size) == FALSE)
            goto failure;

      } while ((vpc->io.readOff == 0) || (vpc->io.readOff < vpc->io.readLen));

      /* perform security checks on received message */
      if (proxy_client_check_msg(vpc, vpc->io.readLen, vpc->p_client_msg) == FALSE)
         goto failure;

      vpc->rxTotal += vpc->p_client_msg->head.len;
      vbi_proxy_msg_close_read(&vpc->io);

      /* if it's the expected reply, we're finished */
      if ( (vpc->p_client_msg->head.type != reply1) &&
           (vpc->p_client_msg->head.type != reply2) )
      {
         /* process asynchronous message (e.g. slicer data or another IND message) */
         if (proxy_client_take_message(vpc) == FALSE)
            goto failure;
      }
      else
         break;
   }

   return TRUE;

failure:
   asprintf(&vpc->p_errorstr, _("Connection lost due to I/O error."));
   return FALSE;
}

/* ----------------------------------------------------------------------------
** Read a message from the socket
** - if no data is available in the socket buffer the function blocks;
**   when the timeout is reached the function returns 0
*/
static int proxy_client_read_message( vbi_proxy_client * vpc,
                                      struct timeval * p_timeout )
{
   vbi_bool io_blocked;
   int  ret;

   /* simultaneous read and write is not supported */
   assert (vpc->io.writeLen == 0);
   assert ((vpc->io.readOff == 0) || (vpc->io.readLen < vpc->io.readOff));

   if (proxy_client_alloc_msg_buf(vpc) == FALSE)
      goto failure;

   do
   {
      ret = proxy_client_wait_select(vpc, p_timeout);
      if (ret < 0)
         goto failure;
      if (ret == 0)
         break;

      if (vbi_proxy_msg_handle_read(&vpc->io, &io_blocked, TRUE, vpc->p_client_msg, vpc->max_client_msg_size) == FALSE)
         goto failure;

   } while (vpc->io.readOff < vpc->io.readLen);

   if (ret > 0)
   {
      /* perform security checks on received message */
      if (proxy_client_check_msg(vpc, vpc->io.readLen, vpc->p_client_msg) == FALSE)
         goto failure;

      vpc->rxTotal += vpc->p_client_msg->head.len;
      vbi_proxy_msg_close_read(&vpc->io);

      /* process the message - frees the buffer if neccessary */
      if (proxy_client_take_message(vpc) == FALSE)
         goto failure;
   }

   return ret;

failure:
   asprintf(&vpc->p_errorstr, _("Connection lost due to I/O error."));
   proxy_client_close(vpc);
   return -1;
}

/* ----------------------------------------------------------------------------
** Wait until ongoing read is finished
** - incoming data is discarded
*/
static vbi_bool proxy_client_wait_idle( vbi_proxy_client * vpc )
{
   PROXY_CLIENT_STATE old_state;
   struct timeval tv;
   vbi_bool io_blocked;

   assert (vpc->io.writeLen == 0);

   if (vpc->io.readOff > 0)
   {
      /* set intermediate state so that incoming data is discarded in the handler */
      tv.tv_sec  = IDLE_TIMEOUT_MSECS / 1000;
      tv.tv_usec = IDLE_TIMEOUT_MSECS * 1000;

      while (vpc->io.readOff < vpc->io.readLen)
      {
         if (proxy_client_wait_select(vpc, &tv) <= 0)
            goto failure;

         if (vbi_proxy_msg_handle_read(&vpc->io, &io_blocked, TRUE, vpc->p_client_msg, vpc->max_client_msg_size) == FALSE)
            goto failure;
      }

      /* perform security checks on received message */
      if (proxy_client_check_msg(vpc, vpc->io.readLen, vpc->p_client_msg) == FALSE)
         goto failure;

      vpc->rxTotal += vpc->p_client_msg->head.len;
      vbi_proxy_msg_close_read(&vpc->io);

      old_state = vpc->state;
      vpc->state = CLNT_STATE_WAIT_IDLE;

      if (proxy_client_take_message(vpc) == FALSE)
         goto failure;

      vpc->state = old_state;
   }

   return TRUE;

failure:
   return FALSE;
}

/* ----------------------------------------------------------------------------
** Start VBI acquisition, i.e. open connection to proxy daemon
*/
static vbi_bool proxy_client_start_acq( vbi_proxy_client * vpc )
{
   VBIPROXY_CONNECT_REQ * p_req_msg;
   VBIPROXY_CONNECT_CNF * p_cnf_msg;
   VBIPROXY_CONNECT_REJ * p_rej_msg;
   struct timeval tv;

   assert(vpc->state == CLNT_STATE_NULL);

   if (proxy_client_connect_server(vpc) == FALSE)
      goto failure;

   /* fake write request: make select to wait for socket to become writable */
   vpc->io.writeLen = 1;
   tv.tv_sec  = 4;
   tv.tv_usec = 0;

   /* wait for socket to reach connected state */
   if (proxy_client_wait_select(vpc, &tv) <= 0)
      goto failure;

   vpc->io.writeLen = 0;

   if (vbi_proxy_msg_finish_connect(vpc->io.sock_fd, &vpc->p_errorstr) == FALSE)
      goto failure;

   if (proxy_client_alloc_msg_buf(vpc) == FALSE)
      goto failure;

   /* write service request parameters */
   p_req_msg = &vpc->p_client_msg->body.connect_req;
   vbi_proxy_msg_fill_magics(&p_req_msg->magics);

   strlcpy((char *) p_req_msg->client_name, vpc->p_client_name, VBIPROXY_CLIENT_NAME_MAX_LENGTH);
   p_req_msg->client_name[VBIPROXY_CLIENT_NAME_MAX_LENGTH - 1] = 0;
   p_req_msg->pid = getpid();

   p_req_msg->client_flags = vpc->client_flags;
   p_req_msg->scanning     = vpc->scanning;
   p_req_msg->services     = vpc->services;
   p_req_msg->strict       = vpc->strict;
   p_req_msg->buffer_count = vpc->buffer_count;

   /* send the connect request message to the proxy server */
   vbi_proxy_msg_write(&vpc->io, MSG_TYPE_CONNECT_REQ, sizeof(p_req_msg[0]),
                       vpc->p_client_msg, FALSE);

   vpc->state = CLNT_STATE_WAIT_CON_CNF;

   /* send message and wait for reply */
   if (proxy_client_rpc(vpc, MSG_TYPE_CONNECT_CNF, MSG_TYPE_CONNECT_REJ) == FALSE)
      goto failure;

   if (vpc->p_client_msg->head.type == MSG_TYPE_CONNECT_CNF)
   {
      p_cnf_msg = &vpc->p_client_msg->body.connect_cnf;

      /* first server message received: contains version info */
      /* note: nxtvepg and endian magics are already checked */
      if (p_cnf_msg->magics.protocol_compat_version != VBIPROXY_COMPAT_VERSION)
      {
         dprintf1("take_message: CONNECT_CNF: reply version %x, protocol %x\n", p_cnf_msg->magics.protocol_version, p_cnf_msg->magics.protocol_compat_version);

         asprintf (&vpc->p_errorstr,
		       _("Incompatible server version %u.%u.%u."),
		       ((p_cnf_msg->magics.protocol_compat_version >> 16) & 0xff),
		       ((p_cnf_msg->magics.protocol_compat_version >>  8) & 0xff),
		       ((p_cnf_msg->magics.protocol_compat_version      ) & 0xff));
         goto failure;
      }
      else if (vpc->endianSwap)
      {  /* endian swapping currently unsupported */
         asprintf(&vpc->p_errorstr, _("Incompatible server architecture (endianess mismatch)."));
         goto failure;
      }
      else
      {  /* version ok -> request block forwarding */
         dprintf1("Successfully connected to proxy (version %x.%x.%x, protocol %x.%x.%x)\n",
                  (p_cnf_msg->magics.protocol_version >> 16) & 0xff,
                  (p_cnf_msg->magics.protocol_version >> 8) & 0xff,
                  (p_cnf_msg->magics.protocol_version) & 0xff,
                  (p_cnf_msg->magics.protocol_compat_version >> 16) & 0xff,
                  (p_cnf_msg->magics.protocol_compat_version >> 8) & 0xff,
                  (p_cnf_msg->magics.protocol_compat_version) & 0xff);

         vpc->dec               = p_cnf_msg->dec;
         vpc->services          = p_cnf_msg->services;
         vpc->daemon_flags      = p_cnf_msg->daemon_flags;
         vpc->vbi_api_revision  = p_cnf_msg->vbi_api_revision;

         vpc->state = CLNT_STATE_CAPTURING;
      }
   }
   else
   {
      p_rej_msg = &vpc->p_client_msg->body.connect_rej;
      dprintf2("take_message: CONNECT_REJ: reply version %x, protocol %x\n", p_rej_msg->magics.protocol_version, p_rej_msg->magics.protocol_compat_version);
      if (vpc->p_errorstr != NULL)
      {
         free(vpc->p_errorstr);
         vpc->p_errorstr = NULL;
      }
      if (p_rej_msg->errorstr[0] != 0)
         vpc->p_errorstr = strdup((char *) p_rej_msg->errorstr);

      goto failure;
   }

   return TRUE;

failure:
   /* failed to establish a connection to the server */
   proxy_client_close(vpc);
   return FALSE;
}

/* ----------------------------------------------------------------------------
** Stop acquisition, i.e. close connection
*/
static void proxy_client_stop_acq( vbi_proxy_client * vpc )
{
   if (vpc->state != CLNT_STATE_NULL)
   {
      /* note: set the new state first to prevent callback from close function */
      vpc->state = CLNT_STATE_NULL;

      proxy_client_close(vpc);
   }
   else
      dprintf1("stop_acq: acq not enabled\n");
}

/* ----------------------------------------------------------------------------
** Process pending callbacks
** - returns FALSE if caller should return from loop
*/
static void
vbi_proxy_process_callbacks( vbi_proxy_client * vpc )
{
   VBI_PROXY_EV_TYPE ev_mask;

   if (vpc->ev_mask != VBI_PROXY_EV_NONE)
   {
      ev_mask = vpc->ev_mask;
      vpc->ev_mask = VBI_PROXY_EV_NONE;

      if (vpc->p_callback_func != NULL)
      {
         vpc->p_callback_func(vpc->p_callback_data, ev_mask);
      }
      else
      {
         if (ev_mask & VBI_PROXY_EV_CHN_RECLAIMED)
         {
         }
      }
   }
}

/* ----------------------------------------------------------------------------
**                  E X P O R T E D   F U N C T I O N S
** --------------------------------------------------------------------------*/

/* document below */
int
vbi_proxy_client_channel_request( vbi_proxy_client * vpc,
                                  VBI_CHN_PRIO chn_prio,
                                  vbi_channel_profile * p_chn_profile )
{
   VBIPROXY_CHN_TOKEN_REQ  * p_req;
   int result;

   if (vpc != NULL)
   {
      if (vpc->state == CLNT_STATE_ERROR)
         return -1;

      dprintf1("Request for channel token: prio=%d\n", chn_prio);
      assert(vpc->state == CLNT_STATE_CAPTURING);

      if (proxy_client_alloc_msg_buf(vpc) == FALSE)
         goto failure;

      /* wait for ongoing read to complete (XXX FIXME: don't discard messages) */
      if (proxy_client_wait_idle(vpc) == FALSE)
         goto failure;

      /* reset token in any case because prio or profile may have changed */
      vpc->has_token      = FALSE;
      vpc->ev_mask       &= ~VBI_PROXY_EV_CHN_GRANTED;
      vpc->chn_prio       = chn_prio;

      vpc->state          = CLNT_STATE_WAIT_RPC_REPLY;

      /* send channel change request to proxy daemon */
      p_req = &vpc->p_client_msg->body.chn_token_req;
      memset(p_req, 0, sizeof(p_req[0]));
      p_req->chn_prio    = chn_prio;
      p_req->chn_profile = *p_chn_profile;

      vbi_proxy_msg_write(&vpc->io, MSG_TYPE_CHN_TOKEN_REQ, sizeof(p_req[0]),
                          vpc->p_client_msg, FALSE);

      /* send message and wait for reply */
      if (proxy_client_rpc(vpc, MSG_TYPE_CHN_TOKEN_CNF, -1) == FALSE)
         goto failure;

      /* process reply message */
      vpc->has_token = vpc->p_client_msg->body.chn_token_cnf.token_ind;
      if (vpc->has_token)
      {
         vpc->ev_mask |= VBI_PROXY_EV_CHN_GRANTED;
      }

      vpc->state = CLNT_STATE_CAPTURING;
      result = (vpc->has_token ? 1 : 0);

      /* invoke callback in case TOKEN_IND was piggy-backed */
      vbi_proxy_process_callbacks(vpc);

      return result;
   }

failure:
   proxy_client_close(vpc);
   return -1;
}


/* document below */
int
vbi_proxy_client_channel_notify( vbi_proxy_client * vpc,
                                 VBI_PROXY_CHN_FLAGS notify_flags,
                                 unsigned int scanning )
{
   VBIPROXY_CHN_NOTIFY_REQ  * p_msg;

   if (vpc != NULL)
   {
      if (vpc->state == CLNT_STATE_ERROR)
         return -1;

      assert(vpc->state == CLNT_STATE_CAPTURING);

      if (proxy_client_alloc_msg_buf(vpc) == FALSE)
         goto failure;

      /* wait for ongoing read to complete (XXX FIXME: don't discard messages) */
      if (proxy_client_wait_idle(vpc) == FALSE)
         goto failure;

      dprintf1("Send channel notification: flags 0x%X, scanning %d (prio=%d, has_token=%d)\n", notify_flags, scanning, vpc->chn_prio, vpc->has_token);

      memset(vpc->p_client_msg, 0, sizeof(vpc->p_client_msg[0]));
      p_msg = &vpc->p_client_msg->body.chn_notify_req;

      p_msg->notify_flags = notify_flags;
      p_msg->scanning     = scanning;

      vbi_proxy_msg_write(&vpc->io, MSG_TYPE_CHN_NOTIFY_REQ, sizeof(p_msg[0]),
                          vpc->p_client_msg, FALSE);

      vpc->state = CLNT_STATE_WAIT_RPC_REPLY;

      /* send message and wait for reply */
      if (proxy_client_rpc(vpc, MSG_TYPE_CHN_NOTIFY_CNF, -1) == FALSE)
         goto failure;

      /* process reply message */
      /* XXX TODO */

      vpc->state = CLNT_STATE_CAPTURING;
   }

   /* invoke callback in case TOKEN_IND was piggy-backed */
   vbi_proxy_process_callbacks(vpc);

   return 0;

failure:
   proxy_client_close(vpc);
   return -1;
}


/* document below */
int
vbi_proxy_client_channel_suspend( vbi_proxy_client * vpc,
                                  VBI_PROXY_SUSPEND cmd )
{
   /* XXX TODO */

   vpc = vpc;
   cmd = cmd;

   return -1;
}


/* document below */
int
vbi_proxy_client_device_ioctl( vbi_proxy_client * vpc, int request, void * p_arg )
{
   VBIPROXY_MSG * p_msg;
   vbi_bool  req_perm;
   int  size;
   int  result = -1;

   if (vpc != NULL)
   {
      if (vpc->state == CLNT_STATE_CAPTURING)
      {
         /* determine size of the argument */
         size = vbi_proxy_msg_check_ioctl(vpc->vbi_api_revision, request, p_arg, &req_perm);
         if (size >= 0)
         {
            /* XXX TODO: for GET type calls on v4l2 use local device */

            if ( (req_perm == FALSE) ||
                 (vpc->chn_prio > VBI_CHN_PRIO_BACKGROUND) || vpc->has_token )
            {
               /* wait for ongoing read to complete (XXX FIXME: don't discard messages) */
               if (proxy_client_wait_idle(vpc) == FALSE)
                  goto failure;

               dprintf1("Forwarding ioctl: 0x%X, argp=0x%lX\n", request, (long)p_arg);

               p_msg = malloc(VBIPROXY_MSG_BODY_OFFSET + VBIPROXY_CHN_IOCTL_REQ_SIZE(size));
               if (p_msg == NULL)
                  goto failure;

               p_msg->body.chn_ioctl_req.request = request;
               p_msg->body.chn_ioctl_req.arg_size = size;
               if (size > 0)
                  memcpy(p_msg->body.chn_ioctl_req.arg_data, p_arg, size);

               vbi_proxy_msg_write(&vpc->io, MSG_TYPE_CHN_IOCTL_REQ,
                                   VBIPROXY_CHN_IOCTL_REQ_SIZE(size), p_msg, TRUE);

               /* send message and wait for reply */
               if (proxy_client_rpc(vpc, MSG_TYPE_CHN_IOCTL_CNF, MSG_TYPE_CHN_IOCTL_REJ) == FALSE)
                  goto failure;

               /* process reply message */
               if (vpc->p_client_msg->head.type == MSG_TYPE_CHN_IOCTL_CNF)
               {
                  if (size > 0)
                     memcpy(p_arg, vpc->p_client_msg->body.chn_ioctl_req.arg_data, size);
                  result = vpc->p_client_msg->body.chn_ioctl_cnf.result;
                  errno = vpc->p_client_msg->body.chn_ioctl_cnf.errcode;
               }
               else
               {
                  errno = EBUSY;
                  result = -1;
               }
               vpc->state = CLNT_STATE_CAPTURING;
            }
            else
            {
               dprintf1("vbi_proxy-client_ioctl: request not allowed without obtaining token first\n");
               errno = EBUSY;
            }
         }
         else
         {
            dprintf1("vbi_proxy-client_ioctl: unknown or not allowed request: 0x%X\n", request);
            errno = EINVAL;
         }
      }
      else
         dprintf1("vbi_proxy-client_ioctl: client in invalid state %d\n", vpc->state);

      vbi_proxy_process_callbacks(vpc);
   }
   else
      dprintf1("vbi_proxy-client_ioctl: invalid NULL ptr param\n");

failure:
   return result;
}


/* document below */
int
vbi_proxy_client_get_channel_desc( vbi_proxy_client * vpc,
                                   unsigned int * p_scanning,
                                   vbi_bool * p_granted )
{
   if (vpc != NULL)
   {
      if (p_scanning != NULL)
         *p_scanning = vpc->scanning;
      if (p_granted != NULL)
         *p_granted = vpc->has_token;

      return 0;
   }
   else
      return -1;
}


/* document below */
vbi_bool
vbi_proxy_client_has_channel_control( vbi_proxy_client * vpc )
{
   if (vpc != NULL)
   {
      return (vpc->has_token);
   }
   else
   {
      dprintf1("vbi_proxy_client-has_channel_token: NULL client param");
      return FALSE;
   }
}


/* document below */
VBI_DRIVER_API_REV
vbi_proxy_client_get_driver_api( vbi_proxy_client * vpc )
{
   if (vpc != NULL)
   {
      return vpc->vbi_api_revision;
   }
   else
      return VBI_API_UNKNOWN;
}


/* document below */
VBI_PROXY_CLIENT_CALLBACK *
vbi_proxy_client_set_callback( vbi_proxy_client * vpc,
                               VBI_PROXY_CLIENT_CALLBACK * p_callback, void * p_data )
{
   VBI_PROXY_CLIENT_CALLBACK * p_prev_cb = NULL;

   if (vpc != NULL)
   {
      p_prev_cb = vpc->p_callback_func;

      vpc->p_callback_func = p_callback;
      vpc->p_callback_data = p_data;
   }
   else
      dprintf1("vbi_proxy_client-set_callback: invalid pointer arg\n");

   return p_prev_cb;
}


/* document below */
vbi_capture *
vbi_proxy_client_get_capture_if( vbi_proxy_client * vpc )
{
   if (vpc != NULL)
   {
      return &vpc->capt_api;
   }
   else
      return NULL;
}

/* ----------------------------------------------------------------------------
**                  D E V I C E   C A P T U R E   A P I
** --------------------------------------------------------------------------*/

/**
 * @internal
 *
 * @param vpc Pointer to initialized proxy client context
 *
 * @return
 * Pointer to a vbi_raw_decoder structure, read only.  Returns @c NULL
 * upon error (i.e. if the client is not connected to the daemon)
 */
static vbi_raw_decoder *
vbi_proxy_client_get_dec_params( vbi_capture * vc )
{
   vbi_proxy_client * vpc = PARENT(vc, vbi_proxy_client, capt_api);

   if (vc != NULL)
      return &vpc->dec;
   else
      return NULL;
}


/**
 * @internal
 *
 * @param vpc Pointer to initialized proxy client context
 *
 * @return
 * File descriptor of the socket used to connect to the proxy daemon or
 * -1 upon error (i.e. if the client is not connected to the daemon)
 * The descriptor can only be used for select() by caller, i.e. not for
 * read/write and must never be closed (call the close function instead)
 */
static int
vbi_proxy_client_get_fd( vbi_capture * vc )
{
   vbi_proxy_client * vpc = PARENT(vc, vbi_proxy_client, capt_api);

   if (vc != NULL)
   {
      return vpc->io.sock_fd;
   }
   else
      return -1;
}


/**
 * @internal
 *
 * Read one frame's worth of VBI data.  If asynchronous events occur,
 * the callback is invoked before the call returns.
 *
 * Note: This function may indicate a timeout (i.e. return 0) even
 * if a previous select indicated readability. This will occur when
 * asynchronous messages (e.g. channel change indications) arrive.
 * Proxy clients should be prepared for this.  Channel change
 * indications can be supressed with VBI_PROXY_CLIENT_NO_STATUS_IND
 * in client flags during creation of the proxy, but there may still
 * be asynchronous messages when a token is granted.
 */
static int
vbi_proxy_client_read( vbi_capture * vc,
                       struct vbi_capture_buffer **pp_raw_buf,
                       struct vbi_capture_buffer **pp_slice_buf,
                       const struct timeval     * p_timeout )
{
   vbi_proxy_client * vpc = PARENT(vc, vbi_proxy_client, capt_api);
   struct timeval timeout = *p_timeout;
   int  lines;
   int  result;

   if ((vc != NULL) && (vpc->state == CLNT_STATE_CAPTURING))
   {
      vpc->sliced_ind = FALSE;

      /* wait for message & read it (note: may also be some status ind) */
      result = proxy_client_read_message(vpc, &timeout);

      if (result > 0)
      {
         if (vpc->sliced_ind != FALSE)
         {
            if (pp_raw_buf != NULL)
            {
               lines = vpc->p_client_msg->body.sliced_ind.raw_lines;

               if (*pp_raw_buf != NULL)
               {
                  /* XXX optimization possible: read sliced msg into buffer to avoid memcpy */
                  memcpy( (*pp_raw_buf)->data,
                          vpc->p_client_msg->body.sliced_ind.u.raw,
                          lines * VBIPROXY_RAW_LINE_SIZE );
               }
               else
               {
                  *pp_raw_buf = &vpc->raw_buf;
                  (*pp_raw_buf)->data = vpc->p_client_msg->body.sliced_ind.u.raw;
               }
               (*pp_raw_buf)->size      = lines * VBIPROXY_RAW_LINE_SIZE;
               (*pp_raw_buf)->timestamp = vpc->p_client_msg->body.sliced_ind.timestamp;
            }

            if (pp_slice_buf != NULL)
            {
               lines = vpc->p_client_msg->body.sliced_ind.sliced_lines;

               if (*pp_slice_buf != NULL)
               {
                  /* XXX optimization possible: read sliced msg into buffer to avoid memcpy */
                  memcpy( (*pp_slice_buf)->data,
                          vpc->p_client_msg->body.sliced_ind.u.sliced,
                          lines * sizeof(vbi_sliced) );
               }
               else
               {
                  *pp_slice_buf = &vpc->slice_buf;
                  (*pp_slice_buf)->data = vpc->p_client_msg->body.sliced_ind.u.sliced;
               }

               (*pp_slice_buf)->size      = lines * sizeof(vbi_sliced);
               (*pp_slice_buf)->timestamp = vpc->p_client_msg->body.sliced_ind.timestamp;
            }
         }
         else
         {  /* not a slicer data unit */
            result = 0;
         }
         vbi_proxy_process_callbacks(vpc);
      }
      return result;
   }
   errno = EBADF;
   return -1;
}


/**
 * @internal
 *
 * Add and/or remove one or more services to an already initialized
 * capture context.
 *
 * Note the "commit" parameter is currently not applicable to proxy clients.
 */
static unsigned int
vbi_proxy_client_update_services( vbi_capture * vc,
                                  vbi_bool reset, vbi_bool commit,
                                  unsigned int services, int strict,
                                  char ** pp_errorstr )
{
   vbi_proxy_client * vpc = PARENT(vc, vbi_proxy_client, capt_api);

   if (vc != NULL)
   {
      if (vpc->state == CLNT_STATE_ERROR)
         return 0;

      assert(vpc->state == CLNT_STATE_CAPTURING);

      if (proxy_client_alloc_msg_buf(vpc) == FALSE)
         goto failure;

      /* wait for ongoing read to complete */
      if (proxy_client_wait_idle(vpc) == FALSE)
         goto failure;

      vpc->state = CLNT_STATE_WAIT_SRV_CNF;

      dprintf1("update_services: send service req: srv %d, strict %d\n", services, strict);

      /* send service request to proxy daemon */
      vpc->p_client_msg->body.service_req.reset        = reset;
      vpc->p_client_msg->body.service_req.commit       = commit;
      vpc->p_client_msg->body.service_req.services     = services;
      vpc->p_client_msg->body.service_req.strict       = strict;
      vbi_proxy_msg_write(&vpc->io, MSG_TYPE_SERVICE_REQ, sizeof(vpc->p_client_msg->body.service_req),
                          vpc->p_client_msg, FALSE);

      /* send message and wait for reply */
      if (proxy_client_rpc(vpc, MSG_TYPE_SERVICE_CNF, MSG_TYPE_SERVICE_REJ) == FALSE)
         goto failure;

      if (vpc->p_client_msg->head.type == MSG_TYPE_SERVICE_CNF)
      {
         memset(&vpc->dec, 0, sizeof(vpc->dec));

         vpc->services = vpc->p_client_msg->body.service_cnf.services;
         memcpy(&vpc->dec, &vpc->p_client_msg->body.service_cnf.dec, sizeof(vpc->dec));
         dprintf1("service cnf: granted service %d\n", vpc->dec.services);
      }
      else
      {
         /* process the message */
         if ( (vpc->p_client_msg->body.service_rej.errorstr[0] != 0) &&
              (pp_errorstr != NULL) )
         {
            *pp_errorstr = strdup((char *) vpc->p_client_msg->body.service_rej.errorstr);
         }
      }
      vpc->state = CLNT_STATE_CAPTURING;

      return services & vpc->services;
   }

failure:
   if (vpc->p_errorstr != NULL)
   {
      if (pp_errorstr != NULL)
         *pp_errorstr = vpc->p_errorstr;
      else
         free(vpc->p_errorstr);
      vpc->p_errorstr = NULL;
   }
   proxy_client_close(vpc);
   return 0;
}


/**
 * @internal
 *
 * Note this function is only present because it's part of the capture
 * device API.  Proxy-aware clients should use the proxy client API
 * function vbi_proxy_client_channel_notify() instead of this one, because
 * it allows to return the channel control "token" at the same time.
 */
static void
vbi_proxy_client_flush( vbi_capture * vc )
{
   vbi_proxy_client * vpc = PARENT(vc, vbi_proxy_client, capt_api);

   if (vc != NULL)
   {
      vbi_proxy_client_channel_notify(vpc, VBI_PROXY_CHN_FLUSH, 0);
   }
}


/**
 * @internal
 *
 * @param vpc Pointer to initialized proxy client context
 *
 * Queries properties of the exported "capture device" file handle.
 */
static VBI_CAPTURE_FD_FLAGS
vbi_proxy_client_get_fd_flags(vbi_capture *vc)
{
	vc = vc;

        return VBI_FD_HAS_SELECT;
}

/**
 * @internal
 *
 * @param vpc Pointer to initialized proxy client context
 *
 * Close connection to the proxy daemon.  The proxy client context
 * can be re-used for another connection later.
 */
static void
vbi_proxy_client_stop( vbi_capture * vc )
{
   vbi_proxy_client * vpc = PARENT(vc, vbi_proxy_client, capt_api);

   if (vc != NULL)
   {
      proxy_client_stop_acq(vpc);
   }
}

/* document below */
vbi_capture *
vbi_capture_proxy_new( struct vbi_proxy_client * vpc,
                       int buffers, int scanning,
                       unsigned int *p_services, int strict,
                       char **pp_errorstr )
{
   if (vpc != NULL)
   {
      if ( (vpc->state == CLNT_STATE_NULL) ||
           (vpc->state == CLNT_STATE_ERROR) )
      {
         if (scanning != 525 && scanning != 625)
            scanning = 0;

         if (buffers < 1)
            buffers = 1;

         if (strict < -1)
            strict = -1;
         else if (strict > 2)
            strict = 2;

         /* check and copy parameters into state struct */
         assert((p_services == NULL) || (*p_services != 0));

         vpc->buffer_count = buffers;
         vpc->scanning     = scanning,
         vpc->services     = ((p_services != NULL) ? *p_services : 0);
         vpc->strict       = strict;

         /* reset state if in error state (e.g. previous connect failed) */
         vpc->state = CLNT_STATE_NULL;

         /* send params to daemon and wait for reply */
         if ( proxy_client_start_acq(vpc) )
         {
            assert(vpc->state == CLNT_STATE_CAPTURING);
            assert((p_services == NULL) || (vpc->services != 0));

            if (p_services != NULL)
               *p_services = vpc->services;

            return &vpc->capt_api;
         }
      }
      else
         dprintf1("vbi_proxy-client_start: illegal state %d for start\n", vpc->state);
   }
   else
      dprintf1("vbi_proxy-client_start: illegal NULL ptr param\n");

   if (pp_errorstr != NULL)
      *pp_errorstr = vpc->p_errorstr;
   else
      free(vpc->p_errorstr);
   vpc->p_errorstr = NULL;

   return NULL;
}

void
vbi_proxy_client_destroy( vbi_proxy_client * vpc )
{
   if (vpc != NULL)
   {
      /* close the connection (during normal shutdown it should already be closed) */
      if (vpc->state != CLNT_STATE_NULL)
         proxy_client_stop_acq(vpc);

      if (vpc->p_srv_host != NULL)
         free(vpc->p_srv_host);

      if (vpc->p_srv_port != NULL)
         free(vpc->p_srv_port);

      if (vpc->p_client_msg != NULL)
         free(vpc->p_client_msg);

      if (vpc->p_errorstr != NULL)
         free(vpc->p_errorstr);

      free(vpc);
   }
}

/* document below */
vbi_proxy_client *
vbi_proxy_client_create( const char *p_dev_name, const char *p_client_name,
                         VBI_PROXY_CLIENT_FLAGS client_flags,
                         char **pp_errorstr, int trace_level )
{
   vbi_proxy_client * vpc;

   if (trace_level)
   {
      fprintf(stderr, "Creating vbi proxy client, rev.\n%s\n", rcsid);
      vbi_proxy_msg_set_debug_level(trace_level);
   }

   vpc = (vbi_proxy_client *) calloc(1, sizeof(vpc[0]));
   if (vpc != NULL)
   {
      /* fill capture interface struct */
      vpc->capt_api.parameters = vbi_proxy_client_get_dec_params;
      vpc->capt_api._delete = vbi_proxy_client_stop;
      vpc->capt_api.get_fd = vbi_proxy_client_get_fd;
      vpc->capt_api.get_fd_flags = vbi_proxy_client_get_fd_flags;
      vpc->capt_api.read = vbi_proxy_client_read;
      vpc->capt_api.update_services = vbi_proxy_client_update_services;
      vpc->capt_api.flush = vbi_proxy_client_flush;

      /* initialize client state with given parameters */
      vpc->p_client_name = strdup(p_client_name);
      vpc->client_flags  = client_flags;
      vpc->p_srv_port    = vbi_proxy_msg_get_socket_name(p_dev_name);
      vpc->p_srv_host    = NULL;
      vpc->trace         = trace_level;

      vpc->state         = CLNT_STATE_NULL;
      vpc->io.sock_fd    = -1;
   }
   else
   {
      asprintf(pp_errorstr, _("Virtual memory exhausted."));
   }
   return vpc;
}

#else /* !ENABLE_PROXY */

/**
 * @addtogroup Proxy VBI capture proxy interface
 * @ingroup Raw
 * @brief Receiving sliced or raw data from VBI proxy daemon
 *
 * Using the VBI proxy daemon instead of capturing directly from a
 * VBI device allows multiple clients to capture concurrently, e.g.
 * to decode multiple data services.
 */

/**
 * @param vpc Pointer to initialized proxy client context
 * @param chn_prio Channel change priority level.  If there are other clients
 *   with higher priority the client will be refused any channel changes.
 * @param p_chn_profile Channel profile for scheduling at background
 *   priority level.
 *
 * This function is used to request permission to switch channels or norm.
 * Since the VBI device can be shared with other proxy clients, clients should
 * wait for permission, so that the proxy daemon can fairly schedule channel
 * requests.
 *
 * Scheduling differs at the 3 priority levels. For an explanation of
 * priorities see enum VBI_CHN_PRIO.  At background level channel changes
 * are coordinated by introduction of a virtual token: only the
 * one client which holds the token is allowed to switch channels. The daemon
 * will wait for the token to be returned before it's granted to another
 * client.  This way conflicting channel changes are avoided.
 *
 * At the upper level the latest request always wins.  To avoid interference
 * the application still might wait until it gets indicated that the token
 * has been returned to the daemon.
 *
 * The token may be granted right away or at a later time, e.g. when it has
 * to be reclaimed from another client first, or if there are other clients
 * with higher priority.  If a callback has been registered, it will be
 * invoked when the token arrives; otherwise
 * vbi_proxy_client_has_channel_control()
 * can be used to poll for it.
 *
 * Note: to set the priority level to "background" only without requesting
 * a channel, set the is_valid member in the profile to @c FALSE.
 *
 * @return
 * 1 if change is allowed, 0 if not allowed,
 * -1 on error, examine @c errno for details.
 *
 * @since 0.2.9
 */
/* XXX TODO improve description */
int
vbi_proxy_client_channel_request( vbi_proxy_client * vpc,
                                  VBI_CHN_PRIO chn_prio,
                                  vbi_channel_profile * p_chn_profile )
{
   errno = 0;
   return -1;
}


/**
 * @param vpc Pointer to initialized proxy client context
 * @param notify_flags Combination of event notification bits
 * @param scanning New norm, if norm event bit is set
 *
 * Send channel control request to proxy daemon.
 * See description of the flags for details.
 *
 * @return
 * 0 upon success, -1 on error, examine @c errno for details.
 *
 * @since 0.2.9
 */
int
vbi_proxy_client_channel_notify( vbi_proxy_client * vpc,
                                 VBI_PROXY_CHN_FLAGS notify_flags,
                                 unsigned int scanning )
{
   return -1;
}


/**
 * @param vpc Pointer to initialized proxy client context
 * @param cmd Control command
 *
 * Request to temporarily suspend capturing
 *
 * @return
 * 0 upon success, -1 on error, examine @c errno for details.
 *
 * @since 0.2.9
 */
int
vbi_proxy_client_channel_suspend( vbi_proxy_client * vpc,
                                  VBI_PROXY_SUSPEND cmd )
{
   return -1;
}


/**
 * @param vpc Pointer to initialized proxy client context
 * @param request Ioctl request code to be passed to driver
 * @param p_arg Ioctl argument buffer to be passed to driver.
 *   For ioctls which return data, the buffer will by modified by
 *   the call (i.e. same as if the ioctl had ben called directly)
 *   Note the required buffer size depends on the request code.
 *
 * @brief Wrapper for ioctl requests on the VBI device
 *
 * This function allows to manipulate parameters of the underlying
 * VBI device.  Not all ioctls are allowed here.  It's mainly intended
 * to be used for channel enumeration and channel/norm changes.  
 * The request codes and parameters are the same as for the actual device.
 * The caller has to query the driver API first and use the respective
 * ioctl codes, same as if the device would be used directly.
 *
 * @return
 * Same as for the ioctl, i.e. -1 on error and errno set appropriately.
 * The funtion also will fail with errno @c EBUSY if the client doesn't
 * have permission to control the channel.
 *
 * @since 0.2.9
 */
int
vbi_proxy_client_device_ioctl( vbi_proxy_client * vpc,
			       int request, void * p_arg )
{
   return -1;
}

/**
 * @param vpc Pointer to initialized proxy client context
 * @param p_scanning Returns new scanning after channel change
 * @param p_granted Returns@c TRUE if client is currently allowed to
 *   switch channels
 *
 * Retrieve info sent by the proxy daemon in a channel change indication.
 *
 * @return
 * 0 upon success, -1 on error.
 *
 * @since 0.2.9
 */
int
vbi_proxy_client_get_channel_desc( vbi_proxy_client * vpc,
                                   unsigned int * p_scanning,
                                   vbi_bool * p_granted )
{
   return -1;
}

/**
 * @param vpc Pointer to initialized proxy client context
 *
 * @brief Query if the client is currently allowed to switch channels
 *
 * @return
 * Returns @c TRUE if client is currently allowed to switch channels.
 *
 * @since 0.2.9
 */
vbi_bool
vbi_proxy_client_has_channel_control( vbi_proxy_client * vpc )
{
   return FALSE;
}

/**
 * @param vpc Pointer to initialized proxy client context
 *
 * @brief Returns the driver type behind the actual capture device
 *
 * This function can be used to query which driver is behind the
 * device which is currently opened by the VBI proxy daemon.
 * Applications which use libzvbi's capture API only need not
 * care about this.  The information is only relevant to applications
 * which need to change channels or norms.
 *
 * The function will fail if the client is currently not connected
 * to the daemon, i.e. VPI capture has to be started first.
 *
 * @return
 * Driver type or -1 on error.
 *
 * @since 0.2.9
 */
VBI_DRIVER_API_REV
vbi_proxy_client_get_driver_api( vbi_proxy_client * vpc )
{
   return VBI_API_UNKNOWN;
}

/**
 * @param vpc Pointer to initialized proxy client context
 * @param p_callback Pointer to callback function
 * @param p_data Void pointer which will be passed through to the
 *   callback function unmodified.
 *
 * @brief Installs callback function for asynchronous events
 *
 * This function installs a callback function which will be invoked
 * upon asynchronous events (e.g. channel changes by other clients.)
 * Since the proxy client has no "life" on it's own (i.e.
 * it's not using an internal thread or process) callbacks will only
 * occur from inside other proxy client function calls.  The client's
 * file description will become readable when an asynchronous message
 * has arrived from the daemon.  Typically the application then will
 * call read to obtain sliced data and the callback will be invoked
 * from inside the read function.  Usually in this case the read call
 * will return zero, i.e. indicate an timeout since no actual sliced
 * data has arrived.
 *
 * Note for channel requests the callback to grant channel control may
 * be invoked before the request function returns.
 * Note you can call any interface function from inside the callback,
 * including the destroy operator.
 *
 * @return
 * Returns pointer to the previous callback or @c NULL if none.
 *
 * @since 0.2.9
 */
VBI_PROXY_CLIENT_CALLBACK *
vbi_proxy_client_set_callback( vbi_proxy_client * vpc,
                               VBI_PROXY_CLIENT_CALLBACK * p_callback,
			       void * p_data )
{
   return NULL;
}

/**
 * @param vpc Pointer to initialized and active proxy client context
 *
 * @brief Returns capture interface for an initialized proxy client
 *
 * This function is for convenience only: it returns the same pointer
 * as the previous call to vbi_capture_proxy_new(), so that the client
 * need not store it.  This pointer is required for function calls
 * through the capture device API (e.g. reading raw or sliced data)
 *
 * @return
 * Pointer to a vbi_capture structure, should be treated as void * by
 * caller, i.e. acessed neither for read nor write.  Returns @c NULL
 * upon error (i.e. if the client is not connected to the daemon)
 *
 * @since 0.2.9
 */
vbi_capture *
vbi_proxy_client_get_capture_if( vbi_proxy_client * vpc )
{
   return NULL;
}

/**
 * @ingroup Device
 *
 * @param p_proxy_client Reference to an initialized proxy client
 *   context.
 * @param buffers Number of intermediate buffers on server side
 *   of the proxy socket connection. (Note this is not related to the
 *   device buffer count parameter of @a v4l2_new et.al.)
 * @param scanning This indicates the current norm: 625 for PAL and
 *   525 for NTSC; set to 0 if you don't know (you should not attempt
 *   to query the device for the norm, as this parameter is only required
 *   for v4l1 drivers which don't support video standard query ioctls)
 * @param p_services This must point to a set of @ref VBI_SLICED_
 *   symbols describing the
 *   data services to be decoded. On return the services actually
 *   decodable will be stored here. See vbi_raw_decoder_add()
 *   for details. If you want to capture raw data only, set to
 *   @c VBI_SLICED_VBI_525, @c VBI_SLICED_VBI_625 or both.
 *   If this parameter is @c NULL, no services will be installed.
 *   You can do so later with vbi_capture_update_services(); note the
 *   reset parameter must be set to @c TRUE in this case.
 * @param strict Will be passed to vbi_raw_decoder_add().
 * @param pp_errorstr If not @c NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 *
 * Open a new connection to a VBI proxy to open a VBI device for the
 * given services.  On side of the proxy daemon, one of the regular
 * capture context creation functions (e.g. v4l2_new) is invoked. 
 * If the creation succeeds, and any of the requested services are
 * available, capturing is started and all captured data is forwarded
 * transparently to the client.
 *
 * Whenever possible the proxy should be used instead of opening the device
 * directly, since it allows the user to start multiple VBI clients in
 * parallel.  When this function fails (usually because the user hasn't
 * started the proxy daemon) applications should automatically fall back
 * to opening the device directly.
 *
 * @return
 * Initialized vbi_capture context, @c NULL on failure.
 *
 * @since 0.2.9
 */
vbi_capture *
vbi_capture_proxy_new( struct vbi_proxy_client *p_proxy_client,
                       int buffers, int scanning,
                       unsigned int *p_services, int strict,
                       char **pp_errorstr )
{
   pthread_once (&vbi_init_once, vbi_init);
   asprintf(pp_errorstr, _("Proxy client interface not compiled."));
   return NULL;
}

/**
 * @param vpc Pointer to initialized proxy client context
 *
 * This function closes the connection to the proxy daemon and frees
 * all resources.  The given context must no longer be used after this
 * function was called.  If the context was used via the capture device
 * interface, the vbi_capture context must be destroyed first.
 *
 * @since 0.2.9
 */
void
vbi_proxy_client_destroy( vbi_proxy_client * vpc )
{
   vpc = vpc;
}

/**
 * @param p_dev_name Name of the device to open, usually one of
 *   @c /dev/vbi or @c /dev/vbi0 and up.  Note: should be the same path as
 *   used by the proxy daemon, else the client may not be able to connect.
 * @param p_client_name Name of the client application, typically identical
 *   to argv[0] (without the path though)  Can be used by the proxy daemon
 *   to fine-tune scheduling or to present the user with a list of
 *   currently connected applications.
 * @param client_flags Can contain one or more members of
 * VBI_PROXY_CLIENT_FLAGS
 * @param pp_errorstr If not @c NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace_level Enable debug output to stderr if non-zero.
 *   Larger values produce more output.
 *
 * This function initializes a proxy daemon client context with the given
 * parameters.  (Note this function does not yet connect the daemon.)
 *
 * @return
 * Initialized proxy client context, @c NULL on failure
 *
 * @since 0.2.9
 */
vbi_proxy_client *
vbi_proxy_client_create(const char *p_dev_name, const char *p_client_name,
                        VBI_PROXY_CLIENT_FLAGS client_flags,
                        char **pp_errorstr, int trace_level)
{
   asprintf(pp_errorstr, _("Proxy client interface not compiled."));
   return NULL;
}

#endif /* !ENABLE_PROXY */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
