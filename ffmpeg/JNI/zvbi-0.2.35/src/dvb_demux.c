/*
 *  libzvbi -- DVB VBI demultiplexer
 *
 *  Copyright (C) 2004, 2006, 2007 Michael H. Schimek
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

/* $Id: dvb_demux.c,v 1.24 2013/07/10 23:12:35 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>

#include "misc.h"		/* CLEAR() */
#include "hamm.h"		/* vbi_rev8() */
#include "dvb.h"
#include "dvb_demux.h"

/**
 * @addtogroup DVBDemux DVB VBI demultiplexer
 * @ingroup LowDec
 * @brief Extracting VBI data from a DVB PES or TS stream.
 *
 * These functions extract raw and/or sliced VBI data from a DVB Packetized
 * Elementary Stream or Transport Stream as defined in EN 300 472 "Digital
 * Video Broadcasting (DVB); Specification for conveying ITU-R System B
 * Teletext in DVB bitstreams" and EN 301 775 "Digital Video Broadcasting
 * (DVB); Specification for the carriage of Vertical Blanking Information
 * (VBI) data in DVB bitstreams".
 *
 * Note EN 300 468 "Specification for Service Information (SI) in DVB
 * systems" defines another method to transmit VPS data in DVB streams.
 * Libzvbi does not provide functions to decode SI tables but the
 * vbi_decode_dvb_pdc_descriptor() function is available to convert a PDC
 * descriptor to a VPS PIL.
 */

/* XXX Preliminary. */
enum {
	VBI_ERR_BUFFER_OVERFLOW		= 0x7080600,
	VBI_ERR_SLICED_BUFFER_OVERFLOW	= 0x7080601,
	VBI_ERR_RAW_BUFFER_OVERFLOW,
	VBI_ERR_SYNC_LOST		= 0x7080700,
	VBI_ERR_SCRAMBLED		= 0x7080800,
	VBI_ERR_STREAM_SYNTAX		= 0x7080900,
	VBI_ERR_DU_OVERFLOW		= 0x7080901,
	VBI_ERR_DU_LENGTH,
	VBI_ERR_DU_LINE_NUMBER,
	VBI_ERR_DU_RAW_SEGMENT_POSITION,
	VBI_ERR_DU_RAW_SEGMENT_LOST,
	VBI_ERR_DU_RAW_DATA_INCOMPLETE,
	VBI_ERR_CALLBACK		= 0x7080a00,
};

struct wrap {
	/* Size must be >= maximum consume + maximum lookahead. */
	uint8_t	*		buffer;

	/* End of data in buffer (exclusive). */
	uint8_t *		bp;

	/* See below. */
	unsigned int		skip;
	unsigned int		consume;
	unsigned int		lookahead;

	/* Unconsumed data in the buffer, beginning at bp - leftover
	   and ending at bp. */
	unsigned int		leftover;
};

/**
 * @internal
 * @param w Wrap-around context.
 * @param dst Wrapped data pointer.
 * @param scan_end End of lookahead range.
 * @param src Source buffer pointer, will be incremented.
 * @param src_left Bytes left in source buffer, will be decremented.
 * @param src_size Size of source buffer.
 *
 * A buffer is assumed in memory at *src + *src_left - src_size, with
 * src_size. This function reads at most *src_left bytes from this
 * buffer starting at *src, incrementing *src and decrementing *src_left
 * by the number of bytes read. NOTE *src_left must be equal to src_size
 * when you change buffers.
 *
 * It removes (reads) w->skip bytes from the buffer and sets w->skip to
 * zero, then removes w->consume bytes (not implemented at this time,
 * assumed to be zero), copying the data AND the following w->lookahead
 * bytes to an output buffer. In other words, *src is incremented by
 * at most w->skip + w->consume bytes.
 *
 * On success TRUE is returned, *dst will point to the begin of the
 * copied data (w->consume + w->lookahead), *scan_end to the end.
 * However *scan_end - *dst can be greater than w->consume + w->lookahead
 * if *src_left permits this. NOTE if copying can be avoided *dst and
 * *scan_end may point into the source buffer, so don't free /
 * overwrite it prematurely. *src_left will be >= 0.
 *
 * w->skip, w->consume and w->lookahead can change between successful
 * calls.
 *
 * If more data is needed the function returns FALSE, and *src_left
 * will be 0.
 */
_vbi_inline vbi_bool
wrap_around			(struct wrap *		w,
				 const uint8_t **	dst,
				 const uint8_t **	scan_end,
				 const uint8_t **	src,
				 unsigned int *		src_left,
				 unsigned int		src_size)
{
	unsigned int available;
	unsigned int required;

	if (w->skip > 0) {
		/* w->skip is not w->consume to save copying. */

		if (w->skip > w->leftover) {
			w->skip -= w->leftover;
			w->leftover = 0;

			if (w->skip > *src_left) {
				w->skip -= *src_left;

				*src += *src_left;
				*src_left = 0;

				return FALSE;
			}

			*src += w->skip;
			*src_left -= w->skip;
		} else {
			w->leftover -= w->skip;
		}

		w->skip = 0;
	}

	available = w->leftover + *src_left;
	required = /* w->consume + */ w->lookahead;

	if (required > available || available > src_size) {
		/* Not enough data at src, or we have bytes left
		   over from the previous buffer, must wrap. */

		if (required > w->leftover) {
			/* Need more data in the wrap_buffer. */

			memmove (w->buffer, w->bp - w->leftover, w->leftover);
			w->bp = w->buffer + w->leftover;

			required -= w->leftover;

			if (required > *src_left) {
				memcpy (w->bp, *src, *src_left);
				w->bp += *src_left;

				w->leftover += *src_left;

				*src += *src_left;
				*src_left = 0;

				return FALSE;
			}

			memcpy (w->bp, *src, required);
			w->bp += required;

			w->leftover = w->lookahead;

			*src += required;
			*src_left -= required;

			*dst = w->buffer;
			*scan_end = w->bp - w->lookahead;
		} else {
			*dst = w->bp - w->leftover;
			*scan_end = w->bp - w->lookahead;

			/* w->leftover -= w->consume; */
		}
	} else {
		/* All the required bytes are in this frame and
		   we have a complete copy of the w->buffer
		   leftover bytes before src. */

		*dst = *src - w->leftover;
		*scan_end = *src + *src_left - w->lookahead;

		/* if (w->consume > w->leftover) {
			unsigned int advance;

			advance = w->consume - w->leftover;

			*src += advance;
			*src_left -= advance;

			w->leftover = 0;
		} else {
			w->leftover -= w->consume;
		} */
	}

	return TRUE;
}

/** @internal */
struct frame {
	/**
	 * Buffer for decoded sliced VBI data. As usual @a sliced_end
	 * is exclusive. Can be @c NULL if no sliced data is needed.
	 */
	vbi_sliced *		sliced_begin;
	vbi_sliced *		sliced_end;

	/** Next free (current) element in the sliced data buffer. */
	vbi_sliced *		sp;

	/**
	 * Buffer for decoded raw VBI data. This is an array of
	 * @a raw_count[0] + @a raw_count[1] lines, with 720 8 bit
	 * luma samples in each line (13.5 MHz sampling rate). Can be
	 * @c NULL if no raw data is needed.
	 */
	uint8_t *		raw;

	/**
	 * The frame lines covered by the raw array, first and second
	 * field respectively.
	 * XXX to be replaced by struct vbi_sampling_par.
	 */
	unsigned int		raw_start[2];
	unsigned int		raw_count[2];

	/**
	 * Pointer to the start of the current line in the @a raw
	 * VBI buffer.
	 */
	uint8_t *		rp;

	/**
	 * Data units can contain at most 251 bytes of payload,
	 * so raw VBI data is transmitted in segments. This field
	 * contains the number of raw VBI samples extracted so
	 * far, is zero before the first and after the last segment
	 * was extracted.
	 */
	unsigned int		raw_offset;

	/**
	 * The field (0 = first or 1 = second) and line (0 = unknown,
	 * 1 ... 31) number found in the last data unit (field_parity,
	 * line_offset).
	 */
	unsigned int		last_field;
	unsigned int		last_field_line;

	/**
	 * A frame line number calculated from @a last_field and
	 * @a last_field_line, or the next available line if
	 * @a last_field_line is zero. Initially zero.
	 */
	unsigned int		last_frame_line;

	/**
	 * The data_unit_id found in the last data unit. Initially
	 * zero.
	 */
	unsigned int		last_data_unit_id;

	/**
	 * The number of data units which have been extracted
	 * from the current PES packet.
	 */
	unsigned int		n_data_units_extracted_from_packet;

	_vbi_log_hook		log;
};

/* Minimum lookahead required to identify the packet header. */
#define PES_HEADER_LOOKAHEAD 48u
#define TS_HEADER_LOOKAHEAD 10u

/* Minimum lookahead required for a TS sync_byte search. */
#define TS_SYNC_SEARCH_LOOKAHEAD (188u + TS_HEADER_LOOKAHEAD - 1u)

/* Round x up to a cache friendly value. */
#define ALIGN(x) ((x + 15) & ~15)

typedef vbi_bool
demux_packet_fn			(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left);

/** @internal */
struct _vbi_dvb_demux {
	/**
	 * PES wrap-around buffer. Must hold one PES packet,
	 * at most 6 + 65535 bytes (start_code[24], stream_id[8],
	 * PES_packet_length[16], max. PES_packet_length).
	 */
	uint8_t			pes_buffer[ALIGN (6 + 65536)];

