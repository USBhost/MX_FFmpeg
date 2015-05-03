/*
 *  libzvbi WSS capture example
 *
 *  Copyright (C) 2005 Michael H. Schimek
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $Id: wss.c,v 1.11 2008/02/19 00:52:04 mschimek Exp $ */

/* This example shows how to extract Wide Screen Signalling data
   (EN 300 294) from video images. The signal is transmitted on the
   first half of PAL/SECAM scan line 23, which ITU-R BT.601 defines
   as the first line of a 576 line picture.

   The author is not aware of any drivers which can capture a scan
   line as raw VBI and video data at the same time, and sliced VBI
   capturing is not supported yet by libzvbi. Note some drivers like
   the Linux saa7134 driver cannot capture line 23 at all.

   gcc -o wss wss.c `pkg-config zvbi-0.2 --cflags --libs` */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef ENABLE_V4L2

#include <fcntl.h>		/* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <libzvbi.h>

#include <asm/types.h>		/* for videodev2.h */
#include "videodev2k.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))

struct buffer {
	void *			start;
	size_t			length;
};

static const char *	dev_name = "/dev/video";

static int		fd;
static struct buffer *	buffers;
static unsigned int	n_buffers;

static int		quit;

static vbi_raw_decoder	rd;

static void
errno_exit			(const char *		s)
{
	fprintf (stderr, "%s error %d, %s\n",
		 s, errno, strerror (errno));

	exit (EXIT_FAILURE);
}

static int
xioctl				(int			fd,
				 int			request,
				 void *			p)
{
	int r;

	do r = ioctl (fd, request, p);
	while (-1 == r && EINTR == errno);

	return r;
}

static void
decode_wss_625			(uint8_t *		buf)
{
	static const char *formats [] = {
		"Full format 4:3, 576 lines",
		"Letterbox 14:9 centre, 504 lines",
		"Letterbox 14:9 top, 504 lines",
		"Letterbox 16:9 centre, 430 lines",
		"Letterbox 16:9 top, 430 lines",
		"Letterbox > 16:9 centre",
		"Full format 14:9 centre, 576 lines",
		"Anamorphic 16:9, 576 lines"
	};
	static const char *subtitles [] = {
		"none",
		"in active image area",
		"out of active image area",
		"<invalid>"
	};
	int g1;
	int parity;

	g1 = buf[0] & 15;

	parity = g1;
	parity ^= parity >> 2;
	parity ^= parity >> 1;
	g1 &= 7;

	printf ("WSS PAL: ");
	if (!(parity & 1))
		printf ("<parity error> ");
	printf ("%s; %s mode; %s colour coding; %s helper; "
		"reserved b7=%d; %s Teletext subtitles; "
		"open subtitles: %s; %s surround sound; "
		"copyright %s; copying %s\n",
		formats[g1],
		(buf[0] & 0x10) ? "film" : "camera",
		(buf[0] & 0x20) ? "MA/CP" : "standard",
		(buf[0] & 0x40) ? "modulated" : "no",
		!!(buf[0] & 0x80),
		(buf[1] & 0x01) ? "have" : "no",
		subtitles[(buf[1] >> 1) & 3],
		(buf[1] & 0x08) ? "have" : "no",
		(buf[1] & 0x10) ? "asserted" : "unknown",
		(buf[1] & 0x20) ? "restricted" : "not restricted");
}

static void
process_image			(const void *		p)
{
	vbi_sliced sliced[1];
	unsigned int n_lines;

	n_lines = vbi_raw_decode (&rd, (uint8_t *) p, sliced);
	if (0 /* test */) {
		/* Error ignored. */
		write (STDOUT_FILENO, p, rd.bytes_per_line);
	} else if (n_lines > 0) {
		assert (VBI_SLICED_WSS_625 == sliced[0].id);
		assert (1 == n_lines);
		decode_wss_625 (sliced[0].data);
	} else {
		fputc ('.', stdout);
		fflush (stdout);
	}
}

static void
init_decoder			(void)
{
	unsigned int services;

	vbi_raw_decoder_init (&rd);

	rd.scanning = 625;
	rd.sampling_format = VBI_PIXFMT_YUYV;

	/* Should be calculated from VIDIOC_CROPCAP information.
	   Common sampling rates are 14.75 MHz to get 768 PAL/SECAM
	   square pixels per line, and 13.5 MHz according to ITU-R Rec.
           BT.601 with 720 pixels/line. Note BT.601 overscans the line:
	   13.5e6 / 720 > 14.75e6 / 768. Don't be fooled by a driver
	   scaling 768 square pixels to 720. */
	rd.sampling_rate = 768 * 14.75e6 / 768;

	rd.bytes_per_line = 768 * 2;

	/* Should be calculated from VIDIOC_CROPCAP information. */
	rd.offset = 0;

	rd.start[0] = 23;
	rd.count[0] = 1;

	rd.start[1] = 0;
	rd.count[1] = 0;

	rd.interlaced = FALSE; /* just one line */
	rd.synchronous = TRUE;

	services = vbi_raw_decoder_add_services (&rd,
						 VBI_SLICED_WSS_625,
						 /* strict */ 2);
	if (0 == services) {
		fprintf (stderr, "Cannot decode WSS\n");
		exit (EXIT_FAILURE);
	}
}

