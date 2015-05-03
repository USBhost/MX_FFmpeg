/*
 *  libzvbi test
 *
 *  Copyright (C) 2004, 2005, 2007 Michael H. Schimek
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

/* $Id: export.c,v 1.24 2008/03/01 07:36:51 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <locale.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include "src/version.h"
#if 2 == VBI_VERSION_MINOR
#  include "src/cache.h"
#  include "src/decoder.h"
#  include "src/export.h"
#  include "src/page_table.h"
#  include "src/vbi.h"
#  include "src/vt.h"
#  define vbi_decoder_feed(vbi, sliced, n_lines, ts)			\
	vbi_decode (vbi, (vbi_sliced *) sliced, n_lines, ts)
#  define vbi_export_info_from_export(ex)				\
	vbi_export_info_export (ex)
   /* Not available. */
#  define vbi_export_set_timestamp(ex, ts) ((void) 0)
#  define vbi_export_set_link_cb(ex, cb, ud) ((void) 0)
#  define vbi_export_set_pdc_cb(ex, cb, ud) ((void) 0)
#elif 3 == VBI_VERSION_MINOR
#  include "src/misc.h"
#  include "src/zvbi.h"
#else
#  error VBI_VERSION_MINOR == ?
#endif

#include "sliced.h"

#undef _
#define _(x) x /* later */

#define PROGRAM_NAME "zvbi-export"

static const char *		option_in_file_name;
static enum file_format		option_in_file_format;
static unsigned int		option_in_ts_pid;

static vbi_bool		option_dcc;
static unsigned int		option_delay;
static vbi_bool			have_option_default_cs;
static vbi_ttx_charset_code	option_default_cs;
static vbi_ttx_charset_code	option_override_cs;
static vbi_bool		option_dump_pg;
static vbi_bool		option_fast;
static vbi_bool		option_header_only;
static vbi_bool		option_hyperlinks;
static vbi_bool		option_navigation;
static vbi_bool		option_padding;
static vbi_bool		option_panels;
static vbi_bool		option_pdc_enum;
static vbi_bool		option_pdc_links;
static vbi_bool		option_row_update;
static vbi_bool		option_subtitles;
static vbi_rgba		option_default_bg;
static vbi_rgba		option_default_fg;
static unsigned int		option_target = 3;

static struct stream *		rst;
static vbi_decoder *		vbi;
static vbi_export *		ex;

static vbi_page_table *	pt;
static vbi_pgno		cc_chan;

static char *			out_file_name_prefix;
static char *			out_file_name_suffix;

static int			cr;

static vbi_bool		quit;

static void
close_output_file		(FILE *			fp)
{
	if (fp != stdout) {
		if (0 != fclose (fp))
			write_error_exit (/* msg: errno */ NULL);
	} else {
		fflush (fp);
	}
}

static char *
output_file_name		(vbi_pgno		pgno,
				 vbi_subno		subno)
{
	char *name;
	int r;

	if (NULL == out_file_name_prefix) {
		error_exit ("This target requires "
			    "an output file name.\n");
	}

	r = asprintf (&name, "%s-%03x-%02x.%s",
		      out_file_name_prefix, pgno, subno,
		      out_file_name_suffix);
	if (r < 0 || NULL == name)
		no_mem_exit ();

	return name;
}

static FILE *
open_output_file		(vbi_pgno		pgno,
				 vbi_subno		subno)
{
	char *name;
	FILE *fp;

	if (NULL == out_file_name_prefix)
		return stdout;

	name = output_file_name (pgno, subno);

	fp = fopen (name, "w");
	if (NULL == fp) {
		error_exit (_("Could not open "
			      "output file '%s': %s."),
			    name, strerror (errno));
	}

	free (name);

	return fp;
}

static void
page_dump			(vbi_page *		pg)
{
	unsigned int row;

	for (row = 0; row < (unsigned int) pg->rows; ++row) {
		const vbi_char *cp;
		unsigned int column;

		fprintf (stderr, "%2d: >", row);

		cp = pg->text + row * pg->columns;

		for (column = 0; column < (unsigned int) pg->columns;
		     ++column) {
			int c;

			c = cp[column].unicode;
			if (c < 0x20 || c > 0x7E)
				c = '.';

			fputc (c, stderr);
		}

		fputs ("<\n", stderr);
	}
}

#if 2 == VBI_VERSION_MINOR

