/*
 *  zvbi-decode -- Decode sliced VBI data using low-level
 *		   libzvbi functions
 *
 *  Copyright (C) 2004, 2006, 2007 Michael H. Schimek
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

/* $Id: decode.c,v 1.36 2009/12/14 23:43:52 mschimek Exp $ */

/* For libzvbi version 0.2.x / 0.3.x. */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include "src/version.h"
#if 2 == VBI_VERSION_MINOR
#  include "src/bcd.h"
#  include "src/cc608_decoder.h"
#  include "src/conv.h"
#  include "src/dvb_demux.h"
#  include "src/hamm.h"
#  include "src/idl_demux.h"
#  include "src/lang.h"
#  include "src/packet-830.h"
#  include "src/pfc_demux.h"
#  include "src/vps.h"
#  include "src/xds_demux.h"
#elif 3 == VBI_VERSION_MINOR
#  include "src/zvbi.h"
#  include "src/misc.h"		/* _vbi_to_ascii() */
#else
#  error VBI_VERSION_MINOR == ?
#endif

#include "sliced.h"

#undef _
#define _(x) x /* i18n TODO */

/* Will be installed one day. */
#define PROGRAM_NAME "zvbi-decode"

static const char *		option_in_file_name;
static enum file_format		option_in_file_format;
static unsigned int		option_in_ts_pid;

static vbi_pgno		option_pfc_pgno;
static unsigned int		option_pfc_stream;

static vbi_bool		option_decode_ttx;
static vbi_bool		option_decode_8301;
static vbi_bool		option_decode_8302;
static vbi_bool		option_decode_caption;
static vbi_bool		option_decode_xds;
static vbi_bool		option_decode_idl;
static vbi_bool		option_decode_vps;
static vbi_bool		option_decode_vps_other;
static vbi_bool		option_decode_wss;

static vbi_bool		option_dump_network;
static vbi_bool		option_dump_hex;
static vbi_bool		option_dump_bin;
static vbi_bool		option_dump_time;
static double			option_metronome_tick;

static vbi_pgno		option_pfc_pgno	= 0;
static unsigned int		option_pfc_stream = 0;

static unsigned int		option_idl_channel = 0;
static unsigned int		option_idl_address = 0;

static struct stream *		rst;

static vbi_pfc_demux *		pfc;
static vbi_idl_demux *		idl;
static vbi_xds_demux *		xds;

extern void
_vbi_pfc_block_dump		(const vbi_pfc_block *	pb,
				 FILE *			fp,
				 vbi_bool		binary);

static vbi_bool
xds_cb				(vbi_xds_demux *	xd,
				 const vbi_xds_packet *	xp,
				 void *			user_data)
{
	xd = xd; /* unused */
	user_data = user_data;

	_vbi_xds_packet_dump (xp, stdout);

	return TRUE; /* no errors */
}

static void
caption				(const uint8_t		buffer[2],
				 unsigned int		line)
{
	if (option_decode_xds && 284 == line) {
		if (!vbi_xds_demux_feed (xds, buffer)) {
			printf ("Parity error in XDS data.\n");
		}
	}

	if (option_decode_caption
	    && (21 == line || 284 == line /* NTSC */
		|| 22 == line /* PAL? */)) {

		printf ("CC line=%3u ", line);
		_vbi_cc608_dump (stdout, buffer[0], buffer[1]);
	}
}

#if 3 == VBI_VERSION_MINOR /* XXX port me back */

static void
dump_cni			(vbi_cni_type		type,
				 unsigned int		cni)
{
	vbi_network nk;
	vbi_bool success;

	if (!option_dump_network)
		return;

	success = vbi_network_init (&nk);
	if (!success)
		no_mem_exit ();

	success = vbi_network_set_cni (&nk, type, cni);
	if (!success)
		no_mem_exit ();

	_vbi_network_dump (&nk, stdout);
	putchar ('\n');

	vbi_network_destroy (&nk);
}

#endif /* 3 == VBI_VERSION_MINOR */