static void
mainloop			(void)
{
	quit = 0;

	while (!quit) {
		struct v4l2_buffer buf;

		for (;;) {
			fd_set fds;
			struct timeval tv;
			int r;

			FD_ZERO (&fds);
			FD_SET (fd, &fds);

			tv.tv_sec = 2;
			tv.tv_usec = 0;

			r = select (fd + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) {
				if (EINTR == errno) {
					/* XXX should subtract the elapsed
					   time from timeout here. */
					continue;
				}

				errno_exit ("select");
			}

			if (0 == r) {
				fprintf (stderr, "select timeout\n");
				exit (EXIT_FAILURE);
			}

			break;
		}

		CLEAR (buf);

		buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory	= V4L2_MEMORY_MMAP;

		if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
			if (EAGAIN == errno)
				continue;

			errno_exit ("VIDIOC_DQBUF");
		}

		assert (buf.index < n_buffers);

		process_image (buffers[buf.index].start);

		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
			errno_exit ("VIDIOC_QBUF");
	}
}

static void
start_capturing			(void)
{
	unsigned int i;
	enum v4l2_buf_type type;

	for (i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;

		CLEAR (buf);

		buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory	= V4L2_MEMORY_MMAP;
	        buf.index	= i;

		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
			errno_exit ("VIDIOC_QBUF");
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
		errno_exit ("VIDIOC_STREAMON");
}

static void
init_device			(void)
{
	struct v4l2_capability cap;
	v4l2_std_id std_id;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;

	if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s is no V4L2 device\n",
				 dev_name);
			exit (EXIT_FAILURE);
		} else {
			errno_exit ("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf (stderr, "%s is no video capture device\n",
			 dev_name);
		exit (EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf (stderr, "%s does not support streaming I/O\n",
			 dev_name);
		exit (EXIT_FAILURE);
	}

	std_id = V4L2_STD_PAL;

	if (-1 == xioctl (fd, VIDIOC_S_STD, &std_id))
		errno_exit ("VIDIOC_S_STD");

	CLEAR (fmt);

	/* We need the top field without vertical scaling,
	   width must be at least 320 pixels. */

	fmt.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width	= 768; 
	fmt.fmt.pix.height	= 576;
	fmt.fmt.pix.pixelformat	= V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field	= V4L2_FIELD_INTERLACED;

	if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
		errno_exit ("VIDIOC_S_FMT");

	/* XXX the driver may adjust width and height, some
	   even change the pixelformat, that should be checked here. */

	CLEAR (req);

	req.count		= 4;
	req.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory		= V4L2_MEMORY_MMAP;

	if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s does not support "
				 "memory mapping\n", dev_name);
			exit (EXIT_FAILURE);
		} else {
			errno_exit ("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf (stderr, "Insufficient buffer memory on %s\n",
			 dev_name);
		exit (EXIT_FAILURE);
	}

	buffers = calloc (req.count, sizeof (*buffers));

	if (!buffers) {
		fprintf (stderr, "Out of memory\n");
		exit (EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR (buf);

		buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory	= V4L2_MEMORY_MMAP;
	        buf.index	= n_buffers;

		if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
			errno_exit ("VIDIOC_QUERYBUF");

	        buffers[n_buffers].length = buf.length;
	        buffers[n_buffers].start =
			mmap (NULL /* start anywhere */,
			      buf.length,
			      PROT_READ | PROT_WRITE /* required */,
			      MAP_SHARED /* recommended */,
			      fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start)
			errno_exit ("mmap");
	}
}

static void
open_device			(void)
{
	struct stat st;	

	if (-1 == stat (dev_name, &st)) {
		fprintf (stderr, "Cannot identify '%s': %d, %s\n",
		         dev_name, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}

	if (!S_ISCHR (st.st_mode)) {
		fprintf (stderr, "%s is no device\n", dev_name);
		exit (EXIT_FAILURE);
	}

	fd = open (dev_name, O_RDWR | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf (stderr, "Cannot open '%s': %d, %s\n",
		         dev_name, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}
}

int
main				(void)
{
	/* Helps debugging. */
	vbi_set_log_fn (/* mask: log everything */ -1,
			vbi_log_on_stderr,
			/* user_data */ NULL);

	open_device ();

	init_device ();

	init_decoder ();

	start_capturing ();

	mainloop ();

	exit (EXIT_SUCCESS);

	return 0;
}

#else /* !ENABLE_V4L2 */

int
main				(int			argc,
				 char **		argv)
{
	fprintf (stderr, "Sorry, V4L2 only. Patches welcome.\n");

	exit (EXIT_FAILURE);
	
	return 0;
}

#endif /* !ENABLE_V4L2 */
