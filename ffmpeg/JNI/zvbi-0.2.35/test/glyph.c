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

/* $Id: glyph.c,v 1.11 2008/03/01 07:36:46 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/lang.h"
#include "src/export.h"

static vbi_page *pg;
static vbi_char ac;
static int cx, cy;

static void
new_page(void)
{
	int i;

	cx = 0;
	cy = 0;

	memset(&ac, 0, sizeof(ac));

	ac.unicode = 0x0020;
	ac.foreground = 1;
	ac.background = 0;

	assert((pg = calloc(1, sizeof(*pg))));

	pg->rows = 25;
	pg->columns = 40;

	for (i = 0; i < pg->rows * pg->columns; i++)
		pg->text[i] = ac;

	pg->color_map[0] = 0x00000000;
	pg->color_map[1] = 0x00FFFFFF;
}

static void
putwchar(int c)
{
	if (c == '\n') {
		cx = 0;
		if (cy < pg->rows - 1)
			cy++;
	} else {
		ac.unicode = c;
		pg->text[cy * pg->columns + cx] = ac;
		if (cx < pg->columns - 1)
			cx++;
	}
}

static void
putwstr(const char *s)
{
	for (; *s; s++)
		putwchar(*s);
}

static const char
national[] = {
	0x23, 0x24, 0x40, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
	0x60, 0x7B, 0x7C, 0x7D, 0x7E
};

static void
store(int s)
{
	vbi_export *e;
	char buf[80];

	assert((e = vbi_export_new("ppm", NULL)));

	snprintf(buf, sizeof(buf) - 1, "char_set_%u.ppm", s);

	vbi_export_file(e, buf, pg);

	vbi_export_delete(e);

	free(pg);
}

static void
print_set(const char *name, int s)
{
	int i, j;

	new_page();

	putwstr(name);
	putwchar('\n');

	for (i = 0; i < 16; i++) {
		for (j = 2; j < 8; j++) {
			putwchar(vbi_teletext_unicode(s, 0, j * 16 + i));
			putwchar(' ');
		}
		putwchar('\n');
	}

	store(s);
}

int
main(int argc, char **argv)
{
	unsigned int i;

	argc = argc;
	argv = argv;

	new_page();

	putwstr("ETS 300 706 Table 36: Latin National Option Sub-sets\n\n");

	for (i = 1; i < 14; i++) {
		unsigned int j;

		for (j = 0; j < sizeof(national) / sizeof(national[0]); j++) {
			putwchar(vbi_teletext_unicode(1, i, national[j]));
			putwchar(' ');
		}

		putwchar('\n');
	}

	store(0);

	print_set("ETS 300 706 Table 35: Latin G0 Primary Set\n", 1);
	print_set("ETS 300 706 Table 37: Latin G2 Supplementary Set\n", 2);
	print_set("ETS 300 706 Table 38: Cyrillic G0 Primary Set - Option 1 - Serbian/Croatian\n", 3);
	print_set("ETS 300 706 Table 39: Cyrillic G0 Primary Set - Option 2 - Russian/Bulgarian\n", 4);
	print_set("ETS 300 706 Table 40: Cyrillic G0 Primary Set - Option 3 - Ukrainian\n", 5);
	print_set("ETS 300 706 Table 41: Cyrillic G2 Supplementary Set\n", 6);
	print_set("ETS 300 706 Table 42: Greek G0 Primary Set\n", 7);
	print_set("ETS 300 706 Table 43: Greek G2 Supplementary Set\n", 8);
	print_set("ETS 300 706 Table 44: Arabic G0 Primary Set\n", 9);
	print_set("ETS 300 706 Table 45: Arabic G2 Supplementary Set\n", 10);
	print_set("ETS 300 706 Table 46: Hebrew G0 Primary Set\n", 11);

	new_page();

	putwstr("ETS 300 706 Table 47: G1 Block Mosaics Set\n\n");

	for (i = 0; i < 16; i++) {
		unsigned int j;

		for (j = 2; j < 8; j++) {
			if (j == 4 || j == 5)
				putwchar(' ');
			else
				putwchar(vbi_teletext_unicode(12, 0, j * 16 + i));
			putwchar(' ');
		}
		putwchar('\n');
	}

	store(12);

	print_set("ETS 300 706 Table 48: G3 Smooth Mosaics and Line Drawing Set\n", 13);

	new_page();

	putwstr("Teletext composed glyphs\n\n   ");

	for (i = 0x40; i < 0x60; i++)
		putwchar(vbi_teletext_unicode(1, 0, i));
	putwstr("\n\n");

	for (i = 0; i < 16; i++) {
		unsigned int j;

		putwchar(vbi_teletext_unicode(2, 0, 0x40 + i));
		putwstr("  ");

		for (j = 0x40; j < 0x60; j++) {
			unsigned int c = vbi_teletext_composed_unicode(i, j);

			putwchar((c == 0) ? '-' : c);
		}

		putwchar('\n');
	}

	store(14);

	new_page();

	putwstr("Teletext composed glyphs\n\n   ");

	for (i = 0x60; i < 0x80; i++)
		putwchar(vbi_teletext_unicode(1, 0, i));
	putwstr("\n\n");

	for (i = 0; i < 16; i++) {
		unsigned int j;

		putwchar(vbi_teletext_unicode(2, 0, 0x40 + i));
		putwstr("  ");

		for (j = 0x60; j < 0x80; j++) {
			unsigned int c = vbi_teletext_composed_unicode(i, j);

			putwchar((c == 0) ? '-' : c);
		}

		putwchar('\n');
	}

	store(15);

	new_page();
	pg->columns = 32;
	pg->rows = 16;

	putwstr("EIA 608 Closed Captioning Basic Character Set\n\n");

	for (i = 0; i < 8; i++) {
		unsigned int j;

		for (j = 0x20; j < 0x80; j += 8) {
			putwchar(vbi_caption_unicode(j + i,
						     /* to_upper */ FALSE));
			putwchar(' ');
		}

		putwchar('\n');
	}

	store(16);

	new_page();
	pg->columns = 32;
	pg->rows = 16;

	putwstr("EIA 608 Closed Captioning Special Characters\n\n");

	for (i = 0; i < 16; i++) {
		putwchar(vbi_caption_unicode(0x1130 | i,
					     /* to_upper */ FALSE));
	}

	store(17);

	exit(EXIT_SUCCESS);
}
