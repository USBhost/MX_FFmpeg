/*
 *  libzvbi -- Unicode conversion helper functions
 *
 *  Copyright (C) 2003-2006 Michael H. Schimek
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

/* $Id: conv.c,v 1.10 2008/02/19 00:35:15 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
// jhkim @2014-6-29
//#include <langinfo.h>

#include "misc.h"
#include "conv.h"
#ifdef ZAPPING8
#  include "common/intl-priv.h"
#else
#  include "intl-priv.h"
#endif

#ifdef HAVE_ICONV

#  include <iconv.h>

struct _vbi_iconv_t {
	iconv_t			icd;
	uint16_t		ucs2_repl[1];
};

#else

struct _vbi_iconv_t {
	int			dummy;
};

#endif

#ifdef HAVE_ICONV

/**
 * @internal
 * @param cd Conversion object returned by vbi_iconv_open().
 * @param dst Pointer to output buffer pointer, will be incremented
 *   by the number of bytes written.
 * @param dst_left Space available in the output buffer, in bytes.
 * @param src Pointer to input buffer pointer, will be incremented
 *   by the number of bytes read.
 * @param src_left Number of bytes left to read in the input buffer.
 *
 * Like iconv(), but converts unrepresentable characters to
 * @a cd->ucs2_repl. The source is assumed to be in UCS-2 format
 * (so we know how to skip unrepresentable characters).
 *
 * @returns
 * See iconv().
 *
 * @since 0.2.23
 */
static size_t
iconv_ucs2			(vbi_iconv_t *		cd,
				 char **		dst,
				 size_t *		dst_left,
				 const char **		src,
				 size_t *		src_left)
{
	size_t r;

	assert (NULL != cd);
	assert (NULL != dst);
	assert (NULL != dst_left);
	assert (NULL != src);
	assert (NULL != src_left);

	r = 0;

	while (*src_left > 0) {
		const char *src1;
		size_t src_left1;

		/* iconv() source pointer may be defined as char **,
		   should be const char ** or const void **. Ignore
		   compiler warnings. */
		r = iconv (cd->icd, (void *) src, src_left,
			   dst, dst_left);
		if (likely ((size_t) -1 != r))
			break; /* success */

		if (EILSEQ != errno)
			break;

		if (0 == cd->ucs2_repl[0])
			return -1; /* do not replace */

		src1 = (const char *) cd->ucs2_repl;
		src_left1 = 2;

		r = iconv (cd->icd, (void *) &src1, &src_left1,
			   dst, dst_left);
		if (unlikely ((size_t) -1 == r))
			break; /* failed */

		*src += 2; /* in UCS-2 format */
		*src_left -= 2;
	}

	return r;
}

#endif /* HAVE_ICONV */

/** @internal */
vbi_bool
_vbi_iconv_ucs2			(vbi_iconv_t *		cd,
				 char **		dst,
				 unsigned long		dst_size,
				 const uint16_t *	src,
				 long			src_length)
{
	assert (NULL != cd);
	assert (NULL != dst);
	assert (NULL != *dst);

	if (NULL == src || 0 == src_length)
		return TRUE;

#ifdef HAVE_ICONV
	{
		const char *s;
		size_t d_left;
		size_t s_left;
		size_t r;

		if (src_length < 0)
			src_length = vbi_strlen_ucs2 (src) + 1;

		s = (const char *) src;
		s_left = src_length * 2;

		d_left = dst_size;
		
		r = iconv_ucs2 (cd, dst, &d_left, &s, &s_left);

		return ((size_t) -1 != r && 0 == s_left);
	}
#else
	dst_size = dst_size; /* unused */

	return FALSE;
#endif
}

/**
 * @internal
 * @param cd Conversion object returned by vbi_iconv_open().
 *
 * Frees all resources associated with the conversion object.
 *
 * @since 0.2.23
 */
void
_vbi_iconv_close		(vbi_iconv_t *		cd)
{
	if (NULL == cd)
		return;

#ifdef HAVE_ICONV
	if ((iconv_t) -1 != cd->icd) {
		iconv_close (cd->icd);
		cd->icd = (iconv_t) -1;
	}

	vbi_free (cd);
#endif
}

