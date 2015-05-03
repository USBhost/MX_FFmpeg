/*
 *  libzvbi test
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
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
 */

/* $Id: capture.c,v 1.39 2008/08/19 10:06:52 mschimek Exp $ */

/* For libzvbi version 0.2.x / 0.3.x. */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

#include "src/version.h"
#if 2 == VBI_VERSION_MINOR
#  include "src/hamm.h"
#  define SCANNING(sp) ((sp)->scanning)
#elif 3 == VBI_VERSION_MINOR
#  include "src/vbi.h"
#  include "src/zvbi.h"
#  include "src/misc.h"
#  define SCANNING(sp)							\
	(0 != ((sp)->videostd_set & VBI_VIDEOSTD_SET_525_60) ? 525 : 625)
#else
#  error VBI_VERSION_MINOR == ?
#endif

#include "sliced.h"

#undef _
#define _(x) x /* TODO */

#define PROGRAM_NAME "zvbi-capture"

static const char *		option_out_file_name;
static enum file_format		option_out_file_format;
static unsigned int		option_out_ts_pid;

static int			option_read_not_pull;
static int			option_strict;
static int			option_dump_wss;
static int			option_dump_sliced;
static int			option_cc_test;
static int			option_cc_test_test;
static vbi_bool		option_raw_output;
static vbi_bool		option_sliced_output;
static unsigned int		option_sim_flags;

static struct stream *		cst;
static struct stream *		wst;
static const char **		sim_cc_streams;
static unsigned int		n_sim_cc_streams;

struct frame {
	vbi_sliced		sliced[50];
	unsigned int		n_lines;

	uint8_t *		raw;

	double			sample_time;
	int64_t			stream_time;
};

static struct frame		frame_buffers[5];
static unsigned int		next_frame;
static unsigned int		n_frames_buffered;
static unsigned int		sliced_output_count;
static unsigned int		raw_output_count;

/*
 *  Dump
 */

extern int vbi_printable (int);

static void
decode_wss_625			(const uint8_t *	buf)
{
	static const char *formats[] = {
		"Full format 4:3, 576 lines",
		"Letterbox 14:9 centre, 504 lines",
		"Letterbox 14:9 top, 504 lines",
		"Letterbox 16:9 centre, 430 lines",
		"Letterbox 16:9 top, 430 lines",
		"Letterbox > 16:9 centre",
		"Full format 14:9 centre, 576 lines",
		"Anamorphic 16:9, 576 lines"
	};
	static const char *subtitles[] = {
		"none",
		"in active image area",
		"out of active image area",
		"?"
	};
	int g1 = buf[0] & 15;
	int parity;

	if (option_dump_wss) {
		parity = g1;
		parity ^= parity >> 2;
		parity ^= parity >> 1;
		g1 &= 7;

		printf ("WSS PAL: ");
		if (!(parity & 1))
			printf ("<parity error> ");
		printf ("%s; %s mode; %s colour coding;\n"
			"  %s helper; reserved b7=%d; %s\n"
			"  open subtitles: %s; %scopyright %s; copying %s\n",
			formats[g1],
			(buf[0] & 0x10) ? "film" : "camera",
			(buf[0] & 0x20) ? "MA/CP" : "standard",
			(buf[0] & 0x40) ? "modulated" : "no",
			!!(buf[0] & 0x80),
			(buf[1] & 0x01) ? "have TTX subtitles; " : "",
			subtitles[(buf[1] >> 1) & 3],
			(buf[1] & 0x08) ? "surround sound; " : "",
			(buf[1] & 0x10) ? "asserted" : "unknown",
			(buf[1] & 0x20) ? "restricted" : "not restricted");
	}
}

