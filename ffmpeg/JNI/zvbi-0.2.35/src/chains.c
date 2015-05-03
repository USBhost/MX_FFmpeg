/*
 * Evil evil evil hack to get VBI apps to cooperate with the vbi proxy.
 * This is based on artsdsp, which again is based on the original esddsp,
 * which esd uses to do the same for audio.
 *
 * Copyright (C) 1998 Manish Singh <yosh@gimp.org>
 * Copyright (C) 2000 Stefan Westerfeld <stefan@space.twc.de> (artsd port)
 * Copyright (C) 2004 Tom Zoerner (VBI port)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if defined(ENABLE_PROXY) && defined(ENABLE_V4L)

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>

#include "io.h"
#include "proxy-msg.h"
#include "proxy-client.h"

#if !defined (__NetBSD__) && !defined (__FreeBSD__) && !defined (__FreeBSD_kernel__)
#include <asm/types.h> /* __u8 and friends */
#ifndef HAVE_S64_U64
#  include <inttypes.h>
  /* Linux 2.6.x asm/types.h defines __s64 and __u64 only
     if __GNUC__ is defined. */
typedef int64_t __s64;
typedef uint64_t __u64;
#endif
#include "videodev2k.h"
#include "videodev.h"
# define BASE_VIDIOCPRIVATE      192
# define BTTV_VERSION            _IOR('v' , BASE_VIDIOCPRIVATE+6, int)
# define BTTV_VBISIZE            _IOR('v' , BASE_VIDIOCPRIVATE+8, int)
#endif


#if defined(HAVE_IOCTL_INT_INT_DOTS)
#define ioctl_request_t int
#elif defined(HAVE_IOCTL_INT_ULONG_DOTS)
#define ioctl_request_t unsigned long
#elif defined(HAVE_IOCTL_INT_ULONGINT_DOTS)
#define ioctl_request_t unsigned long int
#else
#error "unknown ioctl type (check config.h, adapt configure test)..."
#endif
#define fcntl_request_t int

/*
 * original C library functions
 */
typedef int (*orig_open_ptr) (const char *pathname, int flags, ...);
typedef int (*orig_close_ptr) (int fd);
typedef int (*orig_ioctl_ptr) (int fd, ioctl_request_t request, ...);
typedef int (*orig_fcntl_ptr) (int fd, fcntl_request_t request, ...);
typedef ssize_t(*orig_write_ptr) (int fd, const void *buf, size_t count);
typedef ssize_t(*orig_read_ptr) (int fd, void *buf, size_t count);
typedef int (*orig_munmap_ptr) (void *start, size_t length);

static orig_open_ptr orig_open;
static orig_close_ptr orig_close;
static orig_ioctl_ptr orig_ioctl;
static orig_fcntl_ptr orig_fcntl;
static orig_write_ptr orig_write;
static orig_read_ptr orig_read;

static int vbi_chains_init = 0;
static int vbi_chains_debug;
static int vbi_chains_working;
static char *vbi_chains_device;

static vbi_proxy_client *p_proxy_client;
static int vbi_buf_size;
static unsigned int vbi_seq_no;
static int vbi_fd;
static int vbi_fd_nonblocking;

#define CHECK_INIT() if(!vbi_chains_init) vbi_chains_doinit();

#define dprintf1(fmt, arg...)    do {if (vbi_chains_debug >= 1) fprintf(stderr, "proxy-chains: " fmt, ## arg);} while(0)
#define dprintf2(fmt, arg...)    do {if (vbi_chains_debug >= 2) fprintf(stderr, "proxy-chains: " fmt, ## arg);} while(0)

/*
 * Initialization - maybe this should be either be a startup only called
 * routine, or use pthread locks to prevent strange effects in multithreaded
 * use (however it seems highly unlikely that an application would create
 * multiple threads before even using one of redirected the system functions
 * once).
 */

static void vbi_chains_doinit(void)
{
   const char *env;
   char *end;

   vbi_chains_init = 1;

   /* retrieve path to VBI device (ignore if NULL) */
   vbi_chains_device = getenv("VBIPROXY_DEVICE");
   if (vbi_chains_device == NULL)
   {
      fprintf(stderr, "VBIPROXY_DEVICE environment variable not set - disabling proxy\n");
   }
   else
   {
      if (strlen(vbi_chains_device) > 0)
         dprintf1("Will redirect access to device %s\n", vbi_chains_device);
      else
         dprintf1("No device specified: will redirect access to any VBI device\n");
   }

   /* set debug flag */
   env = getenv("VBIPROXY_DEBUG");
   if (env != NULL)
   {
      vbi_chains_debug = strtol(env, &end, 0);
      if ((*env == 0) || (*end != 0))
      {
         fprintf(stderr, "VBIPROXY_DEBUG='%s': not a number - setting debug level 1\n", env);
         vbi_chains_debug = 1;
      }
   }

   /* resolve original symbols */
   orig_open = (orig_open_ptr) dlsym(RTLD_NEXT, "open");
   orig_close = (orig_close_ptr) dlsym(RTLD_NEXT, "close");
   orig_write = (orig_write_ptr) dlsym(RTLD_NEXT, "write");
   orig_read = (orig_read_ptr) dlsym(RTLD_NEXT, "read");
   orig_ioctl = (orig_ioctl_ptr) dlsym(RTLD_NEXT, "ioctl");
   orig_fcntl = (orig_fcntl_ptr) dlsym(RTLD_NEXT, "fcntl");

   /* initialize module variables */
   p_proxy_client = NULL;
   vbi_fd = -1;
   vbi_chains_working = 0;
}