static void
do_export			(vbi_pgno		pgno,
				 vbi_subno		subno)
{
	vbi_page page;
	vbi_bool success;

	if (option_delay > 1) {
		--option_delay;
		return;
	}

	success = vbi_fetch_vt_page (vbi, &page,
				     pgno, subno,
				     VBI_WST_LEVEL_3p5,
				     /* n_rows */ 25,
				     /* navigation */ TRUE);
	if (!success) {
		/* Shouldn't happen. */
		error_exit (_("Unknown error."));
	}

	if (option_dump_pg) {
		page_dump (&page);
	}

	switch (option_target) {
		char *file_name;
		void *buffer;
		void *buffer2;
		FILE *fp;
		size_t size;
		ssize_t ssize;

	case 1:
		buffer = malloc (1 << 20);
		if (NULL == buffer)
			no_mem_exit ();
		ssize = vbi_export_mem (ex, buffer, 1 << 20, &page);
		success = (ssize >= 0);
		if (success) {
			ssize_t ssize2;

			fp = open_output_file (pgno, subno);
			if (1 != fwrite (buffer, ssize, 1, fp))
				write_error_exit (/* msg: errno */ NULL);
			close_output_file (fp);

			/* Test. */
			ssize2 = vbi_export_mem (ex, buffer, 0, &page);
			assert (ssize == ssize2);
			assert (ssize > 0);
			ssize2 = vbi_export_mem (ex, buffer, ssize - 1, &page);
			assert (ssize == ssize2);
		}
		free (buffer);
		break;

	case 2:
		buffer = NULL;
		buffer2 = vbi_export_alloc (ex, &buffer, &size, &page);
		/* Test. */
		assert (buffer == buffer2);
		success = (NULL != buffer);
		if (success) {
			fp = open_output_file (pgno, subno);
			if (1 != fwrite (buffer, size, 1, fp))
				write_error_exit (/* msg: errno */ NULL);
			close_output_file (fp);
			free (buffer);
		}
		break;

	case 3:
		/* This is the default target. The other cases are only
		   implemented for tests and will be removed when I
		   wrote proper unit tests. */

		fp = open_output_file (pgno, subno);
		success = vbi_export_stdio (ex, fp, &page);
		close_output_file (fp);
		break;

	case 5:
		file_name = output_file_name (pgno, subno);
		success = vbi_export_file (ex, file_name, &page);
		free (file_name);
		break;

	default:
		error_exit ("Invalid target %u.", option_target);
		break;
	}

	if (!success) {
		error_exit (_("Export of page %x failed: %s"),
			    pgno,
			    vbi_export_errstr (ex));
	}

	vbi_unref_page (&page);
}

static void
event_handler			(vbi_event *		ev,
				 void *			user_data)
{
	vbi_pgno pgno;
	vbi_subno subno;

	user_data = user_data; /* unused */

	if (quit)
		return;

	switch (ev->type) {
	case VBI_EVENT_TTX_PAGE:
		pgno = ev->ev.ttx_page.pgno;
		subno = ev->ev.ttx_page.subno;

		if (option_log_mask & VBI_LOG_INFO) {
			fprintf (stderr,
				 "Teletext page %03x.%02x   %c",
				 pgno, subno, cr);
		}

		if (0 == vbi_page_table_num_pages (pt)) {
			do_export (pgno, subno);
		} else if (vbi_page_table_contains_page (pt, pgno)) {
			do_export (pgno, subno);

			if (!option_subtitles) {
				vbi_page_table_remove_page (pt, pgno);

				quit = (0 == vbi_page_table_num_pages (pt));
			}
		}

		break;

	default:
		assert (0);
	}
}

static void
finalize			(void)
{
	/* Nothing to do. */
}

static void
init_vbi_decoder		(void)
{
	vbi_bool success;

	vbi = vbi_decoder_new ();
	if (NULL == vbi)
		no_mem_exit ();

	if (have_option_default_cs) {
		vbi_teletext_set_default_region (vbi, option_default_cs);
	}

	success = vbi_event_handler_add (vbi,
					 VBI_EVENT_TTX_PAGE,
					 event_handler,
					 /* user_data */ NULL);
	if (!success)
		no_mem_exit ();
}

#elif 3 == VBI_VERSION_MINOR

#define ev_timestamp ev->timestamp

extern void
vbi_preselection_dump		(const vbi_preselection *pl,
				 FILE *			fp);

