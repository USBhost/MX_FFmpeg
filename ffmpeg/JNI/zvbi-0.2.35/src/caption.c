/*
 *  libzvbi -- Closed Caption decoder
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
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

/* $Id: caption.c,v 1.29 2009/02/16 13:41:51 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>

#include "misc.h"
#include "trigger.h"
#include "format.h"
#include "lang.h"
#include "hamm.h"
#include "tables.h"
#include "vbi.h"

#define elements(array) (sizeof(array) / sizeof(array[0]))

#define ITV_DEBUG(x) /* x */
#define XDS_SEP_DEBUG(x) /* x */
#define XDS_SEP_DUMP(x) /* x */
#define CC_DUMP(x) /* x */
#define CC_TEXT_DUMP(x) /* x */

static inline void
caption_send_event(vbi_decoder *vbi, vbi_event *ev)
{
	/* Permits calling vbi_fetch_cc_page from handler */
	pthread_mutex_unlock(&vbi->cc.mutex);

	vbi_send_event(vbi, ev);

	pthread_mutex_lock(&vbi->cc.mutex);
}

/*
 *  XDS (Extended Data Service) decoder
 */

#define XDS_CURRENT		0
#define XDS_FUTURE		1
#define XDS_CHANNEL		2
#define XDS_MISC		3
#define XDS_PUBLIC_SERVICE	4
#define XDS_RESERVED		5
#define XDS_UNDEFINED		6	/* proprietary format */

#define XDS_END			15

/* vbi_classify_page, program_info language */
static const char *
language[8] = {
	"Unknown",
	"English",
	"Español",
	"Français",
	"Deutsch",
	"Italiano",
	"Other",
	"None"
};

static uint32_t hcrc[128];

static void init_hcrc(void) __attribute__ ((constructor));


/*
http://www.fcc.gov/cgb/statid.html
 *
 *  XDS has no unique station id as EBU (or is the call sign?)
 *  so we create a checksum over the station name.
 */
static void
init_hcrc(void)
{
	unsigned int sum;
	int i, j;

	for (i = 0; i < 128; i++) {
		sum = 0;
		for (j = 7 - 1; j >= 0; j--)
			if (i & (1 << j))
				sum ^= 0x48000000L >> j;
		hcrc[i] = sum;
	}
}

static int
xds_strfu(signed char *d, const uint8_t *s, int len)
{
	int c, neq = 0;

	for (; len > 0 && *s <= 0x20; s++, len--);

	for (; len > 0; s++, len--) {
		c = MAX((uint8_t) 0x20, *s);
		neq |= *d ^ c;
		*d++ = c;
	}

	neq |= *d;
	*d = 0;

	return neq;
}

#define xds_intfu(d, val) (neq |= d ^ (val), d = (val)) 

static void
flush_prog_info(vbi_decoder *vbi, vbi_program_info *pi, vbi_event *e)
{
	e->ev.aspect = pi->aspect;

	vbi_reset_prog_info(pi);

	if (memcmp(&e->ev.aspect, &pi->aspect, sizeof(pi->aspect)) != 0) {
		e->type = VBI_EVENT_ASPECT;
		caption_send_event(vbi, e);
	}

	vbi->cc.info_cycle[pi->future] = 0;
}

