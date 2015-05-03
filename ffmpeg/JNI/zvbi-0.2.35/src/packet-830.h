/*
 *  libzvbi - Teletext packet decoder, packet 8/30
 *
 *  Copyright (C) 2003, 2004 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id: packet-830.h,v 1.3 2009/03/04 21:48:40 mschimek Exp $ */

#ifndef __ZVBI_PACKET_830_H__
#define __ZVBI_PACKET_830_H__

#include <inttypes.h>		/* uint8_t */
#include <time.h>		/* time_t */
#include "macros.h"
#include "pdc.h"		/* vbi_program_id */

VBI_BEGIN_DECLS

/* Public */

/**
 * @addtogroup Packet830
 * @{
 */
extern vbi_bool
vbi_decode_teletext_8301_cni	(unsigned int *		cni,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_decode_teletext_8301_local_time
				(time_t *		utc_time,
				 int *			seconds_east,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2, 3))
#endif
  ;
extern vbi_bool
vbi_decode_teletext_8302_cni	(unsigned int *		cni,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_decode_teletext_8302_pdc	(vbi_program_id *	pid,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
/** @} */

/* Private */

VBI_END_DECLS

#endif /* __ZVBI_PACKET_830_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