	/**
	 * TS wrap-around buffer. Must hold one TS packet for
	 * sync_byte search (188 bytes), plus 9 bytes so we
         * can safely examine the header of the contained PES packet.
	 */
	uint8_t			ts_buffer[ALIGN (TS_SYNC_SEARCH_LOOKAHEAD)];

	/** Output buffer for vbi_dvb_demux_demux(). */
	vbi_sliced		sliced[64];

	/** Wrap-around state. */
	struct wrap		pes_wrap;
	struct wrap		ts_wrap;

	/** Data unit demux state. */
	struct frame		frame;

	/** PTS of current frame. */
	int64_t			frame_pts;

	/** PTS of current PES packet. */
	int64_t			packet_pts;

	/**
	 * A new frame commences in the current PES packet. We remember
	 * this for the next call and return, cannot reset immediately
	 * due to the coroutine design.
	 */
	vbi_bool		new_frame;

	/**
	 * The TS demuxer synchonized in the last iteration. The next
	 * incomming byte should be a sync_byte.
	 */
	vbi_bool		ts_in_sync;

	/** Data units to be extracted from the pes_buffer. */
	const uint8_t *		ts_frame_bp;
	unsigned int		ts_frame_todo;

	/** Payload to be copied from TS to pes_buffer. */
	uint8_t *		ts_pes_bp;
	unsigned int		ts_pes_todo;

	/**
	 * Next expected transport_packet continuity_counter.
	 * Value may be greater than 15, so you must compare
	 * modulo 16. -1 if unknown.
	 */
	int			ts_continuity;

	/** PID of VBI data to be filtered out of a TS. */
	unsigned int		ts_pid;

	/** demux_pes_packet() or demux_ts_packet(). */
	demux_packet_fn *	demux_packet;

	/** For vbi_dvb_demux_demux(). */
	vbi_dvb_demux_cb *	callback;
	void *			user_data;
};

enum systems {
	SYSTEM_525 = 0,
	SYSTEM_625
};

static void
log_block			(vbi_dvb_demux *	dx,
				 const uint8_t *	src,
				 unsigned int		n_bytes)
{
	char buffer[16 * 3 + 1];
	unsigned int i;

	if (0 == n_bytes)
		return;

	for (;;) {
		for (i = 0; i < MIN (n_bytes, 16u); ++i)
			snprintf (buffer + i * 3, 4, "%02x ", src[i]);

		debug2 (&dx->frame.log, "%p: %s", src, buffer);

		if (n_bytes < 16)
			break;

		src += 16;
		n_bytes -= 16;
	}
}

/**
 * @internal
 * @param field The field number (0 == first, 1 == second) will be
 *   stored here.
 * @param field_line The field line number or 0 == undefined will
 *   be stored here.
 * @param frame_line The frame line number or 0 == undefined will
 *   be stored here.
 * @param lofp line_offset / field_parity byte of the data unit
 *   in question.
 * @param system SYSTEM_525 or SYSTEM_625. Used to calculate the
 *   frame line number.
 *
 * Converts the line_offset / field_parity byte of a VBI data unit.
 * Caller must validate the line numbers.
 */
static void
lofp_to_line			(unsigned int *		field,
				 unsigned int *		field_line,
				 unsigned int *		frame_line,
				 unsigned int		lofp,
				 enum systems		system)
{
	unsigned int line_offset;

	/* field_parity */
	*field = !(lofp & (1 << 5));

	line_offset = lofp & 31;

	if (line_offset > 0) {
		static const unsigned int field_start [2][2] = {
			{ 0, 263 },
			{ 0, 313 },
		};

		*field_line = line_offset;
		*frame_line = field_start[system][*field] + line_offset;
	} else {
		/* EN 300 775 section 4.5.2: Unknown line. (This is
		   only permitted for Teletext data units.) */

		*field_line = 0;
		*frame_line = 0;
	}
}

/**
 * @internal
 * @param f VBI data unit decoding context.
 * @param spp A pointer to a vbi_sliced structure will be stored here.
 * @param rpp If not @c NULL, a pointer to a raw VBI line (720 bytes)
 *   will be stored here.
 * @param lofp line_offset / field_parity byte of the data unit
 *   in question.
 * @param system SYSTEM_525 or SYSTEM_625. Used to calculate frame
 *   line numbers.
 *
 * Decodes the line_offset / field_parity (lofp) byte of a VBI data
 * unit, allocating a vbi_sliced (and if requested raw VBI data)
 * slot where the data can be stored.
 *
 * Side effects: On success f->sp will be incremented, the field
 * number (0 == first, 1 == second) will be stored in f->last_field,
 * the field line number or 0 == undefined in f->last_field_line,
 * a frame line number or 0 == undefined in (*spp)->line. The
 * caller must validate the line numbers.
 *
 * @returns
 * - @c 0 Success.
 * - @c -1 A line number wrap-around has occurred, i.e. a new frame
 *   commences at the start of this PES packet. No slots have been
 *   allocated.
 * - @c VBI_ERR_SLICED_BUFFER_OVERFLOW
 *   - Insufficient space in the sliced VBI buffer.
 * - @c VBI_ERR_RAW_BUFFER_OVERFLOW
 *   - The line number is outside the area covered by the raw VBI buffer.
 * - @c VBI_ERR_DU_LINE_NUMBER
 *   - Duplicate line number.
 *   - Wrong order of line numbers.
 *   - Illegal line_offset.
 */
static int
line_address			(struct frame *		f,
				 vbi_sliced **		spp,
				 uint8_t **		rpp,
				 unsigned int		lofp,
				 enum systems		system)
{
	unsigned int field;
	unsigned int field_line;
	unsigned int frame_line;

	if (unlikely (f->sp >= f->sliced_end)) {
		error (&f->log,
		       "Out of sliced VBI buffer space (%d lines).",
		       (int)(f->sliced_end - f->sliced_begin));

		return VBI_ERR_SLICED_BUFFER_OVERFLOW;
	}

	lofp_to_line (&field, &field_line, &frame_line,
		      lofp, system);

	debug2 (&f->log, "Line %u/%u=%u.",
		field, field_line, frame_line);

	/* EN 301 775 section 4.1: A VBI PES packet contains data of
	   one and only one video frame. (But multiple packets may
	   contain data of one frame. In particular I encountered
	   streams where the first and second field lines were
	   transmitted in separate successive packets.)

	   Section 4.1 and 4.5.2: Lines in a frame must be transmitted
	   in ascending order of line numbers, with no duplicates,
	   except for lines with line_offset 0 = undefined and monochrome
	   4:2:2 samples which are transmitted in multiple data units.

	   Section 4.5.2: The toggling of the field_parity flag
	   indicates a new field. (Actually this wouldn't work if
	   Teletext data for only one field is transmitted, or just
	   one line of Teletext, VPS, WSS or CC data. So we take
	   the line_offset into account as well.) */
	if (0 != frame_line) {
		if (frame_line <= f->last_frame_line) {
			if (f->n_data_units_extracted_from_packet > 0) {
				notice (&f->log,
					"Illegal line order: %u <= %u.",
					frame_line, f->last_frame_line);

				return VBI_ERR_DU_LINE_NUMBER;
			}

			if (frame_line < f->last_frame_line)
				return -1; /* new_frame */

			/* Not raw VBI or
			   first_segment_flag set? */
			if (NULL == rpp || (int8_t) lofp < 0)
				return -1; /* new_frame */
		}

		if (NULL != rpp) {
			unsigned int raw_start;
			unsigned int raw_end;
			unsigned int offset;

			raw_start = f->raw_start[field];
			raw_end = raw_start + f->raw_count[field];

			if (frame_line < raw_start
			    || frame_line >= raw_end) {
				notice (&f->log,
					"Raw line %u/%u=%u outside "
					"sampling range %u ... %u, "
					"%u ... %u.",
					field,
					field_line,
					frame_line,
					f->raw_start[0],
					f->raw_start[0] + f->raw_count[0],
					f->raw_start[1],
					f->raw_start[1] + f->raw_count[1]);

				return VBI_ERR_RAW_BUFFER_OVERFLOW;
			}

			offset = frame_line - raw_start;
			if (field > 0)
				offset += f->raw_count[0];

			*rpp = f->raw + offset * 720;
		}

		f->last_field = field;
		f->last_field_line = field_line;
		f->last_frame_line = frame_line;

		*spp = f->sp++;
		(*spp)->line = frame_line;
	} else {
		/* Undefined line. */

		if (NULL != rpp) {
			/* EN 301 775 section 4.9.2. */
			notice (&f->log,
				"Illegal raw VBI line_offset=0.");

			return VBI_ERR_DU_LINE_NUMBER;
		}

		if (0 == f->last_data_unit_id) {
			/* Nothing to do. */
		} else if (field != f->last_field) {
			if (0 == f->n_data_units_extracted_from_packet)
				return -1; /* new frame */

			if (unlikely (field < f->last_field)) {
				notice (&f->log,
					"Illegal line order: %u/x <= %u/x.",
					field, f->last_field);

				return VBI_ERR_DU_LINE_NUMBER;
			}
		}

		f->last_field = field;
		f->last_field_line = field_line;

		*spp = f->sp++;
		(*spp)->line = 0;
	}

	++f->n_data_units_extracted_from_packet;

	return 0; /* success */
}

static void
discard_raw			(struct frame *		f)
{
	debug2 (&f->log, "Discarding raw VBI line.");

	memset (f->rp, 0, 720);

	--f->sp;

	f->raw_offset = 0;
}

static int
demux_samples			(struct frame *		f,
				 const uint8_t *	p,
				 enum systems		system)
{
	unsigned int first_pixel_position;
	unsigned int n_pixels;

