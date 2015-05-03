/*
 *  libzvbi -- Teletext decoder frontend
 *
 *  Copyright (C) 2000, 2001 Michael H. Schimek
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

/* $Id: packet.c,v 1.32 2013/07/10 11:37:28 mschimek Exp $ */

#include "site_def.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "misc.h"
#include "hamm.h"
#include "lang.h"
#include "export.h"
#include "tables.h"
#include "vps.h"
#include "vbi.h"
#include "cache-priv.h"
#include "packet-830.h"

#ifndef FPC
#  define FPC 0
#endif

static vbi_bool convert_drcs(cache_page *vtp, uint8_t *raw);

_vbi_inline void
dump_page_link			(struct ttx_page_link	link)
{
	printf ("T%x %3x/%04x\n", link.function, link.pgno, link.subno);
}

static void
dump_raw(cache_page *vtp, vbi_bool unham)
{
	int i, j;

	printf("Page %03x.%04x\n", vtp->pgno, vtp->subno);

	for (j = 0; j < 25; j++) {
		if (unham)
			for (i = 0; i < 40; i++)
				printf("%01x ", vbi_unham8 (vtp->data.lop.raw[j][i]) & 0xF);
		else
			for (i = 0; i < 40; i++)
				printf("%02x ", vtp->data.lop.raw[j][i]);
		for (i = 0; i < 40; i++)
			putchar(_vbi_to_ascii (vtp->data.lop.raw[j][i]));
		putchar('\n');
	}
}

static void
dump_extension(const struct ttx_extension *ext)
{
	int i;

	printf("Extension:\ndesignations %08x\n", ext->designations);
	printf("char set primary %d secondary %d\n", ext->charset_code[0], ext->charset_code[1]);
	printf("default screen col %d row col %d\n", ext->def_screen_color, ext->def_row_color);
	printf("bbg subst %d color table remapping %d, %d\n",
		ext->fallback.black_bg_substitution, ext->foreground_clut, ext->background_clut);
	printf("panel left %d right %d\n",
		ext->fallback.left_panel_columns,
		ext->fallback.left_panel_columns);
	printf("color map (bgr):\n");
	for (i = 0; i < 40; i++) {
		printf("%08x, ", ext->color_map[i]);
		if ((i % 8) == 7) printf("\n");
	}
	printf("dclut4 global: ");
	for (i = 0; i <= 3; i++)
		printf("%2d ", ext->drcs_clut[i + 2]);
	printf("\ndclut4 normal: ");
	for (i = 0; i <= 3; i++)
		printf("%2d ", ext->drcs_clut[i + 6]);
	printf("\ndclut16 global: ");
	for (i = 0; i <= 15; i++)
		printf("%2d ", ext->drcs_clut[i + 10]);
	printf("\ndclut16 normal: ");
	for (i = 0; i <= 15; i++)
		printf("%2d ", ext->drcs_clut[i + 26]);
	printf("\n\n");
}

static void
dump_drcs(cache_page *vtp)
{
	int i, j, k;
	uint8_t *p = vtp->data.drcs.chars[0];

	printf("\nDRCS page %03x/%04x\n", vtp->pgno, vtp->subno);

	for (i = 0; i < 48; i++) {
		printf("DRC #%d mode %02x\n", i, vtp->data.drcs.mode[i]);

		for (j = 0; j < 10; p += 6, j++) {
			for (k = 0; k < 6; k++)
				printf("%x%x", p[k] & 15, p[k] >> 4);
			putchar('\n');
		}
	}
}

#if 0
static void
dump_page_info(struct teletext *vt)
{
	int i, j;

	for (i = 0; i < 0x800; i += 16) {
		printf("%03x: ", i + 0x100);

		for (j = 0; j < 16; j++)
			printf("%02x:%02x:%04x ",
			       vt->page_info[i + j].code & 0xFF,
			       vt->page_info[i + j].language & 0xFF, 
			       vt->page_info[i + j].subcode & 0xFFFF);

		putchar('\n');
	}

	putchar('\n');
}
#endif

_vbi_inline vbi_bool
unham_page_link(struct ttx_page_link *p, const uint8_t *raw, int magazine)
{
	int b1, b2, b3, err, m;

	err = b1 = vbi_unham16p (raw + 0);
	err |= b2 = vbi_unham16p (raw + 2);
	err |= b3 = vbi_unham16p (raw + 4);

	if (err < 0)
		return FALSE;

	m = ((b3 >> 5) & 6) + (b2 >> 7);

	p->pgno = ((magazine ^ m) ? : 8) * 256 + b1;
	p->subno = (b3 * 256 + b2) & 0x3f7f;

	return TRUE;
}

static inline vbi_bool
parse_mot(struct ttx_magazine *mag, uint8_t *raw, int packet)
{
	int err, i, j;

	switch (packet) {
	case 1 ... 8:
	{
		int index = (packet - 1) << 5;
		int n0, n1;

		for (i = 0; i < 20; index++, i++) {
			if (i == 10)
				index += 6;

			n0 = vbi_unham8 (*raw++);
			n1 = vbi_unham8 (*raw++);

			if ((n0 | n1) < 0)
				continue;

			mag->pop_lut[index] = n0 & 7;
			mag->drcs_lut[index] = n1 & 7;
		}

		return TRUE;
	}

	case 9 ... 14:
	{
		int index = (packet - 9) * 0x30 + 10;

		for (i = 0; i < 20; index++, i++) {
			int n0, n1;

			if (i == 6 || i == 12) {
				if (index == 0x100)
					break;
				else
					index += 10;
			}

			n0 = vbi_unham8 (*raw++);
			n1 = vbi_unham8 (*raw++);

			if ((n0 | n1) < 0)
				continue;

			mag->pop_lut[index] = n0 & 7;
			mag->drcs_lut[index] = n1 & 7;
		}

		return TRUE;
	}

	case 15 ... 18: /* not used */
		return TRUE;

	case 22 ... 23:	/* level 3.5 pops */
		packet--;

	case 19 ... 20: /* level 2.5 pops */
	{
		struct ttx_pop_link *pop;

		pop = &mag->pop_link[0][(packet - 19) * 4];

		for (i = 0; i < 4; raw += 10, pop++, i++) {
			int n[10];

			for (err = j = 0; j < 10; j++)
				err |= n[j] = vbi_unham8 (raw[j]);

			if (err < 0) /* XXX unused bytes poss. not hammed (^ N3) */
				continue;

			pop->pgno = (((n[0] & 7) ? : 8) << 8) + (n[1] << 4) + n[2];

			/* n[3] number of subpages ignored */

			if (n[4] & 1)
				memset(&pop->fallback, 0,
				       sizeof(pop->fallback));
			else {
				int x = (n[4] >> 1) & 3;

				pop->fallback.black_bg_substitution =
					n[4] >> 3;

				/* x: 0/0, 16/0, 0/16, 8/8 */
				pop->fallback.left_panel_columns =
					"\00\20\00\10"[x];
				pop->fallback.right_panel_columns =
					"\00\00\20\10"[x];
			}

			pop->default_obj[0].type = n[5] & 3;
			pop->default_obj[0].address = (n[7] << 4) + n[6];
			pop->default_obj[1].type = n[5] >> 2;
			pop->default_obj[1].address = (n[9] << 4) + n[8];
		}

		return TRUE;
	}

	case 21:	/* level 2.5 drcs */
	case 24:	/* level 3.5 drcs */
	    {
		int index = (packet == 21) ? 0 : 8;
		int n[4];

		for (i = 0; i < 8; raw += 4, index++, i++) {
			for (err = j = 0; j < 4; j++)
				err |= n[j] = vbi_unham8 (raw[j]);

			if (err < 0)
				continue;

			mag->drcs_link[0][index] = (((n[0] & 7) ? : 8) << 8) + (n[1] << 4) + n[2];

			/* n[3] number of subpages ignored */
		}

		return TRUE;
	    }
	}

	return TRUE;
}

static vbi_bool
parse_pop(cache_page *vtp, uint8_t *raw, int packet)
{
	int designation, triplet[13];
	struct ttx_triplet *trip;
	int i;

	if ((designation = vbi_unham8 (raw[0])) < 0)
		return FALSE;

	for (raw++, i = 0; i < 13; raw += 3, i++)
		triplet[i] = vbi_unham24p (raw);

	if (packet == 26)
		packet += designation;

	switch (packet) {
	case 1 ... 2:
		if (!(designation & 1))
			return FALSE; /* fixed usage */

	case 3 ... 4:
		if (designation & 1) {
			int index = (packet - 1) * 26;

			for (index += 2, i = 1; i < 13; index += 2, i++)
				if (triplet[i] >= 0) {
					vtp->data.pop.pointer[index + 0] = triplet[i] & 0x1FF;
					vtp->data.pop.pointer[index + 1] = triplet[i] >> 9;
				}

			return TRUE;
		}

		/* fall through */

	case 5 ... 42:
		trip = vtp->data.pop.triplet + (packet - 3) * 13;

		for (i = 0; i < 13; trip++, i++)
			if (triplet[i] >= 0) {
				trip->address	= (triplet[i] >> 0) & 0x3F;
				trip->mode	= (triplet[i] >> 6) & 0x1F;
				trip->data	= (triplet[i] >> 11);
			}

		return TRUE;
	}

	return FALSE;
}

static unsigned int expand[64];

static void
init_expand(void)
{
	int i, j, n;

	for (i = 0; i < 64; i++) {
		for (n = j = 0; j < 6; j++)
			if (i & (0x20 >> j))
				n |= 1 << (j * 4);
		expand[i] = n;
	}
}

