/*
 *  libzvbi - vbi_dvb_demux unit test
 *
 *  Copyright (C) 2007 Michael H. Schimek
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

/* $Id: test-dvb_demux.cc,v 1.3 2008/03/01 07:36:09 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <assert.h>

#include "src/dvb_demux.h"
#include "test-common.h"

/* TO DO */

static void
test_silly_start_codes		(void)
{
	vbi_dvb_demux *dx;
	uint8_t *packet;
	const uint8_t *p;
	unsigned int p_left;
	vbi_sliced *sliced_out;
	unsigned int max_lines_out;
	int64_t pts_out;
	unsigned int i;

	dx = vbi_dvb_pes_demux_new (/* callback */ NULL,
				    /* user_data */ NULL);
	assert (NULL != dx);

	max_lines_out = 50;
	sliced_out = (vbi_sliced *) xmalloc (max_lines_out
					     * sizeof (*sliced_out));

	packet = (uint8_t *) xmalloc (256);
	memset (packet, -1, 256);

	packet[7] = 0x00;
	packet[8] = 0x00;
	packet[9] = 0x01;

	for (i = 0x00; i < 0xBC; ++i) {
		unsigned int n_lines_out;

		packet[10] = 0x04;

		p = packet;
		p_left = 256;

		n_lines_out = vbi_dvb_demux_cor (dx,
						 sliced_out,
						 max_lines_out,
						 &pts_out,
						 &p,
						 &p_left);
		assert (0 == n_lines_out);
		assert (0 == p_left);
	}

	free (packet);
	free (sliced_out);

	vbi_dvb_demux_delete (dx);
}

int
main				(void)
{
	/* Regression for a bug fixed in 0.2.27. */
	test_silly_start_codes ();

	return 0;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
