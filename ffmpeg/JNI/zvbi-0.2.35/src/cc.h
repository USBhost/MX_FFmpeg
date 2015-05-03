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

/* $Id: cc.h,v 1.9 2008/02/19 00:35:15 mschimek Exp $ */

#ifndef CC_H
#define CC_H

#include <pthread.h>

#include "bcd.h"
#include "format.h"

#ifndef VBI_DECODER
#define VBI_DECODER
typedef struct vbi_decoder vbi_decoder;
#endif

typedef struct {
	int			count;
	int			chksum;
	uint8_t			buffer[32];
} xds_sub_packet;

typedef enum {
	MODE_NONE,
	MODE_POP_ON,
	MODE_PAINT_ON,
	MODE_ROLL_UP,
	MODE_TEXT
} cc_mode;

typedef struct {
	cc_mode			mode;

	int			col, col1;
	int			row, row1;
	int			roll;

	int			nul_ct;	// XXX should be 'silence count'
	double			time;
	unsigned char *		language;		/* Latin-1 */

	vbi_char		attr;
	vbi_char *		line;

	int			hidden;
	vbi_page		pg[2];
} cc_channel;

struct caption {
	pthread_mutex_t		mutex;

	uint8_t			last[2];		/* field 1, cc command repetition */

	int			curr_chan;
	vbi_char		transp_space[2];	/* caption, text mode */
	cc_channel		channel[9];		/* caption 1-4, text 1-4, garbage */

	xds_sub_packet		sub_packet[4][0x18];
	xds_sub_packet *	curr_sp;
	int			xds;

	uint8_t			itv_buf[256];
	int			itv_count;

	int			info_cycle[2];
};

/* Public */

/**
 * @addtogroup Cache
 * @{
 */
extern vbi_bool		vbi_fetch_cc_page(vbi_decoder *vbi, vbi_page *pg,
					  vbi_pgno pgno, vbi_bool reset);
/** @} */

/* Private */

extern void		vbi_caption_init(vbi_decoder *vbi);
extern void		vbi_caption_destroy(vbi_decoder *vbi);
extern void		vbi_decode_caption(vbi_decoder *vbi, int line, uint8_t *buf);
extern void		vbi_caption_desync(vbi_decoder *vbi);
extern void		vbi_caption_channel_switched(vbi_decoder *vbi);
extern void		vbi_caption_color_level(vbi_decoder *vbi);

#endif /* CC_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
