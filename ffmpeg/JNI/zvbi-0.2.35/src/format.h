/*
 *  libzvbi -- Formatted Teletext and Closed Caption page
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

/* $Id: format.h,v 1.11 2008/02/19 00:35:19 mschimek Exp $ */

#ifndef FORMAT_H
#define FORMAT_H

#include "event.h" /* vbi_nuid */

#ifndef VBI_DECODER
#define VBI_DECODER
typedef struct vbi_decoder vbi_decoder;
#endif

/* Public */

#include <inttypes.h>

/**
 * @addtogroup Page Formatted text page
 * @ingroup HiDec
 */

/**
 * @ingroup Page
 * @brief Index into the vbi_page->color_map.
 *
 * The enumerated color names
 * refer to the Teletext and Closed Caption base palette of eight
 * colors. Note however the color_map really has 40 entries for
 * Teletext Level 2.5+, 32 of which are redefinable, the remaining
 * eight are private colors of libzvbi e. g. for
 * navigational information. So these symbols may not necessarily
 * correspond to the respective color.
 */
/* Code depends on order, don't change. */
typedef enum {
	VBI_BLACK,
	VBI_RED,
	VBI_GREEN,
	VBI_YELLOW,
	VBI_BLUE,
	VBI_MAGENTA,
	VBI_CYAN,
	VBI_WHITE
} vbi_color;

/**
 * @ingroup Page
 * @brief Colormap entry: 0xAABBGGRR. libzvbi sets the alpha channel
 * always to 0xFF.
 */
typedef uint32_t vbi_rgba;

/* Private */

#define VBI_RGBA(r, g, b)						\
	((((r) & 0xFF) << 0) | (((g) & 0xFF) << 8)			\
	 | (((b) & 0xFF) << 16) | (0xFF << 24))
#define VBI_R(rgba) (((rgba) >> 0) & 0xFF)
#define VBI_G(rgba) (((rgba) >> 8) & 0xFF)
#define VBI_B(rgba) (((rgba) >> 16) & 0xFF)
#define VBI_A(rgba) (((rgba) >> 24) & 0xFF)

/* Public */

/**
 * @ingroup Page
 * @brief Defines the opacity of a vbi_char and vbi_page border.
 *
 * Teletext Level 2.5 defines a special transparent color which
 * permits unusual characters with transparent foreground, opaque
 * background. For simplicity this type of opacity has been omitted. Also
 * renderers shall rely on the opacity attribute and not attempt to
 * interpret the color value as transparency indicator.
 */
typedef enum {
	/**
	 * This page is supposed to be overlayed onto
	 * video, with video displayed in place of this character (or the page
	 * border). In other words the character is a space (vbi_char->unicode =
	 * U+0020) and the glyph background is transparent. If desired the
	 * renderer may also fall back to VBI_SEMI_TRANSPARENT or VBI_OPAQUE
	 * mode. For this case vbi_char->background names the color to use as
	 * the semi-transparent or opaque background.
	 *
	 * VBI_TRANSPARENT_SPACE is the opacity of subtitle pages (both border and
	 * characters, while the 'boxed' words are marked as VBI_SEMI_TRANSPARENT),
	 * but can also occur on a mainly VBI_OPAQUE page to create a 'window'
	 * effect.
 	 */
	VBI_TRANSPARENT_SPACE,
	/**
	 * Display video instead of the background color.
	 * Here the character is <em>not</em> a space and shall be displayed
	 * in vbi_char->foreground color. Only in the background of the character
	 * video shall look through. Again the renderer may fall back to
	 * VBI_SEMI_TRANSPARENT or VBI_OPAQUE.
	 */
	VBI_TRANSPARENT_FULL,
	/**
	 * Alpha blend video into background color, the
	 * character background becomes translucent. This is the opacity used
	 * for 'boxed' text on an otherwise VBI_TRANSPARENT_SPACE page, typically
	 * a subtitle or Teletext newsflash page. The renderer may fall back
	 * to VBI_OPAQUE.
	 */
	VBI_SEMI_TRANSPARENT,
	/**
	 * Display foreground and background color. Showing
	 * foreground or background transparent instead is not recommended because
	 * the editor may have swapped foreground and background color, then
	 * replaced a glyph by its inverse image, so one cannot really know if
	 * the character foreground or background will appear transparent.
	 */
	VBI_OPAQUE
} vbi_opacity;

/**
 * @ingroup Page
 * @brief Defines the size of a vbi_char in a vbi_page.

 * Double width or height characters expand into the next
 * column right and/or next row below.
 * 
 * Scanning two rows left to right, you will find<br>
 * <pre>
 * VBI_NORMAL_SIZE | VBI_DOUBLE_WIDTH VBI_OVER_TOP | VBI_DOUBLE_HEIGHT  | VBI_DOUBLE_SIZE  VBI_OVER_TOP
 *       x         |         x             x       | VBI_DOUBLE_HEIGHT2 | VBI_DOUBLE_SIZE2 VBI_OVER_BOTTOM
 * </pre>
 * 
 * A VBI_DOUBLE_HEIGHT2, VBI_DOUBLE_SIZE2, VBI_OVER_TOP, VBI_OVER_BOTTOM
 * vbi_char has the same character unicode and attributes as the top/left anchor.
 * Partial characters (like a single VBI_DOUBLE_HEIGHT2) will not appear, so
 * VBI_DOUBLE_HEIGHT2, VBI_DOUBLE_SIZE2, VBI_OVER_TOP, VBI_OVER_BOTTOM
 * can be safely ignored when scanning the page.
 */