static void
decode_wss_cpr1204		(const uint8_t *	buf)
{
	if (option_dump_wss) {
		const int poly = (1 << 6) + (1 << 1) + 1;
		int g = (buf[0] << 12) + (buf[1] << 4) + buf[2];
		int j, crc;

		crc = g | (((1 << 6) - 1) << (14 + 6));

		for (j = 14 + 6 - 1; j >= 0; j--) {
			if (crc & ((1 << 6) << j))
				crc ^= poly << j;
		}

		fprintf (stderr, "WSS CPR >> g=%08x crc=%08x\n", g, crc);
	}
}

static void
decode_sliced			(const vbi_sliced *	s,
				 unsigned int		n_lines,
				 double			sample_time,
				 int64_t		stream_time)
{
	if (option_dump_sliced) {
		const vbi_sliced *q = s;
		unsigned int i;

		printf ("Frame %f %010" PRId64 "\n",
			sample_time, stream_time);

		for (i = 0; i < n_lines; q++, i++) {
			unsigned int j;

			printf ("%08x %3d  ", q->id, q->line);

			for (j = 0; j < sizeof(q->data); j++) {
				printf ("%02x ", 0xFF & q->data[j]);
			}

			putchar (' ');

			for (j = 0; j < sizeof(q->data); j++) {
				char c = _vbi_to_ascii (q->data[j]);
				putchar (c);
			}

			putchar ('\n');
		}
	}

	for (; n_lines > 0; s++, n_lines--) {
		if (s->id == 0) {
			continue;
		} else if (s->id & (VBI_SLICED_VPS |
				    VBI_SLICED_TELETEXT_B |
				    VBI_SLICED_CAPTION_525 |
				    VBI_SLICED_CAPTION_625)) {
			/* Nothing to do.
			   Use the 'decode' tool instead. */
		} else if (s->id & VBI_SLICED_WSS_625) {
			decode_wss_625 (s->data);
		} else if (s->id & VBI_SLICED_WSS_CPR1204) {
			decode_wss_cpr1204 (s->data);
		} else {
			fprintf (stderr, "Oops. Unhandled VBI service %08x\n",
				 s->id);
		}
	}
}

/* Purpose of this function is to record raw and sliced VBI data
   around three kinds of events for remote examination:
   - Frames without data,
   - Lines which contain 0x00 0x00 instead of 0x80 0x80 bytes,
   - Lines where the transmitted bytes have wrong parity.
   The raw VBI data may be required to understand what happened,
   and due to its volume we cannot record it unconditionally. */
static vbi_bool
cc_test				(const vbi_sliced *	sliced,
				 unsigned int		n_lines)
{
	static const unsigned int max_error_count[3] = { 5, 5, 5 };
	static unsigned int error_count[3];
	static unsigned int frame_count;
	unsigned int error_set;
	unsigned int i;

	error_set = 0;

	if (option_cc_test_test && 0 == rand() % 300)
		n_lines = 0;

	if (0 == n_lines) {
		error_msg ("No data on this frame...");
		if (error_count[0] < max_error_count[0]) {
			++error_count[0];
			error_set |= 1 << 0;
		}
	} else {
		for (i = 0; i < n_lines; ++i) {
			if (sliced[i].id & (VBI_SLICED_CAPTION_525 |
					    VBI_SLICED_CAPTION_625)) {
				int b1, b2;
				int c1, c2;

				b1 = sliced[i].data[0];
				b2 = sliced[i].data[1];

				if (option_cc_test_test
				    && 0 == rand() % 300) {
					b1 = 0;
					b2 = 0;
				}

				if (0x00 == b1 && 0x00 == b2) {
					error_msg ("Null bytes...");
					if (error_count[1]
					    < max_error_count[1]) {
						++error_count[1];
						error_set |= 1 << 1;
					}
				}

				c1 = vbi_unpar8 (b1);
				c2 = vbi_unpar8 (b2);

				if (option_cc_test_test
				    && 0 == rand() % 300) {
					c2 = -1;
				}

				if ((c1 | c2) < 0) {
					error_msg ("Parity error...");
					if (error_count[2]
					    < max_error_count[2]) {
						++error_count[2];
						error_set |= 1 << 2;
					}
					break;
				}
			}
		}
	}

	if (0 != error_set) {
		unsigned int n_errors = 0;
		unsigned int n_kinds = 0;

		for (i = 0; i < N_ELEMENTS (error_count); ++i) {
			n_errors += 5 - error_count[i];
			n_kinds += (error_count[i] < 5);
		}

		if (0 == n_kinds) {
			error_msg ("Done.");
			return FALSE; /* terminate loop */
		}

		if (0 == frame_count % (5 * 30)) {
			error_msg ("Waiting for %u errors of %u kinds...",
				   n_errors, n_kinds);
		}
	}

	++frame_count;
 
	if (option_raw_output != (0 != error_set)) {
		option_sliced_output = (0 != error_set);

		raw_output_count = N_ELEMENTS (frame_buffers) * 2;

		if (sliced_output_count < raw_output_count)
			sliced_output_count = raw_output_count;
	}

	return TRUE; /* success, continue loop */
}

