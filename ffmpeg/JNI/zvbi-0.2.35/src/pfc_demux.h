/*
 *  libzvbi -- Teletext Page Format Clear packet demultiplexer
 *
 *  Copyright (C) 2003, 2004, 2007 Michael H. Schimek
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

/* $Id: pfc_demux.h,v 1.14 2008/02/24 14:17:10 mschimek Exp $ */

#ifndef __ZVBI_PFC_DEMUX_H__
#define __ZVBI_PFC_DEMUX_H__

#include <inttypes.h>		/* uint8_t */
#include <stdio.h>		/* FILE */
#include "bcd.h"		/* vbi_pgno */
#include "sliced.h"

VBI_BEGIN_DECLS

/* Public */

/**
 * @addtogroup PFCDemux Teletext PFC packet demultiplexer
 * @ingroup LowDec
 * @brief Functions to decode data transmissions in Teletext
 *   Page Function Clear packets (EN 300 708 section 4).
 * @{
 */

/**
 * @brief One block of data returned by vbi_pfc_demux_cb().
 */
typedef struct {
	/** Source page as requested with vbi_pfc_demux_new(). */
	vbi_pgno		pgno;

	/** Source stream as requested with vbi_pfc_demux_new(). */
	unsigned int		stream;

	/** Application ID transmitted with this data block. */
	unsigned int		application_id;

	/** Size of the data block in bytes, 1 ... 2048. */
	unsigned int		block_size;

	/** Data block. */
	uint8_t			block[2048];
} vbi_pfc_block;

/**
 * @brief PFC demultiplexer context.
 *
 * The contents of this structure are private.
 *
 * Call vbi_pfc_demux_new() to allocate a PFC
 * demultiplexer context.
 */
typedef struct _vbi_pfc_demux vbi_pfc_demux;

/**
 * @param dx PFC demultiplexer context returned by
 *   vbi_pfx_demux_new() and given to vbi_pfc_demux_feed().
 * @param user_data User pointer given to vbi_pfc_demux_new().
 * @param block Structure describing the received data block.
 * 
 * Function called by vbi_pfc_demux_feed() when a
 * new data block is available.
 *
 * @returns
 * FALSE on error, will be returned by vbi_pfc_demux_feed().
 *
 * @bug
 * vbi_pfc_demux_feed() returns the @a user_data pointer as second
 * parameter the @a block pointer as third parameter, but prior to
 * version 0.2.26 this function incorrectly defined @a block as
 * second and @a user_data as third parameter.
 */
typedef vbi_bool
vbi_pfc_demux_cb		(vbi_pfc_demux *	dx,
				 void *			user_data,
				 const vbi_pfc_block *	block);

extern void
vbi_pfc_demux_reset		(vbi_pfc_demux *	dx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_pfc_demux_feed		(vbi_pfc_demux *	dx,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_pfc_demux_feed_frame	(vbi_pfc_demux *	dx,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern void
vbi_pfc_demux_delete		(vbi_pfc_demux *	dx);
extern vbi_pfc_demux *
vbi_pfc_demux_new		(vbi_pgno		pgno,
				 unsigned int		stream,
				 vbi_pfc_demux_cb *	callback,
				 void *			user_data)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_alloc _vbi_nonnull ((3))
#endif
  ;

/** @} */

/* Private */

/**
 * @internal
 */
struct _vbi_pfc_demux {
	/** Expected next continuity index. */
	unsigned int		ci;

	/** Expected next packet. */
	unsigned int		packet;

	/** Expected number of packets. */
	unsigned int		n_packets;

	/** Block index. */
	unsigned int		bi;

	/** Expected number of block bytes. */
	unsigned int		left;

	vbi_pfc_demux_cb *	callback;
	void *			user_data;

	vbi_pfc_block		block;
};

extern void
_vbi_pfc_block_dump		(const vbi_pfc_block *	pb,
				 FILE *			fp,
				 vbi_bool		binary)
  _vbi_nonnull ((1, 2));
extern vbi_bool
_vbi_pfc_demux_decode		(vbi_pfc_demux *	dx,
				 const uint8_t		buffer[42])
  _vbi_nonnull ((1, 2));
extern void
_vbi_pfc_demux_destroy		(vbi_pfc_demux *	dx)
  _vbi_nonnull ((1));
extern vbi_bool
_vbi_pfc_demux_init		(vbi_pfc_demux *	dx,
				 vbi_pgno		pgno,
				 unsigned int		stream,
				 vbi_pfc_demux_cb *	callback,
				 void *			user_data)
  _vbi_nonnull ((1, 4));

VBI_END_DECLS

#endif /* __ZVBI_PFC_DEMUX_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