/**
 * @internal
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param dst Pointer to output buffer pointer, which will be
 *   incremented by the number of bytes written. Can be @c NULL.
 * @param dst_size Space available in the output buffer, in bytes.
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the _vbi_iconv_ucs2()
 *   function will return @c FALSE instead.
 *
 * Helper function to convert text. A start byte sequence will be
 * stored in @a dst if necessary.
 *
 * @returns
 * @c NULL when the conversion is impossible.
 *
 * @since 0.2.23
 */
vbi_iconv_t *
_vbi_iconv_open			(const char *		dst_codeset,
				 const char *		src_codeset,
				 char **		dst,
				 unsigned long		dst_size,
				 int			repl_char)
{
	vbi_iconv_t *cd;

	if (NULL == dst_codeset)
		dst_codeset = "UTF-8";

	if (NULL == src_codeset)
		src_codeset = "UCS-2";

#ifdef HAVE_ICONV
	cd = vbi_malloc (sizeof (*cd));
	if (NULL == cd)
		return NULL;

	cd->icd = iconv_open (dst_codeset, src_codeset);
	if ((iconv_t) -1 == cd->icd) {
		vbi_free (cd);
		return NULL;
	}

	cd->ucs2_repl[0] = repl_char;

	if (NULL != dst) {
		size_t d_left;
		size_t n;

		d_left = dst_size;

		/* Write out the byte sequence to get into the
		   initial state if this is necessary. */
		n = iconv (cd->icd, NULL, NULL, dst, &d_left);

		if ((size_t) -1 == n) {
			_vbi_iconv_close (cd);
			return NULL;
		}
	}

#else /* !HAVE_ICONV */
	dst = dst; /* unused */
	dst_size = dst_size;
	repl_char = repl_char;

	cd = NULL;
#endif
	return cd;
}

/** @internal */
static vbi_bool
same_codeset			(const char *		dst_codeset,
				 const char *		src_codeset)
{
	assert (NULL != dst_codeset);
	assert (NULL != src_codeset);

	for (;;) {
		char d, s;

		d = *dst_codeset;
		s = *src_codeset;

		if (d == s) {
			if (0 == d)
				return TRUE;

			++dst_codeset;
			++src_codeset;
		} else if ('-' == d || '_' == d) {
			++dst_codeset;
		} else if ('-' == s || '_' == s) {
			++src_codeset;
		} else {
			return FALSE;
		}
	}
}

/**
 * @ingroup Conv
 * @param src NUL-terminated UCS-2 string.
 *
 * Counts the characters in the string, up to and excluding
 * the terminating NUL.
 *
 * @since 0.2.23
 */
unsigned long
vbi_strlen_ucs2			(const uint16_t *	src)
{
	const uint16_t *s;

	if (NULL == src)
		return 0;

	for (s = src; 0 != *s; ++s)
		;

	return s - src;
}

/**
 * @internal
 * @param out_size If not @c NULL the actual number of bytes stored
 *   in the buffer (excluding the terminating NUL) will be stored here.
 * @param src Source string.
 * @param src_size Number of bytes in the source string (excluding
 *   the terminating NUL, if any).
 *
 * Copies a string into a newly allocated buffer, with a terminating
 * NUL (4 bytes).
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * it runs out of memory, or when @a src is @c NULL.
 *
 * @since 0.2.23
 */
static char *
strndup_identity		(unsigned long *	out_size,
				 const char *		src,
				 unsigned long		src_size)
{
	char *buffer;

	buffer = vbi_malloc (src_size + 4);
	if (NULL == buffer) {
		if (NULL != out_size)
			*out_size = 0;

		return NULL;
	}

	memcpy (buffer, src, src_size);
	memset (buffer + src_size, 0, 4);

	if (NULL != out_size)
		*out_size = src_size;

	return buffer;
}

/**
 * @internal
 * @param out_size If not @c NULL the actual number of bytes stored
 *   in the buffer (excluding the terminating NUL) will be stored here.
 * @param src Source string in UCS-2 format, can be @c NULL.
 * @param src_length Number of characters (not bytes) in the source
 *   string. Can be -1 if the string is NUL terminated.
 *
 * Converts a string from UCS-2 to UTF-8 format and writes the
 * result with a terminating NUL character into a newly allocated
 * buffer. Note the buffer may be larger than necessary.
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * it runs out of memory, or when @a src is @c NULL.
 *
 * @since 0.2.23
 */
