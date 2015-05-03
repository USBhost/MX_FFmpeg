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

/* $Id: dvb_mux.c,v 1.15 2008/02/19 00:35:15 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>

#include "misc.h"
#include "hamm.h"		/* vbi_rev8() */
#include "dvb.h"
#include "dvb_mux.h"
#include "version.h"

/**
 * @addtogroup DVBMux DVB VBI multiplexer
 * @ingroup LowDec
 * @brief Converting VBI data to a DVB PES or TS stream.
 *
 * These functions convert raw and/or sliced VBI data to a DVB Packetized
 * Elementary Stream or Transport Stream as defined in EN 300 472 "Digital
 * Video Broadcasting (DVB); Specification for conveying ITU-R System B
 * Teletext in DVB bitstreams" and EN 301 775 "Digital Video Broadcasting
 * (DVB); Specification for the carriage of Vertical Blanking Information
 * (VBI) data in DVB bitstreams".
 *
 * Note EN 300 468 "Digital Video Broadcasting (DVB); Specification for
 * Service Information (SI) in DVB systems" defines another method to
 * transmit VPS data in DVB streams. Libzvbi does not provide functions
 * to generate SI tables but the vbi_encode_dvb_pdc_descriptor() function
 * is available to convert a VPS PIL to a PDC descriptor.
 */

#if 3 == VBI_VERSION_MINOR
#  define sp_sample_format sample_format
#  define sp_samples_per_line samples_per_line
#else
#  define sp_sample_format sampling_format
   /* Has no samples_per_line field yet. */
#  define sp_samples_per_line bytes_per_line
#endif

/* Preliminary. */
enum {
	VBI_ERR_BUFFER_OVERFLOW = 0x7081800,
	VBI_ERR_RAW_BUFFER_OVERFLOW,
	VBI_ERR_LINE_NUMBER,
	VBI_ERR_LINE_ORDER,
	VBI_ERR_INVALID_SERVICE,
	VBI_ERR_SAMPLE_NUMBER,
	VBI_ERR_AMBIGUOUS_VIDEOSTD,
	VBI_ERR_RAW_DATA_INTERRUPTION,
	VBI_ERR_NO_RAW_DATA,
	VBI_ERR_NO_SLICED_DATA,
	VBI_ERR_SAMPLING_PAR,
};

/* BT.601-5 table 2: Luminance sampling frequency is 13.5 MHz. For
   525/60 systems we have number of luminance samples per total line
   858, number of luminance samples per active line 720, distance
   from end of digital active line to 0H 16 luminance clock periods.
   For 625/50 systems the numbers are 864, 720 and 12 respectively. */
/* vbi_sampling_par->offset just counts samples since 0H. */
static const unsigned int BT601_525_OFFSET = 858 - 16 - 720;
static const unsigned int BT601_625_OFFSET = 864 - 12 - 720;

/* Brief note about the alignment of data units in VBI packets:

   All TS packets are 188 bytes long. VBI TS packets must not contain
   an adaption_field, leaving 184 bytes of TS payload.

   The PES packet size including all header bytes must be a multiple
   of 184 bytes, so that the PES packet starts and ends at a TS
   packet boundary. ("PES_packet_length = (N x 184) - 6", as the field
   does not count its own size or that of the preceding
   packet_start_code_prefix and stream_id fields.)

   The PES packet header must have a size of 45 bytes.
   ("PES_header_length = 0x24", again not counting itself or the
   preceding fields.) It is followed by the data_identifier byte,
   leaving (N * 4 - 1) * 46 bytes of PES payload for data units.

   When the data_identifier is in range 0x10 to 0x1F inclusive, each
   data unit must have a size of 46 bytes ("data_unit_length = 0x2C")
   for compatibility with EN 300 472. So the data units also end
   at a PES and TS packet boundary. */

/* packet_start_code_prefix [24],
   stream_id [8],
   PES_packet_length [16] */
static const unsigned int MAX_PES_PACKET_SIZE =
	6 + 65535 - (6 + 65535) % 184;

/**
 * @internal
 * @param p Must point to the output buffer where the stuffing data
 *   units will be stored.
 * @param p_left Must contain the number of bytes available in the
 *   @a p buffer.
 * @param last_du_size Size of the data unit immediately before @a
 *   p (its data_unit_length plus two). @a last_du_size must be
 *   < 257. It can be zero if there is no preceding data unit.
 *   Unless @a p_left is zero, @a p_left + @a last_du_size must be >= 2.
 * @param fixed_length If @c TRUE, all data units will have a size
 *   of 46 bytes, @a p_left must be a multiple of 46 and
 *   @a last_du_size is ignored.
 *
 * Fills up a buffer with stuffing data units. If @a p_left is
 * too small to contain even one data unit, the function may append
 * a stuffing byte to the data unit immediately before @a p,
 * incrementing its length by one.
 */
static void
encode_stuffing			(uint8_t *		p,
				 unsigned int		p_left,
				 unsigned int		last_du_size,
				 vbi_bool		fixed_length)
{
	unsigned int du_size;

	/* data_unit_id: DATA_UNIT_STUFFING (0xFF),
	   stuffing byte: 0xFF */
	memset (p, 0xFF, p_left);

	/* EN 301 775 section 4.4.2 and table 1. */
	du_size = fixed_length ? 46 : 257;

	while (p_left >= du_size) {
		/* data_unit_length [8] */
		p[1] = du_size - 2;
		last_du_size = du_size;

		p += du_size;
		p_left -= du_size;
	}

	if (p_left > 0) {
		assert (!fixed_length);

		if (p_left >= 2) {
			/* data_unit_length [8] */
			p[1] = p_left - 2;
		} else {
			/* Assumed the caller enforced a minimum
			   buffer size of two bytes. */
			assert (last_du_size >= 2);

			if (257 == last_du_size) {
				p[(long) 1 - 257] = 256 - 2;
				p[(long) 1 - 1] = 2 - 2;
			} else {
				/* Append a stuffing byte 0xFF to the
				   previous data unit. */
				p[(long) 1 - last_du_size] =
					last_du_size - 1;
			}
		}
	}
}

/**
 * @internal
 * @param packet @a *packet must point at the output buffer where the
 *   data units will be stored. Initially this should be the position
 *   of the first data unit in a PES packet, immediately after the
 *   data_indentifier byte. @a *packet will be incremented by the
 *   cumulative size of the successfully stored data units.
 * @param p_left The number of bytes available in the @a packet
 *   buffer.
 * @param last_du_size The total size of the last data unit stored in
 *   the @a packet buffer, i.e. its data_unit_length plus two, will be
 *   stored here. The returned value may be zero if @a p_left is too
 *   small to contain even one data unit.
 * @param sliced @a *sliced must point at the sliced VBI data to be
 *   converted. All data in this array must belong to the same video
 *   frame. The pointer will be advanced by the number of
 *   successfully converted structures. On failure it will point at
 *   the offending vbi_sliced structure.
 * @param s_left The number of vbi_sliced structures in the @a sliced
 *   array.
 * @param service_mask Only data services in this set will be
 *   encoded. Other data services in the @a *sliced array will be
 *   discarded without further checks. Create a set by ORing
 *   @c VBI_SLICED_ values.
 * @param fixed_length If @c TRUE, all data units will have a size
 *   of 46 bytes.
 *
 * Converts the sliced VBI data in the @a sliced array to VBI data
 * units as defined in EN 300 472 and EN 301 775 and stores them
 * in the @a packet buffer. The function will not fill up the
 * buffer with stuffing bytes. Call the encode_stuffing() function
 * for this purpose.
 *
 * @returns
 * - @c 0 Success. If @a p_left is too small, @a *sliced will point
 *   at the remaining unconverted structures.
 * - @c VBI_ERR_LINE_ORDER
 *   The @a sliced array is not sorted by ascending line number,
 *   except for elements with line number zero.
 * - @c VBI_ERR_INVALID_SERVICE
 *   Only the following data services can be encoded:
 *   - @c VBI_SLICED_TELETEXT_B on lines 7 to 22 and 320 to 335
 *     inclusive, or with line number 0 (undefined). All Teletext
 *     lines will be encoded with data_unit_id 0x02 ("EBU Teletext
 *     non-subtitle data").
 *   - @c VBI_SLICED_VPS on line 16.
 *   - @c VBI_SLICED_CAPTION_625 on line 22.
 *   - @c VBI_SLICED_WSS_625 on line 23.
 * - @c VBI_ERR_LINE_NUMBER
 *   A vbi_sliced structure contains a line number outside the valid
 *   range specified above.
 *
 * All errors are recoverable, just call the function again with
 * updated @a p_left and @a s_left values, possibly after skipping
 * the offending sliced VBI data structure.
 *
 * @bug
 * strict = FALSE is untested.
 */
