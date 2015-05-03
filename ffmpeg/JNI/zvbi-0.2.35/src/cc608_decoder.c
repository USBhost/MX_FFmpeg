/*
 *  libzvbi - EIA 608-B Closed Caption decoder
 *
 *  Copyright (C) 2008 Michael H. Schimek
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the 
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 *  Boston, MA  02110-1301  USA.
 */

/* $Id: cc608_decoder.c,v 1.2 2009/12/14 23:43:23 mschimek Exp $ */

/* This code is experimental and not yet part of the library.
   Tests pending. */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "hamm.h"
#include "lang.h"
#include "conv.h"
#include "format.h"
#include "event-priv.h"
#include "cc608_decoder.h"

#ifndef CC608_DECODER_LOG_INPUT
#  define CC608_DECODER_LOG_INPUT 0
#endif

enum field_num {
	FIELD_1 = 0,
	FIELD_2,
	MAX_FIELDS
};

#define UNKNOWN_CHANNEL 0
#define MAX_CHANNELS 8

/* 47 CFR 15.119 (d) Screen format. */

#define FIRST_ROW		0
#define LAST_ROW		14
#define MAX_ROWS		15

#define ALL_ROWS_MASK ((1 << MAX_ROWS) - 1)

/* Note these are visible columns. We also buffer a zeroth column
   which is implied by 47 CFR 15.119 and EIA 608-B to set the default
   or PAC attributes for column one, and visible as a solid space if
   padding for legibility is enabled. We do not reserve a 33rd column
   for padding since format_row() can just append a space to the
   output. */
#define FIRST_COLUMN		1
#define LAST_COLUMN		32
#define MAX_COLUMNS		32

struct timestamp {
	/* System time when the event occured, zero if no event
	   occured yet. */
	double			sys;

	/* ISO 13818-1 Presentation Time Stamp of the event. Unit is
	   1/90000 second. Only the 33 least significant bits are
	   valid. < 0 if no event occured yet. */
	int64_t			pts;
};

struct channel {
	/**
	 * [0] and [1] are the displayed and non-displayed buffer as
	 * defined in 47 CFR 15.119, and selected by @a
	 * displayed_buffer below. [2] is a snapshot of the displayed
	 * buffer at the last stream event.
	 *
	 * XXX Text channels don't need buffer[2] and buffer[3], we're
	 * wasting memory.
	 */
	uint16_t		buffer[3][MAX_ROWS][1 + MAX_COLUMNS];

	/**
	 * For buffer[0 ... 2], if bit 1 << row is set this row
	 * contains displayable characters, spacing or non-spacing
	 * attributes. (Special character 0x1139 "transparent space"
	 * is not a displayable character.) This information is
	 * intended to speed up copying, erasing and formatting.
	 */
	unsigned int		dirty[3];

	/** Index of the displayed buffer, 0 or 1. */
	unsigned int		displayed_buffer;

	/**
	 * Cursor position: FIRST_ROW ... LAST_ROW and
	 * FIRST_COLUMN ... LAST_COLUMN.
	 */
	unsigned int		curr_row;
	unsigned int		curr_column;

	/**
	 * Text window height in VBI_CC608_MODE_ROLL_UP. The top row
	 * of the window is curr_row - window_rows + 1, the bottom row
	 * is curr_row.
	 *
	 * Note: curr_row - window_rows + 1 may be < FIRST_ROW, this
	 * must be clipped before using window_rows:
	 *
	 * actual_rows = MIN (curr_row - FIRST_ROW + 1, window_rows);
	 *
	 * We won't do that at the RUx command because usually a PAC
	 * command follows which may change curr_row.
	 */
	unsigned int		window_rows;

	/* Most recently received PAC command. */
	unsigned int		last_pac;

	/**
	 * This variable counts successive transmissions of the
	 * letters A to Z. It is reset to zero upon reception of any
	 * letter a to z.
	 *
	 * Some stations do not transmit EIA 608-B extended characters
	 * and except for N with tilde the standard and special
	 * character sets contain only lower case accented
	 * characters. We force these characters to upper case if this
	 * variable indicates live caption, where the text is usually
	 * all upper case.
	 */
	unsigned int		uppercase_predictor;

	/** Current caption mode or VBI_CC608_MODE_UNKNOWN. */
	_vbi_cc608_mode		mode;

	/**
	 * The time when we last received data for this
	 * channel. Intended to detect if this caption channel is
	 * active.
	 */
	struct timestamp	timestamp;

	/**
	 * The time when we received the first (but not necessarily
	 * leftmost) character in the current row. Unless the mode is
	 * VBI_CC608_MODE_POP_ON the next stream event will carry this
	 * timestamp.
	 */
	struct timestamp	timestamp_c0;
};

struct _vbi_cc608_decoder {
	/**
	 * Decoder state. We decode all channels in parallel, this way
	 * clients can switch between channels without data loss or
	 * capture multiple channels with a single decoder instance.
	 *
	 * Also 47 CFR 15.119 and EIA 608-C require us to remember the
	 * cursor position on each channel.
	 */
	struct channel		channel[MAX_CHANNELS];

	/**
	 * Current channel, switched by caption control codes. Can be
	 * one of @c VBI_CAPTION_CC1 (1) ... @c VBI_CAPTION_CC4 (4) or
	 * @c VBI_CAPTION_T1 (5) ... @c VBI_CAPTION_T4 (8) or @c
	 * UNKNOWN_CHANNEL (0) if no channel number was received yet.
	 */
	vbi_pgno		curr_ch_num[MAX_FIELDS];

	/**
	 * Caption control codes (two bytes) may repeat once for error
	 * correction. -1 if no repeated control code can be expected.
	 */
	int			expect_ctrl[MAX_FIELDS][2];

	/**
	 * Receiving XDS data, as opposed to caption / ITV data.
	 * There's no XDS data on the first field, we just use an
	 * array for convenience.
	 */
	vbi_bool		in_xds[MAX_FIELDS];

	/**
	 * Pointer into the channel[] array if a display update event
	 * shall be sent at the end of this iteration, @c NULL
	 * otherwise. Purpose is to suppress an event for the first of
	 * two displayable characters in a caption byte pair.
	 */
	struct channel *	event_pending;

	/**
	 * Remembers past parity errors: One bit for each call of
	 * vbi_cc608_decoder_feed(), most recent result in lsb. The
	 * idea is to disable the decoder if we detect too many
	 * errors.
	 */
	unsigned int		error_history;

	/**
	 * The time when we last received data, including NUL bytes.
	 * Intended to detect if the station transmits any data on
	 * line 21 or 284.
	 */
	struct timestamp	timestamp;

	_vbi_event_handler_list	handlers;
};

/* 47 CFR 15.119 Mid-Row Codes, Preamble Address Codes.
   EIA 608-B Table 3. */
static const vbi_color
color_map [8] = {
	VBI_WHITE, VBI_GREEN, VBI_BLUE, VBI_CYAN,
	VBI_RED, VBI_YELLOW, VBI_MAGENTA,

	/* Note Mid-Row Codes interpret this value as "Italics"; PACs
	   as "White Italics"; Background Attributes as "Black". */
	VBI_BLACK
};

/* 47 CFR 15.119 Preamble Address Codes. */
static const int8_t
pac_row_map [16] = {
	/* 0 */ 10,			/* 0x1040 */
	/* 1 */ -1,			/* no function */
	/* 2 */ 0, 1, 2, 3,		/* 0x1140 ... 0x1260 */
	/* 6 */ 11, 12, 13, 14,		/* 0x1340 ... 0x1460 */
	/* 10 */ 4, 5, 6, 7, 8, 9	/* 0x1540 ... 0x1760 */
};

