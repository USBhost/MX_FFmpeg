/*
 *  libzvbi -- Teletext page cache search functions
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
 *  Copyright (C) 2000, 2001 Iñaki G. Etxebarria
 *
 *  Originally based on AleVT 1.5.1 by Edgar Toernig
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

/* $Id: search.h,v 1.5 2008/02/19 00:35:22 mschimek Exp $ */

#ifndef SEARCH_H
#define SEARCH_H

#ifndef VBI_DECODER
#define VBI_DECODER
typedef struct vbi_decoder vbi_decoder;
#endif

/* Public */

/**
 * @ingroup Search
 * @brief Return codes of the vbi_search_next() function.
 */
typedef enum {
	/**
	 * Pattern not found, @a pg is invalid. Another vbi_search_next()
	 * will restart from the original starting point.
	 */
	VBI_SEARCH_ERROR = -3,
	/**
	 * The search has been canceled by the progress function.
	 * @a pg points to the current page as in success case,
	 * except for the highlighting. Another vbi_search_next()
	 * continues from this page.
	 */
	VBI_SEARCH_CACHE_EMPTY,
	/**
	 * No pages in the cache, @a pg is invalid.
	 */
	VBI_SEARCH_CANCELED,
	/**
	 * Some error occured, condition unclear. Call vbi_search_delete().
	 */
	VBI_SEARCH_NOT_FOUND = 0,
	/**
	 * Pattern found. @a pg points to the page ready for display with the pattern
	 *   highlighted, @a pg->pgno etc.
	 */
	VBI_SEARCH_SUCCESS
} vbi_search_status;

/**
 * @ingroup Search
 * @brief Opaque search context. 
 */
typedef struct vbi_search vbi_search;

/**
 * @addtogroup Search
 * @{
 */
extern vbi_search *	vbi_search_new(vbi_decoder *vbi,
				       vbi_pgno pgno, vbi_subno subno,
				       uint16_t *pattern,
				       vbi_bool casefold, vbi_bool regexp,
				       int (* progress)(vbi_page *pg));
extern void		vbi_search_delete(vbi_search *search);
extern vbi_search_status vbi_search_next(vbi_search *search, vbi_page **pg, int dir);
/** @} */

/* Private */

#endif /* SEARCH_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