static void
pdc_dump			(vbi_page *		pg)
{
	const vbi_preselection *pl;
	unsigned int size;
	unsigned int i;

	pl = vbi_page_get_preselections (pg, &size);
	assert (NULL != pl);

	for (i = 0; i < size; ++i) {
		fprintf (stderr, "%02u: ", i);
		_vbi_preselection_dump (pl + i, stderr);
		fputc ('\n', stderr);
	}

	if (0 == i)
		fputs ("No PDC data\n", stderr);
}

static vbi_bool
export_link			(vbi_export *		e,
				 void *			user_data,
				 const vbi_link *	link)
{
	vbi_bool success;

	user_data = user_data; /* unused */

	if (0)
		fprintf (stderr, "link text: \"%s\"\n", link->name);

	switch (link->type) {
	case VBI_LINK_HTTP:
	case VBI_LINK_FTP:
	case VBI_LINK_EMAIL:
		success = vbi_export_printf (e, "<a href=\"%s\">%s</a>",
			 link->url, link->name);
		break;

	case VBI_LINK_PAGE:
	case VBI_LINK_SUBPAGE:
		success = vbi_export_printf (e, "<a href=\"%s-%3x-%02x"
					      ".%s\">%s</a>",
					      out_file_name_prefix ?
					      out_file_name_prefix : "ttx",
			 link->pgno,
					      (VBI_ANY_SUBNO == link->subno) ?
					      0 : link->subno,
					      out_file_name_suffix ?
					      out_file_name_suffix : "html",
			 link->name);
		break;

	default:
		success = vbi_export_puts (e, link->name);
		break;
	}

	return success;
}

static vbi_bool
export_pdc			(vbi_export *		e,
				 void *			user_data,
				 const vbi_preselection *pl,
				 const char *		text)
{
	unsigned int end;
	vbi_bool success;

	user_data = user_data; /* unused */

	end = pl->at1_hour * 60 + pl->at1_minute + pl->length;

	/* XXX pl->title uses locale encoding but the html page may not.
	   (export charset parameter) */
	success = vbi_export_printf (e, "<acronym title=\"%04u-%02u-%02u "
		 "%02u:%02u-%02u:%02u "
		 "VPS/PDC: %02u%02u TTX: %x Title: %s"
		 "\">%s</acronym>",
		 pl->year, pl->month, pl->day,
		 pl->at1_hour, pl->at1_minute,
		 (end / 60 % 24), end % 60,
		 pl->at2_hour, pl->at2_minute,
		 pl->_pgno, pl->title, text);
	return success;
}

