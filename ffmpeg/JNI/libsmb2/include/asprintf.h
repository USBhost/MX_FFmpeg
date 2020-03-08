
#ifndef _ASPRINTF_H_
#define _ASPRINTF_H_

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#ifndef _vscprintf
/* For some reason, MSVC fails to honour this #ifndef. */
/* Hence function renamed to _vscprintf_so(). */
static inline int _vscprintf_so(const char * format, va_list pargs) {
  int retval;
  va_list argcopy;
  va_copy(argcopy, pargs);
  retval = vsnprintf(NULL, 0, format, argcopy);
  va_end(argcopy);
  return retval;
}
#endif // _vscprintf

#ifndef vasprintf
static inline int vasprintf(char **strp, const char *fmt, va_list ap) {
  int len = _vscprintf_so(fmt, ap);
  if (len == -1) return -1;
  char *str = malloc((size_t)len + 1);
  if (!str) return -1;
  int r = vsnprintf(str, len + 1, fmt, ap); /* "secure" version of vsprintf */
  if (r == -1) return free(str), -1;
  *strp = str;
  return r;
}
#endif // vasprintf

#ifndef asprintf
static inline int asprintf(char *strp[], const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vasprintf(strp, fmt, ap);
  va_end(ap);
  return r;
}
#endif // asprintf

#endif // ! _ASPRINTF_H_
