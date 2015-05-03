/*
 *  libzvbi -- Teletext packet 8/30 low level functions unit test
 *
 *  Copyright (C) 2008 Michael H. Schimek
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

/* $Id: test-packet-830.cc,v 1.1 2009/03/04 21:48:20 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include "src/packet-830.h"
#include "src/hamm.h"
#include "test-pdc.h"

#define N_ELEMENTS(array) (sizeof (array) / sizeof (*(array)))

static const unsigned int
valid_cnis [] = {
	0x0000, 0x0001, 0x0004, 0x0010, 0x0040, 0x0100,
	0x0400, 0x1000, 0x4000, 0x5A5A, 0xA5A5, 0xFFFF
};

static const uint8_t
teletext_8302_sample [42] = {
	0x15, 0xEA, 0x49, 0x15, 0x15, 0xEA, 0xEA, 0xEA, 0x5e, 
	0x15, 0x73, 0xEA, 0x9B,
	/* 00010101 01110011 11101010 10011011
	   0 0 0 0  0 1 0 1  1 1 1 1  1 0 1 1
               l0l1     a0a1 cccdcecf d3d4c6c7  bit 0 = LSB
	   r u l2l1 --m a2a1 c4c3c2c1 p2p1cAc9 }
	   3 2 1 0  3 2 1 0  3 2 1 0  3 2 1 0  } EN 300 231 Table 8
	   13       14       15       16       }
	   r=PRF, u=LUF, l=LCI, a=PCS, c=CNI, p=PIL/PTY, d=day,
	   m=MI/month/minute, h=hour. Compare test-vps.cc. */
	0xEA, 0x49, 0x5E, 0x73,
	/* 11101010 01001001 01011110 01110011
	   1 1 1 1  0 0 1 0  0 0 1 1  0 1 0 1
           m3d0d1d2 h4m0m1m2 h0h1h2h3 m2m3m4m5
           p6p5p4p3 pAp9p8p7 pEpDpCpB pIpHpGpF
	   3 2 1 0  3 2 1 0  3 2 1 0  3 2 1 0
	   17       18       19       20 */
	0xA1, 0x49, 0xB6, 0x15,	0x64,
	/* 10100001 01001001 10110110 00010101 01100100
	   1 1 0 0  0 0 1 0  1 1 0 1  0 0 0 0  0 1 0 0
	   cacbm0m1 c4c5c8c9 c0c1c2c3 p4p5p6p7 p0p1p2p3
           c6c5pKpJ cCcBc8c7 cGcFcEcD p4p3p2p1 p8p7p6p5
	   3 2 1 0  3 2 1 0  3 2 1 0  3 2 1 0  3 2 1 0
	   21       22       23       24       25 */
	0xC2, 0x52, 0xBA, 0x20, 0x52, 0xEF, 0xF4, 0xE5,
	0x20, 0x52, 0xEF, 0x73, 0xE5, 0x6E, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20
};

static const int
bad_mjd [] = {
	0x00000,
	0x01111,
	0x10111,
	0x11011,
	0x11101,
	0x11110,
	0xAAAAB, /* 9999A */
	0xAAABA,
	0xAABAA,
	0xABAAA,
	0xBAAAA, /* A9999 */
	0xFFFFF
};

static const int
bad_utc [] = {
	0x000000,
	0x011111,
	0x101111,
	0x110111,
	0x111011,
	0x111101,
	0x111110,
	0x11111B, /* 00000A */
	0x111172, /* 000061 */
	0x11117B, /* 00006A */
	0x111181, /* 000070 */
	0x111B11, /* 000A00 */
	0x117111, /* 006000 */
	0x351111, /* 240000 */
	0x411111, /* 300000 */
	0xFFFFFF
};

static void
assert_decode_teletext_8301_cni	(unsigned int *		cni,
				 const uint8_t		buffer[42])
{
	uint8_t buffer2[42];
	unsigned int cni2;

	memcpy (buffer2, buffer, sizeof (buffer2));
	memset_rand (cni, sizeof (cni));
	cni2 = *cni;

	assert (TRUE == vbi_decode_teletext_8301_cni (cni, buffer));
	assert ((unsigned int) *cni <= 0xFFFF);
	assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
}

