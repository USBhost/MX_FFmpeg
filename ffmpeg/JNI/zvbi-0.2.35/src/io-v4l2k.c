/*
 *  libzvbi -- Video For Linux Two (version 2002-10 and later)
 *             driver interface
 *
 *  Copyright (C) 2002-2005 Michael H. Schimek
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

static const char rcsid [] =
"$Id: io-v4l2k.c,v 1.50 2009/12/14 23:43:20 mschimek Exp $";

/*
 *  Around Oct-Nov 2002 the V4L2 API was revised for inclusion into
 *  Linux 2.5/2.6. There are a few subtle differences, in order to
 *  keep the source clean this interface has been forked off from the
 *  old V4L2 interface. "v4l2k" is no official designation, there is
 *  none, take it as v4l2-kernel or v4l-2000.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "vbi.h"
#include "io.h"

#ifdef ENABLE_V4L2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>		/* read(), dup2() */
#include <assert.h>
#include <sys/time.h>		/* timeval */
#include <sys/types.h>		/* fd_set */
#include <sys/ioctl.h>		/* for (_)videodev2k.h */
#include <sys/mman.h>		/* PROT_READ, MAP_SHARED */
#include <asm/types.h>		/* __u8 and friends for videodev2k.h */
#include <pthread.h>

#include "raw_decoder.h"
#include "version.h"

#ifndef HAVE_S64_U64
#  include <inttypes.h>
  /* Linux 2.6.x asm/types.h defines __s64 and __u64 only
     if __GNUC__ is defined. */
typedef int64_t __s64;
typedef uint64_t __u64;
#endif

#include "videodev2k.h"
#include "_videodev2k.h"

/* This macro checks at compile time if the arg type is correct,
   device_ioctl() repeats the ioctl if interrupted (EINTR) and logs
   the args and result if sys_log_fp is non-zero. */
#define xioctl(v, cmd, arg)						\
(IOCTL_ARG_TYPE_CHECK_ ## cmd (arg),					\
 device_ioctl (v->capture.sys_log_fp, fprint_ioctl_arg, v->fd,		\
               cmd, (void *)(arg)))

#undef REQUIRE_SELECT
#undef REQUIRE_G_FMT		/* before S_FMT */
#undef REQUIRE_S_FMT		/* else accept current format */

#define ENQUEUE_SUSPENDED       -3
#define ENQUEUE_STREAM_OFF      -2
#define ENQUEUE_BUFS_QUEUED     -1
#define ENQUEUE_IS_UNQUEUED(X)  ((X) >= 0)

#define FLUSH_FRAME_COUNT       2

typedef struct vbi_capture_v4l2 {
	vbi_capture		capture;

	int			fd;
	vbi_bool		close_me;
	int			btype;			/* v4l2 stream type */
	vbi_bool		streaming;
	vbi_bool		read_active;
	int			has_try_fmt;
	int			enqueue;
	struct v4l2_buffer      vbuf;
	struct v4l2_capability  vcap;
	char		      * p_dev_name;

	vbi_sampling_par	sp;
	vbi3_raw_decoder	rd;
        unsigned int            services;    /* all services, including raw */

	double			time_per_frame;

	vbi_capture_buffer	*raw_buffer;
	unsigned int		num_raw_buffers;
	int			buf_req_count;

	vbi_capture_buffer	sliced_buffer;
	int			flush_frame_count;

	vbi_bool		pal_start1_fix;
	vbi_bool		saa7134_ntsc_fix;
	vbi_bool		bttv_offset_fix;
	vbi_bool		cx88_ntsc_fix;
	vbi_bool		bttv_min_start_fix;
	vbi_bool		bttv_ntsc_rate_fix;

	_vbi_log_hook		log;

} vbi_capture_v4l2;

static void
vbi_sliced_data_from_raw	(vbi_capture_v4l2 *	v,
				 vbi_capture_buffer **	sliced,
				 const vbi_capture_buffer *raw)
{
	vbi_capture_buffer *b;
	unsigned int max_lines;
	unsigned int n_lines;

	assert (NULL != sliced);

	b = *sliced;
	if (NULL == b) {
		/* Store sliced data in our buffer
		   and return a buffer pointer. */
		b = &v->sliced_buffer;
		*sliced = b;
	} else {
		/* Store sliced data in client buffer. */
	}

	max_lines = v->sp.count[0] + v->sp.count[1];

	n_lines = vbi3_raw_decoder_decode (&v->rd,
					   (vbi_sliced *) b->data,
					   max_lines,
					   (uint8_t *) raw->data);

	b->size = n_lines * sizeof (vbi_sliced);
	b->timestamp = raw->timestamp;
}


static void
v4l2_stream_stop(vbi_capture_v4l2 *v)
{
	if (v->enqueue >= ENQUEUE_BUFS_QUEUED) {
		info (&v->log, "Suspending stream.");

		if (-1 == xioctl (v, VIDIOC_STREAMOFF, &v->btype)) {
			/* Error ignored. */
		}
	}

	for (; v->num_raw_buffers > 0; v->num_raw_buffers--) {
		device_munmap (v->capture.sys_log_fp,
			       v->raw_buffer[v->num_raw_buffers - 1].data,
			       v->raw_buffer[v->num_raw_buffers - 1].size);
	}

	if (v->raw_buffer != NULL) {
		free(v->raw_buffer);
		v->raw_buffer = NULL;
	}

	v->enqueue = ENQUEUE_SUSPENDED;
}


