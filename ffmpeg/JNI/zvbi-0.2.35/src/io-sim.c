/*
 *  libzvbi -- VBI device simulation
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

/* $Id: io-sim.c,v 1.18 2009/12/14 23:43:40 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <math.h>		/* sin(), log() */
#include <errno.h>
#include <ctype.h>		/* isspace() */
#include <limits.h>		/* INT_MAX */

#include "misc.h"
#include "sliced.h"
#include "version.h"
#include "sampling_par.h"
#include "raw_decoder.h"
#include "hamm.h"

#if 2 == VBI_VERSION_MINOR
#  define sp_sample_format sampling_format
#  define SAMPLES_PER_LINE(sp)						\
	((sp)->bytes_per_line / VBI_PIXFMT_BPP ((sp)->sampling_format))
#  define SYSTEM_525(sp)						\
	(525 == (sp)->scanning)
#else
#  include "vps.h"
#  include "wss.h"
#  define sp_sample_format sample_format
#  define SAMPLES_PER_LINE(sp) ((sp)->samples_per_line)
#  define SYSTEM_525(sp)						\
	(0 != (VBI_VIDEOSTD_SET_525_60 & (sp)->videostd_set))
#endif

#include "io-sim.h"

/**
 * @addtogroup Rawenc Raw VBI encoder
 * @ingroup Raw
 * @brief Converting sliced VBI data to raw VBI images.
 *
 * These are functions converting sliced VBI data to raw VBI images as
 * transmitted in the vertical blanking interval of analog video standards.
 * They are mainly intended for tests of the libzvbi bit slicer and
 * raw VBI decoder.
 */

#if 2 == VBI_VERSION_MINOR
#  define VBI_PIXFMT_RGB24_LE VBI_PIXFMT_RGB24
#  define VBI_PIXFMT_BGR24_LE VBI_PIXFMT_BGR24
#  define VBI_PIXFMT_RGBA24_LE VBI_PIXFMT_RGBA32_LE
#  define VBI_PIXFMT_BGRA24_LE VBI_PIXFMT_BGRA32_LE
#  define VBI_PIXFMT_RGBA24_BE VBI_PIXFMT_RGBA32_BE
#  define VBI_PIXFMT_BGRA24_BE VBI_PIXFMT_BGRA32_BE
#  define vbi_pixfmt_bytes_per_pixel VBI_PIXFMT_BPP
#endif