/** @internal */
void
_vbi_cc608_dump			(FILE *			fp,
				 unsigned int		c1,
				 unsigned int		c2)
{
	const vbi_bool to_upper = FALSE;
	const int repl_char = '?';
	uint16_t ucs2_str[2];
	unsigned int c;
	unsigned int f;
	unsigned int u;

	assert (NULL != fp);

	fprintf (fp, "%02X%02X %02X%c%02X%c",
		 c1 & 0xFF, c2 & 0xFF,
		 c1 & 0x7F, vbi_unpar8 (c1) < 0 ? '*' : ' ',
		 c2 & 0x7F, vbi_unpar8 (c2) < 0 ? '*' : ' ');

	/* Note we ignore wrong parity. */
	c1 &= 0x7F;
	c2 &= 0x7F;

	if (0 == c1) {
		fputs (" null\n", fp);
		return;
	} else if (c1 < 0x10) {
		if (0x0F == c1)
			fputs (" XDS packet end\n", fp);
		else
			fputs (" XDS packet start/continue\n", fp);
		return;
	} else if (c1 >= 0x20) {
		unsigned int i = 0;

		fputs (" '", fp);
		ucs2_str[i++] = vbi_caption_unicode (c1, to_upper);
		if (c2 >= 0x20) {
			ucs2_str[i++] = vbi_caption_unicode (c2, to_upper);
		}
		vbi_fputs_iconv_ucs2 (fp, vbi_locale_codeset (),
				      ucs2_str, i, repl_char);
		fprintf (fp, "'%s\n",
			 (c2 > 0 && c2 < 0x20) ? " invalid" : "");
		return;
	}

	/* Some common bits. */
	c = (c1 >> 3) & 1; /* channel */
	f = c1 & 1; /* field */
	u = c2 & 1; /* underline */

	if (c2 < 0x20) {
		fputs (" invalid\n", fp);
		return;
	} else if (c2 >= 0x40) {
		unsigned int rrrr;
		unsigned int xxx;
		int row;

		/* Preamble Address Codes -- 001 crrr  1ri xxxu */
  
		rrrr = (c1 & 7) * 2 + ((c2 & 0x20) > 0);
		xxx = (c2 >> 1) & 7;

		row = pac_row_map [rrrr];
		if (c2 & 0x10) {
			fprintf (fp, " PAC ch=%u row=%d column=%u u=%u\n",
				 c, row, xxx * 4, u);
		} else {
			fprintf (fp, " PAC ch=%u row=%d color=%u u=%u\n",
				 c, row, xxx, u);
		}

		return;
	}

	/* Control codes -- 001 caaa  01x bbbu */

	switch (c1 & 0x07) {
	case 0:
		if (c2 < 0x30) {
			static const char mnemo [16 * 4] =
				"BWO\0BWS\0BGO\0BGS\0"
				"BBO\0BBS\0BCO\0BCS\0"
				"BRO\0BRS\0BYO\0BYS\0"
				"BMO\0BMS\0BAO\0BAS";

			/* Backgr. Attr. Codes -- 001 c000  010 xxxt */

			fprintf (fp, " %s ch=%u\n",
				 mnemo + (c2 & 0xF) * 4, c);
			return;
		}
		break;

	case 1:
		if (c2 < 0x30) {
			unsigned int xxx;

			/* Mid-Row Codes -- 001 c001  010 xxxu */

			xxx = (c2 >> 1) & 7;
			fprintf (fp, " mid-row ch=%u color=%u u=%u\n",
				 c, xxx, u);
		} else {
			/* Special Characters -- 001 c001  011 xxxx */

			fprintf (fp, " special character ch=%u '", c);
			ucs2_str[0] = vbi_caption_unicode
				(0x1100 | c2, to_upper);
			vbi_fputs_iconv_ucs2 (fp, vbi_locale_codeset (),
					      ucs2_str, 1, repl_char);
			fputs ("'\n", fp);
		}
		return;

	case 2: /* first group */
	case 3: /* second group */
		/* Extended Character Set -- 001 c01x  01x xxxx */

		fprintf (fp, " extended character ch=%u '", c);
		ucs2_str[0] = vbi_caption_unicode (c1 * 256 + c2, to_upper);
		vbi_fputs_iconv_ucs2 (fp, vbi_locale_codeset (),
				      ucs2_str, 1, repl_char);
		fputs ("'\n", fp);
		return;

	case 4: /* f = 0 */
	case 5: /* f = 1 */
		if (c2 < 0x30) {
			static const char mnemo [16 * 4] =
				"RCL\0BS \0AOF\0AON\0"
				"DER\0RU2\0RU3\0RU4\0"
				"FON\0RDC\0TR \0RTD\0"
				"EDM\0CR \0ENM\0EOC";

			/* Misc. Control Codes -- 001 c10f  010 xxxx */

			fprintf (fp, " %s ch=%u f=%u\n",
				 mnemo + (c2 & 0xF) * 4, c, f);
			return;
		}
		break;

	case 6: /* reserved */
		break;

	case 7:
		switch (c2) {
		case 0x21:
		case 0x22:
		case 0x23:
			fprintf (fp, " TO%u ch=%u\n", c2 - 0x20, c);
			return;

		case 0x2D:
			fprintf (fp, " BT ch=%u\n", c);
			return;

		case 0x2E:
			fprintf (fp, " FA ch=%u\n", c);
			return;

		case 0x2F:
			fprintf (fp, " FAU ch=%u\n", c);
			return;

		default:
			break;
		}
		break;
	}

	fprintf (fp, " unknown\n");
}

/* Future stuff. */
enum {
	VBI_UNDERLINE	= (1 << 0),
	VBI_ITALIC	= (1 << 2),
	VBI_FLASH	= (1 << 3)
};

_vbi_inline void
vbi_char_copy_attr		(struct vbi_char *	cp1,
				 const struct vbi_char *cp2,
				 unsigned int		attr)
{
	if (attr & VBI_UNDERLINE)
		cp1->underline = cp2->underline;
	if (attr & VBI_ITALIC)
		cp1->italic = cp2->italic;
	if (attr & VBI_FLASH)
		cp1->flash = cp2->flash;
}

_vbi_inline void
vbi_char_clear_attr		(struct vbi_char *	cp,
				 unsigned int		attr)
{
	if (attr & VBI_UNDERLINE)
		cp->underline = 0;
	if (attr & VBI_ITALIC)
		cp->italic = 0;
	if (attr & VBI_FLASH)
		cp->flash = 0;
}

_vbi_inline void
vbi_char_set_attr		(struct vbi_char *	cp,
				 unsigned int		attr)
{
	if (attr & VBI_UNDERLINE)
		cp->underline = 1;
	if (attr & VBI_ITALIC)
		cp->italic = 1;
	if (attr & VBI_FLASH)
		cp->flash = 1;
}

_vbi_inline unsigned int
vbi_char_has_attr		(const struct vbi_char *cp,
				 unsigned int		attr)
{
	attr &= (VBI_UNDERLINE | VBI_ITALIC | VBI_FLASH);

	if (0 == cp->underline)
		attr &= ~VBI_UNDERLINE;
	if (0 == cp->italic)
		attr &= ~VBI_ITALIC;
	if (0 == cp->flash)
		attr &= ~VBI_FLASH;

	return attr;
}

_vbi_inline unsigned int
vbi_char_xor_attr		(const struct vbi_char *cp1,
				 const struct vbi_char *cp2,
				 unsigned int		attr)
{
	attr &= (VBI_UNDERLINE | VBI_ITALIC | VBI_FLASH);

	if (0 == (cp1->underline ^ cp2->underline))
		attr &= ~VBI_UNDERLINE;
	if (0 == (cp1->italic ^ cp2->italic))
		attr &= ~VBI_ITALIC;
	if (0 == (cp1->flash ^ cp2->flash))
		attr &= ~VBI_FLASH;

	return attr;
}

static void
timestamp_reset			(struct timestamp *	ts)
{
	ts->sys = 0.0;
	ts->pts = -1;
}

static vbi_bool
timestamp_is_set		(const struct timestamp *ts)
{
	return (ts->pts >= 0 || ts->sys > 0.0);
}

static vbi_pgno
channel_num			(const _vbi_cc608_decoder *cd,
				 const struct channel *	ch)
{
	return (ch - cd->channel) + 1;
}

/* This implementation of character attributes is based on 47 CFR
   15.119 (h) and the following sections of EIA 608-B:

   EIA 608-B Annex C.7 "Preamble Address Codes and Tab Offsets
   (Regulatory/Preferred)": "In general, Preamble Address Codes (PACs)
   have no immediate effect on the display. A major exception is the
   receipt of a PAC during roll-up captioning. In that case, if the
   base row designated in the PAC is not the same as the current base
   row, the display shall be moved immediately to the new base
   row. [...] An indenting PAC carries the attributes of white,
   non-italicized, and it sets underlining on or off. Tab Offset
   commands do not change these attributes. If an indenting PAC with
   underline ON is received followed by a Tab Offset and by text, the
   text shall be underlined (except as noted below). When a
   displayable character is received, it is deposited at the current
   cursor position. If there is already a displayable character in the
   column immediately to the left, the new character assumes the
   attributes of that character. The new character may be arriving as
   the result of an indenting PAC (with or without a Tab Offset), and
   that PAC may designate other attributes, but the new character is
   forced to assume the attributes of the character immediately to its
   left, and the PAC's attributes are ignored. If, when a displayable
   character is received, it overwrites an existing PAC or mid-row
   code, and there are already characters to the right of the new
   character, these existing characters shall assume the same
   attributes as the new character. This adoption can result in a
   whole caption row suddenly changing color, underline, italics,
   and/or flash attributes."

   EIA 608-B Annex C.14 "Special Cases Regarding Attributes
   (Normative)": "In most cases, Preamble Address Codes shall set
   attributes for the caption elements they address. It is
   theoretically possible for a service provider to use an indenting
   PAC to start a row at Column 5 or greater, and then to use
   Backspace to move the cursor to the left of the PAC into an area to
   which no attributes have been assigned. It is also possible for a
   roll-up row, having been created by a Carriage Return, to receive
   characters with no PAC used to set attributes. In these cases, and
   in any other case where no explicit attributes have been assigned,
   the display shall be white, non-underlined, non-italicized, and
   non-flashing. In case new displayable characters are received
   immediately after a Delete to End of Row (DER), the display
   attributes of the first deleted character shall remain in effect if
   there is a displayable character to the left of the cursor;
   otherwise, the most recently received PAC shall set the display
   attributes."

   47 CFR 15.119 (n) clarifies that Special Character "transparent
   space" is not a "displayable character". */

/**
 * @internal
 * @param to_upper Convert the lower case Latin characters in the
 *   standard character set to upper case.
 * @param padding Add spaces around words for improved legibility
 *   as defined in 47 CFR 15.119. If @c TRUE the resulting page will
 *   be 34 columns wide, otherwise 32 columns. The height is always 15
 *   rows.
 * @param alpha Add an offset to the vbi_color of characters: +0 for
 *   opaque, +8 for translucent, +16 for transparent characters. Intended
 *   for formatting with an alpha color map.
 */
