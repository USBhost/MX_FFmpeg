/*
 *  libzvbi -- Teletext page cache search functions
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
 *  Copyright (C) 2000, 2001 Iñaki G. Etxebarria
 *
 *  Originally based on AleVT 1.5.1 by Edgar Toernig
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

/* $Id: search.c,v 1.15 2008/02/19 00:35:21 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lang.h"
#include "cache.h"
#include "search.h"
#include "ure.h"
#include "vbi.h"
#include "teletext_decoder.h"

/**
 * @addtogroup Search
 * @ingroup Cache
 * @brief Search the Teletext page cache.
 */

#if defined(HAVE_GLIBC21) || defined(HAVE_LIBUNICODE)

struct vbi_search {
	vbi_decoder *		vbi;

	int			start_pgno;
	int			start_subno;
	int			stop_pgno[2];
	int			stop_subno[2];
	int			row[2], col[2];

	int			dir;

	vbi_bool		(* progress)(vbi_page *pg);

	vbi_page		pg;

	ure_buffer_t		ub;
	ure_dfa_t		ud;
	ucs2_t			haystack[25 * (40 + 1) + 1];
};

#define SEPARATOR 0x000A

#define FIRST_ROW 1
#define LAST_ROW 24

static void
highlight(struct vbi_search *s, cache_page *vtp,
	  ucs2_t *first, long ms, long me)
{
	vbi_page *pg = &s->pg;
	ucs2_t *hp;
	int i, j;

	hp = s->haystack;

	s->start_pgno = vtp->pgno;
	s->start_subno = vtp->subno;
	s->row[0] = LAST_ROW + 1;
	s->col[0] = 0;

	for (i = FIRST_ROW; i < LAST_ROW; i++) {
		vbi_char *acp = &pg->text[i * pg->columns];

		for (j = 0; j < 40; acp++, j++) {
			int offset = hp - first;
 
			if (offset >= me) {
				s->row[0] = i;
				s->col[0] = j;
				return;
			}

			if (offset < ms) {
				if (j == 39) {
					s->row[1] = i + 1;
					s->col[1] = 0;
				} else {
					s->row[1] = i;
					s->col[1] = j + 1;
				}
			}

			switch (acp->size) {
			case VBI_DOUBLE_SIZE:
				if (offset >= ms) {
					acp[pg->columns].foreground = 32 + VBI_BLACK;
					acp[pg->columns].background = 32 + VBI_YELLOW;
					acp[pg->columns + 1].foreground = 32 + VBI_BLACK;
					acp[pg->columns + 1].background = 32 + VBI_YELLOW;
				}

				/* fall through */

			case VBI_DOUBLE_WIDTH:
				if (offset >= ms) {
					acp[0].foreground = 32 + VBI_BLACK;
					acp[0].background = 32 + VBI_YELLOW;
					acp[1].foreground = 32 + VBI_BLACK;
					acp[1].background = 32 + VBI_YELLOW;
				}

				hp++;
				acp++;
				j++;

				break;

			case VBI_DOUBLE_HEIGHT:
				if (offset >= ms) {
					acp[pg->columns].foreground = 32 + VBI_BLACK;
					acp[pg->columns].background = 32 + VBI_YELLOW;
				}

				/* fall through */

			case VBI_NORMAL_SIZE:
				if (offset >= ms) {
					acp[0].foreground = 32 + VBI_BLACK;
					acp[0].background = 32 + VBI_YELLOW;
				}

				hp++;
				break;

			default:
				/* skipped */
				/* hp++; */
				break;
			}
		}

		hp++;
	}
}

