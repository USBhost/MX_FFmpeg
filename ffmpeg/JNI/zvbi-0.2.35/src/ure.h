/*
 * Copyright 1997, 1998, 1999 Computing Research Labs,
 * New Mexico State University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COMPUTING RESEARCH LAB OR NEW MEXICO STATE UNIVERSITY BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* $Id: ure.h,v 1.9 2008/02/19 00:35:22 mschimek Exp $ */

#ifndef _h_ure
#define _h_ure

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if defined(HAVE_GLIBC21) || defined(HAVE_LIBUNICODE)

#include <stdio.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#undef __
#ifdef __STDC__
#define __(x) x
#else
#define __(x) ()
#endif

/*
 * Error codes.
 */
#define _URE_OK               0
#define _URE_UNEXPECTED_EOS   -1
#define _URE_CCLASS_OPEN      -2
#define _URE_UNBALANCED_GROUP -3
#define _URE_INVALID_PROPERTY -4

/*
 * Options that can be combined for searching.
 */
/* mhs: not used, disabled #define URE_IGNORE_NONSPACING      0x01 */
#define URE_DOT_MATCHES_SEPARATORS 0x02
#define URE_NOTBOL		   0x04
#define URE_NOTEOL		   0x08

typedef uint32_t ucs4_t;
typedef uint16_t ucs2_t;

/*
 * Opaque type for memory used when compiling expressions.
 */
typedef struct _ure_buffer_t *ure_buffer_t;

/*
 * Opaque type for the minimal DFA used when matching.
 */
typedef struct _ure_dfa_t *ure_dfa_t;

/*************************************************************************
 *
 * API.
 *
 *************************************************************************/
  
/**
 * @internal
 *
 * Alloc memory for the regex internal buffer, NULL on error.
 * Use ure_buffer_free to free the returned buffer.
 *
 * @return
 * ure_buffer_t.
 */
extern ure_buffer_t ure_buffer_create __((void));

extern void ure_buffer_free __((ure_buffer_t buf));

/**
 * @internal
 * @param re Buffer containing the UCS-2 regexp.
 * @param relen Size in characters of the regexp.
 * @param casefold @c TRUE for matching disregarding case.
 * @param buf The regexp buffer.
 *
 * Compile the given expression into a dfa.
 *
 * @return
 * The compiled DFA, @c NULL on error.
 */
extern ure_dfa_t ure_compile __((ucs2_t *re, unsigned long relen,
                                 int casefold, ure_buffer_t buf));

extern void ure_dfa_free __((ure_dfa_t dfa));

extern void ure_write_dfa __((ure_dfa_t dfa, FILE *out));

/**
 * @internal
 * @param dfa The compiled expression.
 * @param flags Or'ed
 *    @c URE_IGNORED_NONSPACING: Set if nonspacing chars should be ignored.
 *    @c URE_DOT_MATCHES_SEPARATORS: Set if dot operator matches
 *    separator characters too.
 * @param text UCS-2 text to run the compiled regexp against.
 * @param textlen Size in characters of the text.
 * @param match_start Index in text of the first matching char.
 * @param match_end Index in text of the first non-matching char after the
 *              matching characters.
 *
 * Run the compiled regexp search on the given text.
 *
 * @return
 * @c TRUE if the search suceeded.
 */
extern int ure_exec __((ure_dfa_t dfa, int flags,
                        ucs2_t *text, unsigned long textlen,
                        unsigned long *match_start, unsigned long *match_end));

#undef __

#ifdef __cplusplus
}
#endif

#endif /* HAVE_GLIBC21 || HAVE_LIBUNICODE */

#endif /* _h_ure */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