static void
format_row			(struct vbi_char *	cp,
				 unsigned int		max_columns,
				 const struct channel *	ch,
				 unsigned int		buffer,
				 unsigned int		row,
				 vbi_bool		to_upper,
				 vbi_bool		padding,
				 vbi_bool		alpha)
{
	struct vbi_char ac;
	struct vbi_char ac_ts;
	vbi_char *end;
	unsigned int i;

	/* 47 CFR 15.119 (h)(1). EIA 608-B Section 6.4. */
	CLEAR (ac);
	ac.opacity = VBI_OPAQUE;
	ac.foreground = VBI_WHITE;
	ac.background = VBI_BLACK;

	ac_ts = ac;
	ac_ts.unicode = 0x20;
	ac_ts.opacity = VBI_TRANSPARENT_SPACE;
	if (alpha) {
		ac_ts.foreground += 16;
		ac_ts.background += 16;
	}

	end = cp + MAX_COLUMNS;
	if (padding)
		end += 2;

	assert (end <= cp + max_columns);

	/* Shortcut. */
	if (0 == (ch->dirty[buffer] & (1 << row))) {
		while (cp < end)
			*cp++ = ac_ts;

		return;
	}

	if (padding) {
		*cp++ = ac_ts;
	}

	for (i = FIRST_COLUMN - 1; i <= LAST_COLUMN; ++i) {
		unsigned int c;

		ac.unicode = 0x20;

		c = ch->buffer[buffer][row][i];
		if (0 == c) {
			if (padding
			    && VBI_TRANSPARENT_SPACE != cp[-1].opacity
			    && 0x20 != cp[-1].unicode) {
				/* Append a space with the same colors
				   and opacity (opaque or transp.
				   backgr.) as the text to the left of
				   it. */
				*cp++ = ac;
				/* We don't underline spaces, see
				   below. */
				vbi_char_clear_attr (cp - 1, -1);
			} else if (i > 0) {
				*cp++ = ac;
				cp[-1].opacity = VBI_TRANSPARENT_SPACE;
				if (alpha) {
					cp[-1].foreground =
						16 + (ac.foreground & 7);
					cp[-1].background =
						16 + (ac.background & 7);
				}
			}

			continue;
		} else if (c < 0x1020) {
			if (padding
			    && VBI_TRANSPARENT_SPACE == cp[-1].opacity) {
				/* Prepend a space with the same
				   colors and opacity (opaque or
				   transp. backgr.) as the text to the
				   right of it. */
				cp[-1] = ac;
				/* We don't underline spaces, see
				   below. */
				vbi_char_clear_attr (cp - 1, -1);
			}

			if ((c >= 'a' && c <= 'z')
			    || 0x7E == c /* n with tilde */) {
				/* We do not force these characters to
				   upper case because the standard
				   character set includes upper case
				   versions of these characters and
				   lower case was probably
				   deliberately transmitted. */
				ac.unicode = vbi_caption_unicode
					(c, /* to_upper */ FALSE);
			} else {
				ac.unicode = vbi_caption_unicode
					(c, to_upper);
			}
		} else if (c < 0x1040) {
			unsigned int color;

			/* Backgr. Attr. Codes -- 001 c000  010 xxxt */
			/* EIA 608-B Section 6.2. */

			/* This is a set-at spacing attribute. */

			color = (c >> 1) & 7;
			ac.background = color_map[color];

			if (c & 0x0001) {
				if (alpha)
					ac.background += 8;
				ac.opacity = VBI_SEMI_TRANSPARENT;
			} else {
				ac.opacity = VBI_OPAQUE;
			}
		} else if (c < 0x1120) {
			/* Preamble Address Codes -- 001 crrr  1ri xxxu */

			/* PAC is a non-spacing attribute and only
			   stored in the buffer at the addressed
			   column minus one if it replaces a
			   transparent space (EIA 608-B Annex C.7,
			   C.14). There's always a transparent space
			   to the left of the first column but we show
			   this zeroth column only if padding is
			   enabled. */
			if (padding
			    && VBI_TRANSPARENT_SPACE != cp[-1].opacity
			    && 0x20 != cp[-1].unicode) {
				/* See 0 == c. */
				*cp++ = ac;
				vbi_char_clear_attr (cp - 1, -1);
			} else if (i > 0) {
				*cp++ = ac;
				cp[-1].opacity = VBI_TRANSPARENT_SPACE;
				if (alpha) {
					cp[-1].foreground =
						16 + (ac.foreground & 7);
					cp[-1].background =
						16 + (ac.background & 7);
				}
			}

			vbi_char_clear_attr (&ac, VBI_UNDERLINE | VBI_ITALIC);
			if (c & 0x0001)
				vbi_char_set_attr (&ac, VBI_UNDERLINE);
			if (c & 0x0010) {
				ac.foreground = VBI_WHITE;
			} else {
				unsigned int color;

				color = (c >> 1) & 7;
				if (7 == color) {
					ac.foreground = VBI_WHITE;
					vbi_char_set_attr (&ac, VBI_ITALIC);
				} else {
					ac.foreground = color_map[color];
				}
			}

			continue;
		} else if (c < 0x1130) {
			unsigned int color;

			/* Mid-Row Codes -- 001 c001  010 xxxu */
			/* 47 CFR 15.119 Mid-Row Codes table,
			   (h)(1)(ii), (h)(1)(iii). */

			/* 47 CFR 15.119 (h)(1)(i), EIA 608-B Section
			   6.2: Mid-Row codes, FON, BT, FA and FAU are
			   set-at spacing attributes. */

			vbi_char_clear_attr (&ac, -1);
			if (c & 0x0001)
				vbi_char_set_attr (&ac, VBI_UNDERLINE);
			color = (c >> 1) & 7;
			if (7 == color) {
				vbi_char_set_attr (&ac, VBI_ITALIC);
			} else {
				ac.foreground = color_map[color];
			}
		} else if (c < 0x1220) {
			/* Special Characters -- 001 c001  011 xxxx */
			/* 47 CFR 15.119 Character Set Table. */

			if (padding
			    && VBI_TRANSPARENT_SPACE == cp[-1].opacity) {
				cp[-1] = ac;
				vbi_char_clear_attr (cp - 1, -1);
			}

			assert (0x1139 /* transparent space */ != c);
			ac.unicode = vbi_caption_unicode (c, to_upper);
		} else if (c < 0x1428) {
			/* Extended Character Set -- 001 c01x  01x xxxx */
			/* EIA 608-B Section 6.4.2 */

			if (padding
			    && VBI_TRANSPARENT_SPACE == cp[-1].opacity) {
				cp[-1] = ac;
				vbi_char_clear_attr (cp - 1, -1);
			}

			/* We do not force these characters to upper
			   case because the extended character set
			   includes upper case versions of all letters
			   and lower case was probably deliberately
			   transmitted. */
			ac.unicode = vbi_caption_unicode
				(c, /* to_upper */ FALSE);

			if (0x2500 == (ac.unicode & 0xFFE0)) {
				/* Box drawing characters probably
				   shouldn't have these attributes. */
				*cp++ = ac;
				vbi_char_clear_attr (cp - 1,
						     (VBI_ITALIC |
						      VBI_UNDERLINE));
				continue;
			}
		} else if (c < 0x172D) {
			/* FON Flash On -- 001 c10f  010 1000 */
			/* 47 CFR 15.119 (h)(1)(iii). */

			vbi_char_set_attr (&ac, VBI_FLASH);
		} else if (c < 0x172E) {
			/* BT Background Transparent -- 001 c111  010 1101 */
			/* EIA 608-B Section 6.4. */

			ac.opacity = VBI_TRANSPARENT_FULL;
			if (alpha) {
				ac.background = 16 + (ac.background & 7);
			}
		} else if (c <= 0x172F) {
			/* FA Foreground Black -- 001 c111  010 111u */
			/* EIA 608-B Section 6.4. */

			vbi_char_clear_attr (&ac, -1);
			if (c & 0x0001)
				vbi_char_set_attr (&ac, VBI_UNDERLINE);
			ac.foreground = VBI_BLACK;
		}

		*cp++ = ac;

		/* 47 CFR 15.119 and EIA 608-B are silent about
		   underlined spaces, but considering the example in
		   47 CFR (h)(1)(iv) which would produce something
		   ugly like "__text" I suppose we should not
		   underline them. For good measure we also clear the
		   invisible italic and flash attribute. */
		if (0x20 == ac.unicode)
			vbi_char_clear_attr (cp - 1, -1);
	}

	if (padding) {
		ac.unicode = 0x20;
		vbi_char_clear_attr (&ac, -1);

		if (VBI_TRANSPARENT_SPACE != cp[-1].opacity
		    && 0x20 != cp[-1].unicode) {
			*cp++ = ac;
		} else {
			ac.opacity = VBI_TRANSPARENT_SPACE;
			ac.foreground =	16 + (ac.foreground & 7);
			ac.background =	16 + (ac.background & 7);
			*cp++ = ac;
		}
	}

	assert (cp == end);
}

#ifndef VBI_RGBA
#  define VBI_RGBA(r, g, b)						\
	((((r) & 0xFF) << 0) | (((g) & 0xFF) << 8)			\
	 | (((b) & 0xFF) << 16) | (0xFF << 24))
#endif

/**
 * @param cd Caption decoder allocated with vbi_cc608_decoder_new().
 * @param pg The display state will be stored here.
 * @param channel Caption channel @c VBI_CHANNEL_CC1 ...
 *   @c VBI_CHANNEL_CC4 or @c VBI_CHANNEL_T1 ... @c VBI_CHANNEL_T4.
 * @param padding Add spaces around words for improved legibility
 *   as defined in 47 CFR 15.119. If @c TRUE the resulting page will
 *   be 34 columns wide, otherwise 32 columns. The height is always 15
 *   rows.
 *
 * This function stores the current display state of the given caption
 * channel in the @a pg structure. (There is no channel switch
 * function; all channels are decoded simultaneously.)
 *
 * All fields of @a pg will be initialized but the @a vbi, @a nuid, @a
 * dirty, @a nav_link, @a nav_index, and @a font fields will not
 * contain useful information.
 *
 * @returns
 * @c FALSE on failure: The channel number is out of bounds.
 */
vbi_bool
_vbi_cc608_decoder_get_page	(_vbi_cc608_decoder *	cd,
				 vbi_page *		pg,
				 vbi_pgno		channel,
				 vbi_bool		padding)
{
	static const vbi_rgba default_color_map [3 * 8] = {
		0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF,
		0xFFFF0000, 0xFFFF00FF, 0xFFFFFF00, 0xFFFFFFFF,

		0x80000000, 0x800000FF, 0x8000FF00, 0x8000FFFF,
		0x80FF0000, 0x80FF00FF, 0x80FFFF00, 0x80FFFFFF,

		0x00000000, 0x000000FF, 0x0000FF00, 0x0000FFFF,
		0x00FF0000, 0x00FF00FF, 0x00FFFF00, 0x00FFFFFF,
	};
	vbi_char *cp;
	struct channel *ch;
	unsigned int row;
	vbi_bool to_upper;

	assert (NULL != cd);
	assert (NULL != pg);

	if (channel < VBI_CAPTION_CC1 || channel > VBI_CAPTION_T4)
		return FALSE;

	ch = &cd->channel[channel - VBI_CAPTION_CC1];

	CLEAR (*pg);

	pg->pgno = channel;

	pg->rows = MAX_ROWS;

	if (padding)
		pg->columns = MAX_COLUMNS + 2;
	else
		pg->columns = MAX_COLUMNS;

	assert (N_ELEMENTS (pg->text) >= MAX_ROWS * (MAX_COLUMNS + 2));

	pg->dirty.y1 = LAST_ROW;

	pg->screen_opacity = VBI_TRANSPARENT_SPACE;

	assert (sizeof (pg->color_map) >= sizeof (default_color_map));
	memcpy (pg->color_map, default_color_map,
		sizeof (default_color_map));

	cp = pg->text;

	to_upper = (ch->uppercase_predictor > 3);

	for (row = 0; row < MAX_ROWS; ++row) {
		format_row (cp, pg->columns,
			    ch, ch->displayed_buffer,
			    row, to_upper, padding,
			    /* alpha */ TRUE);

		cp += pg->columns;
	}

	return TRUE;
}