static vbi_bool
convert_drcs(cache_page *vtp, uint8_t *raw)
{
	uint8_t *p, *d;
	int i, j, q;

	p = raw;
	vtp->data.drcs.invalid = 0;

	for (i = 0; i < 24; p += 40, i++)
		if (vtp->lop_packets & (2 << i)) {
			for (j = 0; j < 20; j++)
				if (vbi_unpar8 (p[j]) < 0x40) {
					vtp->data.drcs.invalid |= 1ULL << (i * 2);
					break;
				}
			for (j = 20; j < 40; j++)
				if (vbi_unpar8 (p[j]) < 0x40) {
					vtp->data.drcs.invalid |= 1ULL << (i * 2 + 1);
					break;
				}
		} else {
			vtp->data.drcs.invalid |= 3ULL << (i * 2);
		}

	p = raw;
	d = vtp->data.drcs.chars[0];

	for (i = 0; i < 48; i++) {
		switch (vtp->data.drcs.mode[i]) {
		case DRCS_MODE_12_10_1:
			for (j = 0; j < 20; d += 3, j++) {
				d[0] = q = expand[p[j] & 0x3F];
				d[1] = q >> 8;
				d[2] = q >> 16;
			}
			p += 20;
			break;

		case DRCS_MODE_12_10_2:
			if (vtp->data.drcs.invalid & (3ULL << i)) {
				vtp->data.drcs.invalid |= (3ULL << i);
				d += 60;
			} else
				for (j = 0; j < 20; d += 3, j++) {
					q = expand[p[j +  0] & 0x3F]
					  + expand[p[j + 20] & 0x3F] * 2;
					d[0] = q;
					d[1] = q >> 8;
					d[2] = q >> 16;
				}
			p += 40;
			d += 60;
			i += 1;
			break;

		case DRCS_MODE_12_10_4:
			if (vtp->data.drcs.invalid & (15ULL << i)) {
				vtp->data.drcs.invalid |= (15ULL << i);
				d += 60;
			} else
				for (j = 0; j < 20; d += 3, j++) {
					q = expand[p[j +  0] & 0x3F]
					  + expand[p[j + 20] & 0x3F] * 2
					  + expand[p[j + 40] & 0x3F] * 4
					  + expand[p[j + 60] & 0x3F] * 8;
					d[0] = q;
					d[1] = q >> 8;
					d[2] = q >> 16;
				}
			p += 80;
			d += 180;
			i += 3;
			break;

		case DRCS_MODE_6_5_4:
			for (j = 0; j < 20; p += 4, d += 6, j++) {
				q = expand[p[0] & 0x3F]
				  + expand[p[1] & 0x3F] * 2
				  + expand[p[2] & 0x3F] * 4
				  + expand[p[3] & 0x3F] * 8;
				d[0] = (q & 15) * 0x11;
				d[1] = ((q >> 4) & 15) * 0x11;
				d[2] = ((q >> 8) & 15) * 0x11;
				d[3] = ((q >> 12) & 15) * 0x11;
				d[4] = ((q >> 16) & 15) * 0x11;
				d[5] = (q >> 20) * 0x11;
			}
			break;

		default:
			vtp->data.drcs.invalid |= (1ULL << i);
			p += 20;
			d += 60;
			break;
		}
	}

	if (0)
		dump_drcs(vtp);

	return TRUE;
}

static int
page_language(struct teletext *vt, const cache_network *cn,
	      const cache_page *vtp, int pgno, int national)
{
	const struct ttx_magazine *mag;
	const struct ttx_extension *ext;
	int charset_code;
	int lang = -1; /***/

	if (vtp) {
		if (vtp->function != PAGE_FUNCTION_LOP)
			return lang;

		pgno = vtp->pgno;
		national = vtp->national;
	}

	if (vt->max_level <= VBI_WST_LEVEL_1p5)
		mag = &vt->default_magazine;
	else
		mag = cache_network_const_magazine (cn, pgno);

	ext = (NULL != vtp && 0 != vtp->x28_designations) ?
		&vtp->data.ext_lop.ext : &mag->extension;

	charset_code = ext->charset_code[0];

	if (VALID_CHARACTER_SET(charset_code))
		lang = charset_code;

	charset_code = (charset_code & ~7) + national;

	if (VALID_CHARACTER_SET(charset_code))
		lang = charset_code;

	return lang;
}

static vbi_bool
parse_mip_page(vbi_decoder *vbi, cache_page *vtp,
	int pgno, int code, int *subp_index)
{
	uint8_t *raw;
	int subc, old_code, old_subc;
	struct ttx_page_stat *ps;

	if (code < 0)
		return FALSE;

	ps = cache_network_page_stat (vbi->cn, pgno);

	switch (code) {
	case 0x52 ... 0x6F: /* reserved */
	case 0xD2 ... 0xDF: /* reserved */
	case 0xFA ... 0xFC: /* reserved */
	case 0xFF: 	    /* reserved, we use it as 'unknown' flag */
		return TRUE;

	case 0x02 ... 0x4F:
	case 0x82 ... 0xCF:
		subc = code & 0x7F;
		code = (code >= 0x80) ? VBI_PROGR_SCHEDULE :
					VBI_NORMAL_PAGE;
		break;

	case 0x70 ... 0x77:
	{
		cache_page *cp;

		code = VBI_SUBTITLE_PAGE;
		subc = 0;

		/* cp may be NULL. */
		cp = _vbi_cache_get_page (vbi->ca, vbi->cn, pgno,
					  /* subno */ 0,
					  /* subno_mask */ 0);
		ps->charset_code =
			page_language (&vbi->vt, vbi->cn, cp, pgno, code & 7);

		cache_page_unref (cp);

		break;
	}

	case 0x50 ... 0x51: /* normal */
	case 0xD0 ... 0xD1: /* program */
	case 0xE0 ... 0xE1: /* data */
	case 0x7B: /* current program */
	case 0xF8: /* keyword search list */
		if (*subp_index > 10 * 13)
			return FALSE;

		raw = &vtp->data.unknown.raw[*subp_index / 13 + 15]
				    [(*subp_index % 13) * 3 + 1];
		(*subp_index)++;

		if ((subc = vbi_unham16p (raw)
		     | (vbi_unham8 (raw[2]) << 8)) < 0)
			return FALSE;

		if ((code & 15) == 1)
			subc += 1 << 12;
		else if (subc < 2)
			return FALSE;

		code =	(code == 0xF8) ? VBI_KEYWORD_SEARCH_LIST :
			(code == 0x7B) ? VBI_CURRENT_PROGR :
			(code >= 0xE0) ? VBI_CA_DATA_BROADCAST :
			(code >= 0xD0) ? VBI_PROGR_SCHEDULE :
					 VBI_NORMAL_PAGE;
		break;

	default:
		code = code;
		subc = 0;
		break;
	}

	old_code = ps->page_type;
	old_subc = ps->subcode;

	/*
	 *  When we got incorrect numbers and proved otherwise by
	 *  actually receiving the page...
	 */
	if (old_code == VBI_UNKNOWN_PAGE || old_code == VBI_SUBTITLE_PAGE
	    || code != VBI_NO_PAGE || code == VBI_SUBTITLE_PAGE)
		ps->page_type = code;

	if (old_code == VBI_UNKNOWN_PAGE || subc > old_subc)
		ps->subcode = subc;

	return TRUE;
}

static vbi_bool
parse_mip(vbi_decoder *vbi, cache_page *vtp)
{
	int packet, pgno, i, spi = 0;

	if (0)
		dump_raw(vtp, TRUE);

	for (packet = 1, pgno = vtp->pgno & 0xF00; packet <= 8; packet++, pgno += 0x20)
		if (vtp->lop_packets & (1 << packet)) {
			uint8_t *raw = vtp->data.unknown.raw[packet];

			for (i = 0x00; i <= 0x09; raw += 2, i++)
				if (!parse_mip_page(vbi, vtp, pgno + i, vbi_unham16p (raw), &spi))
					return FALSE;
			for (i = 0x10; i <= 0x19; raw += 2, i++)
				if (!parse_mip_page(vbi, vtp, pgno + i, vbi_unham16p (raw), &spi))
					return FALSE;
		}

	for (packet = 9, pgno = vtp->pgno & 0xF00; packet <= 14; packet++, pgno += 0x30)
		if (vtp->lop_packets & (1 << packet)) {
			uint8_t *raw = vtp->data.unknown.raw[packet];

			for (i = 0x0A; i <= 0x0F; raw += 2, i++)
				if (!parse_mip_page(vbi, vtp, pgno + i,
						    vbi_unham16p (raw), &spi))
					return FALSE;
			if (packet == 14) /* 0xFA ... 0xFF */
				break;
			for (i = 0x1A; i <= 0x1F; raw += 2, i++)
				if (!parse_mip_page(vbi, vtp, pgno + i,
						    vbi_unham16p (raw), &spi))
					return FALSE;
			for (i = 0x2A; i <= 0x2F; raw += 2, i++)
				if (!parse_mip_page(vbi, vtp, pgno + i,
						    vbi_unham16p (raw), &spi))
					return FALSE;
		}
/*
	if (0 && packet == 1)
		dump_page_info(&vbi->vt);
*/
	return TRUE;
}

static void
eacem_trigger(vbi_decoder *vbi, cache_page *vtp)
{
	vbi_page pg;
	uint8_t *s;
	int i, j;

	if (0)
		dump_raw(vtp, FALSE);

	if (!(vbi->event_mask & VBI_EVENT_TRIGGER))
		return;

	if (!vbi_format_vt_page(vbi, &pg, vtp, VBI_WST_LEVEL_1p5, 24, 0))
		return;

	s = (uint8_t *) pg.text;

	for (i = 1; i < 25; i++)
		for (j = 0; j < 40; j++) {
			int c = pg.text[i * 41 + j].unicode;
			*s++ = (c >= 0x20 && c <= 0xFF) ? c : 0x20;
		}
	*s = 0;

	vbi_eacem_trigger(vbi, (uint8_t *) pg.text);
}

/*
	11.2 Table Of Pages navigation
*/

static const int dec2bcdp[20] = {
	0x000, 0x040, 0x080, 0x120, 0x160, 0x200, 0x240, 0x280, 0x320, 0x360,
	0x400, 0x440, 0x480, 0x520, 0x560, 0x600, 0x640, 0x680, 0x720, 0x760
};

static vbi_bool
unham_top_page_link		(struct ttx_page_link *	pl,
				 const uint8_t		buffer[8])
{
	int n4[8];
	int err;
	unsigned int i;
	vbi_pgno pgno;
	vbi_subno subno;

	err = 0;

	for (i = 0; i < 8; ++i)
		err |= n4[i] = vbi_unham8 (buffer[i]);

	pgno = n4[0] * 256 + n4[1] * 16 + n4[2];

	if (err < 0
	    || pgno < 0x100
	    || pgno > 0x8FF)
		return FALSE;

	subno = (n4[3] << 12) | (n4[4] << 8) | (n4[5] << 4) | n4[6];

	switch ((enum ttx_top_page_function) n4[7]) {
	case TOP_PAGE_FUNCTION_AIT:
		pl->function = PAGE_FUNCTION_AIT;
		break;

	case TOP_PAGE_FUNCTION_MPT:
		pl->function = PAGE_FUNCTION_MPT;
		break;

	case TOP_PAGE_FUNCTION_MPT_EX:
		pl->function = PAGE_FUNCTION_MPT_EX;
		break;

	default:
		pl->function = PAGE_FUNCTION_UNKNOWN;
		break;
	}

	pl->pgno = pgno;
	pl->subno = subno & 0x3F7F; /* flags? */

	return TRUE;
}