static vbi_bool
decode_frame			(const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	if (option_dump_sliced || option_dump_wss)
		decode_sliced (sliced, n_lines,
			       sample_time, stream_time);

	if (option_cc_test) {
		struct frame *f;
		unsigned int size;

		f = frame_buffers + next_frame;

		if (n_frames_buffered >= N_ELEMENTS (frame_buffers)) {
			write_stream_sliced (wst,
					     (sliced_output_count > 0) ?
					     f->sliced : NULL,
					     f->n_lines,
					     (raw_output_count > 0) ?
					     f->raw : NULL,
					     sp,
					     f->sample_time,
					     f->stream_time);

			if (sliced_output_count > 0)
				--sliced_output_count;
			if (raw_output_count > 0)
				--raw_output_count;

			n_frames_buffered = N_ELEMENTS (frame_buffers) - 1;
		}

		assert (n_lines <= N_ELEMENTS (f->sliced));

		memcpy (f->sliced, sliced, n_lines * sizeof (*sliced));
		f->n_lines = n_lines;

		size = (sp->count[0] + sp->count[1]) * sp->bytes_per_line;
		memcpy (f->raw, raw, size);

		f->sample_time = sample_time;
		f->stream_time = stream_time;

		if (++next_frame >= N_ELEMENTS (frame_buffers))
			next_frame = 0;

		++n_frames_buffered;	

		if (!cc_test (sliced, n_lines))
			return FALSE;

	} else if (option_raw_output || option_sliced_output) {
		write_stream_sliced (wst,
				     option_sliced_output ? sliced : NULL,
				     n_lines,
				     option_raw_output ? raw : NULL,
				     sp,
				     sample_time,
				     stream_time);
	}

	return TRUE;
}

static void
init_frame_buffers		(const vbi_sampling_par *sp)
{
	unsigned int size;
	unsigned int i;

	size = (sp->count[0] + sp->count[1]) * sp->bytes_per_line;

	for (i = 0; i < N_ELEMENTS (frame_buffers); ++i) {
		frame_buffers[i].raw = malloc (size);
		if (NULL == frame_buffers[i].raw)
			no_mem_exit ();
	}
}