static char *
strndup_utf8_ucs2		(unsigned long *	out_size,
				 const uint16_t *	src,
				 long			src_length)
{
	char *d;
	char *buffer;
	const uint16_t *end;

	if (NULL != out_size)
		*out_size = 0;

	if (unlikely (NULL == src))
		return NULL;

	if (src_length < 0)
		src_length = vbi_strlen_ucs2 (src);

	buffer = vbi_malloc (src_length * 3 + 1);
	if (NULL == buffer)
		return NULL;

	d = buffer;

	for (end = src + src_length; src < end; ++src) {
		unsigned int c = *src;

		if (c < 0x80) {
			*d++ = c;
		} else if (c < 0x800) {
			d[0] = 0xC0 | (c >> 6);
			d[1] = 0x80 | (c & 0x3F);
			d += 2;
		} else {
			d[0] = 0xE0 | (c >> 12);
			d[1] = 0x80 | ((c >> 6) & 0x3F);
			d[2] = 0x80 | (c & 0x3F);
			d += 3;
		}
	}

	if (NULL != out_size)
		*out_size = d - buffer;

	*d = 0;

	return buffer;
}

/**
 * @internal
 * @param out_size If not @c NULL the actual number of bytes stored
 *   in the buffer (excluding the terminating NUL) will be stored here.
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src Source string in UCS-2 format, can be @c NULL.
 * @param src_length Number of characters (not bytes) in the source
 *   string. Can be -1 if the string is NUL terminated.
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the function will
 *   fail if the source buffer contains unrepresentable characters.
 *
 * Converts a UCS-2 string with iconv() and writes the result with a
 * terminating NUL character into a newly allocated buffer. Note the
 * buffer may be larger than necessary.
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * the conversion fails, when it runs out of memory or when @a src
 * is @c NULL.
 *
 * @since 0.2.23
 */
static char *
strndup_iconv_from_ucs2		(unsigned long *	out_size,
				 const char *		dst_codeset,
				 const uint16_t *	src,
				 long			src_length,
				 int			repl_char)
{
	char *buffer;
	unsigned long buffer_size;

	if (NULL == dst_codeset || same_codeset (dst_codeset, "UTF8")) {
		return strndup_utf8_ucs2 (out_size, src, src_length);
	} else if (same_codeset (dst_codeset, "UCS2")) {
		return strndup_identity (out_size, (const char *) src,
					 src_length * 2);
	}

	if (NULL != out_size)
		*out_size = 0;

	if (unlikely (NULL == src))
		return NULL;

	buffer = NULL;
	buffer_size = 0;

#ifdef HAVE_ICONV
	if (unlikely (src_length < 0)) {
		src_length = vbi_strlen_ucs2 (src);
	}

	{
		char *d;
		uint32_t *d32;

		for (;;) {
			vbi_iconv_t *cd;
			const char *s;
			size_t d_left;
			size_t s_left;
			size_t r;

			d_left = src_length * 4;
			if (buffer_size > 0)
				d_left = buffer_size * 2;

			d = vbi_malloc (d_left);
			if (unlikely (NULL == d)) {
				errno = ENOMEM;
				return NULL;
			}

			buffer = d;
			buffer_size = d_left;

			cd = _vbi_iconv_open (dst_codeset, "UCS-2",
					       &d, d_left, repl_char);
			if (NULL == cd) {
				vbi_free (buffer);
				buffer = NULL;

				return NULL;
			}

			d_left = buffer_size - (d - buffer)
				- 4 /* room for a UCS-4 NUL */;

			s = (const char *) src;
			s_left = src_length * 2;

			r = iconv_ucs2 (cd, &d, &d_left, &s, &s_left);

			_vbi_iconv_close (cd);
			cd = NULL;

			if (likely ((size_t) -1 != r))
				break;

			vbi_free (buffer);
			buffer = NULL;

			if (E2BIG != errno)
				return NULL;

			/* Buffer was too small, try again. */
		}

		if (NULL != out_size)
			*out_size = d - buffer;

		d32 = (uint32_t *) d;
		*d32 = 0;
	}

#else /* !HAVE_ICONV */
	repl_char = repl_char; /* unused */

#endif

	return buffer;
}

