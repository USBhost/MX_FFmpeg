/*
 *  libzvbi -- Triggers
 *
 *  Implementation of EACEM TP 14-99-16 "Data Broadcasting", rev 0.8;
 *  ATVEF "Enhanced Content Specification", v1.1 (www.atvef.com);
 *  and http://developer.webtv.net
 *
 *  Copyright (C) 2001 Michael H. Schimek
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

/* $Id: trigger.c,v 1.14 2008/02/19 00:35:22 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <math.h>

#include "misc.h"
#include "trigger.h"
#include "tables.h"
#include "vbi.h"

struct vbi_trigger {
	vbi_trigger *		next;
	vbi_link		link;
	double			fire;
	unsigned char		view;
	vbi_bool		_delete;
};

static vbi_bool
verify_checksum(const char *s, int count, int checksum)
{
	register unsigned long sum2, sum1 = checksum;

	for (; count > 1; count -= 2) {
		sum1 += (unsigned long) *s++ << 8;
		sum1 += (unsigned long) *s++;
	}

	sum2 = sum1;

	/*
	 *  There seems to be confusion about how left-over
	 *  bytes shall be added, the example C code in
	 *  RFC 1071 subclause 4.1 contradicts the definition
	 *  in subclause 1 (zero pad to 16 bit).
	 */
	if (count > 0) {
		sum1 += (unsigned long) *s << 8; /* correct */
		sum2 += (unsigned long) *s << 0; /* wrong */
	}

	while (sum1 >= (1 << 16))
		sum1 = (sum1 & 0xFFFFUL) + (sum1 >> 16);

	while (sum2 >= (1 << 16))
		sum2 = (sum2 & 0xFFFFUL) + (sum2 >> 16);

	return sum1 == 0xFFFFUL || sum2 == 0xFFFFUL;
}

static int
parse_dec(const char *s, int digits)
{
	int n = 0;

	while (digits-- > 0) {
		if (!isdigit(*s))
			return -1;

		n = n * 10 + *s++ - '0';
	}

	return n;
}

static int
parse_hex(const char *s, int digits)
{
	int n = 0;

	while (digits-- > 0) {
		if (!isxdigit(*s))
			return -1;

		n = n * 16
			+ (*s & 15) + ((*s > '9') ? 9 : 0);
		s++;
	}

	return n;
}

/*
 *  XXX http://developer.webtv.net/itv/tvlink/main.htm adds more ...???
 */
static time_t
parse_date(const char *s)
{
	struct tm tm;

	memset(&tm, 0, sizeof(tm));

	if ((tm.tm_year = parse_dec(s + 0, 4)) < 0
	    || (tm.tm_mon = parse_dec(s + 4, 2)) < 0
	    || (tm.tm_mday = parse_dec(s + 6, 2)) < 0)
		return (time_t) -1;
	if (s[8]) {
		if (s[8] != 'T'
		    || (tm.tm_hour = parse_dec(s + 9, 2)) < 0
		    || (tm.tm_min = parse_dec(s + 11, 2)) < 0)
			return (time_t) -1;
		if (s[13] &&
		    (tm.tm_sec = parse_dec(s + 13, 2)) < 0)
			return (time_t) -1;
	}

	tm.tm_year -= 1900;

	return mktime(&tm);
}

static int
parse_time(const char *s)
{
	int seconds, frames = 0;

	seconds = strtoul(s, (char **) &s, 10);

	if (*s)
		if (*s != 'F'
		    || (frames = parse_dec(s + 1, 2)) < 0)
			return -1;

	return seconds * 25 + frames;
}

static int
parse_bool(char *s)
{
	return (strcmp(s, "1") == 0) || (strcasecmp(s, "true") == 0);
}

static int
keyword(char *s, const char **keywords, int num)
{
	int i;

	if (!s[0])
		return -1;
	else if (!s[1]) {
		for (i = 0; i < num; i++)
			if (tolower(s[0]) == keywords[i][0])
				return i;
	} else
		for (i = 0; i < num; i++)
			if (strcasecmp(s, keywords[i]) == 0)
				return i;
	return -1;
}

