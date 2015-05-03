/*
 *  libzvbi test
 *
 *  Copyright (C) 2000, 2001 Michael H. Schimek
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

/* $Id: caption.c,v 1.20 2009/12/14 23:43:49 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#ifndef X_DISPLAY_MISSING

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include "src/vbi.h"
#include "src/exp-gfx.h"
#include "src/hamm.h"
#include "src/dvb_demux.h"
#include "src/cc608_decoder.h"
#include "sliced.h"

#define PROGRAM_NAME "caption"

static const char *		option_in_file_name;
static enum file_format		option_in_file_format;
static unsigned int		option_in_ts_pid;
static vbi_bool			option_use_cc608_decoder;
static vbi_bool			option_use_cc608_event;
static double			option_frame_rate;

static struct stream *		rst;

/* For real time playback of recorded streams. */
static double			wait_until;
static double			frame_period;

/* Teletext/CC/VPS/WSS decoder. */
static vbi_decoder *		vbi;

/* Closed Caption decoder. */
static _vbi_cc608_decoder *	cd;


#define WINDOW_WIDTH	640
#define WINDOW_HEIGHT	480

/* Character cell size. */
#define CELL_WIDTH	16
#define CELL_HEIGHT	26

/* Maximum text size in characters. TEXT_COLUMNS includes two spaces
   added for legibility. */
#define TEXT_COLUMNS	34
#define TEXT_ROWS	15

/* Maximum text size in pixels, not counting the vertical offset for
   smooth rolling. */
#define TEXT_WIDTH	(TEXT_COLUMNS * CELL_WIDTH)
#define TEXT_HEIGHT	(TEXT_ROWS * CELL_HEIGHT)

static Display *		display;
static int			screen;
static Colormap			cmap;
static Window			window;
static GC			gc;
static uint8_t *		ximgdata;

/* Color of the "video pixels" we overlay. */
static const vbi_rgba		video_color = 0x80FF80; /* 0xBBGGRR */
static XColor			video_xcolor;

/* Color of the border around the text, if any. */
static const vbi_rgba		border_color = 0xFF8080;
static XColor			border_xcolor;

/* Display and ximage color depth: 15, 16, 24, 32 bits */
static unsigned int		color_depth;

static XImage *			ximage;

/* Current vertical offset for smooth rolling, 0 ... CELL_HEIGHT - 1. */
static unsigned int		vert_offset;

/* Draw the ximage into the window. */
static vbi_bool			update_display;

/* The currently displayed page. */
static vbi_page			curr_page;

/* Draw characters with flash attribute in on or off state. */
static vbi_bool			flash_on;
static unsigned int		flash_count;

/* Get a new copy of the current page from the Caption decoder and
   redraw the ximage. */
static vbi_bool			redraw_page;


/* Switches. */

/* Currently selected Caption channel. */
static vbi_pgno			channel;

/* Add spaces for legibility. */
static vbi_bool			padding;

/* Show a border around the text area. */
static vbi_bool			show_border;

static vbi_bool			smooth_rolling;


static void
put_image			(void)
{
	unsigned int x;
	unsigned int y;
	unsigned int width;

	/* 32 or with padding 34 columns. */
	width = curr_page.columns * CELL_WIDTH;

	x = (WINDOW_WIDTH - width) / 2;

	/* vert_offset is between 0 ... CELL_HEIGHT - 1. */
	y = vert_offset + (WINDOW_HEIGHT
			   - (TEXT_HEIGHT + CELL_HEIGHT)) / 2;

	if (show_border)
		XSetForeground (display, gc, border_xcolor.pixel);
	else
		XSetForeground (display, gc, video_xcolor.pixel);

	XFillRectangle (display, (Drawable) window, gc,
			/* x, y */ 0, 0,
			WINDOW_WIDTH, /* height */ y);

	XFillRectangle (display, (Drawable) window, gc,
			/* x, y */ 0, y,
			x, TEXT_HEIGHT);

	XPutImage (display, window, gc, ximage,
		   /* src_x, src_y */ 0, 0,
		   /* dest_x, dest_y */ x, y,
		   width, TEXT_HEIGHT);

	XFillRectangle (display, (Drawable) window, gc,
			/* x, y */ x + width, y,
			x, TEXT_HEIGHT);

	XFillRectangle (display, (Drawable) window, gc,
			/* x, y */ 0, y + TEXT_HEIGHT,
			WINDOW_WIDTH, WINDOW_HEIGHT - (y + TEXT_HEIGHT));
}