static inline vbi_bool
parse_btt(vbi_decoder *vbi, uint8_t *raw, int packet)
{
	switch (packet) {
	case 1 ... 20:
	{
		int i, j, code, index = dec2bcdp[packet - 1];

		for (i = 0; i < 4; i++) {
			for (j = 0; j < 10; index++, j++) {
				struct ttx_page_stat *ps;

				ps = cache_network_page_stat (vbi->cn,
							      0x100 + index);

				if ((code = vbi_unham8 (*raw++)) < 0)
					break;

				switch (code) {
				case BTT_SUBTITLE:
				{
					cache_page *cp;

					ps->page_type = VBI_SUBTITLE_PAGE;

					cp = _vbi_cache_get_page
						(vbi->ca, vbi->cn,
						 index + 0x100,
						 /* subno */ 0,
						 /* subno_mask */ 0);
					if (NULL != cp) {
						ps->charset_code =
							page_language
							(&vbi->vt,
							 vbi->cn, cp,
							 0, 0);
						cache_page_unref (cp);
					}

					break;
				}

				case BTT_PROGR_INDEX_S:
				case BTT_PROGR_INDEX_M:
					/* Usually schedule, not index (likely BTT_GROUP) */
					ps->page_type = VBI_PROGR_SCHEDULE;
					break;

				case BTT_BLOCK_S:
				case BTT_BLOCK_M:
					ps->page_type = VBI_TOP_BLOCK;
					break;

				case BTT_GROUP_S:
				case BTT_GROUP_M:
					ps->page_type = VBI_TOP_GROUP;
					break;

				case 8 ... 11:
					ps->page_type = VBI_NORMAL_PAGE;
					break;

				default:
					ps->page_type = VBI_NO_PAGE;
					continue;
				}

				switch (code) {
				case BTT_PROGR_INDEX_M:
				case BTT_BLOCK_M:
				case BTT_GROUP_M:
				case BTT_NORMAL_M:
					/* -> mpt, mpt_ex */
					break;

				default:
					ps->subcode = 0;
					break;
				}
			}

			index += ((index & 0xFF) == 0x9A) ? 0x66 : 0x06;
		}

		break;
	}

	case 21 ... 23:
	    {
		struct ttx_page_link *pl;
		int i;

		pl = vbi->cn->btt_link + (packet - 21) * 5;

		vbi->cn->have_top = TRUE;

		for (i = 0; i < 5; raw += 8, pl++, i++) {
			struct ttx_page_stat *ps;

			if (!unham_top_page_link(pl, raw))
				continue;

			if (0) {
				printf("BTT #%d: ", (packet - 21) * 5);
				dump_page_link(*pl);
			}

			switch (pl->function) {
			case PAGE_FUNCTION_MPT:
			case PAGE_FUNCTION_AIT:
			case PAGE_FUNCTION_MPT_EX:
				ps = cache_network_page_stat (vbi->cn, pl->pgno);
				ps->page_type = VBI_TOP_PAGE;
				ps->subcode = 0;
				break;

			default:
				break;
			}
		}

		break;
	    }
	}
/*
	if (0 && packet == 1)
		dump_page_info(&vbi->vt);
*/
	return TRUE;
}

static vbi_bool
parse_ait(cache_page *vtp, uint8_t *raw, int packet)
{
	int i, n;
	struct ttx_ait_title *ait;

	if (packet < 1 || packet > 23)
		return TRUE;

	ait = &vtp->data.ait.title[(packet - 1) * 2];

	if (unham_top_page_link(&ait[0].link, raw + 0)) {
		for (i = 0; i < 12; i++)
			if ((n = vbi_unpar8 (raw[i + 8])) >= 0)
				ait[0].text[i] = n;
	}

	if (unham_top_page_link(&ait[1].link, raw + 20)) {
		for (i = 0; i < 12; i++)
			if ((n = vbi_unpar8 (raw[i + 28])) >= 0)
				ait[1].text[i] = n;
	}

	return TRUE;
}

static inline vbi_bool
parse_mpt(cache_network *cn, uint8_t *raw, int packet)
{
	int i, j, index;
	int n;

	switch (packet) {
	case 1 ... 20:
		index = dec2bcdp[packet - 1];

		for (i = 0; i < 4; i++) {
			for (j = 0; j < 10; index++, j++)
				if ((n = vbi_unham8 (*raw++)) >= 0) {
					struct ttx_page_stat *ps;
					int code, subc;

					ps = cache_network_page_stat
						(cn, 0x100 + index);
					code = ps->page_type;
					subc = ps->subcode;

					if (n > 9)
						n = 0xFFFEL; /* mpt_ex? not transm?? */

					if (code != VBI_NO_PAGE && code != VBI_UNKNOWN_PAGE
					    && (subc >= 0xFFFF || n > subc))
						ps->subcode = n;
				}

			index += ((index & 0xFF) == 0x9A) ? 0x66 : 0x06;
		}
	}

	return TRUE;
}

static inline vbi_bool
parse_mpt_ex(cache_network *cn, uint8_t *raw, int packet)
{
	int i, code, subc;
	struct ttx_page_link p;

	switch (packet) {
	case 1 ... 23:
		for (i = 0; i < 5; raw += 8, i++) {
			struct ttx_page_stat *ps;

			if (!unham_top_page_link(&p, raw))
				continue;

			if (0) {
				printf("MPT-EX #%d: ", (packet - 1) * 5);
				dump_page_link(p);
			}

			if (p.pgno < 0x100)
				break;
			else if (p.pgno > 0x8FF || p.subno < 1)
				continue;

			ps = cache_network_page_stat (cn, p.pgno);
			code = ps->page_type;
			subc = ps->subcode;

			if (code != VBI_NO_PAGE && code != VBI_UNKNOWN_PAGE
			    && (p.subno > subc /* evidence */
				/* || subc >= 0xFFFF unknown */
				|| subc >= 0xFFFE /* mpt > 9 */))
				ps->subcode = p.subno;
		}

		break;
	}

	return TRUE;
}

/**
 * @internal
 * @param vbi Initialized vbi decoding context.
 * @param vtp Raw teletext page to be converted.
 * @param cached The raw page is already cached, update the cache.
 * @param new_function The page function to convert to.
 * 
 * Since MOT, MIP and X/28 are optional, the function of a system page
 * may not be clear until we format a LOP and find a link of certain type,
 * so this function converts a page "after the fact".
 * 
 * @return
 * Pointer to the converted page, either @a vtp or the cached copy.
 **/
cache_page *
vbi_convert_page(vbi_decoder *vbi, cache_page *vtp,
		 vbi_bool cached, enum ttx_page_function new_function)
{
	cache_page page;
	int i;

	if (vtp->function != PAGE_FUNCTION_UNKNOWN)
		return NULL;

	memcpy(&page, vtp, sizeof(*vtp)	- sizeof(vtp->data) + sizeof(vtp->data.unknown));

	switch (new_function) {
	case PAGE_FUNCTION_LOP:
		vtp->function = new_function;
		return vtp;

	case PAGE_FUNCTION_GPOP:
	case PAGE_FUNCTION_POP:
		memset(page.data.pop.pointer, 0xFF, sizeof(page.data.pop.pointer));
		memset(page.data.pop.triplet, 0xFF, sizeof(page.data.pop.triplet));

		for (i = 1; i <= 25; i++)
			if (vtp->lop_packets & (1 << i))
				if (!parse_pop(&page, vtp->data.unknown.raw[i], i))
					return FALSE;

		if (vtp->x26_designations) {
			memcpy (&page.data.pop.triplet[23 * 13],
				vtp->data.enh_lop.enh,
				16 * 13 * sizeof (struct ttx_triplet));
		}

		break;

	case PAGE_FUNCTION_GDRCS:
	case PAGE_FUNCTION_DRCS:
		memmove (&page.data.drcs.lop,
			 &vtp->data.unknown,
			 sizeof (page.data.drcs.lop));
		CLEAR (page.data.drcs.mode);
		page.lop_packets = vtp->lop_packets;

		if (!convert_drcs(&page, vtp->data.unknown.raw[1]))
			return FALSE;

		break;

	case PAGE_FUNCTION_AIT:
		CLEAR (page.data.ait);

		for (i = 1; i <= 23; i++)
			if (vtp->lop_packets & (1 << i))
				if (!parse_ait(&page, vtp->data.unknown.raw[i], i))
					return FALSE;
		break;

	case PAGE_FUNCTION_MPT:
		for (i = 1; i <= 20; i++)
			if (vtp->lop_packets & (1 << i))
				if (!parse_mpt(vbi->cn, vtp->data.unknown.raw[i], i))
					return FALSE;
		break;

	case PAGE_FUNCTION_MPT_EX:
		for (i = 1; i <= 20; i++)
			if (vtp->lop_packets & (1 << i))
				if (!parse_mpt_ex(vbi->cn, vtp->data.unknown.raw[i], i))
					return FALSE;
		break;

	default:
		return NULL;
	}

	page.function = new_function;

	if (cached) {
		cache_page *new_vtp;

		new_vtp = _vbi_cache_put_page (vbi->ca, vbi->cn, &page);
		if (NULL != new_vtp)
			cache_page_unref (vtp);
		return new_vtp;
	} else {
		memcpy (vtp, &page, cache_page_size (&page));
		return vtp;
	}
}

static unsigned int
station_lookup(vbi_cni_type type, int cni, const char **country, const char **name)
{
	const struct vbi_cni_entry *p;

	if (!cni)
		return 0;

	switch (type) {
	case VBI_CNI_TYPE_8301:
		for (p = vbi_cni_table; p->name; p++)
			if (p->cni1 == cni) {
				*country = p->country;
				*name = p->name;
				return p->id;
			}
		break;

	case VBI_CNI_TYPE_8302:
		for (p = vbi_cni_table; p->name; p++)
			if (p->cni2 == cni) {
				*country = p->country;
				*name = p->name;
				return p->id;
			}

		cni &= 0x0FFF;

		/* fall through */

	case VBI_CNI_TYPE_VPS:
		/* if (cni == 0x0DC3) in decoder
			cni = mark ? 0x0DC2 : 0x0DC1; */

		for (p = vbi_cni_table; p->name; p++)
			if (p->cni4 == cni) {
				*country = p->country;
				*name = p->name;
				return p->id;
			}
		break;

	case VBI_CNI_TYPE_PDC_B:
		for (p = vbi_cni_table; p->name; p++)
			if (p->cni3 == cni) {
				*country = p->country;
				*name = p->name;
				return p->id;
			}

		/* try code | 0x0080 & 0x0FFF -> VPS ? */

		break;

	default:
		break;
	}

	return 0;
}

static void
unknown_cni(vbi_decoder *vbi, const char *dl, int cni)
{
	vbi = vbi;

	/* if (cni == 0) */
		return;

	fprintf(stderr,
"This network broadcasts an unknown CNI of 0x%04x using a %s data line.\n"
"If you see this message always when switching to this channel please\n"
"report network name, country, CNI and data line at http://zapping.sf.net\n"
"for inclusion in the Country and Network Identifier table. Thank you.\n",
		cni, dl);
}

/**
 * @internal
 * @param vbi Initialized vbi decoding context.
 * @param buf 13 bytes.
 * 
 * Decode a VPS datagram (13 bytes) according to
 * ETS 300 231 and update decoder state. This may
 * send a @a VBI_EVENT_NETWORK, @a VBI_EVENT_NETWORK_ID
 * or @a VBI_EVENT_PROG_ID.
 */