static void
display_event			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 _vbi_cc608_event_flags	flags)
{
	vbi_event ev;
	struct _vbi_event_cc608_page cc608;

	CLEAR (ev);

	ev.type = _VBI_EVENT_CC608;
	ev.ev._cc608 = &cc608;
	cc608.channel = channel_num (cd, ch);
	cc608.mode = ch->mode;
	cc608.flags = flags;

	_vbi_event_handler_list_send (&cd->handlers, &ev);
}

/* This decoder is mainly designed to overlay caption onto live video,
   but to create transcripts we also offer an event every time a line
   of caption is complete. The event occurs when certain control codes
   are received:

   In POP_ON mode we send the event upon reception of EOC, which swaps
   the displayed and non-displayed memory.

   In ROLL_UP and TEXT mode captioners are not expected to display new
   text by erasing and overwriting a row with PAC, TOx, BS and DER so
   we do not send an event on reception of these codes. In ROLL_UP
   mode CR, EDM, EOC, RCL and RDC complete a line: CR moves the cursor
   to a new row, EDM erases the displayed memory. The remaining codes
   switch to POP_ON or PAINT_ON mode. In TEXT mode CR and TR are our
   line completion indicators. CR works as above and TR erases the
   displayed memory. EDM, EOC, RDC, RCL and RUx have no effect on Text
   channels according to EIA 608.

   In PAINT_ON mode RDC never erases the displayed memory and CR has
   no function. Instead captioners can freely position the cursor and
   erase or overwrite (parts of) rows with PAC, TOx, BS and DER, or
   erase all rows with EDM. We send an event on PAC, EDM, EOC, RCL and
   RUx, provided the characters (including spacing attributes) in the
   current row changed since the last event: PAC is the only control
   code which can move the cursor to the left and/or to a new row, and
   likely to introduce a new line. EOC, RCL and RUx switch to POP_ON
   or ROLL_UP mode. */

static void
stream_event			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 unsigned int		first_row,
				 unsigned int		last_row)
{
	vbi_event ev;
	struct _vbi_event_cc608_stream cc608_stream;
	unsigned int row;
	vbi_bool to_upper;

	CLEAR (ev);

	ev.type = _VBI_EVENT_CC608_STREAM;
	ev.ev._cc608_stream = &cc608_stream;
	cc608_stream.capture_time = ch->timestamp_c0.sys;
	cc608_stream.pts = ch->timestamp_c0.pts;
	cc608_stream.channel = channel_num (cd, ch);
	cc608_stream.mode = ch->mode;

	to_upper = (ch->uppercase_predictor > 3);

	for (row = first_row; row <= last_row; ++row) {
		unsigned int end;

		format_row (cc608_stream.text,
			    N_ELEMENTS (cc608_stream.text),
			    ch, ch->displayed_buffer,
			    row, to_upper,
			    /* padding */ FALSE,
			    /* alpha */ FALSE);

		for (end = N_ELEMENTS (cc608_stream.text);
		     end > 0; --end) {
			if (VBI_TRANSPARENT_SPACE
			    != cc608_stream.text[end - 1].opacity)
				break;
		}

		if (0 == end)
			continue;

		_vbi_event_handler_list_send (&cd->handlers, &ev);
	}

	timestamp_reset (&ch->timestamp_c0);
}

static void
put_char			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 int			c,
				 vbi_bool		displayable,
				 vbi_bool		backspace)
{
	uint16_t *text;
	unsigned int curr_buffer;
	unsigned int row;
	unsigned int column;

	/* 47 CFR Section 15.119 (f)(1), (f)(2), (f)(3). */
	curr_buffer = ch->displayed_buffer
		^ (_VBI_CC608_MODE_POP_ON == ch->mode);

	row = ch->curr_row;
	column = ch->curr_column;

	if (unlikely (backspace)) {
		/* 47 CFR 15.119 (f)(1)(vi), (f)(2)(ii),
		   (f)(3)(i). EIA 608-B Section 6.4.2, 7.4. */
		if (column > FIRST_COLUMN)
			--column;
	} else {
		/* 47 CFR 15.119 (f)(1)(v), (f)(1)(vi), (f)(2)(ii),
		   (f)(3)(i). EIA 608-B Section 7.4. */
		if (column < LAST_COLUMN)
			ch->curr_column = column + 1;
	}

	text = &ch->buffer[curr_buffer][row][0];
	text[column] = c;

	/* Send a display update event when the displayed buffer of
	   the current channel changed, but no more than once for each
	   pair of Closed Caption bytes. */
	/* XXX This may not be a visible change, but such cases are
	   rare and we'd probably need a function almost as complex as
	   format_row() to find out. */
	if (_VBI_CC608_MODE_POP_ON != ch->mode) {
		cd->event_pending = ch;
	}

	if (likely (displayable)) {
		/* EIA 608-B Annex C.7, C.14. */
		if (FIRST_COLUMN == column
		    || 0 == text[column - 1]) {
			/* Note last_pac may be 0 as well. */
			text[column - 1] = ch->last_pac;
		}

		if (c >= 'a' && c <= 'z') {
			ch->uppercase_predictor = 0;
		} else if (c >= 'A' && c <= 'Z') {
			unsigned int up;

			up = ch->uppercase_predictor + 1;
			if (up > 0)
				ch->uppercase_predictor = up;
		}
	} else if (unlikely (0 == c)) {
		unsigned int i;

		/* This is special character "transparent space". */

		for (i = FIRST_COLUMN; i <= LAST_COLUMN; ++i)
			c |= ch->buffer[curr_buffer][row][i];

		ch->dirty[curr_buffer] &= ~((0 == c) << row);

		return;
	} else {
		/* This is a spacing attribute. */

		/* EIA 608-B Annex C.7, C.14. */
		if (FIRST_COLUMN == column
		    || 0 == text[column - 1]) {
			/* Note last_pac may be 0 as well. */
			text[column - 1] = ch->last_pac;
		}
	}

	assert (sizeof (ch->dirty[0]) * 8 - 1 >= MAX_ROWS);
	ch->dirty[curr_buffer] |= 1 << row;

	if (!timestamp_is_set (&ch->timestamp_c0)) {
		ch->timestamp_c0 = cd->timestamp;
	}
}

static void
ext_control_code		(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 unsigned int		c2)
{
	unsigned int column;

	switch (c2) {
	case 0x21: /* TO1 */
	case 0x22: /* TO2 */
	case 0x23: /* TO3 Tab Offset -- 001 c111  010 00xx */
		/* 47 CFR 15.119 (e)(1)(ii). EIA 608-B Section 7.4,
		   Annex C.7. */
		column = ch->curr_column + (c2 & 3);
		ch->curr_column = MIN (column,
				       (unsigned int) LAST_COLUMN);
		break;

	case 0x24: /* Select standard character set in normal size */
	case 0x25: /* Select standard character set in double size */
	case 0x26: /* Select first private character set */
	case 0x27: /* Select second private character set */
	case 0x28: /* Select character set GB 2312-80 (Chinese) */
	case 0x29: /* Select character set KSC 5601-1987 (Korean) */
	case 0x2A: /* Select first registered character set. */
		/* EIA 608-B Section 6.3 Closed Group Extensions. */
		break;

	case 0x2D: /* BT Background Transparent -- 001 c111  010 1101 */
	case 0x2E: /* FA Foreground Black -- 001 c111  010 1110 */
	case 0x2F: /* FAU Foregr. Black Underl. -- 001 c111  010 1111 */
		/* EIA 608-B Section 6.2. */
		put_char (cd, ch, 0x1700 | c2,
			  /* displayable */ FALSE,
			  /* backspace */ TRUE);
		break;

	default:
		/* 47 CFR Section 15.119 (j): Ignore. */
		break;
	}
}

/* Send a stream event if the current row has changed since the last
   stream event. This is necessary in paint-on mode where CR has no
   function and captioners can freely position the cursor to erase or
   overwrite (parts of) rows. */
static void
stream_event_if_changed		(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;
	unsigned int i;

	curr_buffer = ch->displayed_buffer;
	row = ch->curr_row;

	if (0 == (ch->dirty[curr_buffer] & (1 << row)))
		return;

	for (i = FIRST_COLUMN; i <= LAST_COLUMN; ++i) {
		unsigned int c1;
		unsigned int c2;

		c1 = ch->buffer[curr_buffer][row][i];
		if (c1 >= 0x1040) {
			if (c1 < 0x1120) {
				c1 = 0; /* PAC -- non-spacing */
			} else if (c1 < 0x1130 || c1 >= 0x1428) {
				/* MR, FON, BT, FA, FAU -- spacing */
				c1 = 0x20;
			}
		}

		c2 = ch->buffer[2][row][i];
		if (c2 >= 0x1040) {
			if (c2 < 0x1120) {
				c2 = 0;
			} else if (c2 < 0x1130 || c2 >= 0x1428) {
				c1 = 0x20;
			}
		}

		if (c1 != c2) {
			stream_event (cd, ch, row, row);

			memcpy (ch->buffer[2][row],
				ch->buffer[curr_buffer][row],
				sizeof (ch->buffer[0][0]));

			ch->dirty[2] = ch->dirty[curr_buffer];

			return;
		}
	}
}

