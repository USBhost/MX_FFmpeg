/*
 *  VBI proxy daemon
 *
 *  Copyright (C) 2002-2004 Tom Zoerner (and others)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 *
 *
 *  Description:
 *
 *    This is the main module of the VBI proxy daemon.  Please refer to
 *    the manual page for information on the daemon's general purpose.
 *
 *    When started, the daemon will at first only create a named socket in
 *    /tmp for the devices given on the command line and wait for client
 *    connections.  When a client connects the VBI device is opened and
 *    configured for the requested services.  If no services are requested,
 *    the device is still opened, but acquisition not started.  When more
 *    clients connect, the daemon will reset service parameters and add them
 *    newly to the slicer in order of connection times, adjusting VBI device
 *    parameters as required and possible (e.g. enlarging VBI window.)
 *
 *    Client handling was originally derived from alevtd by Gerd Knorr, then
 *    adapted/extended for nxtvepg and again adapted/reduced for the VBI proxy
 *    by Tom Zoerner.
 *
 *
 *  $Log: proxyd.c,v $
 *  Revision 1.19  2008/07/26 06:22:24  mschimek
 *  Changed the license to GPLv2+ with Tom's permission.
 *
 *  Revision 1.18  2007/11/27 17:39:35  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.17  2007/08/27 06:45:24  mschimek
 *  vbi_proxyd_take_service_req, vbi_proxyd_take_message,
 *  vbi_proxyd_take_message: Replaced strncpy() by the faster a safer
 *  strlcpy().
 *  vbi_proxyd_signal_handler, vbi_proxyd_parse_argv: Replaced sprintf() by
 *  the safer snprintf().
 *
 *  Revision 1.16  2006/05/22 09:01:53  mschimek
 *  s/vbi_asprintf/asprintf.
 *
 *  Revision 1.15  2006/02/10 06:25:36  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.14  2005/01/20 01:39:15  mschimek
 *  gcc 4.0 char pointer signedness warnings.
 *
 *  Revision 1.13  2004/12/30 02:26:02  mschimek
 *  printf ptrdiff_t fixes.
 *
 *  Revision 1.12  2004/11/07 10:52:01  mschimek
 *  dprintf: s/proxyd/zvbid.
 *
 *  Revision 1.11  2004/10/25 16:56:26  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.10  2004/10/24 18:15:33  tomzo
 *  - added handling of norm changes
 *  - adapted for interface change to proxy-msg.c (split socket read/write func)
 *  - improved debug level handling (command line option -debug)
 *
 */

static const char rcsid[] = "$Id: proxyd.c,v 1.19 2008/07/26 06:22:24 mschimek Exp $";

#include "config.h"

#ifdef ENABLE_PROXY

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>

#include "src/vbi.h"
#include "src/io.h"
#include "src/bcd.h"
#include "src/proxy-msg.h"

#ifdef ENABLE_V4L2
#include <asm/types.h>
#include "src/videodev2k.h"    /* for setting device priority */
#endif

