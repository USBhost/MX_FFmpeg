/*
 *  libzvbi - Program Delivery Control
 *
 *  Copyright (C) 2000-2004 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id: pdc.c,v 1.4 2009/03/23 01:30:33 mschimek Exp $ */

#include "../site_def.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <float.h>		/* FLT_MAX, DBL_MAX */
#include <errno.h>

#include "misc.h"
#include "hamm.h"		/* vbi_unpar8() */
#include "bcd.h"		/* vbi_is_bcd() */
#include "pdc.h"
#include "conv.h"

/**
 * @addtogroup ProgramID VPS/PDC Program ID
 * @ingroup LowDec
 * @brief Functions to decode VPS/PDC Program IDs and helper functions.
 *
 * Program IDs are transmitted by networks to remotely control video
 * recorders. They can be used to
 * - start and stop recording exactly when a program starts and ends,
 *   even when the program is early or late or overruns,
 * - pause recording during a planned or unplanned interruption
 *   (you can safely assume that no station transmits a pause code
 *   during commercial breaks),
 * - keep recording a program which continues on a different channel,
 * - record a program after it has been postponed to a later date
 *   and possibly a different channel,
 * - record all episodes of a series without prior knowledge of the
 *   broadcast times,
 * - alert the user about emergency messages.
 *
 * The basic principle is to transmit a label along with the program
 * containing the originally announced start date and time. When
 * the label is no longer transmitted the program has ended. When two
 * programs on different channels are scheduled for recording the
 * recorder may have to scan the channels alternately. Better accuracy
 * than a few seconds within the actual start should not be expected.
 *
 * Libzvbi supports Program IDs transmitted in Teletext packet 8/30
 * format 2 and in VPS packets as defined in EN 300 231 "Television
 * systems; Specification of the domestic video Programme Delivery
 * Control system (PDC)", and DVB PDC descriptors as defined in EN 300
 * 468 "Specification for Service Information (SI) in DVB systems".
 * Support for XDS Current/Future Program ID packets as defined in EIA
 * 608-B "Recommended Practice for Line 21 Data Service" is planned
 * but not implemented yet.
 *
 * Program IDs are available through the low level functions
 * vbi_decode_teletext_8302_pdc(), vbi_decode_vps_pdc(),
 * vbi_decode_dvb_pdc_descriptor() and the @a vbi_decoder event @c
 * VBI_EVENT_PROG_ID.
 *
 * @example examples/pdc1.c
 * @example examples/pdc2.c
 */

/* XXX Preliminary, will not be returned to clients. */
enum {
	VBI_ERR_NO_TIME		= 0x7081900,
	VBI_ERR_INVALID_PIL,
};

/**
 * @internal
 * @param pil vbi_pil to print.
 * @param fp Destination stream.
 *
 * Prints a vbi_pil as service code or date and time string without
 * trailing newline. This is intended for debugging.
 */
void
_vbi_pil_dump			(vbi_pil		pil,
				 FILE *			fp)
{
	switch (pil) {
	case VBI_PIL_TIMER_CONTROL:
		fputs ("TC", fp);
		break;

	case VBI_PIL_INHIBIT_TERMINATE:
		fputs ("RI/T", fp);
		break;

	case VBI_PIL_INTERRUPTION:
		fputs ("INT", fp);
		break;

	case VBI_PIL_CONTINUE:
		fputs ("CONT", fp);
		break;

	case VBI_PIL_NSPV:
		/* VBI_PIL_NSPV (PDC) == VBI_PIL_END (XDS) */
		fputs ("NSPV/END", fp);
		break;

	default:
		fprintf (fp, "%05x (%02u-%02u %02u:%02u)",
			 pil,
			 VBI_PIL_MONTH (pil),
			 VBI_PIL_DAY (pil),
			 VBI_PIL_HOUR (pil),
			 VBI_PIL_MINUTE (pil));
		break;
	}
}

#if 3 != VBI_VERSION_MINOR
#  define vbi_cni_type_name(type) ""
#endif

/**
 * @internal
 * @param pid vbi_program_id structure to print.
 * @param fp Destination stream.
 *
 * Prints the contents of a vbi_program_id structure as a string
 * without trailing newline. This is intended for debugging.
 */
void
_vbi_program_id_dump		(const vbi_program_id *	pid,
				 FILE *			fp)
{
	static const char *pcs_audio [] = {
		"UNKNOWN",
		"MONO",
		"STEREO",
		"BILINGUAL"
	};

	fprintf (fp, "ch=%u cni=%04x (%s) pil=",
		 pid->channel,
		 pid->cni,
		 vbi_cni_type_name (pid->cni_type));

	_vbi_pil_dump (pid->pil, fp);

	fprintf (fp,
		 " luf=%u mi=%u prf=%u "
		 "pcs=%s pty=%02x tape_delayed=%u",
		 pid->luf, pid->mi, pid->prf,
		 pcs_audio[pid->pcs_audio],
		 pid->pty, pid->tape_delayed);
}