	first_pixel_position = p[3] * 256 + p[4];
	n_pixels = p[5];

	debug2 (&f->log,
		"Raw VBI data unit first_segment=%u last_segment=%u "
		"field_parity=%u line_offset=%u "
		"first_pixel_position=%u n_pixels=%u.",
		!!(p[2] & (1 << 7)),
		!!(p[2] & (1 << 6)),
		!!(p[2] & (1 << 5)),
		p[2] & 0x1F,
		first_pixel_position,
		n_pixels);

	/* EN 301 775 section 4.9.1: first_pixel_position 0 ... 719,
	   n_pixels 1 ... 251. (n_pixels <= 251 has been checked by
	   caller.) */
	if (unlikely (0 == n_pixels || first_pixel_position >= 720)) {
		notice (&f->log,
			"Illegal raw VBI segment size "
			"%u ... %u (%u pixels).",
			first_pixel_position,
			first_pixel_position + n_pixels,
			n_pixels);

		discard_raw (f);

		return VBI_ERR_DU_RAW_SEGMENT_POSITION;
	}

	/* first_segment_flag */
	if ((int8_t) p[2] < 0) {
		vbi_sliced *s;
		int err;

		if (unlikely (f->raw_offset > 0)) {
			s = f->sp - 1;

			debug2 (&f->log,
				"Raw VBI segment missing in "
				"line %u at offset %u.",
				s->line, f->raw_offset);

			discard_raw (f);

			return VBI_ERR_DU_RAW_DATA_INCOMPLETE;
		}

		err = line_address (f, &s, &f->rp, p[2], system);
		if (unlikely (0 != err))
			return err;

		if (unlikely (f->last_field_line - 7 >= 24 - 7)) {
			--f->sp;

			notice (&f->log,
				"Illegal raw VBI line_offset=%u.",
				f->last_field_line);

			return VBI_ERR_DU_LINE_NUMBER;
		}

		s->id = (SYSTEM_525 == system) ?
			VBI_SLICED_VBI_525 : VBI_SLICED_VBI_625;
	} else {
		unsigned int field;
		unsigned int field_line;
		unsigned int frame_line;
		vbi_sliced *s;

		lofp_to_line (&field, &field_line, &frame_line,
			      p[2], system);

		if (unlikely (0 == f->raw_offset)) {
			/* Don't complain if we just jumped into the
			   stream or discarded the previous segments. */
			switch (f->last_data_unit_id) {
			case 0:
			case DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525:
			case DATA_UNIT_MONOCHROME_SAMPLES:
				return 0; /* success, skip the data unit */

			default:
				break;
			}

			debug2 (&f->log,
				"First raw VBI segment missing in "
				"line %u before offset %u.",
				frame_line, first_pixel_position);

			return VBI_ERR_DU_RAW_SEGMENT_LOST;
		}

		s = f->sp - 1;

		/* EN 301 775 section 4.9.2. */
		if (unlikely (frame_line != s->line
			      || first_pixel_position != f->raw_offset)) {
			debug2 (&f->log,
				"Raw VBI segment(s) missing or "
				"out of order. Expected data for "
				"line %u offset %u, "
				"got line %u offset %u.",
				s->line, f->raw_offset,
				frame_line, first_pixel_position);

			discard_raw (f);

			return VBI_ERR_DU_RAW_SEGMENT_LOST;
		}
	}

	/* EN 301 775 section 4.9 defines a video line as 720
	   luminance samples, but doesn't actually require the
	   transmission of exactly 720 samples starting at offset 0.
	   We discard any samples beyond offset 719. */
	n_pixels = MIN (n_pixels, 720 - first_pixel_position);

	memcpy (f->rp + first_pixel_position, p + 6, n_pixels);

	/* last_segment_flag */
	if (0 != (p[2] & (1 << 6))) {
		f->raw_offset = 0;
	} else {
		f->raw_offset = first_pixel_position + n_pixels;
	}

	return 0; /* success */
}

static void
log_du_ttx			(struct frame *		f,
				 const vbi_sliced *	s)
{
	uint8_t buffer[43];
	unsigned int i;

	for (i = 0; i < 42; ++i)
		buffer[i] = _vbi_to_ascii (s->data[i]);
	buffer[i] = 0;

	debug2 (&f->log, "DU-TTX %u >%s<", s->line, buffer);
}

/**
 * @internal
 * @param f VBI data unit decoding context.
 * @param src *src must point to the first byte (data_unit_id) of
 *   a VBI data unit. Initially this should be the first data unit
 *   in the PES packet, immediately after the data_indentifier byte.
 *   @a *src will be incremented by the size of the successfully
 *   converted data units, pointing to the end of the buffer on
 *   success, or the data_unit_id byte of the offending data unit
 *   on failure.
 * @param src_left *src_left is the number of bytes left in the
 *   @a src buffer. It will be decremented by the size of the
 *   successfully converted data units. When all data units in the
 *   buffer have been successfully converted it will be zero.
 *
 * Converts the data units in a VBI PES packet to vbi_sliced data
 * stored in f->sliced_begin (if not @c NULL) or raw VBI samples
 * stored in f->raw (if not @c NULL).
 *
 * The function skips over unknown data units, stuffing bytes and
 * data units which contain data which was not requested. It aborts
 * and returns an error code when it encounters a non-standard
 * conforming data unit. All errors are recoverable, you can just call
 * the function again, perhaps after calling _vbi_dvb_skip_data_unit().
 *
 * You must set f->n_data_units_extracted_from_packet to zero
 * at the beginning of a new PES packet.
 *
 * @returns
 * - @c 0 Success, next PES packet please.
 * - @c -1 A new frame commences at the start of this PES packet.
 *   No data units were extracted. Flush the output buffers, then
 *   call reset_frame() and then call this function again to convert
 *   the remaining data units.
 * - @c VBI_ERR_SLICED_BUFFER_OVERFLOW
 *   - Insufficient space in the sliced VBI buffer.
 * - @c VBI_ERR_RAW_BUFFER_OVERFLOW
 *   - The line number in the data unit is outside the area covered
 *     by the raw VBI buffer.
 * - @c VBI_ERR_DU_OVERFLOW
 *   - The data unit crosses the end of the buffer.
 * - @c VBI_ERR_DU_LENGTH
 *   - The data_unit_length is too small for the data this unit is
 *     supposed to contain.
 * - @c VBI_ERR_DU_LINE_NUMBER
 *   - Duplicate line number.
 *   - Wrong order of line numbers.
 *   - Illegal line_offset.
 * - @c VBI_ERR_DU_RAW_SEGMENT_POSITION
 *   - Illegal first_pixel_position or n_pixels field in a
 *     monochrome 4:2:2 samples data unit. (Only if raw VBI data
 *     was requested.)
 * - @c VBI_ERR_DU_RAW_SEGMENT_LOST
 *   - The first or following segments of a monochrome 4:2:2 samples
 *     line are missing or out of order. (Only if raw VBI data
 *     was requested.)
 * - @c VBI_ERR_DU_RAW_DATA_INCOMPLETE
 *   - The last segment of a monochrome 4:2:2 samples line is missing.
 *     (Only if raw VBI data was requested.) DO NOT SKIP over this
 *     data unit, it may be valid.
 *
 * @bugs
 * Raw VBI conversion is untested.
 */
