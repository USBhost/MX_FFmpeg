/*
 *  libzvbi -- VBI decoding library
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
 *  Copyright (C) 2000, 2001 Iñaki García Etxebarria
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

/* $Id: vbi.h,v 1.17 2009/03/04 21:47:56 mschimek Exp $ */

#ifndef VBI_H
#define VBI_H

#include <pthread.h>

#include "teletext_decoder.h"
#include "cc.h"
#include "decoder.h"
#include "event.h"
#include "cache-priv.h"
#include "trigger.h"
#include "pfc_demux.h"
#include "pdc.h"

struct event_handler {
	struct event_handler *	next;
	int			event_mask;
	vbi_event_handler	handler;
	void *			user_data;
};

struct vbi_decoder {
	double			time;

	pthread_mutex_t		chswcd_mutex;
        int                     chswcd;

	vbi_event		network;

	vbi_trigger *		triggers;

	pthread_mutex_t         prog_info_mutex;
	vbi_program_info        prog_info[2];
	int                     aspect_source;

	int			brightness;
	int			contrast;

	struct teletext		vt;
	struct caption		cc;

	cache_network *		cn;

	vbi_cache *		ca;

	vbi_pfc_demux		epg_pc[2];

	/* preliminary */
	int			pageref;

	pthread_mutex_t		event_mutex;
	int			event_mask;
	struct event_handler *	handlers;
	struct event_handler *	next_handler;

	unsigned char		wss_last[2];
	int			wss_rep_ct;
	double			wss_time;

	vbi_program_id		vps_pid;
};

#ifndef VBI_DECODER
#define VBI_DECODER
/**
 * @ingroup HiDec
 * @brief Opaque VBI data service decoder object.
 *
 * Allocate with vbi_decoder_new().
 */
typedef struct vbi_decoder vbi_decoder;
#endif

/*
 *  vbi_page_type, the page identification codes,
 *  are derived from the MIP code scheme:
 *
 *  MIP 0x01 ... 0x51 -> 0x01 (subpages)
 *  MIP 0x70 ... 0x77 -> 0x70 (language)
 *  MIP 0x7B -> 0x7C (subpages)
 *  MIP 0x7E -> 0x7F (subpages)
 *  MIP 0x81 ... 0xD1 -> 0x81 (subpages)
 *  MIP reserved -> 0xFF (VBI_UNKNOWN_PAGE)
 *
 *  MIP 0x80 and 0xE0 ... 0xFE are not returned by
 *  vbi_classify_page().
 *
 *  TOP BTT mapping:
 *
 *  BTT 0 -> 0x00 (VBI_NOPAGE)
 *  BTT 1 -> 0x70 (VBI_SUBTITLE_PAGE)
 *  BTT 2 ... 3 -> 0x7F (VBI_PROGR_INDEX)
 *  BTT 4 ... 5 -> 0xFA (VBI_TOP_BLOCK -> VBI_NORMAL_PAGE) 
 *  BTT 6 ... 7 -> 0xFB (VBI_TOP_GROUP -> VBI_NORMAL_PAGE)
 *  BTT 8 ... 11 -> 0x01 (VBI_NORMAL_PAGE)
 *  BTT 12 ... 15 -> 0xFF (VBI_UNKNOWN_PAGE)
 *
 *  0xFA, 0xFB, 0xFF are reserved MIP codes used
 *  by libzvbi to identify TOP and unknown pages.
 */

/* Public */

/**
 * @ingroup HiDec
 * @brief Page classification.
 *
 * See vbi_classify_page().
 */
typedef enum {
	VBI_NO_PAGE = 0x00,
	VBI_NORMAL_PAGE = 0x01,
	VBI_SUBTITLE_PAGE = 0x70,
	VBI_SUBTITLE_INDEX = 0x78,
	VBI_NONSTD_SUBPAGES = 0x79,
	VBI_PROGR_WARNING = 0x7A,
	VBI_CURRENT_PROGR = 0x7C,
	VBI_NOW_AND_NEXT = 0x7D,
	VBI_PROGR_INDEX = 0x7F,
	VBI_PROGR_SCHEDULE = 0x81,
	VBI_UNKNOWN_PAGE = 0xFF
/* Private */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
	, VBI_NOT_PUBLIC = 0x80,
	VBI_CA_DATA_BROADCAST =	0xE0,
	VBI_EPG_DATA = 0xE3,
	VBI_SYSTEM_PAGE = 0xE7,
	VBI_DISP_SYSTEM_PAGE = 0xF7,
	VBI_KEYWORD_SEARCH_LIST = 0xF9,
	VBI_TOP_BLOCK = 0xFA,
	VBI_TOP_GROUP = 0xFB,
	VBI_TRIGGER_DATA = 0xFC,
	VBI_ACI = 0xFD,
	VBI_TOP_PAGE = 0xFE
#endif
/* Public */
} vbi_page_type;

/**
 * @addtogroup Render
 * @{
 */
extern void		vbi_set_brightness(vbi_decoder *vbi, int brightness);
extern void		vbi_set_contrast(vbi_decoder *vbi, int contrast);
/** @} */

/**
 * @addtogroup Service
 * @{
 */
extern vbi_decoder *	vbi_decoder_new(void);
extern void		vbi_decoder_delete(vbi_decoder *vbi);
extern void		vbi_decode(vbi_decoder *vbi, vbi_sliced *sliced,
				   int lines, double timestamp);
extern void             vbi_channel_switched(vbi_decoder *vbi, vbi_nuid nuid);
extern vbi_page_type	vbi_classify_page(vbi_decoder *vbi, vbi_pgno pgno,
					  vbi_subno *subno, char **language);
extern void		vbi_version(unsigned int *major, unsigned int *minor, unsigned int *micro);
extern void
vbi_set_log_fn			(vbi_log_mask		mask,
				 vbi_log_fn *		log_fn,
				 void *			user_data);
/** @} */

/* Private */

extern pthread_once_t	vbi_init_once;
extern void		vbi_init(void);

extern void		vbi_transp_colormap(vbi_decoder *vbi, vbi_rgba *d, vbi_rgba *s, int entries);
extern void             vbi_chsw_reset(vbi_decoder *vbi, vbi_nuid nuid);

#endif /* VBI_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