/**
 * @ingroup Conv
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src Source string in UCS-2 format, can be @c NULL.
 * @param src_length Number of characters (not bytes) in the source
 *   string. Can be -1 if the string is NUL terminated.
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the function will
 *   fail if the source buffer contains unrepresentable characters.
 *
 * Converts a UCS-2 string with iconv() and writes the result with a
 * terminating NUL character into a newly allocated buffer.
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * the conversion fails, when it runs out of memory or when @a src
 * is @c NULL.
 *
 * @since 0.2.23
 */
char *
vbi_strndup_iconv_ucs2		(const char *		dst_codeset,
				 const uint16_t *	src,
				 long			src_length,
				 int			repl_char)
{
	char *buffer;
	char *result;
	unsigned long size;

	buffer = strndup_iconv_from_ucs2 (&size,
					  dst_codeset,
					  src, src_length,
					  repl_char);
	if (NULL == buffer)
		return NULL;

	result = vbi_realloc (buffer, size + 4);
	if (NULL == result)
		result = buffer;

	return result;
}

/**
 * @internal
 * @param out_size If not @c NULL the actual number of bytes stored
 *   in the buffer (excluding the terminating NUL) will be stored here.
 * @param src Source string in EIA 608 (Closed Caption) format, can
 *   be @c NULL.
 * @param src_length Number of characters (= bytes) in the source
 *   string. Can be -1 if the string is NUL terminated.
 * @param to_upper Convert the string to upper case.
 *
 * Converts a string from EIA 608 to UCS-2 format and writes the
 * result with a terminating NUL character into a newly allocated
 * buffer. The function ignores parity bits and the bytes 0x00 ...
 * 0x1F except two byte special and extended characters (e.g.
 * music note 0x11 0x37). Note the buffer may be larger than necessary.
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when the
 * source buffer contains invalid two byte characters, when
 * it runs out of memory, or when @a src is @c NULL.
 *
 * @since 0.2.23
 */
static char *
strndup_ucs2_eia608		(unsigned long *	out_size,
				 const char *		src,
				 long			src_length,
				 vbi_bool		to_upper)
{
	uint16_t *d16;
	char *buffer;
	long i;

	if (NULL != out_size)
		*out_size = 0;

	if (unlikely (NULL == src))
		return NULL;

	if (src_length < 0)
		src_length = strlen (src);

	buffer = vbi_malloc (src_length * 2 + 2);
	if (unlikely (NULL == buffer))
		return NULL;

	d16 = (uint16_t *) buffer;

	for (i = 0; i < src_length; ++i) {
		unsigned int c = src[i] & 0x7F;

		switch (c) {
		case 0x11 ... 0x13:
		case 0x19 ... 0x1B:
			if (unlikely (i + 1 >= src_length))
				goto ilseq;

			c = ((c * 256) + src[++i]) & 0x777F;
			c = vbi_caption_unicode (c, to_upper);

			if (unlikely (0 == c))
				goto ilseq;

			*d16++ = c;

			break;

		case 0x20 ... 0x7F:			
			*d16++ = vbi_caption_unicode (c, to_upper);
			break;

		default:
			break;
		}
	}

	if (NULL != out_size)
		*out_size = (char *) d16 - buffer;

	*d16 = 0;

	return buffer;
	
ilseq:
	vbi_free (buffer);
	buffer = NULL;

	errno = EILSEQ;

	return NULL;
}

/**
 * @internal
 * @param out_size If not @c NULL the actual number of bytes stored
 *   in the buffer (excluding the terminating NUL) will be stored here.
 * @param src_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src Source string, can be @c NULL.
 * @param src_size Number of bytes in the source string (excluding
 *   the terminating NUL, if any).
 *
 * Converts a string from @a src_codeset to UCS-2 and writes the result
 * with a terminating NUL character into a newly allocated buffer. Note
 * the buffer may be larger than necessary.
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * the conversion fails, when it runs out of memory or when @a src
 * is @c NULL.
 *
 * @since 0.2.23
 */