static int
extract_data_units		(struct frame *		f,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	const uint8_t *p;
	const uint8_t *p_end_m2;
	int err = 0;

	assert (*src_left >= 2);

	p = *src;
	p_end_m2 = p + *src_left
		- 2; /* data_unit_id, data_unit_length */

	while (p < p_end_m2) {
		unsigned int data_unit_id;
		unsigned int data_unit_length;
		vbi_sliced *s;
		unsigned int i;

		data_unit_id = p[0];
		data_unit_length = p[1];

		debug2 (&f->log,
			"data_unit_id=0x%02x data_unit_length=%u.",
			data_unit_id, data_unit_length);

		/* Data units must not cross PES packet
		   boundaries, as is evident from
		   EN 301 775 table 1. */
		if (unlikely (p + data_unit_length > p_end_m2)) {
			err = VBI_ERR_DU_OVERFLOW;
			goto failed;
		}

		switch (data_unit_id) {
		case DATA_UNIT_STUFFING:
			break;

		case DATA_UNIT_EBU_TELETEXT_NON_SUBTITLE:
		case DATA_UNIT_EBU_TELETEXT_SUBTITLE:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 1 + 42))
				goto bad_length;

			/* FIXME */
			if (unlikely (0xE4 != p[3])) { /* vbi_rev8 (0x27) */
			        warning (&f->log,
					 "Libzvbi does not support "
					 "Teletext services with "
					 "custom framing code.");
				break;
			}

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			if (unlikely (f->last_field_line > 0
				      && (f->last_field_line - 7
					  >= 23 - 7)))
				goto bad_line;

			/* XXX the data will always be in correct order,
			   but if line_offset is 0 (undefined) we cannot
			   pass the (always valid) field number. */
			s->id = VBI_SLICED_TELETEXT_B;

			for (i = 0; i < 42; ++i)
				s->data[i] = vbi_rev8 (p[4 + i]);

			if (f->log.mask & VBI_LOG_DEBUG2)
				log_du_ttx (f, s);

			break;

		case DATA_UNIT_VPS:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 13))
				goto bad_length;

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			if (unlikely (16 != s->line))
				goto bad_line;

			s->id = (0 == f->last_field) ?
				VBI_SLICED_VPS : VBI_SLICED_VPS_F2;

			memcpy (s->data, p + 3, 13);

			break;

		case DATA_UNIT_WSS:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 2))
				goto bad_length;

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			if (unlikely (23 != s->line))
				goto bad_line;

			s->id = VBI_SLICED_WSS_625;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

		case DATA_UNIT_ZVBI_WSS_CPR1204:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 3))
				goto bad_length;

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_525);
			if (unlikely (0 != err))
				goto failed;

			s->id = VBI_SLICED_WSS_CPR1204;

			s->data[0] = p[3];
			s->data[1] = p[4];
			s->data[2] = p[5];

			break;

		case DATA_UNIT_ZVBI_CLOSED_CAPTION_525:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0))
				goto raw_missing;

			if (unlikely (data_unit_length < 1 + 2))
				goto bad_length;

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_525);
			if (unlikely (0 != err))
				goto failed;

			s->id = (0 == f->last_field) ?
				VBI_SLICED_CAPTION_525_F1 :
				VBI_SLICED_CAPTION_525_F2;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

		case DATA_UNIT_CLOSED_CAPTION:
			if (NULL == f->sliced_begin)
				break;

			if (unlikely (f->raw_offset > 0)) {
			raw_missing:
				s = f->sp - 1;

				debug2 (&f->log,
					"Raw VBI segment missing in "
					"line %u at offset %u.",
					s->line, f->raw_offset);

				discard_raw (f);

				return VBI_ERR_DU_RAW_DATA_INCOMPLETE;
				goto failed;
			}

			if (unlikely (data_unit_length < 1 + 2)) {
			bad_length:
				notice (&f->log,
					"data_unit_length=%u too small "
					"for data_unit_id=%u.",
					data_unit_length, data_unit_id);

				err = VBI_ERR_DU_LENGTH;
				goto failed;
			}

			err = line_address (f, &s, /* rpp */ NULL,
					    p[2], SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			if (unlikely (21 != s->line)) {
			bad_line:
				--f->sp;

				notice (&f->log,
					"Illegal field_parity=%u or "
					"line_offset=%u for "
					"data_unit_id=%u.",
					!f->last_field,
					f->last_field_line,
					data_unit_id);

				err = VBI_ERR_DU_LINE_NUMBER;
				goto failed;
			}

			s->id = (0 == f->last_field) ?
				VBI_SLICED_CAPTION_625_F1 :
				VBI_SLICED_CAPTION_625_F2;

			s->data[0] = vbi_rev8 (p[3]);
			s->data[1] = vbi_rev8 (p[4]);

			break;

		case DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525:
			if (NULL == f->raw)
				break;

			if (unlikely (data_unit_length
				      < (unsigned int)(1 + 2 + 1 + p[5])))
				goto bad_sample_length;

			err = demux_samples (f, p, SYSTEM_525);
			if (unlikely (0 != err))
				goto failed;

			break;

		case DATA_UNIT_MONOCHROME_SAMPLES:
			if (NULL == f->raw)
				break;

			if (unlikely (data_unit_length <
				      (unsigned int)(1 + 2 + 1 + p[5]))) {
			bad_sample_length:
				notice (&f->log,
					"data_unit_length=%u too small "
					"for data_unit_id=%u with %u "
					"samples.",
					data_unit_length,
					data_unit_id, p[5]);

				err = VBI_ERR_DU_LENGTH;
				goto failed;
			}

			/* Actually EN 301 775 section 4.9: "The data
			   is intended to be transcoded into the VBI
			   of either 525 or 625-line video." What's
			   that supposed to mean? */
			err = demux_samples (f, p, SYSTEM_625);
			if (unlikely (0 != err))
				goto failed;

			break;

		default:
			notice (&f->log,
				"Unknown data_unit_id=%u.",
				data_unit_id);
			break;
		}

		f->last_data_unit_id = data_unit_id;

		p += data_unit_length + 2;
	}

	*src = p;
	*src_left = 0;

	return 0; /* success */

 failed:
	/* Also called with err = -1 when a new frame begins in
	   this packet, before any data units were extracted. */

	*src_left = p_end_m2 + 2 - p;
	*src = p;

	return err;
}

/**
 * @internal
 * @param f VBI data unit decoding context.
 *
 * Reset the VBI data unit decoding context at the beginning of a
 * new frame (after extract_data_units() returned -1).
 */
_vbi_inline void
reset_frame			(struct frame *		f)
{
	f->sp = f->sliced_begin;

	/* Take a shortcut if no raw data was ever stored here. */
	if (f->rp > f->raw) {
		unsigned int n_lines;

		n_lines = f->raw_count[0] + f->raw_count[1];
		memset (f->raw, 0, n_lines * 720);
	}

	f->rp = f->raw;
	f->raw_offset = 0;

	f->last_field = 0;
	f->last_field_line = 0;
	f->last_frame_line = 0;

	f->last_data_unit_id = 0;

	f->n_data_units_extracted_from_packet = 0;
}

/**
 * @internal
 * @param buffer *buffer points to the first byte (data_unit_id) of
 *   a VBI data unit. On success it will be incremented by the size
 *   of the data unit, pointing to the next data_unit_id byte.
 * @param buffer_left *buffer_left is the number of bytes left in
 *   @a buffer. On success it will be decremented by the size of the
 *   data unit.
 *
 * Skips over a data unit in a VBI PES packet. The function does
 * not validate the data unit.
 *
 * This is low-level function. DO NOT call it on the raw PES or TS
 * streams consumed by vbi_dvb_demux_cor() or vbi_dvb_demux_feed().
 *
 * @returns
 * @c TRUE on success. @c FALSE if @a *buffer_left is too small to
 * contain a data unit, or if the data unit pointed to by @a *buffer
 * crosses the end of the buffer. @a *buffer and @a *buffer_left
 * remain unchanged in this case.
 */
vbi_bool
_vbi_dvb_skip_data_unit		(const uint8_t **	buffer,
				 unsigned int *		buffer_left)
{
	const uint8_t *src;
	unsigned int src_left;
	unsigned int skip;

	assert (NULL != buffer);
	assert (NULL != buffer_left);

	src = *buffer;
	src_left = *buffer_left;

	if (NULL == src || src_left < 2) {
		/* errno = VBI_ERR_DU_OVERFLOW; */
		return FALSE;
	}

	/* data_unit_id, data_unit_length, data[data_unit_length] */
	skip = 2 + src[1];

	if (skip > src_left) {
		/* errno = VBI_ERR_DU_OVERFLOW; */
		return FALSE;
	}

	*buffer = src + skip;
	*buffer_left = src_left - skip;

	return TRUE;
}

/**
 * @internal
 * @param sliced Converted sliced data will be stored here.
 * @param n_lines The number of elements stored in the @a sliced array
 *   so far will be stored here.
 * @param max_lines The capacity of the @a sliced array.
 * @param buffer *buffer points to the first byte (data_unit_id) of
 *   a VBI data unit. Initially this should be the first data unit
 *   in the PES packet, immediately after the data_indentifier byte.
 *   @a *buffer will be incremented by the size of the successfully
 *   converted data units, pointing to the end of the buffer on
 *   success, or the data_unit_id byte of the offending data unit
 *   on failure.
 * @param buffer_left *buffer_left is the number of bytes left in
 *   @a buffer. It will be decremented by the size of the successfully
 *   converted data units. When all data units in the buffer have been
 *   successfully converted it will be zero.
 *
 * Converts the data units in a VBI PES packet to vbi_sliced data
 * stored in the @a buffer array. Monochrome 4:2:2 samples data
 * units, unknown data units and stuffing bytes are skipped.
 *
 * The function aborts and returns an error code when it encounters
 * a non-standard conforming data unit. All errors are recoverable,
 * you can just call the function again, perhaps after calling
 * _vbi_dvb_skip_data_unit().
 *
 * @returns
 * @c TRUE if all data units in the buffer have been successfully
 * converted. @c FALSE on failure:
 * - @a *buffer is @c NULL or @a *buffer_left is too small to
 *   contain a data unit.
 * - Insufficient space in the @a sliced buffer, @a n_lines will be
 *   equal to @a max_lines. Remedy: call this function again to
 *   convert the remaining data units.
 * - The data unit crosses the end of the buffer. Remedy: discard
 *   the PES packet (EN 301 472 and EN 301 775 do not permit data
 *   units to cross a PES packet boundary), or ignore this error, or
 *   call this function again with more data.
 * - The data_unit_length is too small for the data this unit is
 *   supposed to contain. Remedy: discard the PES packet, or skip this
 *   data unit and call this function again to convert the remaining
 *   data units.
 * - The data unit contains a duplicate line number. Remedy: as above.
 * - The line numbers in the data units are in wrong order. Remedy:
 *   as above.
 * - Illegal line_offset. Remedy: as above.
 */
vbi_bool
_vbi_dvb_demultiplex_sliced	(vbi_sliced *		sliced,
				 unsigned int * 	n_lines,
				 unsigned int		max_lines,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left)
{
	struct frame frame;
	int err;

	assert (NULL != sliced);
	assert (NULL != n_lines);
	assert (NULL != buffer);
	assert (NULL != buffer_left);

	if (NULL == *buffer || *buffer_left < 2) {
		/* errno = ? */
		return FALSE;
	}

	CLEAR (frame);

	frame.sliced_begin = sliced;
	frame.sliced_end = sliced + max_lines;
	frame.sp = sliced;

	err = extract_data_units (&frame, buffer, buffer_left);

	*n_lines = frame.sp - frame.sliced_begin;

	return (0 == err);
}