static int
search_page_fwd(cache_page *vtp, vbi_bool wrapped, void *p)
{
	vbi_search *s = p;
	vbi_char *acp;
	int row, _this, start, stop;
	ucs2_t *hp, *first;
	unsigned long ms, me;
	int flags, i, j;

	_this = (vtp->pgno << 16) + vtp->subno;
	start = (s->start_pgno << 16) + s->start_subno;
	stop  = (s->stop_pgno[0] << 16) + s->stop_subno[0];

	if (start >= stop) {
		if (wrapped && _this >= stop)
			return -1; /* all done, abort */
	} else if (_this < start || _this >= stop)
		return -1; /* all done, abort */

	if (vtp->function != PAGE_FUNCTION_LOP)
		return 0; /* try next */

	if (!vbi_format_vt_page(s->vbi, &s->pg, vtp, s->vbi->vt.max_level, 25, 1))
		return -3; /* formatting error, abort */

	if (s->progress)
		if (!s->progress(&s->pg)) {
			if (_this != start) {
				s->start_pgno = vtp->pgno;
				s->start_subno = vtp->subno;
				s->row[0] = FIRST_ROW;
				s->row[1] = LAST_ROW + 1;
				s->col[0] = s->col[1] = 0;
			}

			return -2; /* canceled */
		}

	/* To Unicode */

	hp = s->haystack;
	first = hp;
	row = (_this == start) ? s->row[0] : -1;
	flags = 0;

	if (row > LAST_ROW)
		return 0; /* try next page */

	for (i = FIRST_ROW; i < LAST_ROW; i++) {
		acp = &s->pg.text[i * s->pg.columns];

		for (j = 0; j < 40; acp++, j++) {
			if (i == row && j <= s->col[0])
				first = hp;

			if (acp->size == VBI_DOUBLE_WIDTH
			    || acp->size == VBI_DOUBLE_SIZE) {
				/* "ZZAAPPZILLA" -> "ZAPZILLA" */
				acp++; /* skip left half */
				j++;
			} else if (acp->size > VBI_DOUBLE_SIZE) {
				/* skip */
				/* *hp++ = 0x0020; */
				continue;
			}

			*hp++ = acp->unicode;
			flags = URE_NOTBOL;
		}

		*hp++ = SEPARATOR;
		flags = 0;
	}

	/* Search */

	if (first >= hp)
		return 0; /* try next page */
/*
fprintf(stderr, "exec: %x/%x; start %d,%d; %c%c%c...\n",
	vtp->pgno, vtp->subno,
	s->row[0], s->col[0],
	_vbi_to_ascii (first[0]),
	_vbi_to_ascii (first[1]),
	_vbi_to_ascii (first[2])
);
*/
	if (!ure_exec(s->ud, flags, first, hp - first, &ms, &me))
		return 0; /* try next page */

	highlight(s, vtp, first, ms, me);

	return 1; /* success, abort */
}

static int
search_page_rev(cache_page *vtp, vbi_bool wrapped, void *p)
{
	vbi_search *s = p;
	vbi_char *acp;
	int row, this, start, stop;
	unsigned long ms, me;
	ucs2_t *hp;
	int flags, i, j;

	this  = (vtp->pgno << 16) + vtp->subno;
	start = (s->start_pgno << 16) + s->start_subno;
	stop  = (s->stop_pgno[1] << 16) + s->stop_subno[1];

	if (start <= stop) {
		if (wrapped && this <= stop)
			return -1; /* all done, abort */
	} else if (this > start || this <= stop)
		return -1; /* all done, abort */

	if (vtp->function != PAGE_FUNCTION_LOP)
		return 0; /* try next page */

	if (!vbi_format_vt_page(s->vbi, &s->pg, vtp, s->vbi->vt.max_level, 25, 1))
		return -3; /* formatting error, abort */

	if (s->progress)
		if (!s->progress(&s->pg)) {
			if (this != start) {
				s->start_pgno = vtp->pgno;
				s->start_subno = vtp->subno;
				s->row[0] = FIRST_ROW;
				s->row[1] = LAST_ROW + 1;
				s->col[0] = s->col[1] = 0;
			}

			return -2; /* canceled */
		}

	/* To Unicode */

	hp = s->haystack;
	row = (this == start) ? s->row[1] : 100;
	flags = 0;

	if (row < FIRST_ROW)
		goto break2;

	for (i = FIRST_ROW; i < LAST_ROW; i++) {
		acp = &s->pg.text[i * s->pg.columns];

		for (j = 0; j < 40; acp++, j++) {
			if (i == row && j >= s->col[1])
				goto break2;

			if (acp->size == VBI_DOUBLE_WIDTH
			    || acp->size == VBI_DOUBLE_SIZE) {
				/* "ZZAAPPZILLA" -> "ZAPZILLA" */
				acp++; /* skip left half */
				j++;
			} else if (acp->size > VBI_DOUBLE_SIZE) {
				/* skip */
				/* *hp++ = 0x0020; */
				continue;
			}

			*hp++ = acp->unicode;
			flags = URE_NOTEOL;
		}

		*hp++ = SEPARATOR;
		flags = 0;
	}
break2:

	if (hp <= s->haystack)
		return 0; /* try next page */

	/* Search */

	ms = me = 0;

	for (i = 0; s->haystack + me < hp; i++) {
		unsigned long ms1, me1;
/*
fprintf(stderr, "exec: %x/%x; %d, %d; '%c%c%c...'\n",
	vtp->pgno, vtp->subno, i, me,
	_vbi_to_ascii (s->haystack[me + 0]),
	_vbi_to_ascii (s->haystack[me + 1]),
	_vbi_to_ascii (s->haystack[me + 2])
);
*/
		if (!ure_exec(s->ud, (me > 0) ? (flags | URE_NOTBOL) : flags,
		    s->haystack + me, hp - s->haystack - me, &ms1, &me1))
			break;

		ms = me + ms1;
		me = me + me1;
	}

	if (i == 0)
		return 0; /* try next page */

	highlight(s, vtp, s->haystack, ms, me);

	return 1; /* success, abort */
}

