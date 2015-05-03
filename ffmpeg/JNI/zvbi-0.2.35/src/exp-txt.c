/*
 *  libzvbi - Text export functions
 *
 *  Copyright (C) 2001, 2002 Michael H. Schimek
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

/* $Id: exp-txt.c,v 1.24 2013/07/02 02:32:06 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <limits.h>
#include <iconv.h>

#include "misc.h"
#include "lang.h"
#include "export.h"
#include "exp-txt.h"

typedef struct text_instance {
	vbi_export		export;

	/* Options */
	int			format;
	char *			charset;
	unsigned		color : 1;
	int			term;
	int			gfx_chr;
	int			def_fg;
	int			def_bg;

	iconv_t			cd;
	char			buf[32];
} text_instance;

static vbi_export *
text_new(void)
{
	text_instance *text;

	if (!(text = calloc(1, sizeof(*text))))
		return NULL;

	return &text->export;
}

static void
text_delete(vbi_export *e)
{
	text_instance *text = PARENT(e, text_instance, export);

	if (text->charset)
		free(text->charset);

	free(text);
}

#define elements(array) (sizeof(array) / sizeof(array[0]))

static const char *
formats[] = {
	N_("ASCII"),
	N_("ISO-8859-1 (Latin-1 Western languages)"),
	N_("ISO-8859-2 (Latin-2 Central and Eastern Europe languages)"),
	N_("ISO-8859-4 (Latin-3 Baltic languages)"),
	N_("ISO-8859-5 (Cyrillic)"),
	N_("ISO-8859-7 (Greek)"),
	N_("ISO-8859-8 (Hebrew)"),
	N_("ISO-8859-9 (Turkish)"),
	N_("KOI8-R (Russian and Bulgarian)"),
	N_("KOI8-U (Ukranian)"),
	N_("ISO-10646/UTF-8 (Unicode)"),
};

static const char *
iconv_formats[] = {
	"ASCII", "ISO-8859-1", "ISO-8859-2", "ISO-8859-4",
	"ISO-8859-5", "ISO-8859-7", "ISO-8859-8", "ISO-8859-9",
        "KOI8-R", "KOI8-U", "UTF-8"
};

static const char *
color_names[] _vbi_unused = {
	N_("Black"), N_("Red"), N_("Green"), N_("Yellow"),
	N_("Blue"), N_("Magenta"), N_("Cyan"), N_("White"),
	N_("Any")
};

static const char *
terminal[] = {
	/* TRANSLATORS:
	   Terminal control codes. */
	N_("None"), N_("ANSI X3.64 / VT 100"), N_("VT 200")
};

static vbi_option_info
text_options[] = {
	VBI_OPTION_MENU_INITIALIZER
	  /* TRANSLATORS: Text export format (ASCII, Unicode, ...) menu */
	  ("format", N_("Format"),
	   0, formats, elements(formats), NULL),
        /* one for users, another for programs */
	VBI_OPTION_STRING_INITIALIZER
	  ("charset", NULL, "", NULL),
	VBI_OPTION_STRING_INITIALIZER
	  ("gfx_chr", N_("Graphics char"),
	   "#", N_("Replacement for block graphic characters: "
		   "a single character or decimal (32) or hex (0x20) code")),
	VBI_OPTION_MENU_INITIALIZER
	  ("control", N_("Control codes"),
	   0, terminal, elements(terminal), NULL),
#if 0 /* obsolete (I think) */
	VBI_OPTION_MENU_INITIALIZER
	  ("fg", N_("Foreground"),
	   8 /* any */, color_names, elements(color_names),
	   N_("Assumed terminal foreground color")),
	VBI_OPTION_MENU_INITIALIZER
	  ("bg", N_("Background"),
	   8 /* any */, color_names, elements(color_names),
	   N_("Assumed terminal background color"))
#endif
};

static vbi_option_info *
option_enum(vbi_export *e, int index)
{
	e = e;

	if (index < 0 || index >= (int) elements(text_options))
		return NULL;

	return text_options + index;
}

#define KEYWORD(str) (strcmp(keyword, str) == 0)

static vbi_bool
option_get(vbi_export *e, const char *keyword, vbi_option_value *value)
{
	text_instance *text = PARENT(e, text_instance, export);

	if (KEYWORD("format")) {
		value->num = text->format;
	} else if (KEYWORD("charset")) {
		if (!(value->str = vbi_export_strdup(e, NULL, text->charset)))
			return FALSE;
	} else if (KEYWORD("gfx_chr")) {
		if (!(value->str = vbi_export_strdup(e, NULL, "x")))
			return FALSE;
		value->str[0] = text->gfx_chr;
	} else if (KEYWORD("control")) {
		value->num = text->term;
	} else if (KEYWORD("fg")) {
		value->num = text->def_fg;
	} else if (KEYWORD("bg")) {
		value->num = text->def_bg;
	} else {
		vbi_export_unknown_option(e, keyword);
		return FALSE;
	}

	return TRUE; /* success */
}

