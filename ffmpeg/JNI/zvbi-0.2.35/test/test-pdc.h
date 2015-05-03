/*
 *  libzvbi -- PDC functions unit test
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

/* $Id: test-pdc.h,v 1.1 2009/03/04 21:48:57 mschimek Exp $ */

#include <float.h>
#include <assert.h>
#include "src/pdc.h"
#include "test-common.h"

static const vbi_pil max_pil = VBI_PIL (15, 31, 31, 63);

class test_pid : public vbi_program_id {
	void assert_valid_any (void) {
		assert ((unsigned int) this->pil <= max_pil);
		assert (0 == (this->luf & ~1));
		assert (0 == (this->mi & ~1));
		assert (0 == (this->prf & ~1));
		assert ((unsigned int) this->pcs_audio <= 3);
		assert ((unsigned int) this->pty <= 0xFF);
		assert (0 == (this->tape_delayed & ~1));
		assert (0 == memcmp_zero (this->_reserved2,
					  sizeof (this->_reserved2)));
		assert (0 == memcmp_zero (this->_reserved3,
					  sizeof (this->_reserved3)));
	}

	void assert_valid_simple (void) {
		assert_valid_any ();
		assert (0 == this->luf);
		assert (1 == this->mi);
		assert (0 == this->prf);
	}

public:
	void clear (void) {
		memset (this, 0, sizeof (*this));
	}

	void randomize (void) {
		memset_rand (this, sizeof (*this));
	}

	void populate_dvb (void) {
		randomize ();
		this->pil &= max_pil;
	}

	void populate_vps (void) {
		populate_dvb ();
		this->cni &= 0xFFF;
		this->pcs_audio = (vbi_pcs_audio)
			((int) this->pcs_audio & 3);
		this->pty &= 0xFF;
	}

	void populate_ttx (void) {
		populate_vps ();
		this->channel = (vbi_pid_channel)
			((int) this->channel & 3);
		this->luf &= 1;
		this->mi &= 1;
		this->prf &= 1;
	}

	void populate_xds (void) {
		randomize ();
		this->pil &= max_pil;
		this->tape_delayed &= 1;
	}

	bool operator == (const test_pid& other) const {
		/* Note: bitwise equal. */
		return (0 == memcmp (this, &other, sizeof (this)));
	}

	void assert_valid_ttx (void) {
		assert_valid_any ();
		assert (this->channel >= VBI_PID_CHANNEL_LCI_0
			&& this->channel <= VBI_PID_CHANNEL_LCI_3);
		assert (VBI_CNI_TYPE_8302 == this->cni_type);
		assert (0 == this->tape_delayed);
	}

	void assert_valid_vps (void) {
		assert_valid_simple ();
		assert (VBI_PID_CHANNEL_VPS == this->channel);
		assert (VBI_CNI_TYPE_VPS == this->cni_type);
		assert (0 == this->tape_delayed);
	}

	void assert_valid_dvb (void) {
		assert_valid_simple ();
		assert (VBI_PID_CHANNEL_PDC_DESCRIPTOR == this->channel);
		assert (VBI_CNI_TYPE_NONE == this->cni_type);
		assert (0 == this->cni);
		assert (VBI_PCS_AUDIO_UNKNOWN == this->pcs_audio);
		assert (0 == this->pty);
		assert (0 == this->tape_delayed);
	}

	void assert_valid_xds (void) {
		assert_valid_simple ();
		assert (VBI_PID_CHANNEL_XDS_CURRENT == this->channel
			|| VBI_PID_CHANNEL_XDS_FUTURE == this->channel);
		assert (VBI_CNI_TYPE_NONE == this->cni_type);
		assert (0 == this->cni);
		assert (VBI_PCS_AUDIO_UNKNOWN == this->pcs_audio);
		assert (0 == this->pty);
	}
};

static time_t
ztime				(const char *		s)
  _vbi_unused;

static time_t
ztime				(const char *		s)
{
	struct tm tm;
	time_t t;

	memset (&tm, 0, sizeof (tm));
	assert (NULL != strptime (s, "%n%Y%m%dT%H%M%S", &tm));
	t = timegm (&tm);
	assert ((time_t) -1 != t);
	return t;
}

#define ANY_TIME (TIME_MAX - 12345)

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
