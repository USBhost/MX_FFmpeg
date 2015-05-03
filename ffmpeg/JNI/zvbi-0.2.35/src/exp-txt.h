/*
 *  libzvbi - Text export functions
 *
 *  Copyright (C) 2001 Michael H. Schimek
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

/* $Id: exp-txt.h,v 1.9 2008/02/19 00:35:15 mschimek Exp $ */

#ifndef EXP_TXT_H
#define EXP_TXT_H

#include "format.h"

/* Public */

/**
 * @addtogroup Render
 * @{
 */
extern int		vbi_print_page_region(vbi_page *pg, char *buf, int size,
					      const char *format, vbi_bool table, vbi_bool ltr,
					      int column, int row, int width, int height);

/**
 * @param pg Source page.
 * @param buf Memory location to hold the output.
 * @param size Size of the buffer in bytes. The function fails
 *   when the data exceeds the buffer capacity.
 * @param format Character set name for iconv() conversion,
 *   for example "ISO-8859-1".
 * @param table When @c FALSE, runs of spaces at the start and
 *   end of rows will be collapsed into single spaces, blank
 *   lines are suppressed.
 * @param ltr Currently ignored, please set to @c TRUE.
 * 
 * Print a Teletext or Closed Caption vbi_page, rows separated
 * by linefeeds "\n", in the desired format. All character attributes
 * and colors will be lost. Graphics characters, DRCS and all
 * characters not representable in the target format will be replaced
 * by spaces.
 * 
 * @return
 * Number of bytes written into @a buf, a value of zero when
 * some error occurred. In this case @a buf may contain incomplete
 * data. Note this function does not append a terminating null
 * character.
 **/
_vbi_inline int
vbi_print_page(vbi_page *pg, char *buf, int size,
	       const char *format, vbi_bool table, vbi_bool ltr)
{
	return vbi_print_page_region(pg, buf, size,
				     format, table, ltr,
				     0, 0, pg->columns, pg->rows);
}
/** @} */

/* Private */

#endif /* EXP_TXT_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
