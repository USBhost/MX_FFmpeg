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

/* $Id: test-pdc.cc,v 1.2 2009/03/23 01:30:45 mschimek Exp $ */

#undef NDEBUG

#include <errno.h>
#include "src/pdc.h"
#include "src/misc.h"		/* mktime(), timegm() */
#include "test-pdc.h"

#if 0 /* for debugging */

static void
print_time			(time_t			time)
{
	char buffer[80];
	struct tm tm;

	printf ("%ld ", (long) time);

	memset (&tm, 0, sizeof (tm));
	localtime_r (&time, &tm);
	strftime (buffer, sizeof (buffer),
		  "%Y-%m-%d %H:%M:%S %Z = ", &tm);
	fputs (buffer, stdout);

	memset (&tm, 0, sizeof (tm));
	gmtime_r (&time, &tm);
	strftime (buffer, sizeof (buffer),
		  "%Y-%m-%d %H:%M:%S UTC", &tm);
	puts (buffer);
}

#endif /* 0 */

static const vbi_pil
valid_dates [] = {
	VBI_PIL (1, 1, 0, 0),
	VBI_PIL (1, 1, 1, 0),
	VBI_PIL (1, 1, 23, 0),
	VBI_PIL (1, 1, 0, 1),
	VBI_PIL (1, 1, 0, 59),
	VBI_PIL (1, 31, 0, 0),
	VBI_PIL (3, 31, 0, 0),
	VBI_PIL (4, 30, 0, 0),
	VBI_PIL (5, 31, 0, 0),
	VBI_PIL (6, 30, 0, 0),
	VBI_PIL (7, 31, 0, 0),
	VBI_PIL (8, 31, 0, 0),
	VBI_PIL (9, 30, 0, 0),
	VBI_PIL (10, 31, 0, 0),
	VBI_PIL (11, 30, 0, 0),
	VBI_PIL (12, 1, 0, 0),
	VBI_PIL (12, 31, 0, 0),
};

static const vbi_pil
invalid_dates [] = {
	0,
	VBI_PIL (0, 1, 0, 0),
	VBI_PIL (1, 0, 0, 0),
	VBI_PIL (1, 1, 24, 0),
	VBI_PIL (1, 1, 31, 0),
	VBI_PIL (1, 1, 0, 60),
	VBI_PIL (1, 1, 0, 63),
	VBI_PIL (2, 30, 0, 0),
	VBI_PIL (2, 31, 0, 0),
	VBI_PIL (4, 31, 0, 0),
	VBI_PIL (6, 31, 0, 0),
	VBI_PIL (9, 31, 0, 0),
	VBI_PIL (11, 31, 0, 0),
	VBI_PIL (13, 1, 0, 0),
	VBI_PIL (15, 1, 0, 0),
	VBI_PIL_TIMER_CONTROL,
	VBI_PIL_INHIBIT_TERMINATE,
	VBI_PIL_INTERRUPTION,
	VBI_PIL_CONTINUE,
	VBI_PIL_NSPV,
	VBI_PIL_END,
};

/* EN 300 231 Annex F. */
static const vbi_pil
normal_dates [] = {
	VBI_PIL (1, 1, 24, 0),
	VBI_PIL (1, 1, 31, 0),
	VBI_PIL (1, 1, 0, 60),
	VBI_PIL (1, 1, 0, 63),
	/* plus all valid_dates[] */
};

/* EN 300 231 Annex F. */
static const vbi_pil
unallocated_dates [] = {
	0,
	VBI_PIL (0, 1, 0, 0),
	VBI_PIL (15, 0, 0, 0),
	VBI_PIL (15, 0, 0, 63),
	VBI_PIL (15, 0, 27, 63),
	VBI_PIL (15, 0, 31, 0),
	VBI_PIL (15, 0, 31, 62),
	VBI_PIL (15, 31, 0, 0),
};

/* EN 300 231 Annex F. */
static const vbi_pil
indefinite_dates [] = {
	VBI_PIL (1, 0, 0, 0),
	VBI_PIL (2, 30, 0, 0),
	VBI_PIL (2, 31, 0, 0),
	VBI_PIL (4, 31, 0, 0),
	VBI_PIL (6, 31, 0, 0),
	VBI_PIL (9, 31, 0, 0),
	VBI_PIL (11, 31, 0, 0),
	VBI_PIL (13, 1, 0, 0),
	VBI_PIL (14, 1, 0, 0),
	VBI_PIL (14, 31, 31, 63),
	VBI_PIL_TIMER_CONTROL,
	VBI_PIL_INHIBIT_TERMINATE,
	VBI_PIL_INTERRUPTION,
	VBI_PIL_CONTINUE,
};

static void
assert_errno			(int			exp_errno)
{
	/* XXX later */
	exp_errno = exp_errno;
}

static void
assert_pil_from_string		(vbi_pil *		pil,
				 const char **		s,
				 vbi_bool		exp_success = TRUE)
{
	const char *s1;
	vbi_bool success;

	s1 = *s;
	*pil = 12345;

	success = _vbi_pil_from_string (pil, s);
	assert (exp_success == success);
	if (!exp_success) {
		assert (s1 == *s);
		assert (12345 == *pil);
	}
}