/* Code depends on order, don't change. */
typedef enum {
	VBI_NORMAL_SIZE, VBI_DOUBLE_WIDTH, VBI_DOUBLE_HEIGHT, VBI_DOUBLE_SIZE,
	VBI_OVER_TOP, VBI_OVER_BOTTOM, VBI_DOUBLE_HEIGHT2, VBI_DOUBLE_SIZE2
} vbi_size;

/**
 * @ingroup Page
 * @brief Attributed character.
 */
typedef struct vbi_char {
	unsigned	underline	: 1;	/**< Display character underlined. */
	unsigned	bold		: 1;	/**< Display character bold. */
	unsigned	italic		: 1;	/**< Display character slanted right. */
	/**
	 * Display character or space (U+0020), one second cycle time.
	 */
	unsigned	flash		: 1;
	/**
	 * Replace character by space (U+0020) if not revealed.
	 * This is used for example to hide text on question & answer pages.
	 */
	unsigned	conceal		: 1;
	/**
	 * No function yet, default is fixed spacing.
	 */
	unsigned	proportional	: 1;
	/**
	 * This character is part of a hyperlink. Call vbi_resolve_link()
	 * to get more information.
	 */
	unsigned	link		: 1;
	/**
	 * Reserved for VPT link flag.
	 */
	unsigned	reserved	: 1;
	/**
	 * Character size, see vbi_size.
	 */
	unsigned	size		: 8;
	/**
	 * Character opacity, see vbi_opacity. Both @a foreground
	 * and @a background color are valid independent of @a opacity.
	 */
	unsigned	opacity		: 8;
	/**
	 * Character foreground color, a vbi_color index
	 * into the vbi_page->color_map.
	 */
	unsigned	foreground	: 8;
	/**
	 * Character background color, a vbi_color index
	 * into the vbi_page->color_map.
	 */
	unsigned	background	: 8;
	/**
	 * DRCS color look-up table offset, see vbi_page for details.
	 */
	unsigned	drcs_clut_offs	: 8;
	/**
	 * Character code according to ISO 10646 UCS-2 (not UTF-16).
	 * 
	 * All Closed Caption characters can be represented in Unicode,
	 * but unfortunately not all Teletext characters.
	 * 
	 * <a href="http://www.etsi.org">ETS 300 706
	 * </a> Table 36 Latin National Subset Turkish, character
	 * 0x23 "Turkish currency symbol" is not representable in Unicode,
	 * thus translated to private code U+E800. I was unable to identify
	 * all Arabic glyphs in Table 44 and 45 Arabic G0 and G2, so for now
	 * these are mapped to private code U+E620 ... U+E67F and U+E720 ...
	 * U+E77F respectively. Table 47 G1 Block Mosaic is not representable
	 * in Unicode, translated to private code U+EE00 ... U+EE7F. That is,
	 * the contiguous form has bit 5 (0x20) set, the separate form cleared.
	 * Table 48 G3 "Smooth Mosaics and Line Drawing Set" is not
	 * representable in Unicode, translated to private code
	 * U+EF20 ... U+EF7F.
	 * 
	 * Teletext Level 2.5+ DRCS are represented by private code
	 * U+F000 ... U+F7FF. The 6 lsb select character 0x00 ... 0x3F
	 * from a DRCS plane, the 5 msb select DRCS plane 0 ... 31, see
	 * vbi_page for details.
	 *
	 * @bug
	 * Some Teletext character sets contain complementary
	 * Latin characters. For example the Greek capital letters Alpha
	 * and Beta are re-used as Latin capital letter A and B, while a
	 * separate code exists for Latin capital letter C. libzvbi will
	 * not analyse the page contents, so Greek A and B are always
	 * translated to Greek Alpha and Beta, C to Latin C, even if they
	 * appear in a pure Latin character word.
	 */
	unsigned	unicode		: 16;
} vbi_char;

struct vbi_font_descr;

/**
 * @ingroup Page
 * @brief Formatted Teletext or Closed Caption page.
 *
 * Clients can fetch pages
 * from the respective cache using vbi_fetch_vt_page() or
 * vbi_fetch_cc_page() for evaluation, display or output. Since
 * the page may reference other objects in cache which are locked
 * by the fetch functions, vbi_unref_page() must be called when done.
 * Note this structure is large, some 10 KB.
 */