static void
encode_teletext_8301_cni	(uint8_t		buffer[42],
				 unsigned int		cni)
{
	/* EN 300 706 Section 9.8.1. */

	/* -3: CRI, FRC. */
	buffer[13 - 3 - 1] = vbi_rev16 (cni);
	buffer[14 - 3 - 1] = vbi_rev16 (cni) >> 8;
}

static void
assert_decode_teletext_8301_local_time
				(time_t *		time,
				 int *			seconds_east,
				 const uint8_t		buffer[42],
				 vbi_bool		exp_success = TRUE,
				 time_t			exp_time = ANY_TIME,
				 int			exp_seconds_east = 0)
{
	uint8_t buffer2[42];
	time_t time2;
	int seconds_east2;

	memcpy (buffer2, buffer, sizeof (buffer2));
	memset_rand (time, sizeof (*time));
	time2 = *time;
	memset_rand (seconds_east, sizeof (*seconds_east));
	seconds_east2 = *seconds_east;

	assert (exp_success
		== vbi_decode_teletext_8301_local_time (time,
							seconds_east,
							buffer));
	if (exp_success) {
		if (ANY_TIME != exp_time) {
			assert (exp_time == *time);
			assert (exp_seconds_east == *seconds_east);
		}
	} else {
		assert (*time == time2);
		assert (*seconds_east == seconds_east2);
	}

	assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
}

static void
assert_decode_teletext_8301_local_time
				(const uint8_t		buffer[42],
				 vbi_bool		exp_success = TRUE,
				 time_t			exp_time = ANY_TIME,
				 int			exp_seconds_east = 0)
{
	time_t time;
	int seconds_east;

	assert_decode_teletext_8301_local_time (&time,
						&seconds_east,
						buffer,
						exp_success,
						exp_time,
						exp_seconds_east);
}

static void
encode_teletext_8301_local_time	(uint8_t		buffer[42],
				 int			mjd,
				 int			utc,
				 int			seconds_east,
				 bool			add_one = TRUE)
{
	buffer[15 - 3 - 1] =
		((abs (seconds_east / (30 * 60)) & 0x1F) << 1)
		| ((seconds_east < 0) ? 0x40 : 0x00);

	if (add_one)
		mjd += 0x11111;
	buffer[16 - 3 - 1] = mjd >> 16;
	buffer[17 - 3 - 1] = mjd >> 8;
	buffer[18 - 3 - 1] = mjd >> 0;

	if (add_one)
		utc += 0x111111;
	buffer[19 - 3 - 1] = utc >> 16;
	buffer[20 - 3 - 1] = utc >> 8;
	buffer[21 - 3 - 1] = utc >> 0;
}

static void
assert_decode_teletext_8302_cni	(unsigned int *		cni,
				 const uint8_t		buffer[42],
				 vbi_bool		exp_success = TRUE)
{
	uint8_t buffer2[42];
	unsigned int cni2;

	memcpy (buffer2, buffer, sizeof (buffer2));
	memset_rand (cni, sizeof (*cni));
	cni2 = *cni;

	assert (exp_success == vbi_decode_teletext_8302_cni (cni, buffer));
	if (exp_success) {
		assert ((unsigned int) *cni <= 0xFFFF);
	} else {
		assert (*cni == cni2);
	}

	assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
}

static void
assert_decode_teletext_8302_pdc	(test_pid *		pid,
				 const uint8_t		buffer[42],
				 vbi_bool		exp_success = TRUE,
				 const test_pid *	exp_pid = NULL)
{
	uint8_t buffer2[42];
	test_pid pid2;

	memcpy (buffer2, buffer, sizeof (buffer2));
	pid->randomize ();
	pid2 = *pid;

	assert (exp_success == vbi_decode_teletext_8302_pdc (pid, buffer));
	if (exp_success) {
		unsigned int cni;

		pid->assert_valid_ttx ();
		assert_decode_teletext_8302_cni (&cni, buffer);
		assert (cni == pid->cni);
		if (NULL != exp_pid) {
			assert (exp_pid->channel == pid->channel);
			assert (exp_pid->cni == pid->cni);
			assert (exp_pid->pil == pid->pil);
			assert (exp_pid->luf == pid->luf);
			assert (exp_pid->mi == pid->mi);
			assert (exp_pid->prf == pid->prf);
			assert (exp_pid->pcs_audio == pid->pcs_audio);
			assert (exp_pid->pty == pid->pty);
		}
	} else {
		assert (pid2 == *pid);
	}

	assert (0 == memcmp (buffer, buffer2, sizeof (buffer2)));
}

