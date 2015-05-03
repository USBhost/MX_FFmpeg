/*
 *  libzvbi - vbi_raw_decoder unit test
 *
 *  Copyright (C) 2004, 2007 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

/* $Id: test-raw_decoder.cc,v 1.4 2008/03/01 07:35:48 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/version.h"
#if 2 == VBI_VERSION_MINOR
#  include "src/raw_decoder.h"
#  include "src/io-sim.h"
#  define N_ELEMENTS(array) (sizeof (array) / sizeof (*(array)))
#  define vbi_pixfmt_bytes_per_pixel(pf) VBI_PIXFMT_BPP(pf)
#  define VBI_PIXFMT_IS_YUV(pf) (0 != (VBI_PIXFMT_SET (pf)		\
					& VBI_PIXFMT_SET_YUV))
#else
#  include "src/misc.h"
#  include "src/zvbi.h"
#endif

#include "test-common.h"

bool verbose;

typedef struct {
	vbi_service_set	service;

	/* Scan lines. */
	unsigned int		first;
	unsigned int		last;
} block;

#define BLOCK_END { 0, 0, 0 }

static void
dump_hex			(const uint8_t *	p,
				 unsigned int 		n)
{
	while (n-- > 0)
		fprintf (stderr, "%02x ", *p++);
}

static void
sliced_rand_lines		(const vbi_sliced *	s_start,
				 const vbi_sliced *	s_end,
				 vbi_sliced *		s,
				 vbi_service_set	service,
				 unsigned int		first_line,
				 unsigned int		last_line)
{
	unsigned int line;

	for (line = first_line; line <= last_line; ++line) {
		const vbi_sliced *t;

		assert (s < s_end);

		for (t = s_start; t < s; ++t)
			assert (t->line != line);

		s->id = service;
		s->line = line;

		memset_rand (s->data, sizeof (s->data));

		++s;
	}
}

static unsigned int
sliced_rand			(vbi_sliced *		s,
				 unsigned int		s_lines,
				 const block *		b)
{
	const vbi_sliced *s_start;
	vbi_service_set services;

	s_start = s;
	services = 0;

	while (b->service) {
		services |= b->service;

		if (b->first > 0) {
			sliced_rand_lines (s_start, s_start + s_lines, s,
					   b->service, b->first, b->last);

			s += b->last - b->first + 1;
		}

		++b;
	}

	if (0)
		fprintf (stderr, "services 0x%08x\n", services);

	return s - s_start;
}

static void
dump_sliced_pair		(const vbi_sliced *	s1,
				 const vbi_sliced *	s2,
				 unsigned int		n_lines)
{
	unsigned int i;

	for (i = 0; i < n_lines; ++i)
		fprintf (stderr, "%2u: "
			 "%30s %3u %02x %02x %02x <-> "
			 "%30s %3u %02x %02x %02x\n",
			 i,
			 vbi_sliced_name (s1[i].id),
			 s1[i].line,
			 s1[i].data[0],
			 s1[i].data[1],
			 s1[i].data[2],
			 vbi_sliced_name (s2[i].id),
			 s2[i].line,
			 s2[i].data[0],
			 s2[i].data[1],
			 s2[i].data[2]);
}

static unsigned int
create_raw			(uint8_t **		raw,
				 vbi_sliced **		sliced,
				 const vbi_sampling_par *sp,
				 const block *		b,
				 unsigned int		pixel_mask,
				 unsigned int		raw_flags)
{
	unsigned int scan_lines;
	unsigned int sliced_lines;
	unsigned int raw_size;
	unsigned int blank_level;
	unsigned int black_level;
	unsigned int white_level;
	vbi_bool success;

	scan_lines = sp->count[0] + sp->count[1];
	raw_size = sp->bytes_per_line * scan_lines;

	*raw = (uint8_t *) malloc (raw_size);
	assert (NULL != *raw);

	*sliced = (vbi_sliced *) malloc (sizeof (**sliced) * 50);
	assert (NULL != *sliced);

	sliced_lines = sliced_rand (*sliced, 50, b);

	/* Use defaults. */
	blank_level = 0;
	black_level = 0;
	white_level = 0;

	if (pixel_mask) {
		memset_rand (*raw, raw_size);

		success = _vbi_raw_video_image (*raw, raw_size, sp,
						 blank_level,
						 black_level,
						 white_level,
						 pixel_mask,
						 raw_flags,
						 *sliced, sliced_lines);
		assert (success);
	} else {
		success = _vbi_raw_vbi_image (*raw, raw_size, sp,
					       blank_level,
					       white_level,
					       raw_flags,
					       *sliced, sliced_lines);
		assert (success);

		if (raw_flags & _VBI_RAW_NOISE_2) {
			static uint32_t seed = 12345678;

			/* Shape as in capture_stream_sim_add_noise(). */
			success = vbi_raw_add_noise (*raw, sp,
						      /* min_freq */ 0,
						      /* max_freq */ 5000000,
						      /* amplitude */ 25,
						      seed);
			assert (success);

			seed = seed * 1103515245 + 56789;
		}
	}

	return sliced_lines;
}