void
vbi_decode_vps(vbi_decoder *vbi, uint8_t *buf)
{
	vbi_network *n = &vbi->network.ev.network;
	const char *country, *name;
	unsigned int cni;

	vbi_decode_vps_cni (&cni, buf);

	if (cni != (unsigned int) n->cni_vps) {
		n->cni_vps = cni;
		n->cycle = 1;

		CLEAR (vbi->vps_pid);
		/* May fail, leaving vbi->vps_pid unmodified. */
		vbi_decode_vps_pdc (&vbi->vps_pid, buf);
	} else if (n->cycle == 1) {
		unsigned int id;

		id = station_lookup(VBI_CNI_TYPE_VPS, cni, &country, &name);

		if (0 == id) {
			n->name[0] = 0;
			unknown_cni(vbi, "VPS", cni);
		} else {
			strlcpy((char *) n->name, name, sizeof(n->name) - 1);
			n->name[sizeof(n->name) - 1] = 0;
		}

		if (id != n->nuid) {
			if (n->nuid != 0)
				vbi_chsw_reset(vbi, id);

			n->nuid = id;

			vbi->network.type = VBI_EVENT_NETWORK;
			vbi_send_event(vbi, &vbi->network);
		}

		vbi->network.type = VBI_EVENT_NETWORK_ID;
		vbi_send_event(vbi, &vbi->network);

		n->cycle = 2;

		if (vbi->event_mask & VBI_EVENT_PROG_ID) {
			vbi_program_id pid;
			vbi_event e;

			CLEAR (pid);
			if (!vbi_decode_vps_pdc (&pid, buf))
				return;

			/* VPS has no error protection so we send an
			   event only after we receive a PID twice. */
			if (0 != memcmp (&pid, &vbi->vps_pid,
					 sizeof (pid))) {
				vbi->vps_pid = pid;
				return;
			}

			/* We also send an event if the PID did not
			   change so the app can see if the signal is
			   still present. */

			CLEAR (e);

			e.type = VBI_EVENT_PROG_ID;
			e.ev.prog_id = &pid;

			vbi_send_event (vbi, &e);
		}
	}
}

static vbi_bool
parse_bsd(vbi_decoder *vbi, uint8_t *raw, int packet, int designation)
{
	vbi_network *n = &vbi->network.ev.network;
	int err, i;

	switch (packet) {
	case 26:
		/* TODO, iff */
		break;

	case 30:
		if (designation >= 4)
			break;

		if (designation <= 1) {
			const char *country, *name;
			int cni;
#if 0
			printf("\nPacket 8/30/%d:\n", designation);
#endif
			cni = vbi_rev16p (raw + 7);

			if (cni != n->cni_8301) {
				n->cni_8301 = cni;
				n->cycle = 1;
			} else if (n->cycle == 1) {
				unsigned int id;

				id = station_lookup(VBI_CNI_TYPE_8301, cni, &country, &name);

				if (!id) {
					n->name[0] = 0;
					unknown_cni(vbi, "8/30/1", cni);
				} else {
					strlcpy((char *) n->name, name,
						sizeof(n->name) - 1);
					n->name[sizeof(n->name) - 1] = 0;
				}

				if (id != n->nuid) {
					if (n->nuid != 0)
						vbi_chsw_reset(vbi, id);

					n->nuid = id;

					vbi->network.type = VBI_EVENT_NETWORK;
					vbi_send_event(vbi, &vbi->network);
				}

				vbi->network.type = VBI_EVENT_NETWORK_ID;
				vbi_send_event(vbi, &vbi->network);

				n->cycle = 2;
			}
#if 0
			if (1) { /* country and network identifier */
				if (station_lookup(VBI_CNI_TYPE_8301, cni, &country, &name))
					printf("... country: %s\n... station: %s\n", country, name);
				else
					printf("... unknown CNI %04x\n", cni);
			}

			if (1) { /* local time */
				int lto, mjd, utc_h, utc_m, utc_s;
				struct tm tm;
				time_t ti;

				lto = (raw[9] & 0x7F) >> 1;

				mjd = + ((raw[10] & 15) - 1) * 10000
				      + ((raw[11] >> 4) - 1) * 1000
				      + ((raw[11] & 15) - 1) * 100
				      + ((raw[12] >> 4) - 1) * 10
				      + ((raw[12] & 15) - 1);

			    	utc_h = ((raw[13] >> 4) - 1) * 10 + ((raw[13] & 15) - 1);
				utc_m = ((raw[14] >> 4) - 1) * 10 + ((raw[14] & 15) - 1);
				utc_s = ((raw[15] >> 4) - 1) * 10 + ((raw[15] & 15) - 1);

				ti = (mjd - 40587) * 86400 + 43200;
				localtime_r(&ti, &tm);

				printf("... local time: MJD %d %02d %s %04d, UTC %02d:%02d:%02d %c%02d%02d\n",
					mjd, tm.tm_mday, month_names[tm.tm_mon + 1], tm.tm_year + 1900,
					utc_h, utc_m, utc_s, (raw[9] & 0x80) ? '-' : '+', lto >> 1, (lto & 1) * 30);
			}
#endif /* BSDATA_TEST */

		} else /* if (designation <= 3) */ {
			int t, b[7];
			const char *country, *name;
			int cni;
#if 0
			printf("\nPacket 8/30/%d:\n", designation);
#endif
			for (err = i = 0; i < 7; i++) {
				err |= t = vbi_unham16p (raw + i * 2 + 6);
				b[i] = vbi_rev8 (t);
			}

			if (err < 0)
				return FALSE;

			cni = + ((b[4] & 0x03) << 10)
			      + ((b[5] & 0xC0) << 2)
			      + (b[2] & 0xC0)
			      + (b[5] & 0x3F)
			      + ((b[1] & 0x0F) << 12);

			if (cni == 0x0DC3)
				cni = (b[2] & 0x10) ? 0x0DC2 : 0x0DC1;

			if (cni != n->cni_8302) {
				n->cni_8302 = cni;
				n->cycle = 1;
			} else if (n->cycle == 1) {
				unsigned int id;

				id = station_lookup(VBI_CNI_TYPE_8302, cni, &country, &name);

				if (!id) {
					n->name[0] = 0;
					unknown_cni(vbi, "8/30/2", cni);
				} else {
					strlcpy((char *) n->name, name,
						sizeof(n->name) - 1);
					n->name[sizeof(n->name) - 1] = 0;
				}

				if (id != n->nuid) {
					if (n->nuid != 0)
						vbi_chsw_reset(vbi, id);

					n->nuid = id;

					vbi->network.type = VBI_EVENT_NETWORK;
					vbi_send_event(vbi, &vbi->network);
				}

				vbi->network.type = VBI_EVENT_NETWORK_ID;
				vbi_send_event(vbi, &vbi->network);

				n->cycle = 2;
			}

#if 0
			if (1) { /* country and network identifier */
				const char *country, *name;

				if (station_lookup(VBI_CNI_TYPE_8302, cni, &country, &name))
					printf("... country: %s\n... station: %s\n", country, name);
				else
					printf("... unknown CNI %04x\n", cni);
			}

			if (1) { /* PDC data */
				int lci, luf, prf, mi, pil;

				lci = (b[0] >> 2) & 3;
				luf = !!(b[0] & 2);
				prf = b[0] & 1;
				mi = !!(b[1] & 0x20);
				pil = ((b[2] & 0x3F) << 14) + (b[3] << 6) + (b[4] >> 2);

				printf("... label channel %d: update %d,"
				       " prepare to record %d, mode %d\n",
					lci, luf, prf, mi);
				dump_pil(pil);
			}

			if (1) {
				int pty, pcs;

				pcs = b[1] >> 6;
				pty = b[6];

				printf("... analog audio: %s\n", pcs_names[pcs]);
				dump_pty(pty);
			}
#endif /* BSDATA_TEST */

		}

#if 0
		/*
		 *  "transmission status message, e.g. the programme title",
		 *  "default G0 set". XXX add to program_info event.
		 */
		if (1) { 
			printf("... status: \"");

			for (i = 20; i < 40; i++) {
				int c = vbi_parity(raw[i]);

				c = (c < 0) ? '?' : _vbi_to_ascii (c);
				putchar(c);
			}

			printf("\"\n");
		}
#endif
		return TRUE;
	}

	return TRUE;
}

static int
same_header(int cur_pgno, const uint8_t *cur,
	    int ref_pgno, const uint8_t *ref,
	    int *page_num_offsetp)
{
	uint8_t buf[3];
	int i, j = 32 - 3, err = 0, neq = 0;

	ref_pgno = ref_pgno;

	/* Assumes vbi_is_bcd(cur_pgno) */
	buf[2] = (cur_pgno & 15) + '0';
	buf[1] = ((cur_pgno >> 4) & 15) + '0';
	buf[0] = (cur_pgno >> 8) + '0';

	vbi_par (buf, 3);

	for (i = 8; i < 32; cur++, ref++, i++) {
		/* Skip page number */
		if (i < j
		    && cur[0] == buf[0]
		    && cur[1] == buf[1]
		    && cur[2] == buf[2]) {
			j = i; /* here, once */
			i += 3;
			cur += 3;
			ref += 3;
			continue;
		}

		err |= vbi_unpar8 (*cur);
		err |= vbi_unpar8 (*ref);

		neq |= *cur - *ref;
	}

	if (err < 0 || j >= 32 - 3) /* parity error, rare */
		return -2; /* inconclusive, useless */

	*page_num_offsetp = j;

	if (!neq)
		return TRUE;

	/* Test false negative due to date transition */

	if (((ref[32] * 256 + ref[33]) & 0x7F7F) == 0x3233
	    && ((cur[32] * 256 + cur[33]) & 0x7F7F) == 0x3030) {
		return -1; /* inconclusive */
	}

	/*
	 *  The problem here is that individual pages or
	 *  magazines from the same network can still differ.
	 */
	return FALSE;
}

static inline vbi_bool
same_clock(const uint8_t *cur, const uint8_t *ref)
{
	int i;

	for (i = 32; i < 40; cur++, ref++, i++)
	       	if (*cur != *ref
		    && (vbi_unpar8 (*cur) | vbi_unpar8 (*ref)) >= 0)
			return FALSE;
	return TRUE;
}