static void
test_pil_from_string		(void)
{
	static const struct {
		const char *		name;
		vbi_pil		pil;
	} good_pils [] = {
		{ "cont",		VBI_PIL_CONTINUE },
		{ "continue",		VBI_PIL_CONTINUE },
		{ "cOnTiNuE",		VBI_PIL_CONTINUE },
		{ "end",		VBI_PIL_END },
		{ "END",		VBI_PIL_END },
		{ "inhibit",		VBI_PIL_INHIBIT_TERMINATE },
		{ "int",		VBI_PIL_INTERRUPTION },
		{ "interruption",	VBI_PIL_INTERRUPTION },
		{ "nspv",		VBI_PIL_NSPV },
		{ "rit",		VBI_PIL_INHIBIT_TERMINATE },
		{ "terminate",		VBI_PIL_INHIBIT_TERMINATE },
		{ "tc",			VBI_PIL_TIMER_CONTROL },
		{ "timer",		VBI_PIL_TIMER_CONTROL },
		{ "  \t\n timer",	VBI_PIL_TIMER_CONTROL },
		{ "00000000",		VBI_PIL (0, 0, 0, 0) },
		{ "15000000",		VBI_PIL (15, 0, 0, 0) },
		{ "00310000",		VBI_PIL (0, 31, 0, 0) },
		{ "00003100",		VBI_PIL (0, 0, 31, 0) },
		{ "00000063",		VBI_PIL (0, 0, 0, 63) },
		{ "\n \t  11-12T13:14",	VBI_PIL (11, 12, 13, 14) },
		{ "1112",		VBI_PIL (0, 0, 11, 12) },
		{ "11:12",		VBI_PIL (0, 0, 11, 12) },
		{ "11-12T13:14",	VBI_PIL (11, 12, 13, 14) },
		{ "1112T13:14",		VBI_PIL (11, 12, 13, 14) },
		{ "111213:14",		VBI_PIL (11, 12, 13, 14) },
		{ "1112T1314",		VBI_PIL (11, 12, 13, 14) },
		{ "11121314",		VBI_PIL (11, 12, 13, 14) },
		{ "11-1213:14",		VBI_PIL (11, 12, 13, 14) },
		{ "11-121314",		VBI_PIL (11, 12, 13, 14) },
		{ "11-12T1314",		VBI_PIL (11, 12, 13, 14) }
	};
	static const struct {
		const char *		name;
		vbi_pil		pil;
		int			c;
	} trailing_garbage [] = {
		{ "int foo",		VBI_PIL_INTERRUPTION,	   ' ' },
		{ "int-foo",		VBI_PIL_INTERRUPTION,	   '-' },
		{ "int\n ",		VBI_PIL_INTERRUPTION,	   '\n' },
		{ "int\t\n",		VBI_PIL_INTERRUPTION,	   '\t' },
		{ "00-00T00:00 ",	VBI_PIL (0, 0, 0, 0),	   ' ' },
		{ "00-00T00:00a",	VBI_PIL (0, 0, 0, 0),	   'a' },
		{ "00000000:00",	VBI_PIL (0, 0, 0, 0),	   ':' },
		{ "01-02T03:04:00",	VBI_PIL (1, 2, 3, 4),	   ':' },
		{ "1413:2016",		VBI_PIL (0, 0, 14, 13),   ':' },
		{ "14:132016",		VBI_PIL (0, 0, 14, 13),   '2' },
		{ "1413+2016",		VBI_PIL (0, 0, 14, 13),   '+' },
		{ "141320167",		VBI_PIL (14, 13, 20, 16), '7' },
		{ "2004-01-01T01:01",	VBI_PIL (0, 0, 20, 4),    '-' }
	};
	static const char *bad_pils [] = {
		"c",
		"intc",
		"endfish",
		"tc2",
		"T",
		"8nspv",
		"0",
		"1",
		"11",
		"-11",
		"+11",
		"111",
		"11-11",
		"1111T",
		"11T11",
		"1-111",
		"111:1",
		"1-1T1:1",
		"11111",
		"11-111",
		"11-1111",
		"111111",
		"111111",
		"1111111",
		"11-1111",
		"11-11111",
		"111111",
		"1111111",
		"11-11 11:11",
		"11-11t11:11",
		"11--111111",
		"11+111111",
		"11T+111111",
		"11T-111111",
		"111111T11",
		"111111-11",
		"200401010101",
		"16000000",
		"99000000",
		"00320000",
		"00990000",
		"00003200",
		"00009900",
		"00000064",
		"00000099"
	};
	vbi_pil p;
	const char *s;
	unsigned int i;

	for (i = 0; i < N_ELEMENTS (good_pils); ++i) {
		s = good_pils[i].name;
		assert_pil_from_string (&p, &s);
		assert (p == good_pils[i].pil);
		assert (0 == *s);
	}

	for (i = 0; i < N_ELEMENTS (trailing_garbage); ++i) {
		s = trailing_garbage[i].name;
		assert_pil_from_string (&p, &s);
		assert (p == trailing_garbage[i].pil);
		assert (*s == trailing_garbage[i].c);
	}

	for (i = 0; i < N_ELEMENTS (bad_pils); ++i) {
		s = bad_pils[i];
		assert_pil_from_string (&p, &s, FALSE);
	}
}