static void
end_of_caption			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;

	/* EOC End Of Caption -- 001 c10f  010 1111 */

	curr_buffer = ch->displayed_buffer;

	switch (ch->mode) {
	case _VBI_CC608_MODE_UNKNOWN:
	case _VBI_CC608_MODE_POP_ON:
		break;

	case _VBI_CC608_MODE_ROLL_UP:
		row = ch->curr_row;
		if (0 != (ch->dirty[curr_buffer] & (1 << row)))
			stream_event (cd, ch, row, row);
		break;

	case _VBI_CC608_MODE_PAINT_ON:
		stream_event_if_changed (cd, ch);
		break;

	case _VBI_CC608_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}

	ch->displayed_buffer = curr_buffer ^= 1;

	/* 47 CFR Section 15.119 (f)(2). */
	ch->mode = _VBI_CC608_MODE_POP_ON;

	if (0 != ch->dirty[curr_buffer]) {
		ch->timestamp_c0 = cd->timestamp;

		stream_event (cd, ch,
			      FIRST_ROW,
			      LAST_ROW);

		display_event (cd, ch, /* flags */ 0);
	}
}

static void
carriage_return			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;
	unsigned int window_rows;
	unsigned int first_row;

	/* CR Carriage Return -- 001 c10f  010 1101 */

	curr_buffer = ch->displayed_buffer;
	row = ch->curr_row;

	switch (ch->mode) {
	case _VBI_CC608_MODE_UNKNOWN:
		return;

	case _VBI_CC608_MODE_ROLL_UP:
		/* 47 CFR Section 15.119 (f)(1)(iii). */
		ch->curr_column = FIRST_COLUMN;

		/* 47 CFR 15.119 (f)(1): "The cursor always remains on
		   the base row." */

		/* XXX Spec? */
		ch->last_pac = 0;

		/* No event if the buffer contains only
		   TRANSPARENT_SPACEs. */
		if (0 == ch->dirty[curr_buffer])
			return;

		window_rows = MIN (row + 1 - FIRST_ROW,
				   ch->window_rows);
		break;

	case _VBI_CC608_MODE_POP_ON:
	case _VBI_CC608_MODE_PAINT_ON:
		/* 47 CFR 15.119 (f)(2)(i), (f)(3)(i): No effect. */
		return;

	case _VBI_CC608_MODE_TEXT:
		/* 47 CFR Section 15.119 (f)(1)(iii). */
		ch->curr_column = FIRST_COLUMN;

		/* XXX Spec? */
		ch->last_pac = 0;

		/* EIA 608-B Section 7.4: "When Text Mode has
		   initially been selected and the specified Text
		   memory is empty, the cursor starts at the topmost
		   row, Column 1, and moves down to Column 1 on the
		   next row each time a Carriage Return is received
		   until the last available row is reached. A variety
		   of methods may be used to accomplish the scrolling,
		   provided that the text is legible while moving. For
		   example, as soon as all of the available rows of
		   text are on the screen, Text Mode switches to the
		   standard roll-up type of presentation." */

		if (LAST_ROW != row) {
			if (0 != (ch->dirty[curr_buffer] & (1 << row))) {
				stream_event (cd, ch, row, row);
			}

			ch->curr_row = row + 1;

			return;
		}

		/* No event if the buffer contains all
		   TRANSPARENT_SPACEs. */
		if (0 == ch->dirty[curr_buffer])
			return;

		window_rows = MAX_ROWS;

		break;
	}

	/* 47 CFR Section 15.119 (f)(1)(iii). */

	if (0 != (ch->dirty[curr_buffer] & (1 << row))) {
		stream_event (cd, ch, row, row);
	}

	first_row = row + 1 - window_rows;
	memmove (ch->buffer[curr_buffer][first_row],
		 ch->buffer[curr_buffer][first_row + 1],
		 (window_rows - 1) * sizeof (ch->buffer[0][0]));

	ch->dirty[curr_buffer] >>= 1;

	memset (ch->buffer[curr_buffer][row], 0,
		sizeof (ch->buffer[0][0]));

	/* See the description of VBI_CC608_START_ROLLING and
	   test/caption for the expected effect. */
	display_event (cd, ch, _VBI_CC608_START_ROLLING);
}

static void
erase_memory			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 unsigned int		buffer)
{
	if (0 != ch->dirty[buffer]) {
		CLEAR (ch->buffer[buffer]);

		ch->dirty[buffer] = 0;

		if (buffer == ch->displayed_buffer)
			display_event (cd, ch, /* flags */ 0);
	}
}

static void
erase_displayed_memory		(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int row;

	/* EDM Erase Displayed Memory -- 001 c10f  010 1100 */

	switch (ch->mode) {
	case _VBI_CC608_MODE_UNKNOWN:
		/* We have not received EOC, RCL, RDC or RUx yet, but
		   ch is valid. */
		break;

	case _VBI_CC608_MODE_ROLL_UP:
		row = ch->curr_row;
		if (0 != (ch->dirty[ch->displayed_buffer] & (1 << row)))
			stream_event (cd, ch, row, row);
		break;

	case _VBI_CC608_MODE_PAINT_ON:
		stream_event_if_changed (cd, ch);
		break;

	case _VBI_CC608_MODE_POP_ON:
		/* Nothing to do. */
		break;

	case _VBI_CC608_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}

	/* May send a display event. */
	erase_memory (cd, ch, ch->displayed_buffer);
}

static void
text_restart			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;

	/* TR Text Restart -- 001 c10f  010 1010 */

	curr_buffer = ch->displayed_buffer;
	row = ch->curr_row;

	/* ch->mode is invariably VBI_CC608_MODE_TEXT. */

	if (0 != (ch->dirty[curr_buffer] & (1 << row))) {
		stream_event (cd, ch, row, row);
	}

	/* EIA 608-B Section 7.4. */
	/* May send a display event. */
	erase_memory (cd, ch, ch->displayed_buffer);

	/* EIA 608-B Section 7.4. */
	ch->curr_row = FIRST_ROW;
	ch->curr_column = FIRST_COLUMN;
}

static void
resume_direct_captioning	(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;

	/* RDC Resume Direct Captioning -- 001 c10f  010 1001 */

	/* 47 CFR 15.119 (f)(1)(x), (f)(2)(vi) and EIA 608-B Annex
	   B.7: Does not erase memory, does not move the cursor when
	   resuming after a Text transmission.

	   XXX If ch->mode is unknown, roll-up or pop-on, what is
	   expected if no PAC is received between RDC and the text? */

	curr_buffer = ch->displayed_buffer;
	row = ch->curr_row;

	switch (ch->mode) {
	case _VBI_CC608_MODE_ROLL_UP:
		if (0 != (ch->dirty[curr_buffer] & (1 << row)))
			stream_event (cd, ch, row, row);

		/* fall through */

	case _VBI_CC608_MODE_UNKNOWN:
	case _VBI_CC608_MODE_POP_ON:
		/* No change since last stream_event(). */
		memcpy (ch->buffer[2], ch->buffer[curr_buffer],
			sizeof (ch->buffer[2]));
		break;

	case _VBI_CC608_MODE_PAINT_ON:
		/* Mode continues. */
		break;

	case _VBI_CC608_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}

	ch->mode = _VBI_CC608_MODE_PAINT_ON;
}

static void
resize_window			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 unsigned int		new_rows)
{
	unsigned int curr_buffer;
	unsigned int max_rows;
	unsigned int old_rows;
	unsigned int row1;

	curr_buffer = ch->displayed_buffer;

	/* Shortcut. */
	if (0 == ch->dirty[curr_buffer])
		return;

	row1 = ch->curr_row + 1;
	max_rows = row1 - FIRST_ROW;
	old_rows = MIN (ch->window_rows, max_rows);
	new_rows = MIN (new_rows, max_rows);

	/* Nothing to do unless the window shrinks. */
	if (0 == new_rows || new_rows >= old_rows)
		return;

	memset (&ch->buffer[curr_buffer][row1 - old_rows][0], 0,
		(old_rows - new_rows)
		* sizeof (ch->buffer[0][0]));

	ch->dirty[curr_buffer] &= -1 << (row1 - new_rows);

	display_event (cd, ch, /* flags */ 0);
}

static void
roll_up_caption			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 unsigned int		c2)
{
	unsigned int window_rows;

	/* Roll-Up Captions -- 001 c10f  010 01xx */

	window_rows = (c2 & 7) - 3; /* 2, 3, 4 */

	switch (ch->mode) {
	case _VBI_CC608_MODE_ROLL_UP:
		/* 47 CFR 15.119 (f)(1)(iv). */
		/* May send a display event. */
		resize_window (cd, ch, window_rows);

		/* fall through */

	case _VBI_CC608_MODE_UNKNOWN:
		ch->mode = _VBI_CC608_MODE_ROLL_UP;
		ch->window_rows = window_rows;

		/* 47 CFR 15.119 (f)(1)(ix): No cursor movements,
		   no memory erasing. */

		break;

	case _VBI_CC608_MODE_PAINT_ON:
		stream_event_if_changed (cd, ch);

		/* fall through */

	case _VBI_CC608_MODE_POP_ON:
		ch->mode = _VBI_CC608_MODE_ROLL_UP;
		ch->window_rows = window_rows;

		/* 47 CFR 15.119 (f)(1)(ii). */
		ch->curr_row = LAST_ROW;
		ch->curr_column = FIRST_COLUMN;

		/* 47 CFR 15.119 (f)(1)(x). */
		/* May send a display event. */
		erase_memory (cd, ch, ch->displayed_buffer);
		erase_memory (cd, ch, ch->displayed_buffer ^ 1);

		break;

	case _VBI_CC608_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}
}

static void
delete_to_end_of_row		(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;

	/* DER Delete To End Of Row -- 001 c10f  010 0100 */

	/* 47 CFR 15.119 (f)(1)(vii), (f)(2)(iii), (f)(3)(ii) and EIA
	   608-B Section 7.4: In all caption modes and Text mode
	   "[the] Delete to End of Row command will erase from memory
	   any characters or control codes starting at the current
	   cursor location and in all columns to its right on the same
	   row." */

	curr_buffer = ch->displayed_buffer
		^ (_VBI_CC608_MODE_POP_ON == ch->mode);

	row = ch->curr_row;

	/* No event if the row contains only TRANSPARENT_SPACEs. */
	if (0 != (ch->dirty[curr_buffer] & (1 << row))) {
		unsigned int column;
		unsigned int i;
		uint16_t c;

		column = ch->curr_column;

		memset (&ch->buffer[curr_buffer][row][column], 0,
			(LAST_COLUMN - column + 1)
			* sizeof (ch->buffer[0][0][0]));

		c = 0;
		for (i = FIRST_COLUMN; i < column; ++i)
			c |= ch->buffer[curr_buffer][row][i];

		ch->dirty[curr_buffer] &= ~((0 == c) << row);

		display_event (cd, ch, /* flags */ 0);
	}
}

