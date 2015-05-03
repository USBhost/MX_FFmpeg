/*
 *  libzvbi -- Error correction functions unit test
 *
 *  Copyright (C) 2003, 2007 Michael H. Schimek
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

/* $Id: test-hamm.cc,v 1.5 2008/03/01 07:35:55 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>		/* mrand48() */
#include <string.h>		/* memset() */

#include "src/hamm.h"

namespace vbi {
  static inline unsigned int rev8 (uint8_t c)
    { return vbi_rev8 (c); };
  static inline unsigned int rev8 (const uint8_t* p)
    { return vbi_rev8 (*p); };
  static inline unsigned int rev16 (uint16_t c)
    { return vbi_rev16 (c); }
  static inline unsigned int rev16 (const uint8_t* p)
    { return vbi_rev16p (p); };

  static inline unsigned int par8 (uint8_t c)
    { return vbi_par8 (c); };
  static inline void par (uint8_t* p, unsigned int n)
    { vbi_par (p, n); };
  static inline void par (uint8_t* begin, uint8_t* end)
    { vbi_par (begin, end - begin); };

  static inline int unpar8 (uint8_t c)
    { return vbi_unpar8 (c); };
  static inline int unpar (uint8_t* p, unsigned int n)
    { return vbi_unpar (p, n); };
  static inline int unpar (uint8_t* begin, uint8_t* end)
    { return vbi_unpar (begin, end - begin); };

  static inline unsigned int ham8 (unsigned int c)
    { return vbi_ham8 (c); };
  static inline void ham16 (uint8_t* p, uint8_t c)
    {
      p[0] = vbi_ham8 (c);
      p[1] = vbi_ham8 (c >> 4);
    }

  static inline int unham8 (uint8_t c)
    { return vbi_unham8 (c); };
  static inline int unham16 (uint16_t c)
    { return ((int) _vbi_hamm8_inv[c & 255])
	| ((int) _vbi_hamm8_inv[c >> 8] << 4); };
  static inline int unham16 (uint8_t* p)
    { return vbi_unham16p (p); };

  static inline void ham24 (uint8_t* p, unsigned int c)
    { vbi_ham24p (p, c); }

  static inline int unham24 (uint8_t* p)
    { return vbi_unham24p (p); };
};

static unsigned int
parity				(unsigned int		n)
{
	unsigned int sh;

	for (sh = sizeof (n) * 8 / 2; sh > 0; sh >>= 1)
		n ^= n >> sh;

	return n & 1;
}

#define BC(n) ((n) * (unsigned int) 0x0101010101010101ULL)

static unsigned int
population_count		(unsigned int		n)
{
	n -= (n >> 1) & BC (0x55);
	n = (n & BC (0x33)) + ((n >> 2) & BC (0x33));
	n = (n + (n >> 4)) & BC (0x0F);

	return (n * BC (0x01)) >> (sizeof (unsigned int) * 8 - 8);
}

unsigned int
hamming_distance		(unsigned int		a,
				 unsigned int		b)
{
	return population_count (a ^ b);
}

static void
test_rev			(void)
{
	unsigned int i;

	for (i = 0; i < 10000; ++i) {
		unsigned int n = (i < 256) ? i : (unsigned int) mrand48 ();
		uint8_t buf[4] = { n, n >> 8, n >> 16, 0xA5 };
		unsigned int r;
		unsigned int j;

		for (r = 0, j = 0; j < 8; ++j)
			if (n & (0x01 << j))
				r |= 0x80 >> j;

		assert (r == vbi::rev8 (n));
		assert (vbi::rev8 (n) == vbi::rev8 (buf));
		assert (vbi::rev16 (n) == vbi::rev16 (buf));
	}
}

static void
test_par_unpar			(void)
{
	unsigned int i;

	for (i = 0; i < 10000; ++i) {
		unsigned int n = (i < 256) ? i : (unsigned int) mrand48 ();
		uint8_t buf[4] = { n, n >> 8, n >> 16, 0xA5 };

		if (parity (n & 0xFF))
			assert (vbi::unpar8 (n) == (int)(n & 127));
		else
			assert (-1 == vbi::unpar8 (n));

		assert (vbi::unpar8 (vbi::par8 (n)) >= 0);

		vbi::par (buf, sizeof (buf));
		assert (vbi::unpar (buf, sizeof (buf)) >= 0);
		assert (0 == ((buf[0] | buf[1] | buf[2]) & 0x80));

		buf[1] = vbi::par8 (buf[1]);
		buf[2] = buf[1] ^ 0x80;

		assert (vbi::unpar (buf, sizeof (buf)) < 0);
		assert (buf[2] == (buf[1] & 0x7F));
	}
}

