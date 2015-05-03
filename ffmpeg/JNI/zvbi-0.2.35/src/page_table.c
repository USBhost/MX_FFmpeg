/*
 *  libzvbi -- Table of Teletext page numbers
 *
 *  Copyright (C) 2006, 2007 Michael H. Schimek
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

/* $Id: page_table.c,v 1.5 2008/02/19 00:35:20 mschimek Exp $ */

/* Note this module is not an offical part of the library yet because
   it needs more testing and the interface may change. Use at your own
   risk. */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <limits.h>
#include <errno.h>

#include "misc.h"
#include "page_table.h"

/**
 * addtogroup PageTable Teletext Page Number Table
 * ingroup LowDec
 * brief A set of Teletext page numbers.
 *
 * Sometimes application want to operate on multiple Teletext
 * pages or subpages. The vbi_page_table structure can simply and
 * efficiently remember the page numbers for you. It is used for
 * example by the vbi_sliced_filter to remember the Teletext pages
 * the caller wishes to keep or drop.
 *
 * The vbi_page_table is optimized for fast queries, while adding or
 * removing pages and especially subpages may take longer.
 */

/* 0 ... 0x3F7E; 0x3F7F == VBI_ANY_SUBNO. */
#define MAX_SUBNO 0x3F7E

/* XXX Later. */
enum {
	VBI_ERR_INVALID_PGNO = 0,
	VBI_ERR_INVALID_SUBNO = 0,
};

struct subpage_range {
	/* 0x100 ... 0x8FF. */
	vbi_pgno		pgno;

	/* 0x0000 ... MAX_SUBNO. */
	vbi_subno		first;

	/* 0x0000 ... MAX_SUBNO, last >= first. */
	vbi_subno		last;
};

struct _vbi_page_table {
	/* One bit for each Teletext page with subpage range
	   0 ... MAX_SUBNO. These are not in the subpages vector.
	   vbi_pgno 0x100 -> pages[0] & 1. */
	uint32_t		pages[(0x900 - 0x100) / 32];

	/* Number of set bits in the pages[] array. */
	unsigned int		pages_popcnt;

	/* A vector of subpages, current size and capacity
	   (counting struct subpage_range). */
	struct subpage_range *	subpages;
	unsigned int		subpages_size;
	unsigned int		subpages_capacity;
};

static __inline__ vbi_bool
valid_pgno			(vbi_pgno		pgno)
{
	return ((unsigned int) pgno - 0x100 < 0x800);
}

