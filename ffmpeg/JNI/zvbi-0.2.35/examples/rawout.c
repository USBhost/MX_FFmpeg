/*
 *  libzvbi raw VBI output example
 *
 *  Copyright (C) 2006 Michael H. Schimek
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

/* $Id: rawout.c,v 1.8 2008/02/19 00:52:04 mschimek Exp $ */

/* This example shows how to convert VBI data in a DVB PES stream
   to raw VBI data.

   gcc -o rawout rawout.c `pkg-config zvbi-0.2 --cflags --libs`

   ./rawout <pes | mplayer - -rawvideo on:w=720:h=34:format=0x32595559 */

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libzvbi.h>

static vbi_dvb_demux *		dvb;
static uint8_t			pes_buffer[2048];
static vbi_sampling_par		sp;
static uint8_t *		image;
static unsigned int		image_size;
static unsigned int		pixel_mask;
static int64_t			last_pts;
static vbi_raw_decoder		rd;

static void
raw_test			(const vbi_sliced *	expect_sliced,
				 unsigned int		expect_n_lines)
{
	vbi_sliced sliced[50];
	unsigned int n_lines;
	unsigned int i;

	n_lines = vbi_raw_decode (&rd, image, sliced);
	assert (n_lines == expect_n_lines);

	for (i = 0; i < n_lines; ++i) {
		unsigned int payload;

		assert (sliced[i].id == expect_sliced[i].id);
		assert (sliced[i].line == expect_sliced[i].line);

		payload = (vbi_sliced_payload_bits (sliced[i].id) + 7) / 8;
		assert (0 == memcmp (sliced[i].data,
				     expect_sliced[i].data,
				     payload));
	}
}

static vbi_bool
convert				(vbi_dvb_demux *	dx,
				 void *			user_data,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 int64_t		pts)
{
	vbi_bool success;
	ssize_t actual;

	dx = dx; /* unused */
	user_data = user_data;

	pts &= ((int64_t) 1 << 33) - 1;

	/* Handle PTS wrap-around. */
	if (0 == last_pts) {
		last_pts = pts;
	} else if (pts < last_pts) {
		last_pts -= (int64_t) 1 << 33;
	}

	while (pts - last_pts > 90000 / 25 * 3 / 2) {
		/* No data for this frame. */

		success = vbi_raw_video_image (image, image_size, &sp,
					       0, 0, 0, pixel_mask, FALSE,
					       NULL, /* n_lines */ 0);
		assert (success);

		raw_test (NULL, 0);

		actual = write (STDOUT_FILENO, image, image_size);
		assert (actual == (ssize_t) image_size);

		last_pts += 90000 / 25;
	}

	success = vbi_raw_video_image (image, image_size, &sp,
				       /* blank_level: default */ 0,
				       /* black_level: default */ 0,
				       /* white_level: default */ 0,
				       pixel_mask,
				       /* swap_fields */ FALSE,
				       sliced, n_lines);
	assert (success);

	raw_test (sliced, n_lines);

	actual = write (STDOUT_FILENO, image, image_size);
	assert (actual == (ssize_t) image_size);

	last_pts = pts;

	return TRUE; /* success */
}

static void
mainloop			(void)
{
	while (1 == fread (pes_buffer, sizeof (pes_buffer), 1, stdin)) {
		vbi_bool success;

		success = vbi_dvb_demux_feed (dvb,
					      pes_buffer,
					      sizeof (pes_buffer));
		assert (success);
	}

	fprintf (stderr, "End of stream.\n");
}

int
main				(void)
{
	if (isatty (STDIN_FILENO)) {
		fprintf (stderr, "No DVB PES on standard input.\n");
		exit (EXIT_FAILURE);
	}

	if (isatty (STDOUT_FILENO)) {
		fprintf (stderr, "Output is binary image data. Pipe to "
			 "another tool or redirect to a file.\n");
		exit (EXIT_FAILURE);
	}

	/* Helps debugging. */
	vbi_set_log_fn ((VBI_LOG_NOTICE |
			 VBI_LOG_WARNING |
			 VBI_LOG_ERROR),
			vbi_log_on_stderr,
			/* user_data */ NULL);

	dvb = vbi_dvb_pes_demux_new (convert, /* user_data */ NULL);
	assert (NULL != dvb);

	memset (&sp, 0, sizeof (sp));

#if 1
	/* ITU BT.601 YUYV. */

	sp.scanning		= 625; /* PAL/SECAM */
	sp.sampling_format	= VBI_PIXFMT_YUYV;
	sp.sampling_rate	= 13.5e6;
	sp.bytes_per_line	= 720 * 2; /* 2 bpp */
	sp.offset		= 9.5e-6 * 13.5e6;
	sp.start[0]		= 6;
	sp.count[0]		= 17;
	sp.start[1]		= 319;
	sp.count[1]		= 17;
	sp.interlaced		= TRUE;
	sp.synchronous		= TRUE;

	/* Other bytes are left unmodified. */
	pixel_mask		= 0x000000FF; /* 0xAAVVUUYY */
#else
	/* PAL square pixels BGRA32. */

	sp.scanning		= 625; /* PAL/SECAM */
	sp.sampling_format	= VBI_PIXFMT_BGRA32_LE;
	sp.sampling_rate	= 14.75e6;
	sp.bytes_per_line	= 768 * 4; /* 4 bpp */
	sp.offset		= 10.2e-6 * 14.75e6;
	sp.start[0]		= 6;
	sp.count[0]		= 17;
	sp.start[1]		= 319;
	sp.count[1]		= 17;
	sp.interlaced		= TRUE;
	sp.synchronous		= TRUE;

	pixel_mask		= 0x0000FF00; /* 0xAABBGGRR */
#endif

	image_size = (sp.count[0] + sp.count[1]) * sp.bytes_per_line;
	image = malloc (image_size);
	assert (NULL != image);

	if (VBI_PIXFMT_YUYV == sp.sampling_format) {
		/* Reset Cb/Cr bytes. */
		memset (image, 0x80, image_size);
	} else {
		memset (image, 0x00, image_size);
	}

	/* To verify the generated raw VBI data we feed it back
	   into a decoder and compare the sliced VBI data. */

	vbi_raw_decoder_init (&rd);

	rd.scanning		= sp.scanning;
	rd.sampling_format	= sp.sampling_format;
	rd.sampling_rate	= sp.sampling_rate;
	rd.bytes_per_line	= sp.bytes_per_line;
	rd.offset		= sp.offset;
	rd.start[0]		= sp.start[0];
	rd.start[1]		= sp.start[1];
	rd.count[0]		= sp.count[0];
	rd.count[1]		= sp.count[1];
	rd.interlaced		= sp.interlaced;
	rd.synchronous		= sp.synchronous;

	/* Strict 0 because the function would consider the
	   square pixel timing too tight to reliably decode
	   Teletext. */
	vbi_raw_decoder_add_services (&rd,
				      (VBI_SLICED_TELETEXT_B |
				       VBI_SLICED_VPS |
				       VBI_SLICED_CAPTION_625),
				      /* strict */ 0);

	mainloop ();

	vbi_dvb_demux_delete (dvb);

	exit (EXIT_SUCCESS);

	return 0;
}
