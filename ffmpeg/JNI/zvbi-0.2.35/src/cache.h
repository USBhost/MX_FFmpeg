/*
 *  libzvbi -- Teletext cache
 *
 *  Copyright (C) 2001, 2002, 2003, 2004, 2007 Michael H. Schimek
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

/* $Id: cache.h,v 1.12 2008/03/05 13:33:11 mschimek Exp $ */

#ifndef __ZVBI_CACHE_H__
#define __ZVBI_CACHE_H__

#include <stdarg.h>
#include "version.h"
#if 2 == VBI_VERSION_MINOR
#  include "lang.h"
typedef struct _vbi_cache vbi_cache;
#elif 3 == VBI_VERSION_MINOR
#  include <sys/time.h>		/* struct timeval */
#  include "network.h"
#  include "page.h"
#  include "top_title.h"
#else
#  error VBI_VERSION_MINOR == ?
#endif
#include "event.h"

VBI_BEGIN_DECLS

#if 3 == VBI_VERSION_MINOR

/**
 * @brief Teletext page type.
 *
 * Some networks provide information to classify Teletext pages.
 * This can be used for example to automatically find program
 * schedule and subtitle pages. See also vbi_cache_get_ttx_page_stat().
 *
 * These codes are defined in EN 300 706 Annex J, Table J.1. Note
 * the libzvbi Teletext decoder collects information about Teletext
 * pages from all received data, not just MIP tables.
 *
 * For compatibility with future extensions applications should ignore
 * codes which are not listed here.
 */
typedef enum {
	/**
	 * The network does not transmit this page (at this time).
	 * This information can be useful to skip pages when the
	 * user browses the service. It comes from a received page
	 * inventory table, libzvbi does not monitor actual
	 * transmission times to conjecture the probability of a
	 * retransmission.
	 */
	VBI_NO_PAGE		= 0x00,

	/**
	 * A normal Teletext page which is intended for display
	 * to the user.
	 *
	 * The libzvbi Teletext decoder does not return the MIP
	 * codes 0x02 to 0x51. The number of subpages is available
	 * through vbi_cache_get_ttx_page_stat() instead.
	 */
	VBI_NORMAL_PAGE		= 0x01,

	/**
	 * Teletext pages are divided into eight "magazines" with
	 * page numbers 1xx to 8xx. Tables Of Pages (TOP) navigation
	 * topically divides them further into groups and blocks.
	 *
	 * This is a normal page which is also the first page of a
	 * TOP block.
	 *
	 * You can determine the title of one TOP block or group with
	 * vbi_cache_get_top_title() and get a list of all blocks and
	 * groups with the vbi_cache_get_top_titles() function.
	 *
	 * You can add a row with links to the closest TOP block and
	 * group pages to a Teletext page with the VBI_NAVIGATION
	 * option to vbi_cache_get_teletext_page().
	 *
	 * This is a libzvbi internal code.
	 */
	VBI_TOP_BLOCK		= 0x60,

	/**
	 * A normal page which is also the first page of a
	 * TOP group. This is a libzvbi internal code.
	 */
	VBI_TOP_GROUP		= 0x61,

	/**
	 * This page contains news in brief and can be displayed
	 * like subtitles. This is a libzvbi internal code.
	 */
	VBI_NEWSFLASH_PAGE	= 0x62,

	/**
	 * This page contains subtitles, however no subtitles may be
	 * transmitted right now, or the page may just display
	 * something like "currently no subtitles".
	 *
	 * The libzvbi Teletext decoder does not return the MIP
	 * codes 0x71 to 0x77. The subtitle language is available
	 * through vbi_cache_get_ttx_page_stat() instead.
	 */
	VBI_SUBTITLE_PAGE	= 0x70,

	/**
	 * A normal page which contains information about subtitles,
	 * for example available languages and the respective page
	 * number.
	 */
	VBI_SUBTITLE_INDEX	= 0x78,

	/**
	 * This page contains the local time encoded as a BCD value
	 * in the subpage number (e.g. 0x2359), usually a normal
	 * page displaying the current time in different time zones.
	 */
	VBI_CLOCK_PAGE		= 0x79,

	/**
	 * A normal page containing information concerning the
	 * content of the current TV programmes so that the viewer
	 * can be warned of the suitability of the contents for
	 * general viewing.  
	 */
	VBI_PROGR_WARNING	= 0x7A,

	/**
	 * A normal page containing information associated with
	 * the current TV programme.
	 *
	 * The libzvbi Teletext decoder does not return the
	 * MIP code 0x7B. The number of subpages is available
	 * through vbi_cache_get_ttx_page_stat() instead.
	 */
	VBI_CURRENT_PROGR	= 0x7C,

	/**
	 * A normal page containing information about the current
	 * and the following programme. Usually this information
	 * is brief and can be displayed like subtitles.
	 */
	VBI_NOW_AND_NEXT	= 0x7D,

	/**
	 * A normal page containing information about the current
	 * TV programmes.
	 */
	VBI_PROGR_INDEX		= 0x7F,

	VBI_NOT_PUBLIC		= 0x80,

	/**
	 * A normal page containing a programme schedule.
	 *
	 * The libzvbi Teletext decoder does not return the
	 * MIP codes 0x82 to 0xD1. The number of subpages is
	 * available through vbi_cache_get_ttx_page_stat()
	 * instead.
	 */
	VBI_PROGR_SCHEDULE	= 0x81,

	/**
	 * The page contains conditional access data and is not
	 * displayable. Currently libzvbi does not evaluate this
	 * data.
	 */
	VBI_CA_DATA		= 0xE0,

	/**
	 * The page contains binary coded electronic programme guide
	 * data and is not displayable. Currently libzvbi does not
	 * evaluate this data. An open source NexTView EPG decoder
	 * is available at http://nxtvepg.sourceforge.net
	 */
	VBI_PFC_EPG_DATA	= 0xE3,

	/**
	 * The page contains binary data, is not displayable.
	 * See also vbi_pfc_demux_new().
	 */
	VBI_PFC_DATA		= 0xE4,

	/**
	 * The page contains character bitmaps, is not displayable.
	 * Libzvbi uses information like this to optimize the caching
	 * of pages. It is not particularly useful for applications.
	 */
	VBI_DRCS_PAGE		= 0xE5,

	/**
	 * The page contains additional data for Level 1.5/2.5/3.5
	 * pages, is not displayable.
	 */
	VBI_POP_PAGE		= 0xE6,

	/**
	 * The page contains additional data, for example a page
	 * inventory table, and is not displayable.
	 */
	VBI_SYSTEM_PAGE		= 0xE7,

	/**
	 * Codes used for the pages associated with packet X/25
	 * keyword searching.
	 */
	VBI_KEYWORD_SEARCH_LIST = 0xF9,

	/**
	 * The page contains a binary coded, programme related HTTP
	 * or FTP URL, e-mail address, or Teletext page number. It
	 * is not displayable. You can get this information from the
	 * Teletext decoder as a VBI_EVENT_TRIGGER event.
	 */
	VBI_TRIGGER_DATA	= 0xFC,

	/**
	 * The page contains binary data about the allocation of
	 * channels in a cable system, presumably network names
	 * and frequencies. Currently libzvbi does not evaluate
	 * this data because the document describing the format
	 * of this page is not freely available.
	 */
	VBI_ACI_PAGE		= 0xFD,

	/**
	 * The page contains MPT, AIT or MPT-EX tables for TOP
	 * navigation, is not displayable. You can access the
	 * information contained in these tables with the
	 * vbi_cache_get_ttx_page_stat() and
	 * vbi_cache_get_top_titles() functions.
	 */
	VBI_TOP_PAGE		= 0xFE,

	/** The page type is unknown, a libzvbi internal code. */
	VBI_UNKNOWN_PAGE	= 0xFF
} vbi_page_type;