static vbi_bool
decode_timestamp		(vbi_dvb_demux *	dx,
				 int64_t *		pts,
				 unsigned int		mark,
				 const uint8_t *	p)
{
	unsigned int t;

	if (mark != (p[0] & 0xF1u)) {
		debug2 (&dx->frame.log,
			"Invalid PTS/DTS byte[0]=0x%02x.", p[0]);
		return FALSE;
	}

	t  = p[1] << 22;
	t |= (p[2] & ~1) << 14;
	t |= p[3] << 7;
	t |= p[4] >> 1;

	if (dx->frame.log.mask & VBI_LOG_DEBUG) {
		int64_t old_pts;
		int64_t new_pts;

		old_pts = *pts;
		new_pts = t | (((int64_t) p[0] & 0x0E) << 29);

		debug1 (&dx->frame.log,
			"TS%x 0x%" PRIx64 " (%+" PRId64 ").",
			mark, new_pts, new_pts - old_pts);
	}

	*pts = t | (((int64_t) p[0] & 0x0E) << 29);

	return TRUE;
}

static vbi_bool
valid_vbi_pes_packet_header	(vbi_dvb_demux *	dx,
				 const uint8_t *	p)
{
	unsigned int header_length;
	unsigned int data_identifier;

	/* PES_header_data_length [8] */
	header_length = p[8];

	debug1 (&dx->frame.log,
		"PES_header_length=%u (%s).",
		header_length,
		(36 == header_length) ? "ok" : "bad");

	/* EN 300 472 section 4.2: Must be 0x24. */
	if (36 != header_length)
		return FALSE;

	data_identifier = p[9 + 36];

	/* data_identifier (EN 301 775 section 4.3.2) */
	switch (data_identifier) {
	case 0x10 ... 0x1F:
	case 0x99 ... 0x9B:
		debug1 (&dx->frame.log,
			"data_identifier=%u (ok).",
			data_identifier);
		break;

	default:
		debug2 (&dx->frame.log,
			"data_identifier=%u (bad).",
			data_identifier);
		return FALSE;
	}

	/* '10',
	   PES_scrambling_control [2] == '00' (not scrambled),
	   PES_priority,
	   data_alignment_indicator == '1' (data unit
	     starts immediately after header),
	   copyright,
	   original_or_copy */
	if (0x84 != (p[6] & 0xF4)) {
		debug2 (&dx->frame.log,
			"Invalid PES header byte[6]=0x%02x.",
			p[6]);
		return FALSE;
	}

	/* PTS_DTS_flags [2],
	   ESCR_flag,
	   ES_rate_flag,
	   DSM_trick_mode_flag,
	   additional_copy_info_flag,
	   PES_CRC_flag,
	   PES_extension_flag */
	switch (p[7] >> 6) {
	case 2:	/* PTS 0010 xxx 1 ... */
		if (!decode_timestamp (dx, &dx->packet_pts, 0x21, p + 9))
			return FALSE;
		break;

	case 3:	/* PTS 0011 xxx 1 ... DTS ... */
		if (!decode_timestamp (dx, &dx->packet_pts, 0x31, p + 9))
			return FALSE;
		break;

	default:
		/* EN 300 472 section 4.2: a VBI PES packet [...]
		   always carries a PTS. (But we don't need one
		   if this packet continues the previous frame.) */
		debug2 (&dx->frame.log,
			"PTS missing in PES header.");

		/* XXX make this optional to handle broken sources. */
		if (dx->new_frame)
			return FALSE;

		break;
	}

	/* FIXME if this is not the first packet of a frame, and a PTS
	   is present, check if we lost any packets. */

	return TRUE;
}

/**
 * @internal
 * @param dx DVB demultiplexer context.
 * @param src *src must point to the first data unit
 *   in the PES packet, immediately after the data_indentifier byte.
 *   @a *src will be incremented by the size of the successfully
 *   converted data units, pointing to the end of the buffer on
 *   success, or the data_unit_id byte of the offending data unit
 *   on failure.
 * @param src_left *src_left is the number of bytes left in the
 *   @a src buffer. It will be decremented by the size of the
 *   successfully converted data units. When all data units in the
 *   buffer have been successfully converted it will be zero.
 *
 * Converts the data units in a VBI PES packet to vbi_sliced data
 * (if dx->frame.sliced_begin != @c NULL) or raw VBI samples (if
 * dx->frame.raw != @c NULL). When a frame is complete, the function
 * calls dx->callback.
 *
 * You must set f->n_data_units_extracted_from_packet to zero
 * at the beginning of a new PES packet.
 *
 * @returns
 * - @c 0 Success, next PES packet please.
 * - @c VBI_ERR_CALLBACK
 *   - A frame is complete, but dx->callback == @c NULL (not an error,
 *     see vbi_dvb_demux_cor() coroutine) or the callback function
 *     returned @c FALSE. No data units were extracted from this
 *     PES packet. This error is recoverable. Just call this function
 *     again with the returned @a src and @a src_left values to
 *     continue after the failed call.
 * - @c VBI_ERR_SLICED_BUFFER_OVERFLOW
 *   - Insufficient space in the sliced VBI buffer. This error is
 *     recoverable; Make room and call this function again with the
 *     returned @a src and @a src_left values to continue where it
 *     left off.
 * - @c VBI_ERR_RAW_BUFFER_OVERFLOW
 *   - The line number in the data unit is outside the area covered
 *     by the raw VBI buffer. (Only if raw VBI data was requested.)
 *     This error is recoverable. Make room or skip the data unit with
 *     _vbi_dvb_skip_data_unit(), then call again as above.
 * - @c Error in the VBI_ERR_STREAM_SYNTAX range
 *   - The data unit is broken. This error is recoverable. You can
 *     skip the data unit and call again as above.
 *
 * To discard a PES packet after an error occurred, call again with
 * a new @a src pointer. To discard all data units collected before
 * the error occurred, set dx->new_frame to TRUE. VBI_ERR_CALLBACK
 * implies that @a src points to a new PES packet and dx->new_frame
 * will be already TRUE.
 */
static int
demux_pes_packet_frame		(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	for (;;) {
		unsigned int n_lines;
		int err;

		if (dx->new_frame) {
			/* New frame commences in this packet. */

			reset_frame (&dx->frame);

			dx->frame_pts = dx->packet_pts;
			dx->new_frame = FALSE;
		}

		err = extract_data_units (&dx->frame, src, src_left);

		if (likely (err >= 0)) {
			/* Data unit extraction successful, packet
			   continues a previous frame, or an error
			   occurred and *src points at the offending
			   data unit. */
			return err;
		}

		debug1 (&dx->frame.log, "New frame.");

		/* A new frame commences in this packet. We must
		   flush dx->frame before we extract data units from
		   this packet. */

		dx->new_frame = TRUE;

		if (NULL == dx->callback)
			return VBI_ERR_CALLBACK;

		n_lines = dx->frame.sp - dx->frame.sliced_begin;

		if (!dx->callback (dx,
				   dx->user_data,
				   dx->frame.sliced_begin,
				   n_lines,
				   dx->frame_pts)) {
			return VBI_ERR_CALLBACK;
		}
	}

	assert (0);
	return 0;
}

/**
 * @internal
 * @param src *src points to DVB PES data, will be incremented by the
 *   number of bytes read from the buffer. This pointer need not align
 *   with PES packet boundaries.
 * @param src_left *src_left is the number of bytes left in @a src
 *   buffer, will be decremented by the number of bytes read.
 *   *src_left need not align with PES packet boundaries.
 *
 * DVB VBI demultiplexer coroutine for MPEG-2 Packetized Elementary
 * Streams. This function extracts data units from DVB VBI PES packets
 * and calls dx->callback when a frame is complete.
 *
 * The function silently discards all VBI PES packets which contain
 * non-standard conforming data. dx->callback (or the caller if
 * dx->callback == @c NULL) must examine the passed PTS
 * (or dx->frame_PTS) to detect data loss.
 *
 * @returns
 * - @c 0 Success, need more data. *src_left will be zero.
 * - @c VBI_ERR_CALLBACK
 *   - A frame is complete, but dx->callback == @c NULL or the callback
 *     function returned @c FALSE. This error is recoverable. Just
 *     call the function again with the returned @a src and @a
 *     src_left values to continue after the failed call.
 */
