/*
 *  zvbi-sliced2pes -- Sliced VBI file converter
 *
 *  Copyright (C) 2004, 2007 Michael H. Schimek
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

/* $Id: sliced2pes.c,v 1.15 2008/03/01 07:36:24 mschimek Exp $ */

/* For libzvbi version 0.2.x / 0.3.x. */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <limits.h>
#include <unistd.h>		/* optarg */
#include <assert.h>
#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include "src/version.h"
#if 2 == VBI_VERSION_MINOR
#  include "src/dvb_mux.h"
#elif 3 == VBI_VERSION_MINOR
#  include "src/zvbi.h"
#else
#  error VBI_VERSION_MINOR == ?
#endif

#include "sliced.h"

#undef _
#define _(x) x /* i18n TODO */

/* Will be installed one day. */
#define PROGRAM_NAME "sliced2pes"

static const char *		option_in_file_name;
static enum file_format		option_in_file_format;
static unsigned int		option_in_ts_pid;

static const char *		option_out_file_name;
static enum file_format		option_out_file_format;
static unsigned int		option_out_ts_pid;

static unsigned long		option_data_identifier;
static unsigned long		option_min_pes_packet_size;
static unsigned long		option_max_pes_packet_size;

static struct stream *		rst;
static struct stream *		wst;

static vbi_bool
output_frame			(const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	raw = raw; /* unused */
	sp = sp;

	write_stream_sliced (wst, sliced, n_lines,
			     /* raw */ NULL,
			     /* sp */ NULL,
			     sample_time, stream_time);
	return TRUE;
}

static void
get_mux_defaults		(void)
{
	vbi_dvb_mux *mx;

	mx = vbi_dvb_pes_mux_new (/* callback */ NULL,
				   /* user_data */ NULL);
	if (NULL == mx)
		no_mem_exit ();

	option_data_identifier =
		vbi_dvb_mux_get_data_identifier (mx);

	option_min_pes_packet_size =
		vbi_dvb_mux_get_min_pes_packet_size (mx);

	option_max_pes_packet_size =
		vbi_dvb_mux_get_max_pes_packet_size (mx);

	vbi_dvb_mux_delete (mx);
}

static void
usage				(FILE *			fp)
{
	get_mux_defaults ();

	fprintf (fp, _("\
%s %s -- VBI stream converter\n\n\
Copyright (C) 2004, 2007 Michael H. Schimek\n\
This program is licensed under GPLv2 or later. NO WARRANTIES.\n\n\
Usage: %s [options] < sliced VBI data > PES or TS stream\n\
-h | --help | --usage             Print this message and exit\n\
-q | --quiet                      Suppress progress and error messages\n\
-v | --verbose                    Increase verbosity\n\
-V | --version                    Print the program version and exit\n\
Input options:\n\
-i | --input name                 Read the VBI data from this file instead\n\
                                  of standard input\n\
-P | --pes | --pes-input          Source is a DVB PES stream\n\
-T | --ts | --ts-input pid        Source is a DVB TS stream\n\
Output options:\n\
-d | --data-identifier n          0x10 ... 0x1F for compatibility with\n\
                                  ETS 300 472 compliant decoders, or\n\
                                  0x99 ... 0x9B as defined in EN 301 775\n\
                                  (default 0x%02lx)\n\
-m | --max | --max-packet-size n  Maximum PES packet size (%lu bytes)\n\
-n | --min | --min-packet-size n  Minimum PES packet size (%lu bytes)\n\
-o | --output name                Write the VBI data to this file instead\n\
                                  of standard output\n\
-p | --pes-output                 Generate a DVB PES stream\n\
-t | --ts-output pid              Generate a DVB TS stream with this PID\n\
"),
		 PROGRAM_NAME, VERSION, program_invocation_name,
		 option_data_identifier,
		 option_max_pes_packet_size,
		 option_min_pes_packet_size);
}

