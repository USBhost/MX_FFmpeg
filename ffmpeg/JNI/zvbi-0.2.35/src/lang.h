/*
 *  libzvbi -- Teletext and Closed Caption character set
 *
 *  Copyright (C) 2000, 2001 Michael H. Schimek
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

/* $Id: lang.h,v 1.9 2008/02/19 00:35:20 mschimek Exp $ */

#ifndef LANG_H
#define LANG_H

#include "bcd.h" /* vbi_bool */
#include "format.h" /* vbi_page */

/**
 * @internal
 *
 * Teletext character set according to ETS 300 706, Section 15.
 */
typedef enum {
	LATIN_G0 = 1,
	LATIN_G2,
	CYRILLIC_1_G0,
	CYRILLIC_2_G0,
	CYRILLIC_3_G0,
	CYRILLIC_G2,
	GREEK_G0,
	GREEK_G2,
	ARABIC_G0,
	ARABIC_G2,
	HEBREW_G0,
	BLOCK_MOSAIC_G1,
	SMOOTH_MOSAIC_G3
} vbi_character_set;

/**
 * @internal
 *
 * Teletext Latin G0 national option subsets according to
 * ETS 300 706, Section 15.2; Section 15.6.2 Table 36.
 */
typedef enum {
	NO_SUBSET,
	CZECH_SLOVAK,
	ENGLISH,
	ESTONIAN,
	FRENCH,
	GERMAN,
	ITALIAN,
	LETT_LITH,
	POLISH,
	PORTUG_SPANISH,
	RUMANIAN,
	SERB_CRO_SLO,
	SWE_FIN_HUN,
	TURKISH
} vbi_national_subset;

/**
 * @internal
 *
 * vbi_font_descriptors[], array of vbi_font_descr implements
 * the Teletext character set designation tables in ETS 300 706,
 * Section 15: Table 32, 33 and 34.  
 */
struct vbi_font_descr {
	vbi_character_set	G0;
	vbi_character_set	G2;	
	vbi_national_subset	subset;		/* applies only to LATIN_G0 */
	char *			label;		/* Latin-1 */
};

extern struct vbi_font_descr	vbi_font_descriptors[88];

#define VALID_CHARACTER_SET(n) ((n) < 88 && vbi_font_descriptors[n].G0)

/* Public */

/**
 * @ingroup Page
 * @brief Opaque font descriptor.
 */
typedef struct vbi_font_descr vbi_font_descr;

/**
 * @ingroup Page
 * @param unicode Unicode as in vbi_char.
 * 
 * @return
 * @c TRUE if @a unicode represents a Teletext or Closed Caption
 * printable character. This excludes Teletext Arabic characters (which
 * are represented by private codes U+E600 ... U+E7FF until the conversion
 * table is ready), the Teletext Turkish currency sign U+E800 which is not
 * representable in Unicode, the Teletext G1 Block Mosaic and G3 Smooth
 * Mosaics and Line Drawing Set, with codes U+EE00 ... U+EFFF, and
 * Teletext DRCS coded U+F000 ... U+F7FF.
 */
_vbi_inline vbi_bool
vbi_is_print(unsigned int unicode)
{
	return unicode < 0xE600;
}

/**
 * @ingroup Page
 * @param unicode Unicode as in vbi_char.
 * 
 * @return
 * @c TRUE if @a unicode represents a Teletext G1 Block Mosaic or G3 Smooth
 * Mosaics and Line Drawing Set, that is a code in range U+EE00 ... U+EFFF.
 */
_vbi_inline vbi_bool
vbi_is_gfx(unsigned int unicode)
{
	return unicode >= 0xEE00 && unicode <= 0xEFFF;
}

/**
 * @ingroup Page
 * @param unicode Unicode as in vbi_char.
 * 
 * @return
 * @c TRUE if @a unicode represents a Teletext DRCS (Dynamically
 * Redefinable Character), that is a code in range U+F000 ... U+F7FF.
 **/
_vbi_inline vbi_bool
vbi_is_drcs(unsigned int unicode)
{
	return unicode >= 0xF000;
}

extern unsigned int
vbi_caption_unicode		(unsigned int		c,
				 vbi_bool		to_upper);

/* Private */

extern unsigned int	vbi_teletext_unicode(vbi_character_set s, vbi_national_subset n, unsigned int c);
extern unsigned int	vbi_teletext_composed_unicode(unsigned int a, unsigned int c);
extern void		vbi_optimize_page(vbi_page *pg, int column, int row, int width, int height);

#endif /* LANG_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
