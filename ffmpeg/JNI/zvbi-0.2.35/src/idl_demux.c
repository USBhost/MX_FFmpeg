/*
 *  libzvbi -- Teletext Independent Data Line packet demultiplexer
 *
 *  Copyright (C) 2005 Michael H. Schimek
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

/* $Id: idl_demux.c,v 1.11 2013/07/10 11:37:23 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "misc.h"
#include "hamm.h"		/* vbi_unham8() */
#include "idl_demux.h"

static void
init_crc16_table		(uint16_t		table[256],
				 unsigned int		poly)
{
	unsigned int i;

	for (i = 0; i < 256; ++i) {
		unsigned int crc;
		unsigned int val;
		unsigned int j;

		crc = 0;
		val = i;

		for (j = 0; j < 8; ++j) {
			crc = (crc >> 1) ^ (poly & ((1 & ~(val ^ crc)) - 1));
			val >>= 1;
		}

		table[i] = crc;
	}
}

/* EN 300 708 section 6.5 IDL Format A */

#define FT_HAVE_RI		(1 << 1)
#define FT_HAVE_CI		(1 << 2)
#define FT_HAVE_DL		(1 << 3)

#define RI_PACKET_REPEATS	(1 << 7)

/* 6.5.7.1 Dummy bytes */
#define SKIP_DUMMY_BYTES	1

static uint16_t			idl_a_crc_table [256];

static vbi_bool
idl_a_demux_feed		(vbi_idl_demux *	dx,
				 const uint8_t		buffer[42],
				 int			ft)
{
	uint8_t buf[40];
	uint8_t hist[256];
	int ial;		/* interpretation and address length */
	unsigned int spa_length;
	int spa;		/* service packet address */
	unsigned int ri;	/* repeat indicator */
	unsigned int ci;	/* continuity indicator */
	unsigned int dl;	/* data length */
	unsigned int crc;
	unsigned int flags;
	unsigned int i;
	unsigned int j;

	ial = vbi_unham8 (buffer[3]);
	if (ial < 0) {
		return FALSE;
	}

	spa_length = (unsigned int) ial & 7;
	if (7 == spa_length) /* reserved */
		return TRUE;

	spa = 0;

	for (i = 0; i < spa_length; ++i)
		spa |= vbi_unham8 (buffer[4 + i]) << (4 * i);

	if (spa < 0) {
		return FALSE;
	}

	if (spa != dx->address)
		return TRUE;

	ri = 0;
	if (ft & FT_HAVE_RI) {
		ri = buffer[4 + i++];
	}

	crc = 0;

	for (j = 4 + i; j < 42; ++j) {
		crc = (crc >> 8) ^ idl_a_crc_table[(crc & 0xFF) ^ buffer[j]];
	}

	if (ft & FT_HAVE_CI) {
		ci = buffer[4 + i++];
	} else {
		ci = crc & 0xFF;
		crc ^= ci | (ci << 8);
	}

	if (0 != crc) {
		if (0 == (ri & RI_PACKET_REPEATS)) {
			/* Packet is corrupt and won't repeat. */

			dx->ci = -1;
			dx->ri = -1;

			dx->flags |= VBI_IDL_DATA_LOST;

			return FALSE;
		} else {
			/* Try again. */

			dx->ri = ri + 1;

			return FALSE;
		}
	}

	if (dx->ri >= 0) {
		if (0 != ((ri ^ dx->ri) & 0xF)) {
			/* Repeat packet(s) lost. */

			dx->ci = -1;
			dx->ri = -1;

			dx->flags |= VBI_IDL_DATA_LOST;

			if (0 != (ri & 0xF)) {
				/* Discard repeat packet. */
				return TRUE;
			}
		}
	} else if (0 != (ri & 0xF)) {
		/* Discard repeat packet. */
		return TRUE;
	}

	if (dx->ci >= 0) {
		if (0 != ((ci ^ dx->ci) & 0xFF)) {
			/* Packet(s) lost. */

			dx->flags |= VBI_IDL_DATA_LOST;
		}
	}

	hist[0x00] = 0;
	hist[0xFF] = 0;
	hist[ci] = 1;

	dx->ci = ci + 1;

	if (ft & FT_HAVE_DL) {
		dl = buffer[4 + i++] & 0x3F;
		dl = MIN (dl, 36 - i);
	} else {
		dl = 36 - i;
	}

	j = 0;

	while (dl-- > 0) {
		unsigned int t;

		t = buffer[4 + i++];

		if (SKIP_DUMMY_BYTES) {
			++hist[t];

			if ((hist[0x00] | hist[0xFF]) & 8) {
				/* 6.5.7.1 Skip dummy byte after
				   8 consecutive bytes of 0x00 or 0xFF. */

				hist[0x00] = 0;
				hist[0xFF] = 0;

				continue;
			}
		}

		buf[j++] = t;
	}

	flags = dx->flags | (ial & VBI_IDL_DEPENDENT);
	dx->flags &= ~VBI_IDL_DATA_LOST;

	return dx->callback (dx, buf, j, dx->flags, dx->user_data);
}