static void
backspace			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;
	unsigned int column;

	/* BS Backspace -- 001 c10f  010 0001 */

	/* 47 CFR Section 15.119 (f)(1)(vi), (f)(2)(ii), (f)(3)(i) and
	   EIA 608-B Section 7.4. */
	column = ch->curr_column;
	if (column <= FIRST_COLUMN)
		return;

	ch->curr_column = --column;

	curr_buffer = ch->displayed_buffer
		^ (_VBI_CC608_MODE_POP_ON == ch->mode);

	row = ch->curr_row;

	/* No event if there's no visible effect. */
	if (0 != ch->buffer[curr_buffer][row][column]) {
		unsigned int i;
		uint16_t c;

		/* 47 CFR 15.119 (f), (f)(1)(vi), (f)(2)(ii) and EIA
		   608-B Section 7.4. */
		ch->buffer[curr_buffer][row][column] = 0;

		c = 0;
		for (i = FIRST_COLUMN; i <= LAST_COLUMN; ++i)
			c |= ch->buffer[curr_buffer][row][i];

		ch->dirty[curr_buffer] &= ~((0 == c) << row);

		display_event (cd, ch, /* flags */ 0);
	}
}

static void
resume_caption_loading		(_vbi_cc608_decoder *	cd,
				 struct channel *	ch)
{
	unsigned int row;

	/* RCL Resume Caption Loading -- 001 c10f  010 0000 */

	switch (ch->mode) {
	case _VBI_CC608_MODE_UNKNOWN:
	case _VBI_CC608_MODE_POP_ON:
		break;

	case _VBI_CC608_MODE_ROLL_UP:
		row = ch->curr_row;
		if (0 != (ch->dirty[ch->displayed_buffer] & (1 << row)))
			stream_event (cd, ch, row, row);
		break;

	case _VBI_CC608_MODE_PAINT_ON:
		stream_event_if_changed (cd, ch);
		break;

	case _VBI_CC608_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}

	/* 47 CFR 15.119 (f)(1)(x): Does not erase memory.
	   (f)(2)(iv): Cursor position remains unchanged. */

	ch->mode = _VBI_CC608_MODE_POP_ON;
}

/* Note curr_ch is invalid if UNKNOWN_CHANNEL == cd->cc.curr_ch_num. */
static struct channel *
switch_channel			(_vbi_cc608_decoder *	cd,
				 struct channel *	curr_ch,
				 vbi_pgno		new_ch_num,
				 enum field_num		f)
{
	struct channel *new_ch;

	if (UNKNOWN_CHANNEL != cd->curr_ch_num[f]
	    && _VBI_CC608_MODE_UNKNOWN != curr_ch->mode) {
		/* XXX Force a display update if we do not send events
		   on every display change. */
	}

	cd->curr_ch_num[f] = new_ch_num;
	new_ch = &cd->channel[new_ch_num - VBI_CAPTION_CC1];

	return new_ch;
}

/* Note ch is invalid if UNKNOWN_CHANNEL == cd->cc.curr_ch_num[f]. */
static void
misc_control_code		(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 unsigned int		c2,
				 unsigned int		ch_num0,
				 enum field_num		f)
{
	unsigned int new_ch_num;

	/* Misc Control Codes -- 001 c10f  010 xxxx */

	/* c = channel (0 -> CC1/CC3/T1/T3, 1 -> CC2/CC4/T2/T4)
	     -- 47 CFR Section 15.119, EIA 608-B Section 7.7.
	   f = field (0 -> F1, 1 -> F2)
	     -- EIA 608-B Section 8.4, 8.5. */

	/* XXX The f flag is intended to detect accidential field
	   swapping and we should use it for that purpose. */

	switch (c2 & 15) {
	case 0:	/* RCL Resume Caption Loading -- 001 c10f  010 0000 */
		/* 47 CFR 15.119 (f)(2) and EIA 608-B Section 7.7. */
		new_ch_num = VBI_CAPTION_CC1 + (ch_num0 & 3);
		ch = switch_channel (cd, ch, new_ch_num, f);
		resume_caption_loading (cd, ch);
		break;

	case 1: /* BS Backspace -- 001 c10f  010 0001 */
		if (UNKNOWN_CHANNEL == cd->curr_ch_num[f]
		    || _VBI_CC608_MODE_UNKNOWN == ch->mode)
			break;
		backspace (cd, ch);
		break;

	case 2: /* reserved (formerly AOF Alarm Off) */
	case 3: /* reserved (formerly AON Alarm On) */
		break;

	case 4: /* DER Delete To End Of Row -- 001 c10f  010 0100 */
		if (UNKNOWN_CHANNEL == cd->curr_ch_num[f]
		    || _VBI_CC608_MODE_UNKNOWN == ch->mode)
			break;
		delete_to_end_of_row (cd, ch);
		break;

	case 5: /* RU2 */
	case 6: /* RU3 */
	case 7: /* RU4 Roll-Up Captions -- 001 c10f  010 01xx */
		/* 47 CFR 15.119 (f)(1) and EIA 608-B Section 7.7. */
		new_ch_num = VBI_CAPTION_CC1 + (ch_num0 & 3);
		ch = switch_channel (cd, ch, new_ch_num, f);
		roll_up_caption (cd, ch, c2);
		break;

	case 8: /* FON Flash On -- 001 c10f  010 1000 */
		if (UNKNOWN_CHANNEL == cd->curr_ch_num[f]
		    || _VBI_CC608_MODE_UNKNOWN == ch->mode)
			break;

		/* 47 CFR 15.119 (h)(1)(i): Spacing attribute. */
		put_char (cd, ch, 0x1428,
			  /* displayable */ FALSE,
			  /* backspace */ FALSE);
		break;

	case 9: /* RDC Resume Direct Captioning -- 001 c10f  010 1001 */
		/* 47 CFR 15.119 (f)(3) and EIA 608-B Section 7.7. */
		new_ch_num = VBI_CAPTION_CC1 + (ch_num0 & 3);
		ch = switch_channel (cd, ch, new_ch_num, f);
		resume_direct_captioning (cd, ch);
		break;

	case 10: /* TR Text Restart -- 001 c10f  010 1010 */
		/* EIA 608-B Section 7.4. */
		new_ch_num = VBI_CAPTION_T1 + (ch_num0 & 3);
		ch = switch_channel (cd, ch, new_ch_num, f);
		text_restart (cd, ch);
		break;

	case 11: /* RTD Resume Text Display -- 001 c10f  010 1011 */
		/* EIA 608-B Section 7.4. */
		new_ch_num = VBI_CAPTION_T1 + (ch_num0 & 3);
		ch = switch_channel (cd, ch, new_ch_num, f);
		/* ch->mode is invariably VBI_CC608_MODE_TEXT. */
		break;

	case 12: /* EDM Erase Displayed Memory -- 001 c10f  010 1100 */
		/* 47 CFR 15.119 (f). EIA 608-B Section 7.7 and Annex
		   B.7: "[The] command shall be acted upon as
		   appropriate for caption processing without
		   terminating the Text Mode data stream." */

		/* We need not check cd->curr_ch_num because bit 2 is
		   implied, bit 1 is the known field number and bit 0
		   is coded in the control code. */
		ch = &cd->channel[ch_num0 & 3];

		erase_displayed_memory (cd, ch);

		break;

	case 13: /* CR Carriage Return -- 001 c10f  010 1101 */
		if (UNKNOWN_CHANNEL == cd->curr_ch_num[f])
			break;
		carriage_return (cd, ch);
		break;

	case 14: /* ENM Erase Non-Displayed Memory -- 001 c10f  010 1110 */
		/* 47 CFR 15.119 (f)(2)(v). EIA 608-B Section 7.7 and
		   Annex B.7: "[The] command shall be acted upon as
		   appropriate for caption processing without
		   terminating the Text Mode data stream." */

		/* See EDM. */
		ch = &cd->channel[ch_num0 & 3];

		erase_memory (cd, ch, ch->displayed_buffer ^ 1);

		break;

	case 15: /* EOC End Of Caption -- 001 c10f  010 1111 */
		/* 47 CFR 15.119 (f), (f)(2), (f)(3)(iv) and EIA 608-B
		   Section 7.7, Annex C.11. */
		new_ch_num = VBI_CAPTION_CC1 + (ch_num0 & 3);
		ch = switch_channel (cd, ch, new_ch_num, f);
		end_of_caption (cd, ch);
		break;
	}
}

static void
move_window			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 unsigned int		new_base_row)
{
	uint8_t *base;
	unsigned int curr_buffer;
	unsigned int bytes_per_row;
	unsigned int old_max_rows;
	unsigned int new_max_rows;
	unsigned int copy_bytes;
	unsigned int erase_begin;
	unsigned int erase_end;

	curr_buffer = ch->displayed_buffer;

	/* Shortcut and no event if we do not move the window or the
	   buffer contains only TRANSPARENT_SPACEs. */
	if (new_base_row == ch->curr_row
	    || 0 == ch->dirty[curr_buffer])
		return;

	base = (void *) &ch->buffer[curr_buffer][FIRST_ROW][0];
	bytes_per_row = sizeof (ch->buffer[0][0]);

	old_max_rows = ch->curr_row + 1 - FIRST_ROW;
	new_max_rows = new_base_row + 1 - FIRST_ROW;
	copy_bytes = MIN (MIN (old_max_rows, new_max_rows),
			  ch->window_rows) * bytes_per_row;

	if (new_base_row < ch->curr_row) {
		erase_begin = (new_base_row + 1) * bytes_per_row;
		erase_end = (ch->curr_row + 1) * bytes_per_row;

		memmove (base + erase_begin - copy_bytes,
			 base + erase_end - copy_bytes, copy_bytes);

		ch->dirty[curr_buffer] >>= ch->curr_row - new_base_row;
	} else {
		erase_begin = (ch->curr_row + 1) * bytes_per_row
			- copy_bytes;
		erase_end = (new_base_row + 1) * bytes_per_row
			- copy_bytes;

		memmove (base + erase_end,
			 base + erase_begin, copy_bytes);

		ch->dirty[curr_buffer] <<= new_base_row - ch->curr_row;
		ch->dirty[curr_buffer] &= ALL_ROWS_MASK;
	}

	memset (base + erase_begin, 0, erase_end - erase_begin);

	display_event (cd, ch, /* flags */ 0);
}

