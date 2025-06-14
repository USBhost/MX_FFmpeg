#include <stdio.h>
#include <fribidi.h>

int
main (
  void
)
{

  FriBidiChar c;

  for (c = 0; c < FRIBIDI_UNICODE_CHARS; c++)
    printf ("0x%04lX	%s\n", (long) c,
	    fribidi_get_bidi_type_name (fribidi_get_bidi_type (c)));

  return 0;
}