#define DBG_MSG 1
#define DBG_QU 2
#define DBG_CLNT 4
#define DBG_SCHED 8
#define dprintf(flags, fmt, arg...)					\
do {									\
	if (opt_debug_level & (flags))					\
		fprintf (stderr, "zvbid: " fmt, ## arg);		\
} while (0)

/* Macro to cast (void *) to (int) and backwards without compiler warning
** (note: 64-bit compilers warn when casting a pointer to an int) */
#define  PVOID2INT(X)    ((int)((long)(X)))
#define  INT2PVOID(X)    ((void *)((long)(X)))

/* ----------------------------------------------------------------------------
** This struct is one element in the slicer data queue
*/
typedef struct PROXY_QUEUE_s
{
        struct PROXY_QUEUE_s  * p_next;
        unsigned int            ref_count;
        unsigned int            use_count;

        int                     max_lines;
        int                     line_count;
        double                  timestamp;
        void                  * p_raw_data;
        vbi_sliced              lines[1];
} PROXY_QUEUE;

#define QUEUE_ELEM_SIZE(Q,C)  (sizeof(PROXY_QUEUE) + (sizeof(vbi_sliced) * ((C) - 1)))

/* ----------------------------------------------------------------------------
** Declaration of types of internal state variables
*/

/* Note mutex conventions:
** - mutex are only required for v4l devices which do not support select(2),
**   because only then a separate thread is started which blocks in read(2)
** - when both the client chain and a slicer queue mutex is required, the
**   client mutex is acquired first; order is important to prevent deadlocks
** - the master thread locks the client chain mutex only for write access,
**   i.e. if a client is added or removed
*/

typedef enum
{
        REQ_TOKEN_NONE,         /* this client is not allowed to switch channels */
        REQ_TOKEN_RECLAIM,      /* return of token will be requested */
        REQ_TOKEN_RELEASE,      /* waiting for client to release token */
        REQ_TOKEN_GRANT,        /* this client will be sent the token a.s.a.p. */
        REQ_TOKEN_GRANTED,      /* this client currently holds the token */
        REQ_TOKEN_RETURNED      /* this client has returned the token, but still 'owns' the channel */
} REQ_TOKEN_STATE;

#define REQ_CONTROLS_CHN(X) ((X) >= REQ_TOKEN_GRANTED)

/* client channel control scheduler state */
typedef struct
{
        REQ_TOKEN_STATE         token_state;
        vbi_bool                is_completed;
        int                     cycle_count;
        time_t                  last_start;
        time_t                  last_duration;
} VBIPROXY_CHN_STATE;

/* client connection state */
typedef enum
{
        REQ_STATE_WAIT_CON_REQ,
        REQ_STATE_WAIT_CLOSE,
        REQ_STATE_FORWARD,
        REQ_STATE_CLOSED,
} REQ_STATE;

#define SRV_MAX_DEVICES                 4
#define VBI_MAX_BUFFER_COUNT           32
#define VBI_MIN_STRICT                 -1
#define VBI_MAX_STRICT                  2
#define VBI_GET_SERVICE_P(PREQ,STRICT)  ((PREQ)->services + (signed)(STRICT) - VBI_MIN_STRICT)
#define VBI_RAW_SERVICES(SRV)           (((SRV) & (VBI_SLICED_VBI_625 | VBI_SLICED_VBI_525)) != 0)

/* this struct holds client-specific state and parameters */
typedef struct PROXY_CLNT_s
{
        struct PROXY_CLNT_s   * p_next;

        REQ_STATE               state;
        VBIPROXY_MSG_STATE      io;
        vbi_bool                endianSwap;
        VBI_PROXY_CLIENT_FLAGS  client_flags;
        int                     dev_idx;

        VBIPROXY_MSG            msg_buf;

        unsigned int            services[VBI_MAX_STRICT - VBI_MIN_STRICT + 1];
        unsigned int            all_services;
        int                     vbi_start[2];
        int                     vbi_count[2];
        int                     buffer_count;
        vbi_bool                buffer_overflow;
        PROXY_QUEUE           * p_sliced;

        vbi_channel_profile     chn_profile;
        VBIPROXY_CHN_STATE      chn_state;
        VBI_CHN_PRIO            chn_prio;
        VBI_PROXY_CHN_FLAGS     chn_status_ind;

} PROXY_CLNT;

/* this struct holds the state of a device */
typedef struct
{
        const char            * p_dev_name;
        char                  * p_sock_path;
        int                     pipe_fd;

        vbi_capture           * p_capture;
        vbi_raw_decoder       * p_decoder;
        int                     vbi_fd;
        VBI_DRIVER_API_REV      vbi_api;

        unsigned int            all_services;
        unsigned int            scanning;
        int                     max_lines;
        PROXY_QUEUE           * p_sliced;
        PROXY_QUEUE           * p_free;
        PROXY_QUEUE           * p_tmp_buf;

        VBI_CHN_PRIO            chn_prio;

        vbi_bool                use_thread;
        int                     wr_fd;
        vbi_bool                wait_for_exit;
        vbi_bool                thread_active;
        pthread_t               thread_id;
        pthread_cond_t          start_cond;
        pthread_mutex_t         start_mutex;
        pthread_mutex_t         queue_mutex;

} PROXY_DEV;

/* this struct holds the global state of the module */
typedef struct
{
        char                  * listen_ip;
        char                  * listen_port;
        vbi_bool                do_tcp_ip;
        int                     tcp_ip_fd;
        int                     max_conn;
        vbi_bool                should_exit;
        vbi_bool                chn_sched_alarm;

        PROXY_CLNT            * p_clnts;
        int                     clnt_count;
        pthread_mutex_t         clnt_mutex;

        PROXY_DEV               dev[4];
        int                     dev_count;

} PROXY_SRV;

#define SRV_CONNECT_TIMEOUT     60
#define SRV_STALLED_STATS_INTV  15
#define SRV_QUEUE_BUFFER_COUNT  10

#define DEFAULT_MAX_CLIENTS     10
#define DEFAULT_VBI_DEV_PATH    "/dev/vbi"
#define DEFAULT_VBI_DEVFS_PATH  "/dev/v4l/vbi"
#define DEFAULT_CHN_PRIO        VBI_CHN_PRIO_INTERACTIVE
#define DEFAULT_BUFFER_COUNT     8

#define MAX_DEV_ERROR_COUNT     10

/* ----------------------------------------------------------------------------
** Local variables
*/
static PROXY_SRV      proxy;

static char         * p_opt_log_name = NULL;
static int            opt_log_level = -1;
static int            opt_syslog_level = -1;
static vbi_bool       opt_no_detach = FALSE;
static vbi_bool       opt_kill_daemon = FALSE;
static unsigned int   opt_max_clients = DEFAULT_MAX_CLIENTS;
static unsigned int   opt_debug_level = 0;
static unsigned int   opt_buffer_count = DEFAULT_BUFFER_COUNT;

/* ----------------------------------------------------------------------------
** Add one buffer to the tail of a queue
** - slicer queue is organized so that new data is appended to the tail,
**   forwarded data is taken from the head
** - note that a buffer is not released from the slicer queue until all
**   clients have processed it's data; client structs hold a pointer to
**   the first unprocessed (by the respective client) buffer in the queue
*/
static void vbi_proxy_queue_add_tail( PROXY_QUEUE ** q, PROXY_QUEUE * p_buf )
{
   PROXY_QUEUE * p_last;

   dprintf(DBG_QU, "queue_add_tail: buffer 0x%lX\n", (long)p_buf);
   p_buf->p_next = NULL;

   if (*q != NULL)
   {
      assert(*q != p_buf);
      p_last = *q;
      while (p_last->p_next != NULL)
         p_last = p_last->p_next;

      p_last->p_next = p_buf;
   }
   else
      *q = p_buf;

   assert((*q != NULL) && ((*q)->p_next != *q));
}

/* ----------------------------------------------------------------------------
** Retrieve one buffer from the queue of unused buffers
** - checks if the buffer size still matches the current VBI format
**   if not, the buffer is re-allocated
*/
static PROXY_QUEUE * vbi_proxy_queue_get_free( PROXY_DEV * p_proxy_dev )
{
   PROXY_QUEUE * p_buf;

   pthread_mutex_lock(&p_proxy_dev->queue_mutex);

   p_buf = p_proxy_dev->p_free;
   if (p_buf != NULL)
   {
      p_proxy_dev->p_tmp_buf = p_buf;
      p_proxy_dev->p_free    = p_buf->p_next;

      pthread_mutex_unlock(&p_proxy_dev->queue_mutex);

      if (p_buf->max_lines != p_proxy_dev->max_lines)
      {
         /* max line parameter changed -> re-alloc the buffer */
         p_proxy_dev->p_tmp_buf = NULL;
         if (p_buf->p_raw_data != NULL)
            free(p_buf->p_raw_data);
         free(p_buf);

         p_buf = malloc(QUEUE_ELEM_SIZE(p_buf, p_proxy_dev->max_lines));
         p_buf->p_raw_data = NULL;
         p_buf->max_lines = p_proxy_dev->max_lines;
         p_proxy_dev->p_tmp_buf = p_buf;
      }

      /* add/remove "sub-buffer" for raw data */
      if (VBI_RAW_SERVICES(p_proxy_dev->all_services))
      {
         if (p_buf->p_raw_data == NULL)
            p_buf->p_raw_data = malloc(p_proxy_dev->max_lines * VBIPROXY_RAW_LINE_SIZE);
      }
      else
      {
         if (p_buf->p_raw_data != NULL)
            free(p_buf->p_raw_data);
         p_buf->p_raw_data = NULL;
      }

      p_buf->p_next     = NULL;
      p_buf->ref_count  = 0;
      p_buf->use_count  = 0;
   }
   else
      pthread_mutex_unlock(&p_proxy_dev->queue_mutex);

   dprintf(DBG_QU, "queue_get_free: buffer 0x%lX\n", (long)p_buf);
   return p_buf;
}

/* ----------------------------------------------------------------------------
** Add a buffer to the queue of unused buffers
** - there's no ordering between buffers in the free queue, hence we don't
**   care if the buffer is inserted at head or tail of the queue
*/
static void vbi_proxy_queue_add_free( PROXY_DEV * p_proxy_dev, PROXY_QUEUE * p_buf )
{
   dprintf(DBG_QU, "queue_add_free: buffer 0x%lX\n", (long)p_buf);

   p_buf->p_next = p_proxy_dev->p_free;
   p_proxy_dev->p_free = p_buf;
}

/* ----------------------------------------------------------------------------
** Decrease reference counter on a buffer, add back to free queue upon zero
** - called when a buffer has been processed for one client
*/
static void vbi_proxy_queue_release_sliced( PROXY_CLNT * req )
{
   PROXY_QUEUE * p_buf;
   PROXY_DEV   * p_proxy_dev;

   p_proxy_dev = proxy.dev + req->dev_idx;

   p_buf = req->p_sliced;
   req->p_sliced = p_buf->p_next;

   if (p_buf->ref_count > 0)
      p_buf->ref_count -= 1;

   if (p_buf->ref_count == 0)
   {
      assert(p_proxy_dev->p_sliced == p_buf);
      p_proxy_dev->p_sliced = p_buf->p_next;

      /* add the buffer to the free queue */
      p_buf->p_next = p_proxy_dev->p_free;
      p_proxy_dev->p_free = p_buf;
   }
}

/* ----------------------------------------------------------------------------
** Free all resources of all buffers in a queue
** - called upon stop of acquisition for all queues
*/
static void vbi_proxy_queue_release_all( int dev_idx )
{
   PROXY_DEV   * p_proxy_dev;
   PROXY_CLNT  * req;
   PROXY_QUEUE * p_next;

   p_proxy_dev = proxy.dev + dev_idx;

   pthread_mutex_lock(&p_proxy_dev->queue_mutex);
   while (p_proxy_dev->p_sliced != NULL)
   {
      p_next = p_proxy_dev->p_sliced->p_next;

      vbi_proxy_queue_add_free(p_proxy_dev, p_proxy_dev->p_sliced);

      p_proxy_dev->p_sliced = p_next;
   }

   for (req = proxy.p_clnts; req != NULL; req = req->p_next)
   {
      if (req->dev_idx == dev_idx)
      {
         req->p_sliced    = NULL;
      }
   }
   pthread_mutex_unlock(&p_proxy_dev->queue_mutex);
}

/* ----------------------------------------------------------------------------
** Free all resources of all buffers in a queue
** - called upon stop of acquisition for all queues
*/
static void vbi_proxy_queue_free_all( PROXY_QUEUE ** q )
{
   PROXY_QUEUE * p_next;

   while (*q != NULL)
   {
      p_next = (*q)->p_next;

      if ((*q)->p_raw_data != NULL)
         free((*q)->p_raw_data);

      free(*q);

      *q = p_next;
   }
}

/* ----------------------------------------------------------------------------
** Allocate buffers
** - determines number of required buffers and adds or removes buffers from queue
** - buffer count depends on
**   (i) minimum which is always allocated (>= number of raw buffers)
**   (ii) max. requested buffer count of all connected clients
**   (iii) number of clients (one spare for each client)
*/
static vbi_bool vbi_proxy_queue_allocate( int dev_idx )
{
   PROXY_DEV    * p_proxy_dev;
   PROXY_CLNT   * p_walk;
   PROXY_QUEUE  * p_buf;
   int  client_count;
   int  buffer_count;
   int  buffer_free;
   int  buffer_used;

   p_proxy_dev = proxy.dev + dev_idx;

   buffer_count = opt_buffer_count;
   client_count = 0;
   for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
   {
      if (p_walk->dev_idx == dev_idx)
      {
         client_count += 1;
         if (buffer_count < p_walk->buffer_count)
            buffer_count = p_walk->buffer_count;
      }
   }
   buffer_count += client_count;

   pthread_mutex_lock(&p_proxy_dev->queue_mutex);

   /* count buffers in sliced data output queue */
   buffer_used = 0;
   for (p_buf = p_proxy_dev->p_sliced; p_buf != NULL; p_buf = p_buf->p_next)
   {
      buffer_used += 1;
   }
   /* count buffers in free queue */
   buffer_free = 0;
   for (p_buf = p_proxy_dev->p_free; p_buf != NULL; p_buf = p_buf->p_next)
   {
      buffer_free += 1;
   }

   dprintf(DBG_MSG, "queue_allocate: need %d buffers, have %d+%d (free+used)\n", buffer_count, buffer_free, buffer_used);

   if (buffer_free + buffer_used > buffer_count)
   {
      /* too many buffers: first reclaim from free queue (possibly too many) */
      vbi_proxy_queue_free_all(&p_proxy_dev->p_free);
      buffer_free = 0;
   }
   /* XXX we could also force-free more buffers in out queue, but that may be overkill */

   while (buffer_free + buffer_used < buffer_count)
   {
      p_buf = malloc(QUEUE_ELEM_SIZE(p_buf, p_proxy_dev->max_lines));
      if (p_buf != NULL)
      {
         p_buf->p_raw_data = NULL;
         p_buf->max_lines = p_proxy_dev->max_lines;

         vbi_proxy_queue_add_free(p_proxy_dev, p_buf);
         buffer_free += 1;
      }
      else
      {
         dprintf(DBG_MSG, "queue_allocate: failed to allocate buffer (errno %d)\n", errno);
         break;
      }
   }

   pthread_mutex_unlock(&p_proxy_dev->queue_mutex);

   return ((unsigned int)(buffer_free + buffer_used)
	   >= opt_buffer_count + client_count);
}

/* ----------------------------------------------------------------------------
** Free the first buffer in the output queue by force
** - required if one client is blocked but others still active
** - client(s) will lose this frame's data
*/
static PROXY_QUEUE * vbi_proxy_queue_force_free( PROXY_DEV * p_proxy_dev )
{
   PROXY_CLNT   * req;

   pthread_mutex_lock(&proxy.clnt_mutex);
   pthread_mutex_lock(&p_proxy_dev->queue_mutex);

   if ((p_proxy_dev->p_free == NULL) && (p_proxy_dev->p_sliced != NULL))
   {
      dprintf(DBG_MSG, "queue_force_free: buffer 0x%lX\n", (long)p_proxy_dev->p_sliced);

      for (req = proxy.p_clnts; req != NULL; req = req->p_next)
      {
         if (req->p_sliced == p_proxy_dev->p_sliced)
         {
            vbi_proxy_queue_release_sliced(req);
         }
      }
   }

   pthread_mutex_unlock(&p_proxy_dev->queue_mutex);
   pthread_mutex_unlock(&proxy.clnt_mutex);

   return vbi_proxy_queue_get_free(p_proxy_dev);
}

/* ----------------------------------------------------------------------------
** Read sliced data and forward it to all clients
*/
static void vbi_proxyd_forward_data( int dev_idx )
{
   PROXY_QUEUE    * p_buf;
   PROXY_CLNT     * req;
   PROXY_DEV      * p_proxy_dev;
   struct timeval timeout;
   int    res;

   p_proxy_dev = proxy.dev + dev_idx;

   /* unlink a buffer from the free queue */
   p_buf = vbi_proxy_queue_get_free(p_proxy_dev);

   if (p_buf == NULL)
      p_buf = vbi_proxy_queue_force_free(p_proxy_dev);

   if (p_buf != NULL)
   {
      timeout.tv_sec  = 0;
      timeout.tv_usec = 0;

      if (VBI_RAW_SERVICES(p_proxy_dev->all_services) == FALSE)
      {
         res = vbi_capture_read_sliced(p_proxy_dev->p_capture, p_buf->lines,
                                       &p_buf->line_count, &p_buf->timestamp, &timeout);
      }
      else
      {
         res = vbi_capture_read(p_proxy_dev->p_capture,
                                p_buf->p_raw_data, p_buf->lines,
                                &p_buf->line_count, &p_buf->timestamp, &timeout);
      }

      if (res > 0)
      {
         assert(p_buf->line_count < p_buf->max_lines);
         pthread_mutex_lock(&proxy.clnt_mutex);
         pthread_mutex_lock(&p_proxy_dev->queue_mutex);

         for (req = proxy.p_clnts; req != NULL; req = req->p_next)
         {
            if ( (req->dev_idx == dev_idx) &&
                 (req->state == REQ_STATE_FORWARD) &&
                 (req->all_services != 0) )
            {
               p_buf->ref_count += 1;

               if (req->p_sliced == NULL)
                  req->p_sliced = p_buf;
            }
         }

         pthread_mutex_unlock(&p_proxy_dev->queue_mutex);
         pthread_mutex_unlock(&proxy.clnt_mutex);
      }
      else if (res < 0)
      {
         /* XXX abort upon error (esp. EBUSY) */
         perror("VBI read");
      }

      pthread_mutex_lock(&p_proxy_dev->queue_mutex);

      if (p_buf->ref_count > 0)
         vbi_proxy_queue_add_tail(&p_proxy_dev->p_sliced, p_buf);
      else
         vbi_proxy_queue_add_free(p_proxy_dev, p_buf);

      p_proxy_dev->p_tmp_buf = NULL;
      pthread_mutex_unlock(&p_proxy_dev->queue_mutex);
   }
   else
      dprintf(DBG_MSG, "forward_data: queue overflow\n");
}

/* ----------------------------------------------------------------------------
** Process a norm change notification
** - query driver for new norm: if sucessful, this overrides information
**   provided by the client (client may also provide 0)
** - trigger sending of norm change indication to all clients if scanning changes:
**   -> clients must re-apply for their services; note norm changes which don't
**   affect the scanning (e.g. PAL<->SECAM) are ignored
*/
static void vbi_proxyd_update_scanning( int dev_idx, PROXY_CLNT * req, int scanning )
{
   PROXY_DEV    * p_proxy_dev;
   PROXY_CLNT   * p_walk;
   unsigned int new_scanning;

   p_proxy_dev = proxy.dev + dev_idx;

   if (p_proxy_dev->p_capture != NULL)
   {
      /* if the info is coming from a client verify it */
      if (req != NULL)
      {
         new_scanning = vbi_capture_get_scanning(p_proxy_dev->p_capture);
         if (new_scanning <= 0)
         {
            if ((scanning == 525) || (scanning == 625))
               new_scanning = scanning;
         }
      }
      else
         new_scanning = scanning;

      if (new_scanning != p_proxy_dev->scanning)
      {
         dprintf(DBG_MSG, "update_scanning: changed from %d to %d\n", p_proxy_dev->scanning, new_scanning);
         p_proxy_dev->scanning = new_scanning;

         /* trigger sending of change indication to all clients except the caller */
         for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
         {
            if ( (p_walk->dev_idx == dev_idx) &&
                 ((p_walk->client_flags & VBI_PROXY_CLIENT_NO_STATUS_IND) == 0) )
            {
               p_walk->chn_status_ind |= VBI_PROXY_CHN_NORM;
            }
         }
      }
   }
}

/* ----------------------------------------------------------------------------
** Helper function: calculate timespec for 50ms timeout
*/
static void vbi_proxyd_calc_timeout_ms( struct timespec * p_tsp, int msecs )
{
   struct timeval  tv;

   gettimeofday(&tv, NULL);
   tv.tv_usec += msecs * 1000L;
   if (tv.tv_usec > 1000 * 1000L)
   {
      tv.tv_sec  += 1;
      tv.tv_usec -= 1000 * 1000;
   }
   p_tsp->tv_sec  = tv.tv_sec;
   p_tsp->tv_nsec = tv.tv_usec * 1000;
}

/* ----------------------------------------------------------------------------
** Clean up after thread cancellation: signal waiting master thread
*/
static void vbi_proxyd_acq_thread_cleanup( void * pvoid_arg )
{
   PROXY_DEV * p_proxy_dev;
   int         dev_idx;

   dev_idx     = PVOID2INT(pvoid_arg);
   p_proxy_dev = proxy.dev + dev_idx;

   dprintf(DBG_QU, "acq thread cleanup: signaling master (%d)\n", p_proxy_dev->wait_for_exit);

   pthread_mutex_lock(&p_proxy_dev->start_mutex);
   if (p_proxy_dev->wait_for_exit)
   {
      pthread_cond_signal(&p_proxy_dev->start_cond);
   }
   if (p_proxy_dev->p_tmp_buf != NULL)
   {
      vbi_proxy_queue_add_free(p_proxy_dev, p_proxy_dev->p_tmp_buf);
      p_proxy_dev->p_tmp_buf = NULL;
   }
   p_proxy_dev->thread_active = FALSE;
   pthread_mutex_unlock(&p_proxy_dev->start_mutex);
}

/* ----------------------------------------------------------------------------
** Main loop for acquisition thread for devices that don't support select(2)
*/
static void * vbi_proxyd_acq_thread( void * pvoid_arg )
{
   PROXY_DEV * p_proxy_dev;
   int         dev_idx;
   int         ret;
   char        byte_buf[1];
   sigset_t    sigmask;

   dev_idx     = PVOID2INT(pvoid_arg);
   p_proxy_dev = proxy.dev + dev_idx;

   /* block signals which are handled by main thread */
   sigemptyset(&sigmask);
   sigaddset(&sigmask, SIGHUP);
   sigaddset(&sigmask, SIGINT);
   sigaddset(&sigmask, SIGTERM);
   pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

   pthread_cleanup_push(vbi_proxyd_acq_thread_cleanup, pvoid_arg);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

   p_proxy_dev->thread_active = TRUE;

   pthread_mutex_lock(&p_proxy_dev->start_mutex);
   pthread_cond_signal(&p_proxy_dev->start_cond);
   pthread_mutex_unlock(&p_proxy_dev->start_mutex);

   while (p_proxy_dev->wait_for_exit == FALSE)
   {
      /* read data from the VBI device and append the buffer to all client queues
      ** note: this function blocks in read(2) until data is available */
      vbi_proxyd_forward_data(dev_idx);

      /* wake up the master thread to process client queues */
      ret = write(p_proxy_dev->wr_fd, byte_buf, 1);

      if ((ret < 0) && (errno != EAGAIN))
      {
         dprintf(DBG_MSG, "acq_thread: write error to pipe: %d\n", errno);
         break;
      }
      else if (ret != 1)
         dprintf(DBG_MSG, "acq_thread: pipe overflow\n");
   }

   pthread_cleanup_pop(1);
   pthread_exit(0);

   return NULL;
}

/* ----------------------------------------------------------------------------
** Stop acquisition thread
*/
static void vbi_proxyd_stop_acq_thread( PROXY_DEV * p_proxy_dev )
{
   struct timespec tsp;
   int ret;
   int vbi_fd;

   assert(p_proxy_dev->use_thread);
   pthread_mutex_lock(&p_proxy_dev->start_mutex);

   if (p_proxy_dev->thread_active)
   {
      p_proxy_dev->wait_for_exit = TRUE;
      pthread_cancel(p_proxy_dev->thread_id);

      vbi_proxyd_calc_timeout_ms(&tsp, 50);
      ret = pthread_cond_timedwait(&p_proxy_dev->start_cond, &p_proxy_dev->start_mutex, &tsp);
      if (ret != 0)
      {  /* thread did not stop within 50ms: probably blocked in read with no incoming data */
         /* dirty hack: force to wake up by closing the file handle */
         vbi_fd = vbi_capture_fd(p_proxy_dev->p_capture);
         close(vbi_fd);
         dprintf(DBG_MSG, "stop_acq_thread: thread did not exit (%d): closed VBI filehandle %d\n", ret, vbi_fd);

         vbi_proxyd_calc_timeout_ms(&tsp, 50);
         ret = pthread_cond_timedwait(&p_proxy_dev->start_cond, &p_proxy_dev->start_mutex, &tsp);
      }
      if (ret == 0)
      {
         ret = pthread_join(p_proxy_dev->thread_id, NULL);
         if (ret == 0)
            dprintf(DBG_MSG, "stop_acq_thread: acq thread killed sucessfully\n");
         else
            dprintf(DBG_MSG, "stop_acq_thread: pthread_join failed: %d (%s)\n", errno, strerror(errno));
      }
   }

   close(p_proxy_dev->vbi_fd);
   close(p_proxy_dev->wr_fd);
   p_proxy_dev->vbi_fd = -1;
   p_proxy_dev->wr_fd = -1;
   p_proxy_dev->use_thread = FALSE;

   pthread_mutex_unlock(&p_proxy_dev->start_mutex);
}

/* ----------------------------------------------------------------------------
** Start a thread to block in read(2) for devices that don't support select(2)
*/
static vbi_bool vbi_proxyd_start_acq_thread( int dev_idx )
{
   PROXY_DEV * p_proxy_dev;
   int       pipe_fds[2];
   vbi_bool  result = FALSE;

   p_proxy_dev = proxy.dev + dev_idx;
   p_proxy_dev->use_thread    = TRUE;
   p_proxy_dev->wait_for_exit = FALSE;
   p_proxy_dev->thread_active = FALSE;

   if (pipe(pipe_fds) == 0)
   {
      p_proxy_dev->vbi_fd = pipe_fds[0];
      p_proxy_dev->wr_fd  = pipe_fds[1];

      fcntl(p_proxy_dev->vbi_fd, F_SETFL, O_NONBLOCK);
      fcntl(p_proxy_dev->wr_fd,  F_SETFL, O_NONBLOCK);

      /* start thread */
      pthread_mutex_lock(&p_proxy_dev->start_mutex);
      if (pthread_create(&p_proxy_dev->thread_id, NULL,
                         vbi_proxyd_acq_thread, INT2PVOID(dev_idx)) == 0)
      {
         dprintf(DBG_MSG, "acquisiton thread started: "
	         "id %ld, device %ld, pipe rd/wr %d/%d\n",
		 (long)p_proxy_dev->thread_id,
		 (long)(p_proxy_dev - proxy.dev),
		 p_proxy_dev->vbi_fd, p_proxy_dev->wr_fd);

         /* wait for the slave to report the initialization result */
         pthread_cond_wait(&p_proxy_dev->start_cond, &p_proxy_dev->start_mutex);
         pthread_mutex_unlock(&p_proxy_dev->start_mutex);

         result = p_proxy_dev->thread_active;
      }
      else
         dprintf(DBG_MSG, "start_acq_thread: pthread_create: %d (%s)\n", errno, strerror(errno));
   }
   else
      dprintf(DBG_MSG, "start_acq_thread: create pipe: %d (%s)\n", errno, strerror(errno));

   return result;
}

/* ----------------------------------------------------------------------------
** Stop VBI acquisition (after the last client quit)
*/
static void vbi_proxy_stop_acquisition( PROXY_DEV * p_proxy_dev )
{
   if (p_proxy_dev->p_capture != NULL)
   {
      dprintf(DBG_MSG, "stop_acquisition: stopping (prev. services 0x%X)\n", p_proxy_dev->all_services);

      if (p_proxy_dev->use_thread)
         vbi_proxyd_stop_acq_thread(p_proxy_dev);

      vbi_capture_delete(p_proxy_dev->p_capture);
      p_proxy_dev->p_capture = NULL;
      p_proxy_dev->p_decoder = NULL;
      p_proxy_dev->vbi_fd = -1;

      vbi_proxy_queue_free_all(&p_proxy_dev->p_free);
      vbi_proxy_queue_free_all(&p_proxy_dev->p_sliced);
   }
}

/* ----------------------------------------------------------------------------
** Open capture device (for the first client)
** - does not yet any services yet
*/
static vbi_bool vbi_proxy_start_acquisition( int dev_idx, char ** pp_errorstr )
{
   PROXY_DEV    * p_proxy_dev;
   char         * p_errorstr;
   vbi_bool       result;

   p_proxy_dev = proxy.dev + dev_idx;
   result      = FALSE;

   /* assign dummy error string if necessary */
   p_errorstr = NULL;
   if (pp_errorstr == NULL)
      pp_errorstr = &p_errorstr;

   p_proxy_dev->vbi_api = VBI_API_V4L2;
   p_proxy_dev->p_capture = vbi_capture_v4l2_new(p_proxy_dev->p_dev_name, opt_buffer_count,
                                                 NULL, -1, pp_errorstr, opt_debug_level);
   if (p_proxy_dev->p_capture == NULL)
   {
      p_proxy_dev->vbi_api = VBI_API_V4L1;
      p_proxy_dev->p_capture = vbi_capture_v4l_new(p_proxy_dev->p_dev_name, p_proxy_dev->scanning,
                                                   NULL, -1, pp_errorstr, opt_debug_level);
   }

   if (p_proxy_dev->p_capture != NULL)
   {
      p_proxy_dev->p_decoder = vbi_capture_parameters(p_proxy_dev->p_capture);
      if (p_proxy_dev->p_decoder != NULL)
      {
         /* allocate buffer queue for sliced output data */
         vbi_proxy_queue_allocate(dev_idx);

         p_proxy_dev->chn_prio = VBI_CHN_PRIO_INTERACTIVE;

         /* get file handle for select() to wait for VBI data */
         if ((vbi_capture_get_fd_flags(p_proxy_dev->p_capture) & VBI_FD_HAS_SELECT) != 0)
         {
            p_proxy_dev->vbi_fd = vbi_capture_fd(p_proxy_dev->p_capture);
            result = (p_proxy_dev->vbi_fd != -1);
         }
         else
            result = vbi_proxyd_start_acq_thread(dev_idx);
      }
      else
         dprintf(DBG_MSG, "start_acquisition: capture device has no slicer!?\n");
   }

   if (result == FALSE)
   {
      vbi_proxy_stop_acquisition(p_proxy_dev);
   }

   if ((pp_errorstr == &p_errorstr) && (p_errorstr != NULL))
      free(p_errorstr);

   return result;
}

/* ----------------------------------------------------------------------------
** Update service mask after a client was added or closed
** - TODO: update buffer_count
*/
static vbi_bool vbi_proxyd_update_services( int dev_idx, PROXY_CLNT * p_new_req,
                                            int new_req_strict, char ** pp_errorstr )
{
   PROXY_CLNT   * req;
   PROXY_CLNT   * p_walk;
   PROXY_DEV    * p_proxy_dev;
   unsigned int   dev_services;
   unsigned int   tmp_services;
   unsigned int   next_srv;
   int            strict;
   int            strict2;
   vbi_bool       is_first;
   vbi_bool       result;

   p_proxy_dev = proxy.dev + dev_idx;

   if (p_proxy_dev->p_capture == NULL)
   {
      /* cpture device not opened yet */
      /* check if other clients have any services enabled */
      next_srv = 0;
      for (req = proxy.p_clnts; req != NULL; req = req->p_next)
         for (strict = VBI_MIN_STRICT; strict <= VBI_MAX_STRICT; strict++)
            next_srv |= *VBI_GET_SERVICE_P(req, strict);

      if (next_srv != 0)
      {
         result = vbi_proxy_start_acquisition(dev_idx, pp_errorstr);
      }
      else
      {
         /* XXX FIXME must open device at least once to query API
         ** XXX must be change since device open may fail with EBUSY: better leave device open while users are connected */
         if (p_proxy_dev->vbi_api == VBI_API_UNKNOWN)
         {
            vbi_proxy_start_acquisition(dev_idx, NULL);
            vbi_proxy_stop_acquisition(p_proxy_dev);
         }
         result = TRUE;
      }
   }
   else
      result = FALSE;

   if (p_proxy_dev->p_capture != NULL)
   {
      /* terminate acq thread because we're about to suspend capturing */
      if (p_proxy_dev->use_thread)
         vbi_proxyd_stop_acq_thread(p_proxy_dev);

      /* XXX TODO: possible optimization: reduce number of update_service calls:
      **           (1) collect all services first; (2) add services at 3 strict levels; (3) update all_services for all clients */
      is_first = TRUE;
      dev_services = 0;
      for (req = proxy.p_clnts; req != NULL; req = req->p_next)
      {
         if ( (req->dev_idx == dev_idx) &&
              (req->state == REQ_STATE_FORWARD) )
         {
            req->all_services = 0;

            for (strict = VBI_MIN_STRICT; strict <= VBI_MAX_STRICT; strict++)
            {
               tmp_services = *VBI_GET_SERVICE_P(req, strict);
               if (tmp_services != 0)
               {
                  next_srv = 0;
                  for (strict2 = strict + 1; strict2 <= VBI_MAX_STRICT; strict2++)
                     if ((next_srv |= *VBI_GET_SERVICE_P(req, strict2)) != 0)
                        break;
                  /* search following clients if more services follow */
                  if (next_srv == 0)
                     for (p_walk = req->p_next; p_walk != NULL; p_walk = p_walk->p_next)
                        for (strict2 = VBI_MIN_STRICT; strict2 <= VBI_MAX_STRICT; strict2++)
                           if ((next_srv |= *VBI_GET_SERVICE_P(p_walk, strict2)) != 0)
                              goto next_srv_found;  // break^2

                  next_srv_found:
                  dprintf(DBG_MSG, "service_update: fd %d: add services=0x%X strict=%d final=%d\n", req->io.sock_fd, tmp_services, strict, (next_srv == 0));

                  tmp_services =
                     vbi_capture_update_services( p_proxy_dev->p_capture,
                                                  is_first, (next_srv == 0),
                                                  tmp_services, strict,
                                                  /* return error strings only for the new client */
                                                  (((req == p_new_req) &&
                                                    (strict == new_req_strict)) ? pp_errorstr : NULL) );

                  dev_services |= tmp_services;
                  req->all_services |= tmp_services;
                  is_first = FALSE;

                  /* must not mask out client service bits unless upon a new request; afterwards
                  ** services must be cached and re-applied, e.g. in case the norm changes back */
                  if (req == p_new_req)
                     *VBI_GET_SERVICE_P(req, strict) &= tmp_services;
               }
            }
         }
      }

      /* check if scanning changed
      ** (even if all clients suspended: some might be waiting for scanning change) */
      vbi_proxyd_update_scanning(dev_idx, NULL, p_proxy_dev->p_decoder->scanning);

      if (dev_services != 0)
      {
         p_proxy_dev->all_services = dev_services;
         p_proxy_dev->max_lines = p_proxy_dev->p_decoder->count[0]
                                + p_proxy_dev->p_decoder->count[1];

         /* grow/shrink buffer queue for sliced output data */
         vbi_proxy_queue_allocate(dev_idx);

         dprintf(DBG_MSG, "service_update: new service mask 0x%X, max.lines=%d, scanning=%d\n", dev_services, p_proxy_dev->max_lines, p_proxy_dev->scanning);

         if ((vbi_capture_get_fd_flags(p_proxy_dev->p_capture) & VBI_FD_HAS_SELECT) != 0)
         {
            result = TRUE;
         }
         else
            result = vbi_proxyd_start_acq_thread(dev_idx);
      }
      else
      {  /* no services set: not an error if clien't didn't request any */
         result = is_first;
      }

      if ((dev_services == 0) || (result == FALSE))
      {
         /* no clients remaining or acq start failed -> stop acquisition */
         vbi_proxy_stop_acquisition(p_proxy_dev);
      }
   }

   return result;
}

/* ----------------------------------------------------------------------------
** Process a client's service request
** - either during connect request or later upon service request
** - note if it's not the first request a different "strictness" may be given;
**   must remember strictness for each service to be able to re-apply for the
**   same services mask to the decoder later
*/
static vbi_bool vbi_proxyd_take_service_req( PROXY_CLNT * req,
                                             unsigned int new_services, int new_strict,
                                             char * errormsg )
{
   char        * p_errorstr;
   PROXY_DEV   * p_proxy_dev;
   int           strict;
   vbi_bool      result;

   p_proxy_dev = proxy.dev + req->dev_idx;
   p_errorstr  = NULL;

   /* remove new services from all strict levels */
   for (strict = VBI_MIN_STRICT; strict <= VBI_MAX_STRICT; strict++)
      *VBI_GET_SERVICE_P(req, strict) &= ~ new_services;

   /* add new services at the given level of strictness */
   *VBI_GET_SERVICE_P(req, new_strict) |= new_services;

   /* merge with other client's requests and pass to the device */
   result = vbi_proxyd_update_services(req->dev_idx, req, new_strict, &p_errorstr);

   if ( (result == FALSE) ||
        ( ((req->all_services & new_services) == 0) && (new_services != 0) ))
   {
      if (p_errorstr != NULL)
      {
         strlcpy(errormsg, p_errorstr, VBIPROXY_ERROR_STR_MAX_LENGTH);
         errormsg[VBIPROXY_ERROR_STR_MAX_LENGTH - 1] = 0;
      }
      else if ( ((*VBI_GET_SERVICE_P(req, new_strict) & new_services) == 0) &&
                (new_services != 0) )
      {
         strlcpy(errormsg, "Sorry, proxy cannot capture any of the requested data services.",
                 VBIPROXY_ERROR_STR_MAX_LENGTH);
         errormsg[VBIPROXY_ERROR_STR_MAX_LENGTH - 1] = 0;
      }
      else
      {
         strlcpy(errormsg, "Internal error in service update.", VBIPROXY_ERROR_STR_MAX_LENGTH);
         errormsg[VBIPROXY_ERROR_STR_MAX_LENGTH - 1] = 0;
      }
      result = FALSE;
   }

   if (p_proxy_dev->p_decoder != NULL)
   {
      /* keep a copy of the VBI line ranges: used as upper/lower boundaries in
      ** sliced data messages MUST NOT be changed later (at least not increased)
      ** even if services change, to avoid overflowing clients' buffers */
      req->vbi_start[0] = p_proxy_dev->p_decoder->start[0];
      req->vbi_count[0] = p_proxy_dev->p_decoder->count[0];
      req->vbi_start[1] = p_proxy_dev->p_decoder->start[1];
      req->vbi_count[1] = p_proxy_dev->p_decoder->count[1];
   }

   if (p_errorstr != NULL)
      free(p_errorstr);

   return result;
}

/* ----------------------------------------------------------------------------
** Search for client which owns the token
*/
static PROXY_CLNT * vbi_proxyd_get_token_owner( int dev_idx )
{
   PROXY_DEV   * p_proxy_dev;
   PROXY_CLNT  * p_walk;
   PROXY_CLNT  * p_owner;

   p_proxy_dev = proxy.dev + dev_idx;
   p_owner = NULL;

   for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
   {
      if (p_walk->dev_idx == dev_idx)
      {
         switch (p_walk->chn_state.token_state)
         {
            case REQ_TOKEN_NONE:
               break;
            case REQ_TOKEN_GRANT:
            case REQ_TOKEN_RETURNED:
            case REQ_TOKEN_RECLAIM:
            case REQ_TOKEN_RELEASE:
            case REQ_TOKEN_GRANTED:
               assert(p_owner == NULL);
               p_owner = p_walk;
               break;
            default:
               assert(FALSE);  /* invalid state */
               break;
         }
      }
   }
   return p_owner;
}

/* ----------------------------------------------------------------------------
** Grant token to a given client
** - basically implements a matrix of all possible token states in current and
**   future token owner: however only one may have non-"NONE" state
** - if the token is still in posession of a different client the request will
**   fail, but the token is reclaimed from the other client
*/
static vbi_bool vbi_proxyd_token_grant( PROXY_CLNT * req )
{
   PROXY_CLNT  * p_owner;
   vbi_bool token_free = TRUE;

   switch (req->chn_state.token_state)
   {
      case REQ_TOKEN_NONE:
         p_owner = vbi_proxyd_get_token_owner(req->dev_idx);
         if ( (p_owner == NULL) ||
              ( (p_owner->chn_state.token_state == REQ_TOKEN_GRANT) ||
                (p_owner->chn_state.token_state == REQ_TOKEN_RETURNED) ))
         {
            /* token is free or grant message not yet sent -> immediately grant to new client */
            req->chn_state.token_state = REQ_TOKEN_GRANT;
            if (p_owner != NULL)
               p_owner->chn_state.token_state = REQ_TOKEN_NONE;
         }
         else
         {  /* have to reclaim token from previous owner first */
            if (p_owner->chn_state.token_state != REQ_TOKEN_RELEASE)
               p_owner->chn_state.token_state = REQ_TOKEN_RECLAIM;

            token_free = FALSE;
         }
         break;

      case REQ_TOKEN_GRANT:
         /* client is already about to be granted the token -> nothing to do */
         break;
      case REQ_TOKEN_RECLAIM:
         /* reclaim message not yet sent -> just return to GRANTED state */
         req->chn_state.token_state = REQ_TOKEN_GRANTED;
         break;
      case REQ_TOKEN_RELEASE:
         /* reclaim already sent -> must re-assign token */
         req->chn_state.token_state = REQ_TOKEN_GRANT;
         break;
      case REQ_TOKEN_GRANTED:
      case REQ_TOKEN_RETURNED:
         /* client is still in control of the channel: nothing to do */
         break;
      default:
         assert(FALSE);  /* invalid state */
         break;
   }
   return token_free;
}

/* ----------------------------------------------------------------------------
** Adapt channel scheduler state when switching away from a channel
*/
static void vbi_proxyd_channel_completed( PROXY_CLNT * req, time_t whence )
{
   PROXY_CLNT  * p_walk;

   assert(REQ_CONTROLS_CHN(req->chn_state.token_state));

   req->chn_state.last_duration = whence - req->chn_state.last_start;
   req->chn_state.is_completed = TRUE;
   req->chn_state.cycle_count += 1;

   dprintf(DBG_MSG, "channel_schedule: fd %d terminated (duration %d, cycle #%d)\n", req->io.sock_fd, (int)req->chn_state.last_duration, req->chn_state.cycle_count);

   if (req->chn_state.cycle_count > 2)
   {
      /* cycle counter overflow: only values 1, 2 allowed (plus 0 for new requests)
      ** -> reduce all counters by one */
      dprintf(DBG_MSG, "channel_schedule: dev #%d: leveling cycle counters\n", req->dev_idx);

      for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
         if (p_walk->dev_idx == req->dev_idx)
            if (p_walk->chn_state.cycle_count > 0)
               p_walk->chn_state.cycle_count -= 1;
   }
   else if (req->chn_state.cycle_count == 1)
   {
      /* cycle counter hops always to maximum, i.e. from 0 to 2 so that a new request
      ** has immediately highest prio, but is only scheduled once before the others */
      for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
         if (p_walk->dev_idx == req->dev_idx)
            if (p_walk->chn_state.cycle_count >= 2)
               break;

      if (p_walk != NULL)
         req->chn_state.cycle_count = 2;
   }
}

/* ----------------------------------------------------------------------------
** Adapt channel scheduler state when switching away from a channel
*/
static void vbi_proxyd_channel_stopped( PROXY_CLNT * req )
{
   time_t  now = time(NULL);

   assert(REQ_CONTROLS_CHN(req->chn_state.token_state));

   if ( (req->chn_state.is_completed == FALSE) &&
        (now - req->chn_state.last_start >= req->chn_profile.min_duration) )
   {
      vbi_proxyd_channel_completed(req, now);
   }
   req->chn_state.is_completed  = FALSE;

   if (req->chn_state.token_state == REQ_TOKEN_GRANTED)
      req->chn_state.token_state = REQ_TOKEN_RECLAIM;
   else
      req->chn_state.token_state = REQ_TOKEN_NONE;
}

/* ----------------------------------------------------------------------------
** Calculate next timer for scheduler
** - since there's only one alarm signal, the nearest timeout on all devices is searched
*/
static void vbi_proxyd_channel_timer_update( void )
{
   PROXY_CLNT  * p_walk;
   PROXY_DEV   * p_proxy_dev;
   time_t        rest;
   time_t        next_sched;
   time_t        now;

   now        = time(NULL);
   next_sched = 0;

   for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
   {
      p_proxy_dev = proxy.dev + p_walk->dev_idx;
      if ( (p_proxy_dev->chn_prio == VBI_CHN_PRIO_BACKGROUND) &&
           REQ_CONTROLS_CHN(p_walk->chn_state.token_state) &&
           (p_walk->chn_state.is_completed == FALSE) )
      {
         rest = p_walk->chn_profile.min_duration - (now - p_walk->chn_state.last_start);
         if ((rest > 0) && ((rest < next_sched) || (next_sched == 0)))
            next_sched = rest;
         else if (rest < 0)
            next_sched = 1;
      }
      /* XXX TODO: set timer to supervise TOKEN RELEASE */
   }

   if (next_sched != 0)
      dprintf(DBG_MSG, "channel_timer_update: set alarm timer in %d secs\n", (int)next_sched);

   alarm(next_sched);
   proxy.chn_sched_alarm = FALSE;
}

/* ----------------------------------------------------------------------------
** Determine which client's channel request is granted
*/
static PROXY_CLNT *
vbi_proxyd_channel_schedule( int dev_idx )
{
   PROXY_CLNT  * p_walk;
   PROXY_CLNT  * p_sched;
   PROXY_CLNT  * p_active;
   PROXY_DEV   * p_proxy_dev;
   time_t        now;

   p_proxy_dev = proxy.dev + dev_idx;
   p_sched     = NULL;
   p_active    = NULL;
   now         = time(NULL);

   for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
   {
      if ( (p_walk->dev_idx == dev_idx) &&
           (p_walk->chn_profile.is_valid) &&
           (p_walk->chn_prio == VBI_CHN_PRIO_BACKGROUND) )
      {
         /* if this client's channel is currently active, check if the reservation has expired */
         if (REQ_CONTROLS_CHN(p_walk->chn_state.token_state))
         {
            if ( (now - p_walk->chn_state.last_start >= p_walk->chn_profile.min_duration) &&
                 (p_walk->chn_state.is_completed == FALSE) )
            {
               vbi_proxyd_channel_completed(p_walk, now);
            }
            p_active = p_walk;
         }
         dprintf(DBG_MSG, "channel_schedule: fd %d: active=%d compl=%d sub-prio=0x%02X cycles#%d min-dur=%d\n", p_walk->io.sock_fd, REQ_CONTROLS_CHN(p_walk->chn_state.token_state), p_walk->chn_state.is_completed, p_walk->chn_profile.sub_prio, p_walk->chn_state.cycle_count, (int)p_walk->chn_profile.min_duration);

         if (p_sched != NULL)
         {
            if ( p_walk->chn_state.cycle_count
                   + ((REQ_CONTROLS_CHN(p_walk->chn_state.token_state) && p_walk->chn_state.is_completed) ? 1 : 0)
                     < p_sched->chn_state.cycle_count
                       + ((REQ_CONTROLS_CHN(p_sched->chn_state.token_state) && p_sched->chn_state.is_completed) ? 1 : 0) )
            {  /* this one is already done (more often) */
               dprintf(DBG_SCHED, "channel_schedule: fd %d wins by cycle count\n", p_walk->io.sock_fd);
               p_sched = p_walk;
            }
            else if (p_walk->chn_profile.sub_prio > p_sched->chn_profile.sub_prio)
            {  /* higher priority found */
               dprintf(DBG_SCHED, "channel_schedule: fd %d wins by sub-prio\n", p_walk->io.sock_fd);
               p_sched = p_walk;
            }
            else if (p_walk->chn_profile.sub_prio == p_sched->chn_profile.sub_prio)
            {  /* same priority */
               if ( REQ_CONTROLS_CHN(p_walk->chn_state.token_state) &&
                    !p_walk->chn_state.is_completed )
               {  /* this one is still active */
                  dprintf(DBG_SCHED, "channel_schedule: fd %d wins by being already active and non-complete\n", p_walk->io.sock_fd);
                  p_sched = p_walk;
               }
               else if ( REQ_CONTROLS_CHN(p_sched->chn_state.token_state) &&
                         p_sched->chn_state.is_completed )
               {  /* prev. selected one was completed -> choose next */
                  dprintf(DBG_SCHED, "channel_schedule: fd %d wins because active one is completed\n", p_walk->io.sock_fd);
                  p_sched = p_walk;
               }
               else if ( !REQ_CONTROLS_CHN(p_walk->chn_state.token_state) &&
                         !REQ_CONTROLS_CHN(p_sched->chn_state.token_state) )
               {  /* none active -> first come first serve */
                  if ( (p_walk->chn_state.last_start < p_sched->chn_state.last_start) ||
                       ( (p_walk->chn_state.last_start == p_sched->chn_state.last_start) &&
                         (p_walk->chn_profile.min_duration < p_sched->chn_profile.min_duration) ))
                  {
                     dprintf(DBG_SCHED, "channel_schedule: fd %d wins because longer non-active\n", p_walk->io.sock_fd);
                     p_sched = p_walk;
                  }
               }
            }
         }
         else
            p_sched = p_walk;
      }
   }

   if ((p_sched != p_active) && (p_active != NULL))
   {
      vbi_proxyd_channel_stopped(p_active);
   }

   return p_sched;
}

/* ----------------------------------------------------------------------------
** Update channel, after channel change request or connection release
** - if only background-prio clients are connected, the scheduler decides;
**   else the channel is switched (if the client matches the daemon's max prio)
*/
static vbi_bool
vbi_proxyd_channel_update( int dev_idx, PROXY_CLNT * req, vbi_bool forced_switch )
{
   PROXY_CLNT  * p_walk;
   PROXY_CLNT  * p_sched;
   PROXY_DEV   * p_proxy_dev;
   VBI_CHN_PRIO  max_chn_prio;
   vbi_bool      result;

   p_proxy_dev = proxy.dev + dev_idx;
   result      = FALSE;

   /* determine new max. channel priority */
   max_chn_prio = VBI_CHN_PRIO_BACKGROUND;
   for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
      if ((p_walk->dev_idx == dev_idx) && (p_walk->chn_prio > max_chn_prio))
         max_chn_prio = p_walk->chn_prio;

   if (p_proxy_dev->chn_prio != max_chn_prio)
   {
#if defined(ENABLE_V4L2) && defined(VIDIOC_S_PRIORITY)
      if (p_proxy_dev->vbi_api == VBI_API_V4L2)
      {
         enum v4l2_priority v4l2_prio = max_chn_prio;
         int fd = vbi_capture_fd(p_proxy_dev->p_capture);
         if (fd != -1)
         {
            if (ioctl(fd, VIDIOC_S_PRIORITY, &v4l2_prio) != 0)
            {
               dprintf(DBG_MSG, "Failed to set register v4l2 channel prio to %d: %d (%s)\n", p_proxy_dev->chn_prio, errno, strerror(errno));
            }
            else
            {
               ioctl(fd, VIDIOC_G_PRIORITY, &v4l2_prio);
               dprintf(DBG_MSG, "channel_update: dev #%d: setting v4l2 channel prio to %d (was %d) (dev prio is %d)\n", dev_idx, max_chn_prio, p_proxy_dev->chn_prio, v4l2_prio);
            }
         }
      }
#endif
      /* save the priority registered with the device */
      p_proxy_dev->chn_prio = max_chn_prio;
   }

   /* non-bg prio OR channel has already been switched -> clear scheduler active flag */
   if ( (max_chn_prio > VBI_CHN_PRIO_BACKGROUND) || forced_switch )
   {
      for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
         if ((p_walk->dev_idx == dev_idx) && REQ_CONTROLS_CHN(p_walk->chn_state.token_state))
            vbi_proxyd_channel_stopped(p_walk);
   }

   if (max_chn_prio == VBI_CHN_PRIO_BACKGROUND)
   {  /* background -> let scheduler decide */
      p_sched = vbi_proxyd_channel_schedule(dev_idx);
   }
   else if ( (req != NULL) &&
             (req->chn_prio == max_chn_prio) )
   {  /* non-background prio -> latest request wins */
      p_sched = req;
   }
   else
   {  /* reject switch by priority */
      p_sched = NULL;
   }

   if ( (p_sched != NULL) &&
        (max_chn_prio == VBI_CHN_PRIO_BACKGROUND) &&
        (REQ_CONTROLS_CHN(p_sched->chn_state.token_state) == FALSE) )
   {
      if ( vbi_proxyd_token_grant(p_sched) )
      {
         p_sched->chn_state.is_completed  = FALSE;
         p_sched->chn_state.last_duration = 0;
         p_sched->chn_state.last_start    = time(NULL);

         /* return TRUE if requested channel control can be granted immediately */
         result = (p_sched == req);
      }
   }
   else
   {  /* no channel change is allowed or required */

      /* flush-only flag: assume client has already done the switch -> must flush VBI buffers */
      if (forced_switch)
      {
         vbi_capture_flush(p_proxy_dev->p_capture);
      }
   }

   if (max_chn_prio == VBI_CHN_PRIO_BACKGROUND)
      vbi_proxyd_channel_timer_update();

   return result;
}

/* ----------------------------------------------------------------------------
** Flush after channel change
*/
static void vbi_proxyd_channel_flush( int dev_idx, PROXY_CLNT * req )
{
   PROXY_CLNT  * p_walk;
   PROXY_DEV   * p_proxy_dev;

   req = req;

   p_proxy_dev = proxy.dev + dev_idx;

   if (p_proxy_dev->p_capture != NULL)
   {
      /* flush capture buffers */
      vbi_capture_flush(p_proxy_dev->p_capture);

      /* flush slicer output buffer queues */
      vbi_proxy_queue_release_all(dev_idx);
   }

   /* trigger sending of change indication to all clients except the caller */
   for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
   {
      if ( (p_walk->dev_idx == dev_idx) &&
           ((p_walk->client_flags & VBI_PROXY_CLIENT_NO_STATUS_IND) == 0) )
      {
         p_walk->chn_status_ind |= VBI_PROXY_CHN_FLUSH;
      }
   }
}

/* ----------------------------------------------------------------------------
** Check channel scheduling on all devices for expired timers
*/
static void vbi_proxyd_channel_timer( void )
{
   PROXY_CLNT  * p_walk;
   PROXY_DEV   * p_proxy_dev;
   int           dev_idx;
   time_t        now;
   vbi_bool      do_schedule;
   int           user_count;

   now = time(NULL);

   for (dev_idx = 0; dev_idx < proxy.dev_count; dev_idx++)
   {
      p_proxy_dev = proxy.dev + dev_idx;
      do_schedule = FALSE;
      user_count  = 0;

      if (p_proxy_dev->chn_prio == VBI_CHN_PRIO_BACKGROUND)
      {
         for (p_walk = proxy.p_clnts; p_walk != NULL; p_walk = p_walk->p_next)
         {
            if (p_walk->dev_idx == dev_idx)
            {
               if ( REQ_CONTROLS_CHN(p_walk->chn_state.token_state) &&
                    (p_walk->chn_state.is_completed == FALSE) &&
                    (now - p_walk->chn_state.last_start >= p_walk->chn_profile.min_duration) )
               {
                  do_schedule = TRUE;
               }
               user_count += 1;
            }
         }

         if (do_schedule && (user_count > 1))
         {
            dprintf(DBG_MSG, "schedule_timer: schedule device #%d\n", dev_idx);

            vbi_proxyd_channel_update(dev_idx, NULL, FALSE);
         }
      }
   }
}

/* ----------------------------------------------------------------------------
** Process client ioctl request
*/
static vbi_bool
vbi_proxyd_take_ioctl_req( PROXY_CLNT * req, int request, void * p_arg_data,
                           unsigned int arg_size, int * p_result, int * p_errcode )
{
   PROXY_DEV   * p_proxy_dev;
   vbi_bool      req_perm;
   int           size;
   int           vbi_fd;
   vbi_bool      opened = FALSE;
   vbi_bool      result = FALSE;

   p_proxy_dev = proxy.dev + req->dev_idx;

   if (p_proxy_dev->p_capture == NULL)
   {
      vbi_proxy_start_acquisition(req->dev_idx, NULL);
      opened = TRUE;
   }

   vbi_fd = vbi_capture_fd(p_proxy_dev->p_capture);
   if (vbi_fd != -1)
   {
      size = vbi_proxy_msg_check_ioctl(p_proxy_dev->vbi_api, request, p_arg_data, &req_perm);
      if ((size >= 0) && (size == (int) arg_size))
      {
         /* FIXME */
         if ( (req_perm == FALSE) ||
              (req->chn_prio >= p_proxy_dev->chn_prio) ||
              REQ_CONTROLS_CHN(req->chn_state.token_state) )
         {
            /* TODO: possibly update norm, flush channel */
            errno = 0;
            /* do the actual ioctl */
            *p_result  = ioctl(vbi_capture_fd(p_proxy_dev->p_capture), request, p_arg_data);
            *p_errcode = errno;

            result = TRUE;
         }
         else
            dprintf(DBG_MSG, "take_ioctl_req: no permission\n");
      }
      else
         dprintf(DBG_MSG, "take_ioctl_req: invalid ioctl 0x%X or size %d\n", request, arg_size);
   }

   if (opened)
   {
      vbi_proxy_stop_acquisition(p_proxy_dev);
   }

   return result;
}

/* ----------------------------------------------------------------------------
** Close the connection to the client
** - frees all allocated resources
*/
static void vbi_proxyd_close( PROXY_CLNT * req, vbi_bool close_all )
{
   close_all = close_all;

   if (req->state != REQ_STATE_CLOSED)
   {
      dprintf(DBG_MSG, "close: fd %d\n", req->io.sock_fd);
      vbi_proxy_msg_logger(LOG_INFO, req->io.sock_fd, 0, "closing connection", NULL);

      vbi_proxy_msg_close_io(&req->io);

      pthread_mutex_lock(&proxy.dev[req->dev_idx].queue_mutex);

      while (req->p_sliced != NULL)
      {
         vbi_proxy_queue_release_sliced(req);
      }

      pthread_mutex_unlock(&proxy.dev[req->dev_idx].queue_mutex);

      req->state = REQ_STATE_CLOSED;
   }
}

/* ----------------------------------------------------------------------------
** Initialize a request structure for a new client and add it to the list
*/
static void vbi_proxyd_add_connection( int listen_fd, int dev_idx, vbi_bool isLocal )
{
   PROXY_CLNT * req;
   PROXY_CLNT * p_walk;
   int sock_fd;

   isLocal = isLocal;

   sock_fd = vbi_proxy_msg_accept_connection(listen_fd);
   if (sock_fd != -1)
   {
      req = calloc(sizeof(*req), 1);
      if (req != NULL)
      {
         dprintf(DBG_MSG, "add_connection: fd %d\n", sock_fd);

         req->state         = REQ_STATE_WAIT_CON_REQ;
         req->io.lastIoTime = time(NULL);
         req->io.sock_fd    = sock_fd;
         req->dev_idx       = dev_idx;
         req->chn_prio      = DEFAULT_CHN_PRIO;

         pthread_mutex_lock(&proxy.clnt_mutex);

         /* append request to the end of the chain
         ** note: order is significant for priority in adding services */
         if (proxy.p_clnts != NULL)
         {
            p_walk = proxy.p_clnts;
            while (p_walk->p_next != NULL)
               p_walk = p_walk->p_next;

            p_walk->p_next = req;
         }
         else
            proxy.p_clnts = req;

         proxy.clnt_count  += 1;

         pthread_mutex_unlock(&proxy.clnt_mutex);
      }
      else
         dprintf(DBG_MSG, "add_connection: fd %d: virtual memory exhausted, abort\n", sock_fd);
   }
}

/* ----------------------------------------------------------------------------
** Initialize state for a new device
*/
static void vbi_proxyd_add_device( const char * p_dev_name )
{
   PROXY_DEV  * p_proxy_dev;

   if (proxy.dev_count < SRV_MAX_DEVICES)
   {
      p_proxy_dev = proxy.dev + proxy.dev_count;

      p_proxy_dev->p_dev_name  = p_dev_name;
      p_proxy_dev->p_sock_path = vbi_proxy_msg_get_socket_name(p_dev_name);
      p_proxy_dev->pipe_fd = -1;
      p_proxy_dev->vbi_fd  = -1;
      p_proxy_dev->wr_fd   = -1;

      /* initialize synchonization facilities */
      pthread_cond_init(&p_proxy_dev->start_cond, NULL);
      pthread_mutex_init(&p_proxy_dev->start_mutex, NULL);
      pthread_mutex_init(&p_proxy_dev->queue_mutex, NULL);

      proxy.dev_count += 1;
   }
}

/* ----------------------------------------------------------------------------
** Transmit one buffer of sliced data
** - returns FALSE upon I/O error
** - also returns a "blocked" flag which is TRUE if not all data could be written
**   can be used by the caller to "stuff" the pipe, i.e. write a series of messages
**   until the pipe is full
** - XXX optimization required: don't copy the block (required however if client
**   doesn't want all services)
*/
static vbi_bool vbi_proxyd_send_sliced( PROXY_CLNT * req, vbi_bool * p_blocked )
{
   VBIPROXY_MSG * p_msg;
   uint32_t  msg_size;
   vbi_bool  result = FALSE;
   int max_lines;
   int idx;

   if ((req != NULL) && (p_blocked != NULL) && (req->p_sliced != NULL))
   {
      if (VBI_RAW_SERVICES(req->all_services))
         msg_size = VBIPROXY_SLICED_IND_SIZE(0, req->p_sliced->max_lines);
      else
         msg_size = VBIPROXY_SLICED_IND_SIZE(req->p_sliced->line_count, 0);

      msg_size += sizeof(VBIPROXY_MSG_HEADER);
      p_msg = malloc(msg_size);

      /* filter for services requested by this client */
      max_lines = req->vbi_count[0] + req->vbi_count[1];
      p_msg->body.sliced_ind.timestamp = req->p_sliced->timestamp;
      p_msg->body.sliced_ind.sliced_lines = 0;
      p_msg->body.sliced_ind.raw_lines = 0;

      /* XXX TODO allow both raw and sliced in the same message */
      if (VBI_RAW_SERVICES(req->all_services) == FALSE)
      {
         for (idx = 0; (idx < req->p_sliced->line_count) && (idx < max_lines); idx++)
         {
            if ((req->p_sliced->lines[idx].id & req->all_services) != 0)
            {
               memcpy(p_msg->body.sliced_ind.u.sliced + p_msg->body.sliced_ind.sliced_lines,
                      req->p_sliced->lines + idx, sizeof(vbi_sliced));
               p_msg->body.sliced_ind.sliced_lines += 1;
            }
         }
      }
      else
      {
         if (req->p_sliced->p_raw_data != NULL)
         {
            memcpy(p_msg->body.sliced_ind.u.raw,
                   req->p_sliced->p_raw_data,
                   VBIPROXY_RAW_LINE_SIZE * req->p_sliced->max_lines);
            p_msg->body.sliced_ind.raw_lines = req->p_sliced->max_lines;
         }
      }
      msg_size = VBIPROXY_SLICED_IND_SIZE(p_msg->body.sliced_ind.sliced_lines,
                                          p_msg->body.sliced_ind.raw_lines);

      vbi_proxy_msg_write(&req->io, MSG_TYPE_SLICED_IND, msg_size, p_msg, TRUE);

      if (vbi_proxy_msg_handle_write(&req->io, p_blocked))
      {
         /* if the last block could not be transmitted fully, quit the loop */
         if (req->io.writeLen > 0)
         {
            dprintf(DBG_CLNT, "send_sliced: socket blocked\n");
            *p_blocked = TRUE;
         }
         result = TRUE;
      }
   }
   else
      dprintf(DBG_MSG, "send_sliced: illegal NULL ptr params\n");

   return result;
}

/* ----------------------------------------------------------------------------
** Checks the size of a message from client to server
*/
static vbi_bool vbi_proxyd_check_msg( VBIPROXY_MSG * pMsg, vbi_bool * pEndianSwap )
{
   VBIPROXY_MSG_HEADER * pHead = &pMsg->head;
   VBIPROXY_MSG_BODY   * pBody = &pMsg->body;
   unsigned int len = pMsg->head.len;
   vbi_bool result = FALSE;

   switch (pHead->type)
   {
      case MSG_TYPE_CONNECT_REQ:
         if ( (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->connect_req)) &&
              (memcmp(pBody->connect_req.magics.protocol_magic, VBIPROXY_MAGIC_STR, VBIPROXY_MAGIC_LEN) == 0) )
         {
            if (pBody->connect_req.magics.endian_magic == VBIPROXY_ENDIAN_MAGIC)
            {
               *pEndianSwap = FALSE;
               result       = TRUE;
            }
            else if (pBody->connect_req.magics.endian_magic == VBIPROXY_ENDIAN_MISMATCH)
            {
               *pEndianSwap = TRUE;
               result       = TRUE;
            }
         }
         break;

      case MSG_TYPE_SERVICE_REQ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->service_req));
         break;

      case MSG_TYPE_CHN_TOKEN_REQ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_token_req));
         break;

      case MSG_TYPE_CHN_NOTIFY_REQ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_notify_req));
         break;

      case MSG_TYPE_CHN_SUSPEND_REQ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_notify_req));
         break;

      case MSG_TYPE_CHN_IOCTL_REQ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) +
                          VBIPROXY_CHN_IOCTL_REQ_SIZE(pBody->chn_ioctl_req.arg_size));
         break;

      case MSG_TYPE_CHN_RECLAIM_CNF:
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->chn_reclaim_cnf));
         break;

      case MSG_TYPE_CLOSE_REQ:
         result = (len == sizeof(VBIPROXY_MSG_HEADER));
         break;

      case MSG_TYPE_DAEMON_PID_REQ:
         result = ( (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->daemon_pid_req)) &&
                    (memcmp(pBody->daemon_pid_req.magics.protocol_magic, VBIPROXY_MAGIC_STR, VBIPROXY_MAGIC_LEN) == 0) &&
                    (pBody->daemon_pid_req.magics.endian_magic == VBIPROXY_ENDIAN_MAGIC) );
         break;

      case MSG_TYPE_DAEMON_PID_CNF:
         /* note: this is a daemon reply but accepted here since the daemon sends it to itself */
         result = (len == sizeof(VBIPROXY_MSG_HEADER) + sizeof(pBody->daemon_pid_cnf));
         break;

      case MSG_TYPE_CONNECT_CNF:
      case MSG_TYPE_CONNECT_REJ:
      case MSG_TYPE_SERVICE_CNF:
      case MSG_TYPE_SERVICE_REJ:
      case MSG_TYPE_SLICED_IND:
      case MSG_TYPE_CHN_TOKEN_CNF:
      case MSG_TYPE_CHN_TOKEN_IND:
      case MSG_TYPE_CHN_NOTIFY_CNF:
      case MSG_TYPE_CHN_SUSPEND_CNF:
      case MSG_TYPE_CHN_SUSPEND_REJ:
      case MSG_TYPE_CHN_IOCTL_CNF:
      case MSG_TYPE_CHN_IOCTL_REJ:
      case MSG_TYPE_CHN_RECLAIM_REQ:
      case MSG_TYPE_CHN_CHANGE_IND:
         dprintf(DBG_MSG, "check_msg: recv client msg %d (%s) at server side\n", pHead->type, vbi_proxy_msg_debug_get_type_str(pHead->type));
         result = FALSE;
         break;

      default:
         dprintf(DBG_MSG, "check_msg: unknown msg #%d\n", pHead->type);
         result = FALSE;
         break;
   }

   if (result == FALSE)
      dprintf(DBG_MSG, "check_msg: illegal msg: len=%d, type=%d (%s)\n", len, pHead->type, vbi_proxy_msg_debug_get_type_str(pHead->type));

   return result;
}