static char *
strndup_iconv_to_ucs2		(unsigned long *	out_size,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size)
{
	char *buffer;
	unsigned long buffer_size;

	if (NULL == src_codeset) {
		src_codeset = "UTF-8";
	} else if (same_codeset (src_codeset, "UCS2")) {
		return strndup_identity (out_size, src, src_size);
	} else if (same_codeset (src_codeset, "EIA608")) {
		return strndup_ucs2_eia608 (out_size, src, src_size, FALSE);
	}

	if (NULL != out_size)
		*out_size = 0;

	if (unlikely (NULL == src))
		return NULL;

	buffer = NULL;
	buffer_size = 0;

#ifdef HAVE_ICONV
	{
		char *d;
		uint16_t *d16;

		for (;;) {
			vbi_iconv_t *cd;
			const char *s;
			size_t d_left;
			size_t s_left;
			size_t r;

			d_left = 16384;
			if (buffer_size > 0)
				d_left = buffer_size * 2;

			d = vbi_malloc (d_left);
			if (NULL == d) {
				errno = ENOMEM;
				return NULL;
			}

			buffer = d;
			buffer_size = d_left;

			cd = _vbi_iconv_open ("UCS-2", src_codeset,
					       &d, d_left,
					       /* repl_char */ 0);
			if (NULL == cd) {
				vbi_free (buffer);
				buffer = NULL;

				return NULL;
			}

			d_left = buffer_size - (d - buffer)
				- 2 /* room for a UCS-2 NUL */;

			s = src;
			s_left = src_size;

			/* Ignore compiler warnings if second argument
			   is declared as char** instead of const char**. */
			r = iconv (cd->icd, (char **) &s, &s_left, &d, &d_left);

			_vbi_iconv_close (cd);
			cd = NULL;

			if ((size_t) -1 != r)
				break;

			vbi_free (buffer);
			buffer = NULL;

			if (E2BIG != errno)
				return NULL;

			/* Buffer was too small, try again. */
		}

		if (NULL != out_size)
			*out_size = d - buffer;

		d16 = (uint16_t *) d;
		*d16 = 0;
	}

#else /* !HAVE_ICONV */
#endif

	return buffer;
}

/**
 * @internal
 * @param out_size If not @c NULL the actual number of bytes stored
 *   in the buffer (excluding the terminating NUL) will be stored here.
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src Source string, can be @c NULL.
 * @param src_size Number of bytes in the source string (excluding
 *   the terminating NUL, if any).
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the function will
 *   fail if the source buffer contains unrepresentable characters.
 *
 * Converts a string with iconv() and writes the result with a
 * terminating NUL character into a newly allocated buffer. Note
 * the buffer may be larger than necessary.
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * the conversion fails, when it runs out of memory or when @a src
 * is @c NULL.
 *
 * @since 0.2.23
 */