static void
do_export			(vbi_pgno		pgno,
				 vbi_subno		subno,
				 double			timestamp)
{
	vbi_page *pg;
	vbi_bool success;

	if (option_delay > 1) {
		--option_delay;
		return;
	}

	if (pgno >= 0x100) {
		if (0 != option_override_cs) {
			pg = vbi_decoder_get_page
				(vbi, NULL /* current network */,
				 pgno, subno,
				 VBI_HEADER_ONLY, option_header_only, 
				 VBI_PADDING, option_padding,
				 VBI_PANELS, option_panels,
				 VBI_NAVIGATION, option_navigation,
				 VBI_HYPERLINKS, option_hyperlinks,
				 VBI_PDC_LINKS, option_pdc_links,
				 VBI_WST_LEVEL, VBI_WST_LEVEL_3p5,
				 VBI_OVERRIDE_CHARSET_0, option_override_cs,
				 VBI_END);
		} else {
			pg = vbi_decoder_get_page
				(vbi, NULL /* current network */,
				 pgno, subno,
				 VBI_HEADER_ONLY, option_header_only, 
				 VBI_PADDING, option_padding,
				 VBI_PANELS, option_panels,
				 VBI_NAVIGATION, option_navigation,
				 VBI_HYPERLINKS, option_hyperlinks,
				 VBI_PDC_LINKS, option_pdc_links,
				 VBI_WST_LEVEL, VBI_WST_LEVEL_3p5,
				 VBI_DEFAULT_CHARSET_0, option_default_cs,
				 VBI_END);
		}
	} else {
		pg = vbi_decoder_get_page
			(vbi, NULL /* current network */,
			 pgno, subno,
			 VBI_PADDING, option_padding,
			 VBI_DEFAULT_FOREGROUND, option_default_fg,
			 VBI_DEFAULT_BACKGROUND, option_default_bg,
			 VBI_ROW_CHANGE, option_row_update,
			 VBI_END);
	}

	assert (NULL != pg);

	if (option_dump_pg) {
		page_dump (pg);
	}

	switch (option_target) {
		char *file_name;
		void *buffer;
		void *buffer2;
		FILE *fp;
		size_t size;
		ssize_t ssize;

	case 1:
		buffer = malloc (1 << 20);
		if (NULL == buffer)
			no_mem_exit ();
		ssize = vbi_export_mem (ex, buffer, 1 << 20, pg);
		success = (ssize >= 0);
		if (success) {
			ssize_t ssize2;

			fp = open_output_file (pgno, subno);
			if (1 != fwrite (buffer, ssize, 1, fp))
				write_error_exit (/* msg: errno */ NULL);
			close_output_file (fp);

			/* Test. */
			ssize2 = vbi_export_mem (ex, buffer, 0, pg);
			assert (ssize == ssize2);
			assert (ssize > 0);
			ssize2 = vbi_export_mem (ex, buffer, ssize - 1, pg);
			assert (ssize == ssize2);
		}
		free (buffer);
		break;

	case 2:
		buffer = NULL;
		buffer2 = vbi_export_alloc (ex, &buffer, &size, pg);
		/* Test. */
		assert (buffer == buffer2);
		success = (NULL != buffer);
		if (success) {
			fp = open_output_file (pgno, subno);
			if (1 != fwrite (buffer, size, 1, fp))
				write_error_exit (/* msg: errno */ NULL);
			close_output_file (fp);
			free (buffer);
		}
		break;

	case 3:
		/* This is the default target. The other cases are only
		   implemented for tests and will be removed when I
		   wrote proper unit tests. */

	fp = open_output_file (pgno, subno);

	/* For proper timing of subtitles. */
	vbi_export_set_timestamp (ex, timestamp);

		success = vbi_export_stdio (ex, fp, pg);

		close_output_file (fp);

		break;

	case 5:
		file_name = output_file_name (pgno, subno);
		success = vbi_export_file (ex, file_name, pg);
		free (file_name);
		break;

	default:
		error_exit ("Invalid target %u.", option_target);
		break;
	}

	if (!success) {
		error_exit (_("Export of page %x failed: %s"),
			    pgno,
			    vbi_export_errstr (ex));
	}

	if (option_pdc_enum) {
		pdc_dump (pg);
	}

	vbi_page_delete (pg);
	pg = NULL;
}

static void
update_page_table		(void)
{
	vbi_pgno pgno;

	if (0 == vbi_page_table_num_pages (pt))
		return;

	pgno = 0;

	while (vbi_page_table_next_page (pt, &pgno)) {
		vbi_ttx_page_stat ps;

		if (!vbi_teletext_decoder_get_ttx_page_stat
		    (vbi_decoder_cast_to_teletext_decoder (vbi),
		     &ps, /* nk: current */ NULL, pgno)) {
			continue;
		}

/* XXX what are the defaults in ps until we receive the page inventory? */
		if (VBI_NO_PAGE == ps.page_type) {
			vbi_page_table_remove_page (pt, pgno);
		} else if (0 && ps.subpages > 0) {
			if (!vbi_page_table_contains_all_subpages (pt, pgno))
				vbi_page_table_remove_subpages (pt,
								 pgno,
								 ps.subpages,
								 0x3F7E);
		}
	}

	quit = (0 == vbi_page_table_num_pages (pt));
}

static vbi_bool
event_handler			(const vbi_event *	ev,
				 void *			user_data)
{
	vbi_pgno pgno;
	vbi_subno subno;

	user_data = user_data; /* unused */

	if (quit)
		return TRUE;

	switch (ev->type) {
	case VBI_EVENT_TTX_PAGE:
		pgno = ev->ev.ttx_page.pgno;
		subno = ev->ev.ttx_page.subno;

		if (option_log_mask & VBI_LOG_INFO) {
			fprintf (stderr,
				 "Teletext page %03x.%02x   %c",
				 pgno, subno, cr);
		}

		if (0 == vbi_page_table_num_pages (pt)) {
			do_export (pgno, subno, ev->timestamp);
		} else if (vbi_page_table_contains_page (pt, pgno)) {
			do_export (pgno, subno, ev->timestamp);

			if (!option_subtitles) {
				vbi_page_table_remove_page (pt, pgno);

				quit = (0 == vbi_page_table_num_pages (pt));
			}
		}

		break;

	case VBI_EVENT_CC_PAGE:
		if (option_row_update
		    && !(ev->ev.caption.flags & VBI_ROW_UPDATE))
			break;

		pgno = ev->ev.caption.channel;

		if (option_log_mask & VBI_LOG_INFO) {
			fprintf (stderr,
				 "Caption channel %u   %c",
				 pgno, cr);
		}

		if (pgno != cc_chan)
			break;

		do_export (pgno, VBI_ANY_SUBNO, ev->timestamp);

		break;

	case VBI_EVENT_PAGE_TYPE:
		update_page_table ();
		break;

	default:
		assert (0);
	}

	return TRUE; /* handled */
}

