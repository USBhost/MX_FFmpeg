/*
 *  libzvbi -- Sliced VBI data filter
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

/* $Id: sliced_filter.c,v 1.6 2008/02/19 00:35:22 mschimek Exp $ */

/* XXX UNTESTED */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <limits.h>
#include <errno.h>

#include "misc.h"
#include "version.h"
#include "hamm.h"		/* vbi_unham16p() */
#include "event.h"		/* VBI_SERIAL */
#include "sliced_filter.h"
#include "page_table.h"

#ifndef VBI_SERIAL
#  define VBI_SERIAL 0x100000
#endif

/* XXX Later. */
enum {
	VBI_ERR_INVALID_PGNO = 0,
	VBI_ERR_INVALID_SUBNO = 0,
	VBI_ERR_BUFFER_OVERFLOW = 0,
	VBI_ERR_PARITY = 0,
};

/* XXX Later. */
#undef _
#define _(x) (x)

/* 0 ... (VBI_ANY_SUBNO = 0x3F7F) - 1. */
#define MAX_SUBNO 0x3F7E

struct _vbi_sliced_filter {
	vbi_page_table *	keep_ttx_pages;

	vbi_bool		keep_ttx_system_pages;

	vbi_sliced *		output_buffer;
	unsigned int		output_max_lines;

	unsigned int		keep_mag_set_next;
	vbi_bool		start;

	vbi_service_set	keep_services;

	char *			errstr;

	_vbi_log_hook		log;

	vbi_sliced_filter_cb *	callback;
	void *			user_data;
};

static void
set_errstr			(vbi_sliced_filter *	sf,
				 const char *		templ,
				 ...)
{
	va_list ap;

	vbi_free (sf->errstr);
	sf->errstr = NULL;

	va_start (ap, templ);

	/* Error ignored. */
	vasprintf (&sf->errstr, templ, ap);

	va_end (ap);
}

static void
no_mem_error			(vbi_sliced_filter *	sf)
{
	vbi_free (sf->errstr);

	/* Error ignored. */
	sf->errstr = strdup (_("Out of memory."));

	errno = ENOMEM;
}

#if 0 /* to do */

vbi_bool
vbi_sliced_filter_drop_cc_channel
				(vbi_sliced_filter *	sf,
				 vbi_pgno		channel)
{
}

vbi_bool
vbi_sliced_filter_keep_cc_channel
				(vbi_sliced_filter *	sf,
				 vbi_pgno		channel)
{
}

#endif

void
vbi_sliced_filter_keep_ttx_system_pages
				(vbi_sliced_filter *	sf,
				 vbi_bool		keep)
{
	assert (NULL != sf);

	sf->keep_ttx_system_pages = !!keep;
}

static __inline__ vbi_bool
valid_ttx_page			(vbi_pgno		pgno)
{
	return ((unsigned int) pgno - 0x100 < 0x800);
}

static vbi_bool
valid_ttx_subpage_range		(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno,
				 vbi_subno		first_subno,
				 vbi_subno		last_subno)
{
	if (unlikely (!valid_ttx_page (pgno))) {
		set_errstr (sf, _("Invalid Teletext page number %x."),
			    pgno);
		errno = VBI_ERR_INVALID_PGNO;
		return FALSE;
	}

	if (likely ((unsigned int) first_subno <= MAX_SUBNO
		    && (unsigned int) last_subno <= MAX_SUBNO))
		return TRUE;

	if (first_subno == last_subno) {
		set_errstr (sf, _("Invalid Teletext subpage number %x."),
			    first_subno);
	} else {
		set_errstr (sf, _("Invalid Teletext subpage range %x-%x."),
			    first_subno, last_subno);
	}

	errno = VBI_ERR_INVALID_SUBNO;

	return FALSE;
}

vbi_bool
vbi_sliced_filter_drop_ttx_subpages
				(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno,
				 vbi_subno		first_subno,
				 vbi_subno		last_subno)
{
	assert (NULL != sf);

	if (VBI_ANY_SUBNO == first_subno
	    && VBI_ANY_SUBNO == last_subno)
		return vbi_sliced_filter_drop_ttx_pages (sf, pgno, pgno);

	if (unlikely (!valid_ttx_subpage_range (sf, pgno,
						first_subno,
						last_subno)))
		return FALSE;

	if (sf->keep_services & VBI_SLICED_TELETEXT_B_625) {
		vbi_page_table_add_all_pages (sf->keep_ttx_pages);
		sf->keep_services &= ~VBI_SLICED_TELETEXT_B_625;
	}

	return vbi_page_table_remove_subpages (sf->keep_ttx_pages,
						pgno,
						first_subno,
						last_subno);
}