/* EN 300 708 section 6.8 IDL Format B */

static vbi_bool
idl_b_demux_feed		(vbi_idl_demux *	dx,
				 const uint8_t		buffer[42],
				 int			ft)
{
	/* TODO */

	dx = dx;
	buffer = buffer;
	ft = ft;

	return FALSE;
}


/* EN 300 708 section 6.6 IDL Datavideo format */

static vbi_bool
datavideo_demux_feed		(vbi_idl_demux *	dx,
				 const uint8_t		buffer[42])
{
	/* TODO */

	dx = dx;
	buffer = buffer;

	return FALSE;
}


/* EN 300 708 section 6.7 IDL Low bit rate audio */

static vbi_bool
audetel_demux_feed		(vbi_idl_demux *	dx,
				 const uint8_t		buffer[42])
{
	/* TODO */

	dx = dx;
	buffer = buffer;

	return FALSE;
}

static vbi_bool
lbra_demux_feed			(vbi_idl_demux *	dx,
				 const uint8_t		buffer[42])
{
	/* TODO */

	dx = dx;
	buffer = buffer;

	return FALSE;
}


/**
 * @param dx IDL demultiplexer allocated with vbi_idl_a_demux_new().
 *
 * Resets the IDL demux context, useful for example after a channel
 * change.
 *
 * @since 0.2.14
 */
void
vbi_idl_demux_reset		(vbi_idl_demux *	dx)
{
	assert (NULL != dx);

	dx->ci = -1;
	dx->ri = -1;
}

/**
 * @param dx IDL demultiplexer allocated with vbi_idl_a_demux_new().
 * @param buffer Teletext packet (last 42 bytes, i. e. without clock
 *   run-in and framing code), as in struct vbi_sliced.
 *
 * This function takes a stream of Teletext packets, filters out packets
 * of the desired data channel and address and calls the output
 * function given to vbi_idl_a_demux_new() when new user data is available.
 *
 * @returns
 * FALSE if the packet contained uncorrectable errors.
 *
 * @since 0.2.14
 */
vbi_bool
vbi_idl_demux_feed		(vbi_idl_demux *	dx,
				 const uint8_t		buffer[42])
{
	int channel;
	int designation;
	int ft; /* format type */

	assert (NULL != dx);
	assert (NULL != buffer);

	channel = vbi_unham8 (buffer[0]);
	designation = vbi_unham8 (buffer[1]);

	if ((channel | designation) < 0)
		return FALSE;

	if (15 != designation /* packet 30 or 31 */
	    || channel != dx->channel)
		return TRUE;

	switch (dx->format) {
	case _VBI_IDL_FORMAT_A:
		if ((ft = vbi_unham8 (buffer[2])) < 0)
			return FALSE;

		if (0 == (ft & 1))
			return idl_a_demux_feed (dx, buffer, ft);
		else
			return TRUE;

	case _VBI_IDL_FORMAT_B:
		if ((ft = vbi_unham8 (buffer[2])) < 0)
			return FALSE;

		if (1 == (ft & 3))
			return idl_b_demux_feed (dx, buffer, ft);
		else
			return TRUE;

	case _VBI_IDL_FORMAT_DATAVIDEO:
		return datavideo_demux_feed (dx, buffer);

	case _VBI_IDL_FORMAT_AUDETEL: /* 6.7.2 */
		return audetel_demux_feed (dx, buffer);

	case _VBI_IDL_FORMAT_LBRA: /* 6.7.3 */
		return lbra_demux_feed (dx, buffer);

	default:
		assert (0);
	}
}

