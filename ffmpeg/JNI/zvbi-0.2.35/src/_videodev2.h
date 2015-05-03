/* Generated file, do not edit! */

#include <stdio.h>
#include "io.h"

#ifndef __GNUC__
#undef __attribute__
#define __attribute__(x)
#endif

static void
fprint_struct_v4l2_capability (FILE *fp, int rw __attribute__ ((unused)), const struct v4l2_capability *t)
{
fprintf (fp, "name=\"%.*s\" "
"type=",
32, (const char *) t->name);
fprint_symbolic (fp, 0, t->type,
(void *) 0);
fprintf (fp, " inputs=%ld "
"outputs=%ld "
"audios=%ld "
"maxwidth=%ld "
"maxheight=%ld "
"minwidth=%ld "
"minheight=%ld "
"maxframerate=%ld "
"flags=",
(long) t->inputs, 
(long) t->outputs, 
(long) t->audios, 
(long) t->maxwidth, 
(long) t->maxheight, 
(long) t->minwidth, 
(long) t->minheight, 
(long) t->maxframerate);
fprint_symbolic (fp, 2, t->flags,
(void *) 0);
fputs (" reserved[] ", fp);
}

static void
fprint_ioctl_arg (FILE *fp, unsigned int cmd, int rw, void *arg)
{
switch (cmd) {
case VIDIOC_QUERYCAP:
if (!arg) { fputs ("VIDIOC_QUERYCAP", fp); return; }
 fprint_struct_v4l2_capability (fp, rw, arg);
break;
	default:
		if (!arg) { fprint_unknown_ioctl (fp, cmd, arg); return; }
		break;
	}
}

static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOC_QUERYCAP (struct v4l2_capability *arg __attribute__ ((unused))) {}