static int
insert_sliced_data_units	(uint8_t **		packet,
				 unsigned int		p_left,
				 unsigned int *		last_du_size,
				 const vbi_sliced **	sliced,
				 unsigned int		s_left,
				 vbi_service_set	service_mask,
				 vbi_bool		fixed_length)
{
	static const vbi_bool strict = TRUE;
	uint8_t *p;
	const vbi_sliced *s;
	unsigned int last_line;

	p = *packet;
	s = *sliced;

	last_line = 0;
	*last_du_size = 0;

	for (; s_left > 0; ++s, --s_left) {
		const unsigned int f2_start = 313;
		unsigned int du_size;
		unsigned int line;
		unsigned int i;

		/* Also skips VBI_SLICED_NONE (0). */
		if (0 == (s->id & service_mask))
			continue;

		/* EN 301 775 section 4.5.2 (Teletext data unit):
		   "Within a field, the line_offset numbering shall
		   follow a progressive incremental order except for
		   the undefined line_offset value 0." */
		if (s->line > 0) {
			/* EN 301 775 section 4.1: "[...] lines shall
			   appear in the bitstream in the same order,
			   as they will appear in the VBI;" "a certain
			   VBI line may never be coded twice within
			   a frame" */
			if (unlikely (s->line <= last_line)) {
				*packet = p;
				*sliced = s;

				return VBI_ERR_LINE_ORDER;
			}

			last_line = s->line;
		}

		line = s->line;

		switch (s->id) {
		case VBI_SLICED_TELETEXT_B_L10_625:
		case VBI_SLICED_TELETEXT_B_L25_625:
		case VBI_SLICED_TELETEXT_B_625:
			du_size = 2 + 2 + 42;

			/* EN 301 775 section 4.5.2: Can be zero
			   (undefined). "The toggling of the field_parity
			   flag indicates a new field." */
			if (0 == line || !strict)
				break;

			if (line >= f2_start)
				line -= f2_start;

			/* EN 301 775 section 4.5.2. */
			if (unlikely (line - 7 > 22 - 7))
				goto bad_line;

			line = s->line;
			break;

		case VBI_SLICED_VPS:
			du_size = 2 + 1 + 13;
			/* EN 301 775 section 4.6.2: Must be
			   line 16 on first field. */
			if (unlikely (16 != line)) {
				/* We prohibit line 0 here because it
				   may confuse decoders too much, even
				   more so if the sliced array contains
				   multiple elements with line 0. */
				if (strict || 0 == line)
					goto bad_line;
			}
			break;

		case VBI_SLICED_WSS_625:
			du_size = 2 + 1 + 2;
			/* EN 301 775 section 4.7.2: Must be
			   line 23 on the first field. */
			if (unlikely (23 != line)) {
				if (strict || 0 == line)
					goto bad_line;
			}
			break;
			
		case VBI_SLICED_CAPTION_625:
		case VBI_SLICED_CAPTION_625_F1:
			du_size = 2 + 1 + 2;
			/* EN 301 775 section 4.8.2: Must be
			   line 21 on the first field. */
			if (unlikely (21 != line)) {
				if (strict || 0 == line)
					goto bad_line;
			}
			break;

#if 0 /* unused, untested */
		case VBI_SLICED_VPS | VBI_SLICED_VPS_F2:
		case VBI_SLICED_VPS_F2:
			/* Libzvbi extension. */
			du_size = 2 + 1 + 13;
			if (strict || 0 == line)
				goto bad_service;
			break;

		case VBI_SLICED_CAPTION_625_F2:
			du_size = 2 + 1 + 2;
			if (strict || 0 == line)
				goto bad_service;
			break;
			
		case VBI_SLICED_CAPTION_525_F1:
		case VBI_SLICED_CAPTION_525_F2:
		case VBI_SLICED_CAPTION_525:
			/* Libzvbi extension. */
			du_size = 2 + 1 + 2;
			if (strict || 0 == line)
				goto bad_service;
			break;
			
		case VBI_SLICED_WSS_CPR1204:
			/* Libzvbi extension. */
			du_size = 2 + 1 + 3;
			if (strict)
				goto bad_service;
			/* FIXME line = ? */
			break;
#endif /* 0 */
		default:
			goto bad_service;

		bad_service:
			*packet = p;
			*sliced = s;
			
			return VBI_ERR_INVALID_SERVICE;
		}

		if (fixed_length) {
			/* EN 301 775 section 4.4.2: "If the
			   data_identifier has a value between 0x10
			   and 0x1F inclusive, [the data_unit_length
			   field] shall always be set to 0x2C." */
			du_size = 2 + 0x2C;
		}

		if (du_size > p_left) {
			/* Data units must not cross PES packet
			   boundaries, as is evident from
			   EN 301 775 table 1. */

			break;
		}

		/* EN 301 775 table 1: N * stuffing_byte. */
		if (fixed_length)
			memset (p, 0xFF, du_size);

		if (0 == line) {
			/* EN 301 775 section 4.5.2 (Teletext data
			   unit): Undefined line. */
			if (last_line >= f2_start) {
				/* Second field. */
				p[2] = (3 << 6) + (0 << 5);
			} else {
				/* First field. */
				p[2] = (3 << 6) + (1 << 5);
			}
		} else if (line < 32) {
			/* Line 1 ... 31 of the first field. */
			p[2] = (3 << 6) + (1 << 5) + line;
		} else if (line < f2_start) {
			goto bad_line;
		} else if (line < f2_start + 32) {
			/* reserved [2] = '11',
			   field_parity = '0' (second field),
			   line_offset [5]. */
			p[2] = (3 << 6) + (0 << 5) + line - f2_start;
		} else {
		bad_line:
			*packet = p;
			*sliced = s;

			return VBI_ERR_LINE_NUMBER;
		}

		/* data_unit_length [8] */
		p[1] = du_size - 2;

		if (s->id & VBI_SLICED_TELETEXT_B_625) {
			/* data_unit_id [8], data_unit_length [8],
			   reserved [2], field_parity, line_offset [5],
			   framing_code [8],
			   magazine_and_packet_address [16],
			   data_block [320] (msb is first bit in VBI) */
			p[0] = DATA_UNIT_EBU_TELETEXT_NON_SUBTITLE;
			p[3] = 0xE4; /* vbi_rev8 (0x27); */

			for (i = 0; i < 42; ++i)
				p[4 + i] = vbi_rev8 (s->data[i]);
		} else if (s->id & (VBI_SLICED_VPS | VBI_SLICED_VPS_F2)) {
			/* data_unit_id [8], data_unit_length [8],
			   reserved [2], field_parity, line_offset [5],
			   vps_data_block [104] (msb first) */
			p[0] = DATA_UNIT_VPS;

			/* EN 301 775 requires that data bits appear
			   in the stream in the same order as they
			   would in the VBI. VPS is msb first
			   transmitted so we need not reflect the
			   bits here. */
			for (i = 0; i < 13; ++i)
				p[3 + i] = s->data[i];
		} else if (s->id & VBI_SLICED_WSS_625) {
			/* data_unit_id [8], data_unit_length [8],
			   reserved[2], field_parity, line_offset [5],
			   wss_data_block[14] (msb first),
			   reserved[2] '11' */
			p[0] = DATA_UNIT_WSS;
			p[3] = vbi_rev8 (s->data[0]);
			p[4] = vbi_rev8 (s->data[1]) | 3;
		} else if (s->id & VBI_SLICED_CAPTION_625) {
			/* data_unit_id [8], data_unit_length [8],
			   reserved[2], field_parity, line_offset [5],
			   data_block[16] (msb first) */
			p[0] = DATA_UNIT_CLOSED_CAPTION;
			p[3] = vbi_rev8 (s->data[0]);
			p[4] = vbi_rev8 (s->data[1]);

#if 0 /* unused, untested */
		} else if (s->id & VBI_SLICED_CAPTION_525) {
			/* data_unit_id [8], data_unit_length [8],
			   reserved [2], field_parity, line_offset [5],
			   data_block [16] (msb first) */
			p[0] = DATA_UNIT_ZVBI_CLOSED_CAPTION_525;
			p[3] = vbi_rev8 (s->data[0]);
			p[4] = vbi_rev8 (s->data[1]);
		} else if (s->id & VBI_SLICED_WSS_CPR1204) {
			/* data_unit_id [8], data_unit_length [8],
			   reserved[2], field_parity, line_offset [5],
			   wss_data_block[20] (msb first),
			   reserved[4] '1111' */
			p[0] = DATA_UNIT_ZVBI_WSS_CPR1204;
			p[3] = s->data[0];
			p[4] = s->data[1];
			p[5] = s->data[2] | 0xF;
#endif /* 0 */
		} else {
			assert (0);
		}

		p += du_size;
		p_left -= du_size;

		*last_du_size = du_size;
	}

	*packet = p;
	*sliced = s;

	return 0; /* success */
}

_vbi_inline vbi_bool
fixed_length_format		(unsigned int		data_identifier)
{
	/* EN 301 775 section 4.4.2: If the data_identifier has a
	   value between 0x10 and 0x1F inclusive, [data_unit_length]
	   shall always be set to 0x2C. (Compatibility with EN 300 472.) */
	return (0x10 == (data_identifier & ~0xF));
}