static int
v4l2_stream_alloc(vbi_capture_v4l2 *v, char ** errstr)
{
	struct v4l2_requestbuffers vrbuf;
	struct v4l2_buffer vbuf;
	char * guess = NULL;
        int errno_copy;

	assert(v->enqueue == ENQUEUE_SUSPENDED);
	assert(v->raw_buffer == NULL);

	info (&v->log,
	      "Requesting %d streaming i/o buffers.",
	      v->buf_req_count);

	memset(&vrbuf, 0, sizeof(vrbuf));
	vrbuf.type = v->btype;
	vrbuf.count = v->buf_req_count;
	vrbuf.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl (v, VIDIOC_REQBUFS, &vrbuf)) {
		asprintf (errstr,
			  _("Cannot request streaming i/o buffers "
			    "from %s (%s): %s."),
			  v->p_dev_name, v->vcap.card,
			  strerror(errno));
		guess = _("Possibly a driver bug.");
		goto failure;
	}

	if (vrbuf.count == 0) {
		asprintf(errstr, _("%s (%s) granted no streaming i/o buffers, "
				   "perhaps the physical memory is exhausted."),
			     v->p_dev_name, v->vcap.card);
		goto failure;
	}

	info (&v->log,
	      "Mapping %d streaming i/o buffers.",
	      vrbuf.count);

	v->raw_buffer = calloc(vrbuf.count, sizeof(v->raw_buffer[0]));

	if (v->raw_buffer == NULL) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->num_raw_buffers = 0;

	while (v->num_raw_buffers < vrbuf.count) {
		uint8_t *p;

		vbuf.type = v->btype;
		vbuf.index = v->num_raw_buffers;
		vbuf.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl (v, VIDIOC_QUERYBUF, &vbuf)) {
			asprintf (errstr,
				  _("Querying streaming i/o buffer #%d "
				    "from %s (%s) failed: %s."),
				  v->num_raw_buffers, v->p_dev_name,
				  v->vcap.card, strerror(errno));
			goto mmap_failure;
		}

		p = device_mmap (v->capture.sys_log_fp,
				 /* start any */ NULL,
				 vbuf.length,
				 PROT_READ | PROT_WRITE,
				 MAP_SHARED, /* MAP_PRIVATE ? */
				 v->fd,
				 vbuf.m.offset);

		/* V4L2 spec requires PROT_WRITE regardless if we write
		   buffers, but broken drivers might reject it. */
		if (MAP_FAILED == p)
			p = device_mmap (v->capture.sys_log_fp,
					 /* start any */ NULL,
					 vbuf.length,
					 PROT_READ,
					 MAP_SHARED,
					 v->fd,
					 vbuf.m.offset);

		if (MAP_FAILED == p) {
			if (errno == ENOMEM && v->num_raw_buffers >= 2) {
				info (&v->log,
				      "Memory mapping buffer #%d "
				      "failed with errno %d (ignored).",
				      v->num_raw_buffers, errno);
				break;
			}

			asprintf(errstr, _("Memory mapping streaming i/o buffer #%d "
					   "from %s (%s) failed: %s."),
				 v->num_raw_buffers, v->p_dev_name, v->vcap.card,
				 strerror(errno));
			goto mmap_failure;
		} else {
			unsigned int i, s;

			v->raw_buffer[v->num_raw_buffers].data = p;
			v->raw_buffer[v->num_raw_buffers].size = vbuf.length;

			for (i = s = 0; i < vbuf.length; i++)
				s += p[i];

			if (s % vbuf.length) {
				fprintf(stderr,
				       "Security warning: driver %s (%s) seems to mmap "
				       "physical memory uncleared. Please contact the "
				       "driver author.\n", v->p_dev_name, v->vcap.card);
				exit(EXIT_FAILURE);
			}
		}

		if (-1 == xioctl (v, VIDIOC_QBUF, &vbuf)) {
			asprintf (errstr,
				  _("Cannot enqueue streaming i/o buffer "
				    "#%d to %s (%s): %s."),
				  v->num_raw_buffers, v->p_dev_name,
				  v->vcap.card, strerror(errno));
			guess = _("Probably a driver bug.");
			goto mmap_failure;
		}

		v->num_raw_buffers++;
	}

	v->enqueue = ENQUEUE_STREAM_OFF;

	return 0;

mmap_failure:
        errno_copy = errno;
	v4l2_stream_stop(v);
        errno = errno_copy;

failure:
	info (&v->log,
	      "Failed with errno %d, errmsg '%s'.",
	      errno, *errstr);

	return -1;
}

static vbi_bool
restart_stream			(vbi_capture_v4l2 *	v)
{
	unsigned int i;

	if (-1 == xioctl (v, VIDIOC_STREAMOFF, &v->btype))
		return FALSE;

	for (i = 0; i < v->num_raw_buffers; ++i) {
		struct v4l2_buffer vbuf;

		CLEAR (vbuf);

		vbuf.index = i;
		vbuf.type = v->btype;
		vbuf.memory = V4L2_MEMORY_MMAP;

		/* Error ignored. */
		xioctl (v, VIDIOC_QBUF, &vbuf);
	}

	if (-1 == xioctl (v, VIDIOC_STREAMON, &v->btype))
		return FALSE;

	return TRUE;
}