/* returns 1 if the filename points to a VBI device */
static int is_vbi_device(const char *pathname)
{
   if ((pathname != NULL) && (vbi_chains_device != NULL))
   {
      if (*vbi_chains_device != 0)
         return (strcmp(pathname, vbi_chains_device) == 0);
      else
         return (strncmp(pathname, "/dev/vbi", 8) == 0) || (strncmp(pathname, "/dev/v4l/vbi", 12) == 0);
   }
   else
      return 0;
}

int open(const char *pathname, int flags, ...)
{
   va_list args;
   mode_t mode = 0;

   CHECK_INIT();

   /*
    * After the documentation, va_arg is not safe if there is no argument to
    * get "random errors will occur", so only get it in case O_CREAT is set,
    * and hope that passing 0 to the orig_open function in all other cases
    * will work.
    */
   va_start(args, flags);
   if (flags & O_CREAT)
   {
      /* The compiler will select one of these at compile-tyime if -O is used.
       * Otherwise, it may be deferred until runtime.
       */
      if (sizeof(int) >= sizeof(mode_t))
      {
         mode = va_arg(args, int);
      }
      else
      {
         mode = va_arg(args, mode_t);
      }
   }
   va_end(args);

   if (!is_vbi_device(pathname) || vbi_chains_working)
   {
      return orig_open(pathname, flags, mode);
   }
   else
   {
      unsigned int services = VBI_SLICED_VBI_625 | VBI_SLICED_VBI_525;
      vbi_raw_decoder *p_dec;
      vbi_capture *p_capt;
      const char *p_client;
      char *p_errmsg = NULL;

      dprintf1("hijacking open on %s...\n", pathname);

      if (p_proxy_client == NULL)
      {
         p_client = getenv("VBIPROXY_CLIENT");
         if (p_client == NULL)
            p_client = "vbi-chain";

         vbi_chains_working = 1;
         p_proxy_client = vbi_proxy_client_create(pathname, p_client,
                                                  VBI_PROXY_CLIENT_NO_STATUS_IND,
                                                  &p_errmsg, vbi_chains_debug);
         if (p_proxy_client != NULL)
         {
            p_capt = vbi_capture_proxy_new(p_proxy_client, 5, 0, &services, 0, &p_errmsg);
            if (p_capt != NULL)
            {
               vbi_fd = vbi_capture_fd(p_capt);

               p_dec = vbi_capture_parameters(p_capt);
               if (p_dec != NULL)
                  vbi_buf_size = (p_dec->count[0] + p_dec->count[1]) * 2048;
               else
                  vbi_buf_size = 0;     /* ERROR */

               vbi_seq_no = 0;
               vbi_fd_nonblocking = 0;
            }
            else
            {
               int save_errno = errno;

               vbi_proxy_client_destroy(p_proxy_client);
               p_proxy_client = NULL;

               errno = save_errno;
            }
         }
         vbi_chains_working = 0;

         if (p_errmsg != NULL)
         {
            dprintf1("Failed to connect to proxy: %s\n", p_errmsg);
            free(p_errmsg);
         }

         if ((vbi_fd != -1) || (errno != 2))
         {
            dprintf2("open returns %d errno=%d (%s)\n", vbi_fd, errno, strerror(errno));
            return vbi_fd;
         }
         else
         {
            dprintf1("proxy not running - trying the actual device...\n");

            return orig_open(pathname, flags, mode);
         }
      }
      else
      {
         errno = EBUSY;
         return -1;
      }
   }
}