/**
 * @param packet @a *packet must point at the output buffer where the
 *   data units will be stored. Initially this should be the position
 *   of the first data unit in a PES packet, immediately after the
 *   data_indentifier byte. @a *packet will be incremented by the
 *   cumulative size of the successfully stored data units.
 * @param packet_left @a *packet_left must contain the number of bytes
 *   available in the @a packet buffer. It will be decremented by
 *   the cumulative size of the successfully stored data units.
 * @param sliced @a *sliced shall point at the sliced VBI data to be
 *   converted, or it can be a @c NULL pointer. All data in this
 *   array must belong to the same video frame. The pointer will be
 *   advanced by the number of successfully converted structures.
 *   On failure it will point at the offending vbi_sliced structure.
 * @param sliced_left @a *sliced_left shall contain the number of
 *   vbi_sliced structures in the @a sliced array, or it can be
 *   zero. It will be decremented by the number of successfully
 *   converted structures.
 * @param service_mask Only data services in this set will be
 *   encoded. Other data services in the @a *sliced array will be
 *   discarded without further checks. Create a set by ORing
 *   @c VBI_SLICED_ values.
 * @param data_identifier When the @a data_indentifier lies in range
 *   0x10 to 0x1F inclusive, the encoded data units will be padded to
 *   data_unit_length 0x2C for compatibility with EN 300 472
 *   compliant decoders. The @a data_identifier itself will NOT be
 *   stored in the output buffer.
 * @param stuffing If TRUE, and space remains in the output buffer
 *   after all data has been successfully converted, or when @a *sliced
 *   is @c NULL or @a *sliced_left is zero, the function fills
 *   the buffer up with stuffing data units.
 *
 * Converts the sliced VBI data in the @a sliced array to VBI data
 * units as defined in EN 300 472 and EN 301 775 and stores them
 * in the @a packet buffer.
 *
 * @returns
 * @c FALSE on failure.
 * - @a *packet is @c NULL or @a *packet_left is less than two
 *   (the minimum data unit size is two bytes). The output buffer
 *   remains unchanged in this case.
 * - The @a data_identifier is in range 0x10 to 0x1F inclusive and
 *   @a *packet_left is not a multiple of 46. The output buffer
 *   remains unchanged in this case.
 * - The @a sliced array is not sorted by ascending line number,
 *   except for elements with line number 0 (undefined).
 * - Only the following data services can be encoded:
 *   - @c VBI_SLICED_TELETEXT_B on lines 7 to 22 and 320 to 335
 *     inclusive, or with line number 0 (undefined). All Teletext
 *     lines will be encoded with data_unit_id 0x02 ("EBU Teletext
 *     non-subtitle data").
 *   - @c VBI_SLICED_VPS on line 16.
 *   - @c VBI_SLICED_CAPTION_625 on line 22.
 *   - @c VBI_SLICED_WSS_625 on line 23.
 * - A vbi_sliced structure contains a line number outside the valid
 *   range specified above.
 *
 * All errors are recoverable. Just call the function again, possibly
 * after skipping the offending sliced VBI data structure, to continue
 * where it left off. Note @a *packet_left must be >= 2 (or a multiple
 * of 46) in each call.
 *
 * @note
 * According to EN 300 472 and EN 301 775 all lines stored in one PES
 * packet must belong to the same video frame (but the data of one frame
 * may be transmitted in several successive PES packets). They must be
 * encoded in the same order as they would be transmitted in the VBI, no
 * line more than once. The function cannot enforce this if multiple calls
 * are necessary to encode all data.
 *
 * @since 0.2.26
 */
vbi_bool
vbi_dvb_multiplex_sliced	(uint8_t **		packet,
				 unsigned int *		packet_left,
				 const vbi_sliced **	sliced,
				 unsigned int *		sliced_left,
				 vbi_service_set	service_mask,
				 unsigned int		data_identifier,
				 vbi_bool		stuffing)
{
	uint8_t *p;
	const vbi_sliced *s;
	unsigned int p_left;
	unsigned int s_left;
	unsigned int last_du_size;
	vbi_bool fixed_length;
	int err;

	assert (NULL != packet);
	assert (NULL != sliced);
	assert (NULL != packet_left);
	assert (NULL != sliced_left);

	p = *packet;
	p_left = *packet_left;

	if (unlikely (NULL == p || p_left < 2)) {
		/* errno = VBI_ERR_BUFFER_OVERFLOW; */
		return FALSE;
	}

	s = *sliced;
	s_left = *sliced_left;

	if (NULL == s)
		s_left = 0;

	fixed_length = fixed_length_format (data_identifier);

	if (fixed_length && unlikely ((p_left % 46) > 0)) {
		/* errno = VBI_ERR_BUFFER_OVERFLOW; */
		return FALSE;
	}

	err = insert_sliced_data_units (packet, p_left,
					&last_du_size,
					sliced, s_left,
					service_mask,
					fixed_length);

	*packet_left -= *packet - p;
	*sliced_left -= *sliced - s;

	if (unlikely (err > 0)) {
		/* errno = err; */
		return FALSE;
	}

	if (stuffing) {
		encode_stuffing (*packet, *packet_left,
				 last_du_size, fixed_length);
		*packet += *packet_left;
		*packet_left = 0;
	}

	return TRUE;
}

/**
 * @internal
 * @param packet @a *packet must point at the output buffer where the
 *   data units will be stored. Initially this should be the position
 *   of the first data unit in a PES packet, immediately after the
 *   data_indentifier byte. @a *packet will be incremented by the
 *   cumulative size of the successfully stored data units.
 * @param p_left This is the number of bytes available in the @a packet
 *   buffer.
 * @param last_du_size The size of the last data unit stored in the
 *   @a packet buffer, i.e. its data_unit_length plus two, will be
 *   stored here. The returned value may be zero if @a p_left is too
 *   small to contain even one data unit.
 * @param raw @a *raw must point at the raw VBI data to be converted,
 *   namely luminance samples as defined in ITU-R BT.601 with 8 bits
 *   per sample. @a *raw will be incremented by the number of
 *   successfully converted samples.
 * @param r_left The number of samples left to be converted in the
 *   @a raw buffer.
 * @param fixed_length If @c TRUE, all data units will have a size
 *   of 46 bytes.
 * @param videostd_set The @a line parameter will be interpreted
 *   according to this set of video standards. It must not change
 *   until all samples have been encoded. In libzvbi 0.2.x only one
 *   of two values are permitted: VBI_VIDEOSTD_SET_625_50 or
 *   VBI_VIDEOSTD_SET_525_60.
 * @param line The ITU-R line number to be encoded in the data units.
 *   It must not change until all samples have been encoded.
 * @param first_pixel_position The horizontal offset where decoders
 *   shall insert the first sample in the VBI, counting samples from
 *   the start of the digital active line as defined in ITU-R BT.601.
 *   Usually @a first_pixel_position is zero and @a n_pixels_total is
 *   720. @a first_pixel_position + @a n_pixels_total must not be greater
 *   than 720. This parameter must not change until all samples have
 *   been encoded.
 * @param n_pixels_total Total size of the @a raw buffer in bytes,
 *   and the total number of samples to be encoded. Initially this
 *   value must be equal to @a r_left, and it must not change until
 *   all samples have been encoded.
 * @param stuffing Leave room for stuffing at the end of the output
 *   buffer.
 *
 * Converts one line of raw VBI samples to one or more "monochrome
 * 4:2:2 samples" data units as defined in EN 301 775, and stores
 * them in the @a packet buffer. The function will not fill up the
 * buffer with stuffing bytes. Call the encode_stuffing() function
 * for this purpose.
 *
 * @returns
 * - @c 0 Success. If @a p_left is too small, @a *sliced will point
 *   at the remaining unconverted samples.
 * - @c VBI_ERR_AMBIGUOUS_VIDEOSTD
 *   The @a videostd_set is ambiguous.
 * - @c VBI_ERR_LINE_NUMBER
 *   The @a line parameter is outside the valid range, that is 7 to
 *   23 and 270 to 286 for 525 line standards, 7 to 23 and 320 to 336
 *   for 625 line standards. All numbers inclusive.
 * - @c VBI_ERR_SAMPLE_NUMBER
 *   @a r_left is greater than @a n_pixels_total or
 *   @a first_pixel_position + @a n_pixels_total is greater than 720.
 *
 * The output buffer remains unchanged on all errors.
 *
 * @since 0.2.26
 */
static int
insert_raw_data_units		(uint8_t **		packet,
				 unsigned int		p_left,
				 unsigned int *		last_du_size,
				 const uint8_t **	raw,
				 unsigned int		r_left,
				 vbi_bool		fixed_length,
				 vbi_videostd_set	videostd_set,
				 unsigned int		line,
				 unsigned int		first_pixel_position,
				 unsigned int		n_pixels_total,
				 vbi_bool		stuffing)
{
	uint8_t *p;
	const uint8_t *r;
	unsigned int crit_p_left;
	unsigned int min_du_size;
	unsigned int f2_start;
	unsigned int lofp;

	p = *packet;

	/* data_unit_id [8],
	   data_unit_length [8],
	   first_segment_flag,
	   last_segment_flag,
	   field_parity,
	   line_offset [5],
	   first_pixel_position [16],
	   n_pixels [8],
	   Y_value [n_pixels * 8] */
	min_du_size = 2 + 4 + 1;

	/* One byte left in the output buffer is too small for a new
	   stuffing data unit, maximum data_unit_length is too big to
	   append a stuffing byte. */
	crit_p_left = stuffing? 2 + 4 + 251 + 1 : 0;

	if (fixed_length) {
		/* EN 301 775 section 4.4.2. */
		min_du_size = 2 + 0x2C;
	}

	if (videostd_set & VBI_VIDEOSTD_SET_525_60) {
		if (unlikely (videostd_set & VBI_VIDEOSTD_SET_625_50))
			return VBI_ERR_AMBIGUOUS_VIDEOSTD;
		f2_start = 263;
	} else if (likely (videostd_set & VBI_VIDEOSTD_SET_625_50)) {
		f2_start = 313;
	} else {
		return VBI_ERR_AMBIGUOUS_VIDEOSTD;
	}		

	r = *raw;

	{
		unsigned int end = first_pixel_position + n_pixels_total;

		if (unlikely (r_left > n_pixels_total
			      || end > 720
			      || end < n_pixels_total /* overflow */)) {
			return VBI_ERR_SAMPLE_NUMBER;
		}
	}

	/* first_segment_flag,
	   last_segment_flag,
	   field_parity = '1' (first field),
	   line_offset [5] */
	lofp = (1 << 5);

	if (line >= f2_start) {
		line -= f2_start;
		/* Second field. */
		lofp = 0;
	}

	/* EN 301 775 table 12. */
	if (unlikely ((unsigned int) line - 7 > 23 - 7)) {
		return VBI_ERR_LINE_NUMBER;
	}

	lofp += line;

	first_pixel_position += n_pixels_total - r_left;

	*last_du_size = 0;

	while (r_left > 0) {
		unsigned int n_pixels;

		if (min_du_size > p_left) {
			/* Data units must not cross PES packet
			   boundaries, as is evident from
			   EN 301 775 table 1. */

			break;
		}

		if (fixed_length) {
			n_pixels = MIN (r_left, (unsigned int)(0x2C - 4));

			/* data_unit_id [8],
			   data_unit_length [8],
			   first_segment_flag,
			   last_segment_flag,
			   field_parity,
			   line_offset [5],
			   first_pixel_position [16],
			   n_pixels [8] */
			p[0] = DATA_UNIT_MONOCHROME_SAMPLES;
			p[1] = 0x2C;
			p[2] = lofp
				+ ((r_left == n_pixels_total) << 7)
				+ ((r_left == n_pixels) << 6);
			p[3] = first_pixel_position >> 8;
			p[4] = first_pixel_position;
			p[5] = n_pixels;

			first_pixel_position += n_pixels;

			memcpy (p + 6, r, n_pixels);

			r += n_pixels;
			r_left -= n_pixels;

			/* Pad to data_unit_length 0x2C if necessary. */
			memset (p + 6 + n_pixels, 0xFF,
				0x2C - 4 - n_pixels);

			*last_du_size = 2 + 0x2C;
		} else {
			if (unlikely (crit_p_left == p_left)) {
				/* We must not call encode_stuffing()
				   with last_du_size >= 257. (One byte
				   is too small for a new stuffing data
				   unit, maximum data_unit_length is
				   too big to append a stuffing byte.) */
				n_pixels = MIN (r_left, 250u);
			} else {
				/* EN 301 775 table 12 (data unit size
				   must not exceed 2 + 255 bytes). */
				n_pixels = MIN (r_left, 251u);

				n_pixels = MIN (n_pixels, p_left - 6);
			}

			/* data_unit_id [8],
			   data_unit_length [8],
			   first_segment_flag,
			   last_segment_flag,
			   field_parity,
			   line_offset [5],
			   first_pixel_position [16],
			   n_pixels [8] */
			p[0] = DATA_UNIT_MONOCHROME_SAMPLES;
			p[1] = 4 + n_pixels;
			p[2] = lofp
				+ ((r_left == n_pixels_total) << 7)
				+ ((r_left == n_pixels) << 6);
			p[3] = first_pixel_position >> 8;
			p[4] = first_pixel_position;
			p[5] = n_pixels;

			first_pixel_position += n_pixels;

			memcpy (p + 6, r, n_pixels);

			r += n_pixels;
			r_left -= n_pixels;

			*last_du_size = 6 + n_pixels;
		}

		p += *last_du_size;
		p_left -= *last_du_size;
	}

	*packet = p;
	*raw = r;

	return 0; /* success */
}