static int
v4l2_stream(vbi_capture *vc, vbi_capture_buffer **raw,
	    vbi_capture_buffer **sliced, const struct timeval *timeout_orig)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	struct timeval timeout = *timeout_orig;
	vbi_capture_buffer *b;
	int r;

	if ((v->enqueue == ENQUEUE_SUSPENDED) || (v->services == 0)) {
		/* stream was suspended (add_services not committed) */
		error (&v->log, "No services set or not committed.");
		errno = ESRCH;
		return -1;
	}

	if (v->enqueue == ENQUEUE_STREAM_OFF) {
		if (-1 == xioctl (v, VIDIOC_STREAMON, &v->btype)) {
			error (&v->log,
			       "Failed to enable streaming, errno %d.",
			       errno);
			return -1;
                }
	} else if (ENQUEUE_IS_UNQUEUED(v->enqueue)) {
		v->vbuf.type = v->btype;
		v->vbuf.index = v->enqueue;
		v->vbuf.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf)) {
			error (&v->log,
			       "Failed to enqueue previous "
			       "buffer, errno %d.",
			       errno);
			return -1;
                }
	}

	v->enqueue = ENQUEUE_BUFS_QUEUED;

	while (1)
	{
		/* wait for the next frame */
		r = vbi_capture_io_select(v->fd, &timeout);
		if (r <= 0) {
			if (r < 0) {
				error (&v->log,
				       "select() failed with errno %d.",
				       errno);
			}
			return r;
		}

		v->vbuf.type = v->btype;
		v->vbuf.memory = V4L2_MEMORY_MMAP;

		/* retrieve the captured frame from the queue */
		r = xioctl (v, VIDIOC_DQBUF, &v->vbuf);
		if (-1 == r) {
			int saved_errno;

			saved_errno = errno;

			error (&v->log,
			       "Failed to dequeue buffer, errno %d.",
			       errno);

			/* On EIO bttv dequeues the buffer, other drivers
			   may not. Actually the caller should restart
			   on error. */

			/* Errors ignored. */
			restart_stream (v);

                        errno = saved_errno;

			return -1;
		}

		if (v->flush_frame_count > 0) {
			v->flush_frame_count -= 1;
			info (&v->log,
			      "Skipping frame (%d remaining).",
			      v->flush_frame_count);

			if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf)) {
				error (&v->log,
				       "Failed to enqueue buffer, errno %d.",
				       errno);
				return -1;
			}
		} else {
			break;
		}
	}

	assert (v->vbuf.index < v->num_raw_buffers);

	b = &v->raw_buffer[v->vbuf.index];
	b->timestamp = v->vbuf.timestamp.tv_sec
		+ v->vbuf.timestamp.tv_usec * (1 / 1e6);

	if (NULL != raw) {
		vbi_capture_buffer *r;

		r = *raw; 
		if (NULL == r) {
			/* Store raw data in our buffer
			   and return a buffer pointer. */
			*raw = b;

			/* Keep this buffer out of the queue. */
			v->enqueue = v->vbuf.index;   
		} else {
			/* Store raw data in client buffer. */
			memcpy (r->data, b->data, b->size);

			/* FIXME client should pass max buffer size. */
			r->size = b->size;

			r->timestamp = b->timestamp; 
		}
	}

	if (NULL != sliced) {
		vbi_sliced_data_from_raw (v, sliced, b);
	}

	/* if no raw pointer returned to the caller, re-queue buffer immediately
	** else the buffer is re-queued upon the next call to read() */
	if (v->enqueue == ENQUEUE_BUFS_QUEUED) {
		if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf)) {
			error (&v->log,
			       "Failed to queue buffer, errno %d.",
			       errno);
			return -1;
		}
	}

	return 1;
}

static void
v4l2_stream_flush(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	struct timeval tv;
	unsigned int max_loop;

	/* stream not enabled yet -> nothing to flush */
	if ( (v->enqueue == ENQUEUE_SUSPENDED) ||
	     (v->enqueue == ENQUEUE_STREAM_OFF) )
		return;

	if (ENQUEUE_IS_UNQUEUED(v->enqueue)) {
		v->vbuf.type = v->btype;
		v->vbuf.index = v->enqueue;
		v->vbuf.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf)) {
			error (&v->log,
			       "Failed to enqueue buffer, errno %d.",
			       errno);
			return;
		}
	}
	v->enqueue = ENQUEUE_BUFS_QUEUED;

	for (max_loop = 0; max_loop < v->num_raw_buffers; max_loop++) {

		/* check if there are any buffers pending for de-queueing */
		/* use zero timeout to prevent select() from blocking */
		memset(&tv, 0, sizeof(tv));
		if (vbi_capture_io_select(v->fd, &tv) <= 0)
			return;

		if ((-1 == xioctl (v, VIDIOC_DQBUF, &v->vbuf))
		    && (errno != EIO))
			return;

		/* immediately queue the buffer again, thereby discarding it's content */
		if (-1 == xioctl (v, VIDIOC_QBUF, &v->vbuf))
			return;
	}
}


static void
v4l2_read_stop(vbi_capture_v4l2 *v)
{
	for (; v->num_raw_buffers > 0; v->num_raw_buffers--) {
		free(v->raw_buffer[v->num_raw_buffers - 1].data);
		v->raw_buffer[v->num_raw_buffers - 1].data = NULL;
	}

	free(v->raw_buffer);
	v->raw_buffer = NULL;
}


static int
v4l2_suspend(vbi_capture_v4l2 *v)
{
	int    fd;

	if (v->streaming) {
		v4l2_stream_stop(v);
	}
	else {
		v4l2_read_stop(v);

		if (v->read_active) {
			info (&v->log, "Reopen device.");

			/* hack: cannot suspend read to allow S_FMT, need to close device */
			fd = device_open (v->capture.sys_log_fp,
					  v->p_dev_name, O_RDWR, 0);
			if (fd == -1) {
				error (&v->log,
				       "Failed to reopen device, "
				       "errno %d.",
				       errno);
				return -1;
			}

			/* use dup2() to keep the same fd, which may be used by our client */
			device_close (v->capture.sys_log_fp, v->fd);
			dup2(fd, v->fd);
			device_close (v->capture.sys_log_fp, fd);

			v->read_active = FALSE;
		}
	}
	return 0;
}