static inline void
xds_decoder(vbi_decoder *vbi, int _class, int type,
	    uint8_t *buffer, int length)
{
	vbi_network *n = &vbi->network.ev.network;
	vbi_program_info *pi;
	int neq, i;
	vbi_event e;

	assert(length > 0 && length <= 32);

// XXX we have no indication how long the program info applies.
//     It will be canceled on channel switch, but who knows
//     what the station transmits when the next program starts.
//     (Nothing, possibly.) A timeout seems necessary. 

	switch (_class) {
	case XDS_CURRENT: /* 0 */
	case XDS_FUTURE: /* 1 */
		if (!(vbi->event_mask & (VBI_EVENT_ASPECT | VBI_EVENT_PROG_INFO)))
			return;

		pi = &vbi->prog_info[_class];
		neq = 0;

		switch (type) {
		case 1:		/* program identification number */
		{
			int month, day, hour, min;

			if (length != 4)
				return;

			month = buffer[3] & 15;
			day = buffer[2] & 31;
			hour = buffer[1] & 31;
			min = buffer[0] & 63;

			if (month == 0 || month > 12
			    || day == 0 || day > 31
			    || hour > 23 || min > 59)
				return;

			month--;
			day--;

			neq = (pi->month ^ month) | (pi->day ^ day)
				| (pi->hour ^ hour) | (pi->min ^ min);

			pi->tape_delayed = !!(buffer[3] & 0x10);

			if (neq) {
				flush_prog_info(vbi, pi, &e);

				pi->month = month;
				pi->day = day;
				pi->hour = hour;
				pi->min = min;

				pi->tape_delayed = !!(buffer[3] & 0x10);
			}

			break;
		}

		case 2:		/* program length */
		{
			int lhour, lmin, ehour = -1, emin = -1, esec = 0;

			if (length < 2 || length > 6)
				return;

			lhour = buffer[1] & 63;
			lmin = buffer[0] & 63;

			if (length >= 3) {
				ehour = buffer[3] & 63;
				emin = buffer[2] & 63;

				if (length >= 5)
					esec = buffer[4] & 63;
			}

			if (lmin > 59 || emin > 59 || esec > 59)
				return;

			xds_intfu(pi->length_hour, lhour);
			xds_intfu(pi->length_min, lmin);
			xds_intfu(pi->elapsed_hour, ehour);
			xds_intfu(pi->elapsed_min, emin);
			xds_intfu(pi->elapsed_sec, esec);

			break;
		}

		case 3:		/* program name */
			if (length < 2)
				return;

			neq = xds_strfu(pi->title, buffer, length);

			if (!neq) { /* no title change */
				if (!(vbi->cc.info_cycle[_class] & (1 << 3)))
					break; /* already reported */

				if (!(vbi->cc.info_cycle[_class] & (1 << 1))) {
					/* Second occurence without PIN */

					flush_prog_info(vbi, pi, &e);

					xds_strfu(pi->title, buffer, length);
					vbi->cc.info_cycle[_class] |= 1 << 3;
				}
			}

			break;

		case 4:		/* program type */
		{
			int neq;

			neq = (pi->type_classf != VBI_PROG_CLASSF_EIA_608);
			pi->type_classf = VBI_PROG_CLASSF_EIA_608;

			for (i = 0; i < length; i++) {
				neq |= pi->type_id[i] ^ buffer[i];
				pi->type_id[i] = buffer[i];
			}

			neq |= pi->type_id[i];
			pi->type_id[i] = 0;

			break;
		}

		case 5:		/* program rating */
		{
			vbi_rating_auth auth;
			int r, g, dlsv = 0;

			if (length != 2)
				return;

			r = buffer[0] & 7;
			g = buffer[1] & 7;

			if (buffer[0] & 0x20)
				dlsv |= VBI_RATING_D;
			if (buffer[1] & 0x08)
				dlsv |= VBI_RATING_L;
			if (buffer[1] & 0x10)
				dlsv |= VBI_RATING_S;
			if (buffer[1] & 0x20)
				dlsv |= VBI_RATING_V;

			if ((buffer[0] & 0x08) == 0) {
				if (r == 0) return;
				auth = VBI_RATING_AUTH_MPAA;
				pi->rating_dlsv = dlsv = 0;
			} else if ((buffer[0] & 0x10) == 0) {
				auth = VBI_RATING_AUTH_TV_US;
				r = g;
			} else if ((buffer[1] & 0x08) == 0) {
				if ((buffer[0] & 0x20) == 0) {
					if ((r = g) > 6) return;
					auth = VBI_RATING_AUTH_TV_CA_EN;
				} else {
					if ((r = g) > 5) return;
					auth = VBI_RATING_AUTH_TV_CA_FR;
				}
				pi->rating_dlsv = dlsv = 0;
			} else
				return;

			if ((neq = (pi->rating_auth != auth
				    || pi->rating_id != r
				    || pi->rating_dlsv != dlsv))) {
				pi->rating_auth = auth;
				pi->rating_id = r;
				pi->rating_dlsv = dlsv;
			}

			break;
		}

		case 6:		/* program audio services */
		{
			static const vbi_audio_mode mode[2][8] = {
				{
					VBI_AUDIO_MODE_UNKNOWN,
					VBI_AUDIO_MODE_MONO,
					VBI_AUDIO_MODE_SIMULATED_STEREO,
					VBI_AUDIO_MODE_STEREO,
					VBI_AUDIO_MODE_STEREO_SURROUND,
					VBI_AUDIO_MODE_DATA_SERVICE,
					VBI_AUDIO_MODE_UNKNOWN, /* "other" */
					VBI_AUDIO_MODE_NONE
				}, {
					VBI_AUDIO_MODE_UNKNOWN,
					VBI_AUDIO_MODE_MONO,
					VBI_AUDIO_MODE_VIDEO_DESCRIPTIONS,
					VBI_AUDIO_MODE_NON_PROGRAM_AUDIO,
					VBI_AUDIO_MODE_SPECIAL_EFFECTS,
					VBI_AUDIO_MODE_DATA_SERVICE,
					VBI_AUDIO_MODE_UNKNOWN, /* "other" */
					VBI_AUDIO_MODE_NONE
				}
			};

			if (length != 2)
				return;

			for (i = 0; i < 2; i++) {
				int l = (buffer[i] >> 3) & 7;
				vbi_audio_mode m = mode[i][buffer[i] & 7];
				/* should be const char *, but I got that
				   wrong and cannot change the public
				   pi->audio[].language type now. */
				unsigned char *s = ((1 << l) & 0xC1) ? NULL :
					(unsigned char *) language[l];

				if (pi->audio[i].mode != m) {
					neq = 1; pi->audio[i].mode = m;
				}
				if (pi->audio[i].language != s) {
					neq = 1; pi->audio[i].language = s;
				}
			}

			break;
		}

		case 7:		/* program caption services */
		{
			int services = 0;

			if (length > 8)
				return;

			for (i = 0; i < 8; i++)
				pi->caption_language[i] = NULL;

			for (i = 0; i < length; i++) {
				int ch = buffer[i] & 7;
				int l = (buffer[i] >> 3) & 7;
				/* should be const char *, but I got that
				   wrong and cannot change the public
				   pi->audio[].language type now. */
				unsigned char *s;

				ch = (ch & 1) * 4 + (ch >> 1);

				services |= 1 << ch;
				s = ((1 << l) & 0xC1) ? NULL :
					(unsigned char *) language[l];

				if (pi->caption_language[ch] != (unsigned char *) s) {
					neq = 1; pi->caption_language[ch] = (unsigned char *) s;
				}

				if (_class == XDS_CURRENT)
					vbi->cc.channel[ch].language =
						pi->caption_language[ch];
			}

			xds_intfu(pi->caption_services, services);

			break;
		}

		case 8:		/* copy generation management system */
			if (length != 1)
				return;

			xds_intfu(pi->cgms_a, buffer[0] & 63);

			break;

		case 9:		/* program aspect ratio */
		{
			vbi_aspect_ratio *r = &e.ev.aspect;

			if (length > 3)
				return;

			memset(&e, 0, sizeof(e));

			r->first_line = (buffer[0] & 63) + 22;
			r->last_line = 262 - (buffer[1] & 63);
			r->film_mode = 0;
			r->open_subtitles = VBI_SUBT_UNKNOWN;

			if (length >= 3 && (buffer[2] & 1))
				r->ratio = 16.0 / 9.0;
			else
				r->ratio = 1.0;

			if (memcmp(r, &vbi->prog_info[0].aspect, sizeof(*r)) != 0) {
				vbi->prog_info[0].aspect = *r;
				vbi->aspect_source = 3;

				e.type = VBI_EVENT_ASPECT;
				caption_send_event(vbi, &e);

				neq = 1;
			}

			break;
		}

		case 0x10 ... 0x17: /* program description */
		{
			int line = type & 7;

			neq = xds_strfu(pi->description[line], buffer, length);

			break;
		}

		default:
			return; /* no event */
		}

		if (0)
			printf("[type %d cycle %08x class %d neq %d]\n",
			       type, vbi->cc.info_cycle[_class], _class, neq);

		if (neq) /* first occurence of this type with this data */
			vbi->cc.info_cycle[_class] |= 1 << type;
		else if (vbi->cc.info_cycle[_class] & (1 << type)) {
			/* Second occurance of this type with same data */

			e.type = VBI_EVENT_PROG_INFO;
			e.ev.prog_info = pi;

			caption_send_event(vbi, &e);

			vbi->cc.info_cycle[_class] = 0; /* all changes reported */
		}

		break;

	case XDS_CHANNEL:
		switch (type) {
		case 1:		/* network name */
			if (xds_strfu(n->name, buffer, length)) {
				n->cycle = 1;
			} else if (n->cycle == 1) {
				signed char *s = n->name;
				uint32_t sum;

				if (n->call[0])
					s = n->call;

				for (sum = 0; *s; s++)
					sum = (sum >> 7) ^ hcrc[(sum ^ *s) & 0x7F];

				sum &= ((1UL << 31) - 1);
				sum |= 1UL << 30;

				if (n->nuid != 0)
					vbi_chsw_reset(vbi, sum);

				n->nuid = sum;

				vbi->network.type = VBI_EVENT_NETWORK;
				caption_send_event(vbi, &vbi->network);

				vbi->network.type = VBI_EVENT_NETWORK_ID;
				caption_send_event(vbi, &vbi->network);

				n->cycle = 3;
			}

			break;

		case 2:		/* network call letters */
			if (xds_strfu(n->call, buffer, length)) {
				if (n->cycle != 1) {
					n->name[0] = 0;
					n->cycle = 0;
				}
			}

			break;

		case 3:		/* channel tape delay */
			if (length != 2)
				return;

			n->tape_delay =
				(buffer[1] & 31) * 60 + (buffer[0] & 63);

			break;

		default:
			break;
		}

		break;

	case XDS_MISC:
		switch (type) {
		case 1:		/* time of day */
			if (length != 6)
				return;
			break;

		case 2:		/* impulse capture id */
			if (length != 6)
				return;
			break;

		case 3:		/* supplemental data location */
			break;

		case 4:		/* local time zone */
			if (length != 1)
				return;
			break;

		case 0x40:	/* out-of-band channel number */
			if (length != 2)
				return;
			break;

		default:
			break;
		}

		break;

	default:
		break;
	}
}