static void
test_ham8_ham16_unham8_unham16	(void)
{
	unsigned int i;

	for (i = 0; i < 10000; ++i) {
		unsigned int n = (i < 256) ? i : (unsigned int) mrand48 ();
		uint8_t buf[4] = { n, n >> 8, n >> 16, 0xA5 };
		unsigned int A, B, C, D;
		int d;

		A = parity (n & 0xA3);
		B = parity (n & 0x8E);
		C = parity (n & 0x3A);
		D = parity (n & 0xFF);

		d = (+ ((n & 0x02) >> 1)
		     + ((n & 0x08) >> 2)
		     + ((n & 0x20) >> 3)
		     + ((n & 0x80) >> 4));

		if (A && B && C) {
			unsigned int nn;

			nn = D ? n : (n ^ 0x40);

			assert (vbi::ham8 (d) == (nn & 255));
			assert (vbi::unham8 (nn) == d);
		} else if (!D) {
			unsigned int nn;
			int dd;

			dd = vbi::unham8 (n);
			assert (dd >= 0 && dd <= 15);

			nn = vbi::ham8 (dd);
			assert (hamming_distance (n & 255, nn) == 1);
		} else {
			assert (-1 == vbi::unham8 (n));
		}

		vbi::ham16 (buf, n);
		assert (vbi::unham16 (buf) == (int)(n & 255));
	}
}

static void
test_ham24			(unsigned int		val)
{
	uint8_t buf[4];
	unsigned int A, B, C, D, E, F;
	unsigned int n;

	memset (buf, 0xA5, sizeof (buf));

	vbi::ham24 (buf, val);

	assert (0xA5 == buf[3]);

	assert ((int)(val & ((1 << 18) - 1)) == vbi::unham24 (buf));

	n = buf[0] | (buf[1] << 8) | (buf[2] << 16);

	A = parity (n & 0x555555);
	B = parity (n & 0x666666);
	C = parity (n & 0x787878);
	D = parity (n & 0x007F80);
	E = parity (n & 0x7F8000);
	F = parity (n & 0xFFFFFF);

	assert (A && B && C && D && E && F);
}

static void
test_unham24			(void)
{
	unsigned int i;

	for (i = 0; i < (1 << 24); ++i) {
		uint8_t buf[4] = { i, i >> 8, i >> 16, 0xA5 };
		unsigned int A, B, C, D, E, F;
		int d;

		A = parity (i & 0x555555);
		B = parity (i & 0x666666);
		C = parity (i & 0x787878);
		D = parity (i & 0x007F80);
		E = parity (i & 0x7F8000);
		F = parity (i & 0xFFFFFF);

		d = (+ ((i & 0x000004) >> (3 - 1))
		     + ((i & 0x000070) >> (5 - 2))
		     + ((i & 0x007F00) >> (9 - 5))
		     + ((i & 0x7F0000) >> (17 - 12)));
		
		if (A && B && C && D && E) {
			/* No error. */
			assert (vbi::unham24 (buf) == d);
		} else if (F) {
			/* Uncorrectable error. */
			assert (vbi::unham24 (buf) < 0);
		} else {
			unsigned int err;
			unsigned int ii;

			/* Single bit error. */

			err = ((E << 4) | (D << 3)
			       | (C << 2) | (B << 1) | A) ^ 0x1F;

			assert (err > 0);

			if (err >= 24) {
				/* Invalid. */
				assert (vbi::unham24 (buf) < 0);
				continue;
			}

			/* Correctable single bit error. */

			ii = i ^ (1 << (err - 1));

			A = parity (ii & 0x555555);
			B = parity (ii & 0x666666);
			C = parity (ii & 0x787878);
			D = parity (ii & 0x007F80);
			E = parity (ii & 0x7F8000);
			F = parity (ii & 0xFFFFFF);

			assert (A && B && C && D && E && F);

			d = (+ ((ii & 0x000004) >> (3 - 1))
			     + ((ii & 0x000070) >> (5 - 2))
			     + ((ii & 0x007F00) >> (9 - 5))
			     + ((ii & 0x7F0000) >> (17 - 12)));

			assert (vbi::unham24 (buf) == d);
		}
	}
}

int
main				(int			argc,
				 char **		argv)
{
	unsigned int i;

	argc = argc; /* unused */
	argv = argv;

	test_rev ();

	test_par_unpar ();

	test_ham8_ham16_unham8_unham16 ();

	for (i = 0; i < (1 << 18); ++i)
		test_ham24 (i);

	test_ham24 (1 << 18);
	test_ham24 (-1);

	test_unham24 ();

	return 0;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