static void
finalize			(void)
{
	const vbi_export_info *xi;

	xi = vbi_export_info_from_export (ex);

	if (xi->open_format && NULL == out_file_name_prefix) {
		if (!vbi_export_stdio (ex, stdout, NULL))
			write_error_exit (vbi_export_errstr (ex));
	}
}

static void
init_vbi_decoder		(void)
{
	vbi_event_mask event_mask;
	vbi_bool success;

	/* XXX videostd? */
	vbi = vbi_decoder_new (/* cache: allocate one */ NULL,
				/* network: current */ NULL,
				VBI_VIDEOSTD_SET_625_50);
	if (NULL == vbi)
		no_mem_exit ();

	vbi_decoder_detect_channel_change (vbi, option_dcc);

	event_mask = (VBI_EVENT_TTX_PAGE |
		      VBI_EVENT_CC_PAGE);
	if (option_fast)
		event_mask |= VBI_EVENT_PAGE_TYPE;

	success = vbi_decoder_add_event_handler (vbi, event_mask,
						  event_handler,
						  /* user_data */ NULL);
	if (!success)
		no_mem_exit ();
}

#else
#  error VBI_VERSION_MINOR == ?
#endif

static vbi_bool
decode_frame			(const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	static vbi_bool have_start_timestamp = FALSE;

	raw = raw; /* unused */
	sp = sp;
	stream_time = stream_time;

	/* To calculate the delay of the first subtitle page. */
	if (!have_start_timestamp) {
		vbi_export_set_timestamp (ex, sample_time);
		have_start_timestamp = TRUE;
	}

	vbi_decoder_feed (vbi, sliced, n_lines, sample_time);

	return !quit;
}

static void
init_export_module		(const char *		module_name)
{
	const vbi_export_info *xi;
	char *errstr;

	errstr = NULL; /* just in case */

	ex = vbi_export_new (module_name, &errstr);
	if (NULL == ex) {
		error_exit (_("Cannot open export module '%s': %s"),
			    module_name, errstr);
		/* NB. free (errstr); here if you don't exit(). */
	}

	if (0 == strncmp (module_name, "html", 4)) {
		if (option_hyperlinks)
			vbi_export_set_link_cb (ex, export_link,
						 /* user_data */ NULL);

		if (option_pdc_links)
			vbi_export_set_pdc_cb (ex, export_pdc,
						/* user_data */ NULL);
	}

	xi = vbi_export_info_from_export (ex);

	if (NULL == xi)
		no_mem_exit ();

	if (NULL == out_file_name_suffix) {
		char *end = NULL;

		out_file_name_suffix = strdup (xi->extension);
		if (NULL == out_file_name_suffix)
			no_mem_exit ();

		out_file_name_suffix = strtok_r (out_file_name_suffix,
						 ",", &end);
	}

#if 3 == VBI_VERSION_MINOR
	if (xi->open_format)
		option_subtitles ^= TRUE;
#endif
}

static void
list_options			(vbi_export *		ex)
{
	const vbi_option_info *oi;
	unsigned int i;

	for (i = 0; (oi = vbi_export_option_info_enum (ex, i)); ++i) {
		char buf[32];

		switch (oi->type) {
		case VBI_OPTION_BOOL:
		case VBI_OPTION_INT:
		case VBI_OPTION_MENU:
			snprintf (buf, sizeof (buf), "%d", oi->def.num);
			break;

		case VBI_OPTION_REAL:
			snprintf (buf, sizeof (buf), "%f", oi->def.dbl);
			break;

		case VBI_OPTION_STRING:
			break;
		}

		if (NULL == oi->tooltip)
			continue;

		if (NULL == oi->tooltip) {
			printf ("  Option '%s' (%s)\n",
				oi->keyword,
				VBI_OPTION_STRING == oi->type ?
				oi->def.str : buf);
		} else {
			printf ("  Option '%s' - %s (%s)\n",
				oi->keyword,
				_(oi->tooltip),
				VBI_OPTION_STRING == oi->type ?
				oi->def.str : buf);
		}

		if (VBI_OPTION_MENU == oi->type) {
			int i;

			for (i = oi->min.num; i <= oi->max.num; ++i) {
				printf ("    %d - %s\n",
					i, _(oi->menu.str[i]));
			}
		}
	}
}