static inline vbi_bool
store_lop(vbi_decoder *vbi, const cache_page *vtp)
{
	struct ttx_page_stat *ps;
	cache_page *new_cp;
	vbi_event event;

	event.type = VBI_EVENT_TTX_PAGE;

	event.ev.ttx_page.pgno = vtp->pgno;
	event.ev.ttx_page.subno = vtp->subno;

	event.ev.ttx_page.roll_header =
		(((vtp->flags & (  C5_NEWSFLASH
				 | C6_SUBTITLE 
				 | C7_SUPPRESS_HEADER
				 | C9_INTERRUPTED
			         | C10_INHIBIT_DISPLAY)) == 0)
		 && (vtp->pgno <= 0x199
		     || (vtp->flags & C11_MAGAZINE_SERIAL))
		 && vbi_is_bcd(vtp->pgno) /* no hex numbers */);

	event.ev.ttx_page.header_update = FALSE;
	event.ev.ttx_page.raw_header = NULL;
	event.ev.ttx_page.pn_offset = -1;

	/*
	 *  We're not always notified about a channel switch,
	 *  this code prevents a terrible mess in the cache.
	 *
	 *  The roll_header thing shall reduce false negatives,
	 *  slows down detection of some stations, but does help.
	 *  A little. Maybe this should be optional.
	 */
	if (event.ev.ttx_page.roll_header) {
		int r;

		if (vbi->vt.header_page.pgno == 0) {
			/* First page after channel switch */
			r = same_header(vtp->pgno, vtp->data.lop.raw[0] + 8,
					vtp->pgno, vtp->data.lop.raw[0] + 8,
					&event.ev.ttx_page.pn_offset);
			event.ev.ttx_page.header_update = TRUE;
			event.ev.ttx_page.clock_update = TRUE;
		} else {
			r = same_header(vtp->pgno, vtp->data.lop.raw[0] + 8,
					vbi->vt.header_page.pgno, vbi->vt.header + 8,
					&event.ev.ttx_page.pn_offset);
			event.ev.ttx_page.clock_update =
				!same_clock(vtp->data.lop.raw[0], vbi->vt.header);
		}

		switch (r) {
		case TRUE:
			// fprintf(stderr, "+");

			pthread_mutex_lock(&vbi->chswcd_mutex);
			vbi->chswcd = 0;
			pthread_mutex_unlock(&vbi->chswcd_mutex);

			vbi->vt.header_page.pgno = vtp->pgno;
			memcpy(vbi->vt.header + 8,
			       vtp->data.lop.raw[0] + 8, 32);

			event.ev.ttx_page.raw_header = vbi->vt.header;

			break;

		case FALSE:
			/*
			 *  What can I do when every magazin has its own
			 *  header? Ouch. Let's hope p100 repeats frequently.
			 */
			if (((vtp->pgno ^ vbi->vt.header_page.pgno) & 0xF00) == 0) {
			     /* pthread_mutex_lock(&vbi->chswcd_mutex);
				if (vbi->chswcd == 0)
					vbi->chswcd = 40;
				pthread_mutex_unlock(&vbi->chswcd_mutex); */

				vbi_chsw_reset(vbi, 0);
				return TRUE;
			}

			/* fall through */

		default: /* inconclusive */
			pthread_mutex_lock(&vbi->chswcd_mutex);

			if (vbi->chswcd > 0) {
				pthread_mutex_unlock(&vbi->chswcd_mutex);
				return TRUE;
			}

			pthread_mutex_unlock(&vbi->chswcd_mutex);

			if (r == -1) {
				vbi->vt.header_page.pgno = vtp->pgno;
				memcpy(vbi->vt.header + 8,
				       vtp->data.lop.raw[0] + 8, 32);

				event.ev.ttx_page.raw_header = vbi->vt.header;

				// fprintf(stderr, "/");
			} else /* broken header */ {
				event.ev.ttx_page.roll_header = FALSE;
				event.ev.ttx_page.clock_update = FALSE;

				// fprintf(stderr, "X");
			}

			break;
		}

		if (0) {
			int i;

			for (i = 0; i < 40; i++)
				putchar(_vbi_to_ascii (vtp->data.unknown.raw[0][i]));
			putchar('\r');
			fflush(stdout);
		}
	} else {
		// fprintf(stderr, "-");
	}

	/*
	 *  Collect information about those pages
	 *  not listed in MIP etc.
	 */
	ps = cache_network_page_stat (vbi->cn, vtp->pgno);

	if (ps->page_type == VBI_SUBTITLE_PAGE) {
		if (ps->charset_code == 0xFF)
			ps->charset_code = page_language
				(&vbi->vt, vbi->cn, vtp, 0, 0);
	} else if (ps->page_type == VBI_NO_PAGE
		   || ps->page_type == VBI_UNKNOWN_PAGE) {
		ps->page_type = VBI_NORMAL_PAGE;
	}

	if (ps->subcode >= 0xFFFE || vtp->subno > ps->subcode)
		ps->subcode = vtp->subno;

	/*
	 *  Store the page and send event.
	 */

	new_cp = _vbi_cache_put_page (vbi->ca, vbi->cn, vtp);
	if (NULL != new_cp) {
		vbi_send_event(vbi, &event);
		cache_page_unref (new_cp);
	}

	return TRUE;
}

static void
lop_parity_check		(cache_page *		cvtp,
				 struct raw_page *	rvtp)
{
	if (0 != cvtp->x26_designations) {
		struct ttx_triplet *trip = cvtp->data.enh_lop.enh;
		struct ttx_triplet *trip_end = trip + N_ELEMENTS (cvtp->data.enh_lop.enh);
		unsigned int row = 0;

		/* This is a little work-around for Teletext encoders
		   which transmit X/26 fallback characters with even
		   parity as noted in EN 300 706 Table 25. The page
		   formatting code can detect parity errors and pick a
		   replacement character, however the parity check
		   below attempts to correct errors when a page is
		   retransmitted, and requires odd parity on all
		   characters. */

		for (; trip < trip_end; ++trip) {
			if (trip->address < 40) {
				switch (trip->mode) {
				case 0x01: /* G1 block mosaic character */
				case 0x02: /* G3 smooth mosaic or line drawing character */
				case 0x0B: /* G3 smooth mosaic or line drawing character */
				case 0x08: /* modified G0 and G2 character set designation */
				case 0x09: /* G0 character */
				case 0x0D: /* drcs character invocation */
				case 0x0F: /* G2 character */
				case 0x10 ... 0x1F: /* characters including diacritical marks */
				{
					unsigned int column = trip->address;
					unsigned int c = rvtp->lop_raw[row][column];
					rvtp->lop_raw[row][column] = vbi_par8 (c);
					break;
				}
				default:
					break;
				}
			} else if (trip->address > 63) {
				/* Missed triplet or uncorrectable transmission error. */
				break;
			} else {
				switch (trip->mode) {
				case 0x01: /* full row colour */
				case 0x04: /* set active position */
					row = trip->address - 40;
					if (0 == row)
						row = 24;
					break;
				case 0x07: /* address display row 0 */
					row = 0;
					break;
				default:
					break;
				}
			}
		}
	}

	/* Level 1 parity check. */

	{
		unsigned int packet;

		for (packet = 1; packet <= 25; ++packet) {
			unsigned int i;
			int n;

			if (0 == (rvtp->lop_packets & (1 << packet)))
				continue;

			n = 0;
			for (i = 0; i < 40; ++i)
				n |= vbi_unpar8 (rvtp->lop_raw[packet][i]);
			if (n >= 0) {
				/* Parity is good, replace cached row. We
				   could replace individual characters, but
				   a single parity bit isn't very reliable. */
				memcpy (cvtp->data.lop.raw[packet],
					rvtp->lop_raw[packet], 40);
				cvtp->lop_packets |= 1 << packet;
			}
		}
	}
}

#define TTX_EVENTS (VBI_EVENT_TTX_PAGE)
#define BSDATA_EVENTS (VBI_EVENT_NETWORK | VBI_EVENT_NETWORK_ID)

/*
 *  Teletext packet 27, page linking
 */
static inline vbi_bool
parse_27(vbi_decoder *vbi, uint8_t *p,
	 cache_page *cvtp, int mag0)
{
	int designation, control;
	int i;

	vbi = vbi;

	if (cvtp->function == PAGE_FUNCTION_DISCARD)
		return TRUE;

	if ((designation = vbi_unham8 (*p)) < 0)
		return FALSE;

//	printf("Packet X/27/%d page %x\n", designation, cvtp->pgno);

	switch (designation) {
	case 0:
		if ((control = vbi_unham8 (p[37])) < 0)
			return FALSE;

		/* printf("%x.%x X/27/%d %02x\n",
		       cvtp->pgno, cvtp->subno, designation, control); */
#if 0
/*
 *  CRC cannot be trusted, some stations transmit rubbish.
 *  Link Control Byte bits 1 ... 3 cannot be trusted, ETS 300 706 is
 *  inconclusive and not all stations follow the suggestions in ETR 287.
 */
		crc = p[38] + p[39] * 256;
		/* printf("CRC: %04x\n", crc); */

		if ((control & 7) == 0)
			return FALSE;
#endif
		cvtp->data.unknown.have_flof = control >> 3; /* display row 24 */

		/* fall through */
	case 1:
	case 2:
	case 3:
		for (p++, i = 0; i <= 5; p += 6, i++) {
			if (!unham_page_link(cvtp->data.unknown.link
					       + designation * 6 + i, p, mag0)) {
				/* return TRUE; */
			}

// printf("X/27/%d link[%d] page %03x/%03x\n", designation, i,
//	cvtp->data.unknown.link[designation * 6 + i].pgno, cvtp->data.unknown.link[designation * 6 + i].subno);
		}

		break;

	case 4:
	case 5:
		for (p++, i = 0; i <= 5; p += 6, i++) {
			int t1, t2;

			t1 = vbi_unham24p (p + 0);
			t2 = vbi_unham24p (p + 3);

			if ((t1 | t2) < 0)
				return FALSE;

			cvtp->data.unknown.link[designation * 6 + i].function = t1 & 3;
			cvtp->data.unknown.link[designation * 6 + i].pgno =
				((((t1 >> 12) & 0x7) ^ mag0) ? : 8) * 256
				+ ((t1 >> 11) & 0x0F0) + ((t1 >> 7) & 0x00F);
			cvtp->data.unknown.link[designation * 6 + i].subno =
				(t2 >> 3) & 0xFFFF;
if(0)
 printf("X/27/%d link[%d] type %d page %03x subno %04x\n", designation, i,
	cvtp->data.unknown.link[designation * 6 + i].function,
	cvtp->data.unknown.link[designation * 6 + i].pgno,
	cvtp->data.unknown.link[designation * 6 + i].subno);
		}

		break;
	}

	return TRUE;
}

struct bit_stream {
	int *			triplet;
	unsigned int		buffer;
	unsigned int		left;
};

static unsigned int
get_bits			(struct	bit_stream *	bs,
				 unsigned int		count)
{
	unsigned int r;
	int n;

	r = bs->buffer;
	n = count - bs->left;

	if (n > 0) {
		bs->buffer = *(bs->triplet)++;
		r |= bs->buffer << bs->left;
		bs->left = 18 - n;
	} else {
		n = count;
		bs->left -= count;
	}

	bs->buffer >>= n;

	return r & ((1UL << count) - 1);
}

/*
 *  Teletext packets 28 and 29, Level 2.5/3.5 enhancement
 */