/* in packet.c */
extern const char *
vbi_page_type_name		(vbi_page_type		type)
  _vbi_const;

#endif /* 3 == VBI_VERSION_MINOR */

#if 3 == VBI_VERSION_MINOR

/**
 * @brief Meta data and statistical info about a cached Teletext page.
 *
 * This data comes from received Teletext page inventory tables or
 * was collected when the page itself was received and decoded. Note
 * the page may not be cached yet, or not anymore due to limited
 * memory, and the network may or may not transmit it again.
 */
typedef struct {
	vbi_pgno			_reserved0;

	/** Teletext page type. */
	vbi_page_type			page_type;

	/**
	 * Primary character set used on the page, can hint at the
	 * language of subtitles. (The information is not reliable
	 * because one character set may cover multiple languages
	 * and some networks transmit incorrect character set codes).
	 * @c NULL if unknown.
	 */
	const vbi_ttx_charset *		ttx_charset;

	/** Expected number of subpages: 0 or 2 ... 79. */
	unsigned int			subpages;

	/** Lowest subno actually received yet. */
	vbi_subno			subno_min;

	/** Highest subno actually received yet. */
	vbi_subno			subno_max;

	struct timeval			_reserved1;

	void *				_reserved2[2];
	int				_reserved3[2];
} vbi_ttx_page_stat;

extern void
vbi_ttx_page_stat_destroy	(vbi_ttx_page_stat *	ps)
  _vbi_nonnull ((1));
