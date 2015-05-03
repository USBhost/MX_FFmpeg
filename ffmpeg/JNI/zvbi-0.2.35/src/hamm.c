/*
 *  libzvbi -- Error correction functions
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

/* $Id: hamm.c,v 1.11 2013/07/10 11:37:08 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <limits.h>		/* CHAR_BIT */

#include "hamm.h"
#include "hamm-tables.h"

/**
 * @ingroup Error
 *
 * @param p Array of unsigned bytes.
 * @param n Size of array.
 * 
 * Of each byte of the array, changes the most significant
 * bit to make the number of set bits odd.
 *
 * @since 0.2.12
 */
void
vbi_par				(uint8_t *		p,
				 unsigned int		n)
{
	while (n-- > 0) {
		uint8_t c = *p;

		/* if 0 == (inv_par[] & 32) change msb of *p. */
		*p++ = c ^ (128 & ~(_vbi_hamm24_inv_par[0][c] << 2));
	}
}

/**
 * @ingroup Error
 * @param p Array of unsigned bytes.
 * @param n Size of array.
 * 
 * Tests the parity and clears the most significant bit of
 * each byte of the array.
 *
 * @return
 * A negative value if any byte of the array had even
 * parity (sum of bits modulo 2 is 0).
 *
 * @since 0.2.12
 */
int
vbi_unpar			(uint8_t *		p,
				 unsigned int		n)
{
	int r = 0;

	while (n-- > 0) {
		uint8_t c = *p;

		/* if 0 == (inv_par[] & 32) set msb of r. */
		r |= ~ _vbi_hamm24_inv_par[0][c]
			<< (sizeof (int) * CHAR_BIT - 1 - 5);

		*p++ = c & 127;
	}

	return r;
}

/**
 * @ingroup Error
 * @param p A Hamming 24/18 protected 24 bit word will be stored here,
 *   last significant byte first, lsb first transmitted.
 * @param c Integer between 0 ... 1 << 18 - 1.
 * 
 * Encodes an 18 bit word with Hamming 24/18 protection
 * as specified in ETS 300 706, Section 8.3.
 *
 * @since 0.2.27
 */
void
vbi_ham24p			(uint8_t *		p,
				 unsigned int		c)
{
	unsigned int D5_D11;
	unsigned int D12_D18;
	unsigned int P5, P6;
	unsigned int Byte_0;

	Byte_0 = (_vbi_hamm24_fwd_0 [(c >> 0) & 0xFF]
		  ^ _vbi_hamm24_fwd_1 [(c >> 8) & 0xFF]
		  ^ _vbi_hamm24_fwd_2 [(c >> 16) & 0x03]);
	p[0] = Byte_0;

	D5_D11 = (c >> 4) & 0x7F;
	D12_D18 = (c >> 11) & 0x7F;

	P5 = 0x80 & ~(_vbi_hamm24_inv_par[0][D12_D18] << 2);
	p[1] = D5_D11 | P5;

	P6 = 0x80 & ((_vbi_hamm24_inv_par[0][Byte_0]
		      ^ _vbi_hamm24_inv_par[0][D5_D11]) << 2);
	p[2] = D12_D18 | P6;
}

/**
 * @ingroup Error
 * @param p Pointer to a Hamming 24/18 protected 24 bit word,
 *   last significant byte first, lsb first transmitted.
 * 
 * Decodes a Hamming 24/18 protected byte triplet
 * as specified in ETS 300 706, Section 8.3.
 * 
 * @return
 * Triplet data bits D18 [msb] ... D1 [lsb] or a negative value
 * if the triplet contained uncorrectable errors.
 *
 * @since 0.2.12
 */
int
vbi_unham24p			(const uint8_t *	p)
{
	unsigned int D1_D4;
	unsigned int D5_D11;
	unsigned int D12_D18;
	unsigned int ABCDEF;
	int32_t d;

	D1_D4 = _vbi_hamm24_inv_d1_d4[p[0] >> 2];
	D5_D11 = p[1] & 0x7F;
	D12_D18 = p[2] & 0x7F;

	d = D1_D4 | (D5_D11 << 4) | (D12_D18 << 11);

	ABCDEF = (_vbi_hamm24_inv_par[0][p[0]]
		  ^ _vbi_hamm24_inv_par[1][p[1]]
		  ^ _vbi_hamm24_inv_par[2][p[2]]);

	/* Correct single bit error, set MSB on double bit error. */
	return d ^ (int) _vbi_hamm24_inv_err[ABCDEF];
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