#undef warning
#define warning(function, templ, args...)				\
do {									\
	if (_vbi_global_log.mask & VBI_LOG_WARNING)			\
		_vbi_log_printf (_vbi_global_log.fn,			\
				  _vbi_global_log.user_data,		\
				  VBI_LOG_WARNING, __FILE__, function,	\
				  templ , ##args);			\
} while (0)

#define PI 3.1415926535897932384626433832795029

#define PULSE(zero_level)						\
do {									\
	if (0 == seq) {							\
		raw[i] = SATURATE (zero_level, 0, 255);			\
	} else if (3 == seq) {						\
		raw[i] = SATURATE (zero_level + (int) signal_amp,	\
				   0, 255);				\
	} else if ((seq ^ bit) & 1) { /* down */			\
		double r = sin (q * tr - (PI / 2.0));			\
		r = r * r * signal_amp;					\
		raw[i] = SATURATE (zero_level + (int) r, 0, 255);	\
	} else { /* up */						\
		double r = sin (q * tr);				\
		r = r * r * signal_amp;					\
		raw[i] = SATURATE (zero_level + (int) r, 0, 255);	\
	}								\
} while (0)

#define PULSE_SEQ(zero_level)						\
do {									\
	double tr;							\
	unsigned int bit;						\
	unsigned int byte;						\
	unsigned int seq;						\
									\
	tr = t - t1;							\
	bit = tr * bit_rate;						\
	byte = bit >> 3;						\
	bit &= 7;							\
	seq = (buf[byte] >> 7) + buf[byte + 1] * 2;			\
	seq = (seq >> bit) & 3;						\
	PULSE (zero_level);						\
} while (0)

#ifndef HAVE_SINCOS

/* This is a GNU extension. */
_vbi_inline void
sincos				(double			x,
				 double *		sinx,
				 double *		cosx)
{
	*sinx = sin (x);
	*cosx = cos (x);
}

#endif

/* This is a GNU extension. */
#ifndef HAVE_LOG2
#  define log2(x) (log (x) / M_LN2)
#endif

static void
signal_teletext			(uint8_t *		raw,
				 const vbi_sampling_par *sp,
				 int			black_level,
				 double			signal_amp,
				 double			bit_rate,
				 unsigned int		frc,
				 unsigned int		payload,
				 const vbi_sliced *	sliced)
{
	double bit_period = 1.0 / bit_rate;
	/* Teletext System B: Sixth CRI pulse at 12 us
	   (+.5 b/c we start with a 0 bit). */
	double t1 = 12e-6 - 13 * bit_period;
	double t2 = t1 + (payload * 8 + 24 + 1) * bit_period;
	double q = (PI / 2.0) * bit_rate;
	double sample_period = 1.0 / sp->sampling_rate;
	unsigned int samples_per_line;
	uint8_t buf[64];
	unsigned int i;
	double t;

	buf[0] = 0x00;
	buf[1] = 0x55; /* clock run-in */
	buf[2] = 0x55;
	buf[3] = frc;

	memcpy (buf + 4, sliced->data, payload);

	buf[payload + 4] = 0x00;

	t = sp->offset / (double) sp->sampling_rate;

	samples_per_line = SAMPLES_PER_LINE (sp);

	for (i = 0; i < samples_per_line; ++i) {
		if (t >= t1 && t < t2)
			PULSE_SEQ (black_level);

		t += sample_period;
	}
}

static void
signal_vps			(uint8_t *		raw,
				 const vbi_sampling_par *sp,
				 int			black_level,
				 int			white_level,
				 const vbi_sliced *	sliced)
{
	static const uint8_t biphase [] = {
		0xAA, 0x6A, 0x9A, 0x5A,
		0xA6, 0x66, 0x96, 0x56,
		0xA9, 0x69, 0x99, 0x59,
		0xA5, 0x65, 0x95, 0x55
	};
	double bit_rate = 15625 * 160 * 2;
	double t1 = 12.5e-6 - .5 / bit_rate;
	double t4 = t1 + ((4 + 13 * 2) * 8) / bit_rate;
	double q = (PI / 2.0) * bit_rate;
	double sample_period = 1.0 / sp->sampling_rate;
	unsigned int samples_per_line;
	double signal_amp = (0.5 / 0.7) * (white_level - black_level);
	uint8_t buf[32];
	unsigned int i;
	double t;

	CLEAR (buf);

	buf[1] = 0x55; /* 0101 0101 */
	buf[2] = 0x55; /* 0101 0101 */
	buf[3] = 0x51; /* 0101 0001 */
	buf[4] = 0x99; /* 1001 1001 */

	for (i = 0; i < 13; ++i) {
		unsigned int b = sliced->data[i];

		buf[5 + i * 2] = biphase[b >> 4];
		buf[6 + i * 2] = biphase[b & 15];
	}

	buf[6 + 12 * 2] &= 0x7F;

	t = sp->offset / (double) sp->sampling_rate;

	samples_per_line = SAMPLES_PER_LINE (sp);

	for (i = 0; i < samples_per_line; ++i) {
		if (t >= t1 && t < t4)
			PULSE_SEQ (black_level);

		t += sample_period;
	}
}

static void
wss_biphase			(uint8_t		buf[32],
				 const vbi_sliced *	sliced)
{
	unsigned int bit;
	unsigned int data;
	unsigned int i;

	/* 29 bit run-in and 24 bit start code, lsb first. */

	buf[0] = 0x00;
	buf[1] = 0x1F; /* 0001 1111 */
	buf[2] = 0xC7; /* 1100 0111 */
	buf[3] = 0x71; /* 0111 0001 */
	buf[4] = 0x1C; /* 000 | 1 1100 */
	buf[5] = 0x8F; /* 1000 1111 */
	buf[6] = 0x07; /* 0000 0111 */
	buf[7] = 0x1F; /*    1 1111 */

	bit = 8 + 29 + 24;
	data = sliced->data[0] + sliced->data[1] * 256;

	for (i = 0; i < 14; ++i) {
		static const unsigned int biphase [] = { 0x38, 0x07 };
		unsigned int byte;
		unsigned int shift;
		unsigned int seq;

		byte = bit >> 3;
		shift = bit & 7;
		bit += 6;

		seq = biphase[data & 1] << shift;
		data >>= 1;

		assert (byte < 31);

		buf[byte] |= seq;
		buf[byte + 1] = seq >> 8;
	}
}

static void
signal_wss_625			(uint8_t *		raw,
				 const vbi_sampling_par *sp,
				 int			black_level,
				 int			white_level,
				 const vbi_sliced *	sliced)
{
	double bit_rate = 15625 * 320;
	double t1 = 11.0e-6 - .5 / bit_rate;
	double t4 = t1 + (29 + 24 + 14 * 6 + 1) / bit_rate;
	double q = (PI / 2.0) * bit_rate;
	double sample_period = 1.0 / sp->sampling_rate;
	double signal_amp = (0.5 / 0.7) * (white_level - black_level);
	unsigned int samples_per_line;
	uint8_t buf[32];
	unsigned int i;
	double t;

	CLEAR (buf);

	wss_biphase (buf, sliced);

	t = sp->offset / (double) sp->sampling_rate;

	samples_per_line = SAMPLES_PER_LINE (sp);

	for (i = 0; i < samples_per_line; ++i) {
		if (t >= t1 && t < t4)
			PULSE_SEQ (black_level);

		t += sample_period;
	}
}

static void
signal_closed_caption		(uint8_t *		raw,
				 const vbi_sampling_par *sp,
				 int			blank_level,
				 int			white_level,
				 unsigned int		flags,
				 double			bit_rate,
				 const vbi_sliced *	sliced)
{
	double D = 1.0 / bit_rate;
	double t0 = 10.5e-6; /* CRI start half amplitude (EIA 608-B) */
	double t1 = t0 - .25 * D; /* CRI start, blanking level */
	double t2 = t1 + 7 * D; /* CRI 7 cycles */
	/* First start bit, left edge half amplitude, minus rise time. */
	double t3 = t0 + 6.5 * D - 120e-9;
	double q1 = PI * bit_rate * 2;
	/* Max. rise/fall time 240 ns (EIA 608-B). */
	double q2 = PI / 120e-9;
	double signal_mean;
	double signal_high;
	double sample_period = 1.0 / sp->sampling_rate;
	unsigned int samples_per_line;
	double t;
	unsigned int data;
	unsigned int i;

	/* Twice 7 data + odd parity, start bit 0 -> 1 */

	data = (sliced->data[1] << 12) + (sliced->data[0] << 4) + 8;

	t = sp->offset / (double) sp->sampling_rate;

	samples_per_line = SAMPLES_PER_LINE (sp);

	if (flags & _VBI_RAW_SHIFT_CC_CRI) {
		/* Wrong signal shape found by Rich Kadel,
		   zapping-misc@lists.sourceforge.net 2006-07-16. */
		t0 += D / 2;
		t1 += D / 2;
		t2 += D / 2;
	}

	if (flags & _VBI_RAW_LOW_AMP_CC) {
		/* Low amplitude signal found by Rich Kadel,
		   zapping-misc@lists.sourceforge.net 2007-08-15. */
		white_level = white_level * 6 / 10;
	}

	signal_mean = (white_level - blank_level) * .25; /* 25 IRE */
	signal_high = blank_level + (white_level - blank_level) * .5;

	for (i = 0; i < samples_per_line; ++i) {
		if (t >= t1 && t < t2) {
			raw[i] = SATURATE (blank_level
					   + (1.0 - cos (q1 * (t - t1)))
					   * signal_mean, 0, 255);
		} else {
			unsigned int bit;
			unsigned int seq;
			double d;

			d = t - t3;
			bit = d * bit_rate;
			seq = (data >> bit) & 3;

			d -= bit * D;
			if ((1 == seq || 2 == seq)
			    && fabs (d) < .120e-6) {
				int level;

				if (1 == seq)
					level = blank_level
						+ (1.0 + cos (q2 * d))
						* signal_mean;
				else
					level = blank_level
						+ (1.0 - cos (q2 * d))
						* signal_mean;
				raw[i] = SATURATE (level, 0, 255);
			} else if (data & (2 << bit)) {
				raw[i] = SATURATE (signal_high, 0, 255);
			} else {
				raw[i] = SATURATE (blank_level, 0, 255);
			}
		}

		t += sample_period;
	}
}

static void
clear_image			(uint8_t *		p,
				 unsigned int		value,
				 unsigned int		width,
				 unsigned int		height,
				 unsigned int		bytes_per_line)
{
	if (width == bytes_per_line) {
		memset (p, value, height * bytes_per_line);
	} else {
		while (height-- > 0) {
			memset (p, value, width);
			p += bytes_per_line;
		}
	}
}

/**
 * @param raw Noise will be added to this raw VBI image.
 * @param sp Describes the raw VBI data in the buffer. @a sp->sampling_format
 *   must be @c VBI_PIXFMT_Y8 (@c VBI_PIXFMT_YUV420 in libzvbi 0.2.x).
 *   Note for compatibility in libzvbi 0.2.x vbi_sampling_par is a
 *   synonym of vbi_raw_decoder, but the (private) decoder fields in
 *   this structure are ignored.
 * @param min_freq Minimum frequency of the noise in Hz.
 * @param max_freq Maximum frequency of the noise in Hz. @a min_freq and
 *   @a max_freq define the cut off frequency at the half power points
 *   (gain -3 dB).
 * @param amplitude Maximum amplitude of the noise, should lie in range
 *   0 to 256.
 * @param seed Seed for the pseudo random number generator built into
 *   this function. Given the same @a seed value the function will add
 *   the same noise, which can be useful for tests.
 *
 * This function adds white noise to a raw VBI image.
 *
 * To produce realistic noise @a min_freq = 0, @a max_freq = 5e6 and
 * @a amplitude = 20 to 50 seems appropriate.
 *
 * @returns
 * FALSE if the @a sp sampling parameters are invalid.
 *
 * @since 0.2.26
 */
vbi_bool
vbi_raw_add_noise		(uint8_t *		raw,
				 const vbi_sampling_par *sp,
				 unsigned int		min_freq,
				 unsigned int		max_freq,
				 unsigned int		amplitude,
				 unsigned int		seed)
{
	double f0, w0, sn, cs, bw, alpha, a0;
	float a1, a2, b0, b1, z0, z1, z2;
	unsigned int n_lines;
	unsigned long samples_per_line;
	unsigned long padding;
	uint32_t seed32;

	assert (NULL != raw);
	assert (NULL != sp);

	if (unlikely (!_vbi_sampling_par_valid_log (sp, /* log */ NULL)))
		return FALSE;

	switch (sp->sp_sample_format) {
#if 3 == VBI_VERSION_MINOR
	case VBI_PIXFMT_YUV444:
	case VBI_PIXFMT_YVU444:
	case VBI_PIXFMT_YUV422:
	case VBI_PIXFMT_YVU422:
	case VBI_PIXFMT_YUV411:
	case VBI_PIXFMT_YVU411:
	case VBI_PIXFMT_YVU420:
	case VBI_PIXFMT_YUV410:
	case VBI_PIXFMT_YVU410:
	case VBI_PIXFMT_Y8:
#endif
	case VBI_PIXFMT_YUV420:
		break;

	default:
		return FALSE;
	}

	if (unlikely (sp->sampling_rate <= 0))
		return FALSE;

	/* Biquad bandpass filter.
	   http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt */

	f0 = ((double) min_freq + max_freq) * 0.5;

	if (f0 <= 0.0)
		return TRUE;

	w0 = 2 * M_PI * f0 / sp->sampling_rate;
	sincos (w0, &sn, &cs);
	bw = fabs (log2 (MAX (min_freq, max_freq) / f0));
	alpha = sn * sinh (log (2) / 2 * bw * w0 / sn);
	a0 = 1 + alpha;
	a1 = 2 * cs / a0;
	a2 = (alpha - 1) / a0;
	b0 = sn / (2*a0);
	b1 = 0;

	if (amplitude > 256)
		amplitude = 256;

	n_lines = sp->count[0] + sp->count[1];

#if 2 == VBI_VERSION_MINOR
	if (unlikely (0 == amplitude
		      || 0 == n_lines
		      || 0 == sp->bytes_per_line))
		return TRUE;

	samples_per_line = sp->bytes_per_line;
	padding = 0;
#else
	if (unlikely (0 == amplitude
		      || 0 == n_lines
		      || 0 == sp->samples_per_line))
		return TRUE;

	samples_per_line = sp->samples_per_line;
	padding = sp->bytes_per_line - samples_per_line;
#endif

	seed32 = seed;

	z1 = 0;
	z2 = 0;

	do {
		uint8_t *raw_end = raw + samples_per_line;

		do {
			int noise;

			/* We use our own simple PRNG to produce
			   predictable results for tests. */
			seed32 = seed32 * 1103515245u + 12345;
			noise = ((seed32 / 65536) % (amplitude * 2 + 1))
				- amplitude;

			z0 = noise + a1 * z1 + a2 * z2;
			noise = (int)(b0 * (z0 - z2) + b1 * z1);
			z2 = z1;
			z1 = z0;

			*raw++ = SATURATE (*raw + noise, 0, 255);
		} while (raw < raw_end);

		raw += padding;
	} while (--n_lines > 0);

	return TRUE;
}

static vbi_bool
signal_u8			(uint8_t *		raw,
				 const vbi_sampling_par *sp,
				 int			blank_level,
				 int			black_level,
				 int			white_level,
				 unsigned int		flags,
				 const vbi_sliced *	sliced,
				 unsigned int		n_sliced_lines,
				 const char *		caller)
{
	unsigned int n_scan_lines;
	unsigned int samples_per_line;

	n_scan_lines = sp->count[0] + sp->count[1];
	samples_per_line = SAMPLES_PER_LINE (sp);

	clear_image (raw,
		     SATURATE (blank_level, 0, 255),
		     samples_per_line,
		     n_scan_lines,
		     sp->bytes_per_line);

	for (; n_sliced_lines-- > 0; ++sliced) {
		unsigned int row;
		uint8_t *raw1;

		if (0 == sliced->line) {
			goto bounds;
		} else if (0 != sp->start[1]
			   && sliced->line >= (unsigned int) sp->start[1]) {
			row = sliced->line - sp->start[1];
			if (row >= (unsigned int) sp->count[1])
				goto bounds;

			if (sp->interlaced) {
				row = row * 2
					+ !(flags & _VBI_RAW_SWAP_FIELDS);
			} else if (0 == (flags & _VBI_RAW_SWAP_FIELDS)) {
				row += sp->count[0];
			}
		} else if (0 != sp->start[0]
			   && sliced->line >= (unsigned int) sp->start[0]) {
			row = sliced->line - sp->start[0];
			if (row >= (unsigned int) sp->count[0])
				goto bounds;

			if (sp->interlaced) {
				row *= 2
					+ !!(flags & _VBI_RAW_SWAP_FIELDS);
			} else if (flags & _VBI_RAW_SWAP_FIELDS) {
				row += sp->count[0];
			}
		} else {
		bounds:
			warning (caller,
				 "Sliced line %u out of bounds.",
				 sliced->line);
			return FALSE;
		}

		raw1 = raw + row * sp->bytes_per_line;

		switch (sliced->id) {
		case VBI_SLICED_TELETEXT_A: /* ok? */
			signal_teletext (raw1, sp,
					 black_level,
					 /* amplitude */ .7 * (white_level
							       - black_level),
					 /* bit_rate */ 25 * 625 * 397,
					 /* FRC */ 0xE7,
					 /* payload */ 37,
					 sliced);
			break;

		case VBI_SLICED_TELETEXT_B_L10_625:
		case VBI_SLICED_TELETEXT_B_L25_625:
		case VBI_SLICED_TELETEXT_B:
			signal_teletext (raw1, sp, black_level,
					 .66 * (white_level - black_level),
					 25 * 625 * 444, 0x27, 42, sliced);
			break;

		case VBI_SLICED_TELETEXT_C_625:
			signal_teletext (raw1, sp, black_level,
					 .7 * (white_level - black_level),
					 25 * 625 * 367, 0xE7, 33, sliced);
			break;

		case VBI_SLICED_TELETEXT_D_625:
			signal_teletext (raw1, sp, black_level,
					 .7 * (white_level - black_level),
					 5642787, 0xA7, 34, sliced);
			break;

		case VBI_SLICED_CAPTION_625_F1:
		case VBI_SLICED_CAPTION_625_F2:
		case VBI_SLICED_CAPTION_625:
			signal_closed_caption (raw1, sp,
					       blank_level,
					       white_level,
					       flags,
					       25 * 625 * 32,
					       sliced);
			break;

		case VBI_SLICED_VPS:
		case VBI_SLICED_VPS_F2:
			signal_vps (raw1, sp,
				    black_level,
				    white_level,
				    sliced);
			break;

		case VBI_SLICED_WSS_625:
			signal_wss_625 (raw1, sp,
					black_level,
					white_level,
					sliced);
			break;

		case VBI_SLICED_TELETEXT_B_525:
			signal_teletext (raw1, sp,
					 black_level,
					 /* amplitude */ .7 * (white_level
							       - black_level),
					 /* bit_rate */ 5727272,
					 /* FRC */ 0x27,
					 /* payload */ 34,
					 sliced);
			break;

		case VBI_SLICED_TELETEXT_C_525:
			signal_teletext (raw1, sp, black_level,
					 .7 * (white_level - black_level),
					 5727272, 0xE7, 33, sliced);
			break;

		case VBI_SLICED_TELETEXT_D_525:
			signal_teletext (raw1, sp, black_level,
					 .7 * (white_level - black_level),
					 5727272, 0xA7, 34, sliced);
			break;

		case VBI_SLICED_CAPTION_525_F1:
		case VBI_SLICED_CAPTION_525_F2:
		case VBI_SLICED_CAPTION_525:
			signal_closed_caption (raw1, sp,
					       blank_level,
					       white_level,
					       flags,
					       30000 * 525 * 32 / 1001,
					       sliced);
			break;

		default:
			warning (caller,
				 "Service 0x%08x (%s) not supported.",
				 sliced->id, vbi_sliced_name (sliced->id));
			return FALSE;
		}
	}

	return TRUE;
}

vbi_bool
_vbi_raw_vbi_image		(uint8_t *		raw,
				 unsigned long		raw_size,
				 const vbi_sampling_par *sp,
				 int			blank_level,
				 int			white_level,
				 unsigned int		flags,
				 const vbi_sliced *	sliced,
				 unsigned int		n_sliced_lines)
{
	unsigned int n_scan_lines;
	unsigned int black_level;

	if (unlikely (!_vbi_sampling_par_valid_log (sp, NULL)))
		return FALSE;

	n_scan_lines = sp->count[0] + sp->count[1];
	if (unlikely (n_scan_lines * sp->bytes_per_line > raw_size)) {
		warning (__FUNCTION__,
			 "(%u + %u lines) * %lu bytes_per_line "
			 "> %lu raw_size.",
			 sp->count[0], sp->count[1],
			 (unsigned long) sp->bytes_per_line, raw_size);
		return FALSE;
	}

	if (unlikely (0 != white_level
		      && blank_level > white_level)) {
		warning (__FUNCTION__,
			 "Invalid blanking %d or peak white level %d.",
			 blank_level, white_level);
	}

	if (SYSTEM_525 (sp)) {
		/* Observed value. */
		const unsigned int peak = 200; /* 255 */

		if (0 == white_level) {
			blank_level = (int)(40.0 * peak / 140);
			black_level = (int)(47.5 * peak / 140);
			white_level = peak;
		} else {
			black_level =
				(int)(blank_level
				      + 7.5 * (white_level - blank_level));
		}
	} else {
		const unsigned int peak = 200; /* 255 */

		if (0 == white_level) {
			blank_level = (int)(43.0 * peak / 140);
			white_level = peak;
		}

		black_level = blank_level;
	}

	return signal_u8 (raw, sp,
			  blank_level, black_level, white_level,
			  flags,
			  sliced, n_sliced_lines,
			  __FUNCTION__);
}

#define RGBA_TO_RGB16(value)						\
	(+(((value) & 0xF8) >> (3 - 0))					\
	 +(((value) & 0xFC00) >> (10 - 5))				\
	 +(((value) & 0xF80000) >> (19 - 11)))

#define RGBA_TO_RGBA15(value)						\
	(+(((value) & 0xF8) >> (3 - 0))					\
	 +(((value) & 0xF800) >> (11 - 5))				\
	 +(((value) & 0xF80000) >> (19 - 10))				\
	 +(((value) & 0x80000000) >> (31 - 15)))

#define RGBA_TO_ARGB15(value)						\
	(+(((value) & 0xF8) >> (3 - 1))					\
	 +(((value) & 0xF800) >> (11 - 6))				\
	 +(((value) & 0xF80000) >> (19 - 11))				\
	 +(((value) & 0x80000000) >> (31 - 0)))

#define RGBA_TO_RGBA12(value)						\
	(+(((value) & 0xF0) >> (4 - 0))					\
	 +(((value) & 0xF000) >> (12 - 4))				\
	 +(((value) & 0xF00000) >> (20 - 8))				\
	 +(((value) & 0xF0000000) >> (28 - 12)))

#define RGBA_TO_ARGB12(value)						\
	(+(((value) & 0xF0) << -(4 - 12))				\
	 +(((value) & 0xF000) >> (12 - 8))				\
	 +(((value) & 0xF00000) >> (20 - 4))				\
	 +(((value) & 0xF0000000) >> (28 - 0)))

#define RGBA_TO_RGB8(value)						\
	(+(((value) & 0xE0) >> (5 - 0))					\
	 +(((value) & 0xE000) >> (13 - 3))				\
	 +(((value) & 0xC00000) >> (22 - 6)))

#define RGBA_TO_BGR8(value)						\
	(+(((value) & 0xE0) >> (5 - 5))					\
	 +(((value) & 0xE000) >> (13 - 2))				\
	 +(((value) & 0xC00000) >> (22 - 0)))

#define RGBA_TO_RGBA7(value)						\
	(+(((value) & 0xC0) >> (6 - 0))					\
	 +(((value) & 0xE000) >> (13 - 2))				\
	 +(((value) & 0xC00000) >> (22 - 5))				\
	 +(((value) & 0x80000000) >> (31 - 7)))

#define RGBA_TO_ARGB7(value)						\
	(+(((value) & 0xC0) >> (6 - 6))					\
	 +(((value) & 0xE000) >> (13 - 3))				\
	 +(((value) & 0xC00000) >> (22 - 1))				\
	 +(((value) & 0x80000000) >> (31 - 0)))

#define MST1(d, val, mask) (d) = ((d) & ~(mask)) | ((val) & (mask))
#define MST2(d, val, mask) (d) = ((d) & (mask)) | (val)

#define SCAN_LINE_TO_N(conv, n)						\
do {									\
	for (i = 0; i < samples_per_line; ++i) {			\
		uint8_t *dd = d + i * (n);				\
		unsigned int value = s[i] * 0x01010101;			\
		unsigned int mask = ~pixel_mask;			\
									\
		value = conv (value) & pixel_mask;			\
		MST2 (dd[0], value >> 0, mask >> 0);			\
		if (n >= 2)						\
			MST2 (dd[1], value >> 8, mask >> 8);		\
		if (n >= 3)						\
			MST2 (dd[2], value >> 16, mask >> 16);		\
		if (n >= 4)						\
			MST2 (dd[3], value >> 24, mask >> 24);		\
	}								\
} while (0)

#define SCAN_LINE_TO_RGB2(conv, endian)					\
do {									\
	for (i = 0; i < samples_per_line; ++i) {			\
		uint8_t *dd = d + i * 2;				\
		unsigned int value = s[i] * 0x01010101;			\
		unsigned int mask;					\
									\
		value = conv (value) & pixel_mask;			\
		mask = ~pixel_mask;		       			\
		MST2 (dd[0 + endian], value >> 0, mask >> 0);		\
		MST2 (dd[1 - endian], value >> 8, mask >> 8);		\
	}								\
} while (0)

vbi_bool
_vbi_raw_video_image		(uint8_t *		raw,
				 unsigned long		raw_size,
				 const vbi_sampling_par *sp,
				 int			blank_level,
				 int			black_level,
				 int			white_level,
				 unsigned int		pixel_mask,
				 unsigned int		flags,
				 const vbi_sliced *	sliced,
				 unsigned int		n_sliced_lines)
{
	unsigned int n_scan_lines;
	unsigned int samples_per_line;
	vbi_sampling_par sp8;
	unsigned int size;
	uint8_t *buf;
	uint8_t *s;
	uint8_t *d;

	if (unlikely (!_vbi_sampling_par_valid_log (sp, NULL)))
		return FALSE;

	n_scan_lines = sp->count[0] + sp->count[1];
	if (unlikely (n_scan_lines * sp->bytes_per_line > raw_size)) {
		warning (__FUNCTION__,
			 "%u + %u lines * %lu bytes_per_line > %lu raw_size.",
			 			 sp->count[0], sp->count[1],
			 (unsigned long) sp->bytes_per_line, raw_size);
		return FALSE;
	}

	if (unlikely (0 != white_level
		      && (blank_level > black_level
			  || black_level > white_level))) {
		warning (__FUNCTION__,
			 "Invalid blanking %d, black %d or peak "
			 "white level %d.",
			 blank_level, black_level, white_level);
	}

	switch (sp->sp_sample_format) {
#if 3 == VBI_VERSION_MINOR
	case VBI_PIXFMT_YVUA24_LE:	/* 0xAAUUVVYY */
	case VBI_PIXFMT_YVU24_LE:	/* 0x00UUVVYY */
#endif
	case VBI_PIXFMT_YVYU:
	case VBI_PIXFMT_VYUY:		/* 0xAAUUVVYY */
		pixel_mask = (+ ((pixel_mask & 0xFF00) << 8)
			      + ((pixel_mask & 0xFF0000) >> 8)
			      + ((pixel_mask & 0xFF0000FF)));
		break;

#if 3 == VBI_VERSION_MINOR
	case VBI_PIXFMT_YUVA24_BE:	/* 0xYYUUVVAA */
#endif
	case VBI_PIXFMT_RGBA24_BE:	/* 0xRRGGBBAA */
		pixel_mask = SWAB32 (pixel_mask);
		break;

#if 3 == VBI_VERSION_MINOR
	case VBI_PIXFMT_YVUA24_BE:	/* 0xYYVVUUAA */
		pixel_mask = (+ ((pixel_mask & 0xFF) << 24)
			      + ((pixel_mask & 0xFFFF00))
			      + ((pixel_mask & 0xFF000000) >> 24));
		break;

	case VBI_PIXFMT_YUV24_BE:	/* 0xAAYYUUVV */
	case VBI_PIXFMT_ARGB24_BE:	/* 0xAARRGGBB */
	case VBI_PIXFMT_BGRA12_LE:
	case VBI_PIXFMT_BGRA12_BE:
	case VBI_PIXFMT_ABGR12_LE:
	case VBI_PIXFMT_ABGR12_BE:
	case VBI_PIXFMT_BGRA7:
	case VBI_PIXFMT_ABGR7:
#endif
	case VBI_PIXFMT_BGR24_LE:	/* 0x00RRGGBB */
	case VBI_PIXFMT_BGRA15_LE:
	case VBI_PIXFMT_BGRA15_BE:
	case VBI_PIXFMT_ABGR15_LE:
	case VBI_PIXFMT_ABGR15_BE:
		pixel_mask = (+ ((pixel_mask & 0xFF) << 16)
			      + ((pixel_mask & 0xFF0000) >> 16)
			      + ((pixel_mask & 0xFF00FF00)));
		break;

#if 3 == VBI_VERSION_MINOR
	case VBI_PIXFMT_YVU24_BE:	/* 0x00YYVVUU */
		pixel_mask = (+ ((pixel_mask & 0xFF) << 16)
			      + ((pixel_mask & 0xFFFF00) >> 8));
		break;
#endif
	case VBI_PIXFMT_BGRA24_BE:	/* 0xBBGGRRAA */
		pixel_mask = (+ ((pixel_mask & 0xFFFFFF) << 8)
			      + ((pixel_mask & 0xFF000000) >> 24));
		break;

	default:
		break;
	}

	switch (sp->sp_sample_format) {
	case VBI_PIXFMT_RGB16_LE:
	case VBI_PIXFMT_RGB16_BE:
	case VBI_PIXFMT_BGR16_LE:
	case VBI_PIXFMT_BGR16_BE:
		pixel_mask = RGBA_TO_RGB16 (pixel_mask);
		break;

	case VBI_PIXFMT_RGBA15_LE:
	case VBI_PIXFMT_RGBA15_BE:
	case VBI_PIXFMT_BGRA15_LE:
	case VBI_PIXFMT_BGRA15_BE:
		pixel_mask = RGBA_TO_RGBA15 (pixel_mask);
		break;

	case VBI_PIXFMT_ARGB15_LE:
	case VBI_PIXFMT_ARGB15_BE:
	case VBI_PIXFMT_ABGR15_LE:
	case VBI_PIXFMT_ABGR15_BE:
		pixel_mask = RGBA_TO_ARGB15 (pixel_mask);
		break;

#if 3 == VBI_VERSION_MINOR
	case VBI_PIXFMT_RGBA12_LE:
	case VBI_PIXFMT_RGBA12_BE:
	case VBI_PIXFMT_BGRA12_LE:
	case VBI_PIXFMT_BGRA12_BE:
		pixel_mask = RGBA_TO_RGBA12 (pixel_mask);
		break;

	case VBI_PIXFMT_ARGB12_LE:
	case VBI_PIXFMT_ARGB12_BE:
	case VBI_PIXFMT_ABGR12_LE:
	case VBI_PIXFMT_ABGR12_BE:
		pixel_mask = RGBA_TO_ARGB12 (pixel_mask);
		break;

	case VBI_PIXFMT_RGB8:
		pixel_mask = RGBA_TO_RGB8 (pixel_mask);
		break;

	case VBI_PIXFMT_BGR8:
		pixel_mask = RGBA_TO_BGR8 (pixel_mask);
		break;

	case VBI_PIXFMT_RGBA7:
	case VBI_PIXFMT_BGRA7:
		pixel_mask = RGBA_TO_RGBA7 (pixel_mask);
		break;

	case VBI_PIXFMT_ARGB7:
	case VBI_PIXFMT_ABGR7:
		pixel_mask = RGBA_TO_ARGB7 (pixel_mask);
		break;
#endif
	default:
		break;
	}

	if (0 == pixel_mask) {
		/* Done! :-) */
		return TRUE;
	}

	/* ITU-R BT.601 sampling assumed. */

	if (SYSTEM_525 (sp)) {
		if (0 == white_level) {
			/* Cutting off the bottom of the signal
			   confuses the vbi_bit_slicer (can't adjust
			   the threshold fast enough), probably other
			   decoders as well. */
			blank_level = 5; /* 16 - 40 * 220 / 100; */
			black_level = 16;
			white_level = 16 + 219;
		}
	} else {
		if (0 == white_level) {
			/* Observed values: 30-30-280 (WSS PAL) -? */
			blank_level = 5; /* 16 - 43 * 220 / 100; */
			black_level = 16;
			white_level = 16 + 219;
		}
	}

	sp8 = *sp;

	samples_per_line = SAMPLES_PER_LINE (sp);

#if 3 == VBI_VERSION_MINOR
	sp8.sample_format = VBI_PIXFMT_Y8;
#else
	sp8.sampling_format = VBI_PIXFMT_YUV420;
#endif

	sp8.bytes_per_line = samples_per_line * 1 /* bpp */;

	size = n_scan_lines * samples_per_line;
	buf = vbi_malloc (size);
	if (NULL == buf) {
		error (NULL, "Out of memory.");
		errno = ENOMEM;
		return FALSE;
	}

	if (!signal_u8 (buf, &sp8,
			blank_level, black_level, white_level,
			flags,
			sliced, n_sliced_lines,
			__FUNCTION__)) {
		vbi_free (buf);
		return FALSE;
	}

	s = buf;
	d = raw;

	while (n_scan_lines-- > 0) {
		unsigned int i;

		switch (sp->sp_sample_format) {
#if 3 == VBI_VERSION_MINOR
		case VBI_PIXFMT_NONE:
		case VBI_PIXFMT_RESERVED0:
		case VBI_PIXFMT_RESERVED1:
		case VBI_PIXFMT_RESERVED2:
		case VBI_PIXFMT_RESERVED3:
			break;

		case VBI_PIXFMT_YUV444:
		case VBI_PIXFMT_YVU444:
		case VBI_PIXFMT_YUV422:
		case VBI_PIXFMT_YVU422:
		case VBI_PIXFMT_YUV411:
		case VBI_PIXFMT_YVU411:
		case VBI_PIXFMT_YVU420:
		case VBI_PIXFMT_YUV410:
		case VBI_PIXFMT_YVU410:
		case VBI_PIXFMT_Y8:
#endif
#if 2 == VBI_VERSION_MINOR
		case VBI_PIXFMT_PAL8:
#endif
		case VBI_PIXFMT_YUV420:
			for (i = 0; i < samples_per_line; ++i)
				MST1 (d[i], s[i], pixel_mask);
			break;

#if 3 == VBI_VERSION_MINOR
		case VBI_PIXFMT_YUVA24_LE:
		case VBI_PIXFMT_YVUA24_LE:
		case VBI_PIXFMT_YUVA24_BE:
		case VBI_PIXFMT_YVUA24_BE:
#endif
		case VBI_PIXFMT_RGBA24_LE:
		case VBI_PIXFMT_RGBA24_BE:
		case VBI_PIXFMT_BGRA24_LE:
		case VBI_PIXFMT_BGRA24_BE:
			SCAN_LINE_TO_N (+, 4);
			break;

#if 3 == VBI_VERSION_MINOR
		case VBI_PIXFMT_YUV24_LE:
		case VBI_PIXFMT_YUV24_BE:
		case VBI_PIXFMT_YVU24_LE:
		case VBI_PIXFMT_YVU24_BE:
#endif
		case VBI_PIXFMT_RGB24_LE:
		case VBI_PIXFMT_BGR24_LE:
			SCAN_LINE_TO_N (+, 3);
			break;

		case VBI_PIXFMT_YUYV:
		case VBI_PIXFMT_YVYU:
			for (i = 0; i < samples_per_line; i += 2) {
				uint8_t *dd = d + i * 2;
				unsigned int uv = (s[i] + s[i + 1] + 1) >> 1;

				MST1 (dd[0], s[i], pixel_mask);
				MST1 (dd[1], uv, pixel_mask >> 8);
				MST1 (dd[2], s[i + 1], pixel_mask);
				MST1 (dd[3], uv, pixel_mask >> 16);
			}
			break;

		case VBI_PIXFMT_UYVY:
		case VBI_PIXFMT_VYUY:
			for (i = 0; i < samples_per_line; i += 2) {
				uint8_t *dd = d + i * 2;
				unsigned int uv = (s[i] + s[i + 1] + 1) >> 1;

				MST1 (dd[0], uv, pixel_mask >> 8);
				MST1 (dd[1], s[i], pixel_mask);
				MST1 (dd[2], uv, pixel_mask >> 16);
				MST1 (dd[3], s[i + 1], pixel_mask);
			}
			break;

		case VBI_PIXFMT_RGB16_LE:
		case VBI_PIXFMT_BGR16_LE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_RGB16, 0);
			break;

		case VBI_PIXFMT_RGB16_BE:
		case VBI_PIXFMT_BGR16_BE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_RGB16, 1);
			break;

		case VBI_PIXFMT_RGBA15_LE:
		case VBI_PIXFMT_BGRA15_LE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_RGBA15, 0);
			break;

		case VBI_PIXFMT_RGBA15_BE:
		case VBI_PIXFMT_BGRA15_BE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_RGBA15, 1);
			break;

		case VBI_PIXFMT_ARGB15_LE:
		case VBI_PIXFMT_ABGR15_LE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_ARGB15, 0);
			break;

		case VBI_PIXFMT_ARGB15_BE:
		case VBI_PIXFMT_ABGR15_BE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_ARGB15, 1);
			break;