static void
encode_teletext_8302_pdc	(uint8_t		buffer[42],
				 const test_pid *	pid)
{
	unsigned int i;

	memset_rand (buffer, 42);

	/* EN 300 706 Section 9.8.2, EN 300 231 Section 8.2.1,
	   TR 300 231 Section 5. */

	/* -3: CRI, FRC. */
	buffer[13 - 3 - 1] = (+ ((pid->channel   << 2) & 0xC)
			      + ((pid->luf       << 1) & 0x2)
			      + ((pid->prf       << 0) & 0x1));
	buffer[14 - 3 - 1] = (+ ((pid->pcs_audio << 2) & 0xC)
			      + ((pid->mi        << 1) & 0x2)
			      + ((buffer[14 - 3 - 1] ) & 0x1));
	buffer[15 - 3 - 1] = pid->cni >> (16 - 4);
	buffer[16 - 3 - 1] = (+ (((pid->cni >> (16 - 10)) << 2) & 0xC)
			      + (((pid->pil >> (20 -  2)) << 0) & 0x3));
	buffer[17 - 3 - 1] = pid->pil >> (20 - 6);
	buffer[18 - 3 - 1] = pid->pil >> (20 - 10);
	buffer[19 - 3 - 1] = pid->pil >> (20 - 14);
	buffer[20 - 3 - 1] = pid->pil >> (20 - 18);
	buffer[21 - 3 - 1] = (+ (((pid->pil >> (20 - 20)) << 2) & 0xC)
			      + (((pid->cni >> (16 -  6)) << 0) & 0x3));
	buffer[22 - 3 - 1] = (+ (((pid->cni >> (16 -  8)) << 2) & 0xC)
			      + (((pid->cni >> (16 - 12)) << 0) & 0x3));
	buffer[23 - 3 - 1] = pid->cni >> (16 - 16);
	buffer[24 - 3 - 1] = pid->pty >> (8 - 4);
	buffer[25 - 3 - 1] = pid->pty >> (8 - 8);

	for (i = 7; i <= 12; ++i)
		buffer[i - 3 - 1] = vbi_ham8 (buffer[i - 3 - 1]);

	for (i = 13; i <= 25; ++i) {
		/* Transmitted MSB first, like VPS. */
		int c = vbi_rev8 (buffer[i - 3 - 1]) >> 4;

		buffer[i - 3 - 1] = vbi_ham8 (c);
	}
}

