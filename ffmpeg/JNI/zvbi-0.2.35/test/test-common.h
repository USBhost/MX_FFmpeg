/*
 *  libzvbi - Unit test helper functions
 *
 *  Copyright (C) 2007 Michael H. Schimek
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

/* $Id: test-common.h,v 1.6 2009/03/04 21:48:11 mschimek Exp $ */

#include <string.h>
#include <inttypes.h>

#include "src/macros.h"

#define RAND(var) memset_rand (&(var), sizeof (var))

extern void *
memset_rand			(void *			dst,
				 size_t			n)
  _vbi_nonnull ((1));
extern int
memcmp_zero			(const void *		src,
				 size_t			n)
  _vbi_nonnull ((1));
extern void *
xmalloc				(size_t			n_bytes)
  _vbi_alloc;
extern void *
xralloc				(size_t			n_bytes)
  _vbi_alloc;
extern void *
xmemdup				(const void *		src,
				 size_t			n_bytes)
  _vbi_alloc _vbi_nonnull ((1));

extern void
test_malloc			(void			(* function)(void),
				 unsigned int		n_cycles = 1);

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
