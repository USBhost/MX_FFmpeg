/*
 *  libzvbi -- Teletext decoder internals
 *
 *  Copyright (C) 2000, 2001, 2003, 2004, 2008 Michael H. Schimek
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

/* $Id: vt.h,v 1.13 2008/03/01 07:37:35 mschimek Exp $ */

#ifndef VT_H
#define VT_H

#include "version.h"
#include "bcd.h"		/* vbi_pgno, vbi_subno */
#if 2 == VBI_VERSION_MINOR
#  include "lang.h"
#  define VBI_TTX_CHARSET_CODE_NONE ((vbi_ttx_charset_code) -1)
typedef unsigned int vbi_ttx_charset_code;
typedef struct vbi_font_descr vbi_ttx_charset;
_vbi_inline const vbi_ttx_charset *
vbi_ttx_charset_from_code	(vbi_ttx_charset_code	code)
{
	if (VALID_CHARACTER_SET (code))
		return vbi_font_descriptors + code;
	else
		return NULL;
}
#elif 3 == VBI_VERSION_MINOR
#  include "lang.h"		/* vbi_ttx_charset_code */
#  include "page.h"		/* vbi_color */
#else
#  error VBI_VERSION_MINOR == ?
#endif

/**
 * @internal
 * EN 300 706 Section 9.4.2, Table 3: Page function.
 * (Packet X/28/0 Format 1, X/28/3 and X/28/4.)
 */
enum ttx_page_function {
	/**
	 * EN 300 706 annex L, EACEM/ECCA Automatic Channel
	 * Installation data (libzvbi internal code).
	 */
	PAGE_FUNCTION_ACI		= -5,

	/**
	 * Data broadcasting page coded according to EN 300 708
	 * clause 4 (Page Format - Clear) containing Electronic
	 * Programme Guide data according to EN 300 707 (NexTView).
	 * (Libzvbi internal code.)
	 */
	PAGE_FUNCTION_EPG		= -4,

	/**
	 * Page contains trigger messages defined according to EACEM
	 * TP 14-99-16 "Data Broadcasting", rev 0.8 (libzvbi internal
	 * code).
	 */
	PAGE_FUNCTION_EACEM_TRIGGER	= -3,

	/** Invalid data (libzvbi internal code). */
	PAGE_FUNCTION_DISCARD		= -2,

	/** Unknown page function (libzvbi internal code). */
	PAGE_FUNCTION_UNKNOWN		= -1,

	/** Basic level one page. */
	PAGE_FUNCTION_LOP		= 0,

	/**
	 * Data broadcasting page coded according
	 * to EN 300 708 Section 4 (Page Format - Clear).
	 */
	PAGE_FUNCTION_DATA,

	/** Global object definition page. */
	PAGE_FUNCTION_GPOP,

	/** Normal object definition page. */
	PAGE_FUNCTION_POP,

	/** Global DRCS downloading page. */
	PAGE_FUNCTION_GDRCS,

	/** Normal DRCS downloading page. */
	PAGE_FUNCTION_DRCS,

	/** Magazine Organization Table. */
	PAGE_FUNCTION_MOT,

	/** Magazine Inventory Page. */
	PAGE_FUNCTION_MIP,

	/** Basic TOP Table. */
	PAGE_FUNCTION_BTT,

	/** TOP Additional Information Table. */
	PAGE_FUNCTION_AIT,

	/** TOP Multi-Page Table. */
	PAGE_FUNCTION_MPT,

	/** TOP Multi-Page Extension Table. */
	PAGE_FUNCTION_MPT_EX,

	/**
	 * Page contains trigger messages defined according to IEC/PAS
	 * 62297 Edition 1.0 (2002-01): "Proposal for introducing a
	 * trigger mechanism into TV transmissions".
	 *
	 * Might be the same as PAGE_FUNCTION_EACEM_TRIGGER, but author
	 * got no copy of IEC/PAS 62297 to verify.
	 */
	PAGE_FUNCTION_IEC_TRIGGER
};

_vbi_inline vbi_bool
ttx_page_function_valid		(enum ttx_page_function	function)
{
	return ((unsigned int) function
		<= (unsigned int) PAGE_FUNCTION_IEC_TRIGGER);
}

extern const char *
ttx_page_function_name		(enum ttx_page_function	function);

/**
 * @internal
 * EN 300 706 Section 9.4.2, Table 3: Page coding bits.
 * (Packet X/28/0 Format 1, X/28/3 and X/28/4.)
 */
enum ttx_page_coding {
	/** Unknown coding (libzvbi internal code). */
	PAGE_CODING_UNKNOWN		= -1,

	/**
	 * 8 bit bytes with 7 data bits and one odd parity bit
	 * in the most significant position.
	 */
	PAGE_CODING_ODD_PARITY,