/**
 * @param search vbi_search context.
 * 
 * Delete the search context created by vbi_search_new().
 */
void
vbi_search_delete(vbi_search *search)
{
	if (!search)
		return;

	if (search->ud)
		ure_dfa_free(search->ud);

	if (search->ub)
		ure_buffer_free(search->ub);

	free(search);
}

static size_t
ucs2_strlen(const void *string)
{
	const ucs2_t *p = (const ucs2_t *) string;
	size_t i = 0;

	if (!string)
		return 0;

	for (i = 0; *p; i++)
		p++;

	return i;
}

/**
 * @param vbi Initialized vbi decoding context.
 * @param pgno 
 * @param subno Page and subpage number of the first (forward) or
 *   last (backward) page to visit. Optional @c VBI_ANY_SUBNO. 
 * @param pattern The Unicode (UCS-2, <em>not</em> UTF-16) search
 *   pattern, a 0-terminated string.
 * @param casefold Boolean, search case insensitive.
 * @param regexp Boolean, the search pattern is a regular expression.
 * @param progress A function called for each page scanned, can be
 *   \c NULL. Shall return @c FALSE to abort the search. @a pg is valid
 *   for display (e. g. @a pg->pgno), do <em>not</em> call
 *   vbi_unref_page() or modify this page.
 * 
 * Allocate a vbi_search context and prepare for searching
 * the Teletext page cache. The context must be freed with
 * vbi_search_delete().
 * 
 * Regular expression searching supports the standard set
 * of operators and constants, with these extensions:
 *
 * <table>
 * <tr><td>\x....</td><td>hexadecimal number of up to 4 digits</td></tr>
 * <tr><td>\X....</td><td>hexadecimal number of up to 4 digits</td></tr>
 * <tr><td>\u....</td><td>hexadecimal number of up to 4 digits</td></tr>
 * <tr><td>\U....</td><td>hexadecimal number of up to 4 digits</td></tr>
 * <tr><td>:title:</td><td>Unicode specific character class</td></tr>
 * <tr><td>:gfx:</td><td>Teletext G1 or G3 graphics</td></tr>
 * <tr><td>:drcs:</td><td>Teletext DRCS</td></tr>
 * <tr><td>\pN1,N2,...,Nn</td><td>Character properties class</td></tr>
 * <tr><td>\PN1,N2,...,Nn</td><td>Negated character properties class</td></tr>
 * </table>
 *
 * <table>
 * <tr><td><b>N</b></td><td><b>Property</b></td></tr>
 * <tr><td>1</td><td>alphanumeric</td></tr>
 * <tr><td>2</td><td>alpha</td></tr>
 * <tr><td>3</td><td>control</td></tr>
 * <tr><td>4</td><td>digit</td></tr>
 * <tr><td>5</td><td>graphical</td></tr>
 * <tr><td>6</td><td>lowercase</td></tr>
 * <tr><td>7</td><td>printable</td></tr>
 * <tr><td>8</td><td>punctuation</td></tr>
 * <tr><td>9</td><td>space</td></tr>
 * <tr><td>10</td><td>uppercase</td></tr>
 * <tr><td>11</td><td>hex digit</td></tr>
 * <tr><td>12</td><td>title</td></tr>
 * <tr><td>13</td><td>defined</td></tr>
 * <tr><td>14</td><td>wide</td></tr>
 * <tr><td>15</td><td>nonspacing</td></tr>
 * <tr><td>16</td><td>Teletext G1 or G3 graphics</td></tr>
 * <tr><td>17</td><td>Teletext DRCS</td></tr>
 * </table>
 *
 * Character classes can contain literals, constants, and character
 * property classes. Example: [abc\U10A\p1,3,4]. Note double height
 * and size characters will match twice, on the upper and lower row,
 * and double width and size characters count as one (reducing the
 * line width) so one can find combinations of normal and enlarged
 * characters.
 *
 * @bug
 * In a multithreaded application the data service decoder may receive
 * and cache new pages during a search session. When these page numbers
 * have been visited already the pages are not searched. At a channel
 * switch (and in future at any time) pages can be removed from cache.
 * All this has yet to be addressed.
 *
 * @return
 * A vbi_search context or @c NULL on error.
 */