static void
xds_separator(vbi_decoder *vbi, uint8_t *buf)
{
	struct caption *cc = &vbi->cc;
	xds_sub_packet *sp = cc->curr_sp;
	int c1 = vbi_unpar8 (buf[0]);
	int c2 = vbi_unpar8 (buf[1]);
	unsigned int class, type;

	XDS_SEP_DEBUG(printf("XDS %02x %02x\n", buf[0], buf[1]));

	if ((c1 | c2) < 0) {
		XDS_SEP_DEBUG(printf("XDS tx error, discard current packet\n"));

		if (sp) {
			sp->count = 0;
			sp->chksum = 0;
			sp = NULL;
		}

		return;
	}

	switch (c1) {
	case 1 ... 14:
		class = (c1 - 1) >> 1;

		if (class > elements(cc->sub_packet)
		    || c2 > (int) elements(cc->sub_packet[0])) {
			XDS_SEP_DEBUG(printf("XDS ignore packet %d/0x%02x\n", class, c2));
			cc->curr_sp = NULL;
			return;
		}

		cc->curr_sp = sp = &cc->sub_packet[class][c2];

		if (c1 & 1) { /* start */
			sp->chksum = c1 + c2;
			sp->count = 2;
		} else if (!sp->count) {
			XDS_SEP_DEBUG(printf("XDS can't continue %d/0x%02x\n", class, c2));
			cc->curr_sp = NULL;
		}

		return;

	case 15:
		if (!sp)
			return;

		sp->chksum += c1 + c2;

		class = (sp - cc->sub_packet[0]) / elements(cc->sub_packet[0]);
		type = (sp - cc->sub_packet[0]) % elements(cc->sub_packet[0]);

		if (sp->chksum & 0x7F) {
			XDS_SEP_DEBUG(printf("XDS ignore packet %d/0x%02x, "
					     "checksum error\n", class, type));
		} else if (sp->count <= 2) {
			XDS_SEP_DEBUG(printf("XDS ignore empty packet "
					     "%d/0x%02x\n", class, type));
		} else {
			xds_decoder(vbi, class, type, sp->buffer, sp->count - 2);

			XDS_SEP_DUMP(
				for (i = 0; i < sp->count - 2; i++)
					printf("%c", _vbi_to_ascii (sp->buffer[i]));
				printf(" %d/0x%02x\n", class, type);
			)
		}

		sp->count = 0;
		sp->chksum = 0;
		cc->curr_sp = NULL;

		return;

	case 0x20 ... 0x7F:
		if (!sp)
			return;

		if (sp->count >= 32 + 2) {
			XDS_SEP_DEBUG(printf("XDS packet length overflow, discard %d/0x%02x\n",
			     	(sp - cc->sub_packet[0]) / elements(cc->sub_packet[0]),
				(sp - cc->sub_packet[0]) % elements(cc->sub_packet[0])));

			sp->count = 0;
			sp->chksum = 0;
			cc->curr_sp = NULL;
			return;
		}

		sp->buffer[sp->count - 2] = c1;
		sp->buffer[sp->count - 1] = c2;
		sp->chksum += c1 + c2;
		sp->count += 1 + !!c2;

		return;

	default:
		assert(!"reached");
	}
}