static void
list_modules			(void)
{
	const vbi_export_info *xi;
	unsigned int i;

	for (i = 0; (xi = vbi_export_info_enum (i)); ++i) {
		vbi_export *ex;

		printf ("'%s' - %s\n",
			_(xi->keyword),
			_(xi->tooltip));

		ex = vbi_export_new (xi->keyword, /* errstr */ NULL);
		if (NULL == ex)
			no_mem_exit ();

		list_options (ex);

		vbi_export_delete (ex);
		ex = NULL;
	}
}

static void
usage				(FILE *			fp)
{
	/* FIXME Supposed to be _(localized) but we can't use #ifs
	   within the _() macro. */
	fprintf (fp, "\
%s %s -- Teletext and Closed Caption export utility\n\n\
Copyright (C) 2004, 2005, 2007 Michael H. Schimek\n\
This program is licensed under GPLv2. NO WARRANTIES.\n\n\
Usage: %s [options] format [page number(s)] < sliced vbi data > file\n\
-h | --help | --usage  Print this message and exit\n\
-q | --quiet           Suppress progress and error messages\n\
-v | --verbose         Increase verbosity\n\
-V | --version         Print the program version and exit\n\
Input options:\n\
-i | --input name      Read the VBI data from this file instead of\n\
                       standard input\n\
-P | --pes             Source is a DVB PES stream\n\
-T | --ts pid          Source is a DVB TS stream\n\
Scan options:\n"
#if 3 == VBI_VERSION_MINOR
"-f | --fast            Do not wait for Teletext pages which are\n\
                       currently not in transmission\n"
#endif
"-w | --wait n          Export the second (third, fourth, ...)\n\
                       transmission of the requested page\n"
#if 3 == VBI_VERSION_MINOR
"Formatting options:\n\
-n | --nav             Add TOP or FLOF navigation elements to Teletext\n\
                       pages\n\
-d | --pad             Add an extra column to Teletext pages for a more\n\
                       balanced view, spaces to Closed Caption pages for\n\
                       readability\n"
#endif
"Export options:\n"
#if 3 == VBI_VERSION_MINOR
"-e | --pdc-enum        Print additional Teletext PDC information\n"
#endif
"-g | --dump-pg         For debugging dump the vbi_page being exported\n"
#if 3 == VBI_VERSION_MINOR
"-l | --links           Turn HTTP, SMTP, etc. links on a Teletext page\n\
                       into hyperlinks in HTML output\n"
#endif
"-o | --output name     Write the page to this file instead of standard\n\
                       output. The page number and a suitable .extension\n\
                       will be appended as necessary.\n"
#if 3 == VBI_VERSION_MINOR
"-p | --pdc             Turn PDC markup on a Teletext page into hyperlinks\n\
                       in HTML output\n\
-r | --row-update      Export a Closed Caption page only when a row is\n\
                       complete, not on every new character. Has only an\n\
                       effect on roll-up and paint-on style caption.\n\
-s | --stream          Export all (rather than just one) transmissions\n\
                       of this page to a single file. This is the default\n\
                       for caption/subtitle formats.\n"
#endif
"Formats:\n\
-m | --list            List available output formats and their options.\n\
		       Append options to the format name separated by\n\
                       commas: text,charset=UTF-8\n\
Valid page numbers are:\n"
#if 3 == VBI_VERSION_MINOR
"1 ... 8                Closed Caption channel 1 ... 4, text channel\n\
                       1 ... 4\n"
#endif
"100 ... 899            Teletext page. The program can export multiple\n\
                       Teletext pages: 100 110 200-299. If no page\n\
                       numbers are given it exports all received Teletext\n\
                       pages until it is terminated.\n\
",
		 PROGRAM_NAME, VERSION, program_invocation_name);
}