static vbi_bool
contains_all_subpages		(const vbi_page_table *pt,
				 vbi_pgno		pgno)
{
	uint32_t mask;
	unsigned int offset;

	mask = 1 << (pgno & 31);
	offset = (pgno - 0x100) >> 5;

	return (0 != (pt->pages[offset] & mask));
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 * @param pgno The page number in question. Need not be a valid
 *   Teletext page number.
 *
 * The function returns @c TRUE if the page @a pgno and all its
 * subpages have been added to the page table. 
 */
vbi_bool
vbi_page_table_contains_all_subpages
				(const vbi_page_table *pt,
				 vbi_pgno		pgno)
{
	assert (NULL != pt);

	if (unlikely (!valid_pgno (pgno)))
		return FALSE;

	return contains_all_subpages (pt, pgno);
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 * @param pgno The page number in question. Need not be a valid
 *   Teletext page number.
 * @param subno The subpage number in question. Need not be a valid
 *   Teletext subpage number. Can be @c VBI_ANY_SUBNO.
 *
 * The function returns @c TRUE if subpage @a subno of page @a pgno
 * has been added to the page table.
 */
vbi_bool
vbi_page_table_contains_subpage	(const vbi_page_table *pt,
				 vbi_pgno		pgno,
				 vbi_subno		subno)
{
	unsigned int i;

	assert (NULL != pt);

	if (unlikely (!valid_pgno (pgno)))
		return FALSE;

	if (contains_all_subpages (pt, pgno))
		return TRUE;

	if (VBI_ANY_SUBNO == subno) {
		for (i = 0; i < pt->subpages_size; ++i) {
			if (pgno == pt->subpages[i].pgno)
				return TRUE;
		}
	} else {
		for (i = 0; i < pt->subpages_size; ++i) {
			if (pgno == pt->subpages[i].pgno
			    && subno >= pt->subpages[i].first
			    && subno <= pt->subpages[i].last)
				return TRUE;
		}
	}

	return FALSE;
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 * @param pgno Pointer to a page number. The function stores here the
 *   next higher page number which has been added to the page table.
 * @param subno Pointer to a subpage number. The function stores here the
 *   next higher subpage number which has been added to the page table.
 *
 * This function can be used to iterate over the page and subpage
 * numbers which have been added to the page table.
 *
 * When @a *pgno is less than 0x100 it will return the lowest
 * page and subpage number in the page table.
 *
 * Otherwise it will return the next subpage of this page, or if no
 * higher subpages of this page have been added the first subpage of
 * the next higher page number in the table. A @a *subno value of
 * @c VBI_ANY_SUBNO stands for the highest subpage number in the
 * table, so the function will also return the first subpage of the
 * next higher page.
 *
 * When all subpages of the returned @a *pgno are in the table, the
 * returned @a *subno will be @c VBI_ANY_SUBNO. (This is the common
 * case and there is no point in iterating through all the subpages.)
 *
 * When no page numbers higher than @a *pgno are in the table, or
 * when this is the highest page and @a *subno is VBI_ANY_SUBNO or
 * there are no subpage numbers higher than @a *subno, the function
 * returns @c FALSE.
 *
 * @code
 * vbi_pgno pgno = 0;
 * vbi_subno subno;
 *
 * // Iterate over the subpages of all pages.
 * while (!vbi_page_table_next_subpage (pt, &pgno, &subno) {
 *     // Do things on page pgno, subno.
 *     // subno is in range 0 to 0x3F7E inclusive, or VBI_ANY_SUBNO. 
 * }
 * @endcode
 */
vbi_bool
vbi_page_table_next_subpage	(const vbi_page_table *pt,
				 vbi_pgno *		pgno,
				 vbi_subno *		subno)
{
	vbi_pgno last_pgno;
	vbi_pgno last_subno;
	vbi_pgno next_pgno;
	vbi_pgno next_subno;
	vbi_pgno min_pgno;
	vbi_subno min_subno;
	uint32_t mask;
	unsigned int offset;
	unsigned int i;

	assert (NULL != pt);
	assert (NULL != pgno);
	assert (NULL != subno);

	last_pgno = *pgno;
	last_subno = *subno;

	if (last_pgno >= 0x8FF) {
		return FALSE;
	} else if (last_pgno < 0x100) {
		next_pgno = 0x100;
	} else {
		if (last_subno <= MAX_SUBNO /* not ANY */) {
			next_subno = last_subno + 1;
			min_subno = MAX_SUBNO + 1;

			for (i = 0; i < pt->subpages_size; ++i) {
				if (last_pgno != pt->subpages[i].pgno)
					continue;

				if (next_subno > pt->subpages[i].last)
					continue;

				if (next_subno >= pt->subpages[i].first) {
					*subno = next_subno;
					return TRUE;
				}

				if (pt->subpages[i].first < min_subno)
					min_subno = pt->subpages[i].first;
			}

			if (min_subno <= MAX_SUBNO) {
				*subno = min_subno;
				return TRUE;
			}
		}

		next_pgno = last_pgno + 1;
	}

	min_pgno = 0x900;

	for (i = 0; i < pt->subpages_size; ++i) {
		if (next_pgno <= pt->subpages[i].pgno
		    && next_pgno < min_pgno) {
			min_pgno = pt->subpages[i].pgno;
			min_subno = pt->subpages[i].first;
		}
	}

	mask = -1 << (next_pgno & 31);
	offset = (next_pgno - 0x100) >> 5;
	mask &= pt->pages[offset];

	next_pgno &= ~31;

	for (;;) {
		if (0 != mask)
			break;

		next_pgno += 32;
		if (next_pgno >= 0x900)
			return FALSE;

		mask = pt->pages[++offset];
	}

	next_pgno += ffs (mask) - 1;

	if (min_pgno < next_pgno) {
		*pgno = min_pgno;
		*subno = min_subno;
	} else {
		*pgno = next_pgno;
		*subno = VBI_ANY_SUBNO;
	}

	return TRUE;
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 * @param pgno Pointer to a page number. The function stores here the
 *   next higher page number which has been added to the page table.
 *
 * This function can be used to iterate over the page numbers
 * which have been added to the page table. If multiple subpages
 * of a page have been added, the function returns this page
 * number only once.
 *
 * When @a *pgno is less than 0x100 it returns the lowest page number
 * in the page table, otherwise the next higher page number which has
 * been added. When there are no higher page numbers it returns @c FALSE.
 *
 * @code
 * vbi_pgno pgno = 0;
 *
 * // Iterate over all pages.
 * while (!vbi_page_table_next_page (pt, &pgno) {
 *     // Do things on page pgno.
 * }
 * @endcode
 */
vbi_bool
vbi_page_table_next_page	(const vbi_page_table *pt,
				 vbi_pgno *		pgno)
{
	vbi_subno subno = VBI_ANY_SUBNO;

	return vbi_page_table_next_subpage (pt, pgno, &subno);
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 *
 * This function returns the number of pages which have been added
 * to the page table. Multiple subpages of a page count as one page.
 *
 * This is a fast function. It just returns the value of a counter
 * maintained by the add and remove functions.
 */
unsigned int
vbi_page_table_num_pages	(const vbi_page_table *pt)
{
	assert (NULL != pt);

	return pt->pages_popcnt + pt->subpages_size;
}

static void
shrink_vector			(void **		vector,
				 unsigned int *		capacity,
				 unsigned int		min_capacity,
				 unsigned int		element_size)
{
	void *new_vec;
	unsigned int new_capacity;

	if (min_capacity >= *capacity)
		return;

	new_capacity = min_capacity;

	new_vec = vbi_realloc (*vector, new_capacity * element_size);
	if (unlikely (NULL == new_vec))
		return;

	*vector = new_vec;
	*capacity = new_capacity;
}

static vbi_bool
extend_vector			(void **		vector,
				 unsigned int *		capacity,
				 unsigned int		min_capacity,
				 unsigned int		element_size)
{
	void *new_vec;
	unsigned int new_capacity;
	unsigned int max_capacity;

	assert (min_capacity > 0);
	assert (element_size > 0);

	/* This looks a bit odd to prevent overflows. */

	max_capacity = UINT_MAX / element_size;

	if (unlikely (min_capacity > max_capacity)) {
		errno = ENOMEM;
		return FALSE;
	}

	new_capacity = *capacity;

	if (unlikely (new_capacity > (max_capacity / 2))) {
		new_capacity = max_capacity;
	} else {
		new_capacity = MIN (min_capacity, new_capacity * 2);
	}

	new_vec = vbi_realloc (*vector, new_capacity * element_size);
	if (unlikely (NULL == new_vec)) {
		/* XXX we should try less new_capacity before giving up. */
		errno = ENOMEM;
		return FALSE;
	}

	*vector = new_vec;
	*capacity = new_capacity;

	return TRUE;
}

static void
shrink_subpages_vector		(vbi_page_table *	pt)
{
	if (pt->subpages_size >= pt->subpages_capacity / 4)
		return;

	shrink_vector ((void **) &pt->subpages,
		       &pt->subpages_capacity,
		       pt->subpages_capacity / 2,
		       sizeof (*pt->subpages));
}

static vbi_bool
extend_subpages_vector		(vbi_page_table *	pt,
				 unsigned int		min_capacity)
{
	if (min_capacity <= pt->subpages_capacity)
		return TRUE;

	return extend_vector ((void **) &pt->subpages,
			      &pt->subpages_capacity,
			      min_capacity,
			      sizeof (*pt->subpages));
}

static vbi_bool
valid_subpage_range		(vbi_pgno		pgno,
				 vbi_subno		first_subno,
				 vbi_subno		last_subno)
{
	if (unlikely (!valid_pgno (pgno))) {
		errno = VBI_ERR_INVALID_PGNO;
		return FALSE;
	}

	if (unlikely ((unsigned int) first_subno > MAX_SUBNO
		      || (unsigned int) last_subno > MAX_SUBNO)) {
		errno = VBI_ERR_INVALID_SUBNO;
		return FALSE;
	}

	return TRUE;
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 * @param pgno The page in question. Must be in range 0x100 to 0x8FF
 *   inclusive.
 * @param first_subno First subpage number to remove.
 * @param last_subno Last subpage number to remove. Both @a first_subno
 *   and @a last_subno must be in range 0 to 0x3F7E inclusive, or
 *   both must be @c VBI_ANY_SUBNO.
 *
 * This function removes the Teletext subpages of page @a pgno from
 * @a first_subno to @a last_subno inclusive. When @a first_subno
 * and @a last_subno is @c VBI_ANY_SUBNO, it removes the page and
 * all its subpages as vbi_page_table_remove_page() does.
 *
 * @a returns
 * @c FALSE on failure (invalid page or subpage numbers or out of memory).
 */
vbi_bool
vbi_page_table_remove_subpages	(vbi_page_table *	pt,
				 vbi_pgno		pgno,
				 vbi_subno		first_subno,
				 vbi_subno		last_subno)
{
	uint32_t mask;
	unsigned int offset;
	unsigned int i;

	assert (NULL != pt);

	if (VBI_ANY_SUBNO == first_subno
	    && VBI_ANY_SUBNO == last_subno)
		return vbi_page_table_remove_pages (pt, pgno, pgno);

	if (unlikely (!valid_subpage_range (pgno, first_subno, last_subno)))
		return FALSE;

	if (first_subno > last_subno)
		SWAP (first_subno, last_subno);

	mask = 1 << (pgno & 31);
	offset = (pgno - 0x100) >> 5;

	if (0 != (pt->pages[offset] & mask)) {
		i = pt->subpages_size;

		if (!extend_subpages_vector (pt, i + 2))
			return FALSE;

		--pt->pages_popcnt;
		pt->pages[offset] &= ~mask;

		if (first_subno > 0) {
			pt->subpages[i].pgno = pgno;
			pt->subpages[i].first = 0;
			pt->subpages[i++].last = first_subno - 1;
		}

		if (last_subno < MAX_SUBNO) {
			pt->subpages[i].pgno = pgno;
			pt->subpages[i].first = last_subno + 1;
			pt->subpages[i++].last = MAX_SUBNO;
		}

		pt->subpages_size = i;

		return TRUE;
	}

	for (i = 0; i < pt->subpages_size; ++i) {
		if (pgno != pt->subpages[i].pgno)
			continue;

		if (first_subno > pt->subpages[i].last)
			continue;

		if (last_subno < pt->subpages[i].first)
			continue;

		if (first_subno > pt->subpages[i].first
		    && last_subno < pt->subpages[i].last) {
			if (!extend_subpages_vector (pt,
						     pt->subpages_size + 1))
				return FALSE;

			memmove (&pt->subpages[i + 1],
				 &pt->subpages[i],
				 (pt->subpages_size - i)
				 * sizeof (*pt->subpages));

			pt->subpages[i].last = first_subno;
			pt->subpages[i + 1].first = last_subno + 1;

			++pt->subpages_size;
			++i;

			continue;
		}

		if (first_subno > pt->subpages[i].first)
			pt->subpages[i].first = first_subno;

		if (last_subno < pt->subpages[i].last)
			pt->subpages[i].last = last_subno;

		if (pt->subpages[i].first > pt->subpages[i].last) {
			memmove (&pt->subpages[i],
				 &pt->subpages[i + 1],
				 (pt->subpages_size - i)
				 * sizeof (*pt->subpages));

			--pt->subpages_size;
			--i;
		}
	}

	shrink_subpages_vector (pt);

	return TRUE;
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 * @param pgno The page in question. Must be in range 0x100 to 0x8FF
 *   inclusive.
 * @param first_subno First subpage number to add.
 * @param last_subno Last subpage number to add. Both @a first_subno
 *   and @a last_subno must be in range 0 to 0x3F7E inclusive, or
 *   both must be @c VBI_ANY_SUBNO.
 *
 * This function adds the Teletext subpages of page @a pgno from
 * from @a first_subno to @a last_subno inclusive. When @a first_subno
 * and @a last_subno is @c VBI_ANY_SUBNO, it adds all subpages as
 * vbi_page_table_add_page() does.
 *
 * @a returns
 * @c FALSE on failure (invalid page or subpage numbers or out of memory).
 */
vbi_bool
vbi_page_table_add_subpages	(vbi_page_table *	pt,
				 vbi_pgno		pgno,
				 vbi_subno		first_subno,
				 vbi_subno		last_subno)
{
	unsigned int i;

	assert (NULL != pt);

	if (VBI_ANY_SUBNO == first_subno
	    && VBI_ANY_SUBNO == last_subno)
		return vbi_page_table_add_pages (pt, pgno, pgno);

	if (unlikely (!valid_subpage_range (pgno, first_subno, last_subno)))
		return FALSE;

	if (vbi_page_table_contains_page (pt, pgno))
		return TRUE;

	if (first_subno > last_subno)
		SWAP (first_subno, last_subno);

	for (i = 0; i < pt->subpages_size; ++i) {
		if (pgno == pt->subpages[i].pgno
		    && last_subno >= pt->subpages[i].first
		    && first_subno <= pt->subpages[i].last) {
			if (first_subno < pt->subpages[i].first)
				pt->subpages[i].first = first_subno;

			if (last_subno > pt->subpages[i].last)
				pt->subpages[i].last = last_subno;

			return TRUE;
		}
	}

	if (!extend_subpages_vector (pt, i + 1))
		return FALSE;

	pt->subpages[i].pgno = pgno;
	pt->subpages[i].first = first_subno;
	pt->subpages[i].last = last_subno;

	pt->subpages_size = i + 1;

	return TRUE;
}

static void
remove_subpages_in_page_range	(vbi_page_table *	pt,
				 vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
{
	unsigned int i;
	unsigned int j;

	for (i = 0, j = 0; i < pt->subpages_size; ++i) {
		if (pt->subpages[i].pgno < first_pgno
		    || pt->subpages[i].pgno > last_pgno) {
			if (j < i) {
				memcpy (&pt->subpages[j],
					&pt->subpages[i],
					sizeof (*pt->subpages));
			}

			++j;
		}
	}

	pt->subpages_size = j;

	shrink_subpages_vector (pt);
}

static vbi_bool
valid_pgno_range		(vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
{
	if (likely (valid_pgno (first_pgno)
		    && valid_pgno (last_pgno)))
		return TRUE;

	errno = VBI_ERR_INVALID_PGNO;

	return FALSE;
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 * @param first_pgno First page number to remove.
 * @param last_pgno Last page number to remove. Both @a first_pgno and
 *   @a last_pgno must be in range 0x100 to 0x8FF inclusive.
 *
 * This function removes all Teletext pages from @a first_pgno to
 * @a last_pgno inclusive, also non-displayable system pages with
 * hex digits in the page number, and all their subpages from the
 * page table.
 *
 * @a returns
 * @c FALSE on failure (invalid page numbers or out of memory).
 */
vbi_bool
vbi_page_table_remove_pages	(vbi_page_table *	pt,
				 vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
{
	uint32_t first_mask;
	uint32_t last_mask;
	uint32_t old_mask;
	unsigned int first_offset;
	unsigned int last_offset;

	assert (NULL != pt);

	if (unlikely (!valid_pgno_range (first_pgno, last_pgno)))
		return FALSE;

	if (first_pgno > last_pgno)
		SWAP (first_pgno, last_pgno);

	if (0x8FF == last_pgno && 0x100 == first_pgno) {
		pt->subpages_size = 0;

		shrink_subpages_vector (pt);

		memset (pt->pages, 0, sizeof (pt->pages));
		pt->pages_popcnt = 0;

		return TRUE;
	}

	remove_subpages_in_page_range (pt, first_pgno, last_pgno);

	/* 0 -> 0xFFFF FFFF, 1 -> 0xFFFF FFFE, 31 -> 0x8000 0000. */
	first_mask = -1 << (first_pgno & 31);
	first_offset = (first_pgno - 0x100) >> 5;

	/* 0 -> 0x01, 1 -> 0x03, 31 -> 0xFFFF FFFF. */
	last_mask = ~(-2 << (last_pgno & 31));
	last_offset = (last_pgno - 0x100) >> 5;

	if (first_offset != last_offset) {
		old_mask = pt->pages[first_offset];
		pt->pages_popcnt -= popcnt (old_mask & first_mask);
		pt->pages[first_offset] = old_mask & ~first_mask;
		first_mask = -1;

		while (++first_offset < last_offset) {
			old_mask = pt->pages[first_offset];
			pt->pages_popcnt -= popcnt (old_mask);
			pt->pages[first_offset] = 0;
		}
	}

	old_mask = pt->pages[last_offset];
	last_mask &= first_mask;
	pt->pages_popcnt -= popcnt (old_mask & last_mask);
	pt->pages[last_offset] = old_mask & ~last_mask;

	return TRUE;
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 * @param first_pgno First page number to add.
 * @param last_pgno Last page number to add. Both @a first_pgno and
 *   @a last_pgno must be in range 0x100 to 0x8FF inclusive.
 *
 * This function adds all Teletext pages from @a first_pgno to
 * @a last_pgno inclusive, also non-displayable system pages with
 * hex digits in the page number, and all their subpages to the page
 * table.
 *
 * @a returns
 * @c FALSE on failure (invalid page numbers or out of memory).
 */
vbi_bool
vbi_page_table_add_pages	(vbi_page_table *	pt,
				 vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
{
	uint32_t first_mask;
	uint32_t last_mask;
	uint32_t old_mask;
	unsigned int first_offset;
	unsigned int last_offset;

	assert (NULL != pt);

	if (unlikely (!valid_pgno_range (first_pgno, last_pgno)))
		return FALSE;

	if (first_pgno > last_pgno)
		SWAP (first_pgno, last_pgno);

	if (0x8FF == last_pgno && 0x100 == first_pgno) {
		pt->subpages_size = 0;

		shrink_subpages_vector (pt);

		memset (pt->pages, -1, sizeof (pt->pages));
		pt->pages_popcnt = 0x800;

		return TRUE;
	}

	/* Remove duplicates of pages[] in subpages. */
	remove_subpages_in_page_range (pt, first_pgno, last_pgno);

	/* 0 -> 0xFFFF FFFF, 1 -> 0xFFFF FFFE, 31 -> 0x8000 0000. */
	first_mask = -1 << (first_pgno & 31);
	first_offset = (first_pgno - 0x100) >> 5;

	/* 0 -> 0x01, 1 -> 0x03, 31 -> 0xFFFF FFFF. */
	last_mask = ~(-2 << (last_pgno & 31));
	last_offset = (last_pgno - 0x100) >> 5;

	if (first_offset != last_offset) {
		old_mask = pt->pages[first_offset];
		pt->pages_popcnt += popcnt (first_mask & ~old_mask);
		pt->pages[first_offset] = first_mask | old_mask;
		first_mask = -1;

		while (++first_offset < last_offset) {
			old_mask = pt->pages[first_offset];
			pt->pages_popcnt += 32 - popcnt (old_mask);
			pt->pages[first_offset] = -1;
		}
	}

	old_mask = pt->pages[last_offset];
	last_mask &= first_mask;
	pt->pages_popcnt += popcnt (last_mask & ~old_mask);
	pt->pages[last_offset] = last_mask | old_mask;

	return TRUE;
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 *
 * This function removes all Teletext pages from 0x100 to 0x8FF
 * inclusive and all their subpages from the page table.
 */
void
vbi_page_table_remove_all_pages	(vbi_page_table *	pt)
{
	vbi_page_table_remove_pages (pt, 0x100, 0x8FF);
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 *
 * This function adds all displayable Teletext pages from 0x100 to
 * 0x899 (i.e. all page numbers which are valid BCD numbers) and all
 * their subpages to the page table.
 */
void
vbi_page_table_add_all_displayable_pages
				(vbi_page_table *	pt)
{
	vbi_pgno pgno;

	assert (NULL != pt);

	for (pgno = 0x100; pgno < 0x900; pgno += 0x100 - 0xA0) {
		vbi_pgno end_pgno = pgno + 0xA0; 

		do {
			uint32_t mask;
			uint32_t old_mask;
			unsigned int offset;

			remove_subpages_in_page_range (pt,
						       pgno + 0x00,
						       pgno + 0x09);

			remove_subpages_in_page_range (pt,
						       pgno + 0x10,
						       pgno + 0x19);

			mask = 0x03FF03FF;
			offset = (pgno - 0x100) >> 5;

			old_mask = pt->pages[offset];
			pt->pages_popcnt += popcnt (mask & ~old_mask);
			pt->pages[offset] = mask | old_mask;

			pgno += 0x20;
		} while (pgno < end_pgno);
	}
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new().
 *
 * This function adds all Teletext pages from 0x100 to 0x8FF inclusive
 * and all their subpages to the page table.
 */
void
vbi_page_table_add_all_pages	(vbi_page_table *	pt)
{
	vbi_page_table_add_pages (pt, 0x100, 0x8FF);
}

/**
 * @param pt Teletext page table allocated with vbi_page_table_new(),
 *   can be @c NULL.
 *
 * Frees all resources associated with @a pt.
 */
void
vbi_page_table_delete		(vbi_page_table *	pt)
{
	if (NULL == pt)
		return;

	vbi_free (pt->subpages);

	CLEAR (*pt);

	vbi_free (pt);		
}

/**
 * Allocates a new Teletext page number table. Initially no page numbers
 * are in the table.
 *
 * @returns
 * @c NULL on failure (out of memory).
 */
vbi_page_table *
vbi_page_table_new		(void)
{
	vbi_page_table *pt;

	pt = vbi_malloc (sizeof (*pt));
	if (NULL == pt) {
		return NULL;
	}

	CLEAR (*pt);

	return pt;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