/* ----------------------------------------------------------------------------
** Handle message from client
** - note: consistancy checks were already done by the I/O handler
**   except for higher level messages (must be checked by acqctl module)
** - implemented as a matrix: "switch" over server state, and "if" cascades
**   over message type
** - XXX warning: inbound messages use the same buffer as outbound!
**   must have finished evaluating the message before assembling the reply
*/
static vbi_bool vbi_proxyd_take_message( PROXY_CLNT *req, VBIPROXY_MSG * pMsg )
{
   VBIPROXY_MSG_BODY * pBody = &pMsg->body;
   vbi_bool result = FALSE;

   dprintf(DBG_CLNT, "take_message: fd %d: recv msg type %d (%s)\n", req->io.sock_fd, pMsg->head.type, vbi_proxy_msg_debug_get_type_str(pMsg->head.type));

   switch (pMsg->head.type)
   {
      case MSG_TYPE_CONNECT_REQ:
         if (req->state == REQ_STATE_WAIT_CON_REQ)
         {
            if (pBody->connect_req.magics.protocol_compat_version == VBIPROXY_COMPAT_VERSION)
            {
               dprintf(DBG_MSG, "New client: fd %d: '%s' pid=%d services=0x%X\n", req->io.sock_fd, pBody->connect_req.client_name, pBody->connect_req.pid, pBody->connect_req.services);

               /* if provided, update norm hint (used for first client on ancient v4l1 drivers only) */
               if (pBody->connect_req.scanning != 0)
                  proxy.dev[req->dev_idx].scanning = pBody->connect_req.scanning;

               /* enable forwarding of captured data (must be set before processing request!) */
               req->state = REQ_STATE_FORWARD;

               req->buffer_count = pBody->connect_req.buffer_count;
               req->client_flags = pBody->connect_req.client_flags;  /* XXX TODO (timeout supression) */

               /* must make very sure strict is within bounds, because it's used as array index */
               if (pBody->connect_req.strict < VBI_MIN_STRICT)
                  pBody->connect_req.strict = VBI_MIN_STRICT;
               else if (pBody->connect_req.strict > VBI_MAX_STRICT)
                  pBody->connect_req.strict = VBI_MAX_STRICT;

               if ( vbi_proxyd_take_service_req(req, pBody->connect_req.services,
						pBody->connect_req.strict,
						(char *) req->msg_buf.body.connect_rej.errorstr) )
               { 
                  /* open & service initialization succeeded -> reply with confirm */
                  vbi_proxy_msg_fill_magics(&req->msg_buf.body.connect_cnf.magics);
                  strlcpy((char *) req->msg_buf.body.connect_cnf.dev_vbi_name,
                          proxy.dev[req->dev_idx].p_dev_name, VBIPROXY_DEV_NAME_MAX_LENGTH);
                  req->msg_buf.body.connect_cnf.dev_vbi_name[VBIPROXY_DEV_NAME_MAX_LENGTH - 1] = 0;
                  req->msg_buf.body.connect_cnf.pid = getpid();
                  req->msg_buf.body.connect_cnf.vbi_api_revision = proxy.dev[req->dev_idx].vbi_api;
                  req->msg_buf.body.connect_cnf.daemon_flags = ((opt_debug_level > 0) ? VBI_PROXY_DAEMON_NO_TIMEOUTS : 0);

                  req->msg_buf.body.connect_cnf.services = req->all_services;
                  if (proxy.dev[req->dev_idx].p_decoder != NULL)
                  {
                     req->msg_buf.body.connect_cnf.dec = *proxy.dev[req->dev_idx].p_decoder;
                     req->msg_buf.body.connect_cnf.dec.pattern = NULL;
                  }
                  else
                  {  /* acquisition not running: if the request is still considered sucessful
                     ** this is only possible if no services were requested */
                     memset(&req->msg_buf.body.connect_cnf.dec, 0,
                            sizeof(req->msg_buf.body.connect_cnf.dec));
                     req->msg_buf.body.connect_cnf.dec.start[0] = -1;
                     req->msg_buf.body.connect_cnf.dec.start[1] = -1;
                  }

                  vbi_proxy_msg_write(&req->io, MSG_TYPE_CONNECT_CNF,
                                      sizeof(req->msg_buf.body.connect_cnf),
                                      &req->msg_buf, FALSE);
               }
               else
               {
                  vbi_proxy_msg_fill_magics(&req->msg_buf.body.connect_rej.magics);

                  vbi_proxy_msg_write(&req->io, MSG_TYPE_CONNECT_REJ,
                                      sizeof(req->msg_buf.body.connect_rej),
                                      &req->msg_buf, FALSE);

                  /* drop the connection after sending the reject message */
                  req->state = REQ_STATE_WAIT_CLOSE;
               }
            }
            else
            {  /* client uses incompatible protocol version */
               vbi_proxy_msg_fill_magics(&req->msg_buf.body.connect_rej.magics);
               strlcpy((char *) req->msg_buf.body.connect_rej.errorstr,
                       "Incompatible proxy protocol version", VBIPROXY_ERROR_STR_MAX_LENGTH);
               req->msg_buf.body.connect_rej.errorstr[VBIPROXY_ERROR_STR_MAX_LENGTH - 1] = 0;
               vbi_proxy_msg_write(&req->io, MSG_TYPE_CONNECT_REJ,
                                   sizeof(req->msg_buf.body.connect_rej),
                                   &req->msg_buf, FALSE);
               /* drop the connection */
               req->state = REQ_STATE_WAIT_CLOSE;
            }
            result = TRUE;
         }
         break;

      case MSG_TYPE_DAEMON_PID_REQ:
         if (req->state == REQ_STATE_WAIT_CON_REQ)
         {  /* this message can be sent instead of a connect request */
            vbi_proxy_msg_fill_magics(&req->msg_buf.body.daemon_pid_cnf.magics);
            req->msg_buf.body.daemon_pid_cnf.pid = getpid();
            vbi_proxy_msg_write(&req->io, MSG_TYPE_DAEMON_PID_CNF,
                                sizeof(req->msg_buf.body.daemon_pid_cnf),
                                &req->msg_buf, FALSE);
            req->state = REQ_STATE_WAIT_CLOSE;
            result = TRUE;
         }
         break;

      case MSG_TYPE_SERVICE_REQ:
         if (req->state == REQ_STATE_FORWARD)
         {
            if (pBody->service_req.reset)
               memset(req->services, 0, sizeof(req->services));

            dprintf(DBG_MSG, "Update client: fd %d services: 0x%X (was %X)\n", req->io.sock_fd, pBody->service_req.services, req->all_services);

            /* flush all buffers in this client's queue */
            pthread_mutex_lock(&proxy.dev[req->dev_idx].queue_mutex);
            while (req->p_sliced != NULL)
            {
               vbi_proxy_queue_release_sliced(req);
            }
            pthread_mutex_unlock(&proxy.dev[req->dev_idx].queue_mutex);

            if ( vbi_proxyd_take_service_req(req, pBody->service_req.services,
					     pBody->service_req.strict,
					     (char *) req->msg_buf.body.service_rej.errorstr) )
            {
               if (proxy.dev[req->dev_idx].p_decoder != NULL)
               {
                  req->msg_buf.body.service_cnf.dec = *proxy.dev[req->dev_idx].p_decoder;
                  req->msg_buf.body.connect_cnf.dec.pattern = NULL;
               }
               else
               {  /* acquisition not running: if the request is still considered sucessful
                  ** this is only possible if no services were requested */
                  memset(&req->msg_buf.body.connect_cnf.dec, 0,
                         sizeof(req->msg_buf.body.connect_cnf.dec));
                  req->msg_buf.body.connect_cnf.dec.start[0] = -1;
                  req->msg_buf.body.connect_cnf.dec.start[1] = -1;
               }
               req->msg_buf.body.service_cnf.services = req->all_services;

               vbi_proxy_msg_write(&req->io, MSG_TYPE_SERVICE_CNF,
                                   sizeof(req->msg_buf.body.service_cnf),
                                   &req->msg_buf, FALSE);
            }
            else
            {
               vbi_proxy_msg_write(&req->io, MSG_TYPE_SERVICE_REJ,
                                   sizeof(req->msg_buf.body.service_rej),
                                   &req->msg_buf, FALSE);
            }
            result = TRUE;
         }
         break;

      case MSG_TYPE_CHN_TOKEN_REQ:
         if (req->state == REQ_STATE_FORWARD)
         {
            dprintf(DBG_MSG, "channel token request: fd %d: prio=%d sub-prio=0x%02X\n", req->io.sock_fd, pBody->chn_token_req.chn_prio, pBody->chn_token_req.chn_profile.sub_prio);

            /* update channel description and profile */
            req->chn_prio = pBody->chn_token_req.chn_prio;
            req->chn_profile = pBody->chn_token_req.chn_profile;
            memset(&req->chn_state, 0, sizeof(req->chn_state));

            /* XXX TODO: return elements: permitted, non_excl */
            memset(&req->msg_buf.body.chn_token_cnf, 0, sizeof(req->msg_buf.body.chn_token_cnf));
            vbi_proxyd_channel_update(req->dev_idx, req, FALSE);
            if (req->chn_state.token_state == REQ_TOKEN_GRANT)
            {
               req->chn_state.token_state = REQ_TOKEN_GRANTED;
               req->msg_buf.body.chn_token_cnf.token_ind = TRUE;
            }
            else
            {
               req->msg_buf.body.chn_token_cnf.token_ind = FALSE;
            }
            vbi_proxy_msg_write(&req->io, MSG_TYPE_CHN_TOKEN_CNF,
                                sizeof(req->msg_buf.body.chn_token_cnf),
                                &req->msg_buf, FALSE);
            result = TRUE;
         }
         break;

      case MSG_TYPE_CHN_NOTIFY_REQ:
         if (req->state == REQ_STATE_FORWARD)
         {
            vbi_bool chn_upd = FALSE;
            vbi_bool chn_forced = FALSE;
            dprintf(DBG_MSG, "channel notify: fd %d: flags=0x%X scanning=%d\n", req->io.sock_fd, pBody->chn_notify_req.notify_flags, pBody->chn_notify_req.scanning);

            if (pBody->chn_notify_req.notify_flags & VBI_PROXY_CHN_NORM)
            {
               /* query (verify) new scanning -> inform all clients (line count changes) */
               vbi_proxyd_update_scanning(req->dev_idx, req, pBody->chn_notify_req.scanning);
            }

            if (pBody->chn_notify_req.notify_flags & VBI_PROXY_CHN_FAIL)
            {
               // XXX TODO: ignore if client hasn't got the token
               //           else inform scheduler: 
            }
            if (pBody->chn_notify_req.notify_flags & VBI_PROXY_CHN_FLUSH)
            {
               vbi_proxyd_channel_flush(req->dev_idx, req);
               chn_upd = TRUE;
               chn_forced = ! REQ_CONTROLS_CHN(req->chn_state.token_state);
            }
            if (pBody->chn_notify_req.notify_flags & VBI_PROXY_CHN_RELEASE)
            {
               if (req->chn_state.token_state != REQ_TOKEN_NONE)
               {
                  req->chn_state.token_state = REQ_TOKEN_NONE;
                  chn_upd = TRUE;
               }
               req->chn_profile.is_valid = FALSE;
            }
            else if (pBody->chn_notify_req.notify_flags & VBI_PROXY_CHN_TOKEN)
            {
               req->chn_state.token_state = REQ_TOKEN_RETURNED;
               chn_upd = TRUE;
            }

            if (chn_upd)
               vbi_proxyd_channel_update(req->dev_idx, req, chn_forced);

            memset(&req->msg_buf.body.chn_notify_cnf, 0, sizeof(req->msg_buf.body.chn_notify_cnf));
            req->msg_buf.body.chn_notify_cnf.scanning = proxy.dev[req->dev_idx].scanning;

            vbi_proxy_msg_write(&req->io, MSG_TYPE_CHN_NOTIFY_CNF,
                                sizeof(req->msg_buf.body.chn_notify_cnf),
                                &req->msg_buf, FALSE);
            req->chn_status_ind = VBI_PROXY_CHN_NONE;
            result = TRUE;
         }
         break;

      case MSG_TYPE_CHN_SUSPEND_REQ:
         /* XXX TODO */
         vbi_proxy_msg_write(&req->io, MSG_TYPE_CHN_SUSPEND_REJ,
                             sizeof(req->msg_buf.body.chn_suspend_rej),
                             &req->msg_buf, FALSE);
         result = TRUE;
         break;

      case MSG_TYPE_CHN_IOCTL_REQ:
         if (req->state == REQ_STATE_FORWARD)
         {
            /* XXX TODO: message may be longer than pre-allocated message buffer */

            if ( vbi_proxyd_take_ioctl_req(req, req->msg_buf.body.chn_ioctl_req.request,
                                                req->msg_buf.body.chn_ioctl_req.arg_data,
                                                req->msg_buf.body.chn_ioctl_req.arg_size,
                                                &req->msg_buf.body.chn_ioctl_cnf.result,
                                                &req->msg_buf.body.chn_ioctl_cnf.errcode) )
            {
               /* note: argsize and arg_data unchanged from req. message */
               dprintf(DBG_MSG, "channel control ioctl: fd %d: request=0x%X result=%d errno=%d\n", req->io.sock_fd, req->msg_buf.body.chn_ioctl_req.request, req->msg_buf.body.chn_ioctl_cnf.result, req->msg_buf.body.chn_ioctl_cnf.errcode);

               vbi_proxy_msg_write(&req->io, MSG_TYPE_CHN_IOCTL_CNF,
                                   VBIPROXY_CHN_IOCTL_CNF_SIZE(req->msg_buf.body.chn_ioctl_req.arg_size),
                                   &req->msg_buf, FALSE);
            }
            else
            {
               vbi_proxy_msg_write(&req->io, MSG_TYPE_CHN_IOCTL_REJ,
                                   sizeof(req->msg_buf.body.chn_ioctl_rej),
                                   &req->msg_buf, FALSE);
            }
            result = TRUE;
         }
         break;

      case MSG_TYPE_CHN_RECLAIM_CNF:
         if (req->chn_state.token_state == REQ_TOKEN_RELEASE)
         {
            dprintf(DBG_MSG, "channel token reclain confirm: fd %d\n", req->io.sock_fd);
            req->chn_state.token_state = REQ_TOKEN_NONE;
            vbi_proxyd_channel_update(req->dev_idx, NULL, FALSE);
         }
         result = TRUE;
         break;

      case MSG_TYPE_CLOSE_REQ:
         /* close the connection */
         vbi_proxyd_close(req, FALSE);
         result = TRUE;
         break;

      default:
         /* unknown message or client-only message */
         dprintf(DBG_MSG, "take_message: protocol error: unexpected message type %d (%s)\n", pMsg->head.type, vbi_proxy_msg_debug_get_type_str(pMsg->head.type));
         break;
   }

   if (result == FALSE)
      dprintf(DBG_MSG, "take_message: message type %d (%s, len %d) not expected in state %d\n", pMsg->head.type, vbi_proxy_msg_debug_get_type_str(pMsg->head.type), pMsg->head.len, req->state);

   return result;
}