static int
v4l2_read_alloc(vbi_capture_v4l2 *v, char ** errstr)
{
	assert(v->raw_buffer == NULL);

	v->raw_buffer = calloc(1, sizeof(v->raw_buffer[0]));

	if (!v->raw_buffer) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->raw_buffer[0].size = (v->sp.count[0] + v->sp.count[1])
		* v->sp.bytes_per_line;

	v->raw_buffer[0].data = malloc(v->raw_buffer[0].size);

	if (!v->raw_buffer[0].data) {
		asprintf(errstr, _("Not enough memory to allocate "
				   "vbi capture buffer (%d KB)."),
			 (v->raw_buffer[0].size + 1023) >> 10);
		goto failure;
	}

	v->num_raw_buffers = 1;

	info (&v->log, "Capture buffer allocated.");

	return 0;

failure:
	info (&v->log,
	      "Failed with errno %d, errmsg '%s'.",
	      errno, *errstr);

	return -1;
}

static int
v4l2_read_frame(vbi_capture_v4l2 *v, vbi_capture_buffer *raw, struct timeval *timeout)
{
	int r;

	/* wait until data is available or timeout expires */
	r = vbi_capture_io_select(v->fd, timeout);
	if (r <= 0) {
		if (r < 0) {
			error (&v->log,
			       "select() failed with errno %d.",
			       errno);
		}
		return r;
	}

	v->read_active = TRUE;

	for (;;) {
		/* from zapping/libvbi/v4lx.c */
		pthread_testcancel();

		r = read(v->fd, raw->data, raw->size);

		if (r == -1  && (errno == EINTR || errno == ETIME))
			continue;
		if (r == -1)
                        return -1;
		if (r != raw->size) {
			errno = EIO;
			return -1;
		}
		else
			break;
	}
	return 1;
}

static int
v4l2_read(vbi_capture *vc, vbi_capture_buffer **raw,
	  vbi_capture_buffer **sliced, const struct timeval *timeout)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	vbi_capture_buffer *my_raw = v->raw_buffer;
	struct timeval tv;
	int r;

	if ((my_raw == NULL) || (v->services == 0)) {
		info (&v->log,
		      "No services set or not committed.");
		errno = EINVAL;
		return -1;
	}

	if (raw == NULL)
		raw = &my_raw;
	if (*raw == NULL)
		*raw = v->raw_buffer;
	else
		(*raw)->size = v->raw_buffer[0].size;

	tv = *timeout;
	while (1)
	{
		r = v4l2_read_frame(v, *raw, &tv);
		if (r <= 0)
			return r;

		if (v->flush_frame_count > 0) {
			v->flush_frame_count -= 1;
			info (&v->log,
			      "Skipping frame (%d remaining).",
			      v->flush_frame_count);
		}
		else
			break;
	}

	gettimeofday(&tv, NULL);

	(*raw)->timestamp = tv.tv_sec + tv.tv_usec * (1 / 1e6);

	if (sliced) {
		vbi_sliced_data_from_raw (v, sliced, *raw);
	}

	return 1;
}

static void v4l2_read_flush(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	struct timeval tv;
	int r;

	if ( (v->raw_buffer == NULL) || (v->read_active == FALSE) )
		return;

	memset(&tv, 0, sizeof(tv));

	r = vbi_capture_io_select(v->fd, &tv);
	if (r <= 0)
		return;

	do
	{
		r = read(v->fd, v->raw_buffer->data, v->raw_buffer->size);
	}
	while ((r < 0) && (errno == EINTR));
}

static vbi_bool
v4l2_get_videostd(vbi_capture_v4l2 *v, char ** errstr)
{
	struct v4l2_standard vstd;
	v4l2_std_id stdid;
	unsigned int i;
	int r;
	char * guess = NULL;

	if (-1 == xioctl (v, VIDIOC_G_STD, &stdid)) {
		asprintf (errstr,
			  _("Cannot query current videostandard "
			    "of %s (%s): %s."),
			  v->p_dev_name, v->vcap.card,
			  strerror(errno));
		guess = _("Probably a driver bug.");
		goto failure;
	}

	for (i = 0; i < 100; ++i) {
		CLEAR (vstd);
		vstd.index = i;

		r = xioctl (v, VIDIOC_ENUMSTD, &vstd);
		if (-1 == r)
			break;

		if (vstd.id & stdid)
			break;
	}

	if (i >= 100) {
		r = -1;
		errno = 0;
	}

	if (-1 == r) {
		asprintf(errstr, _("Cannot query current "
				   "videostandard of %s (%s): %s."),
			 v->p_dev_name, v->vcap.card, strerror(errno));
		guess = _("Probably a driver bug.");
		goto failure;
	}

	info (&v->log,
	      "Current scanning system is %d.",
	      vstd.framelines);

	/* add_vbi_services() eliminates non 525/625 */
	v->sp.scanning = vstd.framelines;

	return TRUE;

failure:
	info (&v->log,
	      "Failed with errno %d, errmsg '%s'.",
	      errno, *errstr);

	return FALSE;
}

static int
v4l2_get_scanning(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
        int old_scanning = v->sp.scanning;
        int new_scanning = -1;

        if ( v4l2_get_videostd(v, NULL) ) {

                new_scanning = v->sp.scanning;
        }
        v->sp.scanning = old_scanning;

        return new_scanning;
}

static void
print_vfmt			(vbi_capture_v4l2 *	v,
				 const char *		s,
				 struct v4l2_format *	vfmt)
{
	if (0 == (v->log.mask & VBI_LOG_INFO))
		return;