static vbi3_raw_decoder *
create_decoder			(const vbi_sampling_par *sp,
				 const block *		b,
				 unsigned int		strict)
{
	vbi3_raw_decoder *rd;
	vbi_service_set in_services;
	vbi_service_set out_services;

	in_services = 0;

	while (b->service) {
		in_services |= b->service;
		++b;
	}

	rd = vbi3_raw_decoder_new (sp);
	assert (NULL != rd);

	if (sp->synchronous) {
#if 2 == VBI_VERSION_MINOR
		vbi3_raw_decoder_set_log_fn
			(rd,
			 vbi_log_on_stderr,
			 /* user_data */ NULL,
			 (vbi_log_mask)(VBI_LOG_INFO * 2 - 1));
#else
		vbi3_raw_decoder_set_log_fn
			(rd,
			 (vbi_log_mask)(VBI_LOG_INFO * 2 - 1),
			 vbi_log_on_stderr,
			 /* user_data */ NULL);
#endif
	} else {
		/* Don't complain about expected failures.
		   XXX Check for those in a different function. */
	}

	out_services = vbi3_raw_decoder_add_services (rd, in_services, strict);

	if (!sp->synchronous) {
		/* Ambiguous. */
		in_services &= ~(VBI_SLICED_VPS |
				 VBI_SLICED_VPS_F2 |
				 VBI_SLICED_WSS_625 |
				 VBI_SLICED_CAPTION_625 |
				 VBI_SLICED_CAPTION_525);
	}

	assert (in_services == out_services);

	return rd;
}

static void
compare_payload			(const vbi_sliced *	in,
				 const vbi_sliced *	out)
{
	unsigned int payload;

	payload = vbi_sliced_payload_bits (out->id);
	if (0 != memcmp (in->data, out->data, payload >> 3)) {
		dump_sliced_pair (in, out, /* n_lines */ 1);
		assert (0);
	}

	if (payload & 7) {
		unsigned int mask = (1 << (payload & 7)) - 1;

		payload = (payload >> 3);

		/* MSBs zero, rest as sent */
		assert (0 == ((in->data[payload] & mask)
			      ^ out->data[payload]));
	}
}