static const char
short_options [] = "d:hi:m:n:o:pqt:vPT:V";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options [] = {
	{ "data-identifier",	required_argument,	NULL,	'd' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "usage",		no_argument,		NULL,	'h' },
	{ "input",		required_argument,	NULL,	'i' },
	{ "max-packet-size",	required_argument,	NULL,	'm' },
	{ "min-packet-size",	required_argument,	NULL,	'n' },
	{ "output",		required_argument,	NULL,	'o' },
	{ "pes-output",		no_argument,		NULL,	'p' },
	{ "quiet",		no_argument,		NULL,	'q' },
	{ "ts-output",		required_argument,	NULL,	't' },
	{ "verbose",		no_argument,		NULL,	'v' },
	{ "pes-input",		no_argument,		NULL,	'P' },
	{ "ts-input",		required_argument,	NULL,	'T' },
	{ "version",		no_argument,		NULL,	'V' },
	{ NULL, 0, 0, 0 }
};
#else
#  define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

static int			option_index;

static void
parse_option_data_identifier	(void)
{
	const char *s = optarg;
	char *end;

	assert (NULL != optarg);

	option_data_identifier = strtoul (s, &end, 0);

	if (option_data_identifier > 0xFF) {
		error_exit (_("Invalid data identifier 0x%02lx."),
			    option_data_identifier);
	}
}

int
main				(int			argc,
				 char **		argv)
{
	init_helpers (argc, argv);

	get_mux_defaults ();

	option_in_file_format = FILE_FORMAT_SLICED;
	option_out_file_format = FILE_FORMAT_DVB_PES;

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
			parse_option_data_identifier ();
			break;

		case 'h':
			usage (stdout);
			exit (EXIT_SUCCESS);

		case 'i':
			assert (NULL != optarg);
			option_in_file_name = optarg;
			break;

		case 'm':
			assert (NULL != optarg);
			option_max_pes_packet_size =
				strtoul (optarg, NULL, 0);
			if (option_max_pes_packet_size > UINT_MAX)
				option_max_pes_packet_size = UINT_MAX;
			break;

		case 'n':
			assert (NULL != optarg);
			option_min_pes_packet_size =
				strtoul (optarg, NULL, 0);
			if (option_min_pes_packet_size > UINT_MAX)
				option_min_pes_packet_size = UINT_MAX;
			break;

		case 'o':
			assert (NULL != optarg);
			option_out_file_name = optarg;
			break;

		case 'p':
			option_out_file_format = FILE_FORMAT_DVB_PES;
			break;

		case 'q':
			parse_option_quiet ();
			break;

		case 't':
			option_out_ts_pid = parse_option_ts ();
			option_out_file_format = FILE_FORMAT_DVB_TS;
			break;

		case 'v':
			parse_option_verbose ();
			break;

		case 'P':
			option_in_file_format = FILE_FORMAT_DVB_PES;
			break;

		case 'T':
			option_in_ts_pid = parse_option_ts ();
			option_in_file_format = FILE_FORMAT_DVB_TS;
			break;

		case 'V':
			printf (PROGRAM_NAME " " VERSION "\n");
			exit (EXIT_SUCCESS);

		default:
			usage (stderr);
			exit (EXIT_FAILURE);
		}
	}

	wst = write_stream_new (option_out_file_name,
				option_out_file_format,
				option_out_ts_pid,
				/* system */ 625);

	write_stream_set_data_identifier (wst, option_data_identifier);
	write_stream_set_pes_packet_size (wst,
					  option_min_pes_packet_size,
					  option_max_pes_packet_size);

	rst = read_stream_new (option_in_file_name,
			       option_in_file_format,
			       option_in_ts_pid,
			       output_frame);

	stream_loop (rst);

	stream_delete (rst);
	rst = NULL;

	stream_delete (wst);
	wst = NULL;

	error_msg (_("End of stream."));

	exit (EXIT_SUCCESS);
}
