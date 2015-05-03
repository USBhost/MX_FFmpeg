/*
 *  libzvbi VPS/PDC example 1
 *
 *  Copyright (C) 2009 Michael H. Schimek
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

/* $Id: pdc1.c,v 1.2 2009/03/23 01:30:42 mschimek Exp $ */

/* This example shows how to receive Program IDs transmitted in VPS
   and Teletext packets using the libzvbi VBI decoder. Example pdc2.c
   demonstrates how video recorders respond to Program IDs.

   If you prefer to decode the VPS and Teletext packets directly see
   test/decode.c for an example.

   gcc -o pdc1 pdc1.c `pkg-config zvbi-0.2 --cflags --libs` */

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

/* Attention! This function returns a static string. */
static const char *
pil_str				(vbi_pil		pil)
{
	static char buffer[32];

	switch (pil) {
	case VBI_PIL_TIMER_CONTROL:	return "TC";
	case VBI_PIL_INHIBIT_TERMINATE:	return "RI/T";
	case VBI_PIL_INTERRUPTION:	return "INT";
	case VBI_PIL_CONTINUE:		return "CONT";
	case VBI_PIL_NSPV:		return "NSPV/END";

	default:
		snprintf (buffer, sizeof (buffer),
			  "%02u%02uT%02u%02u",
			  VBI_PIL_MONTH (pil),
			  VBI_PIL_DAY (pil),
			  VBI_PIL_HOUR (pil),
			  VBI_PIL_MINUTE (pil));
		return buffer;
	}
}

static void
event_handler			(vbi_event *		ev,
				 void *			user_data)
{
	const vbi_program_id *pid;

	user_data = user_data; /* unused, no warning please */

	pid = ev->ev.prog_id;

	printf ("Received PIL %s/%02X on LC %u.\n",
		pil_str (pid->pil), pid->pty, pid->channel);
}

static void
mainloop			(void)
{
	struct timeval timeout;

	/* Don't wait more than two seconds for the driver
	   to return data. */
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;

	for (;;) {
		vbi_capture_buffer *sliced_buffer;
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
	}
}

int
main				(void)
{
	char *errstr;
	vbi_bool success;
	unsigned int services;

	setlocale (LC_ALL, "");

	services = (VBI_SLICED_TELETEXT_B |
		    VBI_SLICED_VPS);

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
					 VBI_EVENT_PROG_ID,
					 event_handler,
					 /* user_data */ NULL);
	assert (success);

	mainloop ();

	vbi_decoder_delete (dec);

	vbi_capture_delete (cap);

	exit (EXIT_SUCCESS);
}