static int
demux_pes_packet		(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	const uint8_t *s;
	unsigned int s_left;
	int err = 0;

	s = *src;
	s_left = *src_left;

	for (;;) {
		const uint8_t *p;
		const uint8_t *scan_begin;
		const uint8_t *scan_end;
		unsigned int packet_length;

		if (!wrap_around (&dx->pes_wrap,
				  &p, &scan_end,
				  &s, &s_left, *src_left))
			break; /* out of data */

		/* Data units */

		if (dx->pes_wrap.lookahead > PES_HEADER_LOOKAHEAD) {
			unsigned int left;

			/* We have a new PES packet in the wrap-around
			   buffer. p points just after the data_identifier
			   byte and lookahead is >= packet length minus
			   header and data_identifier byte. */
			left = dx->pes_wrap.lookahead;

			dx->frame.n_data_units_extracted_from_packet = 0;

			err = demux_pes_packet_frame (dx, &p, &left);

			if (VBI_ERR_CALLBACK == err) {
				goto failed;
			} else if (unlikely (err < 0)) {
				/* For compatibility with older
				   versions just discard the data
				   collected so far for this frame. */
				dx->new_frame = TRUE;
			}

			/* Skip this packet and request enough data
			   to look at the next PES header. */
			dx->pes_wrap.skip = dx->pes_wrap.lookahead;
			dx->pes_wrap.lookahead = PES_HEADER_LOOKAHEAD;

			continue;
		}

		/* Start code scan */

		scan_begin = p;

		for (;;) {
			/* packet_start_code_prefix [24] == 0x000001,
			   stream_id [8] == PRIVATE_STREAM_1 */

			debug1 (&dx->frame.log,
				"packet_start_code=%02x%02x%02x%02x.",
				p[0], p[1], p[2], p[3]);

			if (p[2] & ~1) {
				/* Not 000001 or xx0000 or xxxx00. */
				p += 3;
			} else if (0 != (p[0] | p[1]) || 1 != p[2]) {
				++p;
			} else if (PRIVATE_STREAM_1 == p[3]) {
				break;
			} else if (p[3] < 0xBC) {
				++p;
			} else {
				/* ISO/IEC 13818-1 Table 2-19 stream_id
				   assignments: 0xBC ... 0xFF. */

				/* XXX We shouldn't take this shortcut
				   unless we're sure this is a PES packet
				   header and not some random junk, so we
				   don't miss any data. */

				packet_length = p[4] * 256 + p[5];

				/* Not a VBI PES packet, skip it. */
				dx->pes_wrap.skip = (p - scan_begin)
					+ 6 + packet_length;

				goto outer_continue;
			}

			if (unlikely (p >= scan_end)) {
				/* Start code not found within
				   lookahead bytes. Skip the data and
				   request more. */
				dx->pes_wrap.skip = p - scan_begin;
				goto outer_continue;
			}
		}

		/* Packet header */

		packet_length = p[4] * 256 + p[5];

		debug1 (&dx->frame.log,
			"PES_packet_length=%u.",
			packet_length);

		/* Skip this PES packet if the following checks fail. */
		dx->pes_wrap.skip = (p - scan_begin) + 6 + packet_length;

		/* EN 300 472 section 4.2: N x 184 - 6. (We'll read
		   46 bytes without further checks and need at least
		   one data unit to function properly, be that all
		   stuffing bytes.) */
		if (packet_length < 178)
			continue;

		if (!valid_vbi_pes_packet_header (dx, p))
			continue;

		/* Habemus packet. Skip all data up to the header,
		   the PES packet header itself and the data_identifier
		   byte. Request access to the payload bytes. */
		dx->pes_wrap.skip = (p - scan_begin) + 9 + 36 + 1;
		dx->pes_wrap.lookahead = packet_length - 3 - 36 - 1;

 outer_continue:
		;
	}

	*src = s;
	*src_left = s_left;

	return 0; /* need more data */

 failed:
	*src = s;
	*src_left = s_left;

	return err;
}

/**
 * @internal
 * @param src *src points to DVB PES data, will be incremented by the
 *   number of bytes read from the buffer. This pointer need not align
 *   with PES packet boundaries.
 * @param src_left *src_left is the number of bytes left in @a src
 *   buffer, will be decremented by the number of bytes read.
 *   *src_left need not align with PES packet boundaries.
 *
 * DVB VBI demultiplexer coroutine for MPEG-2 Transport Streams.
 * This function extracts DVB VBI Packetized Elementary Stream packets
 * from the TS, VBI data units from the PES packets and calls
 * dx->callback when a frame is complete.
 *
 * The function silently discards all VBI PES packets which contain
 * non-standard conforming data. dx->callback (or the caller if
 * dx->callback == @c NULL) must examine the passed PTS
 * (or dx->frame_PTS) to detect data loss.
 *
 * @returns
 * - @c 0 Success, need more data. *src_left will be zero.
 * - @c VBI_ERR_CALLBACK
 *   - A frame is complete, but dx->callback == @c NULL or the callback
 *     function returned @c FALSE.
 *
 * Future versions may return the following errors. For now the function
 * just discards the respective TS and PES packets.
 * - @c VBI_ERR_SYNC_LOST
 *   - No sync_byte found within the first 188 bytes.
 *   - No sync_byte found after the end of the previous
 *     transport_packet.
 *   - Wrong continuity_counter.
 *   - transport_error_indicator is set.
 * - @c VBI_ERR_SCRAMBLED
 *   - The payload is scrambled.
 * - @c VBI_ERR_STREAM_SYNTAX
 *   - Incorrect TS packet header.
 *   - Incorrect PES packet header or not a VBI stream.
 *
 * All errors are recoverable. Just call the function again with the
 * returned @a src and @a src_left values to continue where it left off.
 */
