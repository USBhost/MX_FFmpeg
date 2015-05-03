/*
 *  zvbi-date -- Get station date and time from Teletext packet 8/30/2
 *
 *  Copyright (C) 2006, 2007 Michael H. Schimek
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

/* $Id: date.c,v 1.2 2009/03/04 21:47:47 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <locale.h>
#include <errno.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include "sliced.h"

#include "src/vbi.h"
#include "src/event.h"

#define PROGRAM_NAME "zvbi-date"

static vbi_bool			option_set_time;

static struct stream *		cst;
static vbi_decoder *		dec;

static void
print_time			(const vbi_local_time *	lt)
{
	char *buffer;
	size_t buffer_size;
	struct tm tm;
	size_t len;

	CLEAR (tm);

	if (NULL == localtime_r (&lt->time, &tm))
		error_exit (_("Invalid date received."));

	buffer_size = 1 << 16;
	buffer = malloc (buffer_size);
	if (NULL == buffer)
		no_mem_exit ();

	tm.tm_isdst = -1; /* unknown */

#ifdef HAVE_TM_GMTOFF
	if (lt->seconds_east_valid) {
		tm.tm_gmtoff = lt->seconds_east_valid;
		len = strftime (buffer, buffer_size, "%F %T %Z", &tm);
	} else
#endif
	{
		len = strftime (buffer, buffer_size, "%F %T UTC", &tm);
	}

	if (0 == len)
		no_mem_exit ();

	puts (buffer);

	free (buffer);
	buffer = NULL;
}

static void
set_time			(const vbi_local_time *	lt)
{
#if defined HAVE_CLOCK_SETTIME && defined CLOCK_REALTIME
	{
		struct timespec ts;

		ts.tv_sec = lt->time; /* UTC */
		ts.tv_nsec = 0;

		if (0 == clock_settime (CLOCK_REALTIME, &ts))
			return;
	}
#endif

	{
		struct timeval tv;

		tv.tv_sec = lt->time; /* UTC */
		tv.tv_usec = 0;

		if (0 == settimeofday (&tv, /* tz */ NULL))
			return;
	}

	error_exit (_("Cannot set system time: %s."),
		    strerror (errno));
}

static void
event_handler			(vbi_event *		ev,
				 void *			user_data)
{
	user_data = user_data; /* unused */

	switch (ev->type) {
	case VBI_EVENT_LOCAL_TIME:
	{
		const vbi_local_time *lt;

		lt = ev->ev.local_time;

		if (option_log_mask & VBI_LOG_NOTICE) {
			print_time (lt);
		}

		if (option_set_time) {
			set_time (lt);
		}

		exit (EXIT_SUCCESS);
	}

	default:
		assert (0);
	}
}

static vbi_bool
decode_function			(const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	static double first_sample_time = 0.0;

	raw = raw; /* unused */
	sp = sp;
	stream_time = stream_time;

	/* Packet 8/30/2 should repeat once every second. */
	if (first_sample_time <= 0.0) {
		first_sample_time = sample_time;
	} else if (sample_time - first_sample_time > 2.5) {
		error_exit (_("No station tuned in, poor reception, "
			      "or date and time not transmitted."));
	}

	/* Should really be const vbi_sliced *. */
	vbi_decode (dec, (vbi_sliced *) sliced, n_lines, sample_time);

	return TRUE;
}

static void
usage				(FILE *			fp)
{
	fprintf (fp, _("\
%s %s -- Get date and time from Teletext\n\n\
Copyright (C) 2006, 2007 Michael H. Schimek\n\
This program is licensed under GPLv2+. NO WARRANTIES.\n\n\
Usage: %s [options]\n\
-h | --help | --usage  Print this message and exit\n\
-q | --quiet           Suppress progress and error messages\n\
-v | --verbose         Increase verbosity\n\
-V | --version         Print the program version and exit\n\
Device options:\n\
-d | --device file     Capture from this device (default %s)\n\
                       V4L/V4L2: /dev/vbi, /dev/vbi0, /dev/vbi1, ...\n\
                       Linux DVB: /dev/dvb/adapter0/demux0, ...\n\
		       *BSD bktr driver: /dev/vbi, /dev/vbi0, ...\n\
-i | --pid pid         Capture the stream with this PID from a Linux\n\
                       DVB device\n\
-n | --ntsc            Video standard hint for V4L interface (default\n\
		       PAL/SECAM)\n\
-p | --pal | --secam   Video standard hint\n\
Other options:\n\
-s | --set             Set system time from received date and time\n\
"),
		 PROGRAM_NAME, VERSION, program_invocation_name,
		 option_dev_name);
}

static const char short_options [] = "d:hi:npqsvV";

#ifdef HAVE_GETOPT_LONG

static const struct option
long_options [] = {
	{ "device",	required_argument,	NULL,		'd' },
	{ "help",	no_argument,		NULL,		'h' },
	{ "usage",	no_argument,		NULL,		'h' },
	{ "pid",	required_argument,	NULL,		'i' },
	{ "ntsc",	no_argument,		NULL,		'n' },
	{ "pal",	no_argument,		NULL,		'p' },
	{ "secam",	no_argument,		NULL,		'p' },
	{ "quiet",	no_argument,		NULL,		'q' },
	{ "set",	no_argument,		NULL,		's' },
	{ "verbose",	no_argument,		NULL,		'v' },
	{ "version",	no_argument,		NULL,		'V' },
	{ NULL, 0, 0, 0 }
};

#else
#  define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

static int			option_index;

int
main				(int			argc,
				 char **		argv)
{
	unsigned int interfaces;
	unsigned int scanning;
	vbi_bool success;

	init_helpers (argc, argv);

	scanning = 625;

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
			break;

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

		case 'n':
			scanning = 525;
			break;

		case 'p':
			scanning = 625;
			break;

		case 'q':
			parse_option_quiet ();
			break;

		case 's':
			option_set_time = TRUE;
			break;

		case 'v':
			parse_option_verbose ();
			break;

		case 'V':
			printf (PROGRAM_NAME " " VERSION "\n");
			exit (EXIT_SUCCESS);

		default:
			usage (stderr);
			exit (EXIT_FAILURE);
		}
	}

	cst = capture_stream_new (interfaces,
				  option_dev_name,
				  scanning,
				  VBI_SLICED_TELETEXT_B,
				  /* n_buffers (V4L2 mmap) */ 5,
				  option_dvb_pid,
				  /* interlaced (SIM) */ FALSE,
				  /* synchronous (SIM) */ TRUE,
				  /* capture_raw_data */ FALSE,
				  /* read_not_pull */ FALSE,
				  /* strict */ 1,
				  decode_function);

	dec = vbi_decoder_new ();
	if (NULL == dec)
		no_mem_exit ();

	success = vbi_event_handler_register (dec, VBI_EVENT_LOCAL_TIME,
					      event_handler,
					      /* user_data */ NULL);
	if (!success)
		no_mem_exit ();

	stream_loop (cst);

	stream_delete (cst);
	cst = NULL;

	exit (EXIT_SUCCESS);
}