static void
preamble_address_code		(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 unsigned int		c1,
				 unsigned int		c2)
{
	unsigned int row;

	/* PAC Preamble Address Codes -- 001 crrr  1ri xxxu */

	row = pac_row_map[(c1 & 7) * 2 + ((c2 >> 5) & 1)];
	if ((int) row < 0)
		return;

	switch (ch->mode) {
	case _VBI_CC608_MODE_UNKNOWN:
		return;

	case _VBI_CC608_MODE_ROLL_UP:
		/* EIA 608-B Annex C.4. */
		if (ch->window_rows > row + 1)
			row = ch->window_rows - 1;

		/* 47 CFR Section 15.119 (f)(1)(ii). */
		/* May send a display event. */
		move_window (cd, ch, row);

		ch->curr_row = row;

		break;

	case _VBI_CC608_MODE_PAINT_ON:
		stream_event_if_changed (cd, ch);

		/* fall through */

	case _VBI_CC608_MODE_POP_ON:
		/* XXX 47 CFR 15.119 (f)(2)(i), (f)(3)(i): In Pop-on
		   and paint-on mode "Preamble Address Codes can be
		   used to move the cursor around the screen in random
		   order to place captions on Rows 1 to 15." We do not
		   have a limit on the number of displayable rows, but
		   as EIA 608-B Annex C.6 points out, if more than
		   four rows must be displayed they were probably
		   received in error and we should respond
		   accordingly. */

		/* 47 CFR Section 15.119 (d)(1)(i) and EIA 608-B Annex
		   C.7. */
		ch->curr_row = row;

		break;

	case _VBI_CC608_MODE_TEXT:
		/* 47 CFR 15.119 (e)(1) and EIA 608-B Section 7.4:
		   Does not change the cursor row. */
		break;
	}

	if (c2 & 0x10) {
		/* 47 CFR 15.119 (e)(1)(i) and EIA 608-B Table 71. */
		ch->curr_column = FIRST_COLUMN + (c2 & 0x0E) * 2;
	}

	/* PAC is a non-spacing attribute for the next character, see
	   put_char(). */
	ch->last_pac = 0x1000 | c2;
}

static void
control_code			(_vbi_cc608_decoder *	cd,
				 unsigned int		c1,
				 unsigned int		c2,
				 enum field_num		f)
{
	struct channel *ch;
	unsigned int ch_num0;

	if (CC608_DECODER_LOG_INPUT) {
		fprintf (stdout, "%s:%u: %s c1=%02x c2=%02x f=%d\n",
			 __FILE__, __LINE__, __FUNCTION__,
			 c1, c2, f);
	}

	/* b2: Caption / text,
	   b1: field 1 / 2,
	   b0 (lsb): primary / secondary channel. */
	ch_num0 = (((cd->curr_ch_num[f] - VBI_CAPTION_CC1) & 4)
		   + f * 2
		   + ((c1 >> 3) & 1));

	/* Note ch is invalid if UNKNOWN_CHANNEL ==
	   cd->curr_ch_num[f]. */
	ch = &cd->channel[ch_num0];

	if (c2 >= 0x40) {
		/* Preamble Address Codes -- 001 crrr  1ri xxxu */
		if (UNKNOWN_CHANNEL != cd->curr_ch_num[f])
			preamble_address_code (cd, ch, c1, c2);
		return;
	}

	switch (c1 & 7) {
	case 0:
		if (UNKNOWN_CHANNEL == cd->curr_ch_num[f]
		    || _VBI_CC608_MODE_UNKNOWN == ch->mode)
			break;

		if (c2 < 0x30) {
			/* Backgr. Attr. Codes -- 001 c000  010 xxxt */
			/* EIA 608-B Section 6.2. */
			put_char (cd, ch, 0x1000 | c2,
				  /* displayable */ FALSE,
				  /* backspace */ TRUE);
		} else {
			/* Undefined. */
		}

		break;

	case 1:
		if (UNKNOWN_CHANNEL == cd->curr_ch_num[f]
		    || _VBI_CC608_MODE_UNKNOWN == ch->mode)
			break;

		if (c2 < 0x30) {
			/* Mid-Row Codes -- 001 c001  010 xxxu */
			/* 47 CFR 15.119 (h)(1)(i): Spacing attribute. */
			put_char (cd, ch, 0x1100 | c2,
				  /* displayable */ FALSE,
				  /* backspace */ FALSE);
		} else {
			/* Special Characters -- 001 c001  011 xxxx */
			if (0x39 == c2) {
				/* Transparent space. */
				put_char (cd, ch, 0,
					  /* displayable */ FALSE,
					  /* backspace */ FALSE);
			} else {
				put_char (cd, ch, 0x1100 | c2,
					  /* displayable */ TRUE,
					  /* backspace */ FALSE);
			}
		}

		break;

	case 2:
	case 3: /* Extended Character Set -- 001 c01x  01x xxxx */
		if (UNKNOWN_CHANNEL == cd->curr_ch_num[f]
		    || _VBI_CC608_MODE_UNKNOWN == ch->mode)
			break;

		/* EIA 608-B Section 6.4.2. */
		put_char (cd, ch, (c1 * 256 + c2) & 0x777F,
			  /* displayable */ TRUE,
			  /* backspace */ TRUE);
		break;

	case 4:
	case 5:
		if (c2 < 0x30) {
			/* Misc. Control Codes -- 001 c10f  010 xxxx */
			misc_control_code (cd, ch, c2, ch_num0, f);
		} else {
			/* Undefined. */
		}

		break;

	case 6: /* reserved */
		break;

	case 7:	/* Extended control codes -- 001 c111  01x xxxx */
		if (UNKNOWN_CHANNEL == cd->curr_ch_num[f]
		    || _VBI_CC608_MODE_UNKNOWN == ch->mode)
			break;

		ext_control_code (cd, ch, c2);

		break;
	}
}

static vbi_bool
characters			(_vbi_cc608_decoder *	cd,
				 struct channel *	ch,
				 int			c)
{
	if (CC608_DECODER_LOG_INPUT) {
		fprintf (stdout, "%s:%u: %s c=0x%02x='%c'\n",
			 __FILE__, __LINE__, __FUNCTION__,
			 c, _vbi_to_ascii (c));
	}

	if (0 == c) {
		if (_VBI_CC608_MODE_UNKNOWN == ch->mode)
			return TRUE;

		/* XXX After x NUL characters (presumably a caption
		   pause), force a display update if we do not send
		   events on every display change. */

		return TRUE;
	}

	if (c < 0x20) {
		/* Parity error or invalid data. */

		if (c < 0 && _VBI_CC608_MODE_UNKNOWN != ch->mode) {
			/* 47 CFR Section 15.119 (j)(1). */
			put_char (cd, ch, 0x7F,
				  /* displayable */ TRUE,
				  /* backspace */ FALSE);
		}

		return FALSE;
	}

	if (_VBI_CC608_MODE_UNKNOWN != ch->mode) {
		put_char (cd, ch, c,
			  /* displayable */ TRUE,
			  /* backspace */ FALSE);
	}

	return TRUE;
}

/**
 * @param cd Caption decoder allocated with _vbi_cc608_decoder_new().
 * @param buffer A caption byte pair with parity bits.
 * @param line ITU-R line number this data originated from,
 *   usually 21 or 284.
 * @param capture_time System time in seconds when the sliced data was
 *   captured.
 * @param pts ISO 13818-1 Presentation Time Stamp of the sliced
 *   data. @a pts counts 1/90000 seconds from an arbitrary point in the
 *   video stream. Only the 33 least significant bits have to be valid.
 *   If @a pts is negative the function converts @a capture_time to a
 *   PTS.
 *
 * This function decodes two bytes of Closed Caption data and updates
 * the decoder state. It may send one VBI_EVENT_CC608 and one or more
 * VBI_EVENT_CC608_STREAM.
 *
 * @returns
 * @c FALSE if the caption byte pair contained errors.
 */