static void
itv_separator(vbi_decoder *vbi, struct caption *cc, char c)
{
	if (ITV_DEBUG(0 &&) !(vbi->event_mask & VBI_EVENT_TRIGGER))
		return;

	if (c >= 0x20) {
		if (c == '<') // s4-nbc omitted CR
			itv_separator(vbi, cc, 0);
		else if (cc->itv_count > (int) sizeof(cc->itv_buf) - 2)
			cc->itv_count = 0;

		cc->itv_buf[cc->itv_count++] = c;

		return;
	}

	cc->itv_buf[cc->itv_count] = 0;
	cc->itv_count = 0;

	ITV_DEBUG(printf("ITV: <%s>\n", cc->itv_buf));

	vbi_atvef_trigger(vbi, cc->itv_buf);
}

/*
 *  Closed Caption decoder
 */

#define ROWS			15
#define COLUMNS			34

static void
render(vbi_page *pg, int row)
{
	vbi_event event;

	if (row < 0 || pg->dirty.roll) {
		/* no particular row or not fetched
		   since last roll/clear, redraw all */
		pg->dirty.y0 = 0;
		pg->dirty.y1 = ROWS - 1;
		pg->dirty.roll = 0;
	} else {
		pg->dirty.y0 = MIN(row, pg->dirty.y0);
		pg->dirty.y1 = MAX(row, pg->dirty.y1);
	}

	event.type = VBI_EVENT_CAPTION;
	event.ev.caption.pgno = pg->pgno;

	caption_send_event(pg->vbi, &event);
}

static void
clear(vbi_page *pg)
{
	vbi_event event;

	pg->dirty.y0 = 0;
	pg->dirty.y1 = ROWS - 1;
	pg->dirty.roll = -ROWS;

	event.type = VBI_EVENT_CAPTION;
	event.ev.caption.pgno = pg->pgno;

	caption_send_event(pg->vbi, &event);
}

static void
roll_up(vbi_page *pg, int first_row, int last_row)
{
	vbi_event event;

	if (pg->dirty.roll != 0 || pg->dirty.y0 <= pg->dirty.y1) {
		/* not fetched since last update, redraw all */
		pg->dirty.roll = 0;
		pg->dirty.y0 = MIN(first_row, pg->dirty.y0);
		pg->dirty.y1 = MAX(last_row, pg->dirty.y1);
	} else {
		pg->dirty.roll = -1;
		pg->dirty.y0 = first_row;
		pg->dirty.y1 = last_row;
	}

	event.type = VBI_EVENT_CAPTION;
	event.ev.caption.pgno = pg->pgno;

	caption_send_event(pg->vbi, &event);
}

static inline void
update(cc_channel *ch)
{
	vbi_char *acp = ch->line - ch->pg[ch->hidden].text
		+ ch->pg[ch->hidden ^ 1].text;

	memcpy(acp, ch->line, sizeof(*acp) * COLUMNS);
}

static void
word_break(struct caption *cc, cc_channel *ch, int upd)
{
	cc = cc;

	/*
	 *  Add a leading and trailing space.
	 */
	if (ch->col > ch->col1) {
		vbi_char c = ch->line[ch->col1];

		if ((c.unicode & 0x7F) != 0x20
		    && ch->line[ch->col1 - 1].opacity == VBI_TRANSPARENT_SPACE) {
			c.unicode = 0x20;
			ch->line[ch->col1 - 1] = c;
		}

		c = ch->line[ch->col - 1];

		if ((c.unicode & 0x7F) != 0x20
		    && ch->line[ch->col].opacity == VBI_TRANSPARENT_SPACE) {
			c.unicode = 0x20;
			ch->line[ch->col] = c;
		}
	}

	if (!upd || ch->mode == MODE_POP_ON)
		return;

	/*
	 *  NB we render only at spaces (end of word) and
	 *  before cursor motions and mode switching, to keep the
	 *  drawing efforts (scaling etc) at a minimum. update()
	 *  for double buffering at word granularity.
	 *
	 *  XXX should not render if space follows space,
	 *  but force in long words. 
	 */

	update(ch);
	render(ch->pg + 1, ch->row);
}

static inline void
set_cursor(cc_channel *ch, int col, int row)
{
	ch->col = ch->col1 = col;
	ch->row = row;

	ch->line = ch->pg[ch->hidden].text + row * COLUMNS;
}