int
main				(void)
{
	uint8_t buffer1[42];
	test_pid pid1;
	test_pid pid2;
	time_t t1;
	unsigned int cni;
	unsigned int i;

	for (i = 0; i < N_ELEMENTS (valid_cnis); ++i) {
		memset_rand (buffer1, sizeof (buffer1));
		encode_teletext_8301_cni (buffer1, valid_cnis[i]);
		assert_decode_teletext_8301_cni (&cni, buffer1);
		assert (cni == valid_cnis[i]);

		pid1.randomize ();
		pid1.cni = valid_cnis[i];
		encode_teletext_8302_pdc (buffer1, &pid1);
		assert_decode_teletext_8302_cni (&cni, buffer1);
		assert (cni == valid_cnis[i]);

		/* Single bit error. */
		buffer1[15 - 3 - 1] ^= 0x04;
		buffer1[22 - 3 - 1] ^= 0x02;
		assert_decode_teletext_8302_cni (&cni, buffer1);
		assert (cni == valid_cnis[i]);

		/* Double bit error. */
		buffer1[15 - 3 - 1] ^= 0x08;
		assert_decode_teletext_8302_cni (&cni, buffer1, FALSE);
	}

	memset_rand (buffer1, sizeof (buffer1));
	t1 = ztime ("19820131T000000");

	encode_teletext_8301_local_time	(buffer1, 0x00000, 0x000000, 0);
	if (TIME_MIN < -2147483648.0) {
		assert_decode_teletext_8301_local_time
			(buffer1, TRUE, ztime ("18581117T000000"), 0);
	} else {
		/* Not representable as time_t. */
		assert_decode_teletext_8301_local_time (buffer1, FALSE);
	}

	/* EN 300 706 Table 18: "Reference point". */
	encode_teletext_8301_local_time	(buffer1, 0x45000, 0x000000, 0);
	assert_decode_teletext_8301_local_time (buffer1, TRUE, t1, 0);

	/* 2000 is a leap year. */
	encode_teletext_8301_local_time	(buffer1, 0x51603, 0x213243, 0);
	assert_decode_teletext_8301_local_time
		(buffer1, TRUE, ztime ("20000229T213243"), 0);

	/* +1 leap second. EN 300 706 Section 9.8.1 does not specify
	   if UDT counts leap seconds. We assume it does, which should
	   be safe because time_t ignores leap seconds. */ 
	encode_teletext_8301_local_time	(buffer1, 0x53735, 0x235959, 0);
	assert_decode_teletext_8301_local_time
		(buffer1, TRUE, ztime ("20051231T235959"), 0);
	encode_teletext_8301_local_time	(buffer1, 0x53735, 0x235960, 0);
	assert_decode_teletext_8301_local_time
		(buffer1, TRUE, ztime ("20060101T000000"), 0);
	encode_teletext_8301_local_time	(buffer1, 0x53736, 0x000000, 0);
	assert_decode_teletext_8301_local_time
		(buffer1, TRUE, ztime ("20060101T000000"), 0);

	/* -1 leap second just skips 0x235959, not testable. */

	encode_teletext_8301_local_time	(buffer1, 0x99999, 0x235960, 0);
	if (TIME_MAX > 4294967295.0) {
		assert_decode_teletext_8301_local_time
			(buffer1, TRUE, ztime ("21320901T000000"), 0);
	} else {
		/* Not representable as time_t. */
		assert_decode_teletext_8301_local_time (buffer1, FALSE);
	}

	for (i = 0; i < N_ELEMENTS (bad_mjd); ++i) {
		encode_teletext_8301_local_time	(buffer1, bad_mjd[i],
						 0x111111, /* lto */ 0,
						 /* add_one */ FALSE);
		assert_decode_teletext_8301_local_time (buffer1, FALSE);
	}

	for (i = 0; i < N_ELEMENTS (bad_utc); ++i) {
		encode_teletext_8301_local_time	(buffer1, 0x56111,
						 bad_utc[i], /* lto */ 0,
						 /* add_one */ FALSE);
		assert_decode_teletext_8301_local_time (buffer1, FALSE);
	}

	for (i = 0; i <= 0x1F; ++i) {
		encode_teletext_8301_local_time	(buffer1, 0x45000, 0,
						 i * 30 * 60);
		assert_decode_teletext_8301_local_time
			(buffer1, TRUE, t1, i * 30 * 60);
		buffer1[15 - 3 - 1] ^= 0x40;
		assert_decode_teletext_8301_local_time
			(buffer1, TRUE, t1, -i * 30 * 60);
	}

	for (i = 0; i < 1000; ++i) {
		unsigned int j;

		pid1.populate_ttx ();
		encode_teletext_8302_pdc (buffer1, &pid1);

		assert_decode_teletext_8302_cni (&cni, buffer1);
		assert (cni == pid1.cni);

		assert_decode_teletext_8302_pdc (&pid2, buffer1,
						 TRUE, &pid1);

		memset_rand (buffer1, sizeof (buffer1));
		for (j = 13; j <= 25; ++j) {
			buffer1[j - 3 - 1] =
				vbi_ham8 (buffer1[j - 3 - 1] & 0xF);
		}
		assert_decode_teletext_8302_pdc (&pid2, buffer1);
		pid1 = pid2;
		/* Single bit error. */
		buffer1[14 - 3 - 1] ^= 0x02;
		buffer1[23 - 3 - 1] ^= 0x80;
		assert_decode_teletext_8302_pdc (&pid2, buffer1,
						 TRUE, &pid1);
		/* Double bit error. */
		buffer1[23 - 3 - 1] ^= 0x10;
		assert_decode_teletext_8302_pdc (&pid2, buffer1, FALSE);
	}

	assert_decode_teletext_8302_pdc (&pid1, teletext_8302_sample);
	assert (0 == pid1.channel);
	assert (0xFDCB == pid1.cni);
	assert (VBI_PIL (0x0A, 0x0F, 0x0C, 0x28) == pid1.pil);
	assert (0 == pid1.luf);
	assert (1 == pid1.mi);
	assert (0 == pid1.prf);
	assert (0x02 == pid1.pcs_audio);
	assert (0x02 == pid1.pty);

	return 0;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
