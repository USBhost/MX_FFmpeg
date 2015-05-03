/*
 *  libzvbi -- WSS decoder
 *
 *  Copyright (C) 2001, 2002 Michael H. Schimek
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

/* $Id: wss.c,v 1.6 2008/02/19 00:35:23 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "misc.h"
#include "vbi.h"
#include "hamm.h"
#include "wss.h"

void
vbi_decode_wss_625(vbi_decoder *vbi, uint8_t *buf, double time)
{
	vbi_event e;
	vbi_aspect_ratio *r = &e.ev.aspect;
	int parity;

	memset(&e, 0, sizeof(e));

	/* Two producers... */
	if (time < vbi->wss_time)
		return;

	vbi->wss_time = time;

	if (buf[0] != vbi->wss_last[0]
	    || buf[1] != vbi->wss_last[1]) {
		vbi->wss_last[0] = buf[0];
		vbi->wss_last[1] = buf[1];
		vbi->wss_rep_ct = 0;
		return;
	}

	if (++vbi->wss_rep_ct < 3)
		return;

	parity = buf[0] & 15;
	parity ^= parity >> 2;
	parity ^= parity >> 1;

	if (!(parity & 1))
		return;

	r->ratio = 1.0;

	switch (buf[0] & 7) {
	case 0: /* 4:3 */
	case 6: /* 14:9 soft matte */
		r->first_line = 23;
		r->last_line = 310;
		break;
	case 1: /* 14:9 */
		r->first_line = 41;
		r->last_line = 292;
		break;
	case 2: /* 14:9 top */
		r->first_line = 23;
		r->last_line = 274;
		break;
	case 3: /* 16:9 */
	case 5: /* "Letterbox > 16:9" */
		r->first_line = 59; // 59.5 ?
		r->last_line = 273;
		break;
	case 4: /* 16:9 top */
		r->first_line = 23;
		r->last_line = 237;
		break;
	case 7: /* 16:9 anamorphic */
		r->first_line = 23;
		r->last_line = 310;
		r->ratio = 3.0 / 4.0;
		break;
	}

	r->film_mode = !!(buf[0] & 0x10);

	switch ((buf[1] >> 1) & 3) {
	case 0:
		r->open_subtitles = VBI_SUBT_NONE;
		break;
	case 1:
		r->open_subtitles = VBI_SUBT_ACTIVE;
		break;
	case 2:
		r->open_subtitles = VBI_SUBT_MATTE;
		break;
	case 3:
		r->open_subtitles = VBI_SUBT_UNKNOWN;
		break;
	}

	if (memcmp(r, &vbi->prog_info[0].aspect, sizeof(*r)) != 0) {
		vbi->prog_info[0].aspect = *r;
		vbi->aspect_source = 1;

		e.type = VBI_EVENT_ASPECT;
		vbi_send_event(vbi, &e);

		e.type = VBI_EVENT_PROG_INFO;
		e.ev.prog_info = &vbi->prog_info[0];
		vbi_send_event(vbi, &e);
	}

	if (0) {
		static const char *formats[] = {
			"Full format 4:3, 576 lines",
			"Letterbox 14:9 centre, 504 lines",
			"Letterbox 14:9 top, 504 lines",
			"Letterbox 16:9 centre, 430 lines",
			"Letterbox 16:9 top, 430 lines",
			"Letterbox > 16:9 centre",
			"Full format 14:9 centre, 576 lines",
			"Anamorphic 16:9, 576 lines"
		};
		static const char *subtitles[] = {
			"none",
			"in active image area",
			"out of active image area",
			"?"
		};

		printf("WSS: %s; %s mode; %s color coding;\n"
		       "      %s helper; reserved b7=%d; %s\n"
		       "      open subtitles: %s; %scopyright %s; copying %s\n",
		       formats[buf[0] & 7],
		       (buf[0] & 0x10) ? "film" : "camera",
		       (buf[0] & 0x20) ? "MA/CP" : "standard",
		       (buf[0] & 0x40) ? "modulated" : "no",
		       !!(buf[0] & 0x80),
		       (buf[1] & 0x01) ? "have TTX subtitles; " : "",
		       subtitles[(buf[1] >> 1) & 3],
		       (buf[1] & 0x08) ? "surround sound; " : "",
		       (buf[1] & 0x10) ? "asserted" : "unknown",
		       (buf[1] & 0x20) ? "restricted" : "not restricted");
	}
}

void
vbi_decode_wss_cpr1204(vbi_decoder *vbi, uint8_t *buf)
{
	int b0 = buf[0] & 0x80;
	int b1 = buf[0] & 0x40;
	vbi_event e;
	vbi_aspect_ratio *r = &e.ev.aspect;

	memset(&e, 0, sizeof(e));

	if (b1) {
		r->first_line = 72; // wild guess
		r->last_line = 212;
	} else {
		r->first_line = 22;
		r->last_line = 262;
	}

	r->ratio = b0 ? 3.0 / 4.0 : 1.0;
	r->film_mode = 0;
	r->open_subtitles = VBI_SUBT_UNKNOWN;

	if (memcmp(r, &vbi->prog_info[0].aspect, sizeof(*r)) != 0) {
		vbi->prog_info[0].aspect = *r;
		vbi->aspect_source = 2;

		e.type = VBI_EVENT_ASPECT;
		vbi_send_event(vbi, &e);

		e.type = VBI_EVENT_PROG_INFO;
		e.ev.prog_info = &vbi->prog_info[0];
		vbi_send_event(vbi, &e);
	}

	if (0)
		printf("CPR: %d %d\n", !!b0, !!b1);
}

#if 0 /* old stuff */

/* 0001 0100 000 000 == Full 4:3, PALPlus, other flags zero */