static void
assert_pty_validity_window	(time_t *		begin,
				 time_t *		end,
				 time_t			start,
				 const char *		tz,
				 vbi_bool		exp_success = TRUE,
				 int			exp_errno = 0,
				 time_t			exp_begin = ANY_TIME,
				 time_t			exp_end = ANY_TIME)
{
	vbi_bool success;

	*begin = 123;
	*end = 456;

	success = vbi_pty_validity_window (begin, end, start, tz);
	assert (exp_success == success);
	if (exp_success) {
		if (ANY_TIME != exp_begin)
			assert (exp_begin == *begin);
		if (ANY_TIME != exp_end)
			assert (exp_end == *end);
	} else {
		assert_errno (exp_errno);
		assert (123 == *begin);
		assert (456 == *end);
	}
}

static void
assert_pty_validity_window	(time_t			start,
				 const char *		tz,
				 vbi_bool		exp_success = TRUE,
				 int			exp_errno = 0,
				 time_t			exp_begin = ANY_TIME,
				 time_t			exp_end = ANY_TIME)
{
	time_t begin1, end1;

	assert_pty_validity_window (&begin1, &end1, start, tz,
				    exp_success, exp_errno,
				    exp_begin, exp_end);
}

static void
test_pty_validity_window	(void)
{
	time_t t;

	assert_pty_validity_window ((time_t) -1, "UTC");
	assert_pty_validity_window ((time_t) -1, "CET");

	/* GNU libc setenv() doesn't seem to care. "" may be a
	   shorthand for UTC. */
	if (0) {
		t = ztime ("20010101T000000");
		assert_pty_validity_window (t, "", FALSE, EINVAL);
		assert_pty_validity_window (t, "CET=", FALSE, EINVAL);
	}

	if (TIME_MIN >= 0) {
		/* 'begin' and 'end' cannot be smaller than 'time'
		   (unless there was a negative DST offset). */
		assert_pty_validity_window (TIME_MIN, "UTC",
					    TRUE, 0, TIME_MIN);
		assert_pty_validity_window (TIME_MIN, "CET",
					    TRUE, 0, TIME_MIN);
	}

	if (TIME_MAX <= 0x7FFFFFFF) {
		t = TIME_MAX - 30 * 24 * 60 * 60;
		assert_pty_validity_window (t, "UTC", TRUE, 0, t);
		assert_pty_validity_window (t, "CET", TRUE, 0, t);
		t = TIME_MAX - 26 * 24 * 60 * 60;
		assert_pty_validity_window (t, "UTC", FALSE, EOVERFLOW);
		assert_pty_validity_window (t, "CET", FALSE, EOVERFLOW);
		assert_pty_validity_window (TIME_MAX, "UTC",
					    FALSE, EOVERFLOW);
		assert_pty_validity_window (TIME_MAX, "CET",
					    FALSE, EOVERFLOW);
	}

	t = ztime ("20010101T000000");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20010130T040000"));
	t = ztime ("20010415T111111");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20010514T040000"));
	t = ztime ("20010630T222222");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20010729T040000"));
	t = ztime ("20010701T031415");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20010730T040000"));
	t = ztime ("20010915T150901");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20011014T040000"));
	t = ztime ("20011231T235959");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20020129T040000"));

	/* Regular year. */
	t = ztime ("20020131T000000");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20020301T040000"));
	/* Leap year. */
	t = ztime ("20040131T000000");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20040229T040000"));
	t = ztime ("20040229T000000");
	assert_pty_validity_window (t, "UTC", TRUE, 0, t,
				    ztime ("20040329T040000"));

	/* 2004-03-28 01:00 UTC: London local time changes
	   from 01:00 GMT to 02:00 BST. */

	/* Validity window entirely in GMT zone. */
	t = ztime ("20040227T235959");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20040327T040000"));
	/* Validity window begins in GMT zone, ends in BST zone. */
	t = ztime ("20040228T000000");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20040328T030000"));
	t = ztime ("20040328T010000");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20040426T030000"));
	t = ztime ("20040328T020000");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20040426T030000"));
	/* Validity window entirely in BST zone. */
	t = ztime ("20040329T000000");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20040427T030000"));

	/* 2004-10-31 01:00 UTC: London local time changes
	   from 02:00 BST to 01:00 GMT. */

	/* Validity window entirely in BST zone. */
	t = ztime ("20041001T225959"); /* = 2004-10-01 23:59:59 BST */
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20041030T030000"));
	t = ztime ("20041001T235959"); /* = 2004-10-02 00:59:59 BST */
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20041031T040000"));
	/* Validity window begins in BST zone, ends in GMT zone. */
	t = ztime ("20041002T00000");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20041031T040000"));
	t = ztime ("20041031T01000");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20041129T040000"));
	t = ztime ("20041031T02000");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20041129T040000"));
	/* Validity window entirely in GMT zone. */
	t = ztime ("20041101T000000");
	assert_pty_validity_window (t, "Europe/London", TRUE, 0, t,
				    ztime ("20041130T040000"));
}