static void
compare_sliced			(const vbi_sampling_par *sp,
				 const vbi_sliced *	in,
				 const vbi_sliced *	out,
				 const vbi_sliced *	old,
				 unsigned int		in_lines,
				 unsigned int		out_lines,
				 unsigned int		old_lines)
{
	unsigned int i;
	unsigned int min;
	unsigned int id;
	vbi_sliced *in1;
	vbi_sliced *s;

	min = 0;

	for (i = 0; i < out_lines; ++i) {
		unsigned int payload;

		if (sp->synchronous) {
			/* Ascending line numbers. */
			assert (out[i].line > min);
			min = out[i].line;
		} else {
			/* Could be first or second field,
			   we don't know. */
			assert (0 == out[i].line);
		}

		/* Valid service id. */
		assert (0 != out[i].id);
		payload = (vbi_sliced_payload_bits (out[i].id) + 7) >> 3;
		assert (payload > 0);

		/* vbi_sliced big enough. */
		assert (payload <= sizeof (out[i].data));

		/* Writes more than payload. */
		assert (0 == memcmp (out[i].data + payload,
				     old[i].data + payload,
				     sizeof (out[i].data) - payload));
	}

	/* Respects limits. */
	assert (0 == memcmp (out + out_lines,
			     old + out_lines,
			     sizeof (*old) * (old_lines - out_lines)));

	in1 = (vbi_sliced *) xmemdup (in, sizeof (*in) * in_lines);

	for (i = 0; i < out_lines; ++i) {
		if (sp->synchronous) {
			for (s = in1; s < in1 + in_lines; ++s)
				if (s->line == out[i].line)
					break;

			/* Found something we didn't send. */
			assert (s < in1 + in_lines);

			/* Identified as something else. */
			/* fprintf (stderr, "%3u id %08x %08x\n", s->line, s->id, out[i].id); */
			assert (s->id == out[i].id);
		} else {
			/* fprintf (stderr, "%08x ", out[i].id); */

			/* No line numbers, but data must be in
			   same order. */
			for (s = in1; s < in1 + in_lines; ++s)
				if (s->id == out[i].id)
					break;
			
			assert (s < in1 + in_lines);

			/* fprintf (stderr, "from line %3u\n", s->line); */
		}

		compare_payload (s, &out[i]);

		s->id = 0;
	}

	id = 0;
	for (s = in1; s < in1 + in_lines; ++s)
		id |= s->id;

	if (!sp->synchronous) {
		/* Ok these are ambiguous. */
		id &= ~(VBI_SLICED_VPS |
			VBI_SLICED_VPS_F2 |
			VBI_SLICED_WSS_625 |
			VBI_SLICED_CAPTION_625 |
			VBI_SLICED_CAPTION_525);
	}

	/* Anything missed? */
	assert (0 == id);

	free (in1);
	in1 = NULL;
}

static void
test_cycle			(const vbi_sampling_par *sp,
				 const block *		b,
				 unsigned int		pixel_mask,
				 unsigned int		raw_flags,
				 unsigned int		strict)
{
	vbi_sliced *in;
	vbi_sliced out[50];
	vbi_sliced old[50];
	uint8_t *raw;
	vbi3_raw_decoder *rd;
	unsigned int in_lines;
	unsigned int out_lines;

	in_lines = create_raw (&raw, &in, sp, b, pixel_mask, raw_flags);

	if (verbose)
		dump_hex (raw + 120, 12);

	rd = create_decoder (sp, b, strict);

	memset_rand (out, sizeof (out));
	memcpy (old, out, sizeof (old));

	out_lines = vbi3_raw_decoder_decode (rd, out, 40, raw);

	if (verbose) {
#if 2 == VBI_VERSION_MINOR
		fprintf (stderr, "%s %08x in=%u out=%u\n",
			 __FUNCTION__,
			 sp->sampling_format,
			 in_lines, out_lines);
#else
		fprintf (stderr, "%s %s in=%u out=%u\n",
			 __FUNCTION__,
			 vbi_pixfmt_name (sp->sample_format),
			 in_lines, out_lines);
#endif
	}

	if (sp->synchronous) {
		if (verbose && in_lines != out_lines)
			dump_sliced_pair (in, out, MIN (in_lines, out_lines));

		assert (in_lines == out_lines);
	}

	compare_sliced (sp, in, out, old, in_lines, out_lines, 50);

	vbi3_raw_decoder_delete (rd);

	free (in);
	free (raw);
}

static vbi_bool
block_contains_service		(const block *		b,
				 vbi_service_set	services,
				 vbi_bool		exclusive)
{
	vbi_service_set all_services = 0;

	assert (0 != services);

	while (b->service) {
		all_services |= b->service;
		++b;
	}

	if (0 == (all_services & services))
		return FALSE;

	if (exclusive && 0 != (all_services & ~services))
		return FALSE;

	return TRUE;
}

