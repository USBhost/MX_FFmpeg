/*
 *  libzvbi - Closed Caption and Teletext rendering
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
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

/* $Id: exp-gfx.h,v 1.6 2008/02/19 00:35:15 mschimek Exp $ */

#ifndef EXP_GFX_H
#define EXP_GFX_H

#include "format.h"
#include "decoder.h" /* vbi_pixfmt */

/* Public */

/**
 * @addtogroup Render
 * @{
 */
extern void		vbi_draw_vt_page_region(vbi_page *pg, vbi_pixfmt fmt,
						void *canvas, int rowstride,
						int column, int row,
						int width, int height,
						int reveal, int flash_on);
/**
 * @param pg Source page.
 * @param fmt Target format. For now only VBI_PIXFMT_RGBA32_LE (vbi_rgba) permitted.
 * @param canvas Pointer to destination image (currently an array of vbi_rgba). This
 *   must be at least pg->columns * pg->rows * 12 * 10 * pixels large,
 *   without padding between lines.
 * @param reveal If FALSE, draw characters flagged 'concealed' (see vbi_char) as
 *   space (U+0020).
 * @param flash_on If FALSE, draw characters flagged 'blink' (see vbi_char) as
 *   space (U+0020).
 * 
 * Draw a Teletext vbi_page. In this mode one character occupies 12 x 10 pixels.
 */
_vbi_inline void
vbi_draw_vt_page(vbi_page *pg, vbi_pixfmt fmt, void *canvas,
		 int reveal, int flash_on)
{
	vbi_draw_vt_page_region(pg, fmt, canvas, -1, 0, 0,
				pg->columns, pg->rows, reveal, flash_on);
}

extern void		vbi_draw_cc_page_region(vbi_page *pg, vbi_pixfmt fmt,
						void *canvas, int rowstride,
						int column, int row,
						int width, int height);

/**
 * @param pg Source page.
 * @param fmt Target format. For now only VBI_PIXFMT_RGBA32_LE (vbi_rgba) permitted.
 * @param canvas Pointer to destination image (currently an array of vbi_rgba). This
 *   must be at least pg->columns * pg->rows * 16 * 26 * pixels large, without
 *   padding between lines.
 *
 * Draw a Closed Caption vbi_page. In this mode one character occupies
 * 16 x 26 pixels.
 */
_vbi_inline void
vbi_draw_cc_page(vbi_page *pg, vbi_pixfmt fmt, void *canvas)
{
	vbi_draw_cc_page_region(pg, fmt, canvas, -1, 0, 0, pg->columns, pg->rows);
}

extern void vbi_get_max_rendered_size(int *w, int *h);
extern void vbi_get_vt_cell_size(int *w, int *h);
/** @} */

/* Private */

#endif /* EXP_GFX_H */









/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