typedef struct vbi_page {
	/**
	 * Points back to the source context.
	 */ 
	vbi_decoder *		vbi;

	/**
	 * Identifies the network broadcasting this page.
	 */
        vbi_nuid	       	nuid;
	/**
	 * Page number, see vbi_pgno.
	 */
	/* FIXME this shouldn't be int */
 	int			pgno;
	/**
	 * Subpage number, see vbi_subno.
	 */
	/* FIXME this shouldn't be int */
	int			subno;
	/**
	 * Number of character rows in the page.
	 */
	int			rows;
	/**
	 * Number of character columns in the page.
	 */
	int			columns;
	/**
	 * The page contents, these are @a rows x @a columns without
	 * padding between the rows. See vbi_char for details.
	 */
	vbi_char		text[1056];

	/**
	 * To speed up rendering these variables mark the rows
	 * which actually changed since the page has been last fetched
	 * from cache. @a y0 ... @a y1 are the first to last row changed,
	 * inclusive, in range 0 ... @a rows - 1. @a roll indicates the
	 * page has been vertically scrolled this number of rows,
	 * negative numbers up (towards lower row numbers), positive
	 * numbers down. For example -1 means row @a y0 + 1 ... @a y1
	 * moved to @a y0 ... @a y1 - 1, erasing row @a y1 to all spaces.
	 * 
	 * Practically this is only used in Closed Caption roll-up
	 * mode, otherwise all rows are always marked dirty. Clients
	 * are free to ignore this information.
	 */
	struct {
	     /* int			x0, x1; */
		int			y0, y1;
		int			roll;
	}			dirty;

	/**
	 * When a TV displays Teletext or Closed Caption
	 * pages, only a section in the center of the screen is
	 * actually covered by characters. The remaining space is
	 * referred to here as 'border', which can have a color different
	 * from the typical black. (In the Teletext specs this is referred
	 * to as the screen color, hence the field name.) This is a
	 * vbi_color index into the @a color_map.
	 */
	vbi_color		screen_color;
	/**
	 * The 'border' can also have a distinguished
	 * opacity. Typically this will be VBI_OPAQUE, but pages intended
	 * for overlay onto video (Teletext subtitles, newsflash, Caption
	 * pages) will have a @a screen_opacity of VBI_TRANSPARENT_SPACE.
	 * See vbi_opacity for details.
	 */
	vbi_opacity		screen_opacity;
	/**
	 * This is the color palette indexed by vbi_color in
	 * vbi_char and elsewhere, colors defined as vbi_rgba. Note this
	 * palette may not correspond to the vbi_color enumeration since
	 * Teletext allows editors to redefine the entire palette.
	 * Closed Caption and Teletext Level 1.0/1.5 pages use
	 * entries 0 ... 7. Teletext Level 2.5/3.5 pages use entries
	 * 0 ... 31. Navigation related text (TOP, FLOF) added by libzvbi
	 * uses entries 32 ... 39 which are not subject to redefinition.
	 */
	vbi_rgba 		color_map[40];

	/**
	 * DRCS (dynamically redefinable characters) can have
	 * two, four or sixteen different colors. Without further details,
	 * the effective color of each pixel is given by
	 * @code
	 * vbi_page->color_map[drcs_clut[drcs pixel color + vbi_char->drcs_clut_offs]],
	 * @endcode
	 * whereby drcs_clut[0] shall be replaced by vbi_char->foreground,
	 * drcs_clut[1] by vbi_char->background. (Renderers are supposed to convert the
	 * drcs_clut into a private color map of the desired pixel format.)
	 * 
	 * Practically vbi_char->drcs_clut_offs encodes the DRCS color depth
	 * and selects between the vbi_char colors and one of two 4- or
	 * 16-entry Color Look-Up Tables. Also the different resolution of DRCS and
	 * the bitplane color coding is hidden to speed up rendering.
	 */
	uint8_t *		drcs_clut;		/* 64 entries */
	/**
	 * Pointer to DRCS data. Per definition the maximum number of DRCS
	 * usable at the same time, i. e. on one page, is limited to 96. However the
	 * number of DRCS defined in a Teletext data stream can be much larger. The
	 * 32 pointers here correspond to the 32 DRCS character planes mentioned
	 * in the vbi_char description. Each of them points to an array of character
	 * definitions, a DRCS font. One character occupies 60 bytes or 12 x 10 pixels,
	 * stored left to right and top to bottom. The color of each pixel (index
	 * into @a drcs_clut) is coded in four bits stored in little endian order,
	 * first pixel 0x0F, second pixel 0xF0 and so on. For example the first,
	 * top/leftmost pixel can be found at
	 * @code
	 * vbi_page->drcs[(unicode >> 6) & 0x1F][(unicode & 0x3F) * 60]
	 * @endcode
	 *
	 * Do not access DRCS data unless referenced by a vbi_char in @a text, a
	 * segfault may result. Do not access DRCS data after calling
	 * vbi_unref_page(), it may not be cached anymore.
	 */
	uint8_t *		drcs[32];

	struct {
		int			pgno, subno;
	}			nav_link[6];
	char			nav_index[64];

	struct vbi_font_descr *	font[2];
	unsigned int		double_height_lower;	/* legacy */

	vbi_opacity		page_opacity[2];
	vbi_opacity		boxed_opacity[2];
} vbi_page;

/* Private */

#endif /* FORMAT_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