#if 3 == VBI_VERSION_MINOR
		case VBI_PIXFMT_RGBA12_LE:
		case VBI_PIXFMT_BGRA12_LE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_RGBA12, 0);
			break;

		case VBI_PIXFMT_RGBA12_BE:
		case VBI_PIXFMT_BGRA12_BE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_RGBA12, 1);
			break;

		case VBI_PIXFMT_ARGB12_LE:
		case VBI_PIXFMT_ABGR12_LE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_ARGB12, 0);
			break;

		case VBI_PIXFMT_ARGB12_BE:
		case VBI_PIXFMT_ABGR12_BE:
			SCAN_LINE_TO_RGB2 (RGBA_TO_ARGB12, 1);
			break;

		case VBI_PIXFMT_RGB8:
			SCAN_LINE_TO_N (RGBA_TO_RGB8, 1);
			break;

		case VBI_PIXFMT_BGR8:
			SCAN_LINE_TO_N (RGBA_TO_BGR8, 1);
			break;

		case VBI_PIXFMT_RGBA7:
		case VBI_PIXFMT_BGRA7:
			SCAN_LINE_TO_N (RGBA_TO_RGBA7, 1);
			break;

		case VBI_PIXFMT_ARGB7:
		case VBI_PIXFMT_ABGR7:
			SCAN_LINE_TO_N (RGBA_TO_ARGB7, 1);
			break;
