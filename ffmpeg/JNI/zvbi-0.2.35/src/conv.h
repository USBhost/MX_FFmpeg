/*
 *  libzvbi -- Unicode conversion helper functions
 *
 *  Copyright (C) 2003-2006 Michael H. Schimek
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

/* $Id: conv.h,v 1.9 2008/02/24 14:17:58 mschimek Exp $ */

#ifndef __ZVBI_CONV_H__
#define __ZVBI_CONV_H__

#include "macros.h"
#include "lang.h"		/* vbi_ttx_charset */
#ifndef ZAPPING8
#  include "version.h"
#endif

VBI_BEGIN_DECLS

/* Public */

#include <stdio.h>
#include <inttypes.h>		/* uint16_t */

/**
 * @addtogroup Conv Character set conversion functions
 * @ingroup LowDec
 * @brief Helper functions to convert between Closed Caption, Teletext,
 *   Unicode and the locale character set.
 * @{
 */
 
#define VBI_NUL_TERMINATED -1

extern unsigned long
vbi_strlen_ucs2			(const uint16_t *	src);
extern char *
vbi_strndup_iconv		(const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
  _vbi_alloc;
extern char *
vbi_strndup_iconv_ucs2		(const char *		dst_codeset,
				 const uint16_t *	src,
				 long			src_length,
				 int			repl_char)
  _vbi_alloc;
extern char *
vbi_strndup_iconv_caption	(const char *		dst_codeset,
				 const char *		src,
				 long			src_length,
				 int			repl_char)
  _vbi_alloc;
#if 3 == VBI_VERSION_MINOR
extern char *
vbi_strndup_iconv_teletext	(const char *		dst_codeset,
				 const vbi_ttx_charset *cs,
				 const uint8_t *	src,
				 long			src_length,
				 int			repl_char)
  _vbi_alloc _vbi_nonnull ((2));
#endif
extern vbi_bool
vbi_fputs_iconv			(FILE *			fp,
				 const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_fputs_iconv_ucs2		(FILE *			fp,
				 const char *		dst_codeset,
				 const uint16_t *	src,
				 long			src_length,
				 int			repl_char)
  _vbi_nonnull ((1));
extern const char *
vbi_locale_codeset		(void);

/** @} */

/* Private */

typedef struct _vbi_iconv_t vbi_iconv_t;

extern char *
_vbi_strndup_iconv		(unsigned long *	out_size,
				 const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
  _vbi_alloc _vbi_nonnull ((1, 2, 3));
extern vbi_bool
_vbi_iconv_ucs2			(vbi_iconv_t *		cd,
				 char **		dst,
				 unsigned long		dst_size,
				 const uint16_t *	src,
				 long			src_length)
  _vbi_nonnull ((1, 2));
extern void
_vbi_iconv_close		(vbi_iconv_t *		cd);
extern vbi_iconv_t *
_vbi_iconv_open			(const char *		dst_codeset,
				 const char *		src_codeset,
				 char **		dst,
				 unsigned long		dst_size,
				 int			repl_char);

VBI_END_DECLS

#endif /* __ZVBI_CONV_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