/**
 * @param packet *packet must point to the output buffer where the
 *   data units will be stored. Initially this should be the position
 *   of the first data unit in a PES packet, immediately after the
 *   data_indentifier byte. @a *packet will be incremented by the
 *   size of the successfully stored data units.
 * @param packet_left @a *packet_left must contain the number of bytes
 *   available in the @a packet buffer. It will be decremented by
 *   the cumulative size of the successfully stored data units.
 * @param raw @a *raw must point at the raw VBI data to be converted,
 *   namely luminance samples as defined in ITU-R BT.601 with 8 bits
 *   per sample. @a *raw will be incremented by the number of
 *   successfully converted samples.
 * @param raw_left @a *raw_left must contain the number of
 *   samples left to be encoded in the @a raw buffer. It will
 *   be decremented by the number of successfully converted samples.
 * @param data_identifier When the @a data_indentifier lies in range
 *   0x10 to 0x1F inclusive, the encoded data units will be padded to
 *   data_unit_length 0x2C for compatibility with EN 300 472
 *   compliant decoders. The @a data_identifier itself will NOT be
 *   stored in the output buffer.
 * @param videostd_set The @a line parameter will be interpreted
 *   according to this set of video standards. It must not change
 *   until all samples have been encoded. In libzvbi 0.2.x only one
 *   of two values are permitted: VBI_VIDEOSTD_SET_625_50 or
 *   VBI_VIDEOSTD_SET_525_60.
 * @param line The ITU-R line number to be encoded in the data units.
 *   It must not change until all samples have been encoded.
 * @param first_pixel_position The horizontal offset where decoders
 *   shall insert the first sample in the VBI, counting samples from
 *   the start of the digital active line as defined in ITU-R BT.601.
 *   Usually @a first_pixel_position is zero and @a n_pixels_total is
 *   720. @a first_pixel_position + @a n_pixels_total must not be greater
 *   than 720. This parameter must not change until all samples have
 *   been encoded.
 * @param n_pixels_total Total size of the @a raw buffer in bytes,
 *   and the total number of samples to be encoded. Initially this
 *   value must be equal to @a *raw_left, and it must not
 *   change until all samples have been encoded.
 * @param stuffing If TRUE, and space remains in the output buffer
 *   after all samples have been successfully converted, the
 *   function fills up the buffer with stuffing data units.
 *
 * Converts one line of raw VBI samples to one or more "monochrome
 * 4:2:2 samples" data units as defined in EN 301 775, and stores
 * them in the @a packet buffer.
 *
 * @returns
 * @c FALSE on failure:
 * - @a *packet is @c NULL or @a *packet_left is less than two
 *   (the minimum data unit size is two bytes).
 * - @a *raw is @c NULL or @a *raw_left is zero.
 * - The @a data_identifier is in range 0x10 to 0x1F inclusive and
 *   @a *packet_left is not a multiple of 46.
 * - The @a videostd_set is ambiguous.
 * - The @a line parameter is outside the valid range, that is 7 to
 *   23 and 270 to 286 for 525 line standards, 7 to 23 and 320 to 336
 *   for 625 line standards. All numbers inclusive.
 * - @a *raw_left is greater than @a n_pixels_total.
 * - @a first_pixel_position + @a n_pixels_total is greater than 720.
 *
 * The output buffer remains unchanged on all errors.
 *
 * @note
 * According to EN 301 775 all lines stored in one PES packet must
 * belong to the same video frame (but the data of one frame may be
 * transmitted in several successive PES packets). They must be encoded
 * in the same order as they would be transmitted in the VBI, no line more
 * than once. Samples may have to be split into multiple segments and they
 * must be contiguously encoded into adjacent data units. The function
 * cannot enforce this if multiple calls are necessary to encode all
 * samples.
 *
 * @since 0.2.26
 */
vbi_bool
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
{
	uint8_t *p;
	const uint8_t *r;
	unsigned int p_left;
	unsigned int r_left;
	unsigned int last_du_size;
	vbi_bool fixed_length;
	int err;

	assert (NULL != packet);
	assert (NULL != raw);
	assert (NULL != packet_left);
	assert (NULL != raw_left);

	p = *packet;
	p_left = *packet_left;

	if (unlikely (NULL == p || p_left < 2)) {
		/* errno = VBI_ERR_BUFFER_OVERFLOW; */
		return FALSE;
	}

	fixed_length = fixed_length_format (data_identifier);

	if (fixed_length && unlikely ((p_left % 46) > 0)) {
		/* errno = VBI_ERR_BUFFER_OVERFLOW; */
		return FALSE;
	}

	r = *raw;
	r_left = *raw_left;

	if (NULL == r || 0 == r_left) {
		/* errno = VBI_ERR_NO_RAW_DATA; */
		return FALSE;
	}

	err = insert_raw_data_units (packet, p_left,
				     &last_du_size,
				     raw, r_left,
				     fixed_length,
				     videostd_set,
				     line,
				     first_pixel_position,
				     n_pixels_total,
				     stuffing);
	if (unlikely (err > 0)) {
		/* errno = err; */
		return FALSE;
	}

	*packet_left -= *packet - p;
	*raw_left -= *raw - r;

	if (stuffing) {
		encode_stuffing (*packet, *packet_left,
				 last_du_size, fixed_length);
		*packet += *packet_left;
		*packet_left = 0;
	}

	return TRUE;
}

struct _vbi_dvb_mux {
	uint8_t	*		packet;

	/* PES packet generator. */

	/* Caller option: min. total size of PES packets in bytes.
	   Must be a multiple of 184. */
	unsigned int		min_packet_size;

	/* Caller option: max. total size of PES packets in bytes.
	   Must be a multiple of 184. */
	unsigned int		max_packet_size;

	/* data_identifier [8] to be encoded. */
	unsigned int		data_identifier;

	/* If non-zero, the encoding of a raw VBI line in the
	   previous PES packet was unfinished. */
	unsigned int		raw_samples_left;

	/* The vbi_sliced.line number used to encode the
	   unfinished raw VBI line. */
	unsigned int		raw_line;

	/* The vbi_sampling_par.offset used to encode the
	   unfinished raw VBI line. */
	int			raw_offset;

	/* The vbi_sampling_par.samples_per_line used to
	   encode the unfinished raw VBI line. */
	unsigned int		raw_samples_per_line;

	/* The remaining samples (in the first samples_left bytes)
	   of the unfinished raw VBI line. */
	uint8_t			raw_samples[720];

	/* TS packet generator. */

	/* Program ID. Must be in range 0x0010 to 0x1FFF inclusive,
	   or 0 if we generate a PES stream instead of a TS stream. */
	unsigned int		pid;

	/* Will be incremented by one with each TS packet and
	   stored in the TS packet header. */
	unsigned int		continuity_counter;

	/* Coroutine status. */

	/* Current position in the packet[] buffer. */
	unsigned int		cor_offset;

	/* End of the data in the packet[] buffer. */
	unsigned int		cor_end;

	/* Bytes left to be read from the current TS packet. */
	unsigned int		cor_ts_left;

	vbi_dvb_mux_cb *	callback;
	void *			user_data;