#endif /* 3 == VBI_VERSION_MINOR */
		}

		s += sp8.bytes_per_line;
		d += sp->bytes_per_line;
	}

	vbi_free (buf);

	return TRUE;
}

/**
 * @example examples/rawout.c
 * Raw VBI output example.
 */

/**
 * @param raw A raw VBI image will be stored here.
 * @param raw_size Size of the @a raw buffer in bytes. The buffer
 *   must be large enough for @a sp->count[0] + count[1] lines
 *   of @a sp->bytes_per_line each, with @a sp->samples_per_line
 *   (in libzvbi 0.2.x @a sp->bytes_per_line) bytes actually written.
 * @param sp Describes the raw VBI data to generate. @a sp->sampling_format
 *   must be @c VBI_PIXFMT_Y8 (@c VBI_PIXFMT_YUV420 with libzvbi 0.2.x).
 *   @a sp->synchronous is ignored. Note for compatibility in libzvbi
 *   0.2.x vbi_sampling_par is a synonym of vbi_raw_decoder, but the
 *   (private) decoder fields in this structure are ignored.
 * @param blank_level The level of the horizontal blanking in the raw
 *   VBI image. Must be <= @a white_level.
 * @param white_level The peak white level in the raw VBI image. Set to
 *   zero to get the default blanking and white level.
 * @param swap_fields If @c TRUE the second field will be stored first
 *   in the @c raw buffer. Note you can also get an interlaced image
 *   by setting @a sp->interlaced to @c TRUE. @a sp->synchronous is
 *   ignored.
 * @param sliced Pointer to an array of vbi_sliced containing the
 *   VBI data to be encoded.
 * @param n_sliced_lines Number of elements in the @a sliced array.
 *
 * This function basically reverses the operation of the vbi_raw_decoder,
 * taking sliced VBI data and generating a raw VBI image similar to those
 * you would get from raw VBI sampling hardware. The following data services
 * are currently supported: All Teletext services, VPS, WSS 625, Closed
 * Caption 525 and 625.
 *
 * The function encodes sliced data as is, e.g. without adding or
 * checking parity bits, without checking if the line number is correct
 * for the respective data service, or if the signal will fit completely
 * in the given space (@a sp->offset and @a sp->samples_per_line at
 * @a sp->sampling_rate).
 *
 * Apart of the payload the generated video signal is invariable and
 * attempts to be faithful to related standards. You can only change the
 * characteristics of the assumed capture device. Sync pulses and color
 * bursts and not generated if the sampling parameters extend to this area.
 *
 * @note
 * This function is mainly intended for testing purposes. It is optimized
 * for accuracy, not for speed.
 *
 * @returns
 * @c FALSE if the @a raw_size is too small, if the @a sp sampling
 * parameters are invalid, if the signal levels are invalid,
 * if the @a sliced array contains unsupported services or line numbers
 * outside the @a sp sampling parameters.
 *
 * @since 0.2.22
 */
