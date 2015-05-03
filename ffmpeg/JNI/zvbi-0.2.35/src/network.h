/*
 *  libzvbi - Network identification
 *
 *  Copyright (C) 2004-2006 Michael H. Schimek
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

/* $Id: network.h,v 1.2 2009/03/04 21:42:09 mschimek Exp $ */

#ifndef __ZVBI_NETWORK_H__
#define __ZVBI_NETWORK_H__

VBI_BEGIN_DECLS

/* Public */

/**
 */
typedef enum {
	VBI_CNI_TYPE_NONE,
	VBI_CNI_TYPE_UNKNOWN = VBI_CNI_TYPE_NONE,

	/**
	 * Video Programming System (VPS) format, a PDC CNI, for
	 * example from vbi_decode_vps_cni(). Note VPS transmits only
	 * the 4 lsb of the country code (0xcnn).
	 *
	 * Example ZDF: 0xDC2.
	 */
	VBI_CNI_TYPE_VPS,

	/**
	 * Teletext packet 8/30 format 1, for example from
	 * vbi_decode_teletext_8301_cni(). The country code is stored
	 * in the MSB, the network code in the LSB (0xccnn).  Note
	 * these CNIs may use different country and network codes than
	 * the PDC CNIs.
	 *
	 * Example BBC 1: 0x447F, ZDF: 0x4902.
	 */
	VBI_CNI_TYPE_8301,

	/**
	 * Teletext packet 8/30 format 2 (PDC), for example from
	 * vbi_decode_teletext_8302_cni(). The country code is stored
	 * in the MSB, the network code in the LSB (0xccnn).
	 *
	 * Example BBC 1: 0x2C7F, ZDF: 0x1DC2.
	 */
	VBI_CNI_TYPE_8302,

	/**
	 * PDC Preselection method "A" format encoded on Teletext
	 * pages. This number consists of 2 hex digits for the
	 * country code and 3 bcd digits for the network code.
	 *
	 * Example ZDF: 0x1D102. (German PDC-A network codes 101 ... 163
	 * correspond to 8/30/2 codes 0xC1 ... 0xFF. Other countries may
	 * use different schemes.)
	 */
	VBI_CNI_TYPE_PDC_A,

	/**
	 * PDC Preselection method "B" format encoded in Teletext
	 * packet X/26 local enhancement data (0x3cnn). X/26 transmits
	 * only the 4 lsb of the country code and the 7 lsb of
	 * the network code. To avoid ambiguity these CNIs may not
	 * use the same country and network codes as other PDC CNIs.
	 *
	 * Example BBC 1: 0x3C7F.
	 */
	VBI_CNI_TYPE_PDC_B
} vbi_cni_type;

/* Private */

VBI_END_DECLS

#endif /* __ZVBI_NETWORK_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
