/*
 *  libzvbi -- DVB VBI multiplexer
 *
 *  Copyright (C) 2004, 2007 Michael H. Schimek
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

/* $Id: dvb_mux.h,v 1.11 2008/02/24 14:17:51 mschimek Exp $ */

#ifndef __ZVBI_DVB_MUX_H__
#define __ZVBI_DVB_MUX_H__

#include <inttypes.h>		/* uint8_t */
#include "macros.h"
#include "sliced.h"		/* vbi_sliced, vbi_service_set */
#include "sampling_par.h"	/* vbi_videostd_set */

VBI_BEGIN_DECLS

/* Public */

/**
 * @addtogroup DVBMux
 * @{
 */

extern vbi_bool
vbi_dvb_multiplex_sliced	(uint8_t **		packet,
				 unsigned int *		packet_left,
				 const vbi_sliced **	sliced,
				 unsigned int *		sliced_left,
				 vbi_service_set	service_mask,
				 unsigned int		data_identifier,
				 vbi_bool		stuffing)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2, 3, 4))
#endif
  ;
extern vbi_bool
vbi_dvb_multiplex_raw		(uint8_t **		packet,
				 unsigned int *		packet_left,
				 const uint8_t **	raw,
				 unsigned int *		raw_left,
				 unsigned int		data_identifier,
				 vbi_videostd_set	videostd_set,
				 unsigned int		line,
				 unsigned int		first_pixel_position,
				 unsigned int		n_pixels_total,
				 vbi_bool		stuffing)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2, 3, 4))
#endif
  ;

/**
 * @brief DVB VBI multiplexer context.
 *
 * The contents of this structure are private.
 *
 * Call vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new() to allocate
 * a DVB VBI multiplexer context.
 */
typedef struct _vbi_dvb_mux vbi_dvb_mux;

typedef vbi_bool
vbi_dvb_mux_cb			(vbi_dvb_mux *		mx,
				 void *			user_data,
				 const uint8_t *	packet,
				 unsigned int		packet_size);

extern void
vbi_dvb_mux_reset		(vbi_dvb_mux *		mx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_dvb_mux_cor		(vbi_dvb_mux *		mx,
				 uint8_t **		buffer,
				 unsigned int *		buffer_left,
				 const vbi_sliced **	sliced,
				 unsigned int *		sliced_lines,
				 vbi_service_set	service_mask,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sampling_par,	 
				 int64_t		pts)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2, 3, 4, 5))
#endif
  ;
extern vbi_bool
vbi_dvb_mux_feed		(vbi_dvb_mux *		mx,
				 const vbi_sliced *	sliced,
				 unsigned int		sliced_lines,
				 vbi_service_set	service_mask,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sampling_par,
				 int64_t		pts)
  _vbi_nonnull ((1));
extern unsigned int
vbi_dvb_mux_get_data_identifier (const vbi_dvb_mux *	mx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_dvb_mux_set_data_identifier (vbi_dvb_mux *	mx,
				  unsigned int		data_identifier)
  _vbi_nonnull ((1));
extern unsigned int
vbi_dvb_mux_get_min_pes_packet_size
				(vbi_dvb_mux *		mx)
  _vbi_nonnull ((1));
extern unsigned int
vbi_dvb_mux_get_max_pes_packet_size
				(vbi_dvb_mux *		mx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_dvb_mux_set_pes_packet_size (vbi_dvb_mux *	mx,
				  unsigned int		min_size,
				  unsigned int		max_size)
  _vbi_nonnull ((1));
extern void
vbi_dvb_mux_delete		(vbi_dvb_mux *		mx);
extern vbi_dvb_mux *
vbi_dvb_pes_mux_new		(vbi_dvb_mux_cb *	callback,
				 void *			user_data)
  _vbi_alloc;
extern vbi_dvb_mux *
vbi_dvb_ts_mux_new		(unsigned int		pid,
				 vbi_dvb_mux_cb *	callback,
				 void *			user_data)
  _vbi_alloc;

/** @} */

/* Private */

VBI_END_DECLS

#endif /* __ZVBI_DVB_MUX_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