static void
draw_transparent_spaces		(unsigned int		column,
				 unsigned int		row,
				 unsigned int		n_columns)
{
	uint8_t *d;
	const uint8_t *s;
	unsigned int i;
	unsigned int j;

	switch (color_depth) {
	case 32: /* assumed to be B G R A in memory */
		d = ximgdata + column * CELL_WIDTH * 4
			+ row * CELL_HEIGHT * TEXT_WIDTH * 4;

		for (j = 0; j < CELL_HEIGHT; ++j) {
			uint32_t *d32 = (uint32_t *) d;

			for (i = 0; i < n_columns * CELL_WIDTH; ++i)
				d32[i] = video_xcolor.pixel;

			d += TEXT_WIDTH * 4;
		}

		break;

	case 24: /* assumed to be B G R in memory */
		d = ximgdata + column * CELL_WIDTH * 3
			+ row * CELL_HEIGHT * TEXT_WIDTH * 3;

		s = (const uint8_t *) &video_xcolor.pixel;
		if (Z_BYTE_ORDER == Z_BIG_ENDIAN)
			s += sizeof (video_xcolor.pixel) - 3;

		for (j = 0; j < CELL_HEIGHT; ++j) {
			for (i = 0; i < n_columns * CELL_WIDTH; ++i)
				memcpy (d + i * 3, s, 3);

			d += TEXT_WIDTH * 3;
		}

		break;

	case 16: /* assumed to be gggbbbbb rrrrrggg in memory */
	case 15: /* assumed to be gggbbbbb arrrrrgg in memory */
		d = ximgdata + column * CELL_WIDTH * 2
			+ row * CELL_HEIGHT * TEXT_WIDTH * 2;

		for (j = 0; j < CELL_HEIGHT; ++j) {
			uint16_t *d16 = (uint16_t *) d;

			for (i = 0; i < n_columns * CELL_WIDTH; ++i)
				d16[i] = video_xcolor.pixel;

			d += TEXT_WIDTH * 2;
		}

		break;

	default:		
		assert (0);
	}
}

static void
draw_character			(vbi_page *		pg,
				 unsigned int		column,
				 unsigned int		row)
{
	vbi_rgba buffer[CELL_WIDTH * CELL_HEIGHT];
	const vbi_rgba *s;
	uint8_t *p;
	unsigned int i;
	unsigned int j;

	/* Regrettably at the moment this function supports only one
	   pixel format. */
	vbi_draw_cc_page_region (pg, VBI_PIXFMT_RGBA32_LE, buffer,
				 /* bytes_per_line */
				 sizeof (buffer) / CELL_HEIGHT,
				 column, row,
				 /* n_columns, n_rows */ 1, 1);

	/* Alpha blending. */

	if (option_use_cc608_decoder) {
		for (i = 0; i < CELL_WIDTH * CELL_HEIGHT; ++i) {
			uint8_t alpha;

			/* (text * alpha + video * (0xFF - alpha)) / 0xFF. */

			alpha = buffer[i] >> 24;

			if (alpha >= 0xFF) {
				/* Most likely case, nothing to do. */
			} else if (0 == alpha) {
				buffer[i] = video_color;
			} else {
				unsigned int text;

				/* There are really just three alpha levels:
				   0x00, 0x80, and 0xFF so we take a (not
				   quite correct) shortcut. */

				text = (((buffer[i] & 0xFF00FF)
					 + (video_color & 0xFF00FF))
					>> 1) & 0xFF00FF;
				text |= (((buffer[i] & 0xFF00)
					  + (video_color & 0xFF00))
					 >> 1) & 0xFF00;
				buffer[i] = text;
			}
		}
	}

	s = buffer;

	switch (color_depth) {
	case 32: /* assumed to be B G R A in memory */
		p = ximgdata + column * CELL_WIDTH * 4
			+ row * CELL_HEIGHT * TEXT_WIDTH * 4;

		for (j = 0; j < CELL_HEIGHT; ++j) {
			for (i = 0; i < CELL_WIDTH; ++i) {
				p[i * 4 + 0] = *s >> 16;
				p[i * 4 + 1] = *s >> 8;
				p[i * 4 + 2] = *s;
				p[i * 4 + 3] = 0xFF;
				++s;
			}

			p += TEXT_WIDTH * 4;
		}

		break;

	case 24: /* assumed to be B G R in memory */
		p = ximgdata + column * CELL_WIDTH * 3
			+ row * CELL_HEIGHT * TEXT_WIDTH * 3;

		for (j = 0; j < CELL_HEIGHT; ++j) {
			for (i = 0; i < CELL_WIDTH; ++i) {
				p[i * 3 + 0] = *s >> 16;
				p[i * 3 + 1] = *s >> 8;
				p[i * 3 + 2] = *s;
				++s;
			}

			p += TEXT_WIDTH * 3;
		}

		break;

	case 16: /* assumed to be gggbbbbb rrrrrggg in memory */
		p = ximgdata + column * CELL_WIDTH * 2
			+ row * CELL_HEIGHT * TEXT_WIDTH * 2;

		for (j = 0; j < CELL_HEIGHT; ++j) {
			for (i = 0; i < CELL_WIDTH; ++i) {
				unsigned int n;

				n = (*s >> 19) & 0x001F;
				n |= (*s >> 5) & 0x07E0;
				n |= (*s << 8) & 0xF800;
				++s;
				p[i * 2 + 0] = n;
				p[i * 2 + 1] = n >> 8;

			}

			p += TEXT_WIDTH * 2;
		}

		break;

	case 15: /* assumed to be gggbbbbb arrrrrgg in memory */
		p = ximgdata + column * CELL_WIDTH * 2
			+ row * CELL_HEIGHT * TEXT_WIDTH * 2;

		for (j = 0; j < CELL_HEIGHT; ++j) {
			for (i = 0; i < CELL_WIDTH; ++i) {
				unsigned int n;

				n = (*s >> 19) & 0x001F;
				n |= (*s >> 6) & 0x03E0;
				n |= (*s << 7) & 0x7C00;
				n |= 0x8000;
				++s;
				p[i * 2 + 0] = n;
				p[i * 2 + 1] = n >> 8;

			}

			p += TEXT_WIDTH * 2;
		}

		break;
	}
}

