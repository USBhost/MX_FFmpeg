/*
 *  libzvbi - EIA 608-B Closed Caption decoder
 *
 *  Copyright (C) 2008 Michael H. Schimek
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

/* $Id: cc608_decoder.h,v 1.2 2009/12/14 23:43:26 mschimek Exp $ */

/* This code is experimental and not yet part of the library. */

#ifndef __ZVBI_CC608_DECODER_H__
#define __ZVBI_CC608_DECODER_H__

#include <stdio.h>

#include "format.h"
#include "event.h"
#include "sliced.h"

VBI_BEGIN_DECLS

/* Public */

#define VBI_CAPTION_CC1 1 /* primary synchronous caption service (F1) */
#define VBI_CAPTION_CC2 2 /* special non-synchronous use captions (F1) */
#define VBI_CAPTION_CC3 3 /* secondary synchronous caption service (F2) */
#define VBI_CAPTION_CC4 4 /* special non-synchronous use captions (F2) */

#define VBI_CAPTION_T1 5 /* first text service (F1) */
#define VBI_CAPTION_T2 6 /* second text service (F1) */
#define VBI_CAPTION_T3 7 /* third text service (F2) */
#define VBI_CAPTION_T4 8 /* fourth text service (F2) */

/** @internal */
typedef enum {
	_VBI_CC608_MODE_UNKNOWN,
	_VBI_CC608_MODE_ROLL_UP,
	_VBI_CC608_MODE_POP_ON,
	_VBI_CC608_MODE_PAINT_ON,
	_VBI_CC608_MODE_TEXT
} _vbi_cc608_mode;

/** @internal */
typedef enum {
	_VBI_CC608_START_ROLLING = (1 << 0)
} _vbi_cc608_event_flags;

/** @internal */
struct _vbi_event_cc608_page {
	int				channel;
	_vbi_cc608_mode			mode;
	_vbi_cc608_event_flags		flags;
};

/** @internal */
struct _vbi_event_cc608_stream {
	double				capture_time;
	int64_t				pts;
	int				channel;
	_vbi_cc608_mode			mode;
	vbi_char			text[32];
};

typedef struct _vbi_cc608_decoder _vbi_cc608_decoder;

extern void
_vbi_cc608_dump			(FILE *			fp,
				 unsigned int		c1,
				 unsigned int		c2);
extern vbi_bool
_vbi_cc608_decoder_get_page	(_vbi_cc608_decoder *	cd,
				 vbi_page *		pg,
				 vbi_pgno		channel,
				 vbi_bool		padding);
extern vbi_bool
_vbi_cc608_decoder_feed		(_vbi_cc608_decoder *	cd,
				 const uint8_t		buffer[2],
				 unsigned int		line,
				 double			capture_time,
				 int64_t		pts);
extern vbi_bool
_vbi_cc608_decoder_feed_frame	(_vbi_cc608_decoder *	cd,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 double			capture_time,
				 int64_t		pts);
extern void
_vbi_cc608_decoder_remove_event_handler
				(_vbi_cc608_decoder *	cd,
				 vbi_event_handler	callback,
				 void *			user_data);
extern vbi_bool
_vbi_cc608_decoder_add_event_handler
				(_vbi_cc608_decoder *	cd,
				 unsigned int		event_mask,
				 vbi_event_handler	callback,
				 void *			user_data);
extern void
_vbi_cc608_decoder_reset	(_vbi_cc608_decoder *	cd);
extern void
_vbi_cc608_decoder_delete	(_vbi_cc608_decoder *	cd);
extern _vbi_cc608_decoder *
_vbi_cc608_decoder_new		(void);

/* Private */

VBI_END_DECLS

#endif /* __ZVBI_CC608_DECODER_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