vbi_search *
vbi_search_new(vbi_decoder *vbi,
	       vbi_pgno pgno, vbi_subno subno,
	       uint16_t *pattern,
	       vbi_bool casefold, vbi_bool regexp,
	       int (* progress)(vbi_page *pg))
{
	vbi_search *s;
	ucs2_t *esc_pat = NULL;
	int i, j, pat_len = ucs2_strlen(pattern);

	if (pat_len <= 0)
		return NULL;

	if (!(s = calloc(1, sizeof(*s))))
		return NULL;

	if (!regexp) {
		if (!(esc_pat = malloc(sizeof(ucs2_t) * pat_len * 2))) {
			free(s);
			return NULL;
		}

		for (i = j = 0; i < pat_len; i++) {
			if (strchr("!\"#$%&()*+,-./:;=?@[\\]^_{|}~", pattern[i]))
				esc_pat[j++] = '\\';
			esc_pat[j++] = pattern[i];
		}

		pattern = esc_pat;
		pat_len = j;
	}

	if (!(s->ub = ure_buffer_create()))
		goto abort;

	if (!(s->ud = ure_compile(pattern, pat_len, casefold, s->ub))) {
abort:
		vbi_search_delete(s);

		if (!regexp)
			free(esc_pat);

		return NULL;
	}

	if (!regexp)
		free(esc_pat);

	s->stop_pgno[0] = pgno;
	s->stop_subno[0] = (subno == VBI_ANY_SUBNO) ? 0 : subno;

	if (subno <= 0) {
		s->stop_pgno[1] = (pgno <= 0x100) ? 0x8FF : pgno - 1;
		s->stop_subno[1] = 0x3F7E;
	} else {
		s->stop_pgno[1] = pgno;

		if ((subno & 0x7F) == 0)
			s->stop_subno[1] = (subno - 0x100) | 0x7E;
		else
			s->stop_subno[1] = subno - 1;
	}

	s->vbi = vbi;
	s->progress = progress;

	return s;
}

/**
 * @param search Initialized search context.
 * @param pg Place to store the formatted (as with vbi_fetch_vt_page())
 *   Teletext page containing the found pattern. Do <em>not</em>
 *   call vbi_unref_page() for this page. Also the page must not
 *   be modified. See vbi_search_status for semantics.
 * @param dir Search direction +1 forward or -1 backward.
 *
 * Find the next occurence of the search pattern.
 *
 * @return
 * vbi_search_status.
 */
/* XXX fix return type */
int
vbi_search_next(vbi_search *search, vbi_page **pg, int dir)
{
	*pg = NULL;
	dir = (dir > 0) ? +1 : -1;

	if (!search->dir) {
		search->dir = dir;

		if (dir > 0) {
			search->start_pgno = search->stop_pgno[0];
			search->start_subno = search->stop_subno[0];
		} else {
			search->start_pgno = search->stop_pgno[1];
			search->start_subno = search->stop_subno[1];
		}

		search->row[0] = FIRST_ROW;
		search->row[1] = LAST_ROW + 1;
		search->col[0] = search->col[1] = 0;
	}
#if 1 /* should switch to a 'two frontiers meet' model, but ok for now */
	else if (dir != search->dir) {
		search->dir = dir;

		search->stop_pgno[0] = search->start_pgno;
		search->stop_subno[0] = (search->start_subno == VBI_ANY_SUBNO) ?
			0 : search->start_subno;
		search->stop_pgno[1] = search->start_pgno;
		search->stop_subno[1] = search->start_subno;
	}
#endif
	switch (_vbi_cache_foreach_page (search->vbi->ca,
					 search->vbi->cn,
					 search->start_pgno,
					 search->start_subno,
					 dir,
					 (dir > 0) ? search_page_fwd
					 : search_page_rev,
					 /* user_data */ search)) {
	case 1:
		*pg = &search->pg;
		return VBI_SEARCH_SUCCESS;

	case 0:
		return VBI_SEARCH_CACHE_EMPTY;

	case -1:
		search->dir = 0;
		return VBI_SEARCH_NOT_FOUND;

	case -2:
		return VBI_SEARCH_CANCELED;

	default:
		break;
	}

	return VBI_SEARCH_ERROR;
}

#else /* !HAVE_GLIBC21 && !HAVE_LIBUNICODE */

vbi_search *
vbi_search_new(vbi_decoder *vbi,
	       vbi_pgno pgno, vbi_subno subno,
	       uint16_t *pattern,
	       vbi_bool casefold, vbi_bool regexp,
	       int (* progress)(vbi_page *pg))
{
	return NULL;
}

int
vbi_search_next(vbi_search *search, vbi_page **pg, int dir)
{
	return VBI_SEARCH_ERROR;
}

void
vbi_search_delete(vbi_search *search)
{
}

#endif /* !HAVE_GLIBC21 && !HAVE_LIBUNICODE */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