static void
put_char(struct caption *cc, cc_channel *ch, vbi_char c)
{
	/* c.foreground = rand() & 7; */
	/* c.background = rand() & 7; */

	if (ch->col < COLUMNS - 1)
		ch->line[ch->col++] = c;
	else {
		/* line break here? */

		ch->line[COLUMNS - 2] = c;
	}

	if ((c.unicode & 0x7F) == 0x20)
		word_break(cc, ch, 1);
}

static inline cc_channel *
switch_channel(struct caption *cc, cc_channel *ch, int new_chan)
{
	word_break(cc, ch, 1); // we leave for a number of frames

	return &cc->channel[cc->curr_chan = new_chan];
}

static void
erase_memory(struct caption *cc, cc_channel *ch, int page)
{
	vbi_page *pg = ch->pg + page;
	vbi_char *acp = pg->text;
	vbi_char c = cc->transp_space[ch >= &cc->channel[4]];
	int i;

	for (i = 0; i < COLUMNS * ROWS; acp++, i++)
		*acp = c;

	pg->dirty.y0 = 0;
	pg->dirty.y1 = ROWS - 1;
	pg->dirty.roll = ROWS;
}

static const vbi_color
palette_mapping[8] = {
	VBI_WHITE, VBI_GREEN, VBI_BLUE, VBI_CYAN,
	VBI_RED, VBI_YELLOW, VBI_MAGENTA, VBI_BLACK
};

static int
row_mapping[] = {
	10, -1,  0, 1, 2, 3,  11, 12, 13, 14,  4, 5, 6, 7, 8, 9
};

// not verified means I didn't encounter the code in a
// sample stream yet