static vbi_bool
option_set(vbi_export *e, const char *keyword, va_list args)
{
	text_instance *text = PARENT(e, text_instance, export);

	if (KEYWORD("format")) {
		unsigned int format = va_arg(args, unsigned int);

		if (format >= elements(formats)) {
			vbi_export_invalid_option(e, keyword, format);
			return FALSE;
		}
		text->format = format;
	} else if (KEYWORD("charset")) {
		char *string = va_arg(args, char *);

		if (!string) {
			vbi_export_invalid_option(e, keyword, string);
			return FALSE;
		} else if (!vbi_export_strdup(e, &text->charset, string))
			return FALSE;
	} else if (KEYWORD("gfx_chr")) {
		char *s, *string = va_arg(args, char *);
		int value;

		if (!string || !string[0]) {
			vbi_export_invalid_option(e, keyword, string);
			return FALSE;
		}
		if (strlen(string) == 1) {
			value = string[0];
		} else {
			value = strtol(string, &s, 0);
			if (s == string)
				value = string[0];
		}
		text->gfx_chr = (value < 0x20 || value > 0xE000) ? 0x20 : value;
	} else if (KEYWORD("control")) {
		int term = va_arg(args, int);

		if (term < 0 || term > 2) {
			vbi_export_invalid_option(e, keyword, term);
			return FALSE;
		}
		text->term = term;
	} else if (KEYWORD("fg")) {
		int col = va_arg(args, int);

		if (col < 0 || col > 8) {
			vbi_export_invalid_option(e, keyword, col);
			return FALSE;
		}
		text->def_fg = col;
	} else if (KEYWORD("bg")) {
		int col = va_arg(args, int);

		if (col < 0 || col > 8) {
			vbi_export_invalid_option(e, keyword, col);
			return FALSE;
		}
		text->def_bg = col;
	} else {
		vbi_export_unknown_option(e, keyword);
		return FALSE;
	}

	return TRUE;
}

static inline char *
_stpcpy(char *dst, const char *src)
{
	while ((*dst = *src++))
		dst++;
	return dst;
}

static int
match_color8(vbi_rgba color)
{
	int i, d, imin = 0, dmin = INT_MAX;

	for (i = 0; i < 8; i++) {
		d  = ABS((int)(       (i & 1) * 0xFF - VBI_R(color)));
		d += ABS((int)(((i >> 1) & 1) * 0xFF - VBI_G(color)));
		d += ABS((int)( (i >> 2)      * 0xFF - VBI_B(color)));

		if (d < dmin) {
			dmin = d;
			imin = i;
		}
	}

	return imin;
}

static vbi_bool
print_unicode(iconv_t cd, int endian, int unicode, char **p, int n)
{
	char in[2], *ip, *op;
	size_t li, lo, r;

	in[0 + endian] = unicode;
	in[1 - endian] = unicode >> 8;
	ip = in; op = *p;
	li = sizeof(in); lo = n;

	r = iconv(cd, &ip, &li, &op, &lo);

	if ((size_t) -1 == r
	    || (**p == 0x40 && unicode != 0x0040)) {
		in[0 + endian] = 0x20;
		in[1 - endian] = 0;
		ip = in; op = *p;
		li = sizeof(in); lo = n;

		r = iconv(cd, &ip, &li, &op, &lo);

		if ((size_t) -1 == r
		    || (r == 1 && **p == 0x40))
			goto error;
	}

	*p = op;

	return TRUE;

 error:
	return FALSE;
}

/**
 * @param pg Source page.
 * @param buf Memory location to hold the output.
 * @param size Size of the buffer in bytes. The function
 *   fails when the data exceeds the buffer capacity.
 * @param format Character set name for iconv() conversion,
 *   for example "ISO-8859-1".
 * @param table Scan page in table mode, printing all characters
 *   within the source rectangle including runs of spaces at
 *   the start and end of rows. When @c FALSE, scan all characters
 *   from @a column, @a row to @a column + @a width - 1,
 *   @a row + @a height - 1 and all intermediate rows to their
 *   full pg->columns width. In this mode runs of spaces at
 *   the start and end of rows are collapsed into single spaces,
 *   blank lines are suppressed.
 * @param rtl Currently ignored.
 * @param column First source column, 0 ... pg->columns - 1.
 * @param row First source row, 0 ... pg->rows - 1.
 * @param width Number of columns to print, 1 ... pg->columns.
 * @param height Number of rows to print, 1 ... pg->rows.
 * 
 * Print a subsection of a Teletext or Closed Caption vbi_page,
 * rows separated by linefeeds "\n", in the desired format.
 * All character attributes and colors will be lost. Graphics
 * characters, DRCS and all characters not representable in the
 * target format will be replaced by spaces.
 * 
 * @return
 * Number of bytes written into @a buf, a value of zero when
 * some error occurred. In this case @a buf may contain incomplete
 * data. Note this function does not append a terminating null
 * character.
 */