vbi_bool
vbi_raw_vbi_image		(uint8_t *		raw,
				 unsigned long		raw_size,
				 const vbi_sampling_par *sp,
				 int			blank_level,
				 int			white_level,
				 vbi_bool		swap_fields,
				 const vbi_sliced *	sliced,
				 unsigned int		n_sliced_lines)
{
	return _vbi_raw_vbi_image (raw, raw_size, sp,
				   blank_level, white_level,
				   swap_fields ? _VBI_RAW_SWAP_FIELDS : 0,
				   sliced, n_sliced_lines);
}

/**
 * @param raw A raw VBI image will be stored here.
 * @param raw_size Size of the @a raw buffer in bytes. The buffer
 *   must be large enough for @a sp->count[0] + count[1] lines
 *   of @a sp->bytes_per_line each, with @a sp->samples_per_line
 *   times bytes per pixel (in libzvbi 0.2.x @a sp->bytes_per_line)
 *   actually written.
 * @param sp Describes the raw VBI data to generate. Note for
 *  compatibility in libzvbi 0.2.x vbi_sampling_par is a synonym of
 *  vbi_raw_decoder, but the (private) decoder fields in this
 *  structure are ignored.
 * @param blank_level The level of the horizontal blanking in the raw
 *   VBI image. Must be <= @a black_level.
 * @param black_level The black level in the raw VBI image. Must be
 *   <= @a white_level.
 * @param white_level The peak white level in the raw VBI image. Set to
 *   zero to get the default blanking, black and white level.
 * @param pixel_mask This mask selects which color or alpha channel
 *   shall contain VBI data. Depending on @a sp->sampling_format it is
 *   interpreted as 0xAABBGGRR or 0xAAVVUUYY. A value of 0x000000FF
 *   for example writes data in "red bits", not changing other
 *   bits in the @a raw buffer. When the @a sp->sampling_format is a
 *   planar YUV the function writes the Y plane only.
 * @param swap_fields If @c TRUE the second field will be stored first
 *   in the @c raw buffer. Note you can also get an interlaced image
 *   by setting @a sp->interlaced to @c TRUE. @a sp->synchronous is
 *   ignored.
 * @param sliced Pointer to an array of vbi_sliced containing the
 *   VBI data to be encoded.
 * @param n_sliced_lines Number of elements in the @a sliced array.
 *
 * Generates a raw VBI image similar to those you get from video
 * capture hardware. Otherwise identical to vbi_raw_vbi_image().
 *
 * @returns
 * @c FALSE if the @a raw_size is too small, if the @a sp sampling
 * parameters are invalid, if the signal levels are invalid,
 * if the @a sliced array contains unsupported services or line numbers
 * outside the @a sp sampling parameters.
 *
 * @since 0.2.22
 */
vbi_bool
vbi_raw_video_image		(uint8_t *		raw,
				 unsigned long		raw_size,
				 const vbi_sampling_par *sp,
				 int			blank_level,
				 int			black_level,
				 int			white_level,
				 unsigned int		pixel_mask,
				 vbi_bool		swap_fields,
				 const vbi_sliced *	sliced,
				 unsigned int		n_sliced_lines)
{
	return _vbi_raw_video_image (raw, raw_size, sp,
				     blank_level, black_level,
				     white_level, pixel_mask,
				     swap_fields ? _VBI_RAW_SWAP_FIELDS : 0,
				     sliced, n_sliced_lines);
}

/*
	Capture interface
*/

#if 3 == VBI_VERSION_MINOR
#  include "io-priv.h"
#else
#  include "io.h"
#endif
#include "hamm.h"

#define MAGIC 0xd804289c

struct buffer {
	char *			data;
	unsigned int		size;
	unsigned int		capacity;
};

typedef struct {
	vbi_capture		cap;

	unsigned int		magic;

	vbi_sampling_par	sp;

	vbi3_raw_decoder *	rd;

	vbi_bool		decode_raw;

	vbi_capture_buffer	raw_buffer;
	size_t			raw_f1_size;
	size_t			raw_f2_size;

	uint8_t *		desync_buffer[2];
	unsigned int		desync_i;

	double			capture_time;
	int64_t			stream_time;

	vbi_capture_buffer	sliced_buffer;
	vbi_sliced		sliced[50];

	unsigned int		teletext_page;
	unsigned int		teletext_row;

	struct buffer		caption_buffers[2];
	unsigned int		caption_i;

	uint8_t			vps_buffer[13];

	uint8_t			wss_buffer[2];

	unsigned int		noise_min_freq;
	unsigned int		noise_max_freq;
	unsigned int		noise_amplitude;
	unsigned int		noise_seed;

	unsigned int		flags;
} vbi_capture_sim;

unsigned int
_vbi_capture_sim_get_flags	(vbi_capture *		cap)
{
	vbi_capture_sim *sim;

	assert (NULL != cap);

	sim = PARENT (cap, vbi_capture_sim, cap);
	assert (MAGIC == sim->magic);

	return sim->flags;
}

void
_vbi_capture_sim_set_flags	(vbi_capture *		cap,
				 unsigned int		flags)
{
	vbi_capture_sim *sim;

	assert (NULL != cap);

	sim = PARENT (cap, vbi_capture_sim, cap);
	assert (MAGIC == sim->magic);

	sim->flags = flags;
}

/**
 * @param cap Initialized vbi_capture context opened with
 *   vbi_capture_sim_new().
 * @param min_freq Minimum frequency of the noise in Hz.
 * @param max_freq Maximum frequency of the noise in Hz. @a min_freq and
 *   @a max_freq define the cut off frequency at the half power points
 *   (gain -3 dB).
 * @param amplitude Maximum amplitude of the noise, should lie in range
 *   0 to 256.
 *
 * This function shapes the white noise to be added to simulated raw VBI
 * data. By default no noise is added. To disable the noise set
 * @a amplitude to zero.
 *
 * To produce realistic noise @a min_freq = 0, @a max_freq = 5e6 and
 * @a amplitude = 20 to 50 seems appropriate.
 *
 * @since 0.2.26
 */
void
vbi_capture_sim_add_noise	(vbi_capture *		cap,
				 unsigned int		min_freq,
				 unsigned int		max_freq,
				 unsigned int		amplitude)
{
	vbi_capture_sim *sim;

	assert (NULL != cap);

	sim = PARENT (cap, vbi_capture_sim, cap);
	assert (MAGIC == sim->magic);

	if (0 == max_freq)
		amplitude = 0;

	sim->noise_min_freq = min_freq;
	sim->noise_max_freq = max_freq;
	sim->noise_amplitude = amplitude;

	sim->noise_seed = 123456789;
}

static vbi_bool
extend_buffer			(struct buffer *	b,
				 unsigned int		new_capacity)
{
	char *new_data;

	new_data = vbi_realloc (b->data, new_capacity);
	if (NULL == new_data)
		return FALSE;

	b->data = new_data;
	b->capacity = new_capacity;

	return TRUE;
}

static const char
caption_default_test_stream [] =
	"<edm ch=\"0\"/><ru4/><pac row=\"15\"/>"
	"LIBZVBI CAPTION SIMULATION CC1.<cr/>"
	"<edm ch=\"1\"/><ru4/><pac row=\"15\"/>"
	"LIBZVBI CAPTION SIMULATION CC2.<cr/>"
	"<edm ch=\"2\"/><ru4/><pac row=\"15\"/>"
	"LIBZVBI CAPTION SIMULATION CC3.<cr/>"
	"<edm ch=\"3\"/><ru4/><pac row=\"15\"/>"
	"LIBZVBI CAPTION SIMULATION CC4.<cr/>"
;
	/* TODO: regression test for repeated control code bug:
	   <control code field 1>
	   <zero field 2>
	   <repeated control code field 1, to be ignored> */ 

static vbi_bool
get_attr			(unsigned int *		value,
				 const char *		s,
				 const char *		name,
				 unsigned int		default_value,
				 unsigned int		minimum,
				 unsigned int		maximum)
{
	unsigned long u;
	unsigned int len;
	vbi_bool present;

	present = FALSE;
	u = default_value;

	len = strlen (name);

	for (; 0 != *s && '>' != *s; ++s) {
		int delta;

		if (!isalpha (*s))
			continue;

		delta = strncmp (s, name, len);
		if (0 == delta) {
			s += len;
		} else {
			while (isalnum (*s))
				++s;
		}

		while (isspace (*s++))
			;

		if ('=' != s[-1] || '"' != *s)
			break;

		if (0 == delta) {
			present = TRUE;
			u = strtoul (s + 1, NULL, 0);
			break;
		}

		do ++s;
		while (0 != *s && '"' != *s);
	}

	*value = SATURATE (u, (unsigned long) minimum,
			   (unsigned long) maximum);

	return present;
}