static int
demux_ts_packet			(vbi_dvb_demux *	dx,
				 const uint8_t **	src,
				 unsigned int *		src_left)
{
	const uint8_t *s;
	unsigned int s_left;
	unsigned int avail;
	unsigned int lookahead;
	const uint8_t *p;
	int err = 0;

	s = *src;
	s_left = *src_left;

	if (0 == s_left) {
		return 0; /* need more data */
	}

	for (;;) {
		unsigned int consume;
		unsigned int fragment;
		unsigned int skip;
		unsigned int adaptation_field_control;
		unsigned int pid;
		uint8_t b1, b3;

		consume = dx->ts_wrap.consume;

		if (consume > 0) {
			/* Copy TS payload into dx->pes_buffer. */

			if (consume > s_left) {
				memcpy (dx->ts_pes_bp, s, s_left);

				dx->ts_pes_bp += s_left;
				dx->ts_pes_todo -= s_left;

				dx->ts_wrap.consume = consume - s_left;

				goto need_more_data_return;
			}

			memcpy (dx->ts_pes_bp, s, consume);

			dx->ts_pes_bp += consume;
			dx->ts_pes_todo -= consume;

			s += consume;
			s_left -= consume;

			/* Got all data from this TS packet. */
			dx->ts_wrap.consume = 0;

			if (0 == dx->ts_pes_todo) {
				const uint8_t *p;
				unsigned int left;

				/* PES packet is complete, let's take
				   a closer look at the header. */

				p = dx->pes_buffer;
				left = dx->ts_pes_bp - dx->pes_buffer;

				if (0)
					log_block (dx, p, left);

				if (!valid_vbi_pes_packet_header (dx, p)) {
					/* Discard the data collected
					   so far. */
					dx->new_frame = TRUE;

					dx->ts_frame_todo = 0;

					if (0) {
						err = VBI_ERR_STREAM_SYNTAX;
						goto error_return;
					} else {
						continue;
					}
				}

				/* Start after data_identifier byte. */
				dx->ts_frame_bp = dx->pes_buffer + 46;

				/* Data units occupy packet length
				   minus PES header length minus the
				   data_identifier byte. */
				dx->ts_frame_todo = left - 46;

				dx->frame.n_data_units_extracted_from_packet =
					0;
			}
		}

		if (dx->ts_frame_todo > 0) {
			/* Extract more data units from the PES
			   packet in dx->pes_buffer. */

			err = demux_pes_packet_frame (dx,
						      &dx->ts_frame_bp,
						      &dx->ts_frame_todo);

			if (VBI_ERR_CALLBACK == err)
				goto error_return;

			if (0 != err) {
				/* Discard the data collected so far. */
				dx->new_frame = TRUE;

				/* Discard the PES packet. */
				dx->ts_frame_todo = 0;

				if (0) {
					goto error_return;
				}
			}

			/* All data units done. */
		}

		/* Skip over 'skip' TS bytes. */
		skip = dx->ts_wrap.skip;

		if (skip > s_left) {
			dx->ts_wrap.skip = skip - s_left;
			goto need_more_data_return;
		}

		s += skip;
		s_left -= skip;

		dx->ts_wrap.skip = 0;

		/* NB. always > zero. */
		lookahead = dx->ts_wrap.lookahead;

		/* Copy lookahead bytes into our wrap-around buffer
		   to make sure we can look at the header in one piece.
		   Unlike the PES coroutine we don't try to avoid
		   copying because usually lookahead is only 10 bytes,
		   and dx->ts_pes_todo will be larger than the 184
		   bytes payload of a TS packet. */

		if (lookahead > s_left) {
			memcpy (dx->ts_wrap.bp, s, s_left);

			dx->ts_wrap.bp += s_left;
			dx->ts_wrap.lookahead -= s_left;

			goto need_more_data_return;
		}

		memcpy (dx->ts_wrap.bp, s, lookahead);

		s += lookahead;
		s_left -= lookahead;

		dx->ts_wrap.bp += lookahead;

		/* Now we have 10 (TS_HEADER_LOOKAHEAD) or
		   188 + 9 (TS_SYNC_SEARCH_LOOKAHEAD) bytes
		   in dx->ts_buffer. */

		avail = dx->ts_wrap.bp - dx->ts_buffer;
		p = dx->ts_buffer;

		if (likely (dx->ts_in_sync)) {
			if (unlikely (0x47 != p[0])) {
				dx->ts_in_sync = FALSE;

				/* Spoiled. */

				dx->new_frame = TRUE;

				dx->ts_pes_todo = 0;
				dx->ts_wrap.consume = 0;

				dx->ts_continuity = -1; /* unknown */

				/* For the sync_byte search below. */
				dx->ts_wrap.lookahead =
					TS_SYNC_SEARCH_LOOKAHEAD - avail;

				if (0) {
					err = VBI_ERR_SYNC_LOST;
					goto error_return;
				} else {
					goto outer_continue;
				}
			}
		} else {
			const uint8_t *p_end = dx->ts_buffer + 188;

			assert (avail >= TS_SYNC_SEARCH_LOOKAHEAD);

			/* sync_byte search. */

			for (;;) {
				if (0x47 == p[0]) {
					if (p + 188 < dx->ts_wrap.bp
					    && 0x47 == p[188]) {
						break;
					}

					if (p + 7 < dx->ts_wrap.bp
					    && 0x00 == (p[4] | p[5])
					    && 0x01 == p[6]
					    && PRIVATE_STREAM_1 == p[7]) {
						break;
					}

					/* XXX try again with more data? */
				}

				if (unlikely (++p >= p_end)) {
					avail -= 188;

					memmove (dx->ts_buffer, p, avail);

					dx->ts_wrap.bp =
						dx->ts_buffer + avail;
					dx->ts_wrap.lookahead =
						TS_SYNC_SEARCH_LOOKAHEAD
						- avail;

					if (0) {
						err = VBI_ERR_SYNC_LOST;
						goto error_return;
					} else {
						goto outer_continue;
					}
				}
			}

			dx->ts_in_sync = TRUE;

			/* >= TS_HEADER_LOOKAHEAD bytes. */
			avail = dx->ts_wrap.bp - p;
		}

		b1 = p[1];
		pid = (b1 * 256 + p[2]) & 0x1FFF;
		b3 = p[3];

		debug2 (&dx->frame.log, "TS packet tei=%u pusi=%u tp=%u "
			"PID=%u=0x%04x tsc=%u afc=%u cc=%u.",
			!!(b1 & 0x80),
			!!(b1 & 0x40),
			!!(b1 & 0x20),
			pid, pid,
			(b3 >> 6) & 3,
			(b3 >> 4) & 3,
			b3 & 0x0F);

		/* transport_error_indicator */
		if (unlikely (0 != (b1 & 0x80))) {
			debug2 (&dx->frame.log, "Transport error.");
			if (0) {
				err = VBI_ERR_SYNC_LOST;
				goto bad_ts_packet_return;
			} else {
				goto skip_ts_pes_packet;
			}
		}

		/* transport_priority N/A. */

		if (pid != dx->ts_pid)
			goto skip_ts_packet;

		/* transport_scrambling_control [2] */
		if (unlikely (0 != (b3 & 0xC0))) {
			debug2 (&dx->frame.log, "TS scrambled.");
			if (0) {
				err = VBI_ERR_SCRAMBLED;
				goto bad_ts_packet_return;
			} else {
				goto skip_ts_pes_packet;
			}
		}

		adaptation_field_control = b3 & 0x30;

		/* EN 300 472 section 4.1: adaptation_field_control [2]
		   must be '01' or '10'. */
		if (likely (0x10 == adaptation_field_control)) {
			/* No adaptation_field, payload only. */
		} else if (likely (0x20 == adaptation_field_control)) {
			/* adaptation_field only, no payload. */
			goto skip_ts_packet;
		} else {
			/* 0x00 reserved or
			   0x30 adaptation_field followed by payload. */
			debug2 (&dx->frame.log,
				"TS invalid adaption_field_control.");
			if (0) {
				err = VBI_ERR_STREAM_SYNTAX;
				goto bad_ts_packet_return;
			} else {
				goto skip_ts_pes_packet;
			}
		}

		/* continuity_counter [4] */
		if (unlikely (0 != ((dx->ts_continuity ^ b3) & 0x0F))) {
			if (dx->ts_continuity >= 0) {
				unsigned int prev_cont;

				prev_cont = dx->ts_continuity - 1;
				if (0 == ((prev_cont ^ b3) & 0x0F)) {
					debug2 (&dx->frame.log,
						"Repeated TS packet.");
					goto skip_ts_packet;
				} else {
					debug2 (&dx->frame.log,
						"TS continuity "
						"lost: %u -> %u.",
						prev_cont & 0x0F,
						b3 & 0x0F);

					dx->ts_continuity = b3 + 1;

					if (0) {
						err = VBI_ERR_SYNC_LOST;
						goto bad_ts_packet_return;
					} else {
						goto skip_ts_pes_packet;
					}
				}
			} else {
				/* First continuity_counter we saw. */
			}
		}

		dx->ts_continuity = b3 + 1;

		if (0 == dx->ts_pes_todo) {
			unsigned int packet_length;

			/* VBI transport_packets must not contain an
			   adaption_field as well as data_bytes, and the
			   PES_packet_length must be N x 184 - 6, so
			   the PES packet start_code should follow
			   immediately. */
			if (unlikely (0x00 != (p[4] | p[5]) || 0x01 != p[6]
				      || PRIVATE_STREAM_1 != p[7])) {
				if (0) {
					err = VBI_ERR_STREAM_SYNTAX;
					goto bad_ts_packet_return;
				} else {
					goto skip_ts_pes_packet;
				}
			}

			packet_length = p[8] * 256 + p[9];

			debug2 (&dx->frame.log,
				"PES_packet_length=%u.",
				packet_length);

			/* EN 300 472 section 4.2: N x 184 - 6. (We'll
			   read 46 bytes without further checks and need
			   at least one data unit to function properly,
			   be that all stuffing bytes.) */
			if (packet_length < 178) {
				if (0) {
					err = VBI_ERR_STREAM_SYNTAX;
					goto bad_ts_packet_return;
				} else {
					goto skip_ts_pes_packet;
				}
			}

			dx->ts_pes_bp = dx->pes_buffer;
			dx->ts_pes_todo = packet_length + 6;
		} else {
			/* payload_unit_start_indicator */
			if (unlikely (0 != (b1 & 0x40))) {
				debug2 (&dx->frame.log, "Unexpected TS "
					"payload_unit_start_indicator.");
				if (0) {
					err = VBI_ERR_STREAM_SYNTAX;
					goto bad_ts_packet_return;
				} else {
					goto skip_ts_pes_packet;
				}
			}
		}

		if (likely (avail <= 188)) {
			consume = MIN (dx->ts_pes_todo, 184u);
			fragment = MIN (avail - 4, consume);

			memcpy (dx->ts_pes_bp, p + 4, fragment);

			dx->ts_pes_bp += fragment;
			dx->ts_pes_todo -= fragment;

			/* Rest of the payload when it becomes available. */
			dx->ts_wrap.consume = consume - fragment;

			dx->ts_wrap.bp = dx->ts_buffer;
			dx->ts_wrap.lookahead = TS_HEADER_LOOKAHEAD;
		} else {
			/* Possible after resynchronization. */

			fragment = MIN (dx->ts_pes_todo, 184u);

			memcpy (dx->ts_pes_bp, p + 4, fragment);

			dx->ts_pes_bp += fragment;
			dx->ts_pes_todo -= fragment;

			lookahead = avail - 188;

			memmove (dx->ts_buffer, p + 188, lookahead);

			dx->ts_wrap.bp = dx->ts_buffer + lookahead;
			lookahead = MIN (lookahead, TS_HEADER_LOOKAHEAD);
			dx->ts_wrap.lookahead =
				TS_HEADER_LOOKAHEAD - lookahead;
		}

		continue;

	skip_ts_pes_packet:
		/* Discard the data collected so far, so we don't
		   accidentally combine the top field of one frame
		   with the bottom field of another. */
		dx->new_frame = TRUE;

		/* Skip to next PES packet header. */
		dx->ts_pes_todo = 0;
		dx->ts_wrap.consume = 0;

	skip_ts_packet:
		if (likely (avail <= 188)) {
			dx->ts_wrap.skip = 188 - avail;

			dx->ts_wrap.bp = dx->ts_buffer;
			dx->ts_wrap.lookahead = TS_HEADER_LOOKAHEAD;
		} else {
			/* Possible after resynchronization. */

			lookahead = avail - 188;

			memmove (dx->ts_buffer, p + 188, lookahead);

			dx->ts_wrap.bp = dx->ts_buffer + lookahead;
			lookahead = MIN (lookahead, TS_HEADER_LOOKAHEAD);
			dx->ts_wrap.lookahead =
				TS_HEADER_LOOKAHEAD - lookahead;
		}

	outer_continue:
		;
	}

	assert (0);

 need_more_data_return:
	*src = s + s_left;
	*src_left = 0;

	return 0;

 bad_ts_packet_return:
	/* Discard the data collected so far. */
	dx->new_frame = TRUE;

	/* Skip to next PES packet header. */
	dx->ts_pes_todo = 0;
	dx->ts_wrap.consume = 0;

	if (likely (avail <= 188)) {
		/* Skip this TS packet. */
		dx->ts_wrap.skip = 188 - avail;
		
		dx->ts_wrap.bp = dx->ts_buffer;
		dx->ts_wrap.lookahead = TS_HEADER_LOOKAHEAD;
	} else {
		/* Possible after resynchronization. */

		lookahead = avail - 188;

		memmove (dx->ts_buffer, p + 188, lookahead);

		dx->ts_wrap.bp = dx->ts_buffer + lookahead;
		lookahead = MIN (lookahead, TS_HEADER_LOOKAHEAD);
		dx->ts_wrap.lookahead = TS_HEADER_LOOKAHEAD - lookahead;
	}

 error_return:
	*src = s;
	*src_left = s_left;

	return err;
}