_vbi_inline void
caption_command(vbi_decoder *vbi, struct caption *cc,
	unsigned char c1, unsigned char c2, vbi_bool field2)
{
	cc_channel *ch;
	int chan, col, i;
	int last_row;

	chan = (cc->curr_chan & 4) + field2 * 2 + ((c1 >> 3) & 1);
	ch = &cc->channel[chan];

	c1 &= 7;

	if (c2 >= 0x40) {	/* Preamble Address Codes  001 crrr  1ri xxxu */
		int row = row_mapping[(c1 << 1) + ((c2 >> 5) & 1)];

		if (row < 0 || !ch->mode)
			return;

		ch->attr.underline = c2 & 1;
		ch->attr.background = VBI_BLACK;
		ch->attr.opacity = VBI_OPAQUE;
		ch->attr.flash = FALSE;

		word_break(cc, ch, 1);

		if (ch->mode == MODE_ROLL_UP) {
			int row1 = row - ch->roll + 1;

			if (row1 < 0)
				row1 = 0;

			if (row1 != ch->row1) {
				ch->row1 = row1;
				erase_memory(cc, ch, ch->hidden);
				erase_memory(cc, ch, ch->hidden ^ 1);
			}

			set_cursor(ch, 1, ch->row1 + ch->roll - 1);
		} else
			set_cursor(ch, 1, row);

		if (c2 & 0x10) {
			col = ch->col;

			for (i = (c2 & 14) * 2; i > 0 && col < COLUMNS - 1; i--)
				ch->line[col++] = cc->transp_space[chan >> 2];

			if (col > ch->col)
				ch->col = ch->col1 = col;

			ch->attr.italic = FALSE;
			ch->attr.foreground = VBI_WHITE;
		} else {
// not verified
			c2 = (c2 >> 1) & 7;

			if (c2 < 7) {
				ch->attr.italic = FALSE;
				ch->attr.foreground = palette_mapping[c2];
			} else {
				ch->attr.italic = TRUE;
				ch->attr.foreground = VBI_WHITE;
			}
		}

		return;
	}

	switch (c1) {
	case 0:		/* Optional Attributes		001 c000  010 xxxt */
// not verified
		ch->attr.opacity = (c2 & 1) ? VBI_SEMI_TRANSPARENT : VBI_OPAQUE;
		ch->attr.background = palette_mapping[(c2 >> 1) & 7];
		return;

	case 1:
		if (c2 & 0x10) {	/* Special Characters	001 c001  011 xxxx */
// not verified
			c2 &= 15;

			if (c2 == 9) { // "transparent space"
				if (ch->col < COLUMNS - 1) {
					ch->line[ch->col++] = cc->transp_space[chan >> 2];
					ch->col1 = ch->col;
				} else
					ch->line[COLUMNS - 2] = cc->transp_space[chan >> 2];
					// XXX boxed logic?
			} else {
				vbi_char c = ch->attr;

				c.unicode = vbi_caption_unicode (0x1130 | (c2 & 15),
								 /* to_upper */ FALSE);

				put_char(cc, ch, c);
			}
		} else {		/* Midrow Codes		001 c001  010 xxxu */
// not verified
			ch->attr.flash = FALSE;
			ch->attr.underline = c2 & 1;

			c2 = (c2 >> 1) & 7;

			if (c2 < 7) {
				ch->attr.italic = FALSE;
				ch->attr.foreground = palette_mapping[c2];
			} else {
				ch->attr.italic = TRUE;
				ch->attr.foreground = VBI_WHITE;
			}
		}

		return;

	case 2:		/* Optional Extended Characters	001 c01f  01x xxxx */
	case 3:
		/* Send specs to the maintainer of this code */
		return;

	case 4:		/* Misc Control Codes		001 c10f  010 xxxx */
	case 5:		/* Misc Control Codes		001 c10f  010 xxxx */
		/* f ("field"): purpose? */

		switch (c2 & 15) {
		case 0:		/* Resume Caption Loading	001 c10f  010 0000 */
			ch = switch_channel(cc, ch, chan & 3);

			ch->mode = MODE_POP_ON;

// no?			erase_memory(cc, ch);

			return;

		/* case 4: reserved */

		case 5:		/* Roll-Up Captions		001 c10f  010 0xxx */
		case 6:
		case 7:
		{
			int roll = (c2 & 7) - 3;

			ch = switch_channel(cc, ch, chan & 3);

			if (ch->mode == MODE_ROLL_UP && ch->roll == roll)
				return;

			erase_memory(cc, ch, ch->hidden);
			erase_memory(cc, ch, ch->hidden ^ 1);

			ch->mode = MODE_ROLL_UP;
			ch->roll = roll;

			set_cursor(ch, 1, 14);

			ch->row1 = 14 - roll + 1;

			return;
		}

		case 9:		/* Resume Direct Captioning	001 c10f  010 1001 */
// not verified
			ch = switch_channel(cc, ch, chan & 3);
			ch->mode = MODE_PAINT_ON;
			return;

		case 10:	/* Text Restart			001 c10f  010 1010 */
// not verified
			ch = switch_channel(cc, ch, chan | 4);
			set_cursor(ch, 1, 0);
			return;

		case 11:	/* Resume Text Display		001 c10f  010 1011 */
			ch = switch_channel(cc, ch, chan | 4);
			return;

		case 15:	/* End Of Caption		001 c10f  010 1111 */
			ch = switch_channel(cc, ch, chan & 3);
			ch->mode = MODE_POP_ON;

			word_break(cc, ch, 1);

			ch->hidden ^= 1;

			render(ch->pg + (ch->hidden ^ 1), -1 /* ! */);

			erase_memory(cc, ch, ch->hidden); // yes?

			/*
			 *  A Preamble Address Code should follow,
			 *  reset to a known state to be safe.
			 *  Reset ch->line for new ch->hidden.
			 *  XXX row 0?
			 */
			set_cursor(ch, 1, ROWS - 1);

			return;

		case 8:		/* Flash On			001 c10f  010 1000 */
// not verified
			ch->attr.flash = TRUE;
			return;

		case 1:		/* Backspace			001 c10f  010 0001 */
// not verified
			if (ch->mode && ch->col > 1) {
				ch->line[--ch->col] = cc->transp_space[chan >> 2];

				if (ch->col < ch->col1)
					ch->col1 = ch->col;
			}

			return;

		case 13:	/* Carriage Return		001 c10f  010 1101 */
			if (ch == cc->channel + 5)
				itv_separator(vbi, cc, 0);

			if (!ch->mode)
				return;

			last_row = ch->row1 + ch->roll - 1;

			if (last_row > ROWS - 1)
				last_row = ROWS - 1;

			if (ch->row < last_row) {
				word_break(cc, ch, 1);
				set_cursor(ch, 1, ch->row + 1);
			} else {
				vbi_char *acp = &ch->pg[ch->hidden ^ (ch->mode != MODE_POP_ON)]
					.text[ch->row1 * COLUMNS];

				word_break(cc, ch, 1);
				update(ch);

				memmove(acp, acp + COLUMNS, sizeof(*acp) * (ch->roll - 1) * COLUMNS);

				for (i = 0; i <= COLUMNS; i++)
					ch->line[i] = cc->transp_space[chan >> 2];

				if (ch->mode != MODE_POP_ON) {
					update(ch);
					roll_up(ch->pg + (ch->hidden ^ 1), ch->row1, last_row);
				}

				ch->col1 = ch->col = 1;
			}

			return;

		case 4:		/* Delete To End Of Row		001 c10f  010 0100 */
// not verified
			if (!ch->mode)
				return;

			for (i = ch->col; i <= COLUMNS - 1; i++)
				ch->line[i] = cc->transp_space[chan >> 2];

			word_break(cc, ch, 0);

			if (ch->mode != MODE_POP_ON) {
				update(ch);
				render(ch->pg + (ch->hidden ^ 1), ch->row);
			}

			return;

		case 12:	/* Erase Displayed Memory	001 c10f  010 1100 */
// s1, s4: EDM always before EOC
			if (ch->mode != MODE_POP_ON)
				erase_memory(cc, ch, ch->hidden);

			erase_memory(cc, ch, ch->hidden ^ 1);
			clear(ch->pg + (ch->hidden ^ 1));

			return;

		case 14:	/* Erase Non-Displayed Memory	001 c10f  010 1110 */
// not verified
			if (ch->mode == MODE_POP_ON)
				erase_memory(cc, ch, ch->hidden);

			return;
		}

		return;

	/* case 6: reserved */

	case 7:
		if (!ch->mode)
			return;

		switch (c2) {
		case 0x21 ... 0x23:	/* Misc Control Codes, Tabs	001 c111  010 00xx */
// not verified
			col = ch->col;

			for (i = c2 & 3; i > 0 && col < COLUMNS - 1; i--)
				ch->line[col++] = cc->transp_space[chan >> 2];

			if (col > ch->col)
				ch->col = ch->col1 = col;

			return;

		case 0x2D:		/* Optional Attributes		001 c111  010 11xx */
// not verified
			ch->attr.opacity = VBI_TRANSPARENT_FULL;
			break;

		case 0x2E:		/* Optional Attributes		001 c111  010 11xx */
		case 0x2F:
// not verified
			ch->attr.foreground = VBI_BLACK;
			ch->attr.underline = c2 & 1;
			break;

		default:
			return;
		}

		/* Optional Attributes, backspace magic */

		if (ch->col > 1 && (ch->line[ch->col - 1].unicode & 0x7F) == 0x20) {
			vbi_char c = ch->attr;

			c.unicode = 0x0020;
			ch->line[ch->col - 1] = c;
		}
	}
}