static vbi_bool
parse_28_29(vbi_decoder *vbi, uint8_t *p,
	    cache_page *cvtp, int mag8, int packet)
{
	int designation, function, coding;
	int triplets[13];
	struct bit_stream bs;
	struct ttx_extension *ext;
	int i, j, err = 0;

	if ((designation = vbi_unham8 (*p)) < 0)
		return FALSE;

	if (0)
		fprintf(stderr, "Packet %d/%d/%d page %x\n",
			mag8, packet, designation, cvtp->pgno);

	for (p++, i = 0; i < 13; p += 3, i++)
		err |= triplets[i] = vbi_unham24p (p);

	bs.triplet = triplets;
	bs.buffer = 0;
	bs.left = 0;

	switch (designation) {
	case 0: /* X/28/0, M/29/0 Level 2.5 */
	case 4: /* X/28/4, M/29/4 Level 3.5 */
		if (err < 0)
			return FALSE;

		function = get_bits (&bs, 4);
		coding = get_bits (&bs, 3); /* page coding ignored */

//		printf("... function %d\n", function);

		/*
		 *  ZDF and BR3 transmit GPOP 1EE/.. with 1/28/0 function
		 *  0 = PAGE_FUNCTION_LOP, should be PAGE_FUNCTION_GPOP.
		 *  Makes no sense to me. Update: also encountered pages
		 *  mFE and mFF with function = 0. Strange. 
		 */
		if (function != PAGE_FUNCTION_LOP && packet == 28) {
			if (cvtp->function != PAGE_FUNCTION_UNKNOWN
			    && cvtp->function != function)
				return FALSE; /* XXX discard rpage? */

// XXX rethink		cvtp->function = function;
		}

		if (function != PAGE_FUNCTION_LOP)
			return FALSE;

		/* XXX X/28/0 Format 2, distinguish how? */

		ext = &cache_network_magazine (vbi->cn, mag8 * 0x100)->extension;

		if (packet == 28) {
			if (!cvtp->data.ext_lop.ext.designations) {
				cvtp->data.ext_lop.ext = *ext;
			}

			cvtp->x28_designations |= 1 << designation;

			ext = &cvtp->data.ext_lop.ext;
		}

		if (designation == 4 && (ext->designations & (1 << 0)))
			get_bits (&bs, 14 + 2 + 1 + 4);
		else {
			vbi_bool left_panel;
			vbi_bool right_panel;
			unsigned int left_columns;

			ext->charset_code[0] = get_bits (&bs, 7);
			ext->charset_code[1] = get_bits (&bs, 7);

			left_panel = get_bits (&bs, 1);
			right_panel = get_bits (&bs, 1);

			/* 0 - panels required at Level 3.5 only,
			   1 - at 2.5 and 3.5
			   ignored. */
			get_bits (&bs, 1);

			left_columns = get_bits (&bs, 4);

			if (left_panel && 0 == left_columns)
				left_columns = 16;

			ext->fallback.left_panel_columns =
				left_columns & -left_panel;
			ext->fallback.right_panel_columns =
				(16 - left_columns) & -right_panel;
		}

		j = (designation == 4) ? 16 : 32;

		for (i = j - 16; i < j; i++) {
			vbi_rgba col = get_bits (&bs, 12);

			if (i == 8) /* transparent */
				continue;

			col = VBI_RGBA((col >> 0) & 15,
				       (col >> 4) & 15,
				       (col >> 8) & 15);

			ext->color_map[i] = col | (col << 4);
		}

		if (designation == 4 && (ext->designations & (1 << 0)))
			get_bits (&bs, 10 + 1 + 3);
		else {
			ext->def_screen_color = get_bits (&bs, 5);
			ext->def_row_color = get_bits (&bs, 5);

			ext->fallback.black_bg_substitution = get_bits (&bs, 1);

			i = get_bits (&bs, 3); /* color table remapping */

			ext->foreground_clut = "\00\00\00\10\10\20\20\20"[i];
			ext->background_clut = "\00\10\20\10\20\10\20\30"[i];
		}

		ext->designations |= 1 << designation;

		if (packet == 29) {
			if (0 && designation == 4)
				ext->designations &= ~(1 << 0);

			/*
			    XXX update
			    inherited_mag_desig =
			       page->extension.designations >> 16;
			    new_mag_desig = 1 << designation;
			    page_desig = page->extension.designations;
			    if (((inherited_mag_desig | page_desig)
			       & new_mag_desig) == 0)
			    shortcut: AND of (inherited_mag_desig | page_desig)
			      of all pages with extensions, no updates required
			      in round 2++
			    other option, all M/29/x should have been received
			      within the maximum repetition interval of 20 s.
			*/
		}

		return FALSE;

	case 1: /* X/28/1, M/29/1 Level 3.5 DRCS CLUT */
		ext = &cache_network_magazine (vbi->cn, mag8 * 0x100)->extension;

		if (packet == 28) {
			if (!cvtp->data.ext_lop.ext.designations) {
				cvtp->data.ext_lop.ext = *ext;
			}

			cvtp->x28_designations |= 1 << designation;

			ext = &cvtp->data.ext_lop.ext;
			/* XXX TODO - lop? */
		}

		/* 9.4.4: "Compatibility, not for Level 2.5/3.5 decoders."
		   No more details, so we ignore this triplet. */
		++bs.triplet;

		for (i = 0; i < 8; i++)
			ext->drcs_clut[i + 2] = vbi_rev8 (get_bits (&bs, 5)) >> 3;

		for (i = 0; i < 32; i++)
			ext->drcs_clut[i + 10] = vbi_rev8 (get_bits (&bs, 5)) >> 3;

		ext->designations |= 1 << 1;

		if (0)
			dump_extension(ext);

		return FALSE;

	case 3: /* X/28/3 Level 2.5, 3.5 DRCS download page */
		if (packet == 29)
			break; /* M/29/3 undefined */

		if (err < 0)
			return FALSE;

		function = get_bits (&bs, 4);
		coding = get_bits (&bs, 3); /* page coding ignored */

		if (function != PAGE_FUNCTION_GDRCS
		    || function != PAGE_FUNCTION_DRCS)
			return FALSE;

		if (cvtp->function == PAGE_FUNCTION_UNKNOWN) {
			/* If to prevent warning: statement with no effect
			   when .raw unions coincidentally align. */
			if (&cvtp->data.drcs.lop != &cvtp->data.unknown) {
				memmove (&cvtp->data.drcs.lop,
					 &cvtp->data.unknown,
					 sizeof (cvtp->data.drcs.lop));
			}
			cvtp->function = function;
		} else if (cvtp->function != function) {
			cvtp->function = PAGE_FUNCTION_DISCARD;
			return 0;
		}

		get_bits (&bs, 11);

		for (i = 0; i < 48; i++)
			cvtp->data.drcs.mode[i] = get_bits (&bs, 4);

	default: /* ? */
		break;
	}

	return TRUE;
}

/*
 *  Teletext packet 8/30, broadcast service data
 */
static inline vbi_bool
parse_8_30(vbi_decoder *vbi, uint8_t *buffer, int packet)
{
	uint8_t *p;
	int designation;

	p = buffer + 2;

	if ((designation = vbi_unham8 (*p)) < 0)
		return FALSE;

	// printf("Packet 8/30/%d\n", designation);

	if (designation > 4)
		return TRUE; /* ignored */

	if (vbi->event_mask & TTX_EVENTS) {
		if (!unham_page_link(&vbi->cn->initial_page, p + 1, 0))
			return FALSE;

		if ((vbi->cn->initial_page.pgno & 0xFF) == 0xFF) {
			vbi->cn->initial_page.pgno = 0x100;
			vbi->cn->initial_page.subno = VBI_ANY_SUBNO;
		}
	}

	if (vbi->event_mask & BSDATA_EVENTS) {
		if (!parse_bsd(vbi, p, packet, designation))
			return FALSE;
	}

	if (designation < 2) {
		/* 8/30 format 1 */

		if (vbi->event_mask & VBI_EVENT_LOCAL_TIME) {
			vbi_local_time lt;
			vbi_event e;

			CLEAR (e);

			if (!vbi_decode_teletext_8301_local_time
			    (&lt.time, &lt.seconds_east, buffer))
				return FALSE;

			lt.seconds_east_valid = TRUE;
			lt.dst_state = VBI_DST_INCLUDED;

			e.type = VBI_EVENT_LOCAL_TIME;
			e.ev.local_time	= &lt;

			vbi_send_event (vbi, &e);
		}
	} else {
		/* 8/30 format 2 */

		if (vbi->event_mask & VBI_EVENT_PROG_ID) {
			vbi_program_id pid;
			vbi_event e;
			
			if (!vbi_decode_teletext_8302_pdc (&pid, buffer))
				return FALSE;

			CLEAR (e);

			e.type = VBI_EVENT_PROG_ID;
			e.ev.prog_id = &pid;

			vbi_send_event (vbi, &e);
		}
	}

	return TRUE;
}

/**
 * @internal
 * @param vbi Initialized vbi decoding context.
 * @param p Packet data.
 * 
 * Parse a teletext packet (42 bytes) and update the decoder
 * state accordingly. This function may send events.
 * 
 * Return value:
 * FALSE if the packet contained uncorrectable errors. 
 */
