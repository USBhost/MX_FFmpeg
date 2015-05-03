/*
 *  libzvbi -- Linux DVB driver interface
 *
 *  (c) 2004, 2005, 2007 Michael H. Schimek
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef ENABLE_DVB

#include <unistd.h>		/* read() */
#include <errno.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef HAVE_S64_U64
  /* Linux 2.6.x asm/types.h defines __s64 and __u64 only
     if __GNUC__ is defined. */
typedef int64_t __s64;
typedef uint64_t __u64;
#endif
#include "dvb/dmx.h"

#include "io.h"
#include "vbi.h"		/* vbi_init_once */
#include "dvb_demux.h"

typedef struct {
	vbi_capture		capture;

	int			fd;

	/* FIXME This may be too small? */
	uint8_t			pes_buffer[1024 * 8];
	const uint8_t *		bp;
	unsigned int		b_left;

	vbi_dvb_demux *		demux;

	vbi_capture_buffer	sliced_buffer;
	vbi_sliced		sliced_data[256];
	double			sample_time;
	int64_t			last_pts;

	vbi_bool		do_trace;
	vbi_bool		bug_compatible;
} vbi_capture_dvb;

#define printv(format, args...)						\
do {									\
	if (dvb->do_trace) {						\
		fprintf (stderr, "libzvbi: " format ,##args);		\
		fflush (stderr);					\
	}								\
} while (0)

static __inline__ void
timeval_subtract		(struct timeval *	delta,
				 const struct timeval *	tv1,
				 const struct timeval *	tv2)
{
	if (tv1->tv_usec < tv2->tv_usec) {
		delta->tv_sec = tv1->tv_sec - tv2->tv_sec - 1;
		delta->tv_usec = 1000000 + tv1->tv_usec - tv2->tv_usec;
	} else {
		delta->tv_sec = tv1->tv_sec - tv2->tv_sec;
		delta->tv_usec = tv1->tv_usec - tv2->tv_usec;
	}
}

static void
timeout_subtract_elapsed	(struct timeval *	result,
				 const struct timeval *	timeout,
				 const struct timeval *	now,
				 const struct timeval *	start)
{
	struct timeval elapsed;

	timeval_subtract (&elapsed, now, start);

	if ((elapsed.tv_sec | elapsed.tv_usec) > 0) {
		timeval_subtract (result, timeout, &elapsed);

		if ((result->tv_sec | result->tv_usec) < 0) {
			result->tv_sec = 0;
			result->tv_usec = 0;
		}
	} else {
		*result = *timeout;
	}
}

static ssize_t
select_read			(vbi_capture_dvb *	dvb,
				 struct timeval *	now,
				 const struct timeval *	start,
				 const struct timeval *	timeout)
{
	struct timeval tv;
	ssize_t actual;

 select_again:
	/* tv = timeout - (now - start). */
	timeout_subtract_elapsed (&tv, timeout, now, start);

	/* Shortcut: don't wait if timeout is zero or elapsed. */
	if ((tv.tv_sec | tv.tv_usec) > 0) {
		fd_set set;
		int r;

 select_again_with_timeout:
		FD_ZERO (&set);
		FD_SET (dvb->fd, &set);

		/* Note Linux select() may change tv. */
		r = select (dvb->fd + 1,
			    /* readable */ &set,
			    /* writeable */ NULL,
			    /* in exception */ NULL,
			    &tv);

		switch (r) {
		case -1: /* error */
			switch (errno) {
			case EINTR:
				gettimeofday (now, /* timezone */ NULL);
				goto select_again;

			default:
				return -1; /* error */
			}

		case 0: /* timeout */
			if (dvb->bug_compatible)
				return -1; /* error */

			return 0; /* timeout */

		default:
			break;
		}
	}

 read_again:
	/* Non-blocking. */
	actual = read (dvb->fd,
		       dvb->pes_buffer,
		       sizeof (dvb->pes_buffer));

	switch (actual) {
	case -1: /* error */
		switch (errno) {
		case EAGAIN:
			if (dvb->bug_compatible)
				return -1; /* error */

			if ((timeout->tv_sec | timeout->tv_usec) <= 0)
				return 0; /* timeout */

			gettimeofday (now, /* timezone */ NULL);
			timeout_subtract_elapsed (&tv, timeout, now, start);

			if ((tv.tv_sec | tv.tv_usec) <= 0)
				return 0; /* timeout */

			goto select_again_with_timeout;

		case EINTR:
			goto read_again;

		default:
			return -1; /* error */
		}

	case 0: /* EOF? */
		printv ("End of file\n");

		errno = 0;

		return -1; /* error */

	default:
		break;
	}

	return actual;
}

static int
dvb_read			(vbi_capture *		cap,
				 vbi_capture_buffer **	raw,
				 vbi_capture_buffer **	sliced,
				 const struct timeval *	timeout)
{
	vbi_capture_dvb *dvb = PARENT (cap, vbi_capture_dvb, capture);
	vbi_capture_buffer *sb;
	struct timeval start;
	struct timeval now;
	unsigned int n_lines;
	int64_t pts;

	if (!sliced || !(sb = *sliced)) {
		sb = &dvb->sliced_buffer;
		sb->data = dvb->sliced_data;
	}

	start.tv_sec = 0;
	start.tv_usec = 0;

	/* When timeout is zero elapsed time doesn't matter. */
	if ((timeout->tv_sec | timeout->tv_usec) > 0)
		gettimeofday (&start, /* timezone */ NULL);

	now = start;

	for (;;) {
		if (0 == dvb->b_left) {
			ssize_t actual;

			actual = select_read (dvb, &now, &start, timeout);
			if (actual <= 0)
				return actual;

			gettimeofday (&now, /* timezone */ NULL);

			/* XXX inaccurate. Should be the time when we
			   received the first byte of the first packet
			   containing data of the returned frame. Or so. */
			dvb->sample_time = now.tv_sec
				+ now.tv_usec * (1 / 1e6);

			dvb->bp = dvb->pes_buffer;
			dvb->b_left = actual;
		}

		/* Demultiplexer coroutine. Returns when one frame is complete
		   or the buffer is empty, advancing bp and b_left. Don't
		   change sb->data in flight. */
		/* XXX max sliced lines needs an API change. Currently this
		   value is determined by vbi_raw_decoder line count below,
		   256 / 2 because fields don't really apply here and in
		   practice even 32 should be enough. */
		n_lines = vbi_dvb_demux_cor (dvb->demux,
					     sb->data,
					     /* max sliced lines */ 128,
					     &pts,
					     &dvb->bp,
					     &dvb->b_left);
		if (n_lines > 0)
			break;

		if (dvb->bug_compatible) {
			/* Only one read(), timeout ignored. */
			return 0; /* timeout */
		} else {
			/* Read until EAGAIN, another error or the
			   timeout expires, in this order. */
		}
	}

	if (sliced) {
		sb->size = n_lines * sizeof (vbi_sliced);
		sb->timestamp = dvb->sample_time;

		/* XXX PTS needs an API change.
		   sb->sample_time = dvb->sample_time;
		   sb->stream_time = pts; (first sliced line) */
		dvb->last_pts = pts;

		*sliced = sb;
	}

	if (raw && *raw) {
		/* Not implemented yet. */
		sb = *raw;
		sb->size = 0;
	}

	return 1; /* success */
}

static vbi_raw_decoder *
dvb_parameters			(vbi_capture *		cap)
{
	/* This is kinda silly but we keep it for compatibility
	   with earlier versions of the library. */
	static vbi_raw_decoder raw = {
		.count = { 128, 128 }, /* see dvb_read() */
	};

	cap = cap; /* unused, no warning please */

	return &raw;
}

static unsigned int
dvb_update_services		(vbi_capture *		cap,
				 vbi_bool		reset,
				 vbi_bool		commit,
				 unsigned int		services,
				 int			strict,
				 char **		errstr)
{
	cap = cap; /* unused, no warning please */
	reset = reset;
	commit = commit;
	services = services;
	strict = strict;
	errstr = errstr;

	return (VBI_SLICED_TELETEXT_B |
		VBI_SLICED_VPS |
		VBI_SLICED_CAPTION_625 |
		VBI_SLICED_WSS_625);
}

static void
dvb_flush			(vbi_capture *		cap)
{
	vbi_capture_dvb *dvb = PARENT (cap, vbi_capture_dvb, capture);

	vbi_dvb_demux_reset (dvb->demux);

	dvb->bp = dvb->pes_buffer;
	dvb->b_left = 0;
}

static VBI_CAPTURE_FD_FLAGS
dvb_get_fd_flags		(vbi_capture *		cap)
{
	cap = cap; /* unused, no warning please */

	return (VBI_FD_HAS_SELECT |
		VBI_FD_IS_DEVICE);
}

static int
dvb_get_fd			(vbi_capture *		cap)
{
	vbi_capture_dvb *dvb = PARENT (cap, vbi_capture_dvb, capture);

	return dvb->fd;
}

int64_t
vbi_capture_dvb_last_pts	(const vbi_capture *	cap)
{
	const vbi_capture_dvb *dvb =
		CONST_PARENT (cap, vbi_capture_dvb, capture);

	return dvb->last_pts;
}

int
vbi_capture_dvb_filter		(vbi_capture *		cap,
				 int			pid)
{
	vbi_capture_dvb *dvb = PARENT (cap, vbi_capture_dvb, capture);
	struct dmx_pes_filter_params filter;

	CLEAR (filter);

	filter.pid	= pid;
	filter.input	= DMX_IN_FRONTEND;
	filter.output	= DMX_OUT_TAP;
	filter.pes_type	= DMX_PES_OTHER;
	filter.flags	= DMX_IMMEDIATE_START;

	if (-1 == ioctl (dvb->fd, DMX_SET_PES_FILTER, &filter))
		return -1;

	printv ("Capturing PES packets with PID %d\n", pid);

	return 0;
}

static void
dvb_delete			(vbi_capture *		cap)
{
	vbi_capture_dvb *dvb;

	if (NULL == cap)
		return;

	dvb = PARENT (cap, vbi_capture_dvb, capture);

	if (-1 != dvb->fd) {
		/* Error ignored. */
		device_close (dvb->capture.sys_log_fp, dvb->fd);
	}

	vbi_dvb_demux_delete (dvb->demux);

	/* Make unusable. */
	CLEAR (*dvb);

	free (dvb);
}

static vbi_bool
open_device			(vbi_capture_dvb *	dvb,
				 const char *		device_name,
				 char **		errstr)
{
	int saved_errno;
	struct stat st;

	if (-1 == stat (device_name, &st))
		goto io_error;

	if (!S_ISCHR (st.st_mode)) {
		asprintf (errstr, _("%s is not a device."),
			  device_name);
		saved_errno = 0;
		goto failed;
	}

	/* XXX Can we check if this is really a DVB demux device? */

	dvb->fd = device_open (dvb->capture.sys_log_fp,
			       device_name,
			       O_RDONLY | O_NONBLOCK,
			       /* mode */ 0);
	if (-1 == dvb->fd)
		goto io_error;

	return TRUE;

 io_error:
	saved_errno = errno;

	asprintf (errstr, _("Cannot open '%s': %s."),
		  device_name, strerror (saved_errno));

	/* fall through */	

 failed:
	dvb->fd = -1;

	errno = saved_errno;

	return FALSE;
}

vbi_capture *
vbi_capture_dvb_new2		(const char *		device_name,
				 unsigned int		pid,
				 char **		errstr,
				 vbi_bool		trace)
{
	char *error = NULL;
	int saved_errno;
	vbi_capture_dvb *dvb;

	pthread_once (&vbi_init_once, vbi_init);

	if (NULL == errstr)
		errstr = &error;
	*errstr = NULL;

	dvb = vbi_malloc (sizeof (*dvb));
	if (NULL == dvb)
		goto no_memory;

	CLEAR (*dvb);

	dvb->capture.read		= dvb_read;
	dvb->capture.sampling_point	= NULL;
	dvb->capture.debug		= NULL;
	dvb->capture.parameters		= dvb_parameters;
	dvb->capture.update_services	= dvb_update_services;
	dvb->capture.get_scanning	= NULL;
	dvb->capture.flush		= dvb_flush;
	dvb->capture.get_fd		= dvb_get_fd;
	dvb->capture.get_fd_flags	= dvb_get_fd_flags;
	dvb->capture.set_video_path	= NULL;
	dvb->capture._delete		= dvb_delete;

	dvb->fd = -1;

	dvb->do_trace = trace;

	dvb->demux = vbi_dvb_pes_demux_new (/* callback */ NULL,
					    /* user_data */ NULL);
	if (NULL == dvb->demux)
		goto no_memory;

	if (!open_device (dvb, device_name, errstr)) {
		saved_errno = errno;
		goto failed;
	}

	printv ("Opened device %s\n", device_name);

	if (0 != pid) {
		if (-1 == vbi_capture_dvb_filter (&dvb->capture, pid)) {
			saved_errno = errno;
			asprintf (errstr, _("DMX_SET_PES_FILTER failed: %s."),
				  strerror (errno));
			goto failed;
		}
	}

	dvb_flush (&dvb->capture);

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return &dvb->capture;

 no_memory:
	asprintf (errstr, _("Virtual memory exhausted."));

	saved_errno = ENOMEM;

	/* fall through */

 failed:
	if (NULL != dvb) {
		dvb_delete (&dvb->capture);
		dvb = NULL;
	}

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	errno = saved_errno;

	return NULL;
}

vbi_capture *
vbi_capture_dvb_new		(char *			dev,
				 int			scanning,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
{
	char *error = NULL;
	vbi_capture *cap;
	vbi_capture_dvb *dvb;

	scanning = scanning; /* unused, no warning please */
	services = services;
	strict = strict;

	if (NULL == errstr)
		errstr = &error;
	*errstr = NULL;

	cap = vbi_capture_dvb_new2 (dev, /* pid */ 0, errstr, trace);
	if (NULL == cap)
		goto failed;

	dvb = PARENT (cap, vbi_capture_dvb, capture);

	dvb->bug_compatible = TRUE;

 failed:
	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return cap;
}

#else /* !ENABLE_DVB */

#include "io.h"
#include "vbi.h"

/**
 * @param cap Initialized DVB vbi_capture context.
 *
 * Returns the presentation time stamp associated with the data
 * last read from the context. The PTS refers to the first sliced
 * VBI line, not the last packet containing data of that frame.
 *
 * Note timestamps returned by vbi_capture read functions contain
 * the system time (gettimeofday()) when the packet containing the
 * first sliced line was captured, not the PTS.
 *
 * @returns
 * Presentation time stamp (33 bits).
 *
 * @bug
 * The read functions should return the PTS along with
 * the capture timestamp.
 *
 * @since 0.2.13
 */
int64_t
vbi_capture_dvb_last_pts	(const vbi_capture *	cap)
{
	cap = cap; /* unused, no warning please */

	return 0;
}

/**
 * @param cap Initialized DVB vbi_capture context.
 * @param pid Filter out a stream with this PID.
 *
 * Programs the DVB device transport stream demultiplexer on the
 * DVB capture device to filter out packets with this PID.
 *
 * @returns
 * -1 on failure, 0 on success.
 */
int
vbi_capture_dvb_filter		(vbi_capture *		cap,
				 int			pid)
{
	cap = cap; /* unused, no warning please */
	pid = pid;

	return -1;
}

/**
 * @param device_name Name of the DVB device to open.
 * @param pid Filter out a stream with this PID. You can pass 0 here
 *   and set or change the PID later with vbi_capture_dvb_filter().
 * @param errstr If not @c NULL the function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace If @c TRUE print progress and warning messages on stderr.
 *
 * Initializes a vbi_capture context reading from a Linux DVB device.
 * 
 * @returns
 * Initialized vbi_capture context, @c NULL on failure.
 *
 * @since 0.2.13
 */
vbi_capture *
vbi_capture_dvb_new2		(const char *		device_name,
				 unsigned int		pid,
				 char **		errstr,
				 vbi_bool		trace)
{
	device_name = device_name; /* unused, no warning please */
	pid = pid;
	trace = trace;

	if (NULL != errstr)
		asprintf (errstr, _("DVB interface not compiled."));

	return NULL;
}

/**
 * @param dev Name of the DVB device to open.
 * @param scanning Ignored.
 * @param services Ignored.
 * @param strict Ignored.
 * @param errstr If not @c NULL the function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace If @c TRUE print progress and warning messages on stderr.
 * 
 * @deprecated
 * This function is deprecated. Use vbi_capture_dvb_new2() instead.
 *
 * Initializes a vbi_capture context reading from a Linux DVB device.
 * You must call vbi_capture_dvb_filter() before you can read.
 *
 * @returns
 * Initialized vbi_capture context, @c NULL on failure.
 *
 * @bug
 * This function ignores the scanning, services and strict parameter.
 * The read method of this DVB capture context returns -1 on timeout
 * instead of 0. It returns 0 when a single read() does not complete
 * a frame, regardless if the timeout expired. (Decoding resumes with
 * the next call.) Older versions pass select or read EINTR errors
 * back to the caller. They may return partial frames when VBI data
 * of one frame is distributed across multiple PES packets. They will
 * not return VPS, CC, or WSS data and can malfunction or segfault
 * given unusual PES streams. On error and select timeout older versions
 * invariably print a warning on stderr.
 */
vbi_capture *
vbi_capture_dvb_new		(char *			dev,
				 int			scanning,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
{
	dev = dev; /* unused, no warning please */
	scanning = scanning;
	services = services;
	strict = strict;
	trace = trace;

	if (NULL != errstr)
		asprintf (errstr, _("DVB interface not compiled."));

	return NULL;
}

#endif /* !ENABLE_DVB */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