vbi_bool
_vbi_cc608_decoder_feed		(_vbi_cc608_decoder *	cd,
				 const uint8_t		buffer[2],
				 unsigned int		line,
				 double			capture_time,
				 int64_t		pts)
{
	int c1, c2;
	enum field_num f;
	vbi_bool all_successful;

	assert (NULL != cd);

	if (0) {
		fprintf (stdout, "%s:%u: %s "
			 "buffer={ 0x%02x 0x%02x '%c%c' } "
			 "line=%3d capture_time=%f "
			 "pts=%" PRId64 "\n",
			 __FILE__, __LINE__, __FUNCTION__,
			 buffer[0] & 0x7F,
			 buffer[1] & 0x7F,
			 _vbi_to_ascii (buffer[0]),
			 _vbi_to_ascii (buffer[1]),
			 line, capture_time, pts);
	}

	f = FIELD_1;

	switch (line) {
	case 21: /* NTSC */
	case 22: /* PAL/SECAM? */
		break;

	case 284: /* NTSC */
		f = FIELD_2;
		break;

	default:
		return FALSE;
	}

	cd->timestamp.sys = capture_time;

	if (pts < 0)
		pts = (int64_t)(capture_time * 90000);

	/* Modulo 1 << 33 guaranteed in VBI_EVENT_CC608_STREAM dox. */
	cd->timestamp.pts = pts & (((int64_t) 1 << 33) - 1);

	/* XXX deferred reset here */

	if (0 && FIELD_1 == f) {
		_vbi_cc608_dump (stderr, buffer[0], buffer[1]);
	}

	c1 = vbi_unpar8 (buffer[0]);
	c2 = vbi_unpar8 (buffer[1]);

	all_successful = TRUE;

	/* See 47 CFR 15.119 (2)(i)(4). EIA 608-B Section 8.3: Caption
	   control codes on field 2 may repeat as on field 1. Section
	   8.6.2: XDS control codes shall not repeat. */

	if (unlikely (c1 < 0)) {
		goto parity_error;
	} else if (c1 == cd->expect_ctrl[f][0]
		   && c2 == cd->expect_ctrl[f][1]) {
		/* Already acted upon. */
		cd->expect_ctrl[f][0] = -1;
		goto finish;
	}

	if (c1 >= 0x10 && c1 < 0x20) {
		/* Caption control code. */

		/* There's no XDS on field 1, we just
		   use an array to save a branch. */
		cd->in_xds[f] = FALSE;

		/* 47 CFR Section 15.119 (i)(1), (i)(2). */
		if (c2 < 0x20) {
			/* Parity error or invalid control code.
			   Let's hope this code will repeat. */
			goto parity_error;
		}

		control_code (cd, c1, c2, f);

		if (cd->event_pending) {
			display_event (cd, cd->event_pending,
				       /* flags */ 0);
			cd->event_pending = NULL;
		}

		cd->expect_ctrl[f][0] = c1;
		cd->expect_ctrl[f][1] = c2;
	} else {
		cd->expect_ctrl[f][0] = -1;

		if (c1 < 0x10) {
			if (FIELD_1 == f) {
				/* 47 CFR Section 15.119 (i)(1): "If the
				   non-printing character in the pair is
				   in the range 00h to 0Fh, that character
				   alone will be ignored and the second
				   character will be treated normally." */
				c1 = 0;
			} else if (0x0F == c1) {
				/* XDS packet terminator. */
				cd->in_xds[FIELD_2] = FALSE;
				goto finish;
			} else if (c1 >= 0x01) {
				/* XDS packet start or continuation.
				   EIA 608-B Section 7.7, 8.5: Also
				   interrupts a Text mode
				   transmission. */
				cd->in_xds[FIELD_2] = TRUE;
				goto finish;
			}
		}

		{
			struct channel *ch;
			vbi_pgno ch_num;

			ch_num = cd->curr_ch_num[f];
			if (UNKNOWN_CHANNEL == ch_num)
				goto finish;

			ch_num = ((ch_num - VBI_CAPTION_CC1) & 5) + f * 2;
			ch = &cd->channel[ch_num];

			all_successful &= characters (cd, ch, c1);
			all_successful &= characters (cd, ch, c2);

			if (cd->event_pending) {
				display_event (cd, cd->event_pending,
					       /* flags */ 0);
				cd->event_pending = NULL;
			}
		}
	}

 finish:
	cd->error_history = cd->error_history * 2 + all_successful;

	return all_successful;

 parity_error:
	cd->expect_ctrl[f][0] = -1;

	/* XXX Some networks stupidly transmit 0x0000 instead of
	   0x8080 as filler. Perhaps we shouldn't take that as a
	   serious parity error. */
	cd->error_history *= 2;

	return FALSE;
}

/**
 * @param cd Caption decoder allocated with _vbi_cc608_decoder_new().
 * @param sliced Sliced VBI data.
 * @param n_lines Number of lines in the @a sliced array.
 * @param capture_time System time in seconds when the sliced data was
 *   captured.
 * @param pts ISO 13818-1 Presentation Time Stamp of all elements
 *   in the sliced data array. @a pts counts 1/90000 seconds from an
 *   arbitrary point in the video stream. Only the 33 least significant
 *   bits have to be valid. If @a pts is negative the function
 *   converts @a capture_time to a PTS.
 *
 * This function works like _vbi_cc608_decoder_feed() but operates
 * on sliced VBI data and filters out @c VBI_SLICED_CAPTION_525.
 */
vbi_bool
_vbi_cc608_decoder_feed_frame	(_vbi_cc608_decoder *	cd,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 double			capture_time,
				 int64_t		pts)
{
	const vbi_sliced *end;

	assert (NULL != cd);
	assert (NULL != sliced);

	for (end = sliced + n_lines; sliced < end; ++sliced) {
		if (sliced->id & VBI_SLICED_CAPTION_525) {
			if (!_vbi_cc608_decoder_feed (cd,
						      sliced->data,
						      sliced->line,
						      capture_time,
						      pts))
				return FALSE;
		}
	}

	return TRUE;
}

/**
 * @param cd Caption decoder allocated with _vbi_cc608_decoder_new().
 * @param callback Function to be called on events.
 * @param user_data User pointer passed through to the @a callback
 *   function.
 * 
 * Removes an event handler from the caption decoder, if a handler with
 * this @a callback and @a user_data has been registered.
 */
void
_vbi_cc608_decoder_remove_event_handler
				(_vbi_cc608_decoder *	cd,
				 vbi_event_handler	callback,
				 void *			user_data)
{
	_vbi_event_handler_list_remove_by_callback (&cd->handlers,
						    callback,
						    user_data);
}

/**
 * @param cd Caption decoder allocated with _vbi_cc608_decoder_new().
 * @param event_mask Set of events the handler is waiting for,
 *   VBI_EVENT_CC608 or VBI_EVENT_CC608_STREAM.
 * @param callback Function to be called on events by
 *   _vbi_cc608_decoder_feed().
 * @param user_data User pointer passed through to the @a callback
 *   function.
 * 
 * Adds a new event handler to the caption decoder. When the @a
 * callback with this @a user_data is already registered the function
 * changes the set of events the callback function will receive in the
 * future.
 *
 * Any number of handlers can be added, also different handlers for the
 * same event, which will be called in registration order.
 *
 * @returns
 * @c FALSE on failure (out of memory).
 */
vbi_bool
_vbi_cc608_decoder_add_event_handler
				(_vbi_cc608_decoder *	cd,
				 unsigned int		event_mask,
				 vbi_event_handler	callback,
				 void *			user_data)
{
	event_mask &= (_VBI_EVENT_CC608 |
		       _VBI_EVENT_CC608_STREAM);

	if (0 == event_mask) {
		_vbi_event_handler_list_remove_by_callback (&cd->handlers,
							    callback,
							    user_data);
		return TRUE;
	}

	if (NULL != _vbi_event_handler_list_add (&cd->handlers,
						 event_mask,
						 callback,
						 user_data)) {
		return TRUE;
	}

	return FALSE;
}

/**
 * @param cd Caption decoder allocated with _vbi_cc608_decoder_new().
 *
 * Resets the caption decoder, useful for example after a channel
 * change.
 */
void
_vbi_cc608_decoder_reset		(_vbi_cc608_decoder *	cd)
{
	unsigned int ch_num;

	assert (NULL != cd);

	if (CC608_DECODER_LOG_INPUT) {
		fprintf (stderr, "%s:%u: %s\n",
			 __FILE__, __LINE__, __FUNCTION__);

	}

	for (ch_num = 0; ch_num < MAX_CHANNELS; ++ch_num) {
		struct channel *ch;

		ch = &cd->channel[ch_num];

		if (ch_num <= 3) {
			ch->mode = _VBI_CC608_MODE_UNKNOWN;

			/* Plausible for roll-up mode. We don't
			   display text while the caption mode is
			   unknown and may choose more suitable
			   defaults when we receive a mode changing
			   control code. */
			ch->curr_row = LAST_ROW;
			ch->curr_column = FIRST_COLUMN;
			ch->window_rows = 4;
		} else {
			ch->mode = _VBI_CC608_MODE_TEXT; /* invariable */

			/* EIA 608-B Section 7.4: "When Text Mode has
			   initially been selected and the specified
			   Text memory is empty, the cursor starts at
			   the topmost row, Column 1." */
			ch->curr_row = FIRST_ROW;
			ch->curr_column = FIRST_COLUMN;
			ch->window_rows = 0; /* n/a */
		}

		ch->displayed_buffer = 0;

		ch->last_pac = 0;

		CLEAR (ch->buffer);
		CLEAR (ch->dirty);

		timestamp_reset (&ch->timestamp);
		timestamp_reset (&ch->timestamp_c0);
	}

	cd->curr_ch_num[0] = UNKNOWN_CHANNEL;
	cd->curr_ch_num[1] = UNKNOWN_CHANNEL;

	memset (cd->expect_ctrl, -1, sizeof (cd->expect_ctrl));

	CLEAR (cd->in_xds);

	cd->event_pending = NULL;
}

static void
_vbi_cc608_decoder_destroy	(_vbi_cc608_decoder *	cd)
{
	assert (NULL != cd);

	_vbi_event_handler_list_destroy (&cd->handlers);

	CLEAR (*cd);
}

static void
_vbi_cc608_decoder_init		(_vbi_cc608_decoder *	cd)
{
	assert (NULL != cd);

	CLEAR (*cd);

	_vbi_event_handler_list_init (&cd->handlers);
	
	_vbi_cc608_decoder_reset (cd);

	timestamp_reset (&cd->timestamp);
}

/**
 * @param cd Caption decoder allocated with _vbi_cc608_decoder_new(),
 *   can be @a NULL.
 *
 * Frees all resources associated with @a cd.
 */
void
_vbi_cc608_decoder_delete	(_vbi_cc608_decoder *	cd)
{
	if (NULL == cd)
		return;

	_vbi_cc608_decoder_destroy (cd);

	vbi_free (cd);
}

/**
 * Allocates a new EIA 608-B Closed Caption decoder.
 *
 * To enter caption data call the _vbi_cc608_decoder_feed()
 * function. Decoded data is available through VBI_EVENT_CC608_STREAM
 * and the _vbi_cc608_decoder_get_page() function.
 *
 * To be notified when new data is available call
 * _vbi_cc608_decoder_add_event_handler().
 *
 * @returns
 * Pointer to a newly allocated caption decoder which must be freed
 * with _vbi_cc608_decoder_delete() when no longer needed. @c NULL
 * on failure (out of memory).
 */
_vbi_cc608_decoder *
_vbi_cc608_decoder_new		(void)
{
	_vbi_cc608_decoder *cd;

	cd = vbi_malloc (sizeof (*cd));

	if (NULL != cd) {
		_vbi_cc608_decoder_init (cd);
	}

	return cd;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