static void
draw_row			(vbi_page *		pg,
				 unsigned int		row)
{
	const vbi_char *cp;
	unsigned int n_tspaces;
	int column;

	cp = pg->text + row * pg->columns;

	n_tspaces = 0;

	for (column = 0; column < pg->columns; ++column) {
		if (VBI_TRANSPARENT_SPACE == cp[column].opacity) {
			++n_tspaces;
			continue;
		}

		if (n_tspaces > 0) {
			draw_transparent_spaces (column - n_tspaces,
						 row, n_tspaces);
			n_tspaces = 0;
		}

		draw_character (pg, column, row);
	}

	if (n_tspaces > 0) {
		draw_transparent_spaces (column - n_tspaces,
					 row, n_tspaces);
	}
}

static vbi_bool
same_text			(vbi_page *		pg1,
				 unsigned int		row1,
				 vbi_page *		pg2,
				 unsigned int		row2)
{
	if (pg1->columns != pg2->columns)
		return FALSE;

	return (0 == memcmp (pg1->text + row1 * pg1->columns,
			     pg2->text + row2 * pg2->columns,
			     pg1->columns * sizeof (pg1->text[0])));
}

static void
new_draw_page			(vbi_page *		pg)
{
	int row;

	assert (0 == pg->dirty.y0);
	assert ((pg->rows - 1) == pg->dirty.y1);

	for (row = 0; row < pg->rows; ++row) {
		if (same_text (&curr_page, row, pg, row)) {
			continue;
		} else if (same_text (&curr_page, row + 1, pg, row)) {
			unsigned int row_size;

			/* A shortcut for roll-up caption. */

			row_size = TEXT_WIDTH * CELL_HEIGHT
				* color_depth / 8;

			memmove (ximgdata + row * row_size,
				 ximgdata + (row + 1) * row_size,
				 row_size);
		} else {
			draw_row (pg, row);
		}
	}

	curr_page = *pg;
}

static void
old_draw_page			(vbi_page *		pg)
{
	int row;

	for (row = pg->dirty.y0; row <= pg->dirty.y1; ++row)
		draw_row (pg, row);

	/* For put_image(). */
	curr_page.columns = pg->columns;
}