extern void
vbi_ttx_page_stat_init		(vbi_ttx_page_stat *	ps)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_cache_get_ttx_page_stat	(vbi_cache *		ca,
				 vbi_ttx_page_stat *	ps,
				 const vbi_network *	nk,
				 vbi_pgno		pgno)
  _vbi_nonnull ((1, 2, 3));

#else /* 3 != VBI_VERSION_MINOR */
#  define vbi_page_type_name(x) "unknown"
#endif

#if 3 == VBI_VERSION_MINOR

extern vbi_bool
vbi_cache_get_top_title		(vbi_cache *		ca,
				 vbi_top_title *	tt,
				 const vbi_network *	nk,
				 vbi_pgno		pgno,
				 vbi_subno		subno)
  _vbi_nonnull ((1, 2, 3));
extern vbi_top_title *
vbi_cache_get_top_titles	(vbi_cache *		ca,
				 const vbi_network *	nk,
				 unsigned int *		n_elements)
  _vbi_nonnull ((1, 2, 3));

#endif /* 3 == VBI_VERSION_MINOR */

#if 3 == VBI_VERSION_MINOR

/**
 * @brief Values for the vbi_format_option @c VBI_WST_LEVEL.
 */
typedef enum {
	/**
	 * Level 1 - Basic Teletext pages. All pages can be formatted
	 * like this since networks transmit Level 1 data as fallback
	 * for older Teletext decoders.
	 */
	VBI_WST_LEVEL_1,

	/**
	 * Level 1.5 - Additional national and graphics characters.
	 */
	VBI_WST_LEVEL_1p5,

	/**
	 * Level 2.5 - Additional text styles, more colors, DRCS, side
	 * panels. You should enable Level 2.5 only if you can render
	 * and/or export such pages.
	 */
	VBI_WST_LEVEL_2p5,

	/**
	 * Level 3.5 - Multicolor DRCS, proportional script.
	 */
	VBI_WST_LEVEL_3p5
} vbi_wst_level;

#endif /* 3 == VBI_VERSION_MINOR */

#if 3 == VBI_VERSION_MINOR

/**
 * @brief Page formatting options.
 *
 * Pass formatting options as a vector of key/value pairs. The last
 * option key must be @c VBI_END (zero).
 *
 * function (foo, bar,
 *           VBI_PADDING, TRUE,
 *           VBI_DEFAULT_CHARSET_0, 15,
 *           VBI_HEADER_ONLY, FALSE,
 *           VBI_END);
 */
/* We use random numbering assuming the variadic functions using these
   values stop reading when they encounter an unknown number (VBI_END is
   zero). Parameters shall be only int or pointer (vbi_bool is an int,
   enum is an int) for proper automatic casts. */
typedef enum {
	/**
	 * Format only the first row of a Teletext page. This is useful
	 * to display the currently received page number while waiting
	 * for a page requested by the user, and to update the clock in
	 * the upper right corner.
	 *
	 * Parameter: vbi_bool, default FALSE (format all rows).
	 */
	VBI_HEADER_ONLY = 0x37138F00,

	/**
	 * Due to the spacing attributes on Teletext Level 1.0 pages
	 * column 0 of a page often contains all black spaces, unlike
	 * column 39. This option adds a 41st black column (or another
	 * color if column 0 is not black) to give a more balanced
	 * view.
	 *
	 * Parameter: vbi_bool, default FALSE.
	 */
	VBI_PADDING,

	/**
	 * Not implemented yet. Format a Teletext page with side
	 * panels, a Level 2.5 feature. This may add extra columns at
	 * the left and/or right, for a total page width of 40 to 64
	 * columns. This option takes precedence over VBI_PADDING if
	 * side panels have been transmitted.
	 *
	 * Parameter: vbi_bool, default FALSE.
	 */
	VBI_PANELS,

	/**
	 * Enable TOP or FLOF navigation in row 25.
	 * - 0 disable
	 * - 1 FLOF or TOP style 1
	 * - 2 FLOF or TOP style 2
	 *
	 * Parameter: int, default 0.
	 */
	VBI_NAVIGATION,

	/**
	 * Scan the page for page numbers, URLs, e-mail addresses
	 * etc. and turn them into hyperlinks.
	 *
	 * Parameter: vbi_bool, default FALSE.
	 */
	VBI_HYPERLINKS,

	/**
	 * Scan the page for PDC Method A/B preselection data
	 * and create a PDC table and links.
	 *
	 * Parameter: vbi_bool, default FALSE.
	 */
	VBI_PDC_LINKS,

	/**
	 * Format the page at the given Teletext implementation level.
	 * This option is useful if the page cannot be properly
	 * displayed at the highest levels, for example because the
	 * output device supports only the eight basic colors of
	 * Level 1.0.
	 *
	 * Parameter: vbi_wst_level, default VBI_WST_LEVEL_1.
	 */
	VBI_WST_LEVEL,

	/**
	 * The default character set code. Codes transmitted by the
	 * network take precedence. When the network transmits only the
	 * three last significant bits, this value provides the higher
	 * bits, or if this yields no valid code all bits of the
	 * character set code.
	 *
	 * Parameter: vbi_ttx_charset_code, default 0 (English).
	 */
	VBI_DEFAULT_CHARSET_0,

	/**
	 * Teletext pages can use two character sets simultaniously,
	 * selectable by a control code embedded in the page. This
	 * option defines a default character set code like
	 * VBI_DEFAULT_CHARSET_0, but for the secondary character set.
	 */
	VBI_DEFAULT_CHARSET_1,

	/**
	 * Overrides the primary character set code of a page. This
	 * code takes precedence over VBI_DEFAULT_CHARSET_0 and any
	 * code transmitted by the network. The option is useful if the
	 * network transmits incorrect character set codes.
	 *
	 * Parameter: vbi_ttx_charset_code, default is the
	 * transmitted character set code.
	 */
	VBI_OVERRIDE_CHARSET_0,

	/**
	 * Same as VBI_OVERRIDE_CHARSET_0, but for the secondary
	 * character set.
	 */
	VBI_OVERRIDE_CHARSET_1,

	/**
	 * This option selects a default foreground color when
	 * formatting a Closed Caption page. A color value
	 * transmitted by the network takes precedence.
	 *
	 * Parameter: vbi_color, default VBI_WHITE as specified in
	 * EIA 608-B.
	 */
	VBI_DEFAULT_FOREGROUND,

	/**
	 * This option selects a default background color when
	 * formatting a Closed Caption page. A color value
	 * transmitted by the network takes precedence.
	 *
	 * Parameter: vbi_color, default VBI_BLACK as specified in
	 * EIA 608-B.
	 */
	VBI_DEFAULT_BACKGROUND,

	VBI_ROW_CHANGE
} vbi_format_option;