static void
dump_bytes			(const uint8_t *	buffer,
				 unsigned int		n_bytes)
{
	unsigned int j;

	if (option_dump_bin) {
		fwrite (buffer, 1, n_bytes, stdout);
		return;
	}

	if (option_dump_hex) {
		for (j = 0; j < n_bytes; ++j)
			printf ("%02x ", buffer[j]);
	}

	putchar ('>');

	for (j = 0; j < n_bytes; ++j) {
		/* Not all Teletext characters are representable
		   in ASCII or even UTF-8, but at this stage we don't
		   know the Teletext code page for a proper conversion. */
		char c = _vbi_to_ascii (buffer[j]);

		putchar (c);
	}

	puts ("<");
}

static void
packet_8301			(const uint8_t		buffer[42],
				 unsigned int		designation)
{
	unsigned int cni;
	time_t time;
	int gmtoff;
	struct tm tm;

	if (!option_decode_8301)
		return;

	if (!vbi_decode_teletext_8301_cni (&cni, buffer)) {
		printf ("Error in Teletext "
			"packet 8/30 format 1 CNI.\n");
		return;
	}

	if (!vbi_decode_teletext_8301_local_time (&time, &gmtoff, buffer)) {
		printf ("Error in Teletext "
			"packet 8/30 format 1 local time.\n");
		return;
	}

	printf ("Teletext packet 8/30/%u cni=%x time=%u gmtoff=%d ",
		designation, cni, (unsigned int) time, gmtoff);

	gmtime_r (&time, &tm);

	printf ("(%4u-%02u-%02u %02u:%02u:%02u UTC)\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

#if 3 == VBI_VERSION_MINOR
	if (0 != cni)
		dump_cni (VBI_CNI_TYPE_8301, cni);
#endif
}

static void
packet_8302			(const uint8_t		buffer[42],
				 unsigned int		designation)
{
	unsigned int cni;
	vbi_program_id pi;

	if (!option_decode_8302)
		return;

	if (!vbi_decode_teletext_8302_cni (&cni, buffer)) {
		printf ("Error in Teletext "
			"packet 8/30 format 2 CNI.\n");
		return;
	}

	if (!vbi_decode_teletext_8302_pdc (&pi, buffer)) {
		printf ("Error in Teletext "
			"packet 8/30 format 2 PDC data.\n");
		return;
	}

	printf ("Teletext packet 8/30/%u cni=%x ", designation, cni);

	_vbi_program_id_dump (&pi, stdout);

	putchar ('\n');

#if 3 == VBI_VERSION_MINOR
	if (0 != pi.cni)
		dump_cni (pi.cni_type, pi.cni);
#endif
}

static vbi_bool
page_function_clear_cb		(vbi_pfc_demux *	dx,
				 void *			user_data,
		                 const vbi_pfc_block *	block)
{
	dx = dx; /* unused */
	user_data = user_data;

	_vbi_pfc_block_dump (block, stdout, option_dump_bin);

	return TRUE;
}

static vbi_bool
idl_format_a_cb			(vbi_idl_demux *	idl,
				 const uint8_t *	buffer,
				 unsigned int		n_bytes,
				 unsigned int		flags,
				 void *			user_data)
{
	idl = idl;
	user_data = user_data;

	if (!option_dump_bin) {
		printf ("IDL-A%s%s ",
			(flags & VBI_IDL_DATA_LOST) ? " <data lost>" : "",
			(flags & VBI_IDL_DEPENDENT) ? " <dependent>" : "");
	}

	dump_bytes (buffer, n_bytes);

	return TRUE;
}

static void
packet_idl			(const uint8_t		buffer[42],
				 unsigned int		channel)
{
	int pa; /* packet address */
	int ft; /* format type */

	printf ("IDL ch=%u ", channel);

	switch (channel) {
	case 0:
		assert (0);

	case 4:
	case 12:
		printf ("(Low bit rate audio) ");

		dump_bytes (buffer, 42);

		break;

	case 5:
	case 6:
	case 13:
	case 14:
		pa = vbi_unham8 (buffer[3]);
		pa |= vbi_unham8 (buffer[4]) << 4;
		pa |= vbi_unham8 (buffer[5]) << 8;

		if (pa < 0) {
			printf ("Hamming error in Datavideo "
				"packet-address byte.\n");
			return;
		}

		printf ("(Datavideo) pa=0x%x ", pa);

		dump_bytes (buffer, 42);

		break;

	case 8:
	case 9:
	case 10:
	case 11:
	case 15:
		ft = vbi_unham8 (buffer[2]);
		if (ft < 0) {
			printf ("Hamming error in IDL format "
				"A or B format-type byte.\n");
			return;
		}

		if (0 == (ft & 1)) {
			int ial; /* interpretation and address length */
			unsigned int spa_length;
			int spa; /* service packet address */
			unsigned int i;

			ial = vbi_unham8 (buffer[3]);
			if (ial < 0) {
				printf ("Hamming error in IDL format "
					"A interpretation-and-address-"
					"length byte.\n");
				return;
			}

			spa_length = (unsigned int) ial & 7;
			if (7 == spa_length) {
				printf ("(Format A?) ");
				dump_bytes (buffer, 42);
				return;
			}

			spa = 0;

			for (i = 0; i < spa_length; ++i)
				spa |= vbi_unham8 (buffer[4 + i]) << (4 * i);

			if (spa < 0) {
				printf ("Hamming error in IDL format "
					"A service-packet-address byte.\n");
				return;
			}

			printf ("(Format A) spa=0x%x ", spa);
		} else if (1 == (ft & 3)) {
			int an; /* application number */
			int ai; /* application identifier */

			an = (ft >> 2);

			ai = vbi_unham8 (buffer[3]);
			if (ai < 0) {
				printf ("Hamming error in IDL format "
					"B application-number byte.\n");
				return;
			}

			printf ("(Format B) an=%d ai=%d ", an, ai);
		}

		dump_bytes (buffer, 42);

		break;

	default:
		dump_bytes (buffer, 42);

		break;
	}
}

static void
teletext			(const uint8_t		buffer[42],
				 unsigned int		line)
{
	int pmag;
	unsigned int magazine;
	unsigned int packet;

	if (NULL != pfc) {
		if (!vbi_pfc_demux_feed (pfc, buffer)) {
			printf ("Error in Teletext "
				"PFC packet.\n");
			return;
		}
	}

	if (NULL != idl) {
		if (!vbi_idl_demux_feed (idl, buffer)) {
			printf ("Error in Teletext "
				"IDL packet.\n");
			return;
		}
	}

	if (!(option_decode_ttx |
	      option_decode_8301 |
	      option_decode_8302 |
	      option_decode_idl))
		return;

	pmag = vbi_unham16p (buffer);
	if (pmag < 0) {
		printf ("Hamming error in Teletext "
			"packet number.\n");
		return;
	}

	magazine = pmag & 7;
	if (0 == magazine)
		magazine = 8;

	packet = pmag >> 3;

	if (8 == magazine && 30 == packet) {
		int designation;

		designation = vbi_unham8 (buffer[2]);
		if (designation < 0 ) {
			printf ("Hamming error in Teletext "
				"packet 8/30 designation byte.\n");
			return;
		}

		if (designation >= 0 && designation <= 1) {
			packet_8301 (buffer, designation);
			return;
		}

		if (designation >= 2 && designation <= 3) {
			packet_8302 (buffer, designation);
			return;
		}
	}

	if (30 == packet || 31 == packet) {
		if (option_decode_idl) {
#if 1
			packet_idl (buffer, pmag & 15);
#else
			printf ("Teletext IDL packet %u/%2u ",
				magazine, packet);
			dump_bytes (buffer, /* n_bytes */ 42);
#endif
			return;
		}
	}

	if (option_decode_ttx) {
		printf ("Teletext line=%3u %x/%2u ",
			line, magazine, packet);
		dump_bytes (buffer, /* n_bytes */ 42);
		return;
	}
}

static void
vps				(const uint8_t		buffer[13],
				 unsigned int		line)
{
	if (option_decode_vps) {
		unsigned int cni;
#if 1
		vbi_program_id pi;
#endif
		if (option_dump_bin) {
			printf ("VPS line=%3u ", line);
			fwrite (buffer, 1, 13, stdout);
			fflush (stdout);
			return;
		}

		if (!vbi_decode_vps_cni (&cni, buffer)) {
			printf ("Error in VPS packet CNI.\n");
			return;
		}

		if (!vbi_decode_vps_pdc (&pi, buffer)) {
			printf ("Error in VPS packet PDC data.\n");
			return;
		}

		printf ("VPS line=%3u ", line);

		_vbi_program_id_dump (&pi, stdout);

		putchar ('\n');

#if 3 == VBI_VERSION_MINOR
		if (0 != pi.cni)
			dump_cni (pi.cni_type, pi.cni);
#endif
	}

	if (option_decode_vps_other) {
		static char pr_label[2][20];
		static char label[2][20];
		static int l[2] = { 0, 0 };
		unsigned int i;
		int c;

		i = (line != 16);

		c = vbi_rev8 (buffer[1]);

		if (c & 0x80) {
			label[i][l[i]] = 0;
			strcpy (pr_label[i], label[i]);
			l[i] = 0;
		}

		label[i][l[i]] = _vbi_to_ascii (c);

		l[i] = (l[i] + 1) % 16;
		
		printf ("VPS line=%3u bytes 3-10: "
			"%02x %02x (%02x='%c') %02x %02x "
			"%02x %02x %02x %02x (\"%s\")\n",
			line,
			buffer[0], buffer[1],
			c, _vbi_to_ascii (c),
			buffer[2], buffer[3],
			buffer[4], buffer[5], buffer[6], buffer[7],
			pr_label[i]);
	}
}

#if 3 == VBI_VERSION_MINOR /* XXX port me back */

static void
wss_625				(const uint8_t		buffer[2])
{
	if (option_decode_wss) {  
		vbi_aspect_ratio ar;

		if (!vbi_decode_wss_625 (&ar, buffer)) {
			printf ("Error in WSS packet.\n");
			return;
		}

		fputs ("WSS ", stdout);

		_vbi_aspect_ratio_dump (&ar, stdout);

		putchar ('\n');
	}
}

#endif /* 3 == VBI_VERSION_MINOR */

static vbi_bool
decode_frame			(const vbi_sliced *	s,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	static double metronome = 0.0;
	static double last_sample_time = 0.0;
	static int64_t last_stream_time = 0;

	raw = raw; /* unused */
	sp = sp;

	if (option_dump_time || option_metronome_tick > 0.0) {
		/* Sample time: When we captured the data, in
		   		seconds since 1970-01-01 (gettimeofday()).
		   Stream time: For ATSC/DVB the Presentation Time Stamp.
				For analog the frame number multiplied by
				the nominal frame period (1/25 or
				1001/30000 s). Both given in 90 kHz units.
		   Note this isn't fully implemented yet. */

		if (option_metronome_tick > 0.0) {
			printf ("ST %f (adv %+f, err %+f) PTS %"
				PRId64 " (adv %+" PRId64 ", err %+f)\n",
				sample_time, sample_time - last_sample_time,
				sample_time - metronome,
				stream_time, stream_time - last_stream_time,
				(double) stream_time - metronome);

			metronome += option_metronome_tick;
		} else {
			printf ("ST %f (%+f) PTS %" PRId64 " (%+" PRId64 ")\n",
				sample_time, sample_time - last_sample_time,
				stream_time, stream_time - last_stream_time);
		}

		last_sample_time = sample_time;
		last_stream_time = stream_time;
	}

	while (n_lines > 0) {
		switch (s->id) {
		case VBI_SLICED_TELETEXT_B_L10_625:
		case VBI_SLICED_TELETEXT_B_L25_625:
		case VBI_SLICED_TELETEXT_B_625:
			teletext (s->data, s->line);
			break;

		case VBI_SLICED_VPS:
		case VBI_SLICED_VPS_F2:
			vps (s->data, s->line);
			break;

		case VBI_SLICED_CAPTION_625_F1:
		case VBI_SLICED_CAPTION_625_F2:
		case VBI_SLICED_CAPTION_625:
		case VBI_SLICED_CAPTION_525_F1:
		case VBI_SLICED_CAPTION_525_F2:
		case VBI_SLICED_CAPTION_525:
			caption (s->data, s->line);
			break;

		case VBI_SLICED_WSS_625:
#if 3 == VBI_VERSION_MINOR /* XXX port me back */
			wss_625 (s->data);
#endif
			break;

		case VBI_SLICED_WSS_CPR1204:
			break;
		}

		++s;
		--n_lines;
	}

	return TRUE;
}

static void
usage				(FILE *			fp)
{
	/* FIXME Supposed to be localized but we can't use #ifs
	   within the _() macro. */
	fprintf (fp, "\
%s %s -- Low-level VBI decoder\n\n\
Copyright (C) 2004, 2006, 2007 Michael H. Schimek\n\
This program is licensed under GPLv2 or later. NO WARRANTIES.\n\n\
Usage: %s [options] < sliced VBI data\n\
-h | --help | --usage  Print this message and exit\n\
-q | --quiet           Suppress progress and error messages\n\
-V | --version         Print the program version and exit\n\
Input options:\n\
-i | --input name      Read the VBI data from this file instead of\n\
                       standard input\n\
-P | --pes             Source is a DVB PES stream\n\
-T | --ts pid          Source is a DVB TS stream\n\
Decoding options:\n\
-1 | --8301            Teletext packet 8/30 format 1 (local time)\n\
-2 | --8302            Teletext packet 8/30 format 2 (PDC)\n\
-c | --cc              Closed Caption\n\
-j | --idl             Any Teletext IDL packets (M/30, M/31)\n\
-t | --ttx             Decode any Teletext packet\n\
-v | --vps             Video Programming System (PDC)\n"
#if 3 == VBI_VERSION_MINOR /* XXX port me back */
"-w | --wss             Wide Screen Signalling\n"
#endif
"-x | --xds             Decode eXtended Data Service (NTSC line 284)\n\
-a | --all             Everything above, e.g.\n\
                       -i     decode IDL packets\n\
                       -a     decode everything\n\
                       -a -i  everything except IDL\n\
-l | --idl-ch N\n\
-d | --idl-addr NNN    Decode Teletext IDL format A data from channel N,\n\
                       service packet address NNN (default 0)\n\
-r | --vps-other       Decode VPS data unrelated to PDC\n\
-p | --pfc-pgno NNN\n\
-s | --pfc-stream NN   Decode Teletext Page Function Clear data\n\
                       from page NNN (for example 1DF), stream NN\n\
                       (default 0)\n\
Modifying options:\n\
-e | --hex             With -t dump packets in hex and ASCII,\n\
                         otherwise only ASCII\n\
-n | --network         With -1, -2, -v decode CNI and print\n\
                         available information about the network\n\
-b | --bin             With -t, -p, -v dump data in binary format\n\
                         instead of ASCII\n\
-m | --time            Dump capture timestamps\n\
-M | --metronome tick  Compare timestamps against a metronome advancing\n\
                       by tick seconds per frame\n\
",
		 PROGRAM_NAME, VERSION, program_invocation_name);
}

static const char
short_options [] = "12abcd:ehi:jl:mnp:qrs:tvwxM:PT:V";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options [] = {
	{ "8301",	no_argument,		NULL,		'1' },
	{ "8302",	no_argument,		NULL,		'2' },
	{ "all",	no_argument,		NULL,		'a' },
	{ "bin",	no_argument,		NULL,		'b' },
	{ "cc",		no_argument,		NULL,		'c' },
	{ "idl-addr",	required_argument,	NULL,		'd' },
	{ "hex",	no_argument,		NULL,		'e' },
	{ "help",	no_argument,		NULL,		'h' },
	{ "usage",	no_argument,		NULL,		'h' },
	{ "input",	required_argument,	NULL,		'i' },
	{ "idl",	no_argument,		NULL,		'j' },
	{ "idl-ch",	required_argument,	NULL,		'l' },
	{ "time",	no_argument,		NULL,		'm' },
	{ "network",	no_argument,		NULL,		'n' },
	{ "pfc-pgno",	required_argument,	NULL,		'p' },
	{ "quiet",	no_argument,		NULL,		'q' },
	{ "vps-other",	no_argument,		NULL,		'r' },
	{ "pfc-stream",	required_argument,	NULL,		's' },
	{ "ttx",	no_argument,		NULL,		't' },
	{ "vps",	no_argument,		NULL,		'v' },
	{ "wss",	no_argument,		NULL,		'w' },
	{ "xds",	no_argument,		NULL,		'x' },
	{ "metronome",	required_argument,	NULL,		'M' },
	{ "pes",	no_argument,		NULL,		'P' },
	{ "ts",		required_argument,	NULL,		'T' },
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
	init_helpers (argc, argv);

	option_in_file_format = FILE_FORMAT_SLICED;

	for (;;) {
		int c;

		c = getopt_long (argc, argv, short_options,
				 long_options, &option_index);
		if (-1 == c)
			break;

		switch (c) {
		case 0: /* getopt_long() flag */
			break;

		case '1':
			option_decode_8301 ^= TRUE;
			break;

		case '2':
			option_decode_8302 ^= TRUE;
			break;

		case 'a':
			option_decode_ttx = TRUE;
			option_decode_8301 = TRUE;
			option_decode_8302 = TRUE;
			option_decode_caption = TRUE;
			option_decode_idl = TRUE;
			option_decode_vps = TRUE;
			option_decode_wss = TRUE;
			option_decode_xds = TRUE;
			option_pfc_pgno = 0x1DF;
			break;

		case 'b':
			option_dump_bin ^= TRUE;
			break;

		case 'c':
			option_decode_caption ^= TRUE;
			break;

		case 'd':
			assert (NULL != optarg);
			option_idl_address = strtol (optarg, NULL, 0);
			break;

		case 'e':
			option_dump_hex ^= TRUE;
			break;

		case 'h':
			usage (stdout);
			exit (EXIT_SUCCESS);

		case 'i':
			assert (NULL != optarg);
			option_in_file_name = optarg;
			break;

		case 'j':
			option_decode_idl ^= TRUE;
			break;

		case 'l':
			assert (NULL != optarg);
			option_idl_channel = strtol (optarg, NULL, 0);
			break;

		case 'm':
			option_dump_time ^= TRUE;
			break;

		case 'n':
			option_dump_network ^= TRUE;
			break;

		case 'p':
			assert (NULL != optarg);
			option_pfc_pgno = strtol (optarg, NULL, 16);
			break;

		case 'q':
			parse_option_quiet ();
			break;

		case 'r':
			option_decode_vps_other ^= TRUE;
			break;

		case 's':
			assert (NULL != optarg);
			option_pfc_stream = strtol (optarg, NULL, 0);
			break;

		case 't':
			option_decode_ttx ^= TRUE;
			break;

		case 'v':
			option_decode_vps ^= TRUE;
			break;

		case 'w':
			option_decode_wss ^= TRUE;
			break;

		case 'x':
			option_decode_xds ^= TRUE;
			break;

		case 'M':
			assert (NULL != optarg);
			option_metronome_tick = strtod (optarg, NULL);
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

	if (0 != option_pfc_pgno) {
		pfc = vbi_pfc_demux_new (option_pfc_pgno,
					 option_pfc_stream,
					 page_function_clear_cb,
					 /* user_data */ NULL);
		if (NULL == pfc)
			no_mem_exit ();
	}

	if (0 != option_idl_channel) {
		idl = vbi_idl_a_demux_new (option_idl_channel,
					   option_idl_address,
					   idl_format_a_cb,
					   /* user_data */ NULL);
		if (NULL == idl)
			no_mem_exit ();
	}

	if (option_decode_xds) {
		xds = vbi_xds_demux_new (xds_cb,
					 /* used_data */ NULL);
		if (NULL == xds)
			no_mem_exit ();
	}

	rst = read_stream_new (option_in_file_name,
			       option_in_file_format,
			       option_in_ts_pid,
			       decode_frame);

	stream_loop (rst);

	stream_delete (rst);
	rst = NULL;

	error_msg (_("End of stream."));

	vbi_xds_demux_delete (xds);
	xds = NULL;

	vbi_idl_demux_delete (idl);
	idl = NULL;

	vbi_pfc_demux_delete (pfc);
	pfc = NULL;

	exit (EXIT_SUCCESS);
}