static void
assert_pil_validity_window	(time_t *		begin,
				 time_t *		end,
				 vbi_pil		pil,
				 time_t			start,
				 const char *		tz,
				 vbi_bool		exp_success = TRUE,
				 int			exp_errno = 0,
				 time_t			exp_begin = ANY_TIME,
				 time_t			exp_end = ANY_TIME)
{
	time_t begin2, end2;
	vbi_bool success;
	int seconds_east;

	*begin = 123;
	*end = 456;

	success = vbi_pil_validity_window (begin, end,
					    pil, start, tz);
	assert (exp_success == success);
	if (exp_success) {
		if (ANY_TIME != exp_begin)
			assert (exp_begin == *begin);
		if (ANY_TIME != exp_end)
			assert (exp_end == *end);
	} else {
		assert_errno (exp_errno);
		assert (123 == *begin);
		assert (456 == *end);
	}

	if (NULL == tz) {
		return;
	} else if (0 == strcmp (tz, "UTC")) {
		seconds_east = 0;
	} else if (0 == strcmp (tz, "CET")) {
		if (VBI_PIL_MONTH (pil) >= 3
		    && VBI_PIL_MONTH (pil) <= 10) {
			/* GNU libc mktime() changes to CEST if DST is
			   in effect at the given date. Is that
			   expected? */
			return;
		} else {
			seconds_east = 3600;
		}
	} else {
		return;
	}

	begin2 = 123;
	end2 = 456;

	success = vbi_pil_lto_validity_window (&begin2, &end2,
						pil, start, seconds_east);
	assert (exp_success == success);
	if (!exp_success)
		assert_errno (exp_errno);
	assert (begin2 == *begin);
	assert (end2 == *end);
}

static void
assert_pil_validity_window	(vbi_pil		pil,
				 time_t			start,
				 const char *		tz,
				 vbi_bool		exp_success = TRUE,
				 int			exp_errno = 0,
				 time_t			exp_begin = ANY_TIME,
				 time_t			exp_end = ANY_TIME)
{
	time_t begin1, end1;

	assert_pil_validity_window (&begin1, &end1,
				    pil, start, tz,
				    exp_success, exp_errno,
				    exp_begin, exp_end);
}