char *
_vbi_strndup_iconv		(unsigned long *	out_size,
				 const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
{
	if (same_codeset (dst_codeset, src_codeset)) {
		return strndup_identity (out_size, src, src_size);
	} else if (same_codeset (src_codeset, "UCS2")) {
		if (NULL != src && 0 != (src_size & 1)) {
			if (NULL != out_size)
				*out_size = 0;
			errno = EILSEQ;
			return NULL;
		}

		return strndup_iconv_from_ucs2 (out_size,
						dst_codeset,
						(const uint16_t *) src,
						src_size / 2,
						repl_char);
	} else {
		char *buffer;
		char *result;
		unsigned long size;

		buffer = strndup_iconv_to_ucs2 (&size,
						src_codeset,
						src,
						src_size);
		if (NULL == buffer)
			return NULL;

		if (same_codeset (dst_codeset, "UCS2"))
			return buffer;

		result = strndup_iconv_from_ucs2 (out_size,
						  dst_codeset,
						  (const uint16_t *) buffer,
						  size / 2,
						  repl_char);

		vbi_free (buffer);
		buffer = NULL;

		return result;
	}
}

/**
 * @ingroup Conv
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src Source buffer, can be @c NULL.
 * @param src_size Number of bytes in the source string (excluding
 *   the terminating NUL, if any).
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the function will
 *   fail if the source buffer contains unrepresentable characters.
 *
 * Converts a string with iconv() and writes the result with a
 * terminating NUL character into a newly allocated buffer.
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * the conversion fails, when it runs out of memory or when @a src
 * is @c NULL.
 *
 * @since 0.2.23
 */
char *
vbi_strndup_iconv		(const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
{
	char *result;
	char *buffer;
	unsigned long size;

	buffer = _vbi_strndup_iconv (&size,
				     dst_codeset,
				     src_codeset,
				     src, src_size,
				     repl_char);
	if (NULL == buffer)
		return NULL;

	result = vbi_realloc (buffer, size + 4);
	if (NULL == result)
		result = buffer;

	return result;
}

/**
 * @ingroup internal
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src String of Closed Caption characters, can be @c NULL.
 * @param src_length Number of characters (= bytes) in the
 *   source string. Can be -1 if the @a src string is NUL terminated.
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the function will
 *   fail if the source buffer contains unrepresentable characters.
 *
 * Converts a string of EIA 608 Closed Caption characters to another
 * format and stores the result with a terminating NUL in a newly
 * allocated buffer. The function ignores parity bits and the bytes
 * 0x00 ... 0x1F except two byte special and extended characters (e.g.
 * music note 0x11 0x37).
 *
 * @see vbi_caption_unicode()
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when the
 * source buffer contains invalid two byte characters, when the
 * conversion fails, when it runs out of memory or when @a src is
 * @c NULL.
 *
 * @since 0.2.23
 */
char *
vbi_strndup_iconv_caption	(const char *		dst_codeset,
				 const char *		src,
				 long			src_length,
				 int			repl_char)
{
	if (NULL == src)
		return NULL;

	if (src_length < 0)
		src_length = strlen (src);

	return vbi_strndup_iconv (dst_codeset, "EIA-608",
				  src, src_length, repl_char);
}

#if defined ZAPPING8 || 3 == VBI_VERSION_MINOR

/**
 * @internal
 * @param out_size If not @c NULL the actual number of bytes stored
 *   in the buffer (excluding the terminating NUL) will be stored here.
 * @param cs Teletext character set descriptor.
 * @param src Source string in Teletext format, can be @c NULL.
 * @param src_length Number of characters (= bytes) in the source
 *   string. Can be -1 if the string is NUL terminated.
 *
 * Converts a string from Teletext to UCS-2 format and writes the
 * result with a terminating NUL character into a newly allocated
 * buffer. The function ignores parity bits and control codes
 * (0x00 ... 0x1F). Note the buffer may be larger than necessary.
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * it runs out of memory, or when @a src is @c NULL.
 *
 * @since 0.2.23
 */
static char *
strndup_ucs2_teletext		(unsigned long *	out_size,
				 const vbi_ttx_charset *cs,
				 const uint8_t *	src,
				 long			src_length)
{
	uint16_t *d16;
	char *buffer;
	long i;

	assert (NULL != cs);

	if (NULL != out_size)
		*out_size = 0;

	if (unlikely (NULL == src))
		return NULL;

	if (src_length < 0)
		src_length = strlen ((const char *) src);

	buffer = vbi_malloc (src_length * 2 + 2);
	if (NULL == buffer)
		return NULL;

	d16 = (uint16_t *) buffer;

	for (i = 0; i < src_length; ++i) {
		unsigned int c = src[i] & 0x7F;		

		if (c >= 0x20) {
			*d16++ = vbi_teletext_unicode (cs->g0, cs->subset, c);
		}
	}

	if (NULL != out_size)
		*out_size = (char *) d16 - buffer;

	*d16 = 0;

	return buffer;
}

/**
 * @ingroup Conv
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param cs Teletext character set descriptor.
 * @param src String of Teletext characters, can be @c NULL.
 * @param src_length Number of characters (= bytes) in the
 *   source string. Can be -1 if the @a src string is NUL terminated.
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the function will
 *   fail if the source buffer contains unrepresentable characters.
 *
 * Converts a string of Teletext characters to @a dst_codeset and
 * stores the result with a terminating NUL in a newly allocated buffer.
 * The function ignores parity bits and control codes (0x00 ... 0x1F).
 *
 * @returns
 * A pointer to the allocated buffer. You must free() the buffer
 * when it is no longer needed. The function returns @c NULL when
 * the conversion fails, when it runs out of memory or when @a src
 * is @c NULL.
 *
 * @since 0.2.23
 */
char *
vbi_strndup_iconv_teletext	(const char *		dst_codeset,
				 const vbi_ttx_charset *cs,
				 const uint8_t *	src,
				 long			src_length,
				 int			repl_char)
{
	char *buffer;
	char *result;
	unsigned long size;

	buffer = strndup_ucs2_teletext (&size, cs, src, src_length);
	if (NULL == buffer)
		return NULL;

	if (same_codeset (dst_codeset, "UCS2")) {
		result = vbi_realloc (buffer, size + 2);
		if (NULL == result)
			result = buffer;
	} else {
		result = vbi_strndup_iconv (dst_codeset, "UCS-2",
					    buffer, size,
					    repl_char);
		vbi_free (buffer);
		buffer = NULL;
	}

	return result;
}

#endif /* 3 == VBI_VERSION_MINOR */

/**
 * @ingroup Conv
 * @param fp Output file.
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src Source buffer, can be @c NULL.
 * @param src_size Number of bytes in the source string (excluding
 *   the terminating NUL, if any).
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the function will
 *   fail if the source buffer contains unrepresentable characters.
 *
 * Converts a string with iconv() and writes the result into the
 * given file.
 *
 * @returns
 * FALSE on failure.
 *
 * @since 0.2.23
 */
vbi_bool
vbi_fputs_iconv			(FILE *			fp,
				 const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
{
	char *buffer;
	unsigned long size;
	size_t actual;

	assert (NULL != fp);

	if (NULL == src || 0 == src_size)
		return TRUE;

	if (NULL == dst_codeset)
		dst_codeset = "UTF-8";

	if (NULL == src_codeset)
		src_codeset = "UTF-8";

	if (same_codeset (dst_codeset, src_codeset)) {
		return ((size_t) src_size
			== fwrite (src, 1, src_size, fp));
	}

	buffer = _vbi_strndup_iconv (&size,
				     dst_codeset,
				     src_codeset,
				     src, src_size,
				     repl_char);
	if (NULL == buffer)
		return FALSE;

	actual = fwrite (buffer, 1, size, fp);

	vbi_free (buffer);
	buffer = NULL;

	return (actual == (size_t) size);
}

/**
 * @ingroup Conv
 * @param fp Output file.
 * @param dst_codeset Character set name for iconv() conversion,
 *   for example "ISO-8859-1". When @c NULL the default is UTF-8.
 * @param src Source string in UCS-2 format, can be @c NULL.
 * @param src_length Number of characters (not bytes) in the source
 *   string. Can be -1 if the string is NUL terminated.
 * @param repl_char UCS-2 replacement for characters which are not
 *   representable in @a dst_codeset. When zero the function will
 *   fail if the source buffer contains unrepresentable characters.
 *
 * Converts a UCS-2 string with iconv() and writes the result into
 * the given file.
 *
 * @returns
 * FALSE on failure.
 *
 * @since 0.2.23
 */
vbi_bool
vbi_fputs_iconv_ucs2		(FILE *			fp,
				 const char *		dst_codeset,
				 const uint16_t *	src,
				 long			src_length,
				 int			repl_char)
{
	if (NULL == src)
		return TRUE;

	if (src_length < 0)
		src_length = vbi_strlen_ucs2 (src);

	return vbi_fputs_iconv (fp, dst_codeset, "UCS-2",
				(const char *) src, src_length * 2,
				repl_char);
}

/**
 * @ingroup Conv
 * Returns the character encoding used by the current locale, for example
 * "UTF-8". @c NULL if unknown.
 *
 * Note applications must call
 * @code
 * setlocale (LC_ALL, "");
 * @endcode
 * to use the locale specified by the environment. The default C locale
 * uses ASCII encoding.
 *
 * @since 0.2.23
 */
const char *
vbi_locale_codeset		(void)
{
	const char *dst_format;

	dst_format = bind_textdomain_codeset (vbi_intl_domainname, NULL);

	if (NULL == dst_format)
		dst_format = "UTF-8";// nl_langinfo (CODESET);		// jhkim @2014-6-29, encoding is always utf-8 in Android.

	return dst_format; /* may be NULL */
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