static vbi_bool
caption_append_zeroes		(vbi_capture_sim *	sim,
				 vbi_pgno		channel,
				 unsigned int		n_bytes)
{
	struct buffer *b;

	b = &sim->caption_buffers[((channel - 1) >> 1) & 1];

	if (b->size + n_bytes > b->capacity) {
		unsigned int new_capacity;

		new_capacity = b->capacity + ((n_bytes + 255) & ~255);
		if (!extend_buffer (b, new_capacity))
			return FALSE;
	}

	memset (b->data + b->size, 0x80, n_bytes);
	b->size += n_bytes;

	return TRUE;
}

static unsigned int
caption_append_command		(vbi_capture_sim *	sim,
				 unsigned int *		inout_ch,
				 const char *		s)
{
	static const _vbi_key_value_pair elements [] = {
		{ "aof", 0x1422 },
		{ "aon", 0x1423 },
		{ "bao", 0x102E },
		{ "bas", 0x102F },
		{ "bbo", 0x1024 },
		{ "bbs", 0x1025 },
		{ "bco", 0x1026 },
		{ "bcs", 0x1027 },
		{ "bgo", 0x1022 },
		{ "bgs", 0x1023 },
		{ "bmo", 0x102C },
		{ "bms", 0x102D },
		{ "bro", 0x1028 },
		{ "brs", 0x1029 },
		{ "bs", 0x1421 },
		{ "bt", 0x172D },
		{ "bwo", 0x1020 },
		{ "bws", 0x1021 },
		{ "byo", 0x102A },
		{ "bys", 0x102B },
		{ "cmd", 0x0001 },
		{ "cr", 0x142D },
		{ "der", 0x1424 },
		{ "edm", 0x142C },
		{ "enm", 0x142E },
		{ "eoc", 0x142F },
		{ "ext2", 0x1200 },
		{ "ext3", 0x1300 },
		{ "fa", 0x172E },
		{ "fau", 0x172F },
		{ "fon", 0x1428 },
		{ "mr", 0x1120 },
		{ "pac", 0x1040 },
		{ "pause", 0x0002 },
		{ "rcl", 0x1420 },
		{ "rdc", 0x1429 },
		{ "rtd", 0x142B },
		{ "ru2", 0x1425 },
		{ "ru3", 0x1426 },
		{ "ru4", 0x1427 },
		{ "spec", 0x1130 },
		{ "sync", 0x0003 },
		{ "to1", 0x1721 },
		{ "to2", 0x1722 },
		{ "to3", 0x1723 },
		{ "tr", 0x142A },
	};
	static const int row_code [16] = {
		0x1140, 0x1160, 0x1240, 0x1260, 
		0x1540, 0x1560, 0x1640, 0x1660, 
		0x1740, 0x1760, 0x1040, 0x1340, 
		0x1360, 0x1440, 0x1460, -1
	};
	struct buffer *b;
	int value;
	unsigned int cmd;
	unsigned int n_frames;
	unsigned int u_value;
	int n_padding_bytes;
	unsigned int i;
	vbi_bool parity;

	if (!_vbi_keyword_lookup (&value, &s,
				  elements,
				  N_ELEMENTS (elements)))
		return TRUE;

	get_attr (inout_ch, s, "ch", *inout_ch, 1, 4);

	cmd = value | (((*inout_ch - 1) & 1) << 11);

	parity = TRUE;

	switch (value) {
	case 1: /* cmd */
		get_attr (&cmd, s, "code", 0, 0, 0xFFFF);
		parity = FALSE;
		break;

	case 2: /* pause */
		get_attr (&n_frames, s, "frames", 60, 1, INT_MAX);
		if (n_frames > 120 * 60 * 30)
			return TRUE;

		return caption_append_zeroes
			(sim, *inout_ch, n_frames * 2);

	case 3: /* sync */
		n_padding_bytes = sim->caption_buffers[0].size
			- sim->caption_buffers[1].size;

		if (0 == n_padding_bytes) {
			return TRUE;
		} else if (n_padding_bytes < 0) {
			return caption_append_zeroes
				(sim, 0, n_padding_bytes);
		} else {
			return caption_append_zeroes
				(sim, 2, n_padding_bytes);
		}

	case 0x1040: /* preamble address code */
		if (get_attr (&u_value, s, "column", 1, 1, 32)) {
			cmd |= 0x0010 | (((u_value - 1) / 4) << 1);
		} else {
			get_attr (&u_value, s, "color", 0, 0, 7);
			cmd |= u_value << 1;
		}
		get_attr (&u_value, s, "row", 15, 1, 15);
		cmd |= row_code[u_value - 1];
		get_attr (&u_value, s, "u", 0, 0, 1);
		cmd |= u_value;
		break;

	case 0x1120: /* midrow code */
		get_attr (&u_value, s, "color", 0, 0, 7);
		cmd |= u_value << 1;
		get_attr (&u_value, s, "u", 0, 0, 1);
		cmd |= u_value;
		break;

	case 0x1130: /* special character */
		get_attr (&u_value, s, "code", 0, 0, 15);
		cmd |= u_value;
		break;

	case 0x1200: /* extended character set */
	case 0x1300:
		get_attr (&u_value, s, "code", 32, 32, 63);
		cmd |= u_value;
		break;

	case 0x1420: /* rcl */
	case 0x1421: /* bs */
	case 0x1422: /* aof */
	case 0x1423: /* aon */
	case 0x1424: /* der */
	case 0x1425: /* ru3 */
	case 0x1426: /* ru4 */
	case 0x1427: /* ru5 */
	case 0x1428: /* fon */
	case 0x1429: /* rdc */
	case 0x142A: /* tr */
	case 0x142B: /* rtd */
	case 0x142C: /* edm */
	case 0x142D: /* cr */
	case 0x142E: /* enm */
	case 0x142F: /* eoc */
		/* Field bit (EIA 608-B Sec. 8.4, 8.5). */
		cmd |= ((*inout_ch - 1) & 2) << 7;

	default:
		break;
	}

	b = &sim->caption_buffers[((*inout_ch - 1) >> 1) & 1];
	i = b->size;

	if (i + 3 > b->capacity) {
		if (!extend_buffer (b, b->capacity + 256))
			return FALSE;
	}

	if (i & 1)
		b->data[i++] = 0x80;

	if (likely (parity)) {
		b->data[i] = vbi_par8 (cmd >> 8);
		b->data[i + 1] = vbi_par8 (cmd);
	} else {
		/* To test error checks. */
		b->data[i] = cmd >> 8;
		b->data[i + 1] = cmd;
	}

	b->size = i + 2;

	return TRUE;
}

vbi_bool
vbi_capture_sim_load_caption	(vbi_capture *		cap,
				 const char *		stream,
				 vbi_bool		append)
{
	vbi_capture_sim *sim;
	struct buffer *b;
	unsigned int ch;
	const char *s;

	assert (NULL != cap);

	sim = PARENT (cap, vbi_capture_sim, cap);
	assert (MAGIC == sim->magic);

	if (!append) {
		vbi_free (sim->caption_buffers[0].data);
		vbi_free (sim->caption_buffers[1].data);

		CLEAR (sim->caption_buffers);

		sim->caption_i = 0;
	}

	if (NULL == stream)
		return TRUE;

	ch = 1; /* CC1, T1 */

	b = &sim->caption_buffers[0];

	for (s = stream;;) {
		int c = *s++;

		if (0 == c) {
			break;
		} else if (c < 0x20) {
			continue;
		} else if ('&' == c) {
			if ('#' == *s) {
				char *end;

				c = strtoul (s + 1, &end, 10);
				s = end;

				if (';' == *s)
					++s;
			} else if (0 == strncmp (s, "amp;", 4)) {
				s += 4;
			} else if (0 == strncmp (s, "lt;", 3)) {
				s += 3;
				c = '<';
			} else if (0 == strncmp (s, "gt;", 3)) {
				s += 3;
				c = '>';
			} else if (0 == strncmp (s, "ts;", 3)) {
				/* Transparent space. */
				if (!caption_append_command
				    (sim, &ch, "<spec code=\"9\"/>"))
					return FALSE;
				continue;
			}
		} else if ('<' == c) {
			int delimiter;

			if (!caption_append_command (sim, &ch, s))
				return FALSE;

			b = &sim->caption_buffers[((ch - 1) >> 1) & 1];

			/* Skip until '>', except between quotes. */
			delimiter = '>';
			for (; 0 != *s && delimiter != *s; ++s) {
				if ('"' == *s)
					delimiter ^= '>';
			}

			if (0 != *s)
				++s; /* skip delimiter */

			continue;
		}

		if (b->size >= b->capacity) {
			if (!extend_buffer (b, b->capacity + 256))
				return FALSE;
		}

		b->data[b->size++] = vbi_par8 (c);
	}

	return TRUE;
}

static void
gen_caption			(vbi_capture_sim *	sim,
				 vbi_sliced **		inout_sliced,
				 vbi_service_set	service_set,
				 unsigned int		line)
{
	vbi_sliced *s;
	struct buffer *b;
	unsigned int i;

	b = &sim->caption_buffers[(line > 200)];

	i = sim->caption_i;

	if (i + 1 < b->size) {
		s = *inout_sliced;
		*inout_sliced = s + 1;

		s->id = service_set;
		s->line = line;
		s->data[0] = b->data[i];
		s->data[1] = b->data[i + 1];
	}
}