static void
usage				(FILE *			fp)
{
	fprintf (fp, _("\
%s %s -- VBI capture tool\n\n\
Copyright (C) 2000-2007 Michael H. Schimek\n\
This program is licensed under GPLv2 or later. NO WARRANTIES.\n\n\
Usage: %s [options] > sliced VBI data\n\
-h | --help | --usage  Print this message and exit\n\
-q | --quiet           Suppress progress and error messages\n\
-v | --verbose         Increase verbosity\n\
-V | --version         Print the program version and exit\n\
Device options:\n\
-c | --sim-cc file     Simulate a VBI device and load this Closed Caption\n\
                       test stream into the simulation\n\
-d | --device file     Capture from this device (default %s)\n\
                       V4L/V4L2: /dev/vbi, /dev/vbi0, /dev/vbi1, ...\n\
                       Linux DVB: /dev/dvb/adapter0/demux0, ...\n\
		       *BSD bktr driver: /dev/vbi, /dev/vbi0, ...\n\
-i | --pid pid         Capture the stream with this PID from a Linux\n\
                       DVB device\n\
-m | --sim-laced       Simulate a VBI device capturing interlaced raw\n\
                       VBI data\n\
-n | --ntsc            Video standard hint for V4L interface and\n\
                       simulated VBI device (default PAL/SECAM)\n\
-p | --pal | --secam   Video standard hint for V4L interface\n\
-s | --sim             Simulate a VBI device\n\
-u | --sim-unsync      Simulate a VBI device with wrong/unknown field\n\
                       parity\n\
-w | --sim-noise       Simulate a VBI device with noisy signal\n\
-x | --proxy           Capture through the VBI proxy daemon\n\
Output options:\n\
-j | --dump            Sliced VBI data (text)\n\
-l | --sliced          Sliced VBI data (binary)\n\
-o | --output name     Write the VBI data to this file instead of\n\
                       standard output\n"
/* later */ /*
"-r | --raw             Raw VBI data (binary)\n"
*/
"-P | --pes             DVB PES stream\n\
-T | --ts pid          DVB TS stream\n\
"),
		 PROGRAM_NAME, VERSION, program_invocation_name,
		 option_dev_name);
}

static const char short_options[] = "c:d:hi:jlmno:pqr:suvwxPT:V";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options[] = {
	{ "sim-cc",	required_argument,	NULL,		'c' },
	{ "device",	required_argument,	NULL,		'd' },
	{ "help",	no_argument,		NULL,		'h' },
	{ "usage",	no_argument,		NULL,		'h' },
	{ "pid",	required_argument,	NULL,		'i' },
	{ "dump",	no_argument,		NULL,		'j' },
	{ "sliced",	no_argument,		NULL,		'l' },
	{ "sim-laced",	no_argument,		NULL,		'm' },
	{ "ntsc",	no_argument,		NULL,		'n' },
	{ "output",	required_argument,	NULL,		'o' },
	{ "pal",	no_argument,		NULL,		'p' },
	{ "secam",	no_argument,		NULL,		'p' },
	{ "quiet",	no_argument,		NULL,		'q' },
	{ "raw",	optional_argument,	NULL,		'r' },
	{ "sim",	no_argument,		NULL,		's' },
	{ "sim-unsync",	no_argument,		NULL,		'u' },
	{ "verbose",	no_argument,		NULL,		'v' },
	{ "sim-noise",  optional_argument,	NULL,		'w' },
	{ "proxy",	no_argument,		NULL,		'x' },
	{ "pes",	no_argument,		NULL,		'P' },
	{ "ts",		required_argument,	NULL,		'T' },
	{ "version",	no_argument,		NULL,		'V' },
	{ "loose",	no_argument,	&option_strict,		0 },
	{ "strict",	no_argument,	&option_strict,		2 },
	{ "cc-test",	no_argument,	&option_cc_test,	TRUE },
	{ "cc-test-test", no_argument,	&option_cc_test_test,	TRUE },
	{ "dump-wss",	no_argument,	&option_dump_wss,	TRUE },
	{ "read",	no_argument,	&option_read_not_pull,	TRUE },
	{ "pull",	no_argument,	&option_read_not_pull,	FALSE },
	{ NULL, 0, 0, 0 }
};
#else
#define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

static int			option_index;