/* ----------------------------------------------------------------------------
** Set bits for all active sockets in fd_set for select syscall
*/
static int vbi_proxyd_get_fd_set( fd_set * rd, fd_set * wr )
{
   PROXY_CLNT   * req;
   PROXY_DEV    * p_proxy_dev;
   int            dev_idx;
   int            max_fd;

   max_fd = 0;

   /* add TCP/IP and UNIX-domain listening sockets */
   if ((proxy.max_conn == 0) || (proxy.clnt_count < proxy.max_conn))
   {
      if (proxy.tcp_ip_fd != -1)
      {
         FD_SET(proxy.tcp_ip_fd, rd);
         if (proxy.tcp_ip_fd > max_fd)
             max_fd = proxy.tcp_ip_fd;
      }
   }

   /* add listening sockets and VBI devices, if currently opened */
   p_proxy_dev = proxy.dev;
   for (dev_idx = 0; dev_idx < proxy.dev_count; dev_idx++, p_proxy_dev++)
   {
      if (p_proxy_dev->pipe_fd != -1)
      {
         FD_SET(p_proxy_dev->pipe_fd, rd);
         if (p_proxy_dev->pipe_fd > max_fd)
             max_fd = p_proxy_dev->pipe_fd;
      }

      if (p_proxy_dev->vbi_fd != -1)
      {
         FD_SET(p_proxy_dev->vbi_fd, rd);
         if (p_proxy_dev->vbi_fd > max_fd)
            max_fd = p_proxy_dev->vbi_fd;
      }
   }

   /* add client connection sockets */
   for (req = proxy.p_clnts; req != NULL; req = req->p_next)
   {
      /* read and write are exclusive and write takes precedence over read
      ** (i.e. read only if no write is pending or if a read operation has already been started)
      */
      if ( vbi_proxy_msg_read_idle(&req->io) == FALSE )
      {
         FD_SET(req->io.sock_fd, rd);
      }
      else
      if ( (vbi_proxy_msg_write_idle(&req->io) == FALSE) ||
           (req->p_sliced != NULL) || (req->chn_status_ind != VBI_PROXY_CHN_NONE) )
      {
         FD_SET(req->io.sock_fd, wr);
      }
      else
         FD_SET(req->io.sock_fd, rd);

      if (req->io.sock_fd > max_fd)
          max_fd = req->io.sock_fd;
   }

   return max_fd;
}