	_vbi_log_printf (v->log.fn,
			 v->log.user_data,
			 VBI_LOG_INFO,
			 __FILE__,
			 __FUNCTION__,
			 "%sformat %08x [%c%c%c%c], %d Hz, %d bpl, offs %d, "
			 "F1 %d...%d, F2 %d...%d, flags %08x.", s,
			 vfmt->fmt.vbi.sample_format,
			 (char)((vfmt->fmt.vbi.sample_format      ) & 0xff),
			 (char)((vfmt->fmt.vbi.sample_format >>  8) & 0xff),
			 (char)((vfmt->fmt.vbi.sample_format >> 16) & 0xff),
			 (char)((vfmt->fmt.vbi.sample_format >> 24) & 0xff),
			 vfmt->fmt.vbi.sampling_rate,
			 vfmt->fmt.vbi.samples_per_line,
			 vfmt->fmt.vbi.offset,
			 vfmt->fmt.vbi.start[0],
			 vfmt->fmt.vbi.start[0] + vfmt->fmt.vbi.count[0] - 1,
			 vfmt->fmt.vbi.start[1],
			 vfmt->fmt.vbi.start[1] + vfmt->fmt.vbi.count[1] - 1,
			 vfmt->fmt.vbi.flags);
}

static unsigned int
v4l2_update_services(vbi_capture *vc,
		     vbi_bool reset, vbi_bool commit,
		     unsigned int services, int strict,
		     char ** errstr)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	struct v4l2_format vfmt;
	unsigned int    max_rate;
	int    g_fmt;
	int    s_fmt;
	char * guess = NULL;

	/* suspend capturing, or driver will return EBUSY */
	v4l2_suspend(v);

	if (reset) {
                /* query current norm */
	        if (v4l2_get_videostd(v, errstr) == FALSE)
		        goto io_error;

		vbi3_raw_decoder_reset (&v->rd);

                v->services = 0;
        }

	memset(&vfmt, 0, sizeof(vfmt));

	vfmt.type = v->btype = V4L2_BUF_TYPE_VBI_CAPTURE;

	max_rate = 0;

	info (&v->log, "Querying current vbi parameters...");

	g_fmt = xioctl (v, VIDIOC_G_FMT, &vfmt);

	if (-1 == g_fmt) {
		info (&v->log,
		      "...failed with errno %d.",
		      errno);
#ifdef REQUIRE_G_FMT
		asprintf(errstr, _("Cannot query current "
				   "vbi parameters of %s (%s): %s."),
			 v->p_dev_name, v->vcap.card, strerror(errno));
		goto io_error;
#else
		strict = MAX(0, strict);
#endif
	} else {
		info (&v->log, "...success.");

		print_vfmt (v, "VBI capture parameters supported: ", &vfmt);

		if (v->has_try_fmt == -1) {
			struct v4l2_format vfmt_temp = vfmt;

			/* test if TRY_FMT is available by feeding it the current
			** parameters, which should always succeed */
			v->has_try_fmt =
				(0 == xioctl (v, VIDIOC_TRY_FMT, &vfmt_temp));
		}
	}

	if (strict >= 0) {
		struct v4l2_format vfmt_temp = vfmt;
		vbi_sampling_par dec_temp;
		unsigned int f2_offset;
	        unsigned int sup_services;
		int r;

		info (&v->log, "Attempt to set vbi capture parameters.");

		memset(&dec_temp, 0, sizeof(dec_temp));

		sup_services = _vbi_sampling_par_from_services_log
			(&dec_temp, &max_rate,
			 _vbi_videostd_set_from_scanning (v->sp.scanning),
			 services | v->services, &v->log);

	        services &= sup_services;

		if (0 == services) {
			asprintf(errstr,
				 _("Sorry, %s (%s) cannot capture any of the "
				   "requested data services with scanning %d."),
				 v->p_dev_name, v->vcap.card, v->sp.scanning);
			goto failure;
		}

		vfmt.fmt.vbi.sample_format	= V4L2_PIX_FMT_GREY;
		vfmt.fmt.vbi.sampling_rate	= dec_temp.sampling_rate;
		vfmt.fmt.vbi.samples_per_line	= dec_temp.bytes_per_line;
		vfmt.fmt.vbi.offset		= dec_temp.offset;
		vfmt.fmt.vbi.start[0]		= dec_temp.start[0];
		vfmt.fmt.vbi.count[0]		= dec_temp.count[0];
		vfmt.fmt.vbi.start[1]		= dec_temp.start[1];
		vfmt.fmt.vbi.count[1]		= dec_temp.count[1];

		f2_offset = (625 == v->sp.scanning) ? 312 : 263;

		/* Some broken drivers may take start (= 0) into account
		   despite count being zero. */
		if (0 == vfmt.fmt.vbi.count[1]) {
			vfmt.fmt.vbi.start[1] =
				vfmt.fmt.vbi.start[0] + f2_offset;
		} else if (0 == vfmt.fmt.vbi.count[0]) {
			vfmt.fmt.vbi.start[0] =
				vfmt.fmt.vbi.start[1] - f2_offset;
		}

		if (v->bttv_min_start_fix) {
			int min_start[2];
			unsigned int i;

			/* Captures MAX (count[0], count[1]) lines
			   starting at min_start, ignoring the
			   requested start: proxy-test vps. */

			if (625 == v->sp.scanning) {
				min_start[0] = 7;
				min_start[1] = 320;
			} else {
				min_start[0] = 10;
				min_start[1] = 273;
			}

			for (i = 0; i < 2; ++i) {
				if (vfmt.fmt.vbi.count[i] > 0) {
					vfmt.fmt.vbi.count[i] +=
						(int) vfmt.fmt.vbi.start[i]
						- min_start[i];
					vfmt.fmt.vbi.start[i] = min_start[i];
				}
			}
		}

		/* 0 == count check omitted because above we made
		   sure start is valid and drivers should ignore
		   it in this case anyway. */
		if (v->pal_start1_fix
		    && 625 == v->sp.scanning) {
			vfmt.fmt.vbi.start[1] -= 1;
		}

		if (v->saa7134_ntsc_fix
		    && 525 == v->sp.scanning) {
			vfmt.fmt.vbi.start[0] += 6;
			vfmt.fmt.vbi.start[1] += 6;
		}

		print_vfmt (v, "VBI capture parameters requested: ", &vfmt);

		if ((v->has_try_fmt != 1) || commit) {
			s_fmt = VIDIOC_S_FMT;
			/* Arg type check requires constant cmd number. */
			r = xioctl (v, VIDIOC_S_FMT, &vfmt);
		} else {
			s_fmt = VIDIOC_TRY_FMT;
			r = xioctl (v, VIDIOC_TRY_FMT, &vfmt);
		}

		if (-1 == r) {
			switch (errno) {
			case EBUSY:
#ifndef REQUIRE_S_FMT
				if (g_fmt != -1) {
					info (&v->log,
					      "VIDIOC_S_FMT returned EBUSY, "
					      "will try the current "
					      "parameters.");

					vfmt = vfmt_temp;
					break;
				}
#endif
				asprintf(errstr, _("Cannot initialize %s (%s), "
						   "the device is already in use."),
					 v->p_dev_name, v->vcap.card);
				goto io_error;

			default:
				asprintf(errstr, _("Could not set the vbi capture parameters "
						   "for %s (%s): %d, %s."),
					 v->p_dev_name, v->vcap.card, errno, strerror(errno));
				guess = _("Possibly a driver bug.");
				goto io_error;
			}

			if (commit && (v->has_try_fmt == 1)
			    && 0 != vbi3_raw_decoder_services (&v->rd)) {
				/* FIXME strictness of services is not considered */
				unsigned int old_services;
				unsigned int tmp_services;

				old_services = vbi3_raw_decoder_services (&v->rd);

				tmp_services =
					_vbi_sampling_par_check_services_log
					(&v->sp, old_services, /* strict */ 0,
					 &v->log);

				if (old_services != tmp_services)
					vbi3_raw_decoder_remove_services
						(&v->rd, old_services & ~ tmp_services);
			}

		} else {
			info (&v->log,
			      "Successfully %s vbi capture "
			      "parameters.",
			      ((s_fmt == (int)VIDIOC_S_FMT)
			       ? "set" : "tried"));
		}
	}

	print_vfmt (v, "VBI capture parameters granted: ", &vfmt);

	{
		vbi_bool fixed = FALSE;

		if (v->cx88_ntsc_fix
		    && 9 == vfmt.fmt.vbi.start[0]
		    && 272 == vfmt.fmt.vbi.start[1]) {
			/* Captures only 288 * 4 samples/line,
			   no work-around possible. */
			/* XXX when was this fixed? */
			asprintf (errstr,
				  _("A known bug in driver %s %u.%u.%u "
				    "impedes VBI capturing in NTSC mode. "
				    "Please upgrade the driver."),
				  v->vcap.driver,
				  (v->vcap.version >> 16) & 0xFF,
				  (v->vcap.version >> 8) & 0xFF,
				  (v->vcap.version >> 0) & 0xFF);
			errno = 0;
			goto io_error;
		}

		if (v->pal_start1_fix
		    && 625 == v->sp.scanning
		    && 319 == vfmt.fmt.vbi.start[1]) {
			vfmt.fmt.vbi.start[1] += 1;
			fixed = TRUE;
		}

		if (v->bttv_offset_fix
		    && 128 == vfmt.fmt.vbi.offset) {
			vfmt.fmt.vbi.offset = 244;
			fixed = TRUE;
		}

		if (v->bttv_ntsc_rate_fix
		    && 525 == v->sp.scanning
		    && 35468950 == vfmt.fmt.vbi.sampling_rate) {
			vfmt.fmt.vbi.sampling_rate = 28636363;
			fixed = TRUE;
		}

		if (fixed)
			print_vfmt (v, "Fixes applied: ", &vfmt);
	}

	v->sp.sampling_rate	= vfmt.fmt.vbi.sampling_rate;
	v->sp.bytes_per_line	= vfmt.fmt.vbi.samples_per_line;
	v->sp.offset		= vfmt.fmt.vbi.offset;
	v->sp.start[0] 		= vfmt.fmt.vbi.start[0];
	v->sp.start[1] 		= vfmt.fmt.vbi.start[1];
	v->sp.count[0] 		= vfmt.fmt.vbi.count[0];
	v->sp.count[1] 		= vfmt.fmt.vbi.count[1];

	v->sp.interlaced	= !!(vfmt.fmt.vbi.flags
				     & V4L2_VBI_INTERLACED);
	v->sp.synchronous	= !(vfmt.fmt.vbi.flags
				    & V4L2_VBI_UNSYNC);
	v->time_per_frame 	= ((v->sp.scanning == 625)
					   ? 1.0 / 25 : 1001.0 / 30000);
	v->sp.sampling_format	= VBI_PIXFMT_YUV420;

 	if (vfmt.fmt.vbi.sample_format != V4L2_PIX_FMT_GREY) {
		asprintf(errstr, _("%s (%s) offers unknown vbi sampling format #%d. "
				   "This may be a driver bug or libzvbi is too old."),
			 v->p_dev_name, v->vcap.card, vfmt.fmt.vbi.sample_format);
		goto io_error;
	}

	/* grow pattern array if necessary
	** note: must do this even if service 
	** add fails later, to stay in sync with driver */
	vbi3_raw_decoder_set_sampling_par (&v->rd, &v->sp, /* strict */ 0);

	if (services & ~(VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625)) {
		/* Nyquist (we're generous at 1.5) */

		if (v->sp.sampling_rate < (int) max_rate * 3 / 2) {
			asprintf(errstr, _("Cannot capture the requested "
					   "data services with "
					   "%s (%s), the sampling frequency "
					   "%.2f MHz is too low."),
				 v->p_dev_name, v->vcap.card,
				 v->sp.sampling_rate / 1e6);
                        services = 0;
			goto failure;
		}

		info (&v->log, "Nyquist check passed.");

		info (&v->log,
		      "Request decoding of services 0x%08x, "
		      "strict level %d.",
		      services, strict);

		/* those services which are already set must be checked for strictness */
		if (strict > 0 && 0 != (services & vbi3_raw_decoder_services (&v->rd))) {
			unsigned int old_services;
			unsigned int tmp_services;

			old_services = vbi3_raw_decoder_services (&v->rd);
			tmp_services = _vbi_sampling_par_check_services_log
				(&v->sp, services & old_services, strict,
				 &v->log);
			/* mask out unsupported services */
			services &= tmp_services | ~(services & old_services);
		}

		if ( (services & ~vbi3_raw_decoder_services (&v->rd)) != 0 )
	                services &= vbi3_raw_decoder_add_services
				(&v->rd, services
				 & ~vbi3_raw_decoder_services (&v->rd), strict);

		if (services == 0) {
			asprintf(errstr, _("Sorry, %s (%s) cannot capture any of "
					   "the requested data services."),
				 v->p_dev_name, v->vcap.card);
			goto failure;
		}

		if (v->sliced_buffer.data != NULL)
			free(v->sliced_buffer.data);

		v->sliced_buffer.data =
			malloc((v->sp.count[0] + v->sp.count[1]) * sizeof(vbi_sliced));

		if (!v->sliced_buffer.data) {
			asprintf(errstr, _("Virtual memory exhausted."));
			errno = ENOMEM;
			goto io_error;
		}
	}