int
vbi_print_page_region(vbi_page *pg, char *buf, int size,
		      const char *format, vbi_bool table, vbi_bool rtl,
		      int column, int row, int width, int height)
{
	int endian = vbi_ucs2be();
	int column0, column1, row0, row1;
	int x, y, spaces, doubleh, doubleh0;
	iconv_t cd;
	char *p;

	rtl = rtl;

	if (0)
		fprintf (stderr, "vbi_print_page_region '%s' "
		         "table=%d col=%d row=%d width=%d height=%d\n",
			 format, table, column, row, width, height);

	column0 = column;
	row0 = row;
	column1 = column + width - 1;
	row1 = row + height - 1;

	if (!pg || !buf || size < 0 || !format
	    || column0 < 0 || column1 >= pg->columns
	    || row0 < 0 || row1 >= pg->rows
	    || endian < 0)
		return 0;

	if ((cd = iconv_open(format, "UCS-2")) == (iconv_t) -1)
		return 0;

	p = buf;

	doubleh = 0;

	for (y = row0; y <= row1; y++) {
		int x0, x1, xl;

		x0 = (table || y == row0) ? column0 : 0;
		x1 = (table || y == row1) ? column1 : (pg->columns - 1);

		xl = (table || y != row0 || (y + 1) != row1) ? -1 : column1;

		doubleh0 = doubleh;

		spaces = 0;
		doubleh = 0;

		for (x = x0; x <= x1; x++) {
			vbi_char ac = pg->text[y * pg->columns + x];

			if (table) {
				if (ac.size > VBI_DOUBLE_SIZE)
					ac.unicode = 0x0020;
			} else {
				switch (ac.size) {
				case VBI_NORMAL_SIZE:
				case VBI_DOUBLE_WIDTH:
					break;

				case VBI_DOUBLE_HEIGHT:
				case VBI_DOUBLE_SIZE:
					doubleh++;
					break;

				case VBI_OVER_TOP:
				case VBI_OVER_BOTTOM:
					continue;

				case VBI_DOUBLE_HEIGHT2:
				case VBI_DOUBLE_SIZE2:
					if (y > row0)
						ac.unicode = 0x0020;
					break;
				}

				/*
				 *  Special case two lines row0 ... row1, and all chars
				 *  in row0, column0 ... column1 are double height: Skip
				 *  row1, don't wrap around.
				 */
				if (x == xl && doubleh >= (x - x0)) {
					x1 = xl;
					y = row1;
				}

				if (ac.unicode == 0x20 || !vbi_is_print(ac.unicode)) {
					spaces++;
					continue;
				} else {
					if (spaces < (x - x0) || y == row0) {
						for (; spaces > 0; spaces--)
							if (!print_unicode(cd, endian, 0x0020,
									   &p, buf + size - p))
								goto failure;
					} else /* discard leading spaces */
						spaces = 0;
				}
			}

			if (!print_unicode(cd, endian, ac.unicode, &p, buf + size - p))
				goto failure;
		}

		/* if !table discard trailing spaces and blank lines */

		if (y < row1) {
			int left = buf + size - p;

			if (left < 1)
				goto failure;

			if (table) {
				*p++ = '\n'; /* XXX convert this (eg utf16) */
			} else if (spaces >= (x1 - x0)) {
				; /* suppress blank line */
			} else {
				/* exactly one space between adjacent rows */
				if (!print_unicode(cd, endian, 0x0020, &p, left))
					goto failure;
			}
		} else {
			if (doubleh0 > 0) {
				; /* prentend this is a blank double height lower row */
			} else {
				for (; spaces > 0; spaces--)
					if (!print_unicode(cd, endian, 0x0020, &p, buf + size - p))
						goto failure;
			}
		}
	}

	iconv_close(cd);
	return p - buf;

 failure:
	iconv_close(cd);
	return 0;
}