/* ----------------------------------------------------------------------------
** Proxy daemon central connection handling
*/
static void vbi_proxyd_handle_client_sockets( fd_set * rd, fd_set * wr )
{
   PROXY_CLNT    *req;
   PROXY_CLNT    *prev, *tmp;
   vbi_bool      io_blocked;
   time_t now = time(NULL);

   /* handle active connections */
   for (req = proxy.p_clnts, prev = NULL; req != NULL; )
   {
      io_blocked = FALSE;

      if ( FD_ISSET(req->io.sock_fd, rd) &&
           vbi_proxy_msg_write_idle(&req->io) )
      {
         /* incoming data -> start reading */
         dprintf(DBG_CLNT, "handle_client_sockets: fd %d: receiving data\n", req->io.sock_fd);

         if (vbi_proxy_msg_handle_read(&req->io, &io_blocked, TRUE, &req->msg_buf, sizeof(req->msg_buf)))
         {
            /* check for finished read -> process request */
            if ( (req->io.readOff != 0) && (req->io.readOff == req->io.readLen) )
            {
               if (vbi_proxyd_check_msg(&req->msg_buf, &req->endianSwap))
               {
                  vbi_proxy_msg_close_read(&req->io);

                  if (vbi_proxyd_take_message(req, &req->msg_buf) == FALSE)
                  {  /* message no accepted (e.g. wrong state) */
                     vbi_proxyd_close(req, FALSE);
                  }
               }
               else
               {  /* message has illegal size or content */
                  vbi_proxyd_close(req, FALSE);
               }
            }
         }
         else
            vbi_proxyd_close(req, FALSE);
      }
      else if ( FD_ISSET(req->io.sock_fd, wr) &&
                !vbi_proxy_msg_write_idle(&req->io) )
      {
         if (vbi_proxy_msg_handle_write(&req->io, &io_blocked) == FALSE)
         {
            vbi_proxyd_close(req, FALSE);
         }
      }

      if (req->state == REQ_STATE_WAIT_CLOSE)
      {  /* close was pending after last write */
         vbi_proxyd_close(req, FALSE);
      }
      else if (vbi_proxy_msg_is_idle(&req->io))
      {  /* currently no I/O in progress */

         if (req->chn_state.token_state == REQ_TOKEN_RECLAIM)
         {
            dprintf(DBG_MSG, "channel token reclaim: fd %d\n", req->io.sock_fd);
            /* XXX TODO: supervise return of token by timer */
            memset(&req->msg_buf, 0, sizeof(req->msg_buf));
            vbi_proxy_msg_write(&req->io, MSG_TYPE_CHN_RECLAIM_REQ,
                                sizeof(req->msg_buf.body.chn_reclaim_req), &req->msg_buf, FALSE);
            req->chn_state.token_state = REQ_TOKEN_RELEASE;
         }
         else if (req->chn_state.token_state == REQ_TOKEN_GRANT)
         {
            dprintf(DBG_MSG, "channel token grant: fd %d\n", req->io.sock_fd);
            memset(&req->msg_buf, 0, sizeof(req->msg_buf));
            vbi_proxy_msg_write(&req->io, MSG_TYPE_CHN_TOKEN_IND,
                                sizeof(req->msg_buf.body.chn_token_ind), &req->msg_buf, FALSE);
            req->chn_state.token_state = REQ_TOKEN_GRANTED;
         }
         else if (req->chn_status_ind)
         {  /* send channel change indication */
            memset(&req->msg_buf, 0, sizeof(req->msg_buf));
            req->msg_buf.body.chn_change_ind.notify_flags = req->chn_status_ind;
            req->msg_buf.body.chn_change_ind.scanning = proxy.dev[req->dev_idx].scanning;

            vbi_proxy_msg_write(&req->io, MSG_TYPE_CHN_CHANGE_IND,
                                sizeof(req->msg_buf.body.chn_change_ind), &req->msg_buf, FALSE);
            req->chn_status_ind = VBI_PROXY_CHN_NONE;
         }
         else
         {
            /* forward data from slicer out queue */
            while ((req->p_sliced != NULL) && (io_blocked == FALSE))
            {
               dprintf(DBG_QU, "handle_sockets: fd %d: forward sliced frame with %d lines (of max %d)\n", req->io.sock_fd, req->p_sliced->line_count, req->p_sliced->max_lines);
               if (vbi_proxyd_send_sliced(req, &io_blocked) )
               {  /* only in success case because close releases all buffers */
                  pthread_mutex_lock(&proxy.dev[req->dev_idx].queue_mutex);
                  vbi_proxy_queue_release_sliced(req);
                  pthread_mutex_unlock(&proxy.dev[req->dev_idx].queue_mutex);
               }
               else
               {  /* I/O error */
                  vbi_proxyd_close(req, FALSE);
                  io_blocked = TRUE;
               }
            }
         }
      }

      if (req->io.sock_fd == -1)
      {  /* free resources (should be redundant, but does no harm) */
         vbi_proxyd_close(req, FALSE);
      }
      else if ( (req->state == REQ_STATE_WAIT_CON_REQ) &&
                ((req->client_flags & VBI_PROXY_CLIENT_NO_TIMEOUTS) == 0) &&
                vbi_proxy_msg_check_timeout(&req->io, now) )
      {
         dprintf(DBG_MSG, "handle_sockets: fd %d: i/o timeout in state %d (writeLen=%d, readLen=%d, readOff=%d, read msg type=%d: %s)\n", req->io.sock_fd, req->state, req->io.writeLen, req->io.readLen, req->io.readOff, req->msg_buf.head.type, vbi_proxy_msg_debug_get_type_str(req->msg_buf.head.type));
         vbi_proxyd_close(req, FALSE);
      }
      else /* check for protocol or network I/O timeout */
      if ( (req->state == REQ_STATE_WAIT_CON_REQ) &&
           (now > req->io.lastIoTime + SRV_CONNECT_TIMEOUT) )
      {
         dprintf(DBG_MSG, "handle_sockets: fd %d: protocol timeout in state %d\n", req->io.sock_fd, req->state);
         vbi_proxyd_close(req, FALSE);
      }

      if (req->state == REQ_STATE_CLOSED)
      {  /* connection was closed after network error */
         unsigned int clnt_services = req->all_services;
         int dev_idx = req->dev_idx;
         if (proxy.clnt_count > 0)
            proxy.clnt_count -= 1;
         dprintf(DBG_MSG, "handle_sockets: closed conn, %d remain\n", proxy.clnt_count);

         pthread_mutex_lock(&proxy.clnt_mutex);
         /* unlink from list */
         tmp = req;
         if (prev == NULL)
         {
            proxy.p_clnts = req->p_next;
            req = proxy.p_clnts;
         }
         else
         {
            prev->p_next = req->p_next;
            req = req->p_next;
         }
         pthread_mutex_unlock(&proxy.clnt_mutex);

         if (clnt_services != 0)
            vbi_proxyd_update_services(dev_idx, NULL, 0, NULL);
         if (proxy.dev[dev_idx].p_capture != NULL)
            vbi_proxyd_channel_update(dev_idx, NULL, FALSE);
         free(tmp);
      }
      else
      {
         prev = req;
         req = req->p_next;
      }
   }
}