	_vbi_log_hook		log;
};

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 *
 * Clears the internal buffers of the DVB VBI multiplexer.
 *
 * After a vbi_dvb_mux_reset() call the vbi_dvb_mux_cor() function
 * will encode a new PES packet, discarding any data of the previous
 * packet which has not been consumed by the application.
 *
 * @since 0.2.26
 */
void
vbi_dvb_mux_reset		(vbi_dvb_mux *		mx)
{
	assert (NULL != mx);

	mx->raw_samples_left = 0;

	/* Make clear that continuity was lost. */
	mx->continuity_counter = (mx->continuity_counter - 1) & 0xF;

	mx->cor_offset = 0;
	mx->cor_end = 0;
}

static int
samples_pointer			(const uint8_t **	samples,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 unsigned int		line)
{
	unsigned int field;
	unsigned int row;

	if (unlikely (NULL == raw))
		return VBI_ERR_NO_RAW_DATA;

	if (unlikely (NULL == sp))
		return VBI_ERR_SAMPLING_PAR;

	if (unlikely (0 == line))
		return VBI_ERR_LINE_NUMBER;

	field = (line >= 313);

	if (unlikely (line < (unsigned int) sp->start[field]))
		return VBI_ERR_RAW_BUFFER_OVERFLOW;

	row = line - sp->start[field];

	if (unlikely (row >= (unsigned int) sp->count[field]))
		return VBI_ERR_RAW_BUFFER_OVERFLOW;

	if (sp->interlaced) {
		row = row * 2 + field;
	} else {
		if (field > 0)
			row += sp->count[0];
	}

	*samples = raw + row * sp->bytes_per_line;

	return 0; /* success */
}
				 
_vbi_inline void
encode_timestamp		(uint8_t *		p,
				 int64_t		pts,
				 unsigned int		mark)
{
	unsigned int t;

	p[0] = mark + (unsigned int)((pts >> 29) & 0xE);

	t = (unsigned int) pts;

	p[1] = t >> 22;
	p[2] = (t >> 14) | 1;
	p[3] = t >> 7;
	p[4] = t * 2 + 1;
}

static vbi_bool
valid_sampling_par		(vbi_dvb_mux *		mx,
				 const vbi_sampling_par *sp)
{
	unsigned int samples_end;

	/* EN 301 775 section 4.9, BT.601-5. */

#if 3 == VBI_VERSION_MINOR
	if (unlikely (0 == (sp->videostd_set & VBI_VIDEOSTD_SET_625_50)))
		return FALSE;

	switch (sp->sp_sample_format) {
	case VBI_PIXFMT_Y8:
	case VBI_PIXFMT_YUV420:
		break;

	default:
		return FALSE;
	}
#else
	if (unlikely (625 != sp->scanning))
		return FALSE;

	if (unlikely (VBI_PIXFMT_YUV420 != sp->sp_sample_format))
		return FALSE;
#endif

	if (unlikely (13500000 != sp->sampling_rate))
		return FALSE;

	if (unlikely ((unsigned int) sp->offset < BT601_625_OFFSET))
		return FALSE;

	samples_end = sp->offset + sp->sp_samples_per_line;
	if (unlikely (samples_end > BT601_625_OFFSET + 720))
		return FALSE;
	if (unlikely (samples_end < (unsigned int) sp->sp_samples_per_line))
		return FALSE; /* overflow */

	if (unlikely (!sp->synchronous))
		return FALSE;

	return _vbi_sampling_par_valid_log (sp, &mx->log);
}

static void
init_pes_packet_header		(vbi_dvb_mux *		mx)
{
	/* Bytes 0 ... 3 are reserved for the first TS packet header. */

	/* packet_start_code_prefix [24] */
	mx->packet[4 + 0] = 0x00;
	mx->packet[4 + 1] = 0x00;
	mx->packet[4 + 2] = 0x01;

	/* EN 301 775 section 4.3: stream_id set to '1011 1101'
	   meaning 'private_stream_1'. */
	mx->packet[4 + 3] = PRIVATE_STREAM_1;

	/* We initialize bytes 8 ... 9 (packet_length [16])
	   in generate_pes_packet(). */

	/* EN 301 775 section 4.3: data_alignment_indicator set to
	   '1' indicating that the VBI access units are aligned with
	   the PES packets. */

	/* '10',
	   PES_scrambling_control [2] = '00' (not scrambled),
	   PES_priority = '0' (normal),
	   data_alignment_indicator = '1',
	   copyright = '0' (undefined),
	   original_or_copy = '0' (copy) */
	mx->packet[4 + 6] = (2 << 6) | (1 << 2);

	/* EN 301 775 section 4.3: "PTS shall be present." */

	/* PTS_DTS_flags [2] = '10' (PTS only),
	   ESCR_flag = '0' (no ESCR fields),
	   ES_rate_flag = '0' (no ES_rate field),
	   DSM_trick_mode_flag = '0' (no trick mode field),
	   additional_copy_info_flag = '0' (no additional_copy_info field),
	   PES_CRC_flag = '0' (no CRC field),
	   PES_extension_flag = '0' (no extension field). */
	mx->packet[4 + 7] = 2 << 6;

	/* EN 301 775 section 4.3: PES_header_data_length set to '0x24',
	   for a total PES packet header length of 45 bytes. */

	/* PES_header_data_length [8] */
	mx->packet[4 + 8] = 0x24;

	/* Stuffing bytes. */
	memset (mx->packet + 4 + 9, 0xFF, 36);
}

/**
 * @internal
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 * @param packet_size The total size of the generated PES packet will
 *   be stored here. The data itself is stored at @a mx->packet + 4.
 * @param sliced @a *sliced must point at the sliced VBI data to be
 *   converted. All data must belong to the same video frame. The
 *   pointer will be advanced by the number of successfully
 *   converted structures. On failure it will point at
 *   the offending vbi_sliced structure.
 * @param sliced_left @a *sliced_left must contain the number of
 *   vbi_sliced structures in the @a sliced array. It will
 *   be decremented by the number of successfully converted
 *   structures.
 * @param service_mask Only data services in this set will be
 *   encoded. Other data services in the @a *sliced array will be
 *   discarded without further checks. Create a set by ORing
 *   @c VBI_SLICED_ values.
 * @param raw Shall point at a raw VBI data frame of (@a sp->count[0]
 *   + @a sp->count[1]) lines times @a sp->bytes_per_line. Raw
 *   VBI data lines to be inserted into the PES packet are selected
 *   by vbi_sliced structures in the @a sliced array with
 *   id @c VBI_SLICED_VBI_625. The data field in these structures is
 *   ignored. When the array does not contain such structures
 *   @a raw can be @c NULL.
 * @param sp Describes the data in the @a raw buffer. All fields
 *   must be valid, with the following additional constraints:
 *   - .videostd_set must contain one or more bits from the
 *     @c VBI_VIDEOSTD_SET_625_50, in libzvbi 0.2.x .scanning must be 625.
 *   - .sampling_format must be @c VBI_PIXFMT_Y8 or
 *     @c VBI_PIXFMT_YUV420. Chrominance samples are ignored.
 *   - .sampling_rate must be @c 13500000.
 *   - .offset must be >= @c 132.
 *   - .samples_per_line (.bytes_per_line in libzvbi 0.2.x) must be >= @c 1.
 *   - .offset + .samples_per_line must be <= @c 132 + @c 720.
 *   - .synchronous must be @c TRUE.
 *   The .offset and .samples_per_line value must not change until
 *   all raw VBI data has been encoded. When @a raw is @c NULL,
 *   @a sp can be @c NULL too.
 * @param pts This Presentation Time Stamp will be encoded into the
 *   PES packet. Bits 33 ... 63 are discarded.
 *
 * This function converts sliced and/or raw VBI data to a VBI PES
 * packet as defined in EN 300 472 and EN 301 775, and stores it
 * at @a mx->packet + 4.
 *
 * @returns
 * - @c 0 Success. If @a mx->max_packet_size is too small, @a *sliced
 *   will point at the remaining unconverted structures. If
 *   @a mx->max_packet_size is too small to contain the last segment
 *   of a raw VBI line, @a *sliced will point at the vbi_sliced
 *   structure of this line, and the function will encode the
 *   remaining segments in the next call.
 * - @c VBI_ERR_LINE_ORDER
 *   The @a sliced array is not sorted by ascending line number,
 *   except for elements with line number zero.
 * - @c VBI_ERR_INVALID_SERVICE
 *   Only the following data services can be encoded:
 *   - @c VBI_SLICED_TELETEXT_B on lines 7 to 22 and 320 to 335
 *     inclusive, or with line number 0 (undefined). All Teletext
 *     lines will be encoded with data_unit_id 0x02 ("EBU Teletext
 *     non-subtitle data").
 *   - @c VBI_SLICED_VPS on line 16.
 *   - @c VBI_SLICED_CAPTION_625 on line 22.
 *   - @c VBI_SLICED_WSS_625 on line 23.
 *   - Raw VBI data with id @c VBI_SLICED_VBI_625 can be encoded
 *     on lines 7 to 23 and 320 to 336 inclusive.
 * - @c VBI_ERR_LINE_NUMBER
 *   A vbi_sliced structure contains a line number outside the
 *   valid range specified above.
 * - @c VBI_ERR_RAW_DATA_INTERRUPTION
 *   The function expected to continue the encoding of a raw VBI
 *   line, but the id of the first vbi_sliced structure in the
 *   @a *sliced array is not @c VBI_SLICED_VBI_625, or the line
 *   number differs, or @a sp->offset or @a sp->samples_per_line
 *   (bytes_per_line in libzvbi 0.2.x) changed.
 * - @c VBI_ERR_NO_RAW_DATA
 *   @a raw is @c NULL although the @a *sliced array contains a
 *   structure with id @c VBI_SLICED_VBI_625.
 * - @c VBI_ERR_SAMPLING_PAR
 *   @a sp is @c NULL although the @a *sliced array contains a
 *   structure with id @c VBI_SLICED_VBI_625.
 * - @c VBI_ERR_RAW_BUFFER_OVERFLOW
 *   A vbi_sliced structure with id @c VBI_SLICED_VBI_625
 *   contains a line number outside the ranges defined by
 *   @a sp->start[] and @a sp->count[] (i.e. the line is not
 *   in the @a raw buffer).
 *
 * On all errors @a *sliced points at the offending vbi_sliced
 * structure and the output buffer contents are undefined.
 */