static const char
short_options [] = "1a:cdefghi:lmno:pqrsvwAB:C:F:H:O:PT:V";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options [] = {
	{ "all-pages",		no_argument,		NULL,	'1' },
	{ "target",		required_argument,	NULL,	'a' },
	{ "dcc",		no_argument,		NULL,	'c' },
	{ "pad",		no_argument,		NULL,	'd' },
	{ "pdc-enum",		no_argument,		NULL,	'e' },
	{ "fast",		no_argument,		NULL,	'f' },
	{ "dump-pg",		no_argument,		NULL,	'g' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "usage",		no_argument,		NULL,	'h' },
	{ "input",		required_argument,	NULL,	'i' },
	{ "links",		no_argument,		NULL,	'l' },
	{ "list",		no_argument,		NULL,	'm' },
	{ "nav",		no_argument,		NULL,	'n' },
	{ "output",		required_argument,	NULL,	'o' },
	{ "pdc",		no_argument,		NULL,	'p' },
	{ "quiet",		no_argument,		NULL,	'q' },
	{ "row-update", 	no_argument,		NULL,	'r' },
	{ "stream",		no_argument,		NULL,	's' },
	{ "verbose",		no_argument,		NULL,	'v' },
	{ "wait",		no_argument,		NULL,	'w' },
	{ "side-panels",	required_argument,	NULL,	'A' },
	{ "default-bg",		required_argument,	NULL,	'B' },
	{ "default-cs",		required_argument,	NULL,	'C' },
	{ "default-fg",		required_argument,	NULL,	'F' },
	{ "header-only",	required_argument,	NULL,	'H' },
	{ "override-cs",	required_argument,	NULL,	'O' },
	{ "pes",		no_argument,		NULL,	'P' },
	{ "ts",			required_argument,	NULL,	'T' },
	{ "version",		no_argument,		NULL,	'V' },
	{ NULL, 0, 0, 0 }
};
#else
#  define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

static int			option_index;

static void
parse_output_option		(void)
{
	assert (NULL != optarg);

	free (out_file_name_prefix);
	out_file_name_prefix = NULL;

	free (out_file_name_suffix);
	out_file_name_suffix = NULL;

	if (0 == strcmp (optarg, "-")) {
		/* Write to stdout. */
	} else {
		char *s;

		s = strrchr (optarg, '.');
		if (NULL == s) {
			out_file_name_prefix = strdup (optarg);
			if (NULL == out_file_name_prefix)
				no_mem_exit ();
		} else {
			out_file_name_prefix = strndup (optarg, s - optarg);
			if (NULL == out_file_name_prefix)
				no_mem_exit ();

			if (0 != s[1]) {
				out_file_name_suffix = strdup (s + 1);
				if (NULL == out_file_name_suffix)
					no_mem_exit ();
			}
		}
	}
}

static vbi_bool
valid_pgno			(vbi_pgno		pgno)
{
	return (vbi_is_bcd (pgno)
		&& pgno >= 0x100
		&& pgno <= 0x899);
}

static void
invalid_pgno_exit		(const char *		arg)
{
	error_exit (_("Invalid page number '%s'."), arg);
}

static void
parse_page_numbers		(unsigned int		argc,
				 char **		argv)
{
	unsigned int i;

	for (i = 0; i < argc; ++i) {
		vbi_pgno first_pgno;
		vbi_pgno last_pgno;
		vbi_bool success;
		const char *s;
		char *end;

		s = argv[i];

		first_pgno = strtoul (s, &end, 16);
		s = end;

		if (first_pgno >= 1 && first_pgno <= 8) {
			if (0 != cc_chan) {
				error_exit (_("Can export only one "
					      "Closed Caption channel."));
			}

			cc_chan = first_pgno;

			while (*s && isspace (*s))
				++s;

			if (0 != *s)
				invalid_pgno_exit (argv[i]);

			continue;
		}

		if (!valid_pgno (first_pgno))
			invalid_pgno_exit (argv[i]);

		last_pgno = first_pgno;

		while (*s && isspace (*s))
			++s;

		if ('-' == *s) {
			++s;

			while (*s && isspace (*s))
				++s;

			last_pgno = strtoul (s, &end, 16);
			s = end;

			if (!valid_pgno (last_pgno))
				invalid_pgno_exit (argv[i]);
		} else if (0 != *s) {
			invalid_pgno_exit (argv[i]);
		}

		success = vbi_page_table_add_pages (pt, first_pgno,
						     last_pgno);
		if (!success)
			no_mem_exit ();
	}
}