vbi_bool
vbi_decode_teletext(vbi_decoder *vbi, uint8_t *buffer)
{
	cache_page *cvtp;
	struct raw_page *rvtp;
	int pmag, mag0, mag8, packet;
	struct ttx_magazine *mag;
	uint8_t *p;

	p = buffer;

	if ((pmag = vbi_unham16p (p)) < 0)
		return FALSE;

	mag0 = pmag & 7;
	mag8 = mag0 ? : 8;
	packet = pmag >> 3;

	if (packet < 30
	    && !(vbi->event_mask & TTX_EVENTS))
		return TRUE;

	mag = cache_network_magazine (vbi->cn, mag8 * 0x100);
	rvtp = vbi->vt.raw_page + mag0;
	cvtp = rvtp->page;

	p += 2;

	if (0) {
		unsigned int i;

		fprintf(stderr, "packet 0x%x %d >", mag8 * 0x100, packet);
		for (i = 0; i < 40; i++)
			fputc(_vbi_to_ascii (p[i]), stderr);
		fprintf(stderr, "<\n");
	}

	switch (packet) {
	case 0:
	{
		int pgno, page, subpage, flags;
		struct raw_page *curr;
		cache_page *vtp;
		int i;

		if ((page = vbi_unham16p (p)) < 0) {
			vbi_teletext_desync(vbi);
//			printf("Hamming error in packet 0 page number\n");
			return FALSE;
		}

		pgno = mag8 * 256 + page;

		/*
		 *  Store page terminated by new header.
		 */
		while ((curr = vbi->vt.current)) {
			vtp = curr->page;

			if (vtp->flags & C11_MAGAZINE_SERIAL) {
				if (vtp->pgno == pgno)
					break;
			} else {
				curr = rvtp;
				vtp = curr->page;

				if ((vtp->pgno & 0xFF) == page)
					break;
			}

			switch (vtp->function) {
			case PAGE_FUNCTION_DISCARD:
			case PAGE_FUNCTION_EPG:
				break;

			case PAGE_FUNCTION_LOP:
				lop_parity_check(vtp, curr);
				if (!store_lop(vbi, vtp))
					return FALSE;
				break;

			case PAGE_FUNCTION_DRCS:
			case PAGE_FUNCTION_GDRCS:
			{
				if (convert_drcs(vtp,
						 vtp->data.drcs.lop.raw[1]))
					_vbi_cache_put_page (vbi->ca,
							     vbi->cn, vtp);
				break;
			}

			case PAGE_FUNCTION_MIP:
				parse_mip(vbi, vtp);
				break;

			case PAGE_FUNCTION_EACEM_TRIGGER:
				eacem_trigger(vbi, vtp);
				break;

			default:
			{
				cache_page *new_cp;

				new_cp = _vbi_cache_put_page
					(vbi->ca, vbi->cn, vtp);
				cache_page_unref (new_cp);
				break;
			}

			}

			vtp->function = PAGE_FUNCTION_DISCARD;
			break;
		}

		/*
		 *  Prepare for new page.
		 */

		cvtp->pgno = pgno;
		vbi->vt.current = rvtp;

		subpage = vbi_unham16p (p + 2) + vbi_unham16p (p + 4) * 256;
		flags = vbi_unham16p (p + 6);

		if (page == 0xFF || (subpage | flags) < 0) {
			cvtp->function = PAGE_FUNCTION_DISCARD;
			return FALSE;
		}

		cvtp->subno = subpage & 0x3F7F;
		cvtp->national = vbi_rev8 (flags) & 7;
		cvtp->flags = (flags << 16) + subpage;

		if (0 && ((page & 15) > 9 || page > 0x99))
			printf("data page %03x/%04x n%d\n",
			       cvtp->pgno, cvtp->subno, cvtp->national);

		if (1
		    && pgno != 0x1E7
		    && !(cvtp->flags & C4_ERASE_PAGE)
		    && (vtp = _vbi_cache_get_page (vbi->ca,
						   vbi->cn,
						   cvtp->pgno,
						   cvtp->subno,
						   /* subno_mask */ -1)))
		{
			memset(&cvtp->data, 0, sizeof(cvtp->data));
			memcpy(&cvtp->data, &vtp->data,
			       cache_page_size(vtp)
			       - sizeof(*vtp) + sizeof(vtp->data));

			/* XXX write cache directly | erc?*/
			/* XXX data page update */

			cvtp->function = vtp->function;

			switch (cvtp->function) {
			case PAGE_FUNCTION_UNKNOWN:
			case PAGE_FUNCTION_LOP:
				memcpy(cvtp->data.unknown.raw[0], p, 40);

			default:
				break;
			}

			cvtp->lop_packets = vtp->lop_packets;
			cvtp->x26_designations = vtp->x26_designations;
			cvtp->x27_designations = vtp->x27_designations;
			cvtp->x28_designations = vtp->x28_designations;

			cache_page_unref (vtp);
			vtp = NULL;
		} else {
			struct ttx_page_stat *ps;

			ps = cache_network_page_stat (vbi->cn, cvtp->pgno);

			cvtp->flags |= C4_ERASE_PAGE;

			if (0)
				printf("rebuilding %3x/%04x from scratch\n", cvtp->pgno, cvtp->subno);

			if (cvtp->pgno == 0x1F0) {
				cvtp->function = PAGE_FUNCTION_BTT;
				ps->page_type = VBI_TOP_PAGE;
			} else if (cvtp->pgno == 0x1E7) {
				cvtp->function = PAGE_FUNCTION_EACEM_TRIGGER;
				ps->page_type = VBI_DISP_SYSTEM_PAGE;
				ps->subcode = 0;
				memset(cvtp->data.unknown.raw[0], 0x20, sizeof(cvtp->data.unknown.raw));
				memset(cvtp->data.enh_lop.enh, 0xFF, sizeof(cvtp->data.enh_lop.enh));

			} else if (page == 0xFD) {
				cvtp->function = PAGE_FUNCTION_MIP;
				ps->page_type = VBI_SYSTEM_PAGE;
			} else if (page == 0xFE) {
				cvtp->function = PAGE_FUNCTION_MOT;
				ps->page_type = VBI_SYSTEM_PAGE;
			} else if (FPC && ps->page_type == VBI_EPG_DATA) {
				cvtp->function = PAGE_FUNCTION_DISCARD;
#if 0 /* TODO */
				int stream = (cvtp->subno >> 8) & 15;

				if (stream >= 2) {
					cvtp->function = PAGE_FUNCTION_DISCARD;
					// fprintf(stderr, "Discard FPC %d\n", stream);
				} else {
					struct page_clear *pc = vbi->epg_pc + stream;
					int ci = cvtp->subno & 15;

					cvtp->function = PAGE_FUNCTION_EPG;
					pc->pfc.pgno = cvtp->pgno;

					if (((pc->ci + 1) & 15) != ci)
						vbi_reset_page_clear(pc);

					pc->ci = ci;
					pc->packet = 0;
					pc->num_packets = ((cvtp->subno >> 4) & 7)
						+ ((cvtp->subno >> 9) & 0x18);
				}
#endif
			} else {
				cvtp->function = PAGE_FUNCTION_UNKNOWN;

				memcpy(cvtp->data.unknown.raw[0] + 0, p, 40);
				memset(cvtp->data.unknown.raw[0] + 40, 0x20, sizeof(cvtp->data.unknown.raw) - 40);
				memset(cvtp->data.unknown.link, 0xFF, sizeof(cvtp->data.unknown.link));
				memset(cvtp->data.enh_lop.enh, 0xFF, sizeof(cvtp->data.enh_lop.enh));

				cvtp->data.unknown.have_flof = FALSE;
			}

			cvtp->lop_packets = 1;
			cvtp->x26_designations = 0;
			cvtp->x27_designations = 0;
			cvtp->x28_designations = 0;		
		}

		if (cvtp->function == PAGE_FUNCTION_UNKNOWN) {
			enum ttx_page_function function;
			struct ttx_page_stat *ps;

			function = PAGE_FUNCTION_UNKNOWN;
			ps = cache_network_page_stat (vbi->cn, cvtp->pgno);

			switch (ps->page_type) {
			case 0x01 ... 0x51:
			case 0x70 ... 0x7F:
			case 0x81 ... 0xD1:
			case 0xF4 ... 0xF7:
			case VBI_TOP_BLOCK:
			case VBI_TOP_GROUP:
				function = PAGE_FUNCTION_LOP;
				break;

			case VBI_SYSTEM_PAGE:	/* no MOT or MIP?? */
				/* remains function = PAGE_FUNCTION_UNKNOWN; */
				break;

			case VBI_TOP_PAGE:
				for (i = 0; i < 8; i++)
					if (cvtp->pgno == vbi->cn->btt_link[i].pgno)
						break;
				if (i < 8) {
					switch (vbi->cn->btt_link[i].function) {
					case PAGE_FUNCTION_AIT:
					case PAGE_FUNCTION_MPT:
					case PAGE_FUNCTION_MPT_EX:
						function = vbi->cn->btt_link[i].function;
						break;

					default:
						if (0)
							printf("page is TOP, link %d, unknown type %d\n",
								i, vbi->cn->btt_link[i].function);
					}
				} else if (0)
					printf("page claims to be TOP, link not found\n");

				break;

			case 0xE5:
			case 0xE8 ... 0xEB:
				function = PAGE_FUNCTION_DRCS;
				break;

			case 0xE6:
			case 0xEC ... 0xEF:
				function = PAGE_FUNCTION_POP;
				break;

			case VBI_TRIGGER_DATA:
				function = PAGE_FUNCTION_EACEM_TRIGGER;
				break;

			case VBI_EPG_DATA:	/* EPG/NexTView transport layer */
				if (FPC) {
					function = PAGE_FUNCTION_EPG;
					break;
				}

				/* fall through */

			case 0x52 ... 0x6F:	/* reserved */
			case VBI_ACI:		/* ACI page */
			case VBI_NOT_PUBLIC:
			case 0xD2 ... 0xDF:	/* reserved */
			case 0xE0 ... 0xE2:	/* data broadcasting */
			case 0xE4:		/* data broadcasting */
			case 0xF0 ... 0xF3:	/* broadcaster system page */
				function = PAGE_FUNCTION_DISCARD;
				break;

			default:
				if (page <= 0x99 && (page & 15) <= 9)
					function = PAGE_FUNCTION_LOP;
				/* else remains
					function = PAGE_FUNCTION_UNKNOWN; */
			}

			if (function != PAGE_FUNCTION_UNKNOWN) {
				vbi_convert_page(vbi, cvtp, FALSE, function);
			}
		}
//XXX?
		cvtp->data.ext_lop.ext.designations = 0;
		rvtp->lop_packets = 0;
		rvtp->num_triplets = 0;

		return TRUE;
	}

	case 1 ... 25:
	{
		int n;
		int i;

		switch (cvtp->function) {
		case PAGE_FUNCTION_DISCARD:
			return TRUE;

		case PAGE_FUNCTION_MOT:
			if (!parse_mot(cache_network_magazine
				       (vbi->cn, mag8 * 0x100), p, packet))
				return FALSE;
			break;

		case PAGE_FUNCTION_GPOP:
		case PAGE_FUNCTION_POP:
			if (!parse_pop(cvtp, p, packet))
				return FALSE;
			break;

		case PAGE_FUNCTION_GDRCS:
		case PAGE_FUNCTION_DRCS:
			memcpy (cvtp->data.drcs.lop.raw[packet], p, 40);
			break;

		case PAGE_FUNCTION_BTT:
			if (!parse_btt(vbi, p, packet))
				return FALSE;
			break;

		case PAGE_FUNCTION_AIT:
			if (!(parse_ait(cvtp, p, packet)))
				return FALSE;
			break;

		case PAGE_FUNCTION_MPT:
			if (!(parse_mpt(vbi->cn, p, packet)))
				return FALSE;
			break;

		case PAGE_FUNCTION_MPT_EX:
			if (!(parse_mpt_ex(vbi->cn, p, packet)))
				return FALSE;
			break;

		case PAGE_FUNCTION_EPG:
#if 0 /* TODO */
			parse_page_clear(vbi->epg_pc + ((cvtp->subno >> 8) & 1), p, packet);
#endif
			break;

		case PAGE_FUNCTION_LOP:
			/* Parity check postponed until we received the X/26
			   enhancement packets pertaining to this page.
			   See lop_parity_check(). */
			memcpy(rvtp->lop_raw[packet], p, 40);
			rvtp->lop_packets |= 1 << packet;
			return TRUE;

		case PAGE_FUNCTION_EACEM_TRIGGER:
			for (n = i = 0; i < 40; i++)
				n |= vbi_unpar8 (p[i]);
			if (n < 0)
				return FALSE;

			/* fall through */

		case PAGE_FUNCTION_MIP:
		default:
			memcpy(cvtp->data.unknown.raw[packet], p, 40);
			break;
		}

		cvtp->lop_packets |= 1 << packet;

		break;
	}

	case 26:
	{
		int designation;
		struct ttx_triplet triplet;
		int i;

		/*
		 *  Page enhancement packet
		 */

		switch (cvtp->function) {
		case PAGE_FUNCTION_DISCARD:
			return TRUE;

		case PAGE_FUNCTION_GPOP:
		case PAGE_FUNCTION_POP:
			return parse_pop(cvtp, p, packet);

		case PAGE_FUNCTION_GDRCS:
		case PAGE_FUNCTION_DRCS:
		case PAGE_FUNCTION_BTT:
		case PAGE_FUNCTION_AIT:
		case PAGE_FUNCTION_MPT:
		case PAGE_FUNCTION_MPT_EX:
			/* X/26 ? */
			vbi_teletext_desync(vbi);
			return TRUE;

		case PAGE_FUNCTION_EACEM_TRIGGER:
		default:
			break;
		}

		if ((designation = vbi_unham8 (*p)) < 0)
			return FALSE;

		if (rvtp->num_triplets >= 16 * 13
		    || rvtp->num_triplets != designation * 13) {
			rvtp->num_triplets = -1;
			return FALSE;
		}

		for (p++, i = 0; i < 13; p += 3, i++) {
			int t = vbi_unham24p (p);

			if (t < 0)
				break; /* XXX */

			triplet.address = t & 0x3F;
			triplet.mode = (t >> 6) & 0x1F;
			triplet.data = t >> 11;

			cvtp->data.enh_lop.enh[rvtp->num_triplets++] = triplet;
		}

		cvtp->x26_designations |= 1 << designation;

		break;
	}

	case 27:
		if (!parse_27(vbi, p, cvtp, mag0))
			return FALSE;
		break;

	case 28:
		if (cvtp->function == PAGE_FUNCTION_DISCARD)
			break;

		/* fall through */
	case 29:
		if (!parse_28_29(vbi, p, cvtp, mag8, packet))
			return FALSE;
		break;

	case 30:
	case 31:
		/*
		 *  IDL packet (ETS 300 708)
		 */
		switch (/* Channel */ pmag & 15) {
		case 0: /* Packet 8/30 (ETS 300 706) */
			if (!parse_8_30(vbi, buffer, packet))
				return FALSE;
			break;

		default:
			break;
		}

		break;
	}

	return TRUE;
}

