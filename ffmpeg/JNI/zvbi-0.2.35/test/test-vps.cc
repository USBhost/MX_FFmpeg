/*
 *  libzvbi -- VPS low level functions unit test
 *
 *  Copyright (C) 2006, 2008 Michael H. Schimek
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

/* $Id: test-vps.cc,v 1.1 2009/03/04 21:48:17 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <limits.h>
#include "src/vps.h"
#include "test-pdc.h"

#define N_ELEMENTS(array) (sizeof (array) / sizeof (*(array)))

static const unsigned int
valid_cnis [] = {
	0x000, 0x001, 0x004, 0x010, 0x040, 0x100,
	0x400, 0x5A5, 0xA5A, 0xFFF
};

static const uint8_t
vps_sample [13] = {
	0xB1, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 1011 0001 0000 0100 1010 0000
	                       aa   cccc
			       10   fedc */
	0xC3, 0x76, 0x3F, 0x41, 0xFF
	/* 1100 0011 0111 0110 0011 1111 0100 0001 1111 1111
	   ccdd dddm mmmh hhhh mmmm mmcc cccc cccc pppp pppp
	   7643 2103 2104 3210 5432 10ba 9854 3210 7654 3210 */
};

static void
assert_decode_vps_cni		(unsigned int *		cni,
				 const uint8_t		buffer[13])
{
	uint8_t buffer2[13];
	unsigned int cni2;

	memcpy (buffer2, buffer, sizeof (buffer2));
	memset_rand (cni, sizeof (*cni));
	cni2 = *cni;

	assert (TRUE == vbi_decode_vps_cni (cni, buffer));
	assert ((unsigned int) *cni <= 0xFFF);
	assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
}

static void
assert_encode_vps_cni		(uint8_t		buffer[13],
				 unsigned int		cni,
				 vbi_bool		exp_success = TRUE)
{
	static const uint8_t cni_bits[13] = {
		0, 0, 0x0F, 0, 0, 0, 0, 0, 0xC0, 0, 0x03, 0xFF, 0
	};
	uint8_t buffer2[13];
	unsigned int i;

	memset_rand (buffer2, sizeof (buffer2));
	memcpy (buffer, buffer2, sizeof (buffer2));

	assert (exp_success == vbi_encode_vps_cni (buffer, cni));
	if (exp_success) {
		buffer2[2] |= 0x0F;
		for (i = 0; i < sizeof (buffer2); ++i) {
			assert (0 == ((buffer[i] ^ buffer2[i])
				      & ~cni_bits[i]));
		}
	} else {
		assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
	}
}

static void
assert_decode_vps_pdc		(test_pid *		pid,
				 const uint8_t		buffer[13],
				 const test_pid *	exp_pid = NULL)
{
	uint8_t buffer2[13];
	test_pid pid2;
	unsigned int cni;

	memcpy (buffer2, buffer, sizeof (buffer2));
	pid->randomize ();
	pid2 = *pid;

	assert (TRUE == vbi_decode_vps_pdc (pid, buffer));
	pid->assert_valid_vps ();
	assert_decode_vps_cni (&cni, buffer);
	assert (cni == pid->cni);
	if (NULL != exp_pid) {
		assert (exp_pid->cni == pid->cni);
		assert (exp_pid->pil == pid->pil);
		assert (exp_pid->pcs_audio == pid->pcs_audio);
		assert (exp_pid->pty == pid->pty);
	}
	assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
}

static void
assert_encode_vps_pdc		(uint8_t		buffer[13],
				 const test_pid *	pid,
				 vbi_bool		exp_success = TRUE)
{
	static const uint8_t pdc_bits[13] = {
		0, 0, 0xFF, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	};
	uint8_t buffer2[13];
	test_pid pid2;
	unsigned int i;

	pid2 = *pid;
	memset_rand (buffer2, sizeof (buffer2));
	memcpy (buffer, buffer2, sizeof (buffer2));

	assert (exp_success == vbi_encode_vps_pdc (buffer, pid));
	if (exp_success) {
		buffer2[2] |= 0x0F;
		for (i = 0; i < 13; ++i) {
			assert (0 == ((buffer[i] ^ buffer2[i])
				      & ~pdc_bits[i]));
		}
	} else {
		assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
	}

	assert (pid2 == *pid);
}

static void
assert_decode_dvb_pdc_descriptor
				(test_pid *		pid,
				 const uint8_t		buffer[5],
				 vbi_bool		exp_success = TRUE,
				 const test_pid *	exp_pid = NULL)
{
	uint8_t buffer2[5];
	test_pid pid2;

	memcpy (buffer2, buffer, sizeof (buffer2));
	pid->randomize ();
	pid2 = *pid;

	assert (exp_success == vbi_decode_dvb_pdc_descriptor (pid, buffer));
	if (exp_success) {
		pid->assert_valid_dvb ();
		if (NULL != exp_pid) {
			assert (exp_pid->pil == pid->pil);
		}
	} else {
		assert (pid2 == *pid);
	}

	assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
}

static void
assert_encode_dvb_pdc_descriptor
				(uint8_t		buffer[5],
				 const test_pid *	pid,
				 vbi_bool		exp_success = TRUE)
{
	uint8_t buffer2[5];
	test_pid pid2;

	pid2 = *pid;
	memset_rand (buffer2, sizeof (buffer2));
	memcpy (buffer, buffer2, sizeof (buffer2));

	assert (exp_success == vbi_encode_dvb_pdc_descriptor (buffer, pid));
	if (exp_success) {
		/* EN 300 468 section 6.1, 6.2. */
		assert (0x69 == buffer[0]);
		assert (3 == buffer[1]);
		/* EN 300 468 section 3.1. */
		assert (0xF0 == (buffer[2] & 0xF0));
	} else {
		assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
	}

	assert (pid2 == *pid);
}

