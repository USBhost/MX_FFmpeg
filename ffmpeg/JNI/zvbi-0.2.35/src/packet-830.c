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

/* $Id: packet-830.c,v 1.4 2013/07/10 11:37:18 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include "misc.h"
#include "bcd.h"		/* vbi_lbcd2bin() */
#include "hamm.h"		/* vbi_rev16p(), vbi_iham8() */
#include "packet-830.h"

/**
 * @addtogroup Packet830 Teletext Packet 8/30 Decoder
 * @ingroup LowDec
 * @brief Functions to decode Teletext packet 8/30 (ETS 300 706).
 *
 * Teletext pages are transmitted in packets numbered 0 to 31. Packet
 * 0 to 25 contain the text of the page, packet 26 to 29 additional
 * information like Fastext links. Packet 30 and 31 are reserved for
 * data transmissions unrelated to any page. Since each packet
 * contains a magazine number 1 to 8 (the first digit of the Teletext
 * page number) 16 logical channels can be distinguished. Packet 30
 * with magazine number 8 carries a Country and Network Identifier,
 * and either a local time (format 1) or PDC label (format 2).
 *
 * These are low level functions. See test/decode.c for a usage
 * example.
 *
 * The @a vbi_decoder module can decode a full Teletext signal and
 * provide the information transmitted in packet 8/30 as @c
 * VBI_EVENT_NETWORK, @c VBI_EVENT_NETWORK_ID, @c VBI_EVENT_LOCAL_TIME
 * and @c VBI_EVENT_PROG_ID. See examples/network.c and
 * examples/pdc1.c.
 */

/* Resources:
   http://pdc.ro.nu/jd-code.html
*/

/**
 * @param cni CNI of type VBI_CNI_TYPE_8301 will be stored here.
 * @param buffer Teletext packet as defined for @c VBI_SLICED_TELETEXT_B,
 *   i.e. 42 bytes without clock run-in and framing code.
 *
 * Decodes a Teletext packet 8/30 format 1 according to ETS 300 706
 * section 9.8.1, returning the contained 16 bit Country and Network
 * Identifier in @a *cni.
 *
 * @returns
 * Always @c TRUE, no error checking possible. It may be prudent to
 * wait for a second transmission of the received CNI to ensure
 * correct reception.
 *
 * @since 0.2.34
 */
vbi_bool
vbi_decode_teletext_8301_cni	(unsigned int *		cni,
				 const uint8_t		buffer[42])
{
	assert (NULL != cni);
	assert (NULL != buffer);

	*cni = vbi_rev16p (buffer + 9);

	return TRUE;
}

/* Should use vbi_bcd2bin() but that function is limited to four BCD
   digits in libzvbi 0.2 and cannot be changed for compatibility
   reasons. */
static int
bcd2bin				(int			bcd)
{
	unsigned int f;
	int bin = 0;

	for (f = 1; f < 100000; f = f * 10) {
		bin += (bcd & 15) * f;
		bcd >>= 4;
	}

	return bin;
}

/**
 * @param time The current time in seconds since 1970-01-01 00:00 UTC
 *   will be stored here.
 * @param seconds_east The offset of the local time of the intended
 *   audience of the program in seconds east of UTC will be stored here,
 *   including a daylight-saving time offset if daylight-saving is
 *   currently in effect in that time zone. To get the local time of the
 *   intended audience add @a seconds_east to @a time.
 * @param buffer Teletext packet as defined for @c VBI_SLICED_TELETEXT_B,
 *   i.e. 42 bytes without clock run-in and framing code.
 * 
 * Decodes a Teletext packet 8/30 format 1 according to ETS 300 706
 * section 9.8.1, returning the current time in the UTC time zone and
 * the time zone of the intended audience of the program.
 *
 * @returns
 * On error the function returns @c FALSE:
 * - The buffer contains uncorrectable errors or
 * - The time is not representable as a time_t.
 * In these cases @a *time and @a *seconds_east remain unchanged.
 *
 * @since 0.2.34
 */