static void
old_roll_up			(unsigned int		first_row,
				 unsigned int		last_row)
{
	unsigned int row_size;

	/* In the window first_row ... last_row shift all rows up by
	   one row (may be faster than redrawing all characters). */

	assert (first_row < last_row);
	assert (last_row < TEXT_ROWS);

	row_size = TEXT_WIDTH * CELL_HEIGHT * color_depth / 8;

	memmove (ximgdata + first_row * row_size,
		 ximgdata + (first_row + 1) * row_size,
		 (last_row - first_row) * row_size);
}

static void
old_clear_display		(void)
{
	unsigned int row;

	for (row = 0; row < TEXT_ROWS; ++row) {
		draw_transparent_spaces (/* column */ 0, row,
					 TEXT_COLUMNS);
	}
}

static void
get_and_draw_page		(void)
{
	vbi_page page;
	vbi_bool success;

	if (option_use_cc608_decoder) {
		success = _vbi_cc608_decoder_get_page (cd, &page,
						       channel, padding);
	} else {
		success = vbi_fetch_cc_page (vbi, &page, channel,
					     /* reset dirty flags */ TRUE);
	}

	assert (success);

	if (!flash_on) {
		int i;

		for (i = 0; i < page.rows * page.columns; ++i) {
			if (page.text[i].flash) {
				page.text[i].foreground =
					page.text[i].background;
			}
		}
	}

	if (option_use_cc608_decoder) {
		new_draw_page (&page);
	} else {
		old_draw_page (&page);
	}
}

static void
event_handler			(vbi_event *		ev,
				 void *			user_data)
{
	vbi_page page;
	vbi_bool success;

	user_data = user_data;

	switch (ev->type) {
	case VBI_EVENT_CAPTION:
		if (channel != ev->ev.caption.pgno)
			return;

		success = vbi_fetch_cc_page (vbi, &page, channel,
					     /* reset dirty flags */ TRUE);
		assert (success);

		if (abs (page.dirty.roll) > page.rows) {
			old_clear_display ();
			update_display = TRUE;
		} else if (page.dirty.roll == -1) {
			old_roll_up (page.dirty.y0, page.dirty.y1);
			if (smooth_rolling) {
				vert_offset = CELL_HEIGHT
					- 2; /* field lines */
			}
			update_display = TRUE;
		} else {
			old_draw_page (&page);
			update_display = TRUE;
		}

		break;

	case _VBI_EVENT_CC608:
		if (channel != ev->ev._cc608->channel)
			return;

		success = _vbi_cc608_decoder_get_page (cd, &page,
						       channel, padding);
		assert (success);

		/* XXX Perhaps the decoder should pass a roll_offset,
		   first_row, last_row, cc_mode? */
		if (smooth_rolling
		    && 0 != (ev->ev._cc608->flags
			     & _VBI_CC608_START_ROLLING)) {
			vert_offset = CELL_HEIGHT - 2; /* field lines */
		}

		new_draw_page (&page);

		update_display = TRUE;

		break;

	default:
		assert (0);
	}
}

static void
x_event				(void)
{
	while (XPending (display)) {
		XEvent event;

		XNextEvent (display, &event);

		switch (event.type) {
		case KeyPress:
		{
			int c = XLookupKeysym (&event.xkey, 0);

			switch (c) {
			case 'b':
				show_border ^= TRUE;
				update_display = TRUE;
				break;

			case 'c':
			case 'q':
				exit (EXIT_SUCCESS);

			case 'p':
				padding ^= TRUE;
				redraw_page = TRUE;
				break;

			case 's':
				smooth_rolling ^= TRUE;
				if (vert_offset > 0) {
					vert_offset = 0;
					update_display = TRUE;
				}
				break;

			case '1' ... '8':
				channel = c - '1' + VBI_CAPTION_CC1;
				vert_offset = 0;
				redraw_page = TRUE;
				break;

			case XK_F1 ... XK_F8:
				channel = c - XK_F1 + VBI_CAPTION_CC1;
				vert_offset = 0;
				redraw_page = TRUE;
				break;
			}

			break;
		}

		case Expose:
			update_display = TRUE;
			break;

		case ClientMessage:
			/* WM_DELETE_WINDOW. */
			exit (EXIT_SUCCESS);
		}
	}

	if (redraw_page) {
		get_and_draw_page ();
		redraw_page = FALSE;
		update_display = TRUE;
	}

	if (update_display) {
		put_image ();
		update_display = FALSE;
	}

	if (0 == flash_count) {
		flash_on ^= 1;
		flash_count = flash_on ? 20 : 10;
		redraw_page = TRUE;
	} else {
		--flash_count;
	}

	if (vert_offset > 0) {
		vert_offset -= 2 /* field lines */;
		update_display = TRUE;
	}
}

