/*
 *  libzvbi -- Extended Data Service demultiplexer
 *
 *  Copyright (C) 2000-2005 Michael H. Schimek
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

/* $Id: xds_demux.h,v 1.11 2008/02/24 14:16:16 mschimek Exp $ */

#ifndef __ZVBI_XDS_DEMUX_H__
#define __ZVBI_XDS_DEMUX_H__

#include <inttypes.h>		/* uint8_t */
#include <stdio.h>		/* FILE */
#include "macros.h"
#include "sliced.h"

VBI_BEGIN_DECLS

/* Public */

/**
 * @addtogroup XDSDemux
 * @{
 */

/**
 * @brief XDS packet class.
 * XDS data is transmitted in packets. Each packet belongs
 * to one of seven classes.
 */
typedef enum {
	VBI_XDS_CLASS_CURRENT = 0x00,
	VBI_XDS_CLASS_FUTURE,
	VBI_XDS_CLASS_CHANNEL,
	VBI_XDS_CLASS_MISC,
	VBI_XDS_CLASS_PUBLIC_SERVICE,
	VBI_XDS_CLASS_RESERVED,
	VBI_XDS_CLASS_UNDEFINED
} vbi_xds_class;

#define VBI_XDS_MAX_CLASSES (VBI_XDS_CLASS_UNDEFINED + 1)

/**
 * @brief @c VBI_XDS_CLASS_CURRENT and @c VBI_XDS_CLASS_FUTURE subclass.
 */
typedef enum {
	VBI_XDS_PROGRAM_ID = 0x01,
	VBI_XDS_PROGRAM_LENGTH,
	VBI_XDS_PROGRAM_NAME,
	VBI_XDS_PROGRAM_TYPE,
	VBI_XDS_PROGRAM_RATING,
	VBI_XDS_PROGRAM_AUDIO_SERVICES,
	VBI_XDS_PROGRAM_CAPTION_SERVICES,
	VBI_XDS_PROGRAM_CGMS,
	VBI_XDS_PROGRAM_ASPECT_RATIO,
	/** @since 0.2.17 */
	VBI_XDS_PROGRAM_DATA = 0x0C,
	/** @since 0.2.17 */
	VBI_XDS_PROGRAM_MISC_DATA,
	VBI_XDS_PROGRAM_DESCRIPTION_BEGIN = 0x10,
	VBI_XDS_PROGRAM_DESCRIPTION_END = 0x18
} vbi_xds_subclass_program;

/** @brief @c VBI_XDS_CLASS_CHANNEL subclass. */
typedef enum {
	VBI_XDS_CHANNEL_NAME = 0x01,
	VBI_XDS_CHANNEL_CALL_LETTERS,
	VBI_XDS_CHANNEL_TAPE_DELAY,
	/** @since 0.2.17 */
	VBI_XDS_CHANNEL_TSID
} vbi_xds_subclass_channel;

/** @brief @c VBI_XDS_CLASS_MISC subclass. */
typedef enum {
	VBI_XDS_TIME_OF_DAY = 0x01,
	VBI_XDS_IMPULSE_CAPTURE_ID,
	VBI_XDS_SUPPLEMENTAL_DATA_LOCATION,
	VBI_XDS_LOCAL_TIME_ZONE,
	/** @since 0.2.17 */
	VBI_XDS_OUT_OF_BAND_CHANNEL = 0x40,
	/** @since 0.2.17 */
	VBI_XDS_CHANNEL_MAP_POINTER,
	/** @since 0.2.17 */
	VBI_XDS_CHANNEL_MAP_HEADER,
	/** @since 0.2.17 */
	VBI_XDS_CHANNEL_MAP
} vbi_xds_subclass_misc;

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/* Compatibility. */
#define VBI_XDS_MISC_TIME_OF_DAY VBI_XDS_TIME_OF_DAY
#define VBI_XDS_MISC_IMPULSE_CAPTURE_ID VBI_XDS_IMPULSE_CAPTURE_ID
#define VBI_XDS_MISC_SUPPLEMENTAL_DATA_LOCATION \
	VBI_XDS_SUPPLEMENTAL_DATA_LOCATION
