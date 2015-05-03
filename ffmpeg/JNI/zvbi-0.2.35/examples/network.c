/*
 *  libzvbi network identification example
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

/* $Id: network.c,v 1.7 2009/03/23 01:30:22 mschimek Exp $ */

/* This example shows how to identify a network from data transmitted
   in XDS packets, Teletext packet 8/30 format 1 and 2, and VPS packets.

   gcc -o network network.c `pkg-config zvbi-0.2 --cflags --libs` */

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <errno.h>

#include <libzvbi.h>

static vbi_capture *		cap;
static vbi_decoder *		dec;
static vbi_bool			quit;

static unsigned int		services;

static void
handler				(vbi_event *		ev,
				 void *			user_data)
{
	const char *event_name;
	const char *network_name;
	const char *call_sign;
	char *locale_network_name;

	user_data = user_data; /* unused */

	/* VBI_EVENT_NETWORK_ID is always sent when the decoder
	   receives a CNI. VBI_EVENT_NETWORK only if it can
	   determine a network name. */

	switch (ev->type) {
	case VBI_EVENT_NETWORK:
		event_name = "VBI_EVENT_NETWORK";
		quit = TRUE;
		break;

	case VBI_EVENT_NETWORK_ID:
		event_name = "VBI_EVENT_NETWORK_ID";
		break;

	default:
		assert (0);
	}

	network_name = "unknown";
	if (0 != ev->ev.network.name[0])
		network_name = (const char *) ev->ev.network.name;

	/* The network name is an ISO-8859-1 string (the API is
	   quite old...) so we convert it to locale encoding,
	   nowadays usually UTF-8. */
	locale_network_name = vbi_strndup_iconv (vbi_locale_codeset (),
						 "ISO-8859-1",
						 network_name,
						 strlen (network_name),
						 /* repl_char */ '?');

	/* ASCII. */
	call_sign = "unknown";
	if (0 != ev->ev.network.call[0])
		call_sign = (const char *) ev->ev.network.call;

	printf ("%s: receiving: \"%s\" call sign: \"%s\" "
	        "CNI VPS: 0x%x 8/30/1: 0x%x 8/30/2: 0x%x\n",
		event_name,
		(NULL == locale_network_name) ?
		"iconv-error" : locale_network_name,
		call_sign,
		ev->ev.network.cni_vps,
		ev->ev.network.cni_8301,
		ev->ev.network.cni_8302);

	free (locale_network_name);
}

static void
mainloop			(void)
{
	struct timeval timeout;
	vbi_capture_buffer *sliced_buffer;
	unsigned int n_frames;

	/* Don't wait more than two seconds for the driver
	   to return data. */
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;

	/* Should receive a CNI within two seconds,
	   call sign within ten seconds(?). */
	if (services & VBI_SLICED_CAPTION_525)
		n_frames = 11 * 30;
	else
		n_frames = 3 * 25;

	for (; n_frames > 0; --n_frames) {
		unsigned int n_lines;
		int r;

		r = vbi_capture_pull (cap,
				      /* raw_buffer */ NULL,
				      &sliced_buffer,
				      &timeout);
		switch (r) {
		case -1:
			fprintf (stderr, "VBI read error %d (%s)\n",
				 errno, strerror (errno));
			/* Could be ignored, esp. EIO with some drivers. */
			exit (EXIT_FAILURE);

		case 0: 
			fprintf (stderr, "VBI read timeout\n");
			exit (EXIT_FAILURE);

		case 1: /* success */
			break;

		default:
			assert (0);
		}

		n_lines = sliced_buffer->size / sizeof (vbi_sliced);

		vbi_decode (dec,
			    (vbi_sliced *) sliced_buffer->data,
			    n_lines,
			    sliced_buffer->timestamp);

		if (quit)
			return;
	}

	printf ("No network ID received or network unknown.\n");
}

int
main				(void)
{
	char *errstr;
	vbi_bool success;

	setlocale (LC_ALL, "");

	services = (VBI_SLICED_TELETEXT_B |
		    VBI_SLICED_VPS |
		    VBI_SLICED_CAPTION_525);

	cap = vbi_capture_v4l2_new ("/dev/vbi",
				    /* buffers */ 5,
				    &services,
				    /* strict */ 0,
				    &errstr,
				    /* verbose */ FALSE);
	if (NULL == cap) {
		fprintf (stderr,
			 "Cannot capture VBI data with V4L2 interface:\n"
			 "%s\n",
			 errstr);

		free (errstr);

		exit (EXIT_FAILURE);
	}

	dec = vbi_decoder_new ();
	assert (NULL != dec);

	success = vbi_event_handler_add (dec,
					 (VBI_EVENT_NETWORK |
					  VBI_EVENT_NETWORK_ID),
					 handler,
					 /* user_data */ NULL);
	assert (success);

	mainloop ();

	vbi_decoder_delete (dec);

	vbi_capture_delete (cap);

	exit (EXIT_SUCCESS);
}