/* ----------------------------------------------------------------------------
** Set maximum number of open client connections
** - note: does not close connections if max count is already exceeded
*/
static void vbi_proxyd_set_max_conn( unsigned int max_conn )
{
   proxy.max_conn = max_conn;
}

/* ----------------------------------------------------------------------------
** Set server IP address
** - must be called before the listening sockets are created
*/
static void vbi_proxyd_set_address( vbi_bool do_tcp_ip, const char * pIpStr, const char * pPort )
{
   /* free the memory allocated for the old config strings */
   if (proxy.listen_ip != NULL)
   {
      free(proxy.listen_ip);
      proxy.listen_ip = NULL;
   }
   if (proxy.listen_port != NULL)
   {
      free(proxy.listen_port);
      proxy.listen_port = NULL;
   }

   /* make a copy of the new config strings */
   if (pIpStr != NULL)
   {
      proxy.listen_ip = malloc(strlen(pIpStr) + 1);
      strcpy(proxy.listen_ip, pIpStr);
   }
   if (pPort != NULL)
   {
      proxy.listen_port = malloc(strlen(pPort) + 1);
      strcpy(proxy.listen_port, pPort);
   }
   proxy.do_tcp_ip = do_tcp_ip;
}

/* ----------------------------------------------------------------------------
** Emulate device permissions on the socket file
*/
static void vbi_proxyd_set_socket_perm( PROXY_DEV * p_proxy_dev )
{
   struct stat st;

   if (stat(p_proxy_dev->p_dev_name, &st) != -1)
   {
      if ( (chown(p_proxy_dev->p_sock_path, st.st_uid, st.st_gid) != 0) &&
           (chown(p_proxy_dev->p_sock_path, geteuid(), st.st_gid) != 0) )
         dprintf(DBG_MSG, "set_perm: failed to set socket owner %d.%d: %s\n", st.st_uid, st.st_gid, strerror(errno));

      if (chmod(p_proxy_dev->p_sock_path, st.st_mode) != 0)
         dprintf(DBG_MSG, "set_perm: failed to set socket permission %o: %s\n", st.st_mode, strerror(errno));
   }
   else
      dprintf(DBG_MSG, "set_perm: failed to stat VBI device %s\n", p_proxy_dev->p_dev_name);
}

