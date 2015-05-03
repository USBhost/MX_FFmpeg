/*
 *  libzvbi test
 *
 *  Copyright (C) 2000, 2001, 2008 Michael H. Schimek
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

/* $Id: unicode.c,v 1.13 2008/09/11 02:47:12 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <assert.h>
#include <limits.h>

#include "src/lang.h"
#include "src/misc.h"

static void
putwchar			(unsigned int		c)
{
	if (c < 0x80) {
		putchar (c);
        } else if (c < 0x800) {
		putchar (0xC0 | (c >> 6));
		putchar (0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		putchar (0xE0 | (c >> 12));
		putchar (0x80 | ((c >> 6) & 0x3F));
		putchar (0x80 | (c & 0x3F));
	} else if (c < 0x200000) {
		putchar (0xF0 | (c >> 18));
		putchar (0x80 | ((c >> 12) & 0x3F));
		putchar (0x80 | ((c >> 6) & 0x3F));
		putchar (0x80 | (c & 0x3F));
	}
}

static void
putwstr				(const char *		s)
{
	for (; *s; s++)
		putwchar (*s);
}

static const unsigned int
national [] = {
	0x23, 0x24, 0x40, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
	0x60, 0x7B, 0x7C, 0x7D, 0x7E
};

static void
print_set			(const char *		name,
				 unsigned int		s)
{
	unsigned int i, j;

	putwstr (name);
	putwchar ('\n');

	for (i = 0; i < 16; ++i) {
		for (j = 2; j < 8; ++j) {
			putwchar (vbi_teletext_unicode (s, 0, j * 16 + i));
			putwchar (' ');
		}

		putwchar ('\n');
	}

	putwchar ('\n');
}

static void
teletext_composed		(vbi_bool		upper_case)
{
	unsigned int offs;
	unsigned int i, j;

	offs = upper_case ? 0x00 : 0x20;

	putwstr ("Teletext composed glyphs\n\n   ");

	for (i = 0x40; i < 0x60; ++i)
		putwchar (vbi_teletext_unicode (1, 0, i | offs));

	putwstr ("\n\n");

	for (i = 0; i < 16; ++i) {
		putwchar (vbi_teletext_unicode (2, 0, 0x40 + i));
		putwstr ("  ");

		for (j = 0x40; j < 0x60; ++j) {
			unsigned int c;

			c = vbi_teletext_composed_unicode (i, j | offs);
			putwchar ((0 == c) ? '-' : c);
		}

		putwchar ('\n');
	}

	putwchar ('\n');
}

static int
is_teletext_composed		(unsigned int		uc)
{
	unsigned int i, j;

	for (i = 0; i < 16; ++i) {
		for (j = 0x20; j < 0x80; ++j) {
			if (uc == vbi_teletext_composed_unicode (i, j))
				return TRUE;
		}
	}

	return FALSE;
}

static void
teletext_composed_inv		(void)
{
	unsigned int i, j;

	putwstr ("Teletext composed glyphs (Unicode U+0080 ... U+00FF)\n\n");

	for (i = 0; i < 16; ++i) {
		for (j = 0x080; j < 0x100; j += 0x10) {
			putwchar (is_teletext_composed (i + j) ? i + j : '-');
			putwchar (' ');
		}

		putwchar ('\n');
	}

	putwchar ('\n');

	putwstr ("Teletext composed glyphs (Unicode U+0100 ... U+017F)\n\n");

	for (i = 0; i < 16; ++i) {
		for (j = 0x100; j < 0x180; j += 0x10) {
			putwchar (is_teletext_composed (i + j) ? i + j : '-');
			putwchar (' ');
		}

		putwchar ('\n');
	}

	putwchar ('\n');
}

int
main				(int			argc,
				 char **		argv)
{
	unsigned int i, j;

	argc = argc; /* unused */
	argv = argv;

	putwstr ("libzvbi unicode test -*- coding: utf-8 -*-\n\n");
	putwstr ("ETS 300 706 Table 36: Latin National Option Sub-sets\n\n");

	for (i = 1; i < 14; ++i) {
		for (j = 0; j < N_ELEMENTS (national); ++j) {
			putwchar (vbi_teletext_unicode (1, i, national[j]));
			putwchar (' ');
		}

		putwchar ('\n');
	}

	putwchar ('\n');

	print_set ("ETS 300 706 Table 35: Latin G0 Primary Set\n", 1);
	print_set ("ETS 300 706 Table 37: Latin G2 Supplementary Set\n", 2);
	print_set ("ETS 300 706 Table 38: Cyrillic G0 Primary Set "
		   "- Option 1 - Serbian/Croatian\n", 3);
	print_set ("ETS 300 706 Table 39: Cyrillic G0 Primary Set "
		   "- Option 2 - Russian/Bulgarian\n", 4);
	print_set ("ETS 300 706 Table 40: Cyrillic G0 Primary Set "
		   "- Option 3 - Ukrainian\n", 5);
	print_set ("ETS 300 706 Table 41: Cyrillic G2 Supplementary Set\n", 6);
	print_set ("ETS 300 706 Table 42: Greek G0 Primary Set\n", 7);
	print_set ("ETS 300 706 Table 43: Greek G2 Supplementary Set\n", 8);
	print_set ("ETS 300 706 Table 44: Arabic G0 Primary Set\n", 9);
	print_set ("ETS 300 706 Table 45: Arabic G2 Supplementary Set\n", 10);
	print_set ("ETS 300 706 Table 46: Hebrew G0 Primary Set\n", 11);

	putwstr ("ETS 300 706 Table 47: G1 Block Mosaics Set\n\n");

	for (i = 0; i < 16; ++i) {
		for (j = 2; j < 8; ++j) {
			if (j == 4 || j == 5)
				putwchar (' ');
			else
				putwchar (vbi_teletext_unicode
					  (12, 0, j * 16 + i));

			putwchar (' ');
		}

		putwchar ('\n');
	}

	putwchar ('\n');

	print_set ("ETS 300 706 Table 48: G3 Smooth Mosaics and "
		   "Line Drawing Set\n", 13);

	teletext_composed (/* upper_case */ TRUE);
	teletext_composed (/* upper_case */ FALSE);

	teletext_composed_inv ();

	putwstr ("\nEIA 608 Closed Caption Basic Character Set\n\n");

	for (i = 0; i < 8; ++i) {
		for (j = 0x20; j < 0x80; j += 8) {
			putwchar (vbi_caption_unicode (j + i, FALSE));
			putwchar (' ');
		}

		putwstr ("       ");

		for (j = 0x20; j < 0x80; j += 8) {
			putwchar (vbi_caption_unicode (j + i, TRUE));
			putwchar (' ');
		}

		putwchar ('\n');
	}

	putwstr ("\n\nEIA 608 Closed Caption "
		 "Special Characters (0x1130+n)\n\n");

	for (i = 0; i < 16; ++i)
		putwchar (vbi_caption_unicode (0x1130 + i, FALSE));
	putwchar ('\n');
	for (i = 0; i < 16; ++i)
		putwchar (vbi_caption_unicode (0x1130 + i, TRUE));

	putwstr ("\n\nEIA 608 Closed Caption "
		 "Extended Characters (0x1220+n)\n\n");

	for (i = 0; i < 32; ++i)
		putwchar (vbi_caption_unicode (0x1220 + i, FALSE));
	putwchar ('\n');
	for (i = 0; i < 32; ++i)
		putwchar (vbi_caption_unicode (0x1220 + i, TRUE));

	putwstr ("\n\nEIA 608 Closed Caption "
		 "Extended Characters (0x1320+n)\n\n");

	for (i = 0; i < 32; ++i)
		putwchar (vbi_caption_unicode (0x1320 + i, FALSE));
	putwchar ('\n');
	for (i = 0; i < 32; ++i)
		putwchar (vbi_caption_unicode (0x1320 + i, TRUE));

	putwchar ('\n');

	assert ('a' == vbi_caption_unicode ('a', FALSE));
	assert ('A' == vbi_caption_unicode ('a', TRUE));
	assert ('A' == vbi_caption_unicode ('a', -1));
	assert ('A' == vbi_caption_unicode ('a', INT_MAX));

	for (i = 0; i < 2; ++i) {
		assert (0 == vbi_caption_unicode (-1, i));
		assert (0 == vbi_caption_unicode (0x80, i));
		assert (0 == vbi_caption_unicode (0x1130 - 1, i));
		assert (0 == vbi_caption_unicode (0x1130 + 16, i));
		assert (0 == vbi_caption_unicode (0x1220 - 1, i));
		assert (0 == vbi_caption_unicode (0x1220 + 32, i));
		assert (0 == vbi_caption_unicode (0x1320 - 1, i));
		assert (0 == vbi_caption_unicode (0x1320 + 32, i));
		assert (0 == vbi_caption_unicode (INT_MAX, i));
	}

	exit (EXIT_SUCCESS);
}