	/** 8 bit bytes with 8 data bits. */
	PAGE_CODING_UBYTES,

	/** Hamming 24/18 coded triplets (struct ttx_triplet). */
	PAGE_CODING_TRIPLETS,

	/** Hamming 8/4 coded 8 bit bytes. */
	PAGE_CODING_HAMMING84,

	/** Eight HAMMING84 bytes followed by twelve ODD_PARITY bytes. */
	PAGE_CODING_AIT,

	/**
	 * First byte is a Hamming 8/4 coded 4 bit ttx_page_coding
	 * value describing the remaining 39 bytes.
	 */
	PAGE_CODING_META84
};

_vbi_inline vbi_bool
ttx_page_coding_valid		(enum ttx_page_coding	coding)
{
	return ((unsigned int) coding
		<= (unsigned int) PAGE_CODING_META84);
}

extern const char *
ttx_page_coding_name		(enum ttx_page_coding	coding);

/**
 * @internal
 * Page function coded in TOP BTT links to other TOP pages.
 * top_page_number() translates these codes to enum ttx_page_function.
 */
enum ttx_top_page_function {
	/** Multi-Page Table. */
	TOP_PAGE_FUNCTION_MPT		= 1,

	/** Additional Information Table. */
	TOP_PAGE_FUNCTION_AIT,

	/** Multi-Page Extension Table. */
	TOP_PAGE_FUNCTION_MPT_EX,
};

/**
 * @internal
 * Page type coded in TOP BTT pages. decode_btt_page() translates
 * these codes to MIP page type (enum vbi_page_type).
 */
enum ttx_btt_page_type {
	BTT_NO_PAGE			= 0,

	/** Subtitle page. */
	BTT_SUBTITLE,

	/** Index page, single page. */
	BTT_PROGR_INDEX_S,

	/**
	 * Index page, multi-page
	 * (number of subpages coded in MPT or MPT-EX).
	 */
	BTT_PROGR_INDEX_M,

	/** First page of a block, single page. */
	BTT_BLOCK_S,

	/** First page of a block, multi-page. */
	BTT_BLOCK_M,

	/** First page of a group, single page. */
	BTT_GROUP_S,

	/** First page of a group, multi-page. */
	BTT_GROUP_M,

	/** Normal page, single page. */
	BTT_NORMAL_S,

	/** Unknown purpose. */
	BTT_NORMAL_9,

	/** Normal page, multi-page. */
	BTT_NORMAL_M,

	/** Unknown purpose. */
	BTT_NORMAL_11,

	/** Unknown purpose. */
	BTT_12,

	/** Unknown purpose. */
	BTT_13,

	/** Unknown purpose. */
	BTT_14,

	/** Unknown purpose. */
	BTT_15
};

/**
 * @internal
 * EN 300 706 Section 12.3.1, Table 28: Enhancement object type.
 */
enum ttx_object_type {
	/** Depending on context. */
	LOCAL_ENHANCEMENT_DATA		= 0,

	OBJECT_TYPE_NONE		= 0,
	OBJECT_TYPE_ACTIVE,
	OBJECT_TYPE_ADAPTIVE,
	OBJECT_TYPE_PASSIVE
};

extern const char *
ttx_object_type_name		(enum ttx_object_type	type);

/**
 * @internal
 * EN 300 706 Section 14.2, Table 31: DRCS modes.
 * EN 300 706 Section 9.4.6, Table 9: Coding of Packet
 * X/28/3 for DRCS Downloading Pages.
 */
enum ttx_drcs_mode {
	DRCS_MODE_12_10_1		= 0,
	DRCS_MODE_12_10_2,
	DRCS_MODE_12_10_4,
	DRCS_MODE_6_5_4,
	DRCS_MODE_SUBSEQUENT_PTU	= 14,
	DRCS_MODE_NO_DATA
};

extern const char *
ttx_drcs_mode_name		(enum ttx_drcs_mode	mode);

/** @internal */
#define DRCS_PTUS_PER_PAGE 48

/** @internal */
#define NO_PAGE(pgno) (((pgno) & 0xFF) == 0xFF)

/**
 * @internal
 * Teletext page link.
 */
struct ttx_page_link {
	/** Function of the target. */
	enum ttx_page_function		function;

	/**
	 * Page number of the target. NO_PAGE (pgno) == TRUE when
	 * this link is unused or broken.
	 */
	vbi_pgno			pgno;

	/**
	 * Subpage number of the target or VBI_NO_SUBNO.
	 *
	 * For X/27/4 ... 5 format 1 links (struct ttx_lop.link[])
	 * this is the set of required subpages (1 << (0 ... 15))
	 * instead.
	 */
	vbi_subno			subno;
};