/* ----------------------------------------------------------------------------
** Stop the server, close all connections, free resources
*/
static void vbi_proxyd_destroy( void )
{
   PROXY_CLNT  *req, *p_next;
   int  dev_idx;

   /* close all devices */
   for (dev_idx = 0; dev_idx < proxy.dev_count; dev_idx++)
   {
      vbi_proxy_stop_acquisition(proxy.dev + dev_idx);
   }

   /* shutdown all client connections & free resources */
   req = proxy.p_clnts;
   while (req != NULL)
   {
      p_next = req->p_next;
      vbi_proxyd_close(req, TRUE);
      free(req);
      req = p_next;
   }
   proxy.p_clnts = NULL;
   proxy.clnt_count = 0;

   /* close listening sockets */
   for (dev_idx = 0; dev_idx < proxy.dev_count; dev_idx++)
   {
      if (proxy.dev[dev_idx].pipe_fd != -1)
      {
         vbi_proxy_msg_stop_listen(FALSE, proxy.dev[dev_idx].pipe_fd, proxy.dev[dev_idx].p_sock_path);
      }

      if (proxy.dev[dev_idx].p_sock_path != NULL)
         free(proxy.dev[dev_idx].p_sock_path);

      pthread_cond_destroy(&proxy.dev[dev_idx].start_cond);
      pthread_mutex_destroy(&proxy.dev[dev_idx].start_mutex);
      pthread_mutex_destroy(&proxy.dev[dev_idx].queue_mutex);
   }

   if (proxy.tcp_ip_fd != -1)
   {
      vbi_proxy_msg_stop_listen(TRUE, proxy.tcp_ip_fd, NULL);
   }

   vbi_proxy_msg_logger(LOG_NOTICE, -1, 0, "shutting down", NULL);

   /* free the memory allocated for the config strings */
   vbi_proxyd_set_address(FALSE, NULL, NULL);
   vbi_proxy_msg_set_logging(FALSE, 0, 0, NULL);
}

/* ---------------------------------------------------------------------------
** Signal handler to process alarm
*/
static void vbi_proxyd_alarm_handler( int sigval )
{
   sigval = sigval;

   proxy.chn_sched_alarm = TRUE;
}

/* ---------------------------------------------------------------------------
** Signal handler to catch deadly signals
*/
static void vbi_proxyd_signal_handler( int sigval )
{
   char str_buf[10];

   snprintf(str_buf, sizeof (str_buf), "%d", sigval);
   vbi_proxy_msg_logger(LOG_NOTICE, -1, 0, "terminated by signal", str_buf, NULL);

   proxy.should_exit = TRUE;
}

/* ----------------------------------------------------------------------------
** Initialize DB server
*/
static void vbi_proxyd_init( void )
{
   struct sigaction  act;

   if (opt_no_detach == FALSE)
   {
      if (fork() > 0)
         exit(0);
      close(0); 
      open("/dev/null", O_RDONLY, 0);

      if (opt_debug_level == 0)
      {
         close(1);
         open("/dev/null", O_WRONLY, 0);
         close(2);
         dup(1);

         setsid();
      }
   }

   /* ignore broken pipes (handled by select/read) */
   memset(&act, 0, sizeof(act));
   act.sa_handler = SIG_IGN;
   sigaction(SIGPIPE, &act, NULL);

   /* handle alarm timers (for channel change scheduling) */
   memset(&act, 0, sizeof(act));
   act.sa_handler = vbi_proxyd_alarm_handler;
   sigaction(SIGALRM, &act, NULL);

   /* catch deadly signals for a clean shutdown (remove socket file) */
   memset(&act, 0, sizeof(act));
   sigemptyset(&act.sa_mask);
   sigaddset(&act.sa_mask, SIGINT);
   sigaddset(&act.sa_mask, SIGTERM);
   sigaddset(&act.sa_mask, SIGHUP);
   act.sa_handler = vbi_proxyd_signal_handler;
   act.sa_flags = SA_ONESHOT;
   sigaction(SIGINT, &act, NULL);
   sigaction(SIGTERM, &act, NULL);
   sigaction(SIGHUP, &act, NULL);
}