static const uint8_t
genuine_pal_wss_green[720] = {
	 54, 76, 92, 82, 81, 73, 63, 91,200,254,253,183,162,233,246,195,
	190,224,247,237,203,150, 80, 23, 53,103, 93, 38, 61,172,244,243,
	240,202,214,251,233,125, 44, 34, 59, 95, 86, 40, 54,149,235,255,
	240,208,206,247,249,164, 67, 28, 38, 82,100, 49, 26,121,227,249,
	240,206,203,242,248,182, 87, 26, 33, 82, 94, 51, 37,102,197,250,
	251,222,205,226,247,210,118, 38, 41, 78, 91, 67, 55,103,190,254,
	252,240,220,206,211,229,245,254,207,108, 28, 42, 93,100, 60, 21,
	120,200,245,241,223,221,220,216,222,233,234,188, 97, 27, 30, 70,
	 70, 68, 67, 70, 71, 67, 58, 49, 77,159,240,248,226,203,211,227,
	233,221,208,201,221,245,196, 99, 22, 39, 73, 88, 54, 34, 92,181,
	243,246,217,186,212,245,192, 95, 23, 40, 72, 81, 46, 25, 84,170,
	250,246,213,194,225,251,211,136, 22, 39, 73, 91, 62, 38, 84,157,
	236,247,241,208,201,226,235,219,222,233,222,208,230,250,220,158,
	 54, 36, 42, 74, 79, 58, 56, 76, 72, 49, 53, 80, 65, 20, 35,107,
	246,249,239,225,217,221,226,227,218,225,220,215,231,250,236,200,
	 68, 39, 31, 51, 71, 66, 53, 46, 63, 60, 58, 67, 70, 54, 50, 66,
	194,245,246,209,193,228,240,219,116, 35,  8, 64, 93, 58, 39, 61,
	170,232,249,234,210,224,237,231,127, 64, 27, 52, 84, 77, 56, 46,
	136,224,245,233,196,198,240,240,162, 74, 20, 48, 84, 72, 49, 46,
	126,200,246,242,205,202,227,245,172, 87, 19, 29, 72, 83, 59, 36,
	 89,186,249,244,196,192,229,250,202, 94, 24, 37, 76, 95, 82, 49,
	 71,171,251,251,203,189,221,253,220,123, 36, 31, 75, 96, 76, 49,
	 79,160,247,252,232,199,207,234,233,138, 44, 28, 66, 91, 76, 53,
	 59,148,229,252,240,202,202,241,252,165, 64, 20, 39, 72, 78, 65,
	 57, 62, 63, 62, 64, 67, 62, 54, 58, 67, 74, 72, 65, 63, 62, 64,
	 57, 68, 67, 55, 54, 66, 68, 57, 57, 56, 60, 63, 55, 50, 64
};

static void
wss_test_init(v4l_device *v4l)
{
	static const int std_widths[] = {
		768, 704, 640, 384, 352, 320, 192, 176, 160, -160
	};
	uint8_t *p = wss_test_data;
	int i, j, g, width, spl, bpp = 0;
	enum tveng_frame_pixformat fmt;
	int sampling_rate;

	fprintf(stderr, "WSS test enabled\n");

	fmt = TVENG_PIX_RGB565;	/* see below */
	width = 352;		/* pixels per line; assume a line length
				   of 52 usec, i.e. no clipping */

	/* use this for capture widths reported by the driver, not scaled images */
	for (i = 0; width < ((std_widths[i] + std_widths[i + 1]) >> 1); i++);

	spl = std_widths[i]; /* samples in 52 usec */
	sampling_rate = spl / 52.148e-6;

	memset(wss_test_data, 0, sizeof wss_test_data);

	for (i = 0; i < width; i++) {
		double off = i * 704 / (double) spl;

		j = off;
		off -= j;

		g = (genuine_pal_wss_green[j + 1] * off
		    + genuine_pal_wss_green[j + 0] * (1 - off));

		switch (fmt) {
		case TVENG_PIX_RGB24:
		case TVENG_PIX_BGR24:
			*p++ = rand();
			*p++ = g;
			*p++ = rand();
			bpp = 3;
			break;

		case TVENG_PIX_RGB32: /* RGBA / BGRA */
		case TVENG_PIX_BGR32:
			*p++ = rand();
			*p++ = g;
			*p++ = rand();
			*p++ = rand();
			bpp = 4;
			break;

		case TVENG_PIX_RGB565:
			g <<= 3;
			g ^= rand() & 0xF81F;
			*p++ = g;
			*p++ = g >> 8;
			bpp = 2;
			break;

		case TVENG_PIX_RGB555:
			g <<= 2;
			g ^= rand() & 0xFC1F;
			*p++ = g;
			*p++ = g >> 8;
			bpp = 2;
			break;

		case TVENG_PIX_YVU420:
		case TVENG_PIX_YUV420:
			*p++ = g;
			bpp = 1;
			break;

		case TVENG_PIX_YUYV:
			*p++ = g;
			*p++ = rand();
			bpp = 2;
			break;

		case TVENG_PIX_UYVY:
			*p++ = rand();
			*p++ = g;
			bpp = 2;
			break;
		}
	}

	v4l->wss_slicer_fn =
	init_bit_slicer(&v4l->wss_slicer, 
		width,
		sampling_rate, 
		/* cri_rate */ 5000000, 
		/* bit_rate */ 833333,
		/* cri_frc */ 0xC71E3C1F, 
		/* cri_mask */ 0x924C99CE,
		/* cri_bits */ 32, 
		/* frc_bits */ 0, 
		/* payload */ 14 * 1,
		MOD_BIPHASE_LSB_ENDIAN,
		fmt);
}

#endif

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