static void
gen_teletext_b_row		(vbi_capture_sim *	sim,
				 uint8_t	 	return_buf[45])
{
	static uint8_t s1[2][10] = {
		{ 0x02, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15 },
		{ 0x02, 0x15, 0x02, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15 }
	};
	static uint8_t s2[32] = "100\2LIBZVBI\7            00:00:00";
	static uint8_t s3[40] = "  LIBZVBI TELETEXT SIMULATION           ";
	static uint8_t s4[40] = "  Page 100                              ";
	static uint8_t s5[10][42] = {
		{ 0x02, 0x2f, 0x97, 0x20, 0x37, 0x23, 0x23, 0x23, 0x23, 0x23,
		  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
		  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
		  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
		  0xb5, 0x20 },
		{ 0xc7, 0x2f, 0x97, 0x0d, 0xb5, 0x04, 0x20, 0x9d, 0x83, 0x8c,
		  0x08, 0x2a, 0x2a, 0x2a, 0x89, 0x20, 0x20, 0x0d, 0x54, 0x45,
		  0xd3, 0x54, 0x20, 0xd0, 0xc1, 0xc7, 0x45, 0x8c, 0x20, 0x20,
		  0x08, 0x2a, 0x2a, 0x2a, 0x89, 0x0d, 0x20, 0x20, 0x1c, 0x97,
		  0xb5, 0x20 },
		{ 0x02, 0xd0, 0x97, 0x20, 0xb5, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0xea, 0x20 },
		{ 0xc7, 0xd0, 0x97, 0x20, 0xb5, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0xb5, 0x20 },
		{ 0x02, 0xc7, 0x97, 0x20, 0xb5, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x15, 0x1a, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c,
		  0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c,
		  0x2c, 0x2c, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x97, 0x19,
		  0xb5, 0x20 },
		{ 0xc7, 0xc7, 0x97, 0x20, 0xb5, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		  0xb5, 0x20 },
		{ 0x02, 0x8c, 0x97, 0x9e, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x13,
		  0x7f, 0x7f, 0x7f, 0x7f, 0x16, 0x7f, 0x7f, 0x7f, 0x7f, 0x92,
		  0x7f, 0x92, 0x7f, 0x7f, 0x15, 0x7f, 0x7f, 0x15, 0x7f, 0x91,
		  0x91, 0x7f, 0x7f, 0x91, 0x94, 0x7f, 0x94, 0x7f, 0x94, 0x97,
		  0xb5, 0x20 },
		{ 0xc7, 0x8c, 0x97, 0x9e, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x13,
		  0x7f, 0x7f, 0x7f, 0x7f, 0x16, 0x7f, 0x7f, 0x7f, 0x7f, 0x92,
		  0x7f, 0x7f, 0x7f, 0x7f, 0x15, 0x7f, 0x7f, 0x7f, 0x7f, 0x91,
		  0x7f, 0x7f, 0x7f, 0x7f, 0x94, 0x7f, 0x7f, 0x7f, 0x7f, 0x97,
		  0xb5, 0x20 },
		{ 0x02, 0x9b, 0x97, 0x9e, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x13,
		  0x7f, 0x7f, 0x7f, 0x7f, 0x16, 0x7f, 0x7f, 0x7f, 0x7f, 0x92,
		  0x7f, 0x7f, 0x7f, 0x7f, 0x15, 0x7f, 0x7f, 0x7f, 0x7f, 0x91,
		  0x7f, 0x7f, 0x7f, 0x7f, 0x94, 0x7f, 0x7f, 0x7f, 0x7f, 0x97,
		  0xb5, 0x20 },
		{ 0xc7, 0x9b, 0x97, 0x20, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
		  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
		  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
		  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
		  0xa1, 0x20 }
	};
	unsigned int i;

	return_buf[0] = 0x55;
	return_buf[1] = 0x55;
	return_buf[2] = 0x27;

	if (sim->teletext_row >= 13)
		sim->teletext_row = 0;

	switch (sim->teletext_row) {
	case 0:
		memcpy (return_buf + 3, s1[sim->teletext_page], 10);
		sim->teletext_page ^= 1;
		for (i = 0; i < 32; ++i)
			return_buf[13 + i] = vbi_par8 (s2[i]);
		break;

	case 1:
		return_buf[3] = 0x02;
		return_buf[4] = 0x02;
		for (i = 0; i < 40; ++i)
			return_buf[5 + i] = vbi_par8 (s3[i]);
		break;

	case 2:
		return_buf[3] = 0x02;
		return_buf[4] = 0x49;
		for (i = 0; i < 40; ++i)
			return_buf[5 + i] = vbi_par8 (s4[i]);
		break;

	default:
		memcpy (return_buf + 3, s5[sim->teletext_row - 3], 42);
		break;
	}

	++sim->teletext_row;
}

static void
gen_teletext_b			(vbi_capture_sim *	sim,
				 vbi_sliced **		inout_sliced,
				 vbi_sliced *		sliced_end,
				 unsigned int		line)
{
	uint8_t buf[45];
	vbi_sliced *s;

	s = *inout_sliced;

	if (s >= sliced_end)
		return;

	s->id = VBI_SLICED_TELETEXT_B;
	s->line = line;

	gen_teletext_b_row (sim, buf);
	memcpy (&s->data, buf + 3, 42);

	*inout_sliced = s + 1;
}

static unsigned int
gen_sliced_525			(vbi_capture_sim *	sim)
{
	vbi_sliced *s;
	unsigned int i;

	s = sim->sliced;

	assert (N_ELEMENTS (sim->sliced) >= 4);

	if (0) {
		for (i = 0; i < N_ELEMENTS (s->data); ++i)
			s->data[i] = rand ();

		s[1] = s[0];
		s[2] = s[0];

		s[0].id = VBI_SLICED_TELETEXT_B_525;
		s[0].line = 10;
		s[1].id = VBI_SLICED_TELETEXT_C_525;
		s[1].line = 11;
		s[2].id = VBI_SLICED_TELETEXT_D_525;
		s[2].line = 12;

		s += 3;
	}

	if (sim->caption_buffers[0].size > 0)
		gen_caption (sim, &s, VBI_SLICED_CAPTION_525, 21);

	if (sim->caption_buffers[1].size > 0)
		gen_caption (sim, &s, VBI_SLICED_CAPTION_525, 284);

	sim->caption_i += 2;
	if (sim->caption_i >= sim->caption_buffers[0].size
	    && sim->caption_i >= sim->caption_buffers[1].size)
		sim->caption_i = 0;

	return s - sim->sliced;
}

#if 3 == VBI_VERSION_MINOR

vbi_bool
vbi_capture_sim_load_vps	(vbi_capture *		cap,
				 const vbi_program_id *pid)
{
	vbi_capture_sim *sim;
	vbi_program_id pid2;

	assert (NULL != cap);

	sim = PARENT (cap, vbi_capture_sim, cap);
	assert (MAGIC == sim->magic);

	if (NULL == pid) {
		CLEAR (pid2);

		pid2.cni_type	= VBI_CNI_TYPE_VPS;
		pid2.channel	= VBI_PID_CHANNEL_VPS;
		pid2.pil	= VBI_PIL_TIMER_CONTROL;

		pid = &pid2;
	}

	return vbi_encode_vps_pdc (sim->vps_buffer, pid);
}

vbi_bool
vbi_capture_sim_load_wss_625	(vbi_capture *		cap,
				 const vbi_aspect_ratio *ar)
{
	vbi_capture_sim *sim;
	vbi_aspect_ratio ar2;

	assert (NULL != cap);

	sim = PARENT (cap, vbi_capture_sim, cap);
	assert (MAGIC == sim->magic);

	if (NULL == ar) {
		CLEAR (ar2);
		ar = &ar2;
	}

	return vbi_encode_wss_625 (sim->wss_buffer, ar);
}

#endif /* 3 == VBI_VERSION_MINOR */

static unsigned int
gen_sliced_625			(vbi_capture_sim *	sim)
{
	vbi_sliced *s;
	vbi_sliced *end;
	unsigned int i;

	s = sim->sliced;
	end = &sim->sliced[N_ELEMENTS (sim->sliced)];

	assert (N_ELEMENTS (sim->sliced) >= 5);

	if (0) {
		for (i = 0; i < N_ELEMENTS (s->data); ++i)
			s->data[i] = rand ();

		s[1] = s[0];
		s[2] = s[0];

		s[0].id = VBI_SLICED_TELETEXT_A;
		s[0].line = 6;
		s[1].id = VBI_SLICED_TELETEXT_C_625;
		s[1].line = 7;
		s[2].id = VBI_SLICED_TELETEXT_D_625;
		s[2].line = 8;
		
		s += 3;
	}

	gen_teletext_b (sim, &s, end - 3, 9);
	gen_teletext_b (sim, &s, end - 3, 10);
	gen_teletext_b (sim, &s, end - 3, 11);
	gen_teletext_b (sim, &s, end - 3, 12);
	gen_teletext_b (sim, &s, end - 3, 13);
	gen_teletext_b (sim, &s, end - 3, 14);
	gen_teletext_b (sim, &s, end - 3, 15);

	s->id = VBI_SLICED_VPS;
	s->line = 16;
	assert (sizeof (s->data) >= sizeof (sim->vps_buffer));
	memcpy (s->data, sim->vps_buffer, sizeof (sim->vps_buffer));
	++s;

	gen_teletext_b (sim, &s, end - 2, 19);
	gen_teletext_b (sim, &s, end - 2, 20);
	gen_teletext_b (sim, &s, end - 2, 21);

	if (sim->caption_buffers[0].size > 0)
		gen_caption (sim, &s, VBI_SLICED_CAPTION_625, 22);

	sim->caption_i += 2;
	if (sim->caption_i >= sim->caption_buffers[0].size)
		sim->caption_i = 0;

	s->id = VBI_SLICED_WSS_625;
	s->line = 23;
	assert (sizeof (s->data) >= sizeof (sim->wss_buffer));
	memcpy (s->data, sim->wss_buffer, sizeof (sim->wss_buffer));
	++s;

	gen_teletext_b (sim, &s, end, 320);
	gen_teletext_b (sim, &s, end, 321);
	gen_teletext_b (sim, &s, end, 322);
	gen_teletext_b (sim, &s, end, 323);
	gen_teletext_b (sim, &s, end, 324);
	gen_teletext_b (sim, &s, end, 325);
	gen_teletext_b (sim, &s, end, 326);
	gen_teletext_b (sim, &s, end, 327);
	gen_teletext_b (sim, &s, end, 328);
	gen_teletext_b (sim, &s, end, 332);
	gen_teletext_b (sim, &s, end, 333);
	gen_teletext_b (sim, &s, end, 334);
	gen_teletext_b (sim, &s, end, 335);

	return s - sim->sliced;
}

/**
 * @param cap Initialized vbi_capture context opened with
 *   vbi_capture_sim_new().
 * @param enable @c TRUE to enable decoding of the simulated raw
 *   VBI data.
 *
 * By default this module generates sliced VBI data and converts it
 * to raw VBI data, returning both through the read functions. With
 * this function you can enable decoding of the raw VBI data back
 * to sliced VBI data, which is mainly interesting to test the
 * libzvbi bit slicer and raw VBI decoder.
 *
 * @since 0.2.22
 */
void
vbi_capture_sim_decode_raw	(vbi_capture *		cap,
				 vbi_bool		enable)
{
	vbi_capture_sim *sim;

	assert (NULL != cap);

	sim = PARENT (cap, vbi_capture_sim, cap);
	assert (MAGIC == sim->magic);

	sim->decode_raw = !!enable;
}

static void
copy_field			(uint8_t *		dst,
				 const uint8_t *	src,
				 unsigned int		height,
				 unsigned long		bytes_per_line)
{
	while (height-- > 0) {
		memcpy (dst, src, bytes_per_line);

		dst += bytes_per_line;
		src += bytes_per_line * 2;
	}
}