/**
 * @internal
 * @param vbi Initialized vbi decoding context.
 * @param line ITU-R line number this data originated from.
 * @param buf Two bytes.
 * 
 * Decode two bytes of Closed Caption data (Caption, XDS, ITV),
 * updating the decoder state accordingly. May send events.
 */
void
vbi_decode_caption(vbi_decoder *vbi, int line, uint8_t *buf)
{
	struct caption *cc = &vbi->cc;
	char c1 = buf[0] & 0x7F;
	int field2 = 1, i;

	pthread_mutex_lock(&cc->mutex);

	switch (line) {
	case 21:	/* NTSC */
	case 22:	/* PAL */
		field2 = 0;
		break;

	case 335:	/* PAL, hardly XDS */
		break;

	case 284:	/* NTSC */
		CC_DUMP(
			putchar(_vbi_to_ascii (buf[0]));
			putchar(_vbi_to_ascii (buf[1]));
			fflush(stdout);
		)

		if (vbi_unpar8 (buf[0]) >= 0) {
			if (c1 == 0) {
				goto finish;
			} else if (c1 <= 0x0F) {
				xds_separator(vbi, buf);
				cc->xds = (c1 != XDS_END);
				goto finish;
			} else if (c1 <= 0x1F) {
				cc->xds = FALSE;
			} else if (cc->xds) {
				xds_separator(vbi, buf);
				goto finish;
			}
		} else if (cc->xds) {
			xds_separator(vbi, buf);
			goto finish;
		}
 
		break;

	default:
		goto finish;
	}

	if (vbi_unpar8 (buf[0]) < 0) {
		c1 = 127;
		buf[0] = c1; /* traditional 'bad' glyph, ccfont has */
		buf[1] = c1; /*  room, design a special glyph? */
	}

	CC_DUMP(
		putchar(_vbi_to_ascii (buf[0]));
		putchar(_vbi_to_ascii (buf[1]));
		fflush(stdout);
	)

	switch (c1) {
		cc_channel *ch;
		vbi_char c;

	case 0x01 ... 0x0F:
		if (!field2)
			cc->last[0] = 0;
		break; /* XDS field 1?? */

	case 0x10 ... 0x1F:
		if (vbi_unpar8 (buf[1]) >= 0) {
			if (!field2
			    && buf[0] == cc->last[0]
			    && buf[1] == cc->last[1]) {
				/* cmd repetition F1: already executed */
				cc->last[0] = 0; /* one rep */
				break;
			}

			caption_command(vbi, cc, c1, buf[1] & 0x7F, field2);

			if (!field2) {
				cc->last[0] = buf[0];
				cc->last[1] = buf[1];
			}
		} else if (!field2)
			cc->last[0] = 0;

		break;

	default:
		CC_TEXT_DUMP(
			putchar(_vbi_to_ascii (buf[0]));
			putchar(_vbi_to_ascii (buf[1]));
			fflush(stdout);
		)

		ch = &cc->channel[(cc->curr_chan & 5) + field2 * 2];

		if (buf[0] == 0x80 && buf[1] == 0x80) {
			if (ch->mode) {
				if (ch->nul_ct == 2)
					word_break(cc, ch, 1);
				ch->nul_ct += 2;
			}

			break;
		}

		if (!field2)
			cc->last[0] = 0;

		ch->nul_ct = 0;

		if (!ch->mode)
			break;

		ch->time = vbi->time; /* activity measure */

		c = ch->attr;

		for (i = 0; i < 2; i++) {
			char ci = vbi_unpar8 (buf[i]) & 0x7F; /* 127 if bad */

			if (ci <= 0x1F) /* 0x00 no char, 0x01 ... 0x1F invalid */
				continue;

			if (ch == cc->channel + 5) // 'T2'
				itv_separator(vbi, cc, ci);

			c.unicode = vbi_caption_unicode(ci, /* to_upper */ FALSE);

			put_char(cc, ch, c);
		}
	}

 finish:
	pthread_mutex_unlock(&cc->mutex);
}

/**
 * @internal
 * @param vbi Initialized vbi decoding context. 
 * 
 * This function must be called after desynchronisation
 * has been detected (i. e. vbi data has been lost)
 * to reset the Closed Caption decoder.
 */
void
vbi_caption_desync(vbi_decoder *vbi)
{
	struct caption *cc = &vbi->cc;

	/* cc->curr_chan = 8; *//* garbage */

	/* cc->xds = FALSE; */

	if (cc->curr_sp) {
		memset(cc->curr_sp, 0, sizeof(*(cc->curr_sp)));
		cc->curr_sp = NULL;
	}

	cc->itv_count = 0;
}

/**
 * @internal
 * @param vbi Initialized vbi decoding context.
 * 
 * This function must be called after a channel switch,
 * to reset the Closed Caption decoder.
 */
void
vbi_caption_channel_switched(vbi_decoder *vbi)
{
	struct caption *cc = &vbi->cc;
	cc_channel *ch;
	int i;

	for (i = 0; i < 9; i++) {
		ch = &cc->channel[i];

		if (i < 4) {
			ch->mode = MODE_NONE; // MODE_ROLL_UP;
			ch->row = ROWS - 1;
			ch->row1 = ROWS - 3;
			ch->roll = 3;
		} else {
			ch->mode = MODE_TEXT;
			ch->row1 = ch->row = 0;
			ch->roll = ROWS;
		}

		ch->attr.opacity = VBI_OPAQUE;
		ch->attr.foreground = VBI_WHITE;
		ch->attr.background = VBI_BLACK;

		set_cursor(ch, 1, ch->row);

		ch->time = 0.0;

		ch->hidden = 0;

		ch->pg[0].dirty.y0 = 0;
		ch->pg[0].dirty.y1 = ROWS - 1;
		ch->pg[0].dirty.roll = 0;

		erase_memory(cc, ch, 0);

		memcpy(&ch->pg[1], &ch->pg[0], sizeof(ch->pg[1]));
	}

	cc->xds = FALSE;

	memset(&cc->sub_packet, 0, sizeof(cc->sub_packet));

	cc->info_cycle[0] = 0;
	cc->info_cycle[1] = 0;

	vbi_caption_desync(vbi);
}