int ioctl(int fd, ioctl_request_t request, ...)
{
   /*
    * FreeBSD needs ioctl with varargs. However I have no idea how to "forward"
    * the variable args ioctl to the orig_ioctl routine. So I expect the ioctl
    * to have exactly one pointer-like parameter and forward that, hoping that
    * it works
    */
   va_list args;
   void *argp;
   va_start(args, request);
   argp = va_arg(args, void *);
   va_end(args);

   CHECK_INIT();

   if ((fd != vbi_fd) || vbi_chains_working)
   {
      return orig_ioctl(fd, request, argp);
   }
   else if (vbi_fd == -1)
   {
      errno = EBADF;
      return -1;
   }
   else
   {
      /*int *arg = (int *) argp; */
      dprintf1("hijacking ioctl (%d : %x - %p)\n",
               (int) fd, (int) request, (void *) ((argp != NULL) ? argp : "(NULL)"));

      switch (request)
      {
         /* ----------------------------- V4L2 -------------------------------- */

         case VIDIOC_QUERYCAP:
         {
            struct v4l2_capability *p_cap = argp;
            memset(p_cap, 0, sizeof(p_cap[0]));
            strcpy((char *) p_cap->driver, "VBI Proxy");
            strcpy((char *) p_cap->card, "unknown");
            strcpy((char *) p_cap->bus_info, "");
            p_cap->version = VBIPROXY_VERSION;
            p_cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VBI_CAPTURE;
            return 0;
         }

         case VIDIOC_G_FMT:
         {
            struct v4l2_format *p_fmt = argp;
            vbi_capture *p_capt;
            vbi_raw_decoder *p_dec;

            if (p_fmt->type == V4L2_BUF_TYPE_VBI_CAPTURE)
            {
               p_capt = vbi_proxy_client_get_capture_if(p_proxy_client);
               p_dec = vbi_capture_parameters(p_capt);
               memset(p_fmt, 0, sizeof(p_fmt[0]));
               p_fmt->type = V4L2_BUF_TYPE_VBI_CAPTURE;
               p_fmt->fmt.vbi.sampling_rate = p_dec->sampling_rate;
               p_fmt->fmt.vbi.samples_per_line = p_dec->bytes_per_line;
               p_fmt->fmt.vbi.offset = p_dec->offset;
               p_fmt->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
               p_fmt->fmt.vbi.start[0] = p_dec->start[0];
               p_fmt->fmt.vbi.count[0] = p_dec->count[0];
               p_fmt->fmt.vbi.start[1] = p_dec->start[1];
               p_fmt->fmt.vbi.count[1] = p_dec->count[1];
               p_fmt->fmt.vbi.flags = 0;
            }
            return 0;
         }
         case VIDIOC_S_FMT:
         case VIDIOC_TRY_FMT:
         {
            /* XXX TODO: zvbi API does not allow to change params for raw capture */
#if 0
            struct v4l2_format *p_fmt = argp;
            char *p_errmsg = NULL;
            unsigned int services = VBI_SLICED_VBI_625 | VBI_SLICED_VBI_525;
            int res;
            res = vbi_proxy_client_update_services(p_proxy_client, TRUE, TRUE, &services, 0, &p_errmsg);
            if (p_errmsg != NULL)
            {
               dprintf1("Failed to update services: %s\n", p_errmsg);
               free(p_errmsg);
            }
            return res;
#else
            errno = EINVAL;
            return -1;
#endif
         }

         case VIDIOC_G_PRIORITY:
         case VIDIOC_S_PRIORITY:
         {
            enum v4l2_priority * prio = (enum v4l2_priority *) ((long) argp);
            vbi_channel_profile chn_profile;

            memset(&chn_profile, 0, sizeof(chn_profile));
            chn_profile.is_valid = TRUE;
            chn_profile.min_duration = 1;
            chn_profile.exp_duration = 1;
            return vbi_proxy_client_channel_request(p_proxy_client, *prio, &chn_profile);
         }


         /* ----------------------------- V4L1 -------------------------------- */

         case VIDIOCSVBIFMT:
            errno = EINVAL;
            return -1;

         case VIDIOCGVBIFMT:
         {
            vbi_capture *p_capt;
            vbi_raw_decoder *p_dec;
            struct vbi_format fmt;

            p_capt = vbi_proxy_client_get_capture_if(p_proxy_client);
            p_dec = vbi_capture_parameters(p_capt);
            memset(&fmt, 0, sizeof(fmt));
            fmt.sampling_rate = p_dec->sampling_rate;
            fmt.samples_per_line = p_dec->bytes_per_line;
            fmt.sample_format = VIDEO_PALETTE_RAW;
            fmt.start[0] = p_dec->start[0];
            fmt.count[0] = p_dec->count[0];
            fmt.start[1] = p_dec->start[1];
            fmt.count[1] = p_dec->count[1];
            fmt.flags = 0;
            memcpy(argp, &fmt, sizeof(fmt));
            return 0;
         }

         case BTTV_VERSION:
            dprintf1("ioctl BTTV_VERSION\n");
            return ((7 << 16) | (100 << 8));

         case BTTV_VBISIZE:
         {
            vbi_capture *p_capt;
            vbi_raw_decoder *p_dec;
            int size;

            p_capt = vbi_proxy_client_get_capture_if(p_proxy_client);
            p_dec = vbi_capture_parameters(p_capt);
            size = (p_dec->count[0] + p_dec->count[1]) * p_dec->bytes_per_line;
            dprintf1("ioctl BTTV_VBISIZE: %d\n", size);
            return size;
         }

         default:
            /* pass the ioctl to daemon via RPC
            ** (note not all requests are allowed, but this is checked internally) */
            return vbi_proxy_client_device_ioctl(p_proxy_client, request, argp);
      }
   }
}