failure:
        v->services |= services;

	info (&v->log,
	      "Will capture services 0x%08x, "
	      "added 0x%0x commit=%d.",
	      v->services, services, commit);

	if (commit && (v->services != 0)) {
		if (v->streaming) {
			if (v4l2_stream_alloc(v, errstr) != 0)
				goto io_error;
		} else {
			if (v4l2_read_alloc(v, errstr) != 0)
				goto io_error;
		}
	}

	return services;

io_error:
	info (&v->log,
	      "Failed with errno %d, errmsg '%s'.",
	      errno, *errstr);

	return 0;
}

#if 3 == VBI_VERSION_MINOR
static vbi_sampling_par *
v4l2_parameters(vbi_capture *vc)
#else
static vbi_raw_decoder *
v4l2_parameters(vbi_capture *vc)
#endif
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);

	/* For compatibility in libzvbi 0.2
	   struct vbi_sampling_par == vbi_raw_decoder. In 0.3
	   we'll drop the decoding related fields. */
	return &v->sp;
}

static void
v4l2_delete(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);

	if (v->streaming)
		v4l2_stream_stop(v);
	else
		v4l2_read_stop(v);

	_vbi3_raw_decoder_destroy (&v->rd);

	if (v->sliced_buffer.data)
		free(v->sliced_buffer.data);

	if (v->p_dev_name != NULL)
		free(v->p_dev_name);

	if (v->close_me && v->fd != -1)
		device_close (v->capture.sys_log_fp, v->fd);

	free(v);
}