static char *
parse_eacem(vbi_trigger *t, char *s1, unsigned int nuid, double now)
{
	static const char *attributes[] = {
		"active", "countdown", "delete", "expires",
		"name", "priority", "script"
	};
	char buf[256];
	char *s, *e, *d, *dx;
	int active, countdown;
	int c;

	t->link.url[0]    = 0;
	t->link.name[0]   = 0;
	t->link.script[0] = 0;
	t->link.priority  = 9;
	t->link.expires   = 0.0;
	t->link.autoload  = FALSE;
	t->_delete	  = FALSE;
	t->fire		  = now;
	t->view		  = 'w';
	t->link.itv_type  = 0;
	active		  = INT_MAX;

	for (s = s1;; s++) {
		e = s;

		c = *s;

		if (c == '<') {
			if (s != s1)
				return NULL;

			d = (char *) t->link.url;
			dx = d + sizeof(t->link.url) - 2;

			for (s++; (c = *s) != '>'; s++)
				if (c && d < dx)
					*d++ = c;
				else
					return NULL;
			*d++ = 0;
		} else

		if (c == '[' || c == '(') {
			int delim = (c == '[') ? ']' : ')';
			char *attr, *text = "";
			vbi_bool quote = FALSE;

			attr = d = buf;
			dx = d + sizeof(buf) - 2;

			for (s++; c = *s, c != ':' && c != delim; s++) {
				if (c == '%') {
					if ((c = parse_hex(s + 1, 2)) < 0x20)
						return NULL;
					s += 2;
				}

				if (c && d < dx)
					*d++ = c;
				else
					return NULL;
			}

			*d++ = 0;

			if (!attr[0])
				return NULL;

			s++;

			if (c != ':') {
				if (!verify_checksum(s1, e - s1,
						     strtoul(attr, NULL, 16))) {
					if (0)
						fprintf(stderr, "checksum mismatch\n");
					return NULL;
				}

				break;
			}

			for (text = d; quote || (c = *s) != delim; s++) {
				if (c == '"')
					quote ^= TRUE;
				else if (c == '%') {
					if ((c = parse_hex(s + 1, 2)) < 0x20)
						return NULL;
					s += 2;
				}

				if (c && d < dx)
					*d++ = c;
				else
					return NULL;
			}

			*d++ = 0;

			switch (keyword(attr, attributes,
					sizeof(attributes) / sizeof(attributes[0]))) {
			case 0: /* active */
				active = parse_time(text);
			       	if (active < 0)
					return NULL;
				break;

			case 1: /* countdown */
				countdown = parse_time(text);
				if (countdown < 0)
					return NULL;
				t->fire = now + countdown / 25.0;
				break;

			case 2: /* delete */
				t->_delete = TRUE;
				break;

                        case 3: /* expires */
				t->link.expires = parse_date(text);
				if (t->link.expires == (time_t) -1)
					return NULL;
				break;

			case 4: /* name */
				strlcpy((char *) t->link.name, text,
					sizeof(t->link.name) - 1);
				t->link.name[sizeof(t->link.name) - 1] = 0;
				break;

                        case 5: /* priority */
				t->link.priority = strtoul(text, NULL, 10);
				if (t->link.priority > 9)
					return NULL;
				break;

			case 6: /* script */
				strlcpy((char *) t->link.script, text,
					sizeof(t->link.script) - 1);
				t->link.script[sizeof(t->link.script) - 1] = 0;
				break;

			default:
				/* ignored */
				break;
			}
		} else if (c == 0)
			break;
		else
			return NULL;
	}

	if (t->link.expires <= 0.0)
		t->link.expires = t->fire + active / 25.0;
		/* EACEM eqv PAL/SECAM land, 25 fps */

	if (!t->link.url)
		return NULL;

	if (strncmp((char *) t->link.url, "http://", 7) == 0)
		t->link.type = VBI_LINK_HTTP;
	else if (strncmp((char *) t->link.url, "lid://", 6) == 0)
		t->link.type = VBI_LINK_LID;
	else if (strncmp((char *) t->link.url, "tw://", 5) == 0)
		t->link.type = VBI_LINK_TELEWEB;
	else if (strncmp((char *) t->link.url, "dummy", 5) == 0) {
		t->link.pgno = parse_dec((char *) t->link.url + 5, 2);
		if (!t->link.name || t->link.pgno < 0 || t->link.url[7])
			return NULL;
		t->link.type = VBI_LINK_MESSAGE;
	} else if (strncmp((char *) t->link.url, "ttx://", 6) == 0) {
		const struct vbi_cni_entry *p;
		int cni;

		cni = parse_hex((char *) t->link.url + 6, 4);
		if (cni < 0 || t->link.url[10] != '/')
			return NULL;

		t->link.pgno = parse_hex((char *) t->link.url + 11, 3);
		if (t->link.pgno < 0x100 || t->link.url[14] != '/')
			return NULL;

		t->link.subno = parse_hex((char *) t->link.url + 15, 4);
		if (t->link.subno < 0)
			return NULL;

		if (cni > 0) {
			for (p = vbi_cni_table; p->name; p++)
				if (p->cni1 == cni || p->cni4 == cni)
					break;
			if (!p->name)
				return NULL;
			t->link.nuid = p->id;
		} else
			t->link.nuid = nuid;

		t->link.type = VBI_LINK_PAGE;
	} else
		return NULL;

	return s;
}