/* in teletext.c */
extern vbi_page *
vbi_cache_get_teletext_page_va_list
				(vbi_cache *		ca,
				 const vbi_network *	nk,
				 vbi_pgno		pgno,
				 vbi_subno		subno,
				 va_list		format_options)
  _vbi_nonnull ((1, 2));
/* in teletext.c */
extern vbi_page *
vbi_cache_get_teletext_page	(vbi_cache *		ca,
				 const vbi_network *	nk,
				 vbi_pgno		pgno,
				 vbi_subno		subno,
				 ...)
  _vbi_nonnull ((1, 2));

#endif /* 3 == VBI_VERSION_MINOR */

#if 3 == VBI_VERSION_MINOR

/* There are no cache events in libzvbi 0.2. */

/* in cache.c */
extern void
vbi_cache_remove_event_handler	(vbi_cache *		ca,
				 vbi_event_cb *	callback,
				 void *			user_data)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_cache_add_event_handler	(vbi_cache *		ca,
				 vbi_event_mask	event_mask,
				 vbi_event_cb *	callback,
				 void *			user_data)
  _vbi_nonnull ((1));

#endif /* 3 == VBI_VERSION_MINOR */

#if 3 == VBI_VERSION_MINOR

/* These functions are not available in libzvbi 0.2 because
   reference counting is broken and cannot be fixed without
   breaking binary compatibility. */

extern vbi_network *
vbi_cache_get_networks		(vbi_cache *		ca,
				 unsigned int *		n_elements)
  _vbi_nonnull ((1, 2));
extern void
vbi_cache_set_memory_limit	(vbi_cache *		ca,
				 unsigned long		limit)
  _vbi_nonnull ((1));
extern void
vbi_cache_set_network_limit	(vbi_cache *		ca,
				 unsigned int		limit)
  _vbi_nonnull ((1));

#endif /* 3 == VBI_VERSION_MINOR */

#if 2 == VBI_VERSION_MINOR

/* Public */

/**
 * @addtogroup Cache
 * @{
 */
extern void             vbi_unref_page(vbi_page *pg);
extern int              vbi_is_cached(vbi_decoder *, int pgno, int subno);
extern int              vbi_cache_hi_subno(vbi_decoder *vbi, int pgno);
/** @} */

/* Private */

#endif /* 2 == VBI_VERSION_MINOR */

/* These functions are used only internally in libzvbi 0.2. */

extern void
vbi_cache_unref			(vbi_cache *		ca);
extern vbi_cache *
vbi_cache_ref			(vbi_cache *		ca)
  _vbi_nonnull ((1));
extern void
vbi_cache_delete		(vbi_cache *		ca);
extern vbi_cache *
vbi_cache_new			(void)
  _vbi_alloc;

VBI_END_DECLS

#endif /* __ZVBI_CACHE_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