static char *
load_string			(const char *		name)
{
	FILE *fp;
	char *buffer;
	size_t buffer_size;
	size_t done;

	buffer = NULL;
	buffer_size = 0;
	done = 0;

	fp = fopen (name, "r");
	if (NULL == fp) {
		exit (EXIT_FAILURE);
	}

	for (;;) {
		char *new_buffer;
		size_t new_size;
		size_t space;
		size_t actual;

		new_size = 16384;
		if (buffer_size > 0)
			new_size = buffer_size * 2;

		new_buffer = realloc (buffer, new_size);
		if (NULL == new_buffer) {
			free (buffer);
			exit (EXIT_FAILURE);
		}

		buffer = new_buffer;
		buffer_size = new_size;

		space = buffer_size - done - 1;
		actual = fread (buffer + done, 1, space, fp);
		if ((size_t) -1 == actual) {
			exit (EXIT_FAILURE);
		}
		done += actual;
		if (actual < space)
			break;
	}

	buffer[done] = 0;

	fclose (fp);

	return buffer;
}

static void
sim_load_caption		(void)
{
	unsigned int i;

	for (i = 0; i < n_sim_cc_streams; ++i) {
		char *buffer;
		vbi_bool success;

		fprintf (stderr, "Loading '%s'.\n", sim_cc_streams[i]);

		buffer = load_string (sim_cc_streams[i]);
		success = capture_stream_sim_load_caption
			(cst, buffer, /* append */ (i > 0));
		assert (success);
	}
}