/**
 * @brief DVB VBI demux coroutine.
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 * @param sliced Demultiplexed sliced data will be stored here.
 * @param max_lines At most this number of sliced lines will be stored
 *   at @a sliced.
 * @param pts If not @c NULL the Presentation Time Stamp associated with the
 *   first line of the demultiplexed frame will be stored here.
 * @param buffer *buffer points to DVB PES data, will be incremented by the
 *   number of bytes read from the buffer. This pointer need not align with
 *   packet boundaries.
 * @param buffer_left *buffer_left is the number of bytes left in @a buffer,
 *   will be decremented by the number of bytes read. *buffer_left need not
 *   align with packet size. The packet filter works faster with larger
 *   buffers. When you read from an MPEG file, mapping the file into memory
 *   and passing pointers to the mapped data will be fastest.
 *
 * This function consumes an arbitrary number of bytes from a DVB
 * Packetized Elementary Stream (PES), filters
 * out PRIVATE_STREAM_1 PES packets, filters out valid VBI data units,
 * converts them to vbi_sliced format and stores the sliced data at
 * @a sliced.
 *
 * You must not call this function when you passed a callback function to
 * vbi_dvb_pes_demux_new(). Call vbi_dvb_demux_feed() instead.
 *
 * @returns
 * When a frame is complete, the function returns the number of elements
 * stored in the @a sliced array. When more data is needed (@a
 * *buffer_left is zero) or an error occurred it returns the value zero.
 *
 * @bug
 * Demultiplexing of raw VBI data is not supported yet,
 * raw data will be discarded.
 *
 * @since 0.2.10
 */
unsigned int
vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		max_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left)
{
	assert (NULL != dx);
	assert (NULL != sliced);
	assert (NULL != buffer);
	assert (NULL != buffer_left);

	/* FIXME in future version:
	   buffer_left ought to be an unsigned long. */

	/* FIXME can we handle this? */
	assert (NULL == dx->callback);

	/* Doesn't work with TS, and isn't safe in any case. */
	/* dx->frame.sliced_begin = sliced;
	   dx->frame.sliced_end = sliced + max_lines; */

	if (0 != dx->demux_packet (dx, buffer, buffer_left)) {
		unsigned int n_lines;

		if (pts)
			*pts = dx->frame_pts;

		n_lines = dx->frame.sp - dx->frame.sliced_begin;
		n_lines = MIN (n_lines, max_lines); /* XXX error msg */

		if (n_lines > 0) {
			memcpy (sliced, dx->frame.sliced_begin,
				n_lines * sizeof (*sliced));

			dx->frame.sp = dx->frame.sliced_begin;
		}

		return n_lines;
	}

	return 0; /* need more data */
}

/**
 * @brief Feeds DVB VBI demux with data.
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 * @param buffer DVB PES data, need not align with packet boundaries.
 * @param buffer_size Number of bytes in @a buffer, need not align with
 *   packet size. The packet filter works faster with larger buffers.
 *
 * This function consumes an arbitrary number of bytes from a DVB
 * Packetized Elementary Stream (PES), filters
 * out PRIVATE_STREAM_1 PES packets, filters out valid VBI data units,
 * converts them to vbi_sliced format and calls the vbi_dvb_demux_cb
 * function given to vbi_dvb_pes_demux_new() when a new frame is complete.
 *
 * @returns
 * @c FALSE if the data contained errors.
 *
 * @bug
 * Demultiplexing of raw VBI data is not supported yet,
 * raw data will be discarded.
 *
 * @since 0.2.10
 */
vbi_bool
vbi_dvb_demux_feed		(vbi_dvb_demux *	dx,
				 const uint8_t *	buffer,
				 unsigned int		buffer_size)
{
	int err;

	assert (NULL != dx);
	assert (NULL != buffer);
	assert (NULL != dx->callback);

	err = dx->demux_packet (dx, &buffer, &buffer_size);

	return (0 == err);
}

/**
 * @brief Resets DVB VBI demux.
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 *
 * Resets the DVB demux to the initial state as after vbi_dvb_pes_demux_new(),
 * useful for example after a channel change.
 *
 * @since 0.2.10
 */
void
vbi_dvb_demux_reset		(vbi_dvb_demux *	dx)
{
	assert (NULL != dx);

	CLEAR (dx->pes_wrap);

	dx->pes_wrap.buffer = dx->pes_buffer;
	dx->pes_wrap.bp = dx->pes_buffer;

	dx->pes_wrap.lookahead = PES_HEADER_LOOKAHEAD;

	CLEAR (dx->ts_wrap);

	dx->ts_wrap.buffer = dx->ts_buffer;
	dx->ts_wrap.bp = dx->ts_buffer;

	dx->ts_wrap.lookahead = TS_SYNC_SEARCH_LOOKAHEAD;

	CLEAR (dx->frame);

	dx->frame.sliced_begin = dx->sliced;
	dx->frame.sliced_end = dx->sliced + N_ELEMENTS (dx->sliced);

	dx->frame.sp = dx->sliced;

	/* Raw data ignored for now. */

	dx->frame_pts = 0;
	dx->packet_pts = 0;

	dx->new_frame = TRUE;

	dx->ts_in_sync = FALSE;

	dx->ts_frame_bp = NULL;
	dx->ts_frame_todo = 0;

	dx->ts_pes_bp = NULL;
	dx->ts_pes_todo = 0;

	dx->ts_continuity = -1; /* unknown */
}

/**
 * @param dx DVB demultiplexer context allocated with vbi_dvb_pes_demux_new().
 * @param mask Which kind of information to log. Can be @c 0.
 * @param log_fn This function is called with log messages. Consider
 *   vbi_log_on_stderr(). Can be @c NULL to disable logging.
 * @param user_data User pointer passed through to the @a log_fn function.
 *
 * The DVB demultiplexer supports the logging of errors in the PES
 * stream and information useful to debug the demultiplexer.
 *
 * With this function you can redirect log messages generated by this
 * module which would normally go to the global log function (see
 * vbi_set_log_fn()), or enable logging only in the DVB
 * demultiplexer @a dx.
 *
 * @note
 * The log messages may change in the future.
 *
 * @since 0.2.22
 */
void
vbi_dvb_demux_set_log_fn	(vbi_dvb_demux *	dx,
				 vbi_log_mask		mask,
				 vbi_log_fn *		log_fn,
				 void *			user_data)
{
	assert (NULL != dx);

	if (NULL == log_fn)
		mask = 0;

	dx->frame.log.mask = mask;
	dx->frame.log.fn = log_fn;
	dx->frame.log.user_data = user_data;
}

/**
 * @brief Deletes DVB VBI demux.
 * @param dx DVB demultiplexer context allocated with
 *   vbi_dvb_pes_demux_new(), can be @c NULL.
 *
 * Frees all resources associated with @a dx.
 *
 * @since 0.2.10
 */
void
vbi_dvb_demux_delete		(vbi_dvb_demux *	dx)
{
	if (NULL == dx)
		return;

	CLEAR (*dx);

	vbi_free (dx);		
}

/* Experimental. */
vbi_dvb_demux *
_vbi_dvb_ts_demux_new		(vbi_dvb_demux_cb *	callback,
				 void *			user_data,
				 unsigned int		pid)
{
	vbi_dvb_demux *dx;

	/* 0x0000 Program Association Table
	   0x0001 Conditional Access Table
	   0x0002-0x000F reserved
	   0x1FFF Null packet */
	if (pid <= 0x000F || pid >= 0x1FFF) {
		/* errno = VBI_ERR_INVALID_PID; */
		return NULL;
	}

	dx = vbi_malloc (sizeof (*dx));
	if (NULL == dx) {
		errno = ENOMEM;
		return NULL;
	}

	CLEAR (*dx);

	vbi_dvb_demux_reset (dx);

	dx->demux_packet = demux_ts_packet;

	dx->ts_pid = pid;

	dx->callback = callback;
	dx->user_data = user_data;

	return dx;
}

/**
 * @brief Allocates DVB VBI demux.
 * @param callback Function to be called by vbi_dvb_demux_feed() when
 *   a new frame is available. If you want to use the vbi_dvb_demux_cor()
 *   function instead, @a callback must be @c NULL. Conversely you
 *   must not call vbi_dvb_demux_cor() if a @a callback is given.
 * @param user_data User pointer passed through to @a callback function.
 *
 * Allocates a new DVB VBI (EN 301 472, EN 301 775) demultiplexer taking
 * a PES stream as input.
 *
 * @returns
 * Pointer to newly allocated DVB demux context which must be
 * freed with vbi_dvb_demux_delete() when done. @c NULL on failure
 * (out of memory).
 *
 * @since 0.2.10
 */
vbi_dvb_demux *
vbi_dvb_pes_demux_new		(vbi_dvb_demux_cb *	callback,
				 void *			user_data)
{
	vbi_dvb_demux *dx;

	dx = vbi_malloc (sizeof (*dx));
	if (NULL == dx) {
		errno = ENOMEM;
		return NULL;
	}

	CLEAR (*dx);

	vbi_dvb_demux_reset (dx);

	dx->demux_packet = demux_pes_packet;

	dx->callback = callback;
	dx->user_data = user_data;

	return dx;
}

/* For compatibility with Zapping 0.8 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

extern unsigned int
_vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		sliced_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left);
extern void
_vbi_dvb_demux_delete		(vbi_dvb_demux *	dx);
extern vbi_dvb_demux *
_vbi_dvb_demux_pes_new		(vbi_dvb_demux_cb *	callback,
				 void *			user_data);

unsigned int
_vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		sliced_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left)
{
	return vbi_dvb_demux_cor (dx, sliced, sliced_lines,
				  pts, buffer, buffer_left);
}

void
_vbi_dvb_demux_delete		(vbi_dvb_demux *	dx)
{
	vbi_dvb_demux_delete (dx);
}

vbi_dvb_demux *
_vbi_dvb_demux_pes_new		(vbi_dvb_demux_cb *	callback,
				 void *			user_data)
{
	return vbi_dvb_pes_demux_new (callback, user_data);
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