extern void
ttx_page_link_dump		(const struct ttx_page_link *pl,
				 FILE *			fp);

/* Level one page enhancement. */

/**
 * @internal
 * EN 300 706 Section 12.3.1: Packet X/26 code triplet.
 * Broken triplets have all fields set to -1.
 */
struct ttx_triplet {
	uint8_t				address;
	uint8_t				mode;
	uint8_t				data;
};

/**
 * @internal
 * Level one page enhancements triplets (packets X/26).
 */
typedef struct ttx_triplet ttx_enhancement[16 * 13 + 1];

/* Level one page extension. */

/**
 * @internal
 * EN 300 706 Section 9.4.2.2: X/28/0, X/28/4 and
 * EN 300 706 Section 10.6.4: MOT POP link fallback flags.
 */
struct ttx_ext_fallback {
	vbi_bool			black_bg_substitution;

	int				left_panel_columns;
	int				right_panel_columns;
};

/**
 * @internal
 * Index of the "transparent" color in the Level 2.5/3.5 color_map[].
 */
#define VBI_TRANSPARENT_BLACK 8

/**
 * @internal
 * EN 300 706 Section 9.4.2: Packet X/28.
 * EN 300 706 Section 9.5: Packet M/29.
 */
struct ttx_extension {
	/**
	 * We have data from packets X/28 (in LOP) or M/29 (in magazine)
	 * with this set of designations. LOP pages without X/28 packets
	 * should fall back to the magazine defaults unless these bits
	 * are set. The extension data in struct ttx_magazine is always
	 * valid, contains global defaults as specified in EN 300 706
	 * (see _vbi_teletext_decoder_default_magazine()) unless M/29
	 * packets have been received.
	 *
	 * - 1 << 4: color_map[0 ... 15] is valid
	 * - 1 << 1: drcs_clut[] is valid
	 * - 1 << 0 or 1 << 4: the remaining fields are valid.
	 *
	 * color_map[32 .. 39] is always valid.
	 */
	unsigned int			designations;

	/** Primary and secondary character set. */
	vbi_ttx_charset_code		charset_code[2];

	/** Default colors. */
	unsigned int			def_screen_color;
	unsigned int			def_row_color;

	/**
	 * Adding these values (0, 8, 16, 24) to character color
	 * 0 ... 7 gives an index into color_map[] below.
	 */ 
	unsigned int			foreground_clut;
	unsigned int			background_clut;

	struct ttx_ext_fallback		fallback;

	/**
	 * DRCS color lookup table. Translates (G)DRCS pixel values to
	 * indices into the color_map[].
	 *
	 * - 2 dummy entries (12x10x1 (G)DRCS pixels are interpreted
	 *     like built-in character pixels, selecting the current
	 *     foreground or background color).
	 * - 4 entries for 12x10x2 GDRCS pixel 0 ... 3 to color_map[].
	 * - 4 more for local DRCS
	 * - 16 entries for 12x10x4 and 6x5x4 GDRCS pixel 0 ... 15
	 *	to color_map[].
	 * - 16 more for local DRCS.
	 */
#if 2 == VBI_VERSION_MINOR
	/* For compatibility with the drcs_clut pointer in vbi_page. */
	uint8_t				drcs_clut[2 + 2 * 4 + 2 * 16];
#elif 3 == VBI_VERSION_MINOR
	vbi_color			drcs_clut[2 + 2 * 4 + 2 * 16];
#else
#  error VBI_VERSION_MINOR == ?
#endif

	/**
	 * Five palettes of 8 each colors each.
	 *
	 * Level 1.0 and 1.5 pages use only palette #0, Level 2.5 and
	 * 3.5 pages use palette #0 ... #3.
	 *
	 * At Level 2.5 palette #2 and #3 are redefinable, except
	 * color_map[8] which is "transparent" color
	 * (VBI_TRANSPARENT_BLACK). At Level 3.5 palette #0 and #1
	 * are also redefinable.
	 *
	 * Palette #4 never changes and contains libzvbi internal
	 * colors for navigation bars, search text highlighting etc.
	 * These colors are roughly the same as the default palette #0,
	 * see vbi_color.
	 */
	vbi_rgba			color_map[40];
};

extern void
ttx_extension_dump		(const struct ttx_extension *ext,
				 FILE *			fp);

/**
 * @internal
 *
 * EN 300 706 Section 12.3.1, Table 28: Mode 10001, 10101 - Object
 * invocation, object definition. See also triplet_object_address().
 *
 * MOT default, POP and GPOP object address.
 *
 * n8  n7  n6  n5  n4  n3  n2  n1  n0
 * packet  triplet lsb ----- s1 -----
 */