/**
 * @internal
 * @param pil The PIL will be stored here.
 * @param inout_s Pointer to input buffer pointer, which will be
 *   incremented by the number of bytes read. The buffer must contain
 *   a NUL-terminated ASCII or UTF-8 string.
 *
 * Converts a date of the format MM-DDThh:mm to a PIL. MM must be in
 * range 00 ... 15, DD and hh in range 00 ... 31 and mm in range 00
 * ... 63. The MM-DDT part can be omitted. In this case day and month
 * zero will be stored in @a pil. The separators -T: can be omitted.
 * Additionally the symbols "cont[inue]", "end", "inhibit",
 * "int[erruption]", "nspv", "rit", "terminate", "tc" and "timer" are
 * recognized and converted to the respective PIL service
 * code. Leading white space and upper/lower case is ignored.
 *
 * You can call the vbi_pil_is_valid_date() function to determine if
 * a valid date and time or service code has been entered.
 *
 * @returns
 * @c FALSE on syntax errors. In this case @a *pil and @a *inout_s
 * remain unmodified.
 */
vbi_bool
_vbi_pil_from_string		(vbi_pil *		pil,
				 const char **		inout_s)
{
	const char *s;
	unsigned int value[4];
	unsigned int i;
	unsigned int n_fields;
	unsigned int sep_mask;

	assert (NULL != pil);
	assert (NULL != inout_s);
	assert (NULL != *inout_s);

	s = *inout_s;

	while (isspace (*s))
		++s;

	if (!isdigit (*s)) {
		static const _vbi_key_value_pair symbols [] = {
			{ "cont",		VBI_PIL_CONTINUE },
			{ "continue",		VBI_PIL_CONTINUE },
			{ "end",		VBI_PIL_END },
			{ "inhibit",		VBI_PIL_INHIBIT_TERMINATE },
			{ "int",		VBI_PIL_INTERRUPTION },
			{ "interruption",	VBI_PIL_INTERRUPTION },
			{ "nspv",		VBI_PIL_NSPV },
			{ "rit",		VBI_PIL_INHIBIT_TERMINATE },
			{ "terminate",		VBI_PIL_INHIBIT_TERMINATE },
			{ "tc",			VBI_PIL_TIMER_CONTROL },
			{ "timer",		VBI_PIL_TIMER_CONTROL }
		};
		int n;

		if (_vbi_keyword_lookup (&n, inout_s,
					  symbols, N_ELEMENTS (symbols))) {
			*pil = n;
			return TRUE;
		} else {
			return FALSE;
		}
	}

	n_fields = 4;
	sep_mask = 0;

	for (i = 0; i < n_fields; ++i) {
		int c;

		if (!isdigit (s[0])) {
			if (2 == i && 0 == sep_mask) {
				n_fields = 2;
				break;
			}
			return FALSE;
		} else if (!isdigit (s[1])) {
			return FALSE;
		}

		value[i] = (s[0] - '0') * 10 + s[1] - '0';

		s += 2;
		c = *s;

		if (i < n_fields - 1) {
			if (0 == i && ':' == c) {
				n_fields = 2;
				sep_mask |= 1 << 2;
				++s;
			} else if ("-T:"[i] == c) {
				sep_mask |= 1 << i;
				++s;
			}
		}
	}

	if (n_fields < 4) {
		value[3] = value[1];
		value[2] = value[0];
		value[1] = 0;
		value[0] = 0;
	}

	if (unlikely (value[0] > 15
		      || (value[1] | value[2]) > 31
		      || value[3] > 63))
		return FALSE;

	*inout_s = s;

	*pil = VBI_PIL (value[0], value[1], value[2], value[3]);

	return TRUE;
}