static void
delay_raw_data			(vbi_capture_sim *	sim,
				 uint8_t *		raw_data)
{
	unsigned int i;

	/* Delay the raw VBI data by one field. */

	i = sim->desync_i;

	if (sim->sp.interlaced) {
		assert (sim->sp.count[0] == sim->sp.count[1]);

		copy_field (sim->desync_buffer[i ^ 1],
			    raw_data + sim->sp.bytes_per_line,
			    sim->sp.count[0], sim->sp.bytes_per_line);
			    
		copy_field (raw_data + sim->sp.bytes_per_line,
			    raw_data,
			    sim->sp.count[0], sim->sp.bytes_per_line);

		copy_field (raw_data, sim->desync_buffer[i],
			    sim->sp.count[0], sim->sp.bytes_per_line);
	} else {
		memcpy (sim->desync_buffer[i ^ 1],
			raw_data + sim->raw_f1_size,
			sim->raw_f2_size);

		memmove (raw_data + sim->raw_f2_size,
			 raw_data,
			 sim->raw_f1_size);

		memcpy (raw_data,
			sim->desync_buffer[i],
			sim->raw_f2_size);
	}

	sim->desync_i = i ^ 1;
}

static vbi_bool
sim_read			(vbi_capture *		cap,
				 vbi_capture_buffer **	raw,
				 vbi_capture_buffer **	sliced,
				 const struct timeval *	timeout)
{
	vbi_capture_sim *sim = PARENT (cap, vbi_capture_sim, cap);
	unsigned int n_lines;

	timeout = timeout;

	n_lines = 0;

	if (NULL != raw
	    || NULL != sliced) {
		if (SYSTEM_525 (&sim->sp)) {
			n_lines = gen_sliced_525 (sim);
		} else {
			n_lines = gen_sliced_625 (sim);
		}
	}

	if (NULL != raw) {
		uint8_t *raw_data;
		vbi_bool success;

		if (NULL == *raw) {
			/* Return our buffer. */
			*raw = &sim->raw_buffer;
			raw_data = sim->raw_buffer.data;
		} else {
			/* XXX check max size here, after the API required
			   clients to pass one. */
			raw_data = (*raw)->data;
			(*raw)->size = sim->raw_buffer.size;
		}

		(*raw)->timestamp = sim->capture_time;

		memset (raw_data, 0x80, sim->raw_buffer.size);

		success = _vbi_raw_vbi_image (raw_data,
					       sim->raw_buffer.size,
					       &sim->sp,
					       /* blank_level: default */ 0,
					       /* white_level: default */ 0,
					       sim->flags,
					       sim->sliced,
					       n_lines);
		assert (success);

		if (sim->noise_amplitude > 0) {
			success = vbi_raw_add_noise (raw_data,
						      &sim->sp,
						      sim->noise_min_freq,
						      sim->noise_max_freq,
						      sim->noise_amplitude,
						      sim->noise_seed);
			assert (success);

			sim->noise_seed = sim->noise_seed
				* 1103515245 + 56789;
		}

		if (!sim->sp.synchronous)
			delay_raw_data (sim, raw_data);

		if (sim->decode_raw) {
			/* Decode the simulated raw VBI data to test our
			   encoder & decoder. */

			memset (sim->sliced, 0xAA, sizeof (sim->sliced));

			n_lines = vbi3_raw_decoder_decode
				(sim->rd,
				 sim->sliced, sizeof (sim->sliced),
				 raw_data);
		}
	}

	if (NULL != sliced) {
		if (NULL == *sliced) {
			/* Return our buffer. */
			*sliced = &sim->sliced_buffer;
		} else {
			/* XXX check max size here, after the API required
			   clients to pass one. */
			memcpy ((*sliced)->data, sim->sliced,
				n_lines * sizeof (sim->sliced[0]));
		}

		(*sliced)->size = n_lines * sizeof (sim->sliced[0]);
		(*sliced)->timestamp = sim->capture_time;
	}

	if (SYSTEM_525 (&sim->sp)) {
		sim->capture_time += 1001 / 30000.0;
	} else {
		sim->capture_time += 1 / 25.0;
	}

	return TRUE;
}

static vbi_bool
sim_sampling_point		(vbi_capture *		cap,
				 vbi3_bit_slicer_point *point,
				 unsigned int		row,
				 unsigned int		nth_bit)
{
	vbi_capture_sim *sim = PARENT (cap, vbi_capture_sim, cap);

	if (!sim->decode_raw)
		return FALSE;

	return vbi3_raw_decoder_sampling_point (sim->rd, point, row, nth_bit);
}

static vbi_bool
sim_debug			(vbi_capture *		cap,
				 vbi_bool		enable)
{
	vbi_capture_sim *sim = PARENT (cap, vbi_capture_sim, cap);

	return vbi3_raw_decoder_debug (sim->rd, enable);
}

/* For compatibility in libzvbi 0.2
   struct vbi_sampling_par == vbi_raw_decoder. In 0.3
   we'll drop the decoding related fields. */
#if 3 == VBI_VERSION_MINOR
static const vbi_sampling_par *
#else
static vbi_raw_decoder *
#endif
sim_parameters			(vbi_capture *		cap)
{
	vbi_capture_sim *sim = PARENT (cap, vbi_capture_sim, cap);

	return &sim->sp;
}

static int
sim_get_fd			(vbi_capture *		cap)
{
	cap = cap; /* unused */

	return -1; /* not available */
}

static void
sim_delete			(vbi_capture *		cap)
{
	vbi_capture_sim *sim = PARENT (cap, vbi_capture_sim, cap);

	vbi_capture_sim_load_caption (cap,
				       /* test_stream */ NULL,
				       /* append */ FALSE);

	vbi3_raw_decoder_delete (sim->rd);

	vbi_free (sim->desync_buffer[1]);
	vbi_free (sim->desync_buffer[0]);

	vbi_free (sim->raw_buffer.data);

	CLEAR (*sim);

	vbi_free (sim);
}

/**
 * @param scanning Whether to simulate a device receiving PAL/SECAM
 *   (value 625) or NTSC (525) video.
 * @param services This parameter must point to a set of @ref VBI_SLICED_
 *   symbols describing the data services to be simulated. On return the
 *   services actually simulated will be stored here. Currently Teletext
 *   System B, VPS, PAL WSS and PAL/NTSC Closed Caption are supported.
 * @param interlaced If @c TRUE the simulated raw VBI images will be
 *   interlaced like video images. Otherwise they will contain fields in
 *   sequential order, the first field at the top. Usually real devices
 *   provide sequential images.
 * @param synchronous If @c FALSE raw VBI images will be delayed by
 *   one field (putting a bottom field first in raw VBI images), simulating
 *   defective hardware. The @a interlaced and @a synchronous parameters
 *   correspond to fields in struct vbi_raw_decoder.
 *
 * This function opens a simulated VBI device providing raw and sliced VBI
 * data. It can be used to test applications in absence of a real device.
 * 
 * The VBI data is valid but limited. Just one Teletext page and one line
 * of roll-up caption. The WSS and VPS data is set to defaults, the VPS
 * contains no CNI.
 *
 * @note
 * The simulation does not run in real time.
 * Reading from the simulated device will return data immediately.
 * 
 * @returns
 * Initialized vbi_capture context, @c NULL on failure (out of memory).
 *
 * @since 0.2.22
 */
vbi_capture *
vbi_capture_sim_new		(int			scanning,
				 unsigned int *		services,
				 vbi_bool		interlaced,
				 vbi_bool		synchronous)
{
	vbi_capture_sim *sim;
	vbi_videostd_set videostd_set;
	vbi_bool success;

	sim = calloc (1, sizeof (*sim));
	if (NULL == sim) {
		errno = ENOMEM;
		return NULL;
	}

	sim->magic = MAGIC;

	sim->cap.read		= sim_read;
	sim->cap.parameters	= sim_parameters;
	sim->cap.debug		= sim_debug;
	sim->cap.sampling_point	= sim_sampling_point;
	sim->cap.get_fd		= sim_get_fd;
	sim->cap._delete	= sim_delete;

	sim->capture_time = 0.0;

	videostd_set = _vbi_videostd_set_from_scanning (scanning);
	assert (VBI_VIDEOSTD_SET_EMPTY != videostd_set);

	/* Sampling parameters. */

	*services = vbi_sampling_par_from_services
		(&sim->sp, /* return max_rate */ NULL,
		 videostd_set, *services);
	if (0 == *services) {
		goto failure;
	}

	sim->sp.interlaced = interlaced;
	sim->sp.synchronous = synchronous;

	/* Raw VBI buffer. */

	sim->raw_f1_size = sim->sp.bytes_per_line * sim->sp.count[0];
	sim->raw_f2_size = sim->sp.bytes_per_line * sim->sp.count[1];

	sim->raw_buffer.size = sim->raw_f1_size + sim->raw_f2_size;
	sim->raw_buffer.data = vbi_malloc (sim->raw_buffer.size);
	if (NULL == sim->raw_buffer.data) {
		goto failure;
	}

	if (!synchronous) {
		size_t size;

		size = sim->sp.bytes_per_line * sim->sp.count[1];

		sim->desync_buffer[0] = calloc (1, size);
		sim->desync_buffer[1] = calloc (1, size);

		if (NULL == sim->desync_buffer[0]
		    || NULL == sim->desync_buffer[1]) {
			goto failure;
		}
	}

	/* Sliced VBI buffer. */

	sim->sliced_buffer.data = sim->sliced;
	sim->sliced_buffer.size = sizeof (sim->sliced);

	/* Raw VBI decoder. */

	sim->rd = vbi3_raw_decoder_new (&sim->sp);
	if (NULL == sim->rd) {
		goto failure;
	}

	vbi3_raw_decoder_add_services (sim->rd, *services, 0);

	/* Signal simulation. */

#if 3 == VBI_VERSION_MINOR	
	success = vbi_capture_sim_load_vps (&sim->cap, NULL);
	assert (success);

	success = vbi_capture_sim_load_wss_625 (&sim->cap, NULL);
	assert (success);
#else
	{
		const char vps[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x01, 0xff, 0xfc, 0x00,
				     0x00 };
		const char wss[] = { 0x08, 0x06 };

		memcpy (sim->vps_buffer, vps, sizeof (vps));
		memcpy (sim->wss_buffer, wss, sizeof (wss));
	}
#endif

	success = vbi_capture_sim_load_caption
		(&sim->cap, caption_default_test_stream,
		 /* append */ FALSE);
	if (!success) {
		goto failure;
	}

	return &sim->cap;

 failure:
	sim_delete (&sim->cap);

	return NULL;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