typedef int ttx_object_address;

/**
 * @internal
 * Decoded TOP Additional Information Table entry.
 */
struct ttx_ait_title {
	struct ttx_page_link		link;
	uint8_t				text[12];
};

/* Basic level one page. */

/**
 * @internal
 * EN 300 706 Section 9.3.1.3: Control bits (~0xE03F7F),
 * EN 300 706 Section 15.2: National subset C12-C14,
 * EN 300 706 Appendix B.6: Transmission rules for enhancement data.
 */
enum ttx_flags {
	C4_ERASE_PAGE			= 0x000080,
	C5_NEWSFLASH			= 0x004000,
	C6_SUBTITLE			= 0x008000,
	C7_SUPPRESS_HEADER		= 0x010000,
	C8_UPDATE			= 0x020000,
	C9_INTERRUPTED			= 0x040000,
	C10_INHIBIT_DISPLAY		= 0x080000,
	C11_MAGAZINE_SERIAL		= 0x100000,
	C12_FRAGMENT			= 0x200000,
	C13_PARTIAL_PAGE		= 0x400000,
	C14_RESERVED			= 0x800000
};

/**
 * @internal
 * Basic level one page.
 */
struct ttx_lop {
	/** Raw data as received. */
	uint8_t				raw[26][40];

	/** Packet X/27/0-5 links. */
	struct ttx_page_link		link[6 * 6];

	/**
	 * Packet X/27 flag (ETR 287 section 10.4):
	 * Have FLOF navigation, display row 24.
	 */
	vbi_bool			have_flof;
};

/* Magazine defaults. */

/**
 * @internal
 * EN 300 706 Section 10.6.4: MOT object links.
 */
struct ttx_pop_link {
	vbi_pgno			pgno;
	struct ttx_ext_fallback		fallback;
	struct {
		enum ttx_object_type		type;
		ttx_object_address		address;
	}				default_obj[2];
};

/**
 * @internal
 * Magazine defaults.
 */
struct ttx_magazine {
	/** Default extension. */
	struct ttx_extension		extension;

	/**
	 * Converts page number to index into pop_link[] for default
	 * object invocation. Valid range 0 ... 7, -1 if broken.
	 */
	int8_t				pop_lut[0x100];

	/**
	 * Converts page number to index into drcs_link[] for default
	 * object invocation. Valid range 0 ... 7, -1 if broken.
	 */
	int8_t				drcs_lut[0x100];

	/**
	 * Level 2.5 [0] or 3.5 [1], one global [0] and seven local links
	 * to POP page. NO_PAGE(pop_link[][].pgno) == TRUE if the link
	 * is unused or broken.
	 */
	struct ttx_pop_link		pop_link[2][8];

	/**
	 * Level 2.5 [0] or 3.5 [1], one global [0] and seven local links
	 * to DRCS page. NO_PAGE(drcs_link[][]) == TRUE if the link
	 * is unused or broken.
	 */
	vbi_pgno			drcs_link[2][8];
};

extern const struct ttx_magazine *
_vbi_teletext_decoder_default_magazine (void);

/* Network data. */

/** @internal */
#define SUBCODE_SINGLE_PAGE		0x0000
/** @internal */
#define SUBCODE_MULTI_PAGE		0xFFFE
/** @internal */
#define SUBCODE_UNKNOWN			0xFFFF

/**
 * @internal
 * Internal teletext page statistics.
 */
struct ttx_page_stat {
	/* Information gathered from MOT, MIP, BTT, G/POP pages. */

	/** Actually vbi_page_type. */
	uint8_t				page_type;

	/** Actually vbi_ttx_charset_code, 0xFF if unknown. */
	uint8_t				charset_code;

	/**
	 * Highest subpage number transmitted according to MOT, MIP, BTT.
	 * - 0x0000		single page (SUBCODE_SINGLE_PAGE)
	 * - 0x0002 - 0x0079	multi-page
	 * - 0x0080 - 0x3F7F	clock page, other pages with non-standard
	 *			subpages not to be cached
	 * These codes were not transmitted but generated by libzvbi:
	 * - 0xFFFE		has 2+ subpages (SUBCODE_MULTI_PAGE)
	 * - 0xFFFF		unknown (SUBCODE_UNKNOWN)
	 */
	uint16_t			subcode;

	/** Last received page ttx_flags (cache_page.flags). */
	uint32_t			flags;

	/* Cache statistics. */

	/** Subpages cached now and ever. */
	uint8_t				n_subpages;
	uint8_t				max_subpages;

	/** Subpage numbers actually received (0x00 ... 0x79). */
	uint8_t				subno_min;
	uint8_t				subno_max;
};

#endif /* VT_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
