/*
 *  libzvbi -- DVB VBI definitions
 *
 *  Copyright (C) 2004 Michael H. Schimek
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

/* $Id: dvb.h,v 1.4 2008/02/19 00:35:15 mschimek Exp $ */

#ifndef DVB_H
#define DVB_H

/**
 * @internal
 * ISO 13818-1 section 2.4.3.7, Table 2-19 stream_id assignments.
 */
#define PRIVATE_STREAM_1 0xBD

/**
 * @internal
 * EN 301 775 section 4.3.2, Table 2 data_identifier.
 */
typedef enum {
	/* 0x00 ... 0x0F reserved. */

	/* Teletext combined with VPS and/or WSS and/or CC
	   and/or VBI sample data. (EN 300 472, 301 775) */
	DATA_ID_EBU_TELETEXT_BEGIN		= 0x10,
	DATA_ID_EBU_TELETEXT_END		= 0x20,

	/* 0x20 ... 0x7F reserved. */

	DATA_ID_USER_DEFINED1_BEGIN		= 0x80,
	DATA_ID_USER_DEFINED2_END		= 0x99,

	/* Teletext and/or VPS and/or WSS and/or CC and/or
	   VBI sample data. (EN 301 775) */
	DATA_ID_EBU_DATA_BEGIN			= 0x99,
	DATA_ID_EBU_DATA_END			= 0x9C,

	/* 0x9C ... 0xFF reserved. */
} data_identifier;

/**
 * @internal
 * EN 301 775 section 4.3.2, Table 3 data_unit_id.
 */
typedef enum {
	/* 0x00 ... 0x01 reserved. */

	DATA_UNIT_EBU_TELETEXT_NON_SUBTITLE	= 0x02,
	DATA_UNIT_EBU_TELETEXT_SUBTITLE,

	/* 0x04 ... 0x7F reserved. */

	DATA_UNIT_USER_DEFINED1_BEGIN		= 0x80,
	DATA_UNIT_USER_DEFINED1_END		= 0xC0,

	/* Libzvbi private, not defined in EN 301 775. */
	DATA_UNIT_ZVBI_WSS_CPR1204		= 0xB4,
	DATA_UNIT_ZVBI_CLOSED_CAPTION_525,
	DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525,

	DATA_UNIT_EBU_TELETEXT_INVERTED		= 0xC0,

	/* EN 301 775 Table 1 says this is Teletext data,
	   Table 3 says reserved. */
	DATA_UNIT_C1				= 0xC1,

	/* 0xC2 reserved. */

	DATA_UNIT_VPS				= 0xC3,
	DATA_UNIT_WSS,
	DATA_UNIT_CLOSED_CAPTION,
	DATA_UNIT_MONOCHROME_SAMPLES,

	DATA_UNIT_USER_DEFINED2_BEGIN		= 0xC7,
	DATA_UNIT_USER_DEFINED2_END		= 0xFE,

	DATA_UNIT_STUFFING			= 0xFF
} data_unit_id;

#endif /* DVB_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