#define VBI_XDS_MISC_LOCAL_TIME_ZONE VBI_XDS_LOCAL_TIME_ZONE

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * @brief @c VBI_XDS_CLASS_PUBLIC_SERVICE subclass.
 * @since 0.2.17
 */
typedef enum {
	VBI_XDS_WEATHER_BULLETIN = 0x01,
	VBI_XDS_WEATHER_MESSAGE
} vbi_xds_subclass_public_service;

#define VBI_XDS_MAX_SUBCLASSES (0x18)

/**
 * @brief Generic XDS subclass.
 * You must cast to the appropriate
 * subclass type depending on the XDS class.
 */
typedef unsigned int vbi_xds_subclass;

/**
 * @brief XDS Packet.
 * A pointer to this structure is passed to the XDS demux callback.
 *
 * @note The structure may grow in the future.
 */
typedef struct {
	vbi_xds_class		xds_class;
	vbi_xds_subclass	xds_subclass;

	/** XDS packets have variable length 1 ... 32 bytes. */
	unsigned int		buffer_size;

	/**
	 * Packet data. Bit 7 (odd parity) is cleared,
	 * buffer[buffer_size] is 0.
	 */
	uint8_t			buffer[36];
} vbi_xds_packet;

/** @} */

extern void
_vbi_xds_packet_dump		(const vbi_xds_packet *	xp,
				 FILE *			fp);

/**
 * @addtogroup XDSDemux
 * @{
 */

/**
 * @brief XDS demultiplexer.
 *
 * The contents of this structure are private.
 * Call vbi_xds_demux_new() to allocate a XDS demultiplexer.
 */
typedef struct _vbi_xds_demux vbi_xds_demux;

/**
 * @param xd XDS demultiplexer context allocated with vbi_xds_demux_new().
 * @param user_data User data pointer given to vbi_xds_demux_new().
 * @param xp Pointer to the received XDS data packet.
 * 
 * The XDS demux calls a function of this type when an XDS packet
 * has been completely received, all bytes have correct parity and the
 * packet checksum is correct. Other packets are discarded.
 *
 * @returns
 * FALSE on error, will be returned by vbi_xds_demux_feed().
 */
typedef vbi_bool
vbi_xds_demux_cb		(vbi_xds_demux *	xd,
				 const vbi_xds_packet *	xp,
				 void *			user_data);

extern void
vbi_xds_demux_reset		(vbi_xds_demux *	xd);
extern vbi_bool
vbi_xds_demux_feed		(vbi_xds_demux *	xd,
				 const uint8_t		buffer[2]);
extern vbi_bool
vbi_xds_demux_feed_frame	(vbi_xds_demux *	xd,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines);
extern void
vbi_xds_demux_delete		(vbi_xds_demux *	xd);
extern vbi_xds_demux *
vbi_xds_demux_new		(vbi_xds_demux_cb *	callback,
				 void *			user_data)
  _vbi_alloc;

/** @} */

/* Private */

/**
 * @internal
 */
typedef struct {
	uint8_t			buffer[32];
	unsigned int		count;
	unsigned int		checksum;
} _vbi_xds_subpacket;

/**
 * @internal
 */
struct _vbi_xds_demux {
	_vbi_xds_subpacket	subpacket[VBI_XDS_MAX_CLASSES]
					 [VBI_XDS_MAX_SUBCLASSES];

	vbi_xds_packet		curr;
	_vbi_xds_subpacket *	curr_sp;

	vbi_xds_demux_cb *	callback;
	void *			user_data;
};

extern void
_vbi_xds_demux_destroy		(vbi_xds_demux *	xd);
extern vbi_bool
_vbi_xds_demux_init		(vbi_xds_demux *	xd,
				 vbi_xds_demux_cb *	callback,
				 void *			user_data);

VBI_END_DECLS

#endif /* __ZVBI_XDS_DEMUX_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