/**
 * @param dx IDL demultiplexer allocated with vbi_idl_a_demux_new().
 * @param sliced Sliced VBI data.
 * @param n_lines Number of lines in the @a sliced array.
 *
 * This function works like vbi_idl_demux_feed() but operates
 * on sliced VBI data and filters out @c VBI_SLICED_TELETEXT_B_625.
 *
 * @returns
 * FALSE if any Teletext lines contained uncorrectable errors.
 *
 * @since 0.2.26
 */
vbi_bool
vbi_idl_demux_feed_frame	(vbi_idl_demux *	dx,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines)
{
	const vbi_sliced *end;

	assert (NULL != dx);
	assert (NULL != sliced);

	for (end = sliced + n_lines; sliced < end; ++sliced) {
		if (sliced->id & VBI_SLICED_TELETEXT_B_625) {
			if (!vbi_idl_demux_feed (dx, sliced->data))
				return FALSE;
		}
	}

	return TRUE;
}

/** @internal */
void
_vbi_idl_demux_destroy		(vbi_idl_demux *	dx)
{
	assert (NULL != dx);

	CLEAR (*dx);
}

/** @internal */
vbi_bool
_vbi_idl_demux_init		(vbi_idl_demux *	dx,
				 _vbi_idl_format	format,
				 unsigned int		channel,
				 unsigned int		address,
				 vbi_idl_demux_cb *	callback,
				 void *			user_data)
{
	assert (NULL != dx);
	assert (NULL != callback);

	if (channel >= (1 << 4))
		return FALSE;

	switch (format) {
	case _VBI_IDL_FORMAT_A:
		if (address >= (1 << 24))
			return FALSE;

		if (0 == idl_a_crc_table[1]) {
			/* x16 + x9 + x7 + x4 + 1 */
			init_crc16_table (idl_a_crc_table, 0x8940);
		}

		break;

	case _VBI_IDL_FORMAT_DATAVIDEO:
	case _VBI_IDL_FORMAT_B:
	case _VBI_IDL_FORMAT_AUDETEL:
	case _VBI_IDL_FORMAT_LBRA:
		/* TODO */
		break;

	default:
		assert (0);
	}

	dx->format		= format;
	dx->channel		= channel;
	dx->address		= address;

	vbi_idl_demux_reset (dx);

	dx->callback		= callback;
	dx->user_data		= user_data;

	return TRUE;
}

/**
 * @param dx IDL demultiplexer allocated with
 *   vbi_idl_a_demux_new(), can be @c NULL.
 *
 * Frees all resources associated with @a dx.
 *
 * @since 0.2.14
 */
void
vbi_idl_demux_delete		(vbi_idl_demux *	dx)
{
	if (NULL == dx)
		return;

	_vbi_idl_demux_destroy (dx);

	vbi_free (dx);		
}

/**
 * @param channel Filter out packets of this channel. 
 * @param address Filter out packets with this service data address.
 * @param callback Function to be called by vbi_idl_demux_feed() when
 *   new data is available.
 * @param user_data User pointer passed through to @a callback function.
 *
 * Allocates a new Independent Data Line format A (EN 300 708 section 6.5)
 * demultiplexer.
 *
 * @returns
 * Pointer to newly allocated IDL demultiplexer which must be
 * freed with vbi_idl_demux_delete() when done. @c NULL on failure
 * (out of memory).
 *
 * @since 0.2.14
 */
vbi_idl_demux *
vbi_idl_a_demux_new		(unsigned int		channel,
				 unsigned int		address,
				 vbi_idl_demux_cb *	callback,
				 void *			user_data)
{
	vbi_idl_demux *dx;

	assert (NULL != callback);

	if (!(dx = vbi_malloc (sizeof (*dx)))) {
		return NULL;
	}

	if (!_vbi_idl_demux_init (dx, _VBI_IDL_FORMAT_A, channel, address,
				  callback, user_data)) {
		vbi_free (dx);
		dx = NULL;
	}

	return dx;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