int
main				(int			argc,
				 char **		argv)
{
	unsigned int interfaces;
	unsigned int scanning;
	unsigned int services;
	vbi_bool sim_interlaced;
	vbi_bool sim_synchronous;

	init_helpers (argc, argv);

	scanning = 625;

	sim_interlaced = FALSE;
	sim_synchronous = TRUE;

	option_strict = 1;
	option_dump_wss = FALSE;
	option_read_not_pull = FALSE;

	interfaces = (INTERFACE_V4L2 |
		      INTERFACE_V4L |
		      INTERFACE_BKTR);

	for (;;) {
		int c;

		c = getopt_long (argc, argv, short_options,
				 long_options, &option_index);
		if (-1 == c)
			break;

		switch (c) {
		case 0: /* getopt_long() flag */
			if (option_dump_wss) {
				option_raw_output = FALSE;
				option_sliced_output = FALSE;
			}
			break;

		case 'c':
		{
			const char **pp;

			pp = realloc (sim_cc_streams,
				      (n_sim_cc_streams + 1)
				      * sizeof (*pp));
			assert (NULL != pp);
			sim_cc_streams = pp;
			sim_cc_streams[n_sim_cc_streams++] = optarg;
			interfaces = INTERFACE_SIM;
			break;
		}

		case 'd':
			parse_option_dev_name ();
			break;

		case 'h':
			usage (stdout);
			exit (EXIT_SUCCESS);

		case 'i':
			parse_option_dvb_pid ();
			interfaces = INTERFACE_DVB;
			break;

		case 'j':
			option_dump_sliced = TRUE;
			option_raw_output = FALSE;
			option_sliced_output = FALSE;
			break;

		case 'l':
			option_sliced_output = TRUE;
			option_dump_sliced = FALSE;
			option_dump_wss = FALSE;
			if (FILE_FORMAT_XML != option_out_file_format)
				option_out_file_format = FILE_FORMAT_SLICED;
			break;

		case 'm':
			sim_interlaced = TRUE;
			interfaces = INTERFACE_SIM;
			break;

		case 'n':
			scanning = 525;
			break;

		case 'o':
			assert (NULL != optarg);
			option_out_file_name = optarg;
			break;

		case 'p':
			scanning = 625;
			break;

		case 'q':
			parse_option_quiet ();
			break;

#if 0
		case 'r':
			/* Optional optarg: line numbers
			   (not implemented yet). */
			option_raw_output = TRUE;
			option_dump_sliced = FALSE;
			option_dump_wss = FALSE;
			option_out_file_format = FILE_FORMAT_XML;
			break;
#endif
		case 's':
			interfaces = INTERFACE_SIM;
			break;

		case 'u':
			sim_synchronous = FALSE;
			interfaces = INTERFACE_SIM;
			break;

		case 'v':
			parse_option_verbose ();
			break;

		case 'w':
			/* Optional optarg: noise parameters
			   (not implemented yet). */
			interfaces = INTERFACE_SIM;
			option_sim_flags |= _VBI_RAW_NOISE_2;
			break;

		case 'x':
			interfaces &= ~(INTERFACE_SIM | INTERFACE_DVB);
			interfaces |= INTERFACE_PROXY;
			break;

		case 'P':
			option_sliced_output = TRUE;
			option_dump_sliced = FALSE;
			option_dump_wss = FALSE;
			option_out_file_format = FILE_FORMAT_DVB_PES;
			break;

		case 'T':
			option_sliced_output = TRUE;
			option_dump_sliced = FALSE;
			option_dump_wss = FALSE;
			option_out_ts_pid = parse_option_ts ();
			option_out_file_format = FILE_FORMAT_DVB_TS;
			break;

		case 'V':
			printf (PROGRAM_NAME " " VERSION "\n");
			exit (EXIT_SUCCESS);

		default:
			usage (stderr);
			exit (EXIT_FAILURE);
		}
	}

	if (!(option_sliced_output
	      || option_raw_output
	      || option_dump_sliced
	      || option_dump_wss)) {
		error_msg (_("Give one of the -j, -l, -P or -T options\n"
			     "to enable output, or -h for help."));
		exit (EXIT_FAILURE);
	}

	services = (VBI_SLICED_VBI_525 |
		    VBI_SLICED_VBI_625 |
		    VBI_SLICED_TELETEXT_B |
		    VBI_SLICED_CAPTION_525 |
		    VBI_SLICED_CAPTION_625 |
		    VBI_SLICED_VPS |
		    VBI_SLICED_VPS_F2 |
		    VBI_SLICED_WSS_625 |
		    VBI_SLICED_WSS_CPR1204);

	switch (option_out_file_format) {
	case FILE_FORMAT_DVB_PES:
	case FILE_FORMAT_DVB_TS:
		/* Other formats cannot be encoded. */
		services &= (VBI_SLICED_TELETEXT_B |
			     VBI_SLICED_CAPTION_625 |
			     VBI_SLICED_VPS |
			     VBI_SLICED_WSS_625);
		break;

	default:
		break;
	}

	cst = capture_stream_new (interfaces,
				  option_dev_name,
				  scanning,
				  services,
				  /* n_buffers (V4L2 mmap) */ 5,
				  option_dvb_pid,
				  sim_interlaced,
				  sim_synchronous,
				  option_raw_output,
				  option_read_not_pull,
				  option_strict,
				  decode_frame);

	if (interfaces & INTERFACE_SIM) {
		sim_load_caption ();

		capture_stream_sim_set_flags (cst, option_sim_flags);

		if (0 != option_sim_flags)
			capture_stream_sim_decode_raw (cst, TRUE);
	}

	if (option_cc_test_test)
		option_cc_test = TRUE;

	if (option_cc_test) {
		option_raw_output = FALSE;
		option_sliced_output = TRUE;
		option_dump_sliced = FALSE;
		option_dump_wss = FALSE;
		option_out_file_format = FILE_FORMAT_SLICED;
		sliced_output_count = 2 * 60 * 30;
	}

	if (option_raw_output || option_sliced_output) {
		vbi_sampling_par sp;

		capture_stream_get_sampling_par (cst, &sp);

		if (option_cc_test)
			init_frame_buffers (&sp);

		wst = write_stream_new (option_out_file_name,
					option_out_file_format,
					option_out_ts_pid,
					SCANNING (&sp));
	}

	stream_loop (cst);

	stream_delete (wst);
	wst = NULL;

	stream_delete (cst);
	cst = NULL;

	exit(EXIT_SUCCESS);	
}