static void
alloc_color			(XColor *		xc,
				 vbi_rgba		rgba)
{
	xc->red   = VBI_R (rgba) * 0x0101;
	xc->green = VBI_G (rgba) * 0x0101;
	xc->blue  = VBI_B (rgba) * 0x0101;

	XAllocColor (display, cmap, xc);
}

static void
init_window			(int			ac,
				 char **		av)
{
	Atom delete_window_atom;
	XWindowAttributes wa;
	unsigned int row;
	unsigned int image_size;

	ac = ac; /* unused */
	av = av;

	display = XOpenDisplay (NULL);
	if (NULL == display) {
		error_exit ("Cannot open X display.");
	}

	screen = DefaultScreen (display);
	cmap = DefaultColormap (display, screen);

	alloc_color (&video_xcolor, video_color);
	alloc_color (&border_xcolor, border_color);

	assert (TEXT_WIDTH <= WINDOW_WIDTH);
	assert (TEXT_HEIGHT <= WINDOW_HEIGHT);

	window = XCreateSimpleWindow (display,
				      RootWindow (display, screen),
				      /* x, y */ 0, 0,
				      WINDOW_WIDTH, WINDOW_HEIGHT,
				      /* borderwidth */ 2,
				      /* border color */ video_xcolor.pixel,
				      /* bg color */ video_xcolor.pixel);
	if (0 == window) {
		error_exit ("Cannot open X window.");
	}

	XGetWindowAttributes (display, window, &wa);

	/* FIXME determine the R/B order and endianess.
	   Currently we assume lsb == blue, little endian. */
	color_depth = wa.depth;

	switch (wa.depth) {
	case 32:
	case 24:
	case 16:
	case 15:
		break;

	default:
		error_exit ("Sorry, this program cannot run "
			    "on a screen with color depth %u.",
			    wa.depth);
	}

	image_size = TEXT_WIDTH * TEXT_HEIGHT * wa.depth / 8;

	ximgdata = malloc (image_size);
	if (NULL == ximgdata) {
		no_mem_exit ();
	}

	for (row = 0; row < TEXT_ROWS; ++row) {
		draw_transparent_spaces (/* column */ 0, row,
					 TEXT_COLUMNS);
	}

	ximage = XCreateImage (display,
			       DefaultVisual (display, screen),
			       DefaultDepth (display, screen),
			       /* format */ ZPixmap,
			       /* x offset */ 0,
			       (char *) ximgdata,
			       TEXT_WIDTH, TEXT_HEIGHT,
			       /* bitmap_pad */ 8,
			       /* bytes_per_line: contiguous */ 0);
	if (NULL == ximage) {
		no_mem_exit ();
	}

	delete_window_atom = XInternAtom (display, "WM_DELETE_WINDOW",
					  /* only_if_exists */ False);

	XSelectInput (display, window, (KeyPressMask |
					ExposureMask |
					StructureNotifyMask));
	XSetWMProtocols (display, window,
			 &delete_window_atom, /* n_atoms */ 1);
	XStoreName (display, window,
		    "Caption Test - [B|P|Q|S|F1..F8]");

	gc = XCreateGC (display, window,
			/* valuemask */ 0,
			/* values */ NULL);

	XMapWindow (display, window);
	       
	XSync (display, /* discard events */ False);
}

static vbi_bool
decode_frame			(const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	raw = raw;
	sp = sp;

	if (option_frame_rate < 1e9) {
		for (;;) {
			struct timeval tv;
			double now;

			gettimeofday (&tv, /* tz */ NULL);
			now = tv.tv_sec + tv.tv_usec * (1 / 1e6);

			if (now >= wait_until) {
				if (wait_until <= 0.0)
					wait_until = now;

				wait_until += 1 / option_frame_rate;

				break;
			}

			usleep ((wait_until - now) * 1e6);
		}
	}

	if (option_use_cc608_decoder) {
		_vbi_cc608_decoder_feed_frame (cd, sliced, n_lines,
					       sample_time, stream_time);
	} else {
		vbi_decode (vbi, (vbi_sliced *) sliced, n_lines, sample_time);
	}

	x_event ();

	return TRUE;
}