/* ----------------------------------------------------------------------------
** Set up sockets for listening to client requests
*/
static vbi_bool vbi_proxyd_listen( void )
{
   PROXY_DEV  * p_proxy_dev;
   int          dev_idx;
   vbi_bool     result;

   result      = TRUE;
   p_proxy_dev = proxy.dev;

   for (dev_idx = 0; (dev_idx < proxy.dev_count) && result; dev_idx++, p_proxy_dev++)
   {
      if (vbi_proxy_msg_check_connect(p_proxy_dev->p_sock_path) == FALSE)
      {
         /* create named socket in /tmp for listening to local clients */
         p_proxy_dev->pipe_fd = vbi_proxy_msg_listen_socket(FALSE, NULL, p_proxy_dev->p_sock_path);
         if (p_proxy_dev->pipe_fd != -1)
         {
            /* copy VBI device permissions to the listening socket */
            vbi_proxyd_set_socket_perm(p_proxy_dev);

            vbi_proxy_msg_logger(LOG_NOTICE, -1, 0, "started listening on local socket for ", p_proxy_dev->p_dev_name, NULL);
         }
         else
            result = FALSE;
      }
      else
      {
         vbi_proxy_msg_logger(LOG_ERR, -1, 0, "a proxy daemon is already running for ", p_proxy_dev->p_dev_name, NULL);
         result = FALSE;
      }
   }

   if (proxy.do_tcp_ip && result)
   {
      /* create TCP/IP socket */
      proxy.tcp_ip_fd = vbi_proxy_msg_listen_socket(TRUE, proxy.listen_ip, proxy.listen_port);
      if (proxy.tcp_ip_fd != -1)
      {
         vbi_proxy_msg_logger(LOG_NOTICE, -1, 0, "started listening on TCP/IP socket", NULL);
      }
      else
         result = FALSE;
   }

   return result;
}

/* ---------------------------------------------------------------------------
** Proxy daemon main loop
*/
static void vbi_proxyd_main_loop( void )
{
   fd_set  rd, wr;
   int     max_fd;
   int     sel_cnt;
   int     dev_idx;

   while (proxy.should_exit == FALSE)
   {
      FD_ZERO(&rd);
      FD_ZERO(&wr);
      max_fd = vbi_proxyd_get_fd_set(&rd, &wr);

      /* wait for new clients, client messages or VBI device data (indefinitly) */
      sel_cnt = select(((max_fd > 0) ? (max_fd + 1) : 0), &rd, &wr, NULL, NULL);

      if (sel_cnt != -1)
      {
         if (sel_cnt > 0)
            dprintf(DBG_CLNT, "main_loop: select: events on %d sockets\n", sel_cnt);

         for (dev_idx = 0; dev_idx < proxy.dev_count; dev_idx++)
         {
            /* accept new client connections on device socket */
            if ((proxy.dev[dev_idx].pipe_fd != -1) && (FD_ISSET(proxy.dev[dev_idx].pipe_fd, &rd)))
            {
               vbi_proxyd_add_connection(proxy.dev[dev_idx].pipe_fd, dev_idx, TRUE);
            }

            /* check for incoming data on VBI device */
            if ((proxy.dev[dev_idx].vbi_fd != -1) && (FD_ISSET(proxy.dev[dev_idx].vbi_fd, &rd)))
            {
               if (proxy.dev[dev_idx].use_thread == FALSE)
               {
                  vbi_proxyd_forward_data(dev_idx);
               }
               else
               {  /* message from acq thread slave:
                  ** sent data is only a trigger to wake up from select() above -> discard it */
                  char dummy_buf[100];
                  int  rd_count;
                  do {
                     rd_count = read(proxy.dev[dev_idx].vbi_fd, dummy_buf, sizeof(dummy_buf));
                     dprintf(DBG_QU, "main_loop: read from acq thread dev #%d pipe fd %d: %d errno=%d\n", dev_idx, proxy.dev[dev_idx].vbi_fd, sel_cnt, errno);
                  } while (rd_count == 100);
               }
            }
         }

         /* accept new TCP/IP connections */
         if ((proxy.tcp_ip_fd != -1) && (FD_ISSET(proxy.tcp_ip_fd, &rd)))
         {
            vbi_proxyd_add_connection(proxy.tcp_ip_fd, 0, FALSE);
         }

         /* send queued data or process incoming messages from clients */
         vbi_proxyd_handle_client_sockets(&rd, &wr);

         if (proxy.chn_sched_alarm)
         {
            proxy.chn_sched_alarm = FALSE;

            vbi_proxyd_channel_timer();
         }
      }
      else
      {
         if (errno != EINTR)
         {  /* select syscall failed */
            dprintf(DBG_MSG, "main_loop: select with max. fd %d: %s\n", max_fd, strerror(errno));
            sleep(1);
         }
      }
   }
}

/* ---------------------------------------------------------------------------
** Kill-daemon only: exit upon timeout in I/O to daemon
*/
static void vbi_proxyd_kill_timeout( int sigval )
{
   sigval = sigval;

   /* note: cannot use printf in signal handler without risking deadlock */
   exit(2);
}

/* ---------------------------------------------------------------------------
** Connect to running daemon, query its pid and kill it, exit.
*/
static void vbi_proxyd_kill_daemon( void )
{
   struct sigaction  act;
   char * p_errorstr;
   char * p_srv_port;
   vbi_bool     io_blocked;
   VBIPROXY_MSG msg_buf;
   VBIPROXY_MSG_STATE io;

   memset(&io, 0, sizeof(io));
   io.sock_fd  = -1;
   p_errorstr = NULL;
   p_srv_port = vbi_proxy_msg_get_socket_name(proxy.dev[0].p_dev_name);
   if (p_srv_port == NULL)
      goto failure;

   io.sock_fd = vbi_proxy_msg_connect_to_server(FALSE, NULL, p_srv_port, &p_errorstr);
   if (io.sock_fd == -1)
      goto failure;

   memset(&act, 0, sizeof(act));
   act.sa_handler = vbi_proxyd_kill_timeout;
   sigaction(SIGALRM, &act, NULL);

   /* use non-blocking I/O and alarm timer for timeout handling (simpler than select) */
   alarm(4);
   fcntl(io.sock_fd, F_SETFL, 0);

   /* wait for socket to reach connected state */
   if (vbi_proxy_msg_finish_connect(io.sock_fd, &p_errorstr) == FALSE)
      goto failure;

   /* write service request parameters */
   vbi_proxy_msg_fill_magics(&msg_buf.body.daemon_pid_req.magics);

   /* send the pid request message to the proxy server */
   vbi_proxy_msg_write(&io, MSG_TYPE_DAEMON_PID_REQ, sizeof(msg_buf.body.daemon_pid_req),
                       &msg_buf, FALSE);

   if (vbi_proxy_msg_handle_write(&io, &io_blocked) == FALSE)
      goto io_error;

   if (vbi_proxy_msg_handle_read(&io, &io_blocked, TRUE, &msg_buf, sizeof(msg_buf)) == FALSE)
      goto io_error;

   if ( (vbi_proxyd_check_msg(&msg_buf, FALSE) == FALSE) ||
        (msg_buf.head.type != MSG_TYPE_DAEMON_PID_CNF) )
   {
      asprintf(&p_errorstr, "%s", "Proxy protocol error");
      goto failure;
   }

   if (kill(msg_buf.body.daemon_pid_cnf.pid, SIGTERM) != 0)
   {
      asprintf(&p_errorstr, "Failed to kill the daemon process (pid %d): %s",
	       msg_buf.body.daemon_pid_cnf.pid, strerror(errno));
      goto failure;
   }

   dprintf(DBG_MSG, "Killed daemon process %d.\n", msg_buf.body.daemon_pid_cnf.pid);
   close(io.sock_fd);
   exit(0);

io_error:
   if (p_errorstr == NULL)
      asprintf(&p_errorstr, "Lost connection to proxy (I/O error)");

failure:
   /* failed to establish a connection to the server */
   if (io.sock_fd != -1)
      close(io.sock_fd);
   if (p_errorstr != NULL)
   {
      fprintf(stderr, "%s\n", p_errorstr);
      free(p_errorstr);
   }
   exit(1);
}

/* ---------------------------------------------------------------------------
** Print usage and exit
*/
static void proxy_usage_exit( const char *argv0, const char *argvn, const char * reason )
{
   fprintf(stderr, "%s: %s: %s\n"
                   "Options:\n"
                   "       -dev <path>         : VBI device path (allowed repeatedly)\n"
                   "       -buffers <count>    : number of raw capture buffers (v4l2 only)\n"
                   "       -nodetach           : process remains connected to tty\n"
                   "       -kill               : kill running daemon process, then exit\n"
                   "       -debug <level>      : enable debug output: 1=warnings, 2=all\n"
                   "       -syslog <level>     : enable syslog output\n"
                   "       -loglevel <level>   : log file level\n"
                   "       -logfile <path>     : log file name\n"
                   "       -maxclients <count> : max. number of clients\n"
                   "       -help               : this message\n",
                   argv0, reason, argvn);

   exit(1);
}

/* ---------------------------------------------------------------------------
** Parse numeric value in command line options
*/
static vbi_bool proxy_parse_argv_numeric( char * p_number, int * p_value )
{
   char * p_num_end;

   if (*p_number != 0)
   {
      *p_value = strtol(p_number, &p_num_end, 0);

      return (*p_num_end == 0);
   }
   else
      return FALSE;
}

/* ---------------------------------------------------------------------------
** Parse command line options
*/
static void vbi_proxyd_parse_argv( int argc, char * argv[] )
{
   struct stat stb;
   int arg_val;
   int arg_idx = 1;

   while (arg_idx < argc)
   {
      if (strcasecmp(argv[arg_idx], "-dev") == 0)
      {
         if (arg_idx + 1 < argc)
         {
            if (proxy.dev_count >= SRV_MAX_DEVICES)
               proxy_usage_exit(argv[0], argv[arg_idx], "too many device paths");
            if (stat(argv[arg_idx + 1], &stb) == -1)
               proxy_usage_exit(argv[0], argv[arg_idx +1], strerror(errno));
            if (!S_ISCHR(stb.st_mode))
               proxy_usage_exit(argv[0], argv[arg_idx +1], "not a character device");
            if (access(argv[arg_idx + 1], R_OK | W_OK) == -1)
               proxy_usage_exit(argv[0], argv[arg_idx +1], "failed to access device");

            vbi_proxyd_add_device(argv[arg_idx + 1]);
            arg_idx += 2;
         }
         else
            proxy_usage_exit(argv[0], argv[arg_idx], "missing mode keyword after");
      }
      else if (strcasecmp(argv[arg_idx], "-buffers") == 0)
      {
         if ((arg_idx + 1 < argc) && proxy_parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_buffer_count = arg_val;
            if ((opt_buffer_count < 1) || (opt_buffer_count > VBI_MAX_BUFFER_COUNT))
               proxy_usage_exit(argv[0], argv[arg_idx], "buffer count unsupported");
            arg_idx += 2;
         }
         else
            proxy_usage_exit(argv[0], argv[arg_idx], "missing buffer count after");
      }
      else if (strcasecmp(argv[arg_idx], "-debug") == 0)
      {
         if ((arg_idx + 1 < argc) && proxy_parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_debug_level = arg_val;
            if (opt_debug_level > 0)
               opt_debug_level |= DBG_MSG;
            arg_idx += 2;
         }
         else
            proxy_usage_exit(argv[0], argv[arg_idx], "missing debug level after");
      }
      else if (strcasecmp(argv[arg_idx], "-nodetach") == 0)
      {
         opt_no_detach = TRUE;
         arg_idx += 1;
      }
      else if (strcasecmp(argv[arg_idx], "-kill") == 0)
      {
         opt_kill_daemon = TRUE;
         arg_idx += 1;
      }
      else if (strcasecmp(argv[arg_idx], "-syslog") == 0)
      {
         if ((arg_idx + 1 < argc) && proxy_parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_syslog_level = arg_val;
            arg_idx += 2;
         }
         else
            proxy_usage_exit(argv[0], argv[arg_idx], "missing log level after");
      }
      else if (strcasecmp(argv[arg_idx], "-loglevel") == 0)
      {
         if ((arg_idx + 1 < argc) && proxy_parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_log_level = arg_val;
            arg_idx += 2;
         }
         else
            proxy_usage_exit(argv[0], argv[arg_idx], "missing log level after");
      }
      else if (strcasecmp(argv[arg_idx], "-logfile") == 0)
      {
         if (arg_idx + 1 < argc)
         {
            p_opt_log_name = argv[arg_idx + 1];
            arg_idx += 2;
         }
         else
            proxy_usage_exit(argv[0], argv[arg_idx], "missing mode keyword after");
      }
      else if (strcasecmp(argv[arg_idx], "-maxclients") == 0)
      {
         if ((arg_idx + 1 < argc) && proxy_parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_max_clients = arg_val;
            arg_idx += 2;
         }
         else
            proxy_usage_exit(argv[0], argv[arg_idx], "missing log level after");
      }
      else if (strcasecmp(argv[arg_idx], "-help") == 0)
      {
         char versbuf[50];
         snprintf(versbuf, sizeof (versbuf), "(version %d.%d.%d)", VBIPROXY_VERSION>>16, (VBIPROXY_VERSION>>8)&0xff, VBIPROXY_VERSION&0xff);
         proxy_usage_exit(argv[0], versbuf, "the following options are available");
      }
      else
         proxy_usage_exit(argv[0], argv[arg_idx], "unknown option or argument");
   }

   /* if no device was given, use default path */
   if (proxy.dev_count == 0)
   {
      /* use devfs path if subdirectory exists */
      if (access(DEFAULT_VBI_DEVFS_PATH, R_OK | W_OK) == 0)
         vbi_proxyd_add_device(DEFAULT_VBI_DEVFS_PATH);
      else
         vbi_proxyd_add_device(DEFAULT_VBI_DEV_PATH);
   }
}

/* ----------------------------------------------------------------------------
** Proxy daemon entry point
*/
int main( int argc, char ** argv )
{
   /* initialize state struct */
   memset(&proxy, 0, sizeof(proxy));
   proxy.tcp_ip_fd = -1;
   pthread_mutex_init(&proxy.clnt_mutex, NULL);

   vbi_proxyd_parse_argv(argc, argv);
   vbi_proxy_msg_set_debug_level( (opt_debug_level == 0) ? 0 : ((opt_debug_level & DBG_CLNT) ? 2 : 1) );

   if (opt_kill_daemon)
   {
      vbi_proxyd_kill_daemon();
      exit(0);
   }

   dprintf(DBG_MSG, "proxy daemon starting, rev.\n%s\n", rcsid);

   vbi_proxyd_init();

   vbi_proxyd_set_max_conn(opt_max_clients);
   vbi_proxyd_set_address(FALSE, NULL, NULL);
   vbi_proxy_msg_set_logging(opt_debug_level > 0, opt_syslog_level, opt_log_level, p_opt_log_name);

   /* start listening for client connections */
   if (vbi_proxyd_listen())
   {
      vbi_proxyd_main_loop();
   }
   vbi_proxyd_destroy();
   pthread_mutex_destroy(&proxy.clnt_mutex);

   exit(0);
   return 0;
}

#endif  /* ENABLE_PROXY */