vbi_bool
vbi_decode_teletext_8301_local_time
				(time_t *		time,
				 int *			seconds_east,
				 const uint8_t		buffer[42])
{
	int64_t mjd;
	int64_t utc;
	int64_t t;
	int bcd;
	int field;
	int offset;

	assert (NULL != time);
	assert (NULL != seconds_east);
	assert (NULL != buffer);

	/* Modified Julian Date. */

	bcd = (+ ((buffer[12] & 15) << 16)
	       +  (buffer[13] << 8)
	       +   buffer[14]
	       - 0x11111);
	if (unlikely (!vbi_is_bcd (bcd))) {
		errno = 0;
		return FALSE;
	}

	mjd = bcd2bin (bcd);

	/* UTC time. */

	bcd = (+ (buffer[15] << 16)
	       + (buffer[16] << 8)
	       +  buffer[17]
	       - 0x111111);
	if (unlikely (!vbi_is_bcd (bcd))) {
		errno = 0;
		return FALSE;
	}

	utc = (bcd & 15) + ((bcd >> 4) & 15) * 10;
	if (unlikely (utc > 60)) {
		errno = 0;
		return FALSE;
	}

	field = ((bcd >> 8) & 15) + ((bcd >> 12) & 15) * 10;
	if (unlikely (field >= 60)) {
		errno = 0;
		return FALSE;
	}
	utc += field * 60;

	field = ((bcd >> 16) & 15) + (bcd >> 20) * 10;
	if (unlikely (field >= 24)) {
		errno = 0;
		return FALSE;
	}
	utc += field * 3600;

	/* Local time offset in seconds east of UTC. */

	offset = (buffer[11] & 0x3E) * (15 * 60);
	if (buffer[11] & 0x40)
		offset = -offset;

	t = (mjd - 40587) * 86400 + utc;
	if (t < TIME_MIN || t > TIME_MAX) {
		errno = EOVERFLOW;
		return FALSE;
	}


	*time = t;

	*seconds_east = offset;

	return TRUE;
}

/**
 * @param cni CNI of type VBI_CNI_TYPE_8302 will be stored here.
 * @param buffer Teletext packet as defined for @c VBI_SLICED_TELETEXT_B,
 *   i.e. 42 bytes without clock run-in and framing code.
 *
 * Decodes a Teletext packet 8/30 format 2 according to ETS 300 706
 * section 9.8.2, returning the contained 16 bit Country and Network
 * Identifier in @a *cni.
 *
 * @returns
 * @c FALSE if the buffer contains uncorrectable errors. In this case
 * @a *cni remains unchanged.
 *
 * @since 0.2.34
 */
vbi_bool
vbi_decode_teletext_8302_cni	(unsigned int *		cni,
				 const uint8_t		buffer[42])
{
	int b[13];

	assert (NULL != cni);
	assert (NULL != buffer);

	b[ 7] = vbi_unham16p (buffer + 10);
	b[ 8] = vbi_unham16p (buffer + 12);
	b[10] = vbi_unham16p (buffer + 16);
	b[11] = vbi_unham16p (buffer + 18);

	if (unlikely ((b[7] | b[8] | b[10] | b[11]) < 0))
		return FALSE;

	b[ 7] = vbi_rev8 (b[ 7]);
	b[ 8] = vbi_rev8 (b[ 8]);
	b[10] = vbi_rev8 (b[10]);
	b[11] = vbi_rev8 (b[11]);

	*cni = (+ ((b[ 7] & 0x0F) << 12)
		+ ((b[10] & 0x03) << 10)
		+ ((b[11] & 0xC0) << 2)
		+  (b[ 8] & 0xC0)
		+  (b[11] & 0x3F));

	return TRUE;
}

/**
 * @param pid PDC program ID will be stored here.
 * @param buffer Teletext packet as defined for @c VBI_SLICED_TELETEXT_B,
 *   i.e. 42 bytes without clock run-in and framing code.
 * 
 * Decodes a Teletext packet 8/30 format 2 according to ETS 300 231,
 * and stores the contained PDC recording-control data in @a *pid.
 *
 * @returns
 * @c FALSE if the buffer contains uncorrectable errors or invalid
 * data. In this case @a *pid remains unchanged.
 *
 * @since 0.2.34
 */
vbi_bool
vbi_decode_teletext_8302_pdc	(vbi_program_id *	pid,
				 const uint8_t		buffer[42])
{
	uint8_t b[13];
	unsigned int i;
	int error;

	assert (NULL != pid);
	assert (NULL != buffer);

	error = vbi_unham8 (buffer[9]);
	b[ 6] = vbi_rev8 (error) >> 4;

	for (i = 7; i <= 12; ++i) {
		int t;

		t = vbi_unham16p (buffer + i * 2 - 4);
		error |= t;
		b[i] = vbi_rev8 (t);
	}

	if (unlikely (error < 0))
		return FALSE;

	CLEAR (*pid);

	pid->channel	= VBI_PID_CHANNEL_LCI_0 + ((b[6] >> 2) & 3);

	pid->cni_type	= VBI_CNI_TYPE_8302;

	pid->cni	= (+ ((b[ 7] & 0x0F) << 12)
			   + ((b[10] & 0x03) << 10)
			   + ((b[11] & 0xC0) << 2)
			   +  (b[ 8] & 0xC0)
			   +  (b[11] & 0x3F));

	pid->pil	= (+ ((b[ 8] & 0x3F) << 14)
			   +  (b[ 9]         << 6)
			   +  (b[10]         >> 2));

	pid->luf	= (b[6] >> 1) & 1;
	pid->mi		= (b[7] >> 5) & 1;
	pid->prf	= (b[6] >> 0) & 1;

	pid->pcs_audio	= (b[7] >> 6) & 3;

	pid->pty	= b[12];

	return TRUE;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