vbi_bool
vbi_sliced_filter_keep_ttx_subpages
				(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno,
				 vbi_subno		first_subno,
				 vbi_subno		last_subno)
{
	assert (NULL != sf);

	if (VBI_ANY_SUBNO == first_subno
	    && VBI_ANY_SUBNO == last_subno)
		return vbi_sliced_filter_keep_ttx_pages (sf, pgno, pgno);

	if (unlikely (!valid_ttx_subpage_range (sf, pgno,
						first_subno,
						last_subno)))
		return FALSE;

	if (sf->keep_services & VBI_SLICED_TELETEXT_B_625)
		return TRUE;

	return vbi_page_table_add_subpages (sf->keep_ttx_pages,
					     pgno, first_subno, last_subno);
}

static vbi_bool
valid_ttx_page_range		(vbi_sliced_filter *	sf,
				 vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
{
	if (likely (valid_ttx_page (first_pgno)
		    && valid_ttx_page (last_pgno)))
		return TRUE;

	if (first_pgno == last_pgno) {
		set_errstr (sf, _("Invalid Teletext page number %x."),
			    first_pgno);
	} else {
		set_errstr (sf, _("Invalid Teletext page range %x-%x."),
			    first_pgno, last_pgno);
	}

	errno = VBI_ERR_INVALID_PGNO;

	return FALSE;
}

vbi_bool
vbi_sliced_filter_drop_ttx_pages
				(vbi_sliced_filter *	sf,
				 vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
{
	assert (NULL != sf);

	if (unlikely (!valid_ttx_page_range (sf, first_pgno, last_pgno)))
		return FALSE;

	if (sf->keep_services & VBI_SLICED_TELETEXT_B_625) {
		vbi_page_table_add_all_pages (sf->keep_ttx_pages);
		sf->keep_services &= ~VBI_SLICED_TELETEXT_B_625;
	}

	return vbi_page_table_remove_pages (sf->keep_ttx_pages,
					     first_pgno, last_pgno);
}

vbi_bool
vbi_sliced_filter_keep_ttx_pages
				(vbi_sliced_filter *	sf,
				 vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
{
	assert (NULL != sf);

	if (unlikely (!valid_ttx_page_range (sf, first_pgno, last_pgno)))
		return FALSE;

	if (sf->keep_services & VBI_SLICED_TELETEXT_B_625)
		return TRUE;

	return vbi_page_table_add_pages (sf->keep_ttx_pages,
					  first_pgno, last_pgno);
}

vbi_service_set
vbi_sliced_filter_drop_services
				(vbi_sliced_filter *	sf,
				 vbi_service_set	services)
{
	assert (NULL != sf);

	if (services & VBI_SLICED_TELETEXT_B_625)
		vbi_page_table_remove_all_pages (sf->keep_ttx_pages);

	return sf->keep_services &= ~services;
}

vbi_service_set
vbi_sliced_filter_keep_services
				(vbi_sliced_filter *	sf,
				 vbi_service_set	services)
{
	assert (NULL != sf);

	if (services & VBI_SLICED_TELETEXT_B_625)
		vbi_page_table_remove_all_pages (sf->keep_ttx_pages);

	return sf->keep_services |= services;
}

void
vbi_sliced_filter_reset	(vbi_sliced_filter *	sf)
{
	assert (NULL != sf);

	sf->keep_mag_set_next = 0;
	sf->start = TRUE;
}

static vbi_bool
decode_teletext_packet_0	(vbi_sliced_filter *	sf,
				 unsigned int *		keep_mag_set,
				 const uint8_t		buffer[42],
				 unsigned int		magazine)
{
	int page;
	int flags;
	vbi_pgno pgno;
	unsigned int mag_set;

	page = vbi_unham16p (buffer + 2);
	if (unlikely (page < 0)) {
		set_errstr (sf, _("Hamming error in Teletext "
				  "page number."));
		errno = VBI_ERR_PARITY;
		return FALSE;
	}

	if (0xFF == page) {
		/* Filler, discard. */
		*keep_mag_set = 0;
		return TRUE;
	}

	pgno = magazine * 0x100 + page;

	flags = vbi_unham16p (buffer + 4)
		| (vbi_unham16p (buffer + 6) << 8)
		| (vbi_unham16p (buffer + 8) << 16);
	if (unlikely (flags < 0)) {
		set_errstr (sf, _("Hamming error in Teletext "
				  "packet flags."));
		errno = VBI_ERR_PARITY;
		return FALSE;
	}

	/* Blank lines are not transmitted and there's no page end mark,
	   so Teletext decoders wait for another page before displaying
	   the previous one. In serial transmission mode that is any
	   page, in parallel mode a page of the same magazine. */
	if (flags & VBI_SERIAL) {
		mag_set = -1;
	} else {
		mag_set = 1 << magazine;
	}

	if (!vbi_is_bcd (pgno)) {
		/* Page inventories and TOP pages (e.g. to
		   find subtitles), DRCS and object pages, etc. */
		if (sf->keep_ttx_system_pages)
			goto match;
	} else {
		vbi_subno subno;

		subno = flags & 0x3F7F;

		if (vbi_page_table_contains_subpage (sf->keep_ttx_pages,
						      pgno, subno))
			goto match;
	}

	if (*keep_mag_set & mag_set) {
		/* To terminate the previous page we keep the header
		   packet of this page (keep_mag_set) but discard all
		   following packets (keep_mag_set_next). */
		sf->keep_mag_set_next = *keep_mag_set & ~mag_set;
	} else if (sf->start) {
		/* Keep the very first page header and its timestamp,
		   which is important for subtitle timing. */
		*keep_mag_set = mag_set;
		sf->keep_mag_set_next = 0;
	} else {
		/* Discard this and following packets until we
		   find another header packet. */
		*keep_mag_set &= ~mag_set;
		sf->keep_mag_set_next = *keep_mag_set;
	}

	sf->start = FALSE;

	return TRUE;

 match:
	/* Keep this and following packets. */
	*keep_mag_set |= mag_set;
	sf->keep_mag_set_next = *keep_mag_set;

	sf->start = FALSE;

	return TRUE;
}

static vbi_bool
decode_teletext			(vbi_sliced_filter *	sf,
				 vbi_bool *		keep,
				 const uint8_t		buffer[42],
				 unsigned int		line)
{
	int pmag;
	unsigned int magazine;
	unsigned int packet;
	unsigned int keep_mag_set;

	line = line;

	keep_mag_set = sf->keep_mag_set_next;

	pmag = vbi_unham16p (buffer);
	if (unlikely (pmag < 0)) {
		set_errstr (sf, _("Hamming error in Teletext "
				  "packet/magazine number."));
		errno = VBI_ERR_PARITY;
		return FALSE;
	}

	magazine = pmag & 7;
	if (0 == magazine)
		magazine = 8;

	packet = pmag >> 3;

	switch (packet) {
	case 0: /* page header */
		if (!decode_teletext_packet_0 (sf, &keep_mag_set,
					       buffer, magazine))
			return FALSE;
		break;

	case 1 ... 25: /* page body */
		break;

	case 26: /* page enhancement packet */
	case 27: /* page linking */
	case 28:
	case 29: /* level 2.5/3.5 enhancement */
		break;

	case 30:
	case 31: /* IDL packet (ETS 300 708). */
		*keep = FALSE;
		return TRUE;

	default:
		assert (0);
	}

	*keep = !!(keep_mag_set & (1 << magazine));

	return TRUE;
}

/**
 * @brief Sliced VBI filter coroutine.
 * @param sf Sliced VBI filter context allocated with
 *   vbi_sliced_filter_new().
 * @param sliced_out Filtered sliced data will be stored here.
 *   @a sliced_out and @a sliced_in can be the same.
 * @param n_lines_out The number of sliced lines in the
 *   @a sliced_out buffer will be stored here.
 * @param max_lines_out The maximum number of sliced lines this
 *   function may store in the @a sliced_out buffer.
 * @param sliced_in The sliced data to be filtered.
 * @param n_lines_in Pointer to a variable which contains the
 *   number of sliced lines to be read from the @a sliced_in buffer.
 *   When the function fails, it stores here the number of sliced
 *   lines successfully read so far.
 *
 * This function takes one video frame worth of sliced VBI data and
 * filters out the lines which match the selected criteria.
 *
 * @returns
 * @c TRUE on success. @c FALSE if there is not enough room in the
 * output buffer to store the filtered data, or when the function
 * detects an error in the sliced input data. On failure the
 * @a sliced_out buffer will contain the data successfully filtered
 * so far, @a *n_lines_out will be valid, and @a *n_lines_in will
 * contain the number of lines read so far.
 *
 * @since 99.99.99
 */
vbi_bool
vbi_sliced_filter_cor		(vbi_sliced_filter *	sf,
				 vbi_sliced *		sliced_out,
				 unsigned int *		n_lines_out,
				 unsigned int		max_lines_out,
				 const vbi_sliced *	sliced_in,
				 unsigned int *		n_lines_in)
{
	unsigned int in;
	unsigned int out;

	assert (NULL != sf);
	assert (NULL != sliced_out);
	assert (NULL != n_lines_out);
	assert (NULL != sliced_in);

	errno = 0;

	out = 0;

	for (in = 0; in < *n_lines_in; ++in) {
		vbi_bool pass_through;

		pass_through = FALSE;

		if (sliced_in[in].id & sf->keep_services) {
			pass_through = TRUE;
		} else {
			switch (sliced_in[in].id) {
			case VBI_SLICED_TELETEXT_B_L10_625:
			case VBI_SLICED_TELETEXT_B_L25_625:
			case VBI_SLICED_TELETEXT_B_625:
				if (!decode_teletext (sf,
						      &pass_through,
						      sliced_in[in].data,
						      sliced_in[in].line))
					goto failed;
				break;

			default:
				break;
			}
		}

		if (pass_through) {
			if (out >= max_lines_out) {
				set_errstr (sf, _("Output buffer overflow."));
				errno = VBI_ERR_BUFFER_OVERFLOW;
				goto failed;
			}

			memcpy (&sliced_out[out],
				&sliced_in[in],
				sizeof (*sliced_out));
			++out;
		}
	}

	*n_lines_out = out;

	return TRUE;

 failed:
	*n_lines_in = in;
	*n_lines_out = out;

	return FALSE;
}

/**
 * @brief Feeds the sliced VBI filter with data.
 * @param sf Sliced VBI filter context allocated with
 *   vbi_sliced_filter_new().
 * @param sliced The sliced data to be filtered.
 * @param n_lines Pointer to a variable which contains the
 *   number of sliced lines to be read from the @a sliced buffer.
 *   When the function fails, it stores here the number of sliced
 *   lines successfully read so far.
 *
 * This function takes one video frame worth of sliced VBI data and
 * filters out the lines which match the selected criteria. Then if
 * no error occurred it calls the callback function passed to
 * vbi_sliced_filter_new() with a pointer to the filtered lines.
 *
 * @returns
 * @c TRUE on success. @c FALSE if the function detects an error in
 * the sliced input data, and @a *n_lines_in will contain the lines
 * successfully read so far.
 *
 * @since 99.99.99
 */
vbi_bool
vbi_sliced_filter_feed		(vbi_sliced_filter *	sf,
				 const vbi_sliced *	sliced,
				 unsigned int *		n_lines)
{
	unsigned int n_lines_out;

	assert (NULL != sf);
	assert (NULL != sliced);
	assert (NULL != n_lines);
	assert (*n_lines <= UINT_MAX / sizeof (*sf->output_buffer));

	if (unlikely (sf->output_max_lines < *n_lines)) {
		vbi_sliced *s;
		unsigned int n;

		n = MIN (*n_lines, 50U);
		s = vbi_realloc (sf->output_buffer,
				  n * sizeof (*sf->output_buffer));
		if (unlikely (NULL == s)) {
			no_mem_error (sf);
			return FALSE;
		}

		sf->output_buffer = s;
		sf->output_max_lines = n;
	}

	if (!vbi_sliced_filter_cor (sf,
				     sf->output_buffer,
				     &n_lines_out,
				     sf->output_max_lines,
				     sliced,
				     n_lines)) {
		return FALSE;
	}

	if (NULL != sf->callback) {
		return sf->callback (sf,
				     sf->output_buffer,
				     n_lines_out,
				     sf->user_data);
	}

	return TRUE;
}

const char *
vbi_sliced_filter_errstr	(vbi_sliced_filter *	sf)
{
	assert (NULL != sf);

	return sf->errstr;
}

void
vbi_sliced_filter_set_log_fn	(vbi_sliced_filter *    sf,
				 vbi_log_mask		mask,
				 vbi_log_fn *		log_fn,
				 void *			user_data)
{
	assert (NULL != sf);

	if (NULL == log_fn)
		mask = 0;

	sf->log.mask = mask;
	sf->log.fn = log_fn;
	sf->log.user_data = user_data;
}

void
vbi_sliced_filter_delete	(vbi_sliced_filter *	sf)
{
	if (NULL == sf)
		return;

	vbi_page_table_delete (sf->keep_ttx_pages);

	vbi_free (sf->output_buffer);
	vbi_free (sf->errstr);

	CLEAR (*sf);

	vbi_free (sf);		
}

vbi_sliced_filter *
vbi_sliced_filter_new		(vbi_sliced_filter_cb *callback,
				 void *			user_data)
{
	vbi_sliced_filter *sf;

	sf = vbi_malloc (sizeof (*sf));
	if (NULL == sf) {
		return NULL;
	}

	CLEAR (*sf);

	sf->keep_ttx_pages = vbi_page_table_new ();
	if (NULL == sf->keep_ttx_pages) {
		vbi_free (sf);
		return NULL;
	}

	vbi_sliced_filter_reset (sf);

	sf->callback = callback;
	sf->user_data = user_data;

	return sf;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