static vbi_rgba
default_color_map[8] = {
	VBI_RGBA(0x00, 0x00, 0x00), VBI_RGBA(0xFF, 0x00, 0x00),
	VBI_RGBA(0x00, 0xFF, 0x00), VBI_RGBA(0xFF, 0xFF, 0x00),	
	VBI_RGBA(0x00, 0x00, 0xFF), VBI_RGBA(0xFF, 0x00, 0xFF),
	VBI_RGBA(0x00, 0xFF, 0xFF), VBI_RGBA(0xFF, 0xFF, 0xFF)
};

/**
 * @internal
 * @param vbi Initialized vbi decoding context.
 * 
 * After the client changed text brightness and saturation
 * this function adjusts the Closed Caption color palette.
 */
void
vbi_caption_color_level(vbi_decoder *vbi)
{
	int i;

	vbi_transp_colormap(vbi, vbi->cc.channel[0].pg[0].color_map,
			    default_color_map, 8);

	for (i = 1; i < 16; i++)
		memcpy(vbi->cc.channel[i >> 1].pg[i & 1].color_map,
		       vbi->cc.channel[0].pg[0].color_map,
		       sizeof(default_color_map));
}

/**
 * @internal
 * @param vbi VBI decoding context.
 * 
 * This function is called during @a vbi destruction
 * to destroy Closed Caption subset of @a vbi.
 */
void
vbi_caption_destroy(vbi_decoder *vbi)
{
	pthread_mutex_destroy(&vbi->cc.mutex);
}

/**
 * @internal
 * @param vbi VBI decoding context.
 * 
 * This function is called during @a vbi initialization
 * to initialize the Closed Caption subset of @a vbi.
 */
void
vbi_caption_init(vbi_decoder *vbi)
{
	struct caption *cc = &vbi->cc;
	cc_channel *ch;
	int i;

	memset(cc, 0, sizeof(struct caption));

	pthread_mutex_init(&cc->mutex, NULL);

	for (i = 0; i < 9; i++) {
		ch = &cc->channel[i];

		ch->pg[0].vbi = vbi;

		ch->pg[0].pgno = i + 1;
		ch->pg[0].subno = 0;

		ch->pg[0].rows = ROWS;
		ch->pg[0].columns = COLUMNS;

		ch->pg[0].screen_color = 0;
		ch->pg[0].screen_opacity = (i < 4) ? VBI_TRANSPARENT_SPACE : VBI_OPAQUE;

		ch->pg[0].font[0] = vbi_font_descriptors; /* English */
		ch->pg[0].font[1] = vbi_font_descriptors;

		memcpy(&ch->pg[1], &ch->pg[0], sizeof(ch->pg[1]));
	}

       	for (i = 0; i < 2; i++) {
		cc->transp_space[i].foreground = VBI_WHITE;
		cc->transp_space[i].background = VBI_BLACK;
		cc->transp_space[i].unicode = 0x0020;
	}

	cc->transp_space[0].opacity = VBI_TRANSPARENT_SPACE;
	cc->transp_space[1].opacity = VBI_OPAQUE;

	vbi_caption_channel_switched(vbi);

	vbi_caption_color_level(vbi);
}

/**
 * @param vbi Initialized vbi decoding context.
 * @param pg Place to store the formatted page.
 * @param pgno Page number 1 ... 8 of the page to fetch, see vbi_pgno.
 * @param reset @c TRUE resets the vbi_page dirty fields in cache after
 *   fetching. Pass @c FALSE only if you plan to call this function again
 *   to update other displays.
 * 
 * Fetches a Closed Caption page designated by @a pgno from the cache,
 * formats and stores it in @a pg. CC pages are transmitted basically in
 * two modes: at once and character by character ("roll-up" mode).
 * Either way you get a snapshot of the page as it should appear on
 * screen at present. With vbi_event_handler_add() you can request a
 * @c VBI_EVENT_CAPTION event to be notified about pending changes
 * (in case of "roll-up" mode that is with each new word received)
 * and the vbi_page->dirty fields will mark the lines actually in
 * need of updates, to speed up rendering.
 * 
 * Although safe to do, this function is not supposed to be
 * called from an event handler, since rendering may block decoding
 * for extended periods of time.
 * 
 * @return
 * @c FALSE if some error occured.
 */
vbi_bool
vbi_fetch_cc_page(vbi_decoder *vbi, vbi_page *pg, vbi_pgno pgno, vbi_bool reset)
{
	cc_channel *ch = vbi->cc.channel + ((pgno - 1) & 7);
	vbi_page *spg;

	reset = reset;

	if (pgno < 1 || pgno > 8)
		return FALSE;

	pthread_mutex_lock(&vbi->cc.mutex);

	spg = ch->pg + (ch->hidden ^ 1);

	memcpy(pg, spg, sizeof(*pg)); /* shortcut? */

	spg->dirty.y0 = ROWS;
	spg->dirty.y1 = -1;
	spg->dirty.roll = 0;

	pthread_mutex_unlock(&vbi->cc.mutex);

	return 1;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