static char *
parse_atvef(vbi_trigger *t, char *s1, double now)
{
	static const char *attributes[] = {
		"auto", "expires", "name", "script",
		"type" /* "t" */, "time", "tve",
		"tve-level", "view" /* "v" */
	};
	static const char *type_attrs[] = {
		"program", "network", "station", "sponsor",
		"operator", "tve"
	};
	char buf[256];
	char *s, *e, *d, *dx;
	int c;

	t->link.url[0]    = 0;
	t->link.name[0]   = 0;
	t->link.script[0] = 0;
	t->link.priority  = 9;
	t->fire      = now;
	t->link.expires   = 0.0;
	t->link.autoload  = FALSE;
	t->_delete    = FALSE;
	t->view      = 'w';
	t->link.itv_type  = 0;

	for (s = s1;; s++) {
		e = s;
		c = *s;

		if (c == '<') {
			if (s != s1)
				return NULL;

			d = (char *) t->link.url;
			dx = (char *) d + sizeof(t->link.url) - 1;

			for (s++; (c = *s) != '>'; s++)
				if (c && d < dx)
					*d++ = c;
				else
					return NULL;
			*d++ = 0;
		} else

		if (c == '[') {
			char *attr, *text = "";
			vbi_bool quote = FALSE;

			attr = d = buf;
			dx = d + sizeof(buf) - 2;

			for (s++; c = *s, c != ':' && c != ']'; s++) {
				if (c == '%') {
					if ((c = parse_hex(s + 1, 2)) < 0x20)
						return NULL;
					s += 2;
				}

				if (c && d < dx)
					*d++ = c;
				else
					return NULL;
			}

			*d++ = 0;

			if (!attr[0])
				return NULL;

			s++; 

			if (c != ':') {
				unsigned int i;

				for (i = 1; i < (sizeof(type_attrs) / sizeof(type_attrs[0]) - 1); i++)
					if (strcasecmp(type_attrs[i], attr) == 0)
						break;

				if (i < (sizeof(type_attrs) / sizeof(type_attrs[0]) - 1)) {
					t->link.itv_type = i + 1;
					continue;
				}

				if (!verify_checksum(s1, e - s1,
						     strtoul(attr, NULL, 16)))
					return NULL;

				break;
			}

			for (text = d; quote || (c = *s) != ']'; s++) {
				if (c == '"')
					quote ^= TRUE;
				else if (c == '%') {
					if ((c = parse_hex(s + 1, 2)) < 0x20)
						return NULL;
					s += 2;
				}

				if (c && d < dx)
					*d++ = c;
				else
					return NULL;
			}

			*d++ = 0;

			switch (keyword(attr, attributes,
					sizeof(attributes) / sizeof(attributes[0]))) {
			case 0: /* auto */
				t->link.autoload = parse_bool(text);
				break;

			case 1: /* expires */
				t->link.expires = parse_date(text);
				if (t->link.expires < 0.0)
					return NULL;
				break;

			case 2: /* name */
				strlcpy((char *) t->link.name, text,
					sizeof(t->link.name) - 1);
				t->link.name[sizeof(t->link.name) - 1] = 0;
				break;

			case 3: /* script */
				strlcpy((char *) t->link.script, text,
					sizeof(t->link.script));
				t->link.script[sizeof(t->link.script) - 1] = 0;
				break;

			case 4: /* type */
				t->link.itv_type = keyword(text, type_attrs,
					sizeof(type_attrs) / sizeof(type_attrs[0])) + 1;
				break;

			case 5: /* time */
				t->fire = parse_date(text);
				if (t->fire < 0.0)
					return NULL;
				break;

			case 6: /* tve */
			case 7: /* tve-level */
				/* ignored */
				break;

			case 8: /* view (tve == v) */
				t->view = *text;
				break;

			default:
				/* ignored */
				break;
			}

		} else if (c == 0)
			break;
		else
			return NULL;
	}

	if (!t->link.url)
		return NULL;

	if (strncmp((char *) t->link.url, "http://", 7) == 0)
		t->link.type = VBI_LINK_HTTP;
	else if (strncmp((char *) t->link.url, "lid://", 6) == 0)
		t->link.type = VBI_LINK_LID;
	else
		return NULL;

	return s;
}

/**
 * vbi_trigger_flush:
 * @param vbi Initialized vbi decoding context.
 * 
 * Discard all triggers stored to fire at a later time. This function
 * must be called before deleting the @vbi context.
 **/