static void
test_pil_validity_window	(void)
{
	struct tm tm_min, tm_max;
	vbi_pil p, p1;
	time_t t, t1;
	time_t begin, end;
	time_t begin2, end2;
	unsigned int i;

	p1 = VBI_PIL (1, 1, 0, 0);
	t1 = ztime ("20010101T000000");

	for (i = 0; i < N_ELEMENTS (valid_dates); ++i) {
		vbi_pil p = valid_dates[i];

		assert_pil_validity_window (p, t1, "UTC");
		assert_pil_validity_window (p, t1, "CET");
		assert_pil_validity_window (p, t1, NULL);
	}

	for (i = 0; i < N_ELEMENTS (normal_dates); ++i) {
		vbi_pil p = normal_dates[i];

		assert_pil_validity_window (p, t1, "UTC");
		assert_pil_validity_window (p, t1, "CET");
		assert_pil_validity_window (p, t1, NULL);
	}

	for (i = 0; i < N_ELEMENTS (valid_dates); ++i) {
		vbi_pil p = valid_dates[i];
		int j;

		for (j = -13 * 3600; j <= +13 * 3600; j += 3744) {
			char tz[16];
			time_t begin2, end2;

			snprintf (tz, sizeof (tz), "UTC%c%02u:%02u:%02u",
				  (j < 0) ? '+' : '-',
				  abs (j) / 3600,
				  abs (j) / 60 % 60,
				  abs (j) % 60);
			assert (TRUE == vbi_pil_validity_window
				(&begin, &end, p, t1, tz));
			assert (TRUE == vbi_pil_lto_validity_window
				(&begin2, &end2, p, t1, j));
			assert (begin == begin2);
			assert (end == end2);
		}
	}

	for (i = 0; i < N_ELEMENTS (unallocated_dates); ++i) {
		vbi_pil p = unallocated_dates[i];

		assert_pil_validity_window (p, t1, "UTC", FALSE, EINVAL);
		assert_pil_validity_window (p, t1, "CET", FALSE, EINVAL);
		assert_pil_validity_window (p, t1, NULL, FALSE, EINVAL);
	}

	for (i = 0; i < N_ELEMENTS (indefinite_dates); ++i) {
		vbi_pil p = indefinite_dates[i];

		assert_pil_validity_window (p, t1, "UTC", TRUE, 0,
					    TIME_MIN, TIME_MAX);
		assert_pil_validity_window (p, t1, "CET", TRUE, 0,
					    TIME_MIN, TIME_MAX);
		assert_pil_validity_window (p, t1, NULL, TRUE, 0,
					    TIME_MIN, TIME_MAX);
	}

	/* Invalid day in year 2001, therefore indefinite time window. */
	assert_pil_validity_window (VBI_PIL (2, 29, 12, 0), t1, "UTC",
				    TRUE, 0, TIME_MIN, TIME_MAX);
	assert_pil_validity_window (VBI_PIL (2, 29, 12, 0), t1, "CET",
				    TRUE, 0, TIME_MIN, TIME_MAX);
	/* Valid day in year 2004. */
	assert_pil_validity_window (VBI_PIL (2, 29, 12, 0),
				    ztime ("20040101T000000"), "UTC");

	assert_pil_validity_window (p1, (time_t) -1, "UTC");
	assert_pil_validity_window (p1, (time_t) -1, "CET");

	/* GNU libc setenv() doesn't seem to care. "" may be a shorthand
	   for UTC. */
	if (0) {
		assert_pil_validity_window (p1, t1, "", FALSE, EINVAL);
		assert_pil_validity_window (p1, t1, "CET=", FALSE, EINVAL);
	}

	if (TIME_MIN >= 0) {
		t = TIME_MIN;
		assert (NULL != gmtime_r (&t, &tm_min));
		assert (t == timegm (&tm_min));
		p = VBI_PIL (tm_min.tm_mon + 1, tm_min.tm_mday,
			     tm_min.tm_hour, /* minute */ 59),
			assert_pil_validity_window (p, TIME_MIN, "UTC",
						    FALSE, EOVERFLOW);
	}

	if (TIME_MAX <= 0x7FFFFFFF) {
		t = TIME_MAX;
		assert (NULL != gmtime_r (&t, &tm_max));
		assert (t == timegm (&tm_max));
		p = VBI_PIL (tm_max.tm_mon + 1, tm_max.tm_mday,
			     tm_max.tm_hour, 0),
			assert_pil_validity_window (p, TIME_MAX, "UTC",
						    FALSE, EOVERFLOW);
	}

	t = ztime ("20010101T000000");
	assert_pil_validity_window (VBI_PIL (6, 30, 23, 59), t,
				    "UTC", TRUE, 0,
				    ztime ("20010630T000000"),
				    ztime ("20010701T040000"));
	assert_pil_validity_window (VBI_PIL (7, 1, 0, 0), t,
				    "UTC", TRUE, 0,
				    ztime ("20000630T200000"),
				    ztime ("20000702T040000"));
	t = ztime ("20010415T000000");
	assert_pil_validity_window (VBI_PIL (7, 1, 0, 0), t,
				    "UTC", TRUE, 0,
				    ztime ("20010630T200000"),
				    ztime ("20010702T040000"));
	t = ztime ("20010630T000000");
	assert_pil_validity_window (VBI_PIL (7, 1, 23, 59), t,
				    "UTC", TRUE, 0,
				    ztime ("20010701T000000"),
				    ztime ("20010702T040000"));
	assert_pil_validity_window (VBI_PIL (12, 31, 23, 59), t,
				    "UTC", TRUE, 0,
				    ztime ("20001231T000000"),
				    ztime ("20010101T040000"));
	assert_pil_validity_window (VBI_PIL (1, 1, 0, 0), t,
				    "UTC", TRUE, 0,
				    ztime ("20001231T200000"),
				    ztime ("20010102T040000"));
	t = ztime ("20010701T000000");
	assert_pil_validity_window (VBI_PIL (1, 1, 0, 0), t,
				    "UTC", TRUE, 0,
				    ztime ("20001231T200000"),
				    ztime ("20010102T040000"));
	assert_pil_validity_window (VBI_PIL (12, 31, 23, 59), t,
				    "UTC", TRUE, 0,
				    ztime ("20011231T000000"),
				    ztime ("20020101T040000"));
	t = ztime ("20010915T000000");
	assert_pil_validity_window (VBI_PIL (1, 1, 0, 0), t,
				    "UTC", TRUE, 0,
				    ztime ("20011231T200000"),
				    ztime ("20020102T040000"));
	t = ztime ("20011231T000000");
	assert_pil_validity_window (VBI_PIL (1, 1, 0, 0), t,
				    "UTC", TRUE, 0,
				    ztime ("20011231T200000"),
				    ztime ("20020102T040000"));
	assert_pil_validity_window (VBI_PIL (6, 30, 23, 59), t,
				    "UTC", TRUE, 0,
				    ztime ("20010630T000000"),
				    ztime ("20010701T040000"));
	assert_pil_validity_window (VBI_PIL (7, 1, 0, 0), t,
				    "UTC", TRUE, 0,
				    ztime ("20010630T200000"),
				    ztime ("20010702T040000"));

	/* 2004-03-28 01:00 UTC: London local time changes
	   from 01:00 GMT to 02:00 BST. */
	t = ztime ("20040301T000000");

	/* Validity window entirely in GMT zone. */
	assert_pil_validity_window (VBI_PIL (3, 26, 23, 59), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040326T000000"),
				    ztime ("20040327T040000"));
	/* Validity window begins in GMT zone, ends in BST zone. */
	assert_pil_validity_window (VBI_PIL (3, 27, 0, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040326T200000"),
				    ztime ("20040328T030000"));
	assert_pil_validity_window (VBI_PIL (3, 27, 23, 59), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040327T000000"),
				    ztime ("20040328T030000"));
	assert_pil_validity_window (VBI_PIL (3, 28, 0, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040327T200000"),
				    ztime ("20040329T030000"));
	assert_pil_validity_window (VBI_PIL (3, 28, 1, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040327T200000"),
				    ztime ("20040329T030000"));
	assert_pil_validity_window (VBI_PIL (3, 28, 2, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040327T200000"),
				    ztime ("20040329T030000"));
	assert_pil_validity_window (VBI_PIL (3, 28, 3, 59), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040327T200000"),
				    ztime ("20040329T030000"));
	/* Between 04:00-23:59 local time the validity window begins
	   at 00:00 local time of the same day, which is still 00:00
	   UTC. */
	assert_pil_validity_window (VBI_PIL (3, 28, 4, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040328T000000"),
				    ztime ("20040329T030000"));
	/* Validity window entirely in BST zone. */
	assert_pil_validity_window (VBI_PIL (3, 29, 0, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20040328T190000"),
				    ztime ("20040330T030000"));

	/* 2004-10-31 01:00 UTC: London local time changes
	   from 02:00 BST to 01:00 GMT. */
	t = ztime ("20041001T000000");

	/* Validity window entirely in BST zone. */
	assert_pil_validity_window (VBI_PIL (10, 29, 0, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041028T190000"),
				    ztime ("20041030T030000"));
	assert_pil_validity_window (VBI_PIL (10, 29, 23, 59), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041028T230000"),
				    ztime ("20041030T030000"));
	/* Validity window begins in BST zone, ends in GMT zone. */
	assert_pil_validity_window (VBI_PIL (10, 30, 0, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041029T190000"),
				    ztime ("20041031T040000"));
	assert_pil_validity_window (VBI_PIL (10, 30, 23, 59), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041029T230000"),
				    ztime ("20041031T040000"));
	assert_pil_validity_window (VBI_PIL (10, 31, 0, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041030T190000"),
				    ztime ("20041101T040000"));
	assert_pil_validity_window (VBI_PIL (10, 31, 1, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041030T190000"),
				    ztime ("20041101T040000"));
	assert_pil_validity_window (VBI_PIL (10, 31, 2, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041030T190000"),
				    ztime ("20041101T040000"));
	assert_pil_validity_window (VBI_PIL (10, 31, 3, 59), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041030T190000"),
				    ztime ("20041101T040000"));
	/* Between 04:00-23:59 local time the validity window begins
	   at 00:00 local time of the same day, which is still 23:00
	   UTC. */
	assert_pil_validity_window (VBI_PIL (10, 31, 4, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041030T230000"),
				    ztime ("20041101T040000"));
	/* Validity window entirely in GMT zone. */
	assert_pil_validity_window (VBI_PIL (11, 1, 0, 0), t,
				    "Europe/London", TRUE, 0,
				    ztime ("20041031T200000"),
				    ztime ("20041102T040000"));

	assert (TRUE == vbi_pty_validity_window
		(&begin, &end, t1, "UTC"));
	assert (begin == t1);
	assert (TRUE == vbi_pil_validity_window
		(&begin2, &end2, VBI_PIL_NSPV, t1, "UTC"));
	assert (begin2 == begin);
	assert (end2 == end);
	assert (TRUE == vbi_pil_lto_validity_window
		(&begin2, &end2, VBI_PIL_NSPV, t1, 0));
	assert (begin2 == begin);
	assert (end2 == end);
	/* 'pil' is assumed to be a time in the UTC + 'seconds_east'
	   zone, but seconds_east does not apply if pil is
	   NSPV. Instead '*begin' is defined only by 'start' here,
	   which is already given in UTC. vbi_pty_validity_window()
	   *may* use 'tz' to correct the validity window for DST, but
	   that is impossible with seconds_east. */
	assert (TRUE == vbi_pil_lto_validity_window
		(&begin2, &end2, VBI_PIL_NSPV, t1, 12345));
	assert (begin2 == begin);
	assert (end2 == end);

	assert (TRUE == vbi_pty_validity_window
		(&begin, &end, t1, "UTC+2"));
	assert (TRUE == vbi_pil_validity_window
		(&begin2, &end2, VBI_PIL_NSPV, t1, "UTC+2"));
	assert (begin2 == begin);
	assert (end2 == end);
}

static void
assert_pil_to_time		(vbi_pil		pil,
				 time_t			start,
				 const char *		tz,
				 time_t			exp_result = ANY_TIME,
				 int			exp_errno = 0)
{
	time_t result;
	int seconds_east;

	result = vbi_pil_to_time (pil, start, tz);
	if (ANY_TIME == exp_result) {
		assert ((time_t) -1 != result);
	} else if ((time_t) -1 == exp_result) {
		assert_errno (exp_errno);
	}

	if (NULL == tz) {
		return;
	} else if (0 == strcmp (tz, "UTC")) {
		seconds_east = 0;
	} else if (0 == strcmp (tz, "CET")) {
		if (VBI_PIL_MONTH (pil) >= 3
		    && VBI_PIL_MONTH (pil) <= 10) {
			/* GNU libc mktime() changes to CEST if DST is
			   in effect at the given date. Is that
			   expected? */
			return;
		} else {
			seconds_east = 3600;
		}
	} else {
		return;
	}

	result = vbi_pil_lto_to_time (pil, start, seconds_east);
	if (ANY_TIME == exp_result) {
		assert ((time_t) -1 != result);
	} else if ((time_t) -1 == exp_result) {
		assert_errno (exp_errno);
	}
}

static void
assert_pil_to_time		(vbi_pil		pil,
				 time_t			start,
				 unsigned int		exp_year)
{
	char tz[32];

	snprintf (tz, sizeof (tz), "%04u%02u%02uT%02u%02u00",
		  exp_year,
		  VBI_PIL_MONTH (pil),
		  VBI_PIL_DAY (pil),
		  VBI_PIL_HOUR (pil),
		  VBI_PIL_MINUTE (pil));

	assert_pil_to_time (pil, start, "UTC", ztime (tz));
}

static void
test_pil_to_time		(void)
{
	struct tm tm_min, tm_max;
	vbi_pil p, p1;
	time_t t, t1;
	unsigned int i;

	p1 = VBI_PIL (1, 1, 0, 0);
	t1 = ztime ("20010101T000000");

	for (i = 0; i < N_ELEMENTS (valid_dates); ++i) {
		vbi_pil p = valid_dates[i];

		assert_pil_to_time (p, t1, "UTC");
		assert_pil_to_time (p, t1, "CET");
		assert_pil_to_time (p, t1, (const char *) NULL);
	}

	for (i = 0; i < N_ELEMENTS (valid_dates); ++i) {
		vbi_pil p = valid_dates[i];
		int j;

		for (j = -13 * 3600; j <= +13 * 3600; j += 3744) {
			char tz[16];

			snprintf (tz, sizeof (tz), "UTC%c%02u:%02u:%02u",
				  (j < 0) ? '+' : '-',
				  abs (j) / 3600,
				  abs (j) / 60 % 60,
				  abs (j) % 60);
			t = vbi_pil_to_time (p, t1, tz);
			assert ((time_t) -1 != t);
			assert (t == vbi_pil_lto_to_time (p, t1, j));
		}
	}

	for (i = 0; i < N_ELEMENTS (invalid_dates); ++i) {
		vbi_pil p = invalid_dates[i];

		assert_pil_to_time (p, t1, "UTC",
				    /* exp_result */ -1, EINVAL);
		assert_pil_to_time (p, t1, "CET",
				    /* exp_result */ -1, EINVAL);
		assert_pil_to_time (p, t1, NULL,
				    /* exp_result */ -1, EINVAL);
	}

	assert_pil_to_time (VBI_PIL (2, 29, 12, 0), t1, "UTC",
			    -1, EINVAL);
	assert_pil_to_time (VBI_PIL (2, 29, 12, 0), t1, "CET",
			    -1, EINVAL);
	assert_pil_to_time (VBI_PIL (2, 29, 12, 0),
			    ztime ("20040101T000000"), "UTC",
			    ztime ("20040229T000000"));

	/* GNU libc setenv() doesn't seem to care. "" may be a shorthand
	   for UTC. */
	if (0) {
		assert_pil_to_time (p1, t1, "",
				    /* exp_result */ -1, EINVAL);
		assert_pil_to_time (p1, t1, "CET=",
				    /* exp_result */ -1, EINVAL);
	}

	assert_pil_to_time (p1, (time_t) -1, "UTC");
	assert_pil_to_time (p1, (time_t) -1, "CET");

	if (TIME_MIN >= 0) {
		t = TIME_MIN;

		assert (NULL != gmtime_r (&t, &tm_min));

		assert (t == timegm (&tm_min));
		p = VBI_PIL (tm_min.tm_mon + 1, tm_min.tm_mday,
			     tm_min.tm_hour, 59),
			assert_pil_to_time (p, TIME_MIN, "UTC");
		assert_pil_to_time (p, TIME_MIN, "UTC-1",
				    /* exp_result */ -1, EOVERFLOW);

		assert ((time_t) -1 == vbi_pil_lto_to_time
			(p, TIME_MIN, /* seconds_east */ -3600));
		assert_errno (EOVERFLOW);
		if (tm_min.tm_hour > 0) {
			assert_pil_to_time (VBI_PIL (tm_min.tm_mon + 1,
						     tm_min.tm_mday,
						     tm_min.tm_hour - 1,
						     /* minute */ 59),
					    TIME_MIN, "UTC",
					    /* exp_result */ -1,
					    EOVERFLOW);
		} else if (tm_min.tm_mday > 1) {
			assert_pil_to_time (VBI_PIL (tm_min.tm_mon + 1,
						     tm_min.tm_mday - 1,
						     tm_min.tm_hour, 59),
					    TIME_MIN, "UTC",
					    /* exp_result */ -1,
					    EOVERFLOW);
		} else if (tm_min.tm_mon > 0) {
			assert_pil_to_time (VBI_PIL (tm_min.tm_mon + 1 - 1,
						     tm_min.tm_mday - 1,
						     tm_min.tm_hour, 59),
					    TIME_MIN, "UTC",
					    /* exp_result */ -1,
					    EOVERFLOW);
		}
	}

	if (TIME_MAX <= 0x7FFFFFFF) {
		/* -1 because GNU libc timegm() appears to clamp
		   against TIME_MAX, which is catched by libzvbi. */
		t = TIME_MAX - 1;
		assert (NULL != gmtime_r (&t, &tm_max));
		assert (t == timegm (&tm_max));
		p = VBI_PIL (tm_max.tm_mon + 1, tm_max.tm_mday,
			     tm_max.tm_hour, 0),
			assert_pil_to_time (p, TIME_MAX, "UTC");
		assert_pil_to_time (p, TIME_MAX, "UTC+1",
				    /* exp_result */ -1, EOVERFLOW);

		assert ((time_t) -1 == vbi_pil_lto_to_time
			(p, TIME_MAX, /* seconds_east */ 3600));
		assert_errno (EOVERFLOW);
		if (tm_max.tm_hour < 23) {
			assert_pil_to_time (VBI_PIL (tm_max.tm_mon + 1,
						     tm_max.tm_mday,
						     tm_max.tm_hour + 1,
						     /* minute */ 0),
					    TIME_MAX, "UTC",
					    /* exp_result */ -1,
					    EOVERFLOW);
		} else if (tm_max.tm_mday < 28) {
			assert_pil_to_time (VBI_PIL (tm_max.tm_mon + 1,
						     tm_max.tm_mday + 1,
						     tm_max.tm_hour, 0),
					    TIME_MAX, "UTC",
					    /* exp_result */ -1,
					    EOVERFLOW);
		} else if (tm_max.tm_mon < 11) {
			assert_pil_to_time (VBI_PIL (tm_max.tm_mon + 1 + 1,
						     tm_max.tm_mday + 1,
						     tm_max.tm_hour, 0),
					    TIME_MAX, "UTC",
					    /* exp_result */ -1,
					    EOVERFLOW);
		}
	}

	t = ztime ("20010101T000000");
	assert_pil_to_time (VBI_PIL (1, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (6, 30, 23, 59), t, 2001);
	assert_pil_to_time (VBI_PIL (7, 1, 0, 0), t, 2000);
	assert_pil_to_time (VBI_PIL (12, 31, 23, 59), t, 2000);
	t = ztime ("20010415T000000");
	assert_pil_to_time (VBI_PIL (1, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (6, 30, 23, 59), t, 2001);
	assert_pil_to_time (VBI_PIL (7, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (12, 31, 23, 59), t, 2000);
	t = ztime ("20010630T000000");
	assert_pil_to_time (VBI_PIL (1, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (6, 30, 23, 59), t, 2001);
	assert_pil_to_time (VBI_PIL (7, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (12, 31, 23, 59), t, 2000);
	t = ztime ("20010701T000000");
	assert_pil_to_time (VBI_PIL (1, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (6, 30, 23, 59), t, 2001);
	assert_pil_to_time (VBI_PIL (7, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (12, 31, 23, 59), t, 2001);
	t = ztime ("20010915T000000");
	assert_pil_to_time (VBI_PIL (1, 1, 0, 0), t, 2002);
	assert_pil_to_time (VBI_PIL (6, 30, 23, 59), t, 2001);
	assert_pil_to_time (VBI_PIL (7, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (12, 31, 23, 59), t, 2001);
	t = ztime ("20011231T000000");
	assert_pil_to_time (VBI_PIL (1, 1, 0, 0), t, 2002);
	assert_pil_to_time (VBI_PIL (6, 30, 23, 59), t, 2001);
	assert_pil_to_time (VBI_PIL (7, 1, 0, 0), t, 2001);
	assert_pil_to_time (VBI_PIL (12, 31, 23, 59), t, 2001);

	/* GMT */
	assert (ztime ("20010215T200000")
		== vbi_pil_to_time (VBI_PIL (2, 15, 20, 0), t1,
				    "Europe/London"));
	assert (ztime ("20010215T200000")
		== vbi_pil_lto_to_time (VBI_PIL (2, 15, 20, 0), t1, 0));
	/* CET (UTC + 1h) */
	assert (ztime ("20010215T190000")
		== vbi_pil_to_time (VBI_PIL (2, 15, 20, 0), t1,
				    "Europe/Paris"));
	assert (ztime ("20010215T190000")
		== vbi_pil_lto_to_time (VBI_PIL (2, 15, 20, 0), t1, 3600));
	/* CEST (UTC + 2h) */
	assert (ztime ("20010715T180000")
		== vbi_pil_to_time (VBI_PIL (7, 15, 20, 0),
				    ztime ("20010701T000000"),
				    "Europe/Paris"));
	/* CET because PIL month 2; year 2001 because 8 - 2 <= 6. */
	assert (ztime ("20010215T190000")
		== vbi_pil_to_time (VBI_PIL (2, 15, 20, 0),
				    ztime ("20010831T210000"),
				    "Europe/Paris"));
	/* CET because PIL month 2; year 2002 because 'start' is
	   already 2001-09-01 01:00 in CEST zone. */
	assert (ztime ("20020215T190000")
		== vbi_pil_to_time (VBI_PIL (2, 15, 20, 0),
				    ztime ("20010831T230000"),
				    "Europe/Paris"));
	
	/* XXX Maybe other DST conventions should be tested:
	   http://en.wikipedia.org/wiki/Daylight_saving_time_around_the_world
	*/
}

static void
test_pil_is_valid_date		(void)
{
	unsigned int i;

	for (i = 0; i < N_ELEMENTS (valid_dates); ++i) {
		assert (vbi_pil_is_valid_date (valid_dates[i]));
	}

	assert (vbi_pil_is_valid_date (VBI_PIL (2, 29, 0, 0)));

	for (i = 0; i < N_ELEMENTS (invalid_dates); ++i) {
		assert (!vbi_pil_is_valid_date (invalid_dates[i]));
	}

	assert (vbi_pil_is_valid_date (VBI_PIL (1, 1, 0, 0) | ~max_pil));
}

int
main				(void)
{
	test_pil_is_valid_date ();
	test_pil_to_time ();
	test_pil_validity_window ();
	test_pty_validity_window ();
	test_pil_from_string ();

	return 0;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