static void
usage				(FILE *			fp)
{
	fprintf (fp, _("\
%s %s\n\n\
Copyright (C) 2000, 2001, 2007, 2008, 2009 Michael H. Schimek\n\
This program is licensed under GPLv2 or later. NO WARRANTIES.\n\n\
Usage: %s [options] < sliced VBI data\n\
-h | --help | --usage  Print this message and exit\n\
-i | --input name      Read the VBI data from this file instead\n\
                       of standard input\n\
-c | --cc608-decoder   Use new cc608_decoder\n\
-P | --pes             Source is a DVB PES stream\n\
-T | --ts pid          Source is a DVB TS stream\n\
-V | --version         Print the program version and exit\n\
"),
		 PROGRAM_NAME, VERSION, program_invocation_name);
}

static const char
short_options [] = "cehi:r:PT:V";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options [] = {
	{ "cc608-decoder",	no_argument,		NULL,	'c' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "usage",		no_argument,		NULL,	'h' },
	{ "cc608-event",	no_argument,		NULL,	'e' },
	{ "input",		required_argument,	NULL,	'i' },
	{ "frame-rate",		required_argument,	NULL,	'r' },
	{ "pes",		no_argument,		NULL,	'P' },
	{ "ts",			required_argument,	NULL,	'T' },
	{ "version",		no_argument,		NULL,	'V' },
	{ NULL, 0, 0, 0 }
};
#else
#  define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

static int			option_index;

int
main				 (int			argc,
				 char **		argv)
{
	vbi_bool success;

	init_helpers (argc, argv);

	option_in_file_format = FILE_FORMAT_SLICED;
	option_frame_rate = 1e9;

	for (;;) {
		int c;

		c = getopt_long (argc, argv, short_options,
				 long_options, &option_index);
		if (-1 == c)
			break;

		switch (c) {
		case 0: /* getopt_long() flag */
			break;

		case 'c':
			option_use_cc608_decoder = TRUE;
			break;

		case 'h':
			usage (stdout);
			exit (EXIT_SUCCESS);

		case 'i':
			assert (NULL != optarg);
			option_in_file_name = optarg;
			break;

		case 'e':
			option_use_cc608_event = TRUE;
			break;

		case 'r':
			assert (NULL != optarg);
			option_frame_rate = strtod (optarg, NULL);
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

	init_window (argc, argv);

	if (option_use_cc608_decoder) {
		/* Note this is an experimental module, the interface
		   and output may change. */

		cd = _vbi_cc608_decoder_new ();
		if (NULL == cd)
			no_mem_exit ();

		success = _vbi_cc608_decoder_add_event_handler
			(cd, _VBI_EVENT_CC608,
			 event_handler, /* user_data */ NULL);
		if (!success)
			no_mem_exit ();
	} else {
		unsigned int event_mask;

		/* Teletext/CC/VPS/WSS decoder. */
		vbi = vbi_decoder_new ();
		if (NULL == vbi)
			no_mem_exit ();

		if (option_use_cc608_event) {
			error_exit ("Not implemented yet.\n");
			event_mask = _VBI_EVENT_CC608;
		} else {
			event_mask = VBI_EVENT_CAPTION;
		}

		success = vbi_event_handler_add (vbi, event_mask,
						 event_handler,
						 /* used_data */ NULL);
		if (!success)
			no_mem_exit ();
	}

	/* Switches. */
	channel = VBI_CAPTION_CC1;
	padding = TRUE;
	show_border = FALSE;
	smooth_rolling = TRUE;

	rst = read_stream_new (option_in_file_name,
			       option_in_file_format,
			       option_in_ts_pid,
			       decode_frame);

	wait_until = 0;
	frame_period = 1 / option_frame_rate;

	stream_loop (rst);

	stream_delete (rst);

	error_msg (_("End of stream."));

	for (;;) {
		x_event ();
		usleep (33333);
	}

	_vbi_cc608_decoder_delete (cd);
	vbi_decoder_delete (vbi);

	exit (EXIT_SUCCESS);
}

#else /* X_DISPLAY_MISSING */

int
main				(int			argc,
				 char **		argv)
{
	printf ("Not compiled with X11 support.\n");
	exit(EXIT_FAILURE);
}

#endif