static const uint8_t
month_days[12] = {
	31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/**
 * @param pil Program Identification Label.
 *
 * Determines if @a pil represents a valid date and time.
 *
 * Since PILs have no year field February 29th is considered valid.
 * You can find out if this date is valid in a given year with the
 * vbi_pil_to_time() function.
 *
 * 24:00 is not valid (an unreal hour) as defined in EN 300 231
 * Annex F and EIA 608-B Section 9.5.1.1.
 *
 * @returns
 * @c TRUE if @a pil represents a valid date and time, @c FALSE
 * if @a pil contains an unreal date or time (e.g. Jan 0 27:61),
 * a service code or unallocated code.
 *
 * @since 0.2.34
 */
vbi_bool
vbi_pil_is_valid_date		(vbi_pil		pil)
{
	unsigned int month;
	unsigned int day;

	month = VBI_PIL_MONTH (pil);
	day = VBI_PIL_DAY (pil);

	/* Note this also checks for zero month and day. */
	return (month - 1 < 12
		&& day - 1 < month_days[month - 1]
		&& VBI_PIL_HOUR (pil) < 24
		&& VBI_PIL_MINUTE (pil) < 60);
}

static vbi_bool
tm_mon_mday_from_pil		(struct tm *		tm,
				 vbi_pil		pil)
{
	unsigned int month0;

	month0 = VBI_PIL_MONTH (pil) - 1;

	if (month0 >= (unsigned int) tm->tm_mon + 6) {
		/* POSIX defines tm_year as int. */
		if (unlikely (tm->tm_year <= INT_MIN))
			return FALSE;
		--tm->tm_year;
	} else if (month0 + 6 < (unsigned int) tm->tm_mon) {
		if (unlikely (tm->tm_year >= INT_MAX))
			return FALSE;
		++tm->tm_year;
	}

	tm->tm_mon = month0;
	tm->tm_mday = VBI_PIL_DAY (pil);

	return TRUE;
}

static vbi_bool
is_leap_year			(unsigned int		year)
{
	if (0 != year % 4)
		return FALSE;
	else if (0 == year % 400)
		return TRUE;
	else
		return (0 != year % 100);
}

static vbi_bool
tm_leap_day_check		(const struct tm *	tm)
{
	return (1 != tm->tm_mon
		|| tm->tm_mday <= 28
		|| is_leap_year (tm->tm_year + 1900));
}

static vbi_bool
restore_tz			(char **		old_tz,
				 const char *		tz)
{
	int saved_errno;

	if (NULL == tz)
		return TRUE;

	if (NULL == *old_tz) {
		/* Errors ignored, should never fail. */
		unsetenv ("TZ");
	} else {
		if (unlikely (-1 == setenv ("TZ", *old_tz,
					    /* overwrite */ TRUE))) {
			/* ENOMEM: Out of memory. */
			saved_errno = errno;

			free (*old_tz);
			*old_tz = NULL;

			errno = saved_errno;
			return FALSE;
		}

		free (*old_tz);
		*old_tz = NULL;
	}

	tzset ();

	return TRUE;
}

static vbi_bool
change_tz			(char **		old_tz,
				 const char *		tz)
{
	const char *s;
	int saved_errno;

	*old_tz = NULL;

	s = getenv ("TZ");
	if (NULL != s) {
		*old_tz = strdup (s);
		if (unlikely (NULL == *old_tz)) {
			errno = ENOMEM;
			return FALSE;
		}
	}

	if (unlikely (-1 == setenv ("TZ", tz, /* overwrite */ TRUE))) {
		/* EINVAL: tz is "" or contains '='.
		   ENOMEM: Out of memory. */
		saved_errno = errno;

		free (*old_tz);
		*old_tz = NULL;

		errno = saved_errno;
		return FALSE;
	}

	tzset ();

	return TRUE;
}

static vbi_bool
localtime_tz			(struct tm *		tm,
				 char **		old_tz,
				 time_t			t,
				 const char *		tz)
{
	int saved_errno;

	*old_tz = NULL;

	/* Some system calls below may not set errno on failure. */
	errno = 0;

	if (NULL != tz) {
		if (unlikely (!change_tz (old_tz, tz)))
			return FALSE;
	}

	CLEAR (*tm);

	if ((time_t) -1 == t) {
		if (unlikely ((time_t) -1 == time (&t))) {
			saved_errno = errno;

			/* We do not ignore errors here because the
			   information that TZ wasn't restored seems
			   more important to me. */
			if (unlikely (!restore_tz (old_tz, tz)))
				return FALSE;

			/* time() can fail but POSIX defines no
			   error code. On Linux EFAULT is possible. */
			if (0 == saved_errno)
				errno = VBI_ERR_NO_TIME;
			else
				errno = saved_errno;
			return FALSE;
		}
	}

	if (unlikely (NULL == localtime_r (&t, tm))) {
		saved_errno = errno;

		if (unlikely (!restore_tz (old_tz, tz)))
			return FALSE;

		errno = saved_errno;
		return FALSE;
	}

	return TRUE;
}

#undef mktime
#undef timegm

time_t
_vbi_mktime			(struct tm *		tm);

/**
 * @internal
 * GNU libc mktime() appears to clamp its result, but it will be used
 * in video recording apps where malfunction is not an option. I want
 * it to fail with EOVERFLOW as POSIX suggests, so the caller can
 * detect and address the problem. Don't call this function directly,
 * we #define mktime to override the libc version.
 */
time_t
_vbi_mktime			(struct tm *		tm)
{
	time_t result = mktime (tm);

	if (unlikely (result <= TIME_MIN || result >= TIME_MAX)) {
		errno = EOVERFLOW;
		result = (time_t) -1;
	}

	return result;
}

#ifdef HAVE_TIMEGM

/**
 * @internal
 * timegm() is a GNU extension. It works like mktime(), but interprets
 * @a tm as a time in the UTC zone instead of the time zone defined by
 * the TZ environment variable. GNU libc timegm() appears to clamp the
 * result, but we want an EOVERFLOW error instead. In fact no error
 * codes are defined for timegm(), so we check for 0 == errno
 * too. Note this function is NOT THREAD SAFE because its replacement
 * is not. Don't call this function directly, we #define timegm to
 * override the libc version.
 */
time_t
_vbi_timegm			(struct tm *		tm)
{
	time_t result;

	errno = 0;
	result = timegm (tm);

	if (unlikely ((time_t) -1 == result)) {
		if (0 == errno)
			errno = EOVERFLOW;
	} else if (unlikely (result <= TIME_MIN || result >= TIME_MAX)) {
		errno = EOVERFLOW;
		result = (time_t) -1;
	}

	return result;
}

#else

time_t
_vbi_timegm			(struct tm *		tm);

/**
 * @internal
 *
 * Replacement for timegm() on non-GNU systems. Note this function is
 * NOT THREAD SAFE because the C library permits the conversion of a
 * broken-down time in an arbitrary time zone only by setting the TZ
 * environment variable. The function may also fail to restore the
 * value of TZ if insufficient memory is available. Don't call this
 * function directly, we #define timegm.
 */
time_t
_vbi_timegm			(struct tm *		tm)
{
	char *old_tz;
	int saved_errno;
	time_t result;

	if (unlikely (!change_tz (&old_tz, "UTC")))
		return (time_t) -1;

	result = mktime (tm);
	if (unlikely (result <= TIME_MIN
		      || result >= TIME_MAX)) {
		saved_errno = EOVERFLOW;
		result = (time_t) -1;
	} else {
		saved_errno = errno;
	}

	if (unlikely (!restore_tz (&old_tz, "UTC")))
		return (time_t) -1;

	errno = saved_errno;
	return result;
}

#endif /* !HAVE_TIMEGM */

#define mktime _vbi_mktime
#define timegm _vbi_timegm

static time_t
valid_pil_lto_to_time		(vbi_pil		pil,
				 time_t			start,
				 int			seconds_east)
{
	struct tm tm;

	/* Some system calls below may not set errno on failure, but
	   we must distinguish those errors from VBI_ERR_INVALID_PIL
	   for valid_pil_lto_validity_window(). */
	errno = 0;

	CLEAR (tm);

	if ((time_t) -1 == start) {
		if (unlikely ((time_t) -1 == time (&start))) {
			/* time() can fail but POSIX defines no
			   error code. On Linux EFAULT is possible. */
			if (0 == errno)
				errno = VBI_ERR_NO_TIME;
			return (time_t) -1;
		}
	}

	if (seconds_east < 0) {
		/* Note start can be negative. */
		if (unlikely (start < -seconds_east)) {
			errno = EOVERFLOW;
			return (time_t) -1;
		}
	} else {
		if (unlikely (start > TIME_MAX - seconds_east)) {
			errno = EOVERFLOW;
			return (time_t) -1;
		}
	}

	start += seconds_east;

	if (unlikely (NULL == gmtime_r (&start, &tm)))
		return (time_t) -1;

	if (unlikely (!tm_mon_mday_from_pil (&tm, pil))) {
		errno = EOVERFLOW;
		return (time_t) -1;
	}

	if (unlikely (!tm_leap_day_check (&tm))) {
		errno = VBI_ERR_INVALID_PIL;
		return (time_t) -1;
	}

	tm.tm_hour = VBI_PIL_HOUR (pil);
	tm.tm_min = VBI_PIL_MINUTE (pil);
	tm.tm_sec = 0;

	start = timegm (&tm);
	if (unlikely ((time_t) -1 == start))
		return (time_t) -1;

	if (seconds_east > 0) {
		/* Note start can be negative. */
		if (unlikely (start < seconds_east)) {
			errno = EOVERFLOW;
			return (time_t) -1;
		}
	} else {
		if (unlikely (start > TIME_MAX + seconds_east)) {
			errno = EOVERFLOW;
			return (time_t) -1;
		}
	}

	return start - seconds_east;
}

/**
 * @param pil Program Identification Label (PIL) to convert.
 * @param start The most recently announced start time of the
 *   program. If zero the current system time will be used.
 * @param seconds_east A time zone specified as an offset in seconds
 *   east of UTC, for example +1 * 60 * 60 for CET. @a seconds_east
 *   may include a daylight-saving time (DST) offset.
 *
 * This function converts a PIL to a time_t in the same manner
 * localtime() converts a broken-down time to time_t.
 *
 * Since PILs do not contain a year field, the year is determined from
 * the @a start parameter, that is the most recently announced start
 * time of the program or "AT-1" in EN 300 231 parlance. If @a pil
 * contains a month more than five months after @a start, @a pil is
 * assumed to refer to an earlier date than @a start.
 *
 * @a pil is assumed to be a time in the time zone specified by @a
 * seconds_east. @a start will be converted to a local time in the
 * same time zone to determine the correct year.
 *
 * Teletext packet 8/30 format 2, VPS and DVB PDC descriptors give a
 * PIL relative to the time zone of the intended audience of the
 * program. An offset from UTC including the DST offset in effect at
 * the specified date may be available on Teletext program
 * announcement pages (see struct vbi_preselection). Another offset
 * from UTC including the @em current DST offset is available as @c
 * VBI_EVENT_LOCAL_TIME, but of course that information is
 * insufficient to determine if DST is in effect at other dates.
 *
 * XDS Current/Future Program ID packets give a PIL relative to UTC,
 * so @a seconds_east should be zero.
 *
 * @returns
 * The PIL as a time_t, that is the number of seconds since
 * 1970-01-01 00:00 UTC. On error the function
 * returns (time_t) -1:
 * - @a pil does not contain a valid date or time. February 29th is
 *   a valid date only if the estimated year is a leap year.
 * - @a start is zero and the current system time could not be
 *   determined.
 * - The time specified by @a pil, @a start and @a seconds_east
 *   cannot be represented as a time_t value (2038 is closer than
 *   you think!).
 *
 * @since 0.2.34
 *
 * @bug
 * This function is not thread safe. That is a limitation of the C
 * library which permits the conversion of a broken-down time in an
 * arbitrary time zone only by setting the TZ environment variable.
 * The function may also fail to restore the value of TZ if
 * insufficient memory is available.
 */
time_t
vbi_pil_lto_to_time		(vbi_pil		pil,
				 time_t			start,
				 int			seconds_east)
{
	time_t t;

	if (unlikely (!vbi_pil_is_valid_date (pil))) {
#if 3 == VBI_VERSION_MINOR
		errno = VBI_ERR_INVALID_PIL;
#else
		errno = 0;
#endif
		return (time_t) -1;
	}

	t = valid_pil_lto_to_time (pil, start, seconds_east);
#if 2 == VBI_VERSION_MINOR
	errno = 0;
#endif
	return t;
}

/**
 * @param pil Program Identification Label (PIL) to convert.
 * @param start The most recently announced start time of the
 *   program. If zero the current system time will be used.
 * @param tz A time zone name in the same format as the TZ environment
 *   variable. If @c NULL the current value of TZ will be used.
 *
 * This function converts a PIL to a time_t in the same manner
 * localtime() converts a broken-down time to time_t.
 *
 * Since PILs do not contain a year field, the year is determined from
 * the @a start parameter, that is the most recently announced start
 * time of the program or "AT-1" in EN 300 231 parlance. If @a pil
 * contains a month more than five months after @a start, @a pil is
 * assumed to refer to an earlier date than @a start.
 *
 * @a pil is assumed to be a time in the time zone @a tz. @a start
 * will be converted to a local time in the same time zone to
 * determine the correct year.
 *
 * Teletext packet 8/30 format 2, VPS and DVB PDC descriptors give a
 * PIL relative to the time zone of the intended audience of the
 * program. Ideally the time zone would be specified as a geographic
 * area like "Europe/London", such that the function can determine
 * the correct offset from UTC and if daylight-saving time is in
 * effect at the specified date. See the documentation of the
 * localtime() function and the TZ environment variable for details.
 *
 * XDS Current/Future Program ID packets give a PIL relative to UTC.
 * Just specify time zone "UTC" in this case.
 *
 * @returns
 * The PIL as a time_t, that is the number of seconds since
 * 1970-01-01 00:00 UTC. On error the function
 * returns (time_t) -1:
 * - @a pil does not contain a valid date or time. February 29th is
 *   a valid date only if the estimated year is a leap year.
 * - @a tz is empty or contains an equal sign '='.
 * - @a start is zero and the current system time could not be
 *   determined.
 * - The time specified by @a pil, @a start and @a tz cannot be
 *   represented as a time_t value.
 * - Insufficient memory was available.
 *
 * @since 0.2.34
 *
 * @bug
 * This function is not thread safe unless @a tz is @c NULL. That is
 * a limitation of the C library which permits the conversion of a
 * broken-down time in an arbitrary time zone only by setting the TZ
 * environment variable. The function may also fail to restore the
 * value of TZ if insufficient memory is available.
 */
time_t
vbi_pil_to_time			(vbi_pil		pil,
				 time_t			start,
				 const char *		tz)
{
	struct tm tm;
	char *old_tz;
	time_t result;
	int saved_errno;

	if (unlikely (!vbi_pil_is_valid_date (pil))) {
#if 3 == VBI_VERSION_MINOR
		errno = VBI_ERR_INVALID_PIL;
#else
		errno = 0;
#endif
		return (time_t) -1;
	}

	if (NULL != tz && 0 == strcmp (tz, "UTC")) {
		time_t t;

		t = valid_pil_lto_to_time (pil, start,
					   /* seconds_east */ 0);
#if 2 == VBI_VERSION_MINOR
		errno = 0;
#endif
		return t;
	}

	if (unlikely (!localtime_tz (&tm, &old_tz, start, tz))) {
#if 2 == VBI_VERSION_MINOR
		errno = 0;
#endif
		return (time_t) -1;
	}

	if (unlikely (!tm_mon_mday_from_pil (&tm, pil))) {
		saved_errno = EOVERFLOW;
		goto failed;
	}

	if (unlikely (!tm_leap_day_check (&tm))) {
		saved_errno = VBI_ERR_INVALID_PIL;
		goto failed;
	}

	tm.tm_hour = VBI_PIL_HOUR (pil);
	tm.tm_min = VBI_PIL_MINUTE (pil);
	tm.tm_sec = 0;

	tm.tm_isdst = -1; /* unknown */

	result = mktime (&tm);
	if (unlikely ((time_t) -1 == result))
		goto failed;

	if (unlikely (!restore_tz (&old_tz, tz))) {
#if 2 == VBI_VERSION_MINOR
		errno = 0;
#endif
		return (time_t) -1;
	}

	return result;

 failed:
	if (unlikely (!restore_tz (&old_tz, tz))) {
#if 2 == VBI_VERSION_MINOR
		errno = 0;
#endif
		return (time_t) -1;
	}

#if 3 == VBI_VERSION_MINOR
	errno = saved_errno;
#else
	errno = 0;
#endif
	return (time_t) -1;
}

#if 0 /* just an idea */
time_t
vbi_pil_year_to_time		(vbi_pil		pil,
				 int			year,
				 const char *		tz)
{
}
time_t
vbi_pil_year_lto_to_time	(vbi_pil		pil,
				 int			year,
				 int			seconds_east)
{
}
#endif

static vbi_bool
pty_utc_validity_window		(time_t *		begin,
				 time_t *		end,
				 time_t			time)
{
	struct tm tm;
	unsigned int seconds_since_midnight;
	unsigned int duration;

	memset (&tm, 0, sizeof (tm));

	errno = 0;

	if (unlikely (NULL == gmtime_r (&time, &tm)))
		return FALSE;

	seconds_since_midnight =
		+ tm.tm_hour * 3600
		+ tm.tm_min * 60
		+ tm.tm_sec;

	/* This is safe because UTC does not observe DST and POSIX
	   time_t does not count leap seconds. (When a leap second is
	   inserted in UTC, POSIX time "freezes" for one second. When
	   a leap second is removed POSIX time jumps forward one
	   second.) The result is unambiguous because leap seconds are
	   inserted or removed just before midnight and we only return
	   full hours. */
	duration =
		+ 4 * 7 * 24 * 60 * 60
		+ (24 + 4) * 60 * 60
		- seconds_since_midnight;

	if (unlikely (time > (time_t)(TIME_MAX - duration))) {
		errno = EOVERFLOW;
		return FALSE;
	}

	*begin = time;
	*end = time + duration;

	return TRUE;
}

/**
 * @param begin The begin of the validity of the PTY will be stored
 *   here.
 * @param end The end of the validity of the PTY will be stored here.
 * @param last_transm The last time when a program ID with the PTY
 *   in question was broadcast by the network.
 * @param tz A time zone name in the same format as the TZ environment
 *   variable. If @c NULL the current value of TZ will be used.
 *
 * This function calculates the validity time window of a Program Type
 * (PTY) code according to EN 300 231. That is the time window where a
 * network can be expected to broadcast another program with the same
 * PTY, approximately up to four weeks after its last
 * transmission. When the PTY is a series code (>= 0x80) and not
 * transmitted again before @a end, the network may assign the code to
 * another series.
 *
 * @a tz is the time zone of the intended audience of the program.
 * Ideally the time zone would be specified as a geographic area like
 * "Europe/London", such that the function can determine if
 * daylight-saving time is in effect at @a time or at the end of the
 * validity time window. See the documentation of the localtime()
 * function and the TZ environment variable for details. If no time
 * zone name is available "UTC" should be specified, the returned
 * @a end time may be off by one hour in this case.
 *
 * @returns
 * On error the function returns @c FALSE and @a *begin and @a *end
 * remain unchanged:
 * - @a tz is empty or contains an equal sign '='.
 * - The @a end time cannot be represented as a time_t value
 *   (December 2037 is closer than you think!).
 * - Insufficient memory was available.
 *
 * @since 0.2.34
 *
 * @bug
 * This function is not thread safe unless @a tz is @c NULL.
 * That is a limitation of the C library which permits the conversion
 * of a broken-down time in an arbitrary time zone only by setting
 * the TZ environment variable. The function may also fail to restore
 * the value of TZ if insufficient memory is available.
 */
vbi_bool
vbi_pty_validity_window		(time_t *		begin,
				 time_t *		end,
				 time_t			last_transm,
				 const char *		tz)
{
	char *old_tz;
	struct tm tm;
	time_t stop;
	int saved_errno;

	assert (NULL != begin);
	assert (NULL != end);

	if (NULL != tz && 0 == strcmp (tz, "UTC")) {
		vbi_bool success;

		success = pty_utc_validity_window (begin, end,
						   last_transm);
#if 2 == VBI_VERSION_MINOR
		errno = 0;
#endif
		return success;
	}

	if (unlikely (!localtime_tz (&tm, &old_tz, last_transm, tz)))
		goto failed;

	tm.tm_mday += 4 * 7 + 1;
	tm.tm_hour = 4;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	tm.tm_isdst = -1; /* unknown */

	stop = mktime (&tm);
	if (unlikely ((time_t) -1 == stop)) {
		saved_errno = errno;

		if (unlikely (!restore_tz (&old_tz, tz)))
			return FALSE;

#if 3 == VBI_VERSION_MINOR
		errno = saved_errno;
#else
		errno = 0;
#endif
		return FALSE;
	}

	if (unlikely (!restore_tz (&old_tz, tz)))
		goto failed;

	*begin = last_transm;
	*end = stop;

	return TRUE;

 failed:
#if 2 == VBI_VERSION_MINOR
	errno = 0;
#endif
	return FALSE;
}

static vbi_bool
valid_pil_lto_validity_window	(time_t *		begin,
				 time_t *		end,
				 vbi_pil		pil,
				 time_t			start,
				 int			seconds_east)
{
	time_t t;

	t = valid_pil_lto_to_time (pil & VBI_PIL (/* month */ 15,
						   /* day */ 31,
						   /* hour */ 0,
						   /* minute */ 0),
				   start, seconds_east);
	if (unlikely ((time_t) -1 == t)) {
		if (VBI_ERR_INVALID_PIL == errno) {
			/* Annex F: "Invalid days -
			   indefinite time window". */
			*begin = TIME_MIN;
			*end = TIME_MAX;
			return TRUE;
		} else {
			return FALSE;
		}
	}

	/* EN 300 231 Section 9.3. */

	/* Just adding a number of seconds is safe because UTC does
	   not observe DST and POSIX time_t does not count leap
	   seconds. The results are unambiguous because leap seconds
	   are inserted or removed just before midnight and we only
	   return full hours. */

	if (unlikely (t > TIME_MAX - 28 * 60 * 60)) {
		errno = EOVERFLOW;
		return FALSE;
	}

	if (VBI_PIL_HOUR (pil) < 4) {
		if (unlikely (t < 4 * 60 * 60)) {
			errno = EOVERFLOW;
			return FALSE;
		}

		*begin = t - 4 * 60 * 60;
	} else {
		*begin = t;
	}

	*end = t + 28 * 60 * 60;

	return TRUE;
}

/**
 * @param begin The start of the validity of the PIL will be stored
 *   here.
 * @param end The end of the validity of the PIL will be stored here.
 * @param pil Program Identification Label (PIL).
 * @param start The most recently announced start time of the
 *   program. If zero the current system time will be used.
 * @param seconds_east A time zone specified as an offset in seconds
 *   east of UTC, for example +1 * 60 * 60 for CET. @a seconds_east
 *   may include a daylight-saving time (DST) offset.
 *
 * This function calculates the validity time window of a PIL
 * according to EN 300 231. That is the time window where a network
 * can be expected to broadcast this PIL, usually from 00:00 on the
 * same day until exclusive 04:00 on the next day.
 *
 * Since PILs do not contain a year field, the year is determined from
 * the @a start parameter, that is the most recently announced start
 * time of the program or "AT-1" in EN 300 231 parlance. If @a pil
 * contains a month more than five months after @a start, @a pil is
 * assumed to refer to an earlier date than @a start.
 *
 * @a pil is assumed to be a time in the time zone specified by @a
 * seconds_east. @a start will be converted to a local time in the
 * same time zone to determine the correct year.
 *
 * Teletext packet 8/30 format 2, VPS and DVB PDC descriptors give a
 * PIL relative to the time zone of the intended audience of the
 * program. An offset from UTC including the DST offset in effect at
 * the specified date may be available on Teletext program
 * announcement pages (see struct vbi_preselection). Another offset
 * from UTC including the current DST offset is available as @c
 * VBI_EVENT_LOCAL_TIME. But of course these offsets are insufficient
 * to determine if DST is in effect at any given date, so the returned
 * @a begin or @a end may be off by one hour if the validity window
 * straddles a DST discontinuity.
 *
 * If @a pil is @c VBI_PIL_NSPV this function ignores @a seconds_east
 * and returns the same values as vbi_pty_validity_window().
 *
 * @returns
 * On error the function returns @c FALSE:
 * - @a pil is not @c VBI_PIL_NSPV and does not contain a
 *   valid date or time. February 29th is a valid date only if the
 *   estimated year is a leap year.
 * - @a start is zero and the current system time could not be
 *   determined.
 * - The time specified by @a pil, @a start and @a seconds_east cannot
 *   be represented as a time_t value.
 *
 * @since 0.2.34
 *
 * @bug
 * This function is not thread safe. That is a limitation of the C
 * library which permits the conversion of a broken-down time in an
 * arbitrary time zone only by setting the TZ environment variable.
 * The function may also fail to restore the value of TZ if
 * insufficient memory is available.
 */
vbi_bool
vbi_pil_lto_validity_window	(time_t *		begin,
				 time_t *		end,
				 vbi_pil		pil,
				 time_t			start,
				 int			seconds_east)
{
	unsigned int month;

	assert (NULL != begin);
	assert (NULL != end);

	month = VBI_PIL_MONTH (pil);
	if (0 == month) {
		/* EN 300 231 Annex F: "Unallocated". */
#if 3 == VBI_VERSION_MINOR
		errno = VBI_ERR_INVALID_PIL;
#else
		errno = 0;
#endif
		return FALSE;
	} else if (month <= 12) {
		unsigned int day;
		vbi_bool success;

		day = VBI_PIL_DAY (pil);
		if (day - 1 >= month_days[month - 1]) {
			/* Annex F: "Invalid days
			   - indefinite time window". */
			*begin = TIME_MIN;
			*end = TIME_MAX;
			return TRUE;
		}

		success = valid_pil_lto_validity_window
			(begin, end, pil, start, seconds_east);
#if 2 == VBI_VERSION_MINOR
		errno = 0;
#endif
		return success;
	} else if (month <= 14) {
		/* Annex F: "Indefinite time window". */
		*begin = TIME_MIN;
		*end = TIME_MAX;
		return TRUE;
	} else {
		vbi_bool success;

		/* Annex F: "Unallocated except for the following
		 * service codes (for which there is no restriction to
		 * the time window of validity)". */
		switch (pil) {
		case VBI_PIL_TIMER_CONTROL:
		case VBI_PIL_INHIBIT_TERMINATE:
		case VBI_PIL_INTERRUPTION:
		case VBI_PIL_CONTINUE:
			*begin = TIME_MIN;
			*end = TIME_MAX;
			return TRUE;

		/* EN 300 231 Section 9.3, Annex E.3. */
		case VBI_PIL_NSPV:
			success = pty_utc_validity_window (begin, end,
							   start);
#if 2 == VBI_VERSION_MINOR
			errno = 0;
#endif
			return success;

		default:
#if 3 == VBI_VERSION_MINOR
			errno = VBI_ERR_INVALID_PIL;
#else
			errno = 0;
#endif
			return FALSE;
		}
	}
}

static vbi_bool
valid_pil_validity_window	(time_t *		begin,
				 time_t *		end,
				 vbi_pil		pil,
				 time_t			start,
				 const char *		tz)
{
	char *old_tz;
	struct tm tm;
	struct tm tm2;
	time_t stop;
	int saved_errno;

	/* EN 300 231 Section 9.3 and Annex F. */

	old_tz = NULL;

	if (NULL != tz && 0 == strcmp (tz, "UTC")) {
		return valid_pil_lto_validity_window
			(begin, end, pil, start, /* seconds_east */ 0);
	}

	if (unlikely (!localtime_tz (&tm, &old_tz, start, tz)))
		return FALSE;

	if (unlikely (!tm_mon_mday_from_pil (&tm, pil))) {
		saved_errno = EOVERFLOW;
		goto failed;
	}

	if (unlikely (!tm_leap_day_check (&tm))) {
		/* Annex F: "Invalid days - indefinite time window". */
		if (!restore_tz (&old_tz, tz))
			return FALSE;
		*begin = TIME_MIN;
		*end = TIME_MAX;
		return TRUE;
	}

	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	tm.tm_isdst = -1; /* unknown */

	tm2 = tm;

	if (VBI_PIL_HOUR (pil) < 4) {
		--tm.tm_mday;
		tm.tm_hour = 20;
	}

	start = mktime (&tm);
	if (unlikely ((time_t) -1 == start))
		goto failed;

	tm2.tm_mday += 1;
	tm2.tm_hour = 4;

	stop = mktime (&tm2);
	if (unlikely ((time_t) -1 == stop))
		goto failed;

	if (unlikely (!restore_tz (&old_tz, tz)))
		return FALSE;

	*begin = start;
	*end = stop;

	return TRUE;

 failed:
	if (unlikely (!restore_tz (&old_tz, tz)))
		return FALSE;

	errno = saved_errno;
	return FALSE;
}

/**
 * @param begin The start of the validity of the PIL will be stored
 *   here.
 * @param end The end of the validity of the PIL will be stored here.
 * @param pil Program Identification Label (PIL).
 * @param start The most recently announced start time of the program.
 *   If zero the current system time will be used.
 * @param tz A time zone name in the same format as the TZ environment
 *   variable. If @c NULL the current value of TZ will be used.
 *
 * This function calculates the validity time window of a PIL
 * according to EN 300 231. That is the time window where a network
 * can be expected to broadcast this PIL, usually from 00:00 on the
 * same day until 04:00 on the next day.
 *
 * Since PILs do not contain a year field, the year is determined from
 * the @a start parameter, that is the most recently announced start
 * time of the program or "AT-1" in EN 300 231 parlance. If @a pil
 * contains a month more than five months after @a start, @a pil is
 * assumed to refer to an earlier date than @a start.
 *
 * @a pil is assumed to be a time in the time zone specified by @a
 * seconds_east. @a start will be converted to a local time in the
 * same time zone to determine the correct year.
 *
 * Teletext packet 8/30 format 2, VPS and DVB PDC descriptors give a
 * PIL relative to the time zone of the intended audience of the
 * program. Ideally the time zone would be specified as a geographic
 * area like "Europe/London", such that the function can determine the
 * correct offset from UTC and if daylight-saving time is in effect at
 * any time within the validity window. See the documentation of the
 * localtime() function and the TZ environment variable for details.
 *
 * If @a pil is @c VBI_PIL_NSPV this function returns the same values
 * as vbi_pty_validity_window().
 *
 * @returns
 * On error the function returns @c FALSE:
 * - @a pil is not @c VBI_PIL_NSPV and does not contain a
 *   valid date or time. February 29th is a valid date only if the
 *   estimated year is a leap year.
 * - @a tz is empty or contains an equal sign '='.
 * - @a start is zero and the current system time could not be
 *   determined.
 * - The time specified by @a pil, @a start and @a tz cannot be
 *   represented as a time_t value.
 * - Insufficient memory was available.
 *
 * @since 0.2.34
 *
 * @bug
 * This function is not thread safe unless @a tz is @c NULL.
 * That is a limitation of the C library which permits the conversion
 * of a broken-down time in an arbitrary time zone only by setting
 * the TZ environment variable. The function may also fail to restore
 * the value of TZ if insufficient memory is available.
 */
vbi_bool
vbi_pil_validity_window		(time_t *		begin,
				 time_t *		end,
				 vbi_pil		pil,
				 time_t			start,
				 const char *		tz)
{
	unsigned int month;

	assert (NULL != begin);
	assert (NULL != end);

	month = VBI_PIL_MONTH (pil);
	if (0 == month) {
		/* EN 300 231 Annex F: "Unallocated". */
#if 3 == VBI_VERSION_MINOR
		errno = VBI_ERR_INVALID_PIL;
#else
		errno = 0;
#endif
		return FALSE;
	} else if (month <= 12) {
		unsigned int day;
		vbi_bool success;

		day = VBI_PIL_DAY (pil);
		if (day - 1 >= month_days[month - 1]) {
			/* Annex F: "Invalid days
			   - indefinite time window". */
			*begin = TIME_MIN;
			*end = TIME_MAX;
			return TRUE;
		}

		success = valid_pil_validity_window
			(begin, end, pil, start, tz);
#if 2 == VBI_VERSION_MINOR
		errno = 0;
#endif
		return success;
	} else if (month <= 14) {
		/* Annex F: "Indefinite time window". */
		*begin = TIME_MIN;
		*end = TIME_MAX;
		return TRUE;
	} else {
		vbi_bool success;

		/* Annex F: "Unallocated except for the following
		 * service codes (for which there is no restriction to
		 * the time window of validity)". */
		switch (pil) {
		case VBI_PIL_TIMER_CONTROL:
		case VBI_PIL_INHIBIT_TERMINATE:
		case VBI_PIL_INTERRUPTION:
		case VBI_PIL_CONTINUE:
			*begin = TIME_MIN;
			*end = TIME_MAX;
			return TRUE;

		/* EN 300 231 Section 9.3, Annex E.3. */
		case VBI_PIL_NSPV:
			success = vbi_pty_validity_window
				(begin, end, start, tz);
#if 2 == VBI_VERSION_MINOR
			errno = 0;
#endif
			return success;

		default:
#if 3 == VBI_VERSION_MINOR
			errno = VBI_ERR_INVALID_PIL;
#else
			errno = 0;
#endif
			return FALSE;
		}
	}
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