int
main				(void)
{
	uint8_t buffer1[13];
	test_pid pid1;
	test_pid pid2;
	unsigned int cni;
	unsigned int i;

	for (i = 0; i < N_ELEMENTS (valid_cnis); ++i) {
		assert_encode_vps_cni (buffer1, valid_cnis[i]);
		assert_decode_vps_cni (&cni, buffer1);
		assert (cni == valid_cnis[i]);
	}

	assert_decode_vps_cni (&cni, vps_sample);
	assert (0x0DC1 == cni);

	/* TR 101 231. */
	assert_encode_vps_cni (buffer1, 0xDC3);
	buffer1[5 - 3] &= ~(0x80 >> 3);
	assert_decode_vps_cni (&cni, buffer1);
	assert (0xDC2 == cni); /* ZDF */
	buffer1[5 - 3] |= 0x80 >> 3;
	assert_decode_vps_cni (&cni, buffer1);
	assert (0xDC1 == cni); /* ARD */

	assert_encode_vps_cni (buffer1, 0x1000, FALSE);
	assert_encode_vps_cni (buffer1, INT_MIN, FALSE);
	assert_encode_vps_cni (buffer1, INT_MAX, FALSE);
	assert_encode_vps_cni (buffer1, UINT_MAX, FALSE);

	for (i = 0; i < 1000; ++i) {
		pid1.populate_vps ();
		assert_encode_vps_pdc (buffer1, &pid1);

		assert_decode_vps_cni (&cni, buffer1);
		assert (cni == pid1.cni);

		assert_decode_vps_pdc (&pid2, buffer1, &pid1);

		memset_rand (buffer1, sizeof (buffer1));
		assert_decode_vps_pdc (&pid2, buffer1);

		pid1.randomize ();
		pid1.pil &= max_pil;
		assert_encode_dvb_pdc_descriptor (buffer1, &pid1);

		assert_decode_dvb_pdc_descriptor (&pid2, buffer1,
						  TRUE, &pid1);

		memset_rand (buffer1, sizeof (buffer1));
		/* EN 300 468 section 6.1, 6.2. */
		buffer1[0] = 0x69;
		buffer1[1] = 3;
		assert_decode_dvb_pdc_descriptor (&pid2, buffer1);
	}

	assert_decode_vps_pdc (&pid1, vps_sample);
	assert (0xDC1 == pid1.cni);
	assert (VBI_PIL (0x0B, 0x01, 0x16, 0x0F) == pid1.pil);
	assert (0x02 == pid1.pcs_audio);
	assert (0xFF == pid1.pty);

	pid1.populate_vps ();
	pid1.cni = 0x1000;
	assert_encode_vps_pdc (buffer1, &pid1, FALSE);
	pid1.cni = UINT_MAX;
	assert_encode_vps_pdc (buffer1, &pid1, FALSE);

	/* TR 101 231. */
	pid1.populate_vps ();
	pid1.cni = 0xDC3;
	assert_encode_vps_pdc (buffer1, &pid1);
	buffer1[5 - 3] &= ~(0x80 >> 3);
	assert_decode_vps_pdc (&pid1, buffer1);
	assert (0xDC2 == pid1.cni);
	buffer1[5 - 3] |= 0x80 >> 3;
	assert_decode_vps_pdc (&pid1, buffer1);
	assert (0xDC1 == pid1.cni);

	pid1.populate_vps ();
	pid1.pil = max_pil + 1;
	assert_encode_vps_pdc (buffer1, &pid1, FALSE);
	pid1.pil = UINT_MAX;
	assert_encode_vps_pdc (buffer1, &pid1, FALSE);

	pid1.populate_vps ();
	pid1.pcs_audio = (vbi_pcs_audio) 4;
	assert_encode_vps_pdc (buffer1, &pid1, FALSE);
	pid1.pcs_audio = (vbi_pcs_audio) UINT_MAX;
	assert_encode_vps_pdc (buffer1, &pid1, FALSE);

	pid1.populate_vps ();
	pid1.pty = 0x100;
	assert_encode_vps_pdc (buffer1, &pid1, FALSE);
	pid1.pty = UINT_MAX;
	assert_encode_vps_pdc (buffer1, &pid1, FALSE);

	/* EN 300 468 section 6.1, 6.2. */
	memset_rand (buffer1, sizeof (buffer1));
	buffer1[0] = 0x69;
	buffer1[1] = 2;
	assert_decode_dvb_pdc_descriptor (&pid2, buffer1, FALSE);
	buffer1[1] = 4;
	assert_decode_dvb_pdc_descriptor (&pid2, buffer1, FALSE);
	buffer1[0] = 0x6a;
	buffer1[1] = 3;
	assert_decode_dvb_pdc_descriptor (&pid2, buffer1, FALSE);

	pid1.randomize ();
	pid1.pil = max_pil + 1;
	assert_encode_dvb_pdc_descriptor (buffer1, &pid1, FALSE);
	pid1.pil = UINT_MAX;
	assert_encode_dvb_pdc_descriptor (buffer1, &pid1, FALSE);

	return 0;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