static void
test_vbi			(const vbi_sampling_par *sp,
				 const block *		b,
				 unsigned int		strict)
{
	test_cycle (sp, b, /* pixel_mask */ 0,
		    /* raw_flags */ 0, strict);

	/* Tests incorrect signal shape reported by Rich Kadel. */
	if (block_contains_service (b, VBI_SLICED_CAPTION_525,
				    /* exclusive */ FALSE))
		test_cycle (sp, b, /* pixel_mask */ 0,
			    _VBI_RAW_SHIFT_CC_CRI, strict);

	/* Tests low amplitude CC signals reported by Rich Kadel. */
	if (block_contains_service (b, VBI_SLICED_CAPTION_525,
				    /* exclusive */ TRUE)
	    && sp->sampling_rate >= 27000000) {
		unsigned int i;

		/* Repeat because the noise varies. */
		for (i = 0; i < 1000; ++i) {
			test_cycle (sp, b, /* pixel_mask */ 0,
				    _VBI_RAW_LOW_AMP_CC |
				    _VBI_RAW_NOISE_2, strict);
		}
	}
}

static void
test_video			(const vbi_sampling_par *sp,
				 const block *		b,
				 unsigned int		strict)
{
	vbi_sampling_par sp2;
	unsigned int pixel_mask;
	vbi_pixfmt pixfmt;
	unsigned int samples_per_line;

	sp2 = *sp;
#if 2 == VBI_VERSION_MINOR
	samples_per_line = sp->bytes_per_line
		/ vbi_pixfmt_bytes_per_pixel (sp->sampling_format);
#else
	samples_per_line = sp->samples_per_line;
#endif

	for (pixfmt = (vbi_pixfmt) 0; pixfmt < VBI_MAX_PIXFMTS;
	     pixfmt = (vbi_pixfmt)(pixfmt + 1)) {
		if (0 == (VBI_PIXFMT_SET_ALL & VBI_PIXFMT_SET (pixfmt)))
			continue;

#if 2 == VBI_VERSION_MINOR
		sp2.sampling_format = pixfmt;
#else
		sp2.sample_format = pixfmt;
#endif
		sp2.bytes_per_line = samples_per_line
			* vbi_pixfmt_bytes_per_pixel (pixfmt);

		/* Check bit slicer looks at Y/G */
		if (VBI_PIXFMT_IS_YUV (pixfmt))
			pixel_mask = 0xFF;
		else
			pixel_mask = 0xFF00;

		test_cycle (&sp2, b, pixel_mask,
			    /* raw_flags */ 0, strict);

		if (block_contains_service (b, VBI_SLICED_CAPTION_525,
					    /* exclusive */ FALSE))
			test_cycle (&sp2, b, pixel_mask,
				    _VBI_RAW_SHIFT_CC_CRI, strict);
	}
}

static const block ttx_a [] = {
	{ VBI_SLICED_TELETEXT_A,	6, 22 },
	{ VBI_SLICED_TELETEXT_A,	318, 335 },
	BLOCK_END,
};

static const block ttx_c_625 [] = {
	{ VBI_SLICED_TELETEXT_C_625,	6, 22 },
	{ VBI_SLICED_TELETEXT_C_625,	318, 335 },
	BLOCK_END,
};

static const block ttx_d_625 [] = {
	{ VBI_SLICED_TELETEXT_D_625,	6, 22 },
	{ VBI_SLICED_TELETEXT_D_625,	318, 335 },
	BLOCK_END,
};

static const block ttx_wss_cc_625 [] = {
	{ VBI_SLICED_TELETEXT_B_625,	6, 21 },
	{ VBI_SLICED_CAPTION_625,	22, 22 },
	{ VBI_SLICED_WSS_625,		23, 23 },
	{ VBI_SLICED_TELETEXT_B_625,	318, 334 },
	{ VBI_SLICED_CAPTION_625,	335, 335 },
	BLOCK_END,
};

static const block hi_f1_625 [] = {
	{ VBI_SLICED_VPS,		16, 16 },
	{ VBI_SLICED_CAPTION_625_F1,	22, 22 },
	{ VBI_SLICED_WSS_625,		23, 23 },
	BLOCK_END,
};

static const block hi_f2_525 [] = {
	{ VBI_SLICED_CAPTION_525_F2,	284, 284 },
	BLOCK_END,
};

static const block vps_wss_cc_625 [] = {
	{ VBI_SLICED_VPS,		16, 16 },
	{ VBI_SLICED_CAPTION_625,	22, 22 },
	{ VBI_SLICED_WSS_625,		23, 23 },
	{ VBI_SLICED_CAPTION_625,	335, 335 },
	BLOCK_END,
};