static int
generate_pes_packet		(vbi_dvb_mux *		mx,
				 unsigned int *		packet_size,
				 const vbi_sliced **	sliced,
				 unsigned int *		sliced_left,
				 vbi_service_set	service_mask,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 int64_t		pts)
{
	uint8_t *p;
	uint8_t *p_end;
	const vbi_sliced *s;
	const vbi_sliced *s_begin;
	const vbi_sliced *s_end;
	const uint8_t *samples;
	const uint8_t *samples_end;
	unsigned int p_left;
	unsigned int last_line;
	unsigned int last_du_size;
	unsigned int packet_length;
	unsigned int size;
	vbi_bool fixed_length;
	int err;

	/* The PES packet header starts at mx->packet + 4 and has
	   already been initialized up to the data_identifier byte. */

	encode_timestamp (&mx->packet[4 + 9], pts,
			  /* mark: PTS only */ 0x21);

	mx->packet[4 + 45] = mx->data_identifier;

	fixed_length = fixed_length_format (mx->data_identifier);

	/* TS packet header,
	   PES packet header,
	   data_identifier [8] */
	p = mx->packet + 4 + 45 + 1;
	p_end = p + mx->max_packet_size - 46;

	s = *sliced;
	s_end = s + *sliced_left;

	if (mx->raw_samples_left > 0
	    && unlikely (s >= s_end
			 || VBI_SLICED_VBI_625 != s->id
			 || mx->raw_line != s->line
			 || mx->raw_offset != sp->offset
			 || (mx->raw_samples_per_line
			     != (unsigned int) sp->sp_samples_per_line))) {
		err = VBI_ERR_RAW_DATA_INTERRUPTION;
		goto failed;
	}

	s_begin = s;

	last_line = 0;

	for (;;) {
		if (s < s_end) {
			if (s->line > 0) {
				/* EN 301 775 section 4.1: "[...] lines
				   shall appear in the bitstream in the
				   same order, as they will appear in
				   the VBI;" "a certain VBI line may
				   never be coded twice within a frame" */
				if (unlikely (s->line <= last_line)) {
					err = VBI_ERR_LINE_ORDER;
					goto failed;
				}

				last_line = s->line;
			}

			if (VBI_SLICED_VBI_625 != s->id) {
				++s;
				continue;
			}
		}

		/* Encode any sliced lines preceding this
		   raw line or end of sliced data. */

		err = insert_sliced_data_units (&p,
						p_end - p,
						&last_du_size,
						&s_begin,
						s - s_begin,
						service_mask,
						fixed_length);
		if (unlikely (0 != err)) {
			s = s_begin;
			goto failed;
		}

		if (s_begin < s) {
			/* Not enough space to encode all sliced data. */
			s = s_begin;
			break;
		}

		if (s >= s_end)
			break;

		if (0 == (service_mask & VBI_SLICED_VBI_625)) {
			s_begin = ++s;
			continue;
		}

		/* New or continued raw VBI line. */

		if (0 == mx->raw_samples_left) {
			/* XXX Perhaps another option would be to
			   store raw and sp in s? */
			err = samples_pointer (&samples, raw, sp, s->line);
			if (unlikely (0 != err))
				goto failed;

			mx->raw_samples_left = sp->sp_samples_per_line;
		} else {
			samples = mx->raw_samples;
		}

		assert (mx->raw_samples_left <= sizeof (mx->raw_samples));

		samples_end = samples + mx->raw_samples_left;

		err = insert_raw_data_units (&p,
					     p_end - p,
					     &last_du_size,
					     &samples,
					     mx->raw_samples_left,
					     fixed_length,
					     VBI_VIDEOSTD_SET_625_50,
					     s->line,
					     sp->offset - BT601_625_OFFSET,
					     sp->sp_samples_per_line,
					     /* stuffing */ TRUE);
		if (unlikely (0 != err)) {
			mx->raw_samples_left = 0;
			goto failed;
		}

		mx->raw_samples_left = samples_end - samples;
		if (mx->raw_samples_left > 0) {
			/* Not enough space to encode samples_left. */

			/* No tricks, mister. */
			memcpy (mx->raw_samples, samples,
				mx->raw_samples_left);

			mx->raw_line = s->line;
			mx->raw_offset = sp->offset;
			mx->raw_samples_per_line = sp->sp_samples_per_line;

			break;
		}

		s_begin = ++s;
	}

	*sliced = s;
	*sliced_left = s_end - s;

	size = p - mx->packet - 4;

	if (size < mx->min_packet_size) {
		p_left = mx->min_packet_size - size;
	} else {
		unsigned int remainder;

		p_left = 0;
		remainder = size % 184;

		/* EN 301 775 section 4.3: Total PES packet size must
		   be a multiple of 184. */
		if (remainder > 0)
			p_left = 184 - remainder;
	}

	size += p_left;

	encode_stuffing (p, p_left, last_du_size, fixed_length);

	/* packet_start_code_prefix [24],
	   stream_id [8],
	   PES_packet_length [16] */
	packet_length = size - 6;

	/* PES_packet_length [16] */
	mx->packet[4 + 4] = packet_length >> 8;
	mx->packet[4 + 5] = packet_length;

	*packet_size = size;

	return 0; /* success */

 failed:
	*sliced = s;
	*sliced_left = s_end - s;

	*packet_size = 0;

	return err;
}