int fcntl(int fd, fcntl_request_t request, ...)
{
   va_list args;
   void *argp;
   va_start(args, request);
   argp = va_arg(args, void *);
   va_end(args);

   CHECK_INIT();

   if ((fd != vbi_fd) || vbi_chains_working)
   {
      return orig_fcntl(fd, request, argp);
   }
   else if (vbi_fd == -1)
   {
      errno = EBADF;
      return -1;
   }
   else
   {
      dprintf2("hijacking fcntl (%d : %x - %p)\n",
               (int) fd, (int) request, (void *) ((argp != NULL) ? argp : "(no 3rd arg)"));
      if (request == F_SETFL)
      {
         vbi_fd_nonblocking = (((long) argp & O_NONBLOCK) != 0);
         dprintf1("Setting NONBLOCK mode flag: %d\n", vbi_fd_nonblocking);
         return 0;
      }
      else if (request == F_GETFL)
      {
         return (orig_fcntl(fd, request, argp) & ~O_NONBLOCK) | (vbi_fd_nonblocking ? O_NONBLOCK : 0);
      }
      else
         return orig_fcntl(fd, request, argp);
   }
}


int close(int fd)
{
   CHECK_INIT();

   if ((fd != vbi_fd) || vbi_chains_working)
   {
      return orig_close(fd);
   }
   else if (vbi_fd == -1)
   {
      errno = EBADF;
      return -1;
   }
   else
   {
      dprintf1("close...\n");
      vbi_chains_working = 1;

      vbi_proxy_client_destroy(p_proxy_client);
      p_proxy_client = NULL;
      vbi_fd = -1;

      vbi_chains_working = 0;
      return 0;
   }
}

ssize_t write(int fd, const void *buf, size_t count)
{
   CHECK_INIT();

   if ((fd != vbi_fd) || vbi_chains_working)
   {
      return orig_write(fd, buf, count);
   }
   else if (vbi_fd == -1)
   {
      errno = EBADF;
      return -1;
   }
   else
   {
      dprintf1("write() called for VBI - ignored\n");
      /* write access to VBI device is useless */
      /* indicate nothing's been written */
      return 0;
   }
}

ssize_t read(int fd, void *buf, size_t count)
{
   CHECK_INIT();

   if ((fd != vbi_fd) || vbi_chains_working)
   {
      return orig_read(fd, buf, count);
   }
   else if (vbi_fd == -1)
   {
      errno = EBADF;
      return -1;
   }
   else
   {
      vbi_capture *p_capt;
      vbi_capture_buffer *p_capt_buf;
      struct timeval timeout;
      double timestamp;
      int result;

      dprintf2("read %d bytes buf=0x%lX\n", (int) count, (long) buf);
      vbi_chains_working = 1;

      p_capt = vbi_proxy_client_get_capture_if(p_proxy_client);
      timeout.tv_sec = (vbi_fd_nonblocking ? 0 : 60 * 60 * 24);
      timeout.tv_usec = 0;

      if (count >= (size_t) vbi_buf_size)
      {
         /* buffer is large enough -> capture directly into the user buffer */
         result = vbi_capture_read_raw(p_capt, buf, &timestamp, &timeout);
         if (result > 0)
         {
            /* stamp frame sequence number into last 4 byes of each frame's data */
            *(unsigned int *) ((long) buf + count - 4) = vbi_seq_no++;

            result = vbi_buf_size;
         }
         else if (result == 0)
         {
            result = -1;
            errno = EAGAIN;
         }
      }
      else
      {
         /* buffer not large enough -> copy manually */
         result = vbi_capture_pull_raw(p_capt, &p_capt_buf, &timeout);
         if (result > 0)
         {
            /* copy requested portion into user buffer (rest of frame's data is discarded) */
            if (count > (size_t) p_capt_buf->size)
               count = p_capt_buf->size;
            memcpy(buf, p_capt_buf->data, count);

            *(unsigned int *) ((long) buf + count - 4) = vbi_seq_no++;

            result = count;
         }
         else if (result == 0)
         {
            result = -1;
            errno = EAGAIN;
         }
      }
      vbi_chains_working = 0;
      dprintf2("read returns %d (of %d)\n", (int) result, vbi_buf_size);

      return (ssize_t) result;
   }
}

#endif /* !ENABLE_PROXY || !ENABLE_V4L */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