void
vbi_trigger_flush(vbi_decoder *vbi)
{
	vbi_trigger *t;

	while ((t = vbi->triggers)) {
		vbi->triggers = t->next;
		free(t);
	}
}

/**
 * vbi_deferred_trigger:
 * @param vbi Initialized vbi decoding context.
 * 
 * This function must be called at regular intervals,
 * preferably once per video frame, to fire (send a trigger
 * event) previously received triggers which reached their
 * fire time. 'Now' is supposed to be @vbi->time.
 **/
void
vbi_deferred_trigger(vbi_decoder *vbi)
{
	vbi_trigger *t, **tp;

	for (tp = &vbi->triggers; (t = *tp); tp = &t->next)
		if (t->fire <= vbi->time) {
			vbi_event ev;

			ev.type = VBI_EVENT_TRIGGER;
			ev.ev.trigger = &t->link;
			vbi_send_event(vbi, &ev);

			*tp = t->next;
			free(t);
		} else
			tp = &t->next;
}

static void
add_trigger(vbi_decoder *vbi, vbi_trigger *a)
{
	vbi_trigger *t;

	if (a->_delete) {
		vbi_trigger **tp;

		for (tp = &vbi->triggers; (t = *tp); tp = &t->next)
			if (strcmp((char *) a->link.url, (char *) t->link.url) == 0
			    && fabs(a->fire - t->fire) < 0.1) {
				*tp = t->next;
				free(t);
			} else
				tp = &t->next;

		return;
	}

	for (t = vbi->triggers; t; t = t->next)
		if (strcmp((char *) a->link.url, (char *) t->link.url) == 0
		    && fabs(a->fire - t->fire) < 0.1)
			return;

	if (a->fire <= vbi->time) {
		vbi_event ev;

		ev.type = VBI_EVENT_TRIGGER;
		ev.ev.trigger = &a->link;
		vbi_send_event(vbi, &ev);

		return;
	}

	if (!(t = malloc(sizeof(*t))))
		return;

	t->next = vbi->triggers;
	vbi->triggers = t;
}

/**
 * vbi_atvef_trigger:
 * @param vbi Initialized vbi decoding context.
 * @param s EACEM string (supposedly ASCII).
 * 
 * Parse an EACEM string and add it to the trigger list (where it
 * may fire immediately or at a later time).
 **/
void
vbi_eacem_trigger(vbi_decoder *vbi, unsigned char *s)
{
	vbi_trigger t;
	char *r;

	r = (char *) s;

	while ((r = parse_eacem(&t, r,
				vbi->network.ev.network.nuid, vbi->time))) {
		if (0)
			fprintf(stderr, "At %f eacem link type %d '%s', <%s> '%s', "
				"%08x %03x.%04x, exp %f, pri %d %d, auto %d; "
				"fire %f view %d del %d\n",
				vbi->time,
				t.link.type, t.link.name, t.link.url, t.link.script,
				t.link.nuid, t.link.pgno, t.link.subno,
				t.link.expires, t.link.priority, t.link.itv_type,
				t.link.autoload, t.fire, t.view, t._delete);

		t.link.eacem = TRUE;

		if (t.link.type == VBI_LINK_LID
		    || t.link.type == VBI_LINK_TELEWEB)
			return;

		add_trigger(vbi, &t);
	}
}

/**
 * vbi_atvef_trigger:
 * @param vbi Initialized vbi context.
 * @param s ATVEF string (ASCII).
 * 
 * Parse an ATVEF string and add it to the trigger list (where it
 * may fire immediately or at a later time).
 **/
void
vbi_atvef_trigger(vbi_decoder *vbi, unsigned char *s)
{
	vbi_trigger t;

	if (parse_atvef(&t, (char *) s, vbi->time)) {
		if (0)
			fprintf(stderr, "At %f atvef link type %d '%s', <%s> '%s', "
				"%08x %03x.%04x, exp %f, pri %d %d, auto %d; "
				"fire %f view %d del %d\n",
				vbi->time,
				t.link.type, t.link.name, t.link.url, t.link.script,
				t.link.nuid, t.link.pgno, t.link.subno,
				t.link.expires, t.link.priority, t.link.itv_type,
				t.link.autoload, t.fire, t.view, t._delete);

		t.link.eacem = FALSE;

		if (t.view == 't' /* WebTV */
		    || strchr((char *) t.link.url, '*') /* trigger matching */
		    || t.link.type == VBI_LINK_LID)
			return;

		add_trigger(vbi, &t);
	}
}


/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