static const block cc_625 [] = {
	{ VBI_SLICED_CAPTION_625,	22, 22 },
	{ VBI_SLICED_CAPTION_625,	335, 335 },
	BLOCK_END,
};

static const block ttx_c_525 [] = {
	{ VBI_SLICED_TELETEXT_C_525,	10, 21 },
	{ VBI_SLICED_TELETEXT_C_525,	272, 284 },
	BLOCK_END,
};

static const block ttx_d_525 [] = {
	{ VBI_SLICED_TELETEXT_D_525,	10, 21 },
	{ VBI_SLICED_TELETEXT_D_525,	272, 284 },
	BLOCK_END,
};

static const block hi_525 [] = {
	{ VBI_SLICED_TELETEXT_B_525,	10, 20 },
	{ VBI_SLICED_CAPTION_525,	21, 21 },
	{ VBI_SLICED_TELETEXT_B_525,	272, 283 },
	{ VBI_SLICED_CAPTION_525,	284, 284 },
	BLOCK_END,
};

static const block cc_525 [] = {
	{ VBI_SLICED_CAPTION_525,	21, 21 },
	{ VBI_SLICED_CAPTION_525,	284, 284 },
	BLOCK_END,
};

static void
test2				(const vbi_sampling_par *sp)
{
#if 2 == VBI_VERSION_MINOR
	if (625 == sp->scanning) {
#else
	if (sp->videostd_set & VBI_VIDEOSTD_SET_625_50) {
#endif
		if (sp->sampling_rate >= 13500000) {
			vbi_sampling_par sp1;

			/* We cannot mix Teletext standards; bit rate and
			   FRC are too similar to reliable distinguish. */
			test_vbi (sp, ttx_a, 1);
			test_vbi (sp, ttx_c_625, 1);

			/* Needs sampling beyond 0H + 63 us (?) */
#if 2 == VBI_VERSION_MINOR
			if (sp->bytes_per_line
			    == 2048 * VBI_PIXFMT_BPP (sp->sampling_format))
				test_vbi (sp, ttx_d_625, 1);
#else
			if (sp->bytes_per_line == 2048
			    * vbi_pixfmt_bytes_per_pixel (sp->sample_format))
				test_vbi (sp, ttx_d_625, 1);
#endif

			test_vbi (sp, ttx_wss_cc_625, 1);
			test_video (sp, ttx_wss_cc_625, 1);

			/* For low_pass_bit_slicer test. */
			test_vbi (sp, vps_wss_cc_625, 1);

			if (!sp->interlaced) {
				sp1 = *sp;
				sp1.start[1] = 0;
				sp1.count[1] = 0;
				test_vbi (&sp1, hi_f1_625, 2);
			}
		} else if (sp->sampling_rate >= 5000000) {
			test_vbi (sp, vps_wss_cc_625, 1);
			test_video (sp, vps_wss_cc_625, 1);
		} else {
			/* WSS not possible below 5 MHz due to a cri_rate
			   check in bit_slicer_init(), but much less won't
			   work anyway. */
			test_vbi (sp, cc_625, 1);
			test_video (sp, cc_625, 1);
		}
	} else {
		if (sp->sampling_rate >= 13500000) {
			vbi_sampling_par sp1;

			test_vbi (sp, ttx_c_525, 1);
			test_vbi (sp, ttx_d_525, 1);

			test_vbi (sp, hi_525, 1);
			test_video (sp, hi_525, 1);

			/* CC only for the low-amp CC test. */
			test_vbi (sp, cc_525, 1);

			if (!sp->interlaced) {
				sp1 = *sp;
				sp1.start[0] = 0;
				sp1.count[0] = 0;
				test_vbi (&sp1, hi_f2_525, 2);
			}
		} else {
			test_vbi (sp, cc_525, 1);
			test_video (sp, cc_525, 1);
		}
	}
}

static void
test1				(const vbi_sampling_par *sp)
{
	static const struct {
		unsigned int	sampling_rate;
		unsigned int	samples_per_line;
	} res [] = {
		/* bt8x8 PAL		~35.5 MHz / 2048
		   bt8x8 NTSC		~28.6 MHz / 2048
		   PAL 1:1		~14.7 MHz / 768
		   ITU-R BT.601		 13.5 MHz / 720
		   NTSC 1:1		~12.3 MHz / 640 */
		{ 35468950,	2048 },
		{ 27000000,	1440 },
		{ 13500000,	 720 },
		{  3000000,	 176 },
	};
	vbi_sampling_par sp2;
	unsigned int i;

	for (i = 0; i < N_ELEMENTS (res); ++i) {
		if (verbose)
			fprintf (stderr, "%.2f MHz %u spl\n",
				 res[i].sampling_rate / 1e6,
				 res[i].samples_per_line);

		sp2 = *sp;
		sp2.sampling_rate	= res[i].sampling_rate;
#if 2 == VBI_VERSION_MINOR
		sp2.bytes_per_line	= res[i].samples_per_line
			* vbi_pixfmt_bytes_per_pixel (sp2.sampling_format);
#else
		sp2.samples_per_line	= res[i].samples_per_line;
		sp2.bytes_per_line	= res[i].samples_per_line
			* vbi_pixfmt_bytes_per_pixel (sp2.sample_format);
#endif

		sp2.offset		= (int)(9.7e-6 * sp2.sampling_rate);

		test2 (&sp2);
	}
}

static void
test_services			(void)
{
	vbi_sampling_par sp;
	vbi_service_set set;

	memset (&sp, 0x55, sizeof (sp));

	set = vbi_sampling_par_from_services (&sp,
					       /* &max_rate */ NULL,
					       VBI_VIDEOSTD_SET_625_50,
					       ~0 & ~VBI_SLICED_VBI_625);
	assert (set == (VBI_SLICED_TELETEXT_A |
			VBI_SLICED_TELETEXT_B_625 |
			VBI_SLICED_TELETEXT_C_625 |
			VBI_SLICED_TELETEXT_D_625 |
			VBI_SLICED_VPS |
			VBI_SLICED_VPS_F2 |
			VBI_SLICED_CAPTION_625 |
			VBI_SLICED_WSS_625));
	test2 (&sp);

	set = vbi_sampling_par_from_services (&sp,
					       /* &max_rate */ NULL,
					       VBI_VIDEOSTD_SET_525_60,
					       ~0 & ~VBI_SLICED_VBI_525);
	assert (set == (VBI_SLICED_TELETEXT_B_525 |
			VBI_SLICED_TELETEXT_C_525 |
			VBI_SLICED_TELETEXT_D_525 |
			VBI_SLICED_CAPTION_525 |
			VBI_SLICED_2xCAPTION_525
			/* Needs fix */
			/* | VBI_SLICED_WSS_CPR1204 */ ));
	test2 (&sp);
}

static void
test_line_order			(vbi_bool		synchronous)
{
	vbi_sampling_par sp;

	memset (&sp, 0x55, sizeof (sp));

#if 2 == VBI_VERSION_MINOR
	sp.scanning		= 625;
	sp.sampling_format	= VBI_PIXFMT_YUV420;
#else
	sp.videostd_set		= VBI_VIDEOSTD_SET_PAL_BG;
	sp.sample_format	= VBI_PIXFMT_YUV420;
#endif
	sp.start[0]		= 6;
	sp.count[0]		= 23 - 6 + 1;
	sp.start[1]		= 318;
	sp.count[1]		= 335 - 318 + 1;
	sp.interlaced		= FALSE;
	sp.synchronous		= synchronous;

	test1 (&sp);

	sp.interlaced		= TRUE;

	test1 (&sp);

#if 2 == VBI_VERSION_MINOR
	sp.scanning		= 525;
	sp.sampling_format	= VBI_PIXFMT_YUV420;
#else
	sp.videostd_set		= VBI_VIDEOSTD_SET_NTSC;
	sp.sample_format	= VBI_PIXFMT_YUV420;
#endif
	sp.start[0]		= 10;
	sp.count[0]		= 21 - 10 + 1;
	sp.start[1]		= 272;
	sp.count[1]		= 284 - 272 + 1;
	sp.interlaced		= FALSE;
	sp.synchronous		= synchronous;

	test1 (&sp);
}

int
main				(int			argc,
				 char **		argv)
{
	argv = argv;

	verbose = (argc > 1);

	test_services ();

	test_line_order (/* synchronous */ TRUE);
	test_line_order (/* synchronous */ FALSE);

	/* More... */

	return 0;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
