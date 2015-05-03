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

/* $Id: sliced_filter.h,v 1.5 2008/02/24 14:16:58 mschimek Exp $ */

#ifndef __ZVBI_SLICED_FILTER_H__
#define __ZVBI_SLICED_FILTER_H__

#include "macros.h"
#include "bcd.h"
#include "sliced.h"		/* vbi_sliced, vbi_service_set */

VBI_BEGIN_DECLS

typedef struct _vbi_sliced_filter vbi_sliced_filter;

typedef vbi_bool
vbi_sliced_filter_cb		(vbi_sliced_filter *	sf,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 void *			user_data);

extern vbi_service_set
vbi_sliced_filter_keep_services
				(vbi_sliced_filter *	sf,
				 vbi_service_set	services)
  _vbi_nonnull ((1));
extern vbi_service_set
vbi_sliced_filter_drop_services
				(vbi_sliced_filter *	sf,
				 vbi_service_set	services)
  _vbi_nonnull ((1));

extern vbi_bool
vbi_sliced_filter_keep_ttx_pages
				(vbi_sliced_filter *	sf,
				 vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_sliced_filter_drop_ttx_pages
				(vbi_sliced_filter *	sf,
				 vbi_pgno		first_pgno,
				 vbi_pgno		last_pgno)
  _vbi_nonnull ((1));
static __inline__ vbi_bool
vbi_sliced_filter_keep_ttx_page	(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno)
{
	return vbi_sliced_filter_keep_ttx_pages (sf, pgno, pgno);
}
static __inline__ vbi_bool
vbi_sliced_filter_drop_ttx_page	(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno)
{
	return vbi_sliced_filter_drop_ttx_pages (sf, pgno, pgno);
}
extern vbi_bool
vbi_sliced_filter_keep_ttx_subpages
				(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno,
				 vbi_subno		first_subno,
				 vbi_subno		last_subno)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_sliced_filter_drop_ttx_subpages
				(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno,
				 vbi_subno		first_subno,
				 vbi_subno		last_subno)
  _vbi_nonnull ((1));
static __inline__ vbi_bool
vbi_sliced_filter_keep_ttx_subpage
				(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno,
				 vbi_subno		subno)
{
	return vbi_sliced_filter_keep_ttx_subpages (sf, pgno, subno, subno);
}
static __inline__ vbi_bool
vbi_sliced_filter_drop_ttx_subpage
				(vbi_sliced_filter *	sf,
				 vbi_pgno		pgno,
				 vbi_subno		subno)
{
	return vbi_sliced_filter_drop_ttx_subpages (sf, pgno, subno, subno);
}
extern void
vbi_sliced_filter_keep_ttx_system_pages
				(vbi_sliced_filter *	sf,
				 vbi_bool		keep)
  _vbi_nonnull ((1));
extern void
vbi_sliced_filter_reset	(vbi_sliced_filter *	sf)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_sliced_filter_cor		(vbi_sliced_filter *	sf,
				 vbi_sliced *		sliced_out,
				 unsigned int *		n_lines_out,
				 unsigned int		max_lines_out,
				 const vbi_sliced *	sliced_in,
				 unsigned int *		n_lines_in)
  _vbi_nonnull ((1, 2, 3, 5, 6));
extern vbi_bool
vbi_sliced_filter_feed		(vbi_sliced_filter *	sf,
				 const vbi_sliced *	sliced,
				 unsigned int *		n_lines)
  _vbi_nonnull ((1, 2, 3));

extern const char *
vbi_sliced_filter_errstr	(vbi_sliced_filter *	sf)
  _vbi_nonnull ((1));
extern void
vbi_sliced_filter_set_log_fn	(vbi_sliced_filter *	sf,
				 vbi_log_mask		mask,
				 vbi_log_fn *		log_fn,
				 void *			user_data)
  _vbi_nonnull ((1));
extern void
vbi_sliced_filter_delete	(vbi_sliced_filter *	sf);
extern vbi_sliced_filter *
vbi_sliced_filter_new		(vbi_sliced_filter_cb *callback,
				 void *			user_data)
  _vbi_alloc;

VBI_END_DECLS

#endif /* __ZVBI_SLICED_FILTER_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