static VBI_CAPTURE_FD_FLAGS
v4l2_get_fd_flags(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);
	VBI_CAPTURE_FD_FLAGS result;

        result = VBI_FD_IS_DEVICE | VBI_FD_HAS_SELECT;
        if (v->streaming)
                result |= VBI_FD_HAS_MMAP;

        return result;
}

static int
v4l2_get_fd(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);

	return v->fd;
}

static void
v4l2_flush(vbi_capture *vc)
{
	vbi_capture_v4l2 *v = PARENT(vc, vbi_capture_v4l2, capture);

	v->flush_frame_count = FLUSH_FRAME_COUNT;

	if (v->streaming)
		v4l2_stream_flush(vc);
	else
		v4l2_read_flush(vc);
}

/* document below */
vbi_capture *
vbi_capture_v4l2k_new		(const char *		dev_name,
				 int			fd,
				 int			buffers,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
{
	char *guess = NULL;
	char *error = NULL;
	vbi_capture_v4l2 *v;
	_vbi_log_hook log;

	pthread_once (&vbi_init_once, vbi_init);

	/* Needed to reopen device. */
	assert(dev_name != NULL);

	assert(buffers > 0);

	if (!errstr)
		errstr = &error;
	*errstr = NULL;

	CLEAR (log);
	if (trace) {
		log.fn = vbi_log_on_stderr;
		log.mask = VBI_LOG_INFO * 2 - 1;
	}

	if (!(v = calloc(1, sizeof(*v)))) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->log = log;

	_vbi3_raw_decoder_init (&v->rd, /* sampling_par */ NULL);

	if (trace) {
		vbi3_raw_decoder_set_log_fn (&v->rd,
					     vbi_log_on_stderr,
					     /* user_data */ NULL,
					     /* mask */ VBI_LOG_INFO * 2 - 1);
	}

	if (0)
		v->capture.sys_log_fp = stderr;

	info (&v->log,
	      "Try to open V4L2 2.6 VBI device, "
	      "libzvbi interface rev.\n  %s.",
	      rcsid);

	v->p_dev_name = strdup(dev_name);

	if (v->p_dev_name == NULL) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->capture.parameters = v4l2_parameters;
	v->capture._delete = v4l2_delete;
	v->capture.get_fd = v4l2_get_fd;
	v->capture.get_fd_flags = v4l2_get_fd_flags;
	v->capture.update_services = v4l2_update_services;
	v->capture.get_scanning = v4l2_get_scanning;
	v->capture.flush = v4l2_flush;

	if (-1 == fd) {
		v->fd = device_open (v->capture.sys_log_fp,
				     v->p_dev_name, O_RDWR, 0);
		if (-1 == v->fd) {
			asprintf(errstr, _("Cannot open '%s': %d, %s."),
				     v->p_dev_name, errno, strerror(errno));
			goto io_error;
		}

		v->close_me = TRUE;

		info (&v->log,
		      "Opened %s.",
		      v->p_dev_name);
	} else {
		v->fd = fd;
		v->close_me = FALSE;

		info (&v->log,
		      "Using v4l2k device fd %d.",
		      fd);
	}

	if (-1 == xioctl (v, VIDIOC_QUERYCAP, &v->vcap)) {
		asprintf(errstr, _("Cannot identify '%s': %d, %s."),
			     v->p_dev_name, errno, strerror(errno));
		guess = _("Probably not a v4l2 device.");
		goto io_error;
	}

	if (!(v->vcap.capabilities & V4L2_CAP_VBI_CAPTURE)) {
		asprintf(errstr, _("%s (%s) is not a raw vbi device."),
			     v->p_dev_name, v->vcap.card);
		goto failure;
	}

	info (&v->log,
	      "%s (%s) is a v4l2 vbi device,\n"
	      "driver %s, version 0x%08x.",
	      v->p_dev_name, v->vcap.card,
	      v->vcap.driver, v->vcap.version);

	if (0 == strcmp ((char *) v->vcap.driver, "bttv")) {
		if (v->vcap.version <= 0x00090F) {
			v->pal_start1_fix = TRUE;
			v->bttv_min_start_fix = TRUE;
		}
		v->bttv_offset_fix = TRUE;
		v->bttv_ntsc_rate_fix = TRUE;
	} else if (0 == strcmp ((char *) v->vcap.driver, "saa7134")) {
		if (v->vcap.version <= 0x00020C)
			v->saa7134_ntsc_fix = TRUE;
		v->pal_start1_fix = TRUE;
	} else if (0 == strcmp ((char *) v->vcap.driver, "cx8800")) {
		v->cx88_ntsc_fix = TRUE;
	}

	v->has_try_fmt = -1;
	v->buf_req_count = buffers;

	if (v->vcap.capabilities & V4L2_CAP_STREAMING
	    && !vbi_capture_force_read_mode) {
		info (&v->log, "Using streaming interface.");

		fcntl(v->fd, F_SETFL, O_NONBLOCK);

		v->streaming = TRUE;
		v->enqueue = ENQUEUE_SUSPENDED;

		v->capture.read = v4l2_stream;

	} else if (v->vcap.capabilities & V4L2_CAP_READWRITE) {
		info (&v->log, "Using read interface.");

		v->capture.read = v4l2_read;

		v->read_active = FALSE;

	} else {
		asprintf(errstr,
			     _("%s (%s) lacks a vbi read interface, "
			       "possibly an output only device "
			       "or a driver bug."),
			     v->p_dev_name, v->vcap.card);
		goto failure;
	}

        v->services = 0;

	if (services != NULL) {
                assert(*services != 0);
                v->services = v4l2_update_services(&v->capture, TRUE, TRUE,
                                                   *services, strict, errstr);
                if (v->services == 0)
                        goto failure;

                *services = v->services;
        }

	info (&v->log,
	      "Successfully opened %s (%s).",
	      v->p_dev_name, v->vcap.card);

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return &v->capture;

 io_error:
 failure:
	info ((NULL != v) ? &v->log : &log,
	      "Failed with errno %d, errmsg '%s'.",
	      errno, *errstr);

	if (NULL != v)
		v4l2_delete (&v->capture);

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return NULL;
}

#else

/**
 * @param dev_name Name of the device to open, usually one of
 *   @c /dev/vbi or @c /dev/vbi0 and up.
 * @param fd File handle of VBI device if already opened by caller,
 *   else value -1.
 * @param buffers Number of device buffers for raw vbi data, when
 *   the driver supports streaming. Otherwise one bounce buffer
 *   is allocated for vbi_capture_pull().
 * @param services This must point to a set of @ref VBI_SLICED_
 *   symbols describing the
 *   data services to be decoded. On return the services actually
 *   decodable will be stored here. See vbi_raw_decoder_add()
 *   for details. If you want to capture raw data only, set to
 *   @c VBI_SLICED_VBI_525, @c VBI_SLICED_VBI_625 or both.
 *   If this parameter is @c NULL, no services will be installed.
 *   You can do so later with vbi_capture_update_services(); note the
 *   reset parameter must be set to @c TRUE in this case.
 * @param strict Will be passed to vbi_raw_decoder_add().
 * @param errstr If not @c NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace If @c TRUE print progress messages on stderr.
 * 
 * @return
 * Initialized vbi_capture context, @c NULL on failure.
 */
vbi_capture *
vbi_capture_v4l2k_new(const char *dev_name, int fd, int buffers,
		      unsigned int *services, int strict,
		      char **errstr, vbi_bool trace)
{
	dev_name = dev_name;
	fd = fd;
	buffers = buffers;
	services = services;
	strict = strict;

	pthread_once (&vbi_init_once, vbi_init);

	if (trace)
		fprintf (stderr, "Libzvbi V4L2 2.6 interface rev.\n  %s\n",
			 rcsid);

	if (errstr)
		asprintf (errstr,
			  _("V4L2 driver interface not compiled."));

	return NULL;
}

#endif /* !ENABLE_V4L2 */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