/*
 *  ETS 300 706 Table 30: Colour Map
 */
static const vbi_rgba
default_color_map[40] = {
	VBI_RGBA(0x00, 0x00, 0x00), VBI_RGBA(0xFF, 0x00, 0x00),
	VBI_RGBA(0x00, 0xFF, 0x00), VBI_RGBA(0xFF, 0xFF, 0x00),
	VBI_RGBA(0x00, 0x00, 0xFF), VBI_RGBA(0xFF, 0x00, 0xFF),
	VBI_RGBA(0x00, 0xFF, 0xFF), VBI_RGBA(0xFF, 0xFF, 0xFF),
	VBI_RGBA(0x00, 0x00, 0x00), VBI_RGBA(0x77, 0x00, 0x00),
	VBI_RGBA(0x00, 0x77, 0x00), VBI_RGBA(0x77, 0x77, 0x00),
	VBI_RGBA(0x00, 0x00, 0x77), VBI_RGBA(0x77, 0x00, 0x77),
	VBI_RGBA(0x00, 0x77, 0x77), VBI_RGBA(0x77, 0x77, 0x77),
	VBI_RGBA(0xFF, 0x00, 0x55), VBI_RGBA(0xFF, 0x77, 0x00),
	VBI_RGBA(0x00, 0xFF, 0x77), VBI_RGBA(0xFF, 0xFF, 0xBB),
	VBI_RGBA(0x00, 0xCC, 0xAA), VBI_RGBA(0x55, 0x00, 0x00),
	VBI_RGBA(0x66, 0x55, 0x22), VBI_RGBA(0xCC, 0x77, 0x77),
	VBI_RGBA(0x33, 0x33, 0x33), VBI_RGBA(0xFF, 0x77, 0x77),
	VBI_RGBA(0x77, 0xFF, 0x77), VBI_RGBA(0xFF, 0xFF, 0x77),
	VBI_RGBA(0x77, 0x77, 0xFF), VBI_RGBA(0xFF, 0x77, 0xFF),
	VBI_RGBA(0x77, 0xFF, 0xFF), VBI_RGBA(0xDD, 0xDD, 0xDD),

	/* Private colors */

	VBI_RGBA(0x00, 0x00, 0x00), VBI_RGBA(0xFF, 0xAA, 0x99),
	VBI_RGBA(0x44, 0xEE, 0x00), VBI_RGBA(0xFF, 0xDD, 0x00),
	VBI_RGBA(0xFF, 0xAA, 0x99), VBI_RGBA(0xFF, 0x00, 0xFF),
	VBI_RGBA(0x00, 0xFF, 0xFF), VBI_RGBA(0xEE, 0xEE, 0xEE)
};

/**
 * @param vbi Initialized vbi decoding context.
 * @param default_region A value between 0 ... 80, index into
 *   the Teletext character set table according to ETS 300 706,
 *   Section 15 (or libzvbi source file lang.c). The three last
 *   significant bits will be replaced.
 *
 * Teletext uses a 7 bit character set. To support multiple
 * languages there are eight national variants which replace
 * the square bracket, backslash and other characters in a
 * fashion similar to ISO 646. These national variants are
 * selected by a 3 bit code in the header of each Teletext page.
 *
 * Eventually eight character sets proved to be insufficient,
 * so manufacturers of Teletext decoders interpreted these bits
 * differently in certain countries or regions. Teletext Level
 * 1.5 finally defined a method to transmit an 8 bit character
 * code which applies to a single page or provides the upper 5
 * bits of the character code for all pages.
 *
 * Regrettably some networks still only transmit the lower 3
 * bits. With this function you can supply an 8 bit default
 * character code for all pages. The built-in default is 16.
 */
void
vbi_teletext_set_default_region(vbi_decoder *vbi, int default_region)
{
	int i;

	if (default_region < 0 || default_region > 87)
		return;

	vbi->vt.region = default_region;

	for (i = 0x100; i <= 0x800; i += 0x100) {
		struct ttx_extension *ext;

		ext = &cache_network_magazine (vbi->cn, i)->extension;

		ext->charset_code[0] = default_region;
		ext->charset_code[1] = 0;
	}

	vbi->vt.default_magazine.extension.charset_code[0] = default_region;
	vbi->vt.default_magazine.extension.charset_code[1] = 0;
}

/**
 * @param vbi Initialized vbi decoding context.
 * @param level 
 * 
 * @deprecated
 * This became a parameter of vbi_fetch_vt_page().
 */
void
vbi_teletext_set_level(vbi_decoder *vbi, int level)
{
	if (level < VBI_WST_LEVEL_1)
		level = VBI_WST_LEVEL_1;
	else if (level > VBI_WST_LEVEL_3p5)
		level = VBI_WST_LEVEL_3p5;

	vbi->vt.max_level = level;
}

/**
 * @internal
 * @param vbi Initialized vbi decoding context.
 * 
 * This function must be called after desynchronisation
 * has been detected (i. e. vbi data has been lost)
 * to reset the Teletext decoder.
 */
void
vbi_teletext_desync(vbi_decoder *vbi)
{
	int i;

	/* Discard all in progress pages */

	for (i = 0; i < 8; i++)
		vbi->vt.raw_page[i].page->function = PAGE_FUNCTION_DISCARD;

#if 0 /* TODO */
	vbi_reset_page_clear(vbi->epg_pc + 0);
	vbi_reset_page_clear(vbi->epg_pc + 1);

	vbi->epg_pc[0].pfc.stream = 1;
	vbi->epg_pc[1].pfc.stream = 2;
#endif
}

static void
ttx_extension_init		(struct ttx_extension *	ext)
{
	unsigned int i;

	CLEAR (*ext);

	ext->def_screen_color	= VBI_BLACK;	/* A.5 */
	ext->def_row_color	= VBI_BLACK;	/* A.5 */

	for (i = 0; i < 8; ++i)
		ext->drcs_clut[2 + i] = i & 3;

	for (i = 0; i < 32; ++i)
		ext->drcs_clut[2 + 8 + i] = i & 15;

	memcpy (ext->color_map, default_color_map,
		sizeof (ext->color_map));
}

static void
ttx_magazine_init		(struct ttx_magazine *	mag)
{
	ttx_extension_init (&mag->extension);

	/* Valid range 0 ... 7, -1 == broken link. */
	memset (mag->pop_lut, -1, sizeof (mag->pop_lut));
	memset (mag->drcs_lut, -1, sizeof (mag->pop_lut));

	/* NO_PAGE (pgno): (pgno & 0xFF) == 0xFF. */
	memset (mag->pop_link, -1, sizeof (mag->pop_link));
	memset (mag->drcs_link, -1, sizeof (mag->drcs_link));
}

static void
ttx_page_stat_init		(struct ttx_page_stat *	ps)
{
	CLEAR (*ps);

	ps->page_type		= VBI_UNKNOWN_PAGE;
	ps->charset_code	= 0xFF;
	ps->subcode		= SUBCODE_UNKNOWN;
}

/**
 * @param vbi Initialized vbi decoding context.
 * 
 * This function must be called after a channel switch,
 * to reset the Teletext decoder.
 */
void
vbi_teletext_channel_switched(vbi_decoder *vbi)
{
	unsigned int i;

	vbi->cn->initial_page.pgno = 0x100;
	vbi->cn->initial_page.subno = VBI_ANY_SUBNO;

	vbi->cn->have_top = FALSE;

	for (i = 0; i < N_ELEMENTS (vbi->cn->_pages); ++i)
		ttx_page_stat_init (vbi->cn->_pages + i);

	/* Magazine defaults */

	for (i = 0; i < N_ELEMENTS (vbi->cn->_magazines); ++i)
		ttx_magazine_init (vbi->cn->_magazines + i);

	vbi_teletext_set_default_region(vbi, vbi->vt.region);

	vbi_teletext_desync(vbi);
}


/**
 * @internal
 * @param vbi VBI decoding context.
 * 
 * This function is called during @a vbi destruction
 * to destroy the Teletext subset of @a vbi object.
 */
void
vbi_teletext_destroy(vbi_decoder *vbi)
{
	vbi = vbi;
}

/**
 * @internal
 * @param vbi VBI decoding context.
 * 
 * This function is called during @a vbi initialization
 * to initialize the Teletext subset of @a vbi object.
 */
void
vbi_teletext_init(vbi_decoder *vbi)
{
	init_expand();

	vbi->vt.region = 16;
	vbi->vt.max_level = VBI_WST_LEVEL_2p5;

	ttx_magazine_init (&vbi->vt.default_magazine);

	vbi_teletext_channel_switched(vbi);     /* Reset */
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