static void
generate_ts_packet_header	(vbi_dvb_mux *		mx,
				 unsigned int		offset)
{
	uint8_t *p;

	p = mx->packet + offset;

	/* sync_byte [8] = 0x47 */
	p[0] = 0x47;

	/* ISO 13818-1 section 2.4.3.3:
	   "payload_unit_start_indicator is set if exactly one
	   PES packet commences in this TS packet immediately
	   after the header." */
	if (0 == offset) {
		/* transport_error_indicator = '0' (no error),
		   payload_unit_start_indicator = '1',
		   transport_priority,
		   PID [5 msb of 13] */
		p[1] = (1 << 6) | (mx->pid >> 8);
	} else {
		/* transport_error_indicator = '0' (no error),
		   payload_unit_start_indicator = '0',
		   transport_priority,
		   PID [5 msb of 13] */
		p[1] = mx->pid >> 8;
	}

	/* PID [8 lsb of 13] */
	p[2] = mx->pid;

	/* EN 300 472 section 4.1: "adaptation_field_control:
	   only the values '01' and '10' are permitted." */

	/* transport_scrambling_control [2] = '00' (not scrambled),
	   adaptation_field_control [2] = '01' (payload only),
	   continuity_counter [4] */
	p[3] = (1 << 4) + (mx->continuity_counter++ & 15);
}

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 * @param buffer @a *buffer must point at the output buffer where the
 *   converted data will be stored, and will be incremented by the
 *   number of bytes stored there.
 * @param buffer_left @a *buffer_left must contain the number of bytes
 *   available in the @a buffer, and will be decremented by number
 *   of bytes stored there.
 * @param sliced @a *sliced must point at the sliced VBI data to be
 *   converted. All data must belong to the same video frame. The
 *   pointer will be advanced by the number of successfully
 *   converted structures. On failure it will point at
 *   the offending vbi_sliced structure.
 * @param sliced_left @a *sliced_left must contain the number of
 *   vbi_sliced structures in the @a sliced array. It will
 *   be decremented by the number of successfully converted
 *   structures.
 * @param service_mask Only data services in this set will be
 *   encoded. Other data services in the @a sliced array will be
 *   discarded without further checks. Create a set by ORing
 *   @c VBI_SLICED_ values.
 * @param raw Shall point at a raw VBI frame of (@a sp->count[0]
 *   + @a sp->count[1]) lines times @a sp->bytes_per_line. The function
 *   encodes only those lines which have been selected by vbi_sliced
 *   structures in the @a sliced array with id @c VBI_SLICED_VBI_625.
 *   The data field of these structures is ignored. When the @a sliced
 *   array does not contain such structures @a raw can be @c NULL.
 * @param sp Describes the data in the @a raw buffer. When @a raw is
 *   @c NULL, @a sp can be @c NULL too. Otherwise all fields must be
 *   valid, with the following additional constraints:
 *   - .videostd_set must contain one or more bits from the
 *     @c VBI_VIDEOSTD_SET_625_50. In libzvbi 0.2.x .scanning must be 625.
 *   - .sampling_format must be @c VBI_PIXFMT_Y8 or
 *     @c VBI_PIXFMT_YUV420. Chrominance samples are ignored.
 *   - .sampling_rate must be @c 13500000.
 *   - .offset must be >= @c 132, and the value must not change until
 *     all samples have been encoded.
 *   - .samples_per_line (.bytes_per_line in libzvbi 0.2.x) must be >=
 *     @c 1, and the value must not change until all samples have been
 *     encoded.
 *   - .offset + .samples_per_line must be <= @c 132 + @c 720.
 *   - .synchronous must be @c TRUE.
  * @param pts This Presentation Time Stamp will be encoded into the
 *   PES packet. Bits 33 ... 63 are discarded.
 *
 * This function converts raw and/or sliced VBI data to one DVB VBI PES
 * packet or one or more TS packets as defined in EN 300 472 and
 * EN 301 775, and stores them in the output buffer.
 *
 * If the returned @a *buffer_left value is zero and the returned
 * @a *sliced_left value is greater than zero another call will be
 * necessary to convert the remaining data.
 *
 * After a vbi_dvb_mux_reset() call the vbi_dvb_mux_cor() function
 * will encode a new PES packet, discarding any data of the previous
 * packet which has not been consumed by the application.
 *
 * @returns
 * @c FALSE on failure:
 * - @a *buffer is @c NULL or @a *buffer_left is zero.
 * - @a *sliced in @c NULL or @a *sliced_left is zero.
 * - The maximum PES packet size, or the value selected with
 *   vbi_dvb_mux_set_pes_packet_size(), is too small to contain all
 *   the sliced and raw VBI data.
 * - The @a sliced array is not sorted by ascending line number,
 *   except for elements with line number 0 (undefined).
 * - Only the following data services can be encoded:
 *   - @c VBI_SLICED_TELETEXT_B on lines 7 to 22 and 320 to 335
 *     inclusive, or with line number 0 (undefined). All Teletext
 *     lines will be encoded with data_unit_id 0x02 ("EBU Teletext
 *     non-subtitle data").
 *   - @c VBI_SLICED_VPS on line 16.
 *   - @c VBI_SLICED_CAPTION_625 on line 22.
 *   - @c VBI_SLICED_WSS_625 on line 23.
 *   - Raw VBI data with id @c VBI_SLICED_VBI_625 can be encoded
 *     on lines 7 to 23 and 320 to 336 inclusive. Note for compliance
 *     with the Teletext buffer model defined in EN 300 472,
 *     EN 301 775 recommends to encode at most one raw and one
 *     sliced, or two raw VBI lines per frame.
 * - A vbi_sliced structure contains a line number outside the
 *   valid range specified above.
 * - @a raw is @c NULL although the @a *sliced array contains a
 *   structure with id @c VBI_SLICED_VBI_625.
 * - @a sp is @c NULL although the @a *sliced array contains a
 *   structure with id @c VBI_SLICED_VBI_625.
 * - One or more fields of the @a sp structure are invalid.
 * - A vbi_sliced structure with id @c VBI_SLICED_VBI_625
 *   contains a line number outside the ranges defined by
 *   @a sp->start[] and @a sp->count[] (i.e. the line is not
 *   in the @a raw buffer).
 *
 * On all errors @a *sliced will point at the offending vbi_sliced
 * structure and the output buffer remains unchanged.
 *
 * @since 0.2.26
 */
vbi_bool
vbi_dvb_mux_cor		(vbi_dvb_mux *		mx,
				 uint8_t **		buffer,
				 unsigned int *		buffer_left,
				 const vbi_sliced **	sliced,
				 unsigned int *		sliced_left,
				 vbi_service_set	service_mask,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,	 
				 int64_t		pts)
{
	uint8_t *p;
	unsigned int p_left;
	unsigned int offset;

	assert (NULL != mx);
	assert (NULL != buffer);
	assert (NULL != buffer_left);
	assert (NULL != sliced);
	assert (NULL != sliced_left);

	p = *buffer;
	p_left = *buffer_left;

	if (unlikely (NULL == p || 0 == p_left)) {
		/* errno = VBI_ERR_BUFFER_OVERFLOW; */
		return FALSE;
	}

	if (NULL != sp && !valid_sampling_par (mx, sp)) {
		/* errno = VBI_ERR_SAMPLING_PAR; */
		return FALSE;
	}

	offset = mx->cor_offset;

	if (offset >= mx->cor_end) {
		const vbi_sliced *s;
		unsigned int s_left;
		int err;

		s = *sliced;
		s_left = *sliced_left;

		if (unlikely (NULL == s || 0 == s_left)) {
			/* errno = VBI_ERR_NO_SLICED_DATA; */
			return FALSE;
		}

		err = generate_pes_packet (mx, &mx->cor_end,
					   &s, &s_left,
					   service_mask,
					   raw, sp,
					   pts);
		if (unlikely (0 != err)) {
			*sliced = s;
			*sliced_left = s_left;
			mx->cor_end = 0;
			/* errno = err; */
			return FALSE;
		}

		if (unlikely (s_left > 0)) {
			*sliced = s;
			*sliced_left = s_left;
			mx->cor_end = 0;
			/* errno = VBI_ERR_BUFFER_OVERFLOW; */
			return FALSE;
		}

		offset = 4;
		mx->cor_end += 4;
		mx->cor_ts_left = 0;
	}

	if (0 == mx->pid) {
		unsigned int size;

		size = MIN (p_left, mx->cor_end - offset);

		memcpy (p, mx->packet + offset, size);

		p += size;
		p_left -= size;

		offset += size;
	} else {
		unsigned int ts_left;

		ts_left = mx->cor_ts_left;

		do {
			unsigned int size;

			if (0 == ts_left) {
				offset -= 4;
				generate_ts_packet_header (mx, offset);
				ts_left = 188;
			}

			size = MIN (p_left, ts_left);

			memcpy (p, mx->packet + offset, size);

			p += size;
			p_left -= size;

			offset += size;
			ts_left -= size;
		} while (p_left > 0 && offset < mx->cor_end);

		mx->cor_ts_left = ts_left;
	}

	mx->cor_offset = offset;

	if (offset >= mx->cor_end) {
		*sliced += *sliced_left;
		*sliced_left = 0;
	}

	*buffer = p;
	*buffer_left = p_left;

	return TRUE;
}

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 * @param sliced Pointer to the sliced VBI data to be
 *   converted. All data must belong to the same video frame.
 * @param sliced_lines The number of vbi_sliced structures
 *   in the @a sliced array.
 * @param service_mask Only data services in this set will be
 *   encoded. Other data services in the @a sliced array will be
 *   discarded without further checks. Create a set by ORing
 *   @c VBI_SLICED_ values.
 * @param raw Shall point at a raw VBI frame of (@a sp->count[0]
 *   + @a sp->count[1]) lines times @a sp->bytes_per_line. The function
 *   encodes only those lines which have been selected by vbi_sliced
 *   structures in the @a sliced array with id @c VBI_SLICED_VBI_625.
 *   The data field of these structures is ignored. When the @a sliced
 *   array does not contain such structures @a raw can be @c NULL.
 * @param sp Describes the data in the @a raw buffer. When @a raw is
 *   @c NULL, @a sp can be @c NULL too. Otherwise all fields
 *   must be valid, with the following additional constraints:
 *   - .videostd_set must contain one or more bits from the
 *     @c VBI_VIDEOSTD_SET_625_50. In libzvbi 0.2.x .scanning must be
 *     625.
 *   - .sampling_format must be @c VBI_PIXFMT_Y8 or
 *     @c VBI_PIXFMT_YUV420. Chrominance samples are ignored.
 *   - .sampling_rate must be @c 13500000.
 *   - .offset must be >= @c 132.
 *   - .samples_per_line (in libzvbi 0.2.x .bytes_per_line) must be >= @c 1.
 *   - .offset + .samples_per_line must be <= @c 132 + @c 720.
 *   - .synchronous must be @c TRUE.
 * @param pts This Presentation Time Stamp will be encoded into the
 *   PES packet. Bits 33 ... 63 are discarded.
 *
 * This function converts raw and/or sliced VBI data to one DVB VBI PES
 * packet or one or more TS packets as defined in EN 300 472 and
 * EN 301 775. For output it calls the callback function passed to
 * vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new() once for each
 * PES or TS packet.
 *
 * @returns
 * @c FALSE on failure.
 * - No callback function was selected with vbi_dvb_pes_mux_new()
 *   or vbi_dvb_ts_mux_new().
 * - The callback function returned @c FALSE. Any remaining TS packets
 *   which have not been output yet are discarded.
 * - The maximum PES packet size, or the value selected with
 *   vbi_dvb_mux_set_pes_packet_size(), is too small to contain all
 *   the sliced and raw VBI data.
 * - The @a sliced array is not sorted by ascending line number,
 *   except for elements with line number 0 (undefined).
 * - Only the following data services can be encoded:
 *   - @c VBI_SLICED_TELETEXT_B on lines 7 to 22 and 320 to 335
 *     inclusive, or with line number 0 (undefined). All Teletext
 *     lines will be encoded with data_unit_id 0x02 ("EBU Teletext
 *     non-subtitle data").
 *   - @c VBI_SLICED_VPS on line 16.
 *   - @c VBI_SLICED_CAPTION_625 on line 22.
 *   - @c VBI_SLICED_WSS_625 on line 23.
 *   - Raw VBI data with id @c VBI_SLICED_VBI_625 can be encoded
 *     on lines 7 to 23 and 320 to 336 inclusive.
 * - A vbi_sliced structure contains a line number outside the
 *   valid range specified above.
 * - @a raw is @c NULL although the @a sliced array contains a
 *   structure with id @c VBI_SLICED_VBI_625.
 * - @a sp is @c NULL although the @a sliced array contains a
 *   structure with id @c VBI_SLICED_VBI_625.
 * - One or more fields of the @a sp structure are invalid.
 * - A vbi_sliced structure with id @c VBI_SLICED_VBI_625
 *   contains a line number outside the ranges defined by
 *   @a sp->start[] and @a sp->count[] (i.e. the line is not
 *   in the @a raw buffer).
 *
 * The function does not call the callback function on failure.
 *
 * @since 0.2.26
 */
vbi_bool
vbi_dvb_mux_feed		(vbi_dvb_mux *		mx,
				 const vbi_sliced *	sliced,
				 unsigned int		sliced_lines,
				 vbi_service_set	service_mask,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,	 
				 int64_t		pts)
{
	const vbi_sliced *s;
	unsigned int s_left;
	unsigned int packet_size;
	int err;

	assert (NULL != mx);

	if (unlikely (NULL == mx->callback)) {
		/* errno = VBI_ERR_NO_CALLBACK */
		return FALSE;
	}

	if (NULL != sp && !valid_sampling_par (mx, sp)) {
		/* errno = VBI_ERR_SAMPLING_PAR; */
		return FALSE;
	}

	if (unlikely (mx->cor_offset < mx->cor_end)) {
		warning (&mx->log,
			 "Lost unconsumed data from a previous "
			 "vbi_dvb_mux_cor() call.");
		mx->cor_end = 0;
	}

	s = sliced;
	s_left = sliced_lines;

	if (NULL == s)
		s_left = 0;

	err = generate_pes_packet (mx, &packet_size,
				   &s, &s_left,
				   service_mask,
				   raw, sp,
				   pts);
	if (unlikely (0 != err)) {
		/* errno = err; */
		return FALSE;
	}

	if (unlikely (s_left > 0)) {
		/* errno = VBI_ERR_BUFFER_OVERFLOW; */
		return FALSE;
	}

	if (0 == mx->pid) {
		return mx->callback (mx, mx->user_data,
				     mx->packet + 4, packet_size);
	} else {
		unsigned int offset;

		/* The PES packet starts at mx->packet + 4, so we can
		   prepend a TS packet header without copying. Note
		   this overwrites the PES packet (and the end of the
		   previous TS packet) in the second and following
		   iterations. */

		offset = 0;

		do {
			generate_ts_packet_header (mx, offset);

			if (!mx->callback (mx, mx->user_data,
					   mx->packet + offset, 188))
				return FALSE;

			offset += 184; /* sic */
		} while (offset < packet_size);
	}

	return TRUE;
}

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 *
 * Returns the data_identifier the multiplexer encodes into PES packets.
 *
 * @since 0.2.26
 */
unsigned int
vbi_dvb_mux_get_data_identifier (const vbi_dvb_mux *	mx)
{
	assert (NULL != mx);
	return mx->data_identifier;
}

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 * @param data_identifier The data_identifier byte to be stored in
 *   PES packets.
 *
 * Determines the data_identifier byte to be stored in PES packets.
 * For compatibility with decoders compliant to EN 300 472 this should
 * be a value in the range 0x10 to 0x1F inclusive. The values 0x99
 * to 0x9B inclusive as defined in EN 301 775 are also permitted.
 *
 * The default data_identifier is 0x10.
 *
 * @returns
 * @c FALSE if the @a data_identifier is outside the valid ranges
 * specified above.
 *
 * @since 0.2.26
 */
vbi_bool
vbi_dvb_mux_set_data_identifier (vbi_dvb_mux *	mx,
				  unsigned int		data_identifier)
{
	assert (NULL != mx);

	if (likely ((data_identifier >= 0x10
		     && data_identifier < 0x20)
		    || (data_identifier >= 0x99
			&& data_identifier < 0x9C))) {
		mx->data_identifier = data_identifier;
		return TRUE;
	} else {
		return FALSE;
	}
}

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 *
 * Returns the maximum size of PES packets the multiplexer generates.
 *
 * @since 0.2.26
 */
unsigned int
vbi_dvb_mux_get_min_pes_packet_size
				(vbi_dvb_mux *		mx)
{
	assert (NULL != mx);
	return mx->min_packet_size;
}

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 *
 * Returns the minimum size of PES packets the multiplexer generates.
 *
 * @since 0.2.26
 */
unsigned int
vbi_dvb_mux_get_max_pes_packet_size
				(vbi_dvb_mux *		mx)
{
	assert (NULL != mx);
	return mx->max_packet_size;
}

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new().
 * @param min_size Minimum PES packet size.
 * @param max_size Maximum PES packet size.
 *
 * Determines the minimum and maximum total size of PES packets
 * generated by the multiplexer, including all header bytes. When
 * the data to be stored in a packet is smaller than the minimum size,
 * the multiplexer will fill the packet up with stuffing bytes. When
 * the data is larger than the maximum size the vbi_dvb_mux_feed() and
 * vbi_dvb_mux_cor() functions will fail.
 *
 * The PES packet size must be a multiple of 184 bytes, in the range 184
 * to 65504 bytes inclusive, and this function will round @a min_size up
 * and @a max_size down accordingly. If after rounding the maximum size is
 * lower than the minimum, it will be set to the same value as the
 * minimum size.
 *
 * The default minimum size is 184, the default maximum 65504 bytes. For
 * compatibility with decoders compliant to the Teletext buffer model
 * defined in EN 300 472 the maximum should not exceed 1472 bytes.
 *
 * @returns
 * @c FALSE on failure (out of memory).
 *
 * @since 0.2.26
 */
vbi_bool
vbi_dvb_mux_set_pes_packet_size (vbi_dvb_mux *	mx,
				  unsigned int		min_size,
				  unsigned int		max_size)
{
	/* EN 301 775 section 4.3: PES_packet_length must be N * 184 - 6. */

	if (min_size < 184) {
		min_size = 184;
	} else if (min_size > MAX_PES_PACKET_SIZE) {
		min_size = MAX_PES_PACKET_SIZE;
	} else {
		min_size += 183;
		min_size -= min_size % 184;
	}

	if (max_size < min_size) {
		max_size = min_size;
	} else if (max_size > MAX_PES_PACKET_SIZE) {
		max_size = MAX_PES_PACKET_SIZE;
	} else {
		max_size -= max_size % 184;
	}

	mx->min_packet_size = min_size;
	mx->max_packet_size = max_size;

	/* May allocate something in the future. */

	return TRUE;
}

/**
 * @param mx DVB VBI multiplexer context allocated with
 *   vbi_dvb_pes_mux_new() or vbi_dvb_ts_mux_new(). Can be @c NULL.
 *
 * Frees all resources associated with @a mx.
 *
 * @since 0.2.26
 */
void
vbi_dvb_mux_delete		(vbi_dvb_mux *		mx)
{
	if (NULL == mx)
		return;

	vbi_free (mx->packet);

	CLEAR (*mx);

	vbi_free (mx);
}

/**
 * @param callback Function to be called by vbi_dvb_mux_feed() when
 *   a new packet is available. Can be @c NULL if you want to use the
 *   vbi_dvb_mux_cor() coroutine instead.
 * @param user_data User pointer passed through to the @a callback function.
 *
 * Allocates a new DVB VBI multiplexer converting raw and/or sliced VBI data
 * to MPEG-2 Packetized Elementary Stream (PES) packets as defined in the
 * standards EN 300 472 and EN 301 775.
 *
 * @returns
 * Pointer to newly allocated DVB VBI multiplexer context, which must be
 * freed with vbi_dvb_mux_delete() when done. @c NULL on failure
 * (out of memory).
 *
 * @since 0.2.26
 */
vbi_dvb_mux *
vbi_dvb_pes_mux_new		(vbi_dvb_mux_cb *	callback,
				 void *			user_data)
{
	vbi_dvb_mux *mx;

	mx = vbi_malloc (sizeof (*mx));
	if (unlikely (NULL == mx)) {
		errno = ENOMEM;
		return NULL;
	}

	CLEAR (*mx);

	/* EN 301 775 section 4.3: PES_packet_length must be
	   N * 184 - 6, for a total packet size of N * 184 bytes. */
	mx->min_packet_size = 1 * 184;
	mx->max_packet_size = MAX_PES_PACKET_SIZE;

	/* We do not store this in mx->packet[] directly to avoid
	   a race with the coroutine. */
	mx->data_identifier = 0x10;

	/* Actually 4 + 9 * 184 would be enough for the first TS
	   packet header, 46 bytes PES packet header, and 2 * 17 lines
	   per frame (7 to 23 inclusive) times 46 bytes per line. */
	mx->packet = vbi_malloc (4 + mx->max_packet_size);
	if (unlikely (NULL == mx->packet)) {
		vbi_dvb_mux_delete (mx);
		errno = ENOMEM;
		return NULL;
	}

	init_pes_packet_header (mx);

	mx->callback = callback;
	mx->user_data = user_data;

	return mx;
}

/**
 * @param pid This Program ID will be stored in the header of the generated
 *   TS packets. The @a pid must be in range @c 0x0010 to @c 0x1FFE inclusive.
 * @param callback Function to be called by vbi_dvb_mux_feed() when
 *   a new packet is available. Can be @c NULL if you want to use the
 *   vbi_dvb_mux_cor() coroutine instead.
 * @param user_data User pointer passed through to the @a callback function.
 *
 * Allocates a new DVB VBI multiplexer converting raw and/or sliced VBI data
 * to MPEG-2 Transport Stream (TS) packets as defined in the standards
 * EN 300 472 and EN 301 775.
 *
 * @returns
 * Pointer to newly allocated DVB VBI multiplexer context, which must be
 * freed with vbi_dvb_mux_delete() when done. @c NULL on failure
 * (out of memory or invalid @a pid).
 *
 * @since 0.2.26
 */
vbi_dvb_mux *
vbi_dvb_ts_mux_new		(unsigned int		pid,
				 vbi_dvb_mux_cb *	callback,
				 void *			user_data)
{
	vbi_dvb_mux *mx;

	/* 0x0000 Program Association Table
	   0x0001 Conditional Access Table
	   0x0002-0x000F reserved
	   0x1FFF Null packet */
	if (pid <= 0x000F || pid >= 0x1FFF) {
		/* errno = VBI_ERR_INVALID_PID; */
		return NULL;
	}

	mx = vbi_dvb_pes_mux_new (callback, user_data);

	if (NULL != mx) {
		mx->pid = pid;
	}

	return mx;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