int
main				(int			argc,
				 char **		argv)
{
	const char *module_name;
	unsigned int n_pages;
	vbi_bool all_pages;

	init_helpers (argc, argv);

	option_in_file_format = FILE_FORMAT_SLICED;

	option_default_fg = (vbi_rgba) 0xFFFFFF;
	option_default_bg = (vbi_rgba) 0x000000;

	all_pages = FALSE;

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
			/* Compatibility (used to be pgno -1). */
			all_pages = TRUE;
			break;

		case 'a':
			/* For debugging. */
			assert (NULL != optarg);
			option_target = strtoul (optarg, NULL, 0);
			break;

		case 'c':
			option_dcc = TRUE;
			break;

		case 'd':
			option_padding = TRUE;
			break;

		case 'e':
			option_pdc_enum = TRUE;
			break;

		case 'f':
			option_fast = TRUE;
			break;

		case 'g':
			option_dump_pg = TRUE;
			break;

		case 'h':
			usage (stdout);
			exit (EXIT_SUCCESS);

		case 'i':
			assert (NULL != optarg);
			option_in_file_name = optarg;
			break;

		case 'l':
			option_hyperlinks = TRUE;
			break;

		case 'm':
			list_modules ();
			exit (EXIT_SUCCESS);

		case 'n':
			option_navigation = TRUE;
			break;

		case 'o':
			parse_output_option ();
			break;

		case 'p':
			option_pdc_links = TRUE;
			break;

		case 'q':
			parse_option_quiet ();
			break;

		case 'r':
			option_row_update = TRUE;
			break;

		case 's':
			option_subtitles = TRUE;
			break;

		case 'v':
			parse_option_verbose ();
			break;

		case 'w':
			option_delay += 1;
			break;

		case 'A':
			option_panels = TRUE;
			break;

		case 'B':
			assert (NULL != optarg);
			option_default_bg = strtoul (optarg, NULL, 0);
			break;

		case 'C':
			assert (NULL != optarg);
			option_default_cs = strtoul (optarg, NULL, 0);
			have_option_default_cs = TRUE;
			break;

		case 'F':
			assert (NULL != optarg);
			option_default_fg = strtoul (optarg, NULL, 0);
			break;

		case 'H':
			option_header_only = TRUE;
			break;

		case 'O':
			assert (NULL != optarg);
			option_override_cs = strtoul (optarg, NULL, 0);
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

	option_pdc_links |= option_pdc_enum;

	if (argc - optind < 1) {
		usage (stderr);
		exit (EXIT_FAILURE);
	}

	module_name = argv[optind++];

	pt = vbi_page_table_new ();
	if (NULL == pt)
		no_mem_exit ();

	if (all_pages) {
		/* Compatibility. */

		out_file_name_prefix = strdup ("test");
		if (NULL == out_file_name_prefix)
			no_mem_exit ();
	} else {
		parse_page_numbers (argc - optind, &argv[optind]);
	}

	n_pages = vbi_page_table_num_pages (pt);

	if (1 != n_pages && option_delay > 0) {
		error_exit (_("The --wait option requires "
			      "a single page number."));
	}

	if (NULL == out_file_name_prefix) {
		switch (n_pages) {
		case 0: /* all pages? */
			error_exit (_("No page number or "
				      "output file name specified."));
			break;

		case 1: /* one page to stdout */
			break;

		default: /* multiple pages */
			error_exit (_("No output file name specified."));
			break;
		}
	}

	init_export_module (module_name);

	init_vbi_decoder ();

	cr = isatty (STDERR_FILENO) ? '\r' : '\n';

	rst = read_stream_new (option_in_file_name,
			       option_in_file_format,
			       option_in_ts_pid,
			       decode_frame);

	stream_loop (rst);

	stream_delete (rst);
	rst = NULL;

	vbi_decoder_delete (vbi);
	vbi = NULL;

	finalize ();

	free (out_file_name_prefix);
	out_file_name_prefix = NULL;

	free (out_file_name_suffix);
	out_file_name_suffix = NULL;

	if (!option_subtitles) {
		n_pages = vbi_page_table_num_pages (pt);

		if (1 == n_pages) {
			vbi_pgno pgno = 0;

			vbi_page_table_next_page (pt, &pgno);

			error_exit (_("End of stream. Page %03x not found."),
				    pgno);
		} else if (n_pages > 0) {
			error_exit (_("End of stream. %u pages not found."));
		}
	}

	vbi_page_table_delete (pt);
	pt = NULL;

	vbi_export_delete (ex);
	ex = NULL;

	exit (EXIT_SUCCESS);
}