static int
print_char(text_instance *text, int endian, vbi_page *pg, vbi_char old, vbi_char this)
{
	char *p;
	vbi_char chg, off;

	p = text->buf;

	if (text->term > 0) {
		union { vbi_char c; uint64_t i; } u_old, u_tmp, u_this;

		assert(sizeof(vbi_char) == 8);

		u_old.c = old;
		u_this.c = this;

		u_tmp.i = u_old.i ^ u_this.i; chg = u_tmp.c;
		u_tmp.i = u_tmp.i &~u_this.i; off = u_tmp.c;

		/* http://www.cs.ruu.nl/wais/html/na-dir/emulators-faq/part3.html */

		if (chg.size)
			switch (this.size) {
			case VBI_NORMAL_SIZE:
				p = _stpcpy(p, "\e#5");
				break;
			case VBI_DOUBLE_WIDTH:
				p = _stpcpy(p, "\e#6");
				break;
			case VBI_DOUBLE_HEIGHT:
			case VBI_DOUBLE_HEIGHT2:
				break; /* ignore */
			case VBI_DOUBLE_SIZE:
				p = _stpcpy(p, "\e#3");
				break;
			case VBI_DOUBLE_SIZE2:
				p = _stpcpy(p, "\e#4");
				break;
			case VBI_OVER_TOP:
			case VBI_OVER_BOTTOM:
				return -1; /* don't print */
			}

		p = _stpcpy(p, "\e[");

		if (text->term == 1) {
			if (off.underline || off.bold || off.flash) {
				*p++ = ';'; /* \e[0; reset */
				chg.underline = this.underline;
				chg.bold = this.bold;
				chg.flash = this.flash;
				chg.foreground = ~0;
				chg.background = ~0;
			}
		}

		if (chg.underline) {
			if (!this.underline)
				*p++ = '2'; /* off */
			p = _stpcpy(p, "4;"); /* underline */
		}

		if (chg.bold) {
			if (!this.bold)
				*p++ = '2'; /* off */
			p = _stpcpy(p, "1;"); /* bold */
		}

		/* italic ignored */

		if (chg.flash) {
			if (!this.flash)
				*p++ = '2'; /* off */
			p = _stpcpy(p, "5;"); /* flash */
		}

		/* FIXME what is the real buffer size? */
		if (chg.foreground)
			p += snprintf(p, 4, "3%c;", '0'
				     + match_color8(pg->color_map[this.foreground]));

		if (chg.background)
			p += snprintf(p, 4, "4%c;", '0'
				     + match_color8(pg->color_map[this.background]));

		if (p[-1] == '[')
			p -= 2; /* no change */
		else
			p[-1] = 'm'; /* replace last semicolon */
	}

	if (!vbi_is_print(this.unicode)) {
		if (vbi_is_gfx(this.unicode))
			this.unicode = text->gfx_chr;
		else
			this.unicode = 0x0020;
	}

	if (!print_unicode(text->cd, endian, this.unicode, &p,
			   text->buf + sizeof(text->buf) - p)) {
		vbi_export_write_error(&text->export);
		return 0;
	}

	return p - text->buf;
}

static vbi_bool
export				(vbi_export *		e,
				 vbi_page *		pg)
{
	int endian = vbi_ucs2be();
	text_instance *text = PARENT(e, text_instance, export);
	vbi_page page;
	vbi_char *cp, old;
	int column, row, n;
	const char *charset;

	if (text->charset && text->charset[0])
		charset = text->charset;
	else
		charset = iconv_formats[text->format];

	text->cd = iconv_open (charset, "UCS-2");
	if ((iconv_t) -1 == text->cd || endian < 0) {
		vbi_export_error_printf(&text->export,
					_("Character conversion Unicode "
					  "(UCS-2) to %s not supported."),
			charset);

		if ((iconv_t) -1 != text->cd)
			iconv_close (text->cd);

		return FALSE;
	}

	page = *pg;

	/* optimize */

	memset(&old, ~0, sizeof(old));

	for (cp = page.text, row = 0;;) {
		for (column = 0; column < pg->columns; column++) {
			n = print_char(text, endian, &page, old, *cp);

			if (n < 0) {
				; /* skipped */
			} else if (n == 0) {
				iconv_close(text->cd);
				return FALSE;
			} else if (n == 1) {
				vbi_export_putc (e, text->buf[0]);
			} else {
				vbi_export_write (e, text->buf, n);
			}

			old = *cp++;
		}

		row++;

		if (row >= pg->rows) {
			if (text->term > 0)
				vbi_export_printf (e, "\e[m\n"); /* reset */
			else
				vbi_export_putc (e, '\n');
			break;
		} else {
			vbi_export_putc (e, '\n');
		}
	}

	iconv_close(text->cd);

	return !e->write_error;
}

static vbi_export_info
info_text = {
	.keyword	= "text",
	.label		= N_("Text"),
	.tooltip	= N_("Export this page as text file"),

	.mime_type	= "text/plain",
	.extension	= "txt",
};

vbi_export_class
vbi_export_class_text = {
	._public		= &info_text,
	._new			= text_new,
	._delete		= text_delete,
	.option_enum		= option_enum,
	.option_get		= option_get,
	.option_set		= option_set,
	.export			= export
};

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
