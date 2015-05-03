/* Generated file, do not edit! */

#include <stdio.h>
#include "io.h"

#ifndef __GNUC__
#undef __attribute__
#define __attribute__(x)
#endif

static void
fprint_struct_vbi_format (FILE *fp, int rw __attribute__ ((unused)), const struct vbi_format *t)
{
fprintf (fp, "sampling_rate=%lu "
"samples_per_line=%lu "
"sample_format=%lu "
"start[]=? "
"count[]=? "
"flags=",
(unsigned long) t->sampling_rate, 
(unsigned long) t->samples_per_line, 
(unsigned long) t->sample_format);
fprint_symbolic (fp, 2, t->flags,
"UNSYNC", (unsigned long) VBI_UNSYNC,
"INTERLACED", (unsigned long) VBI_INTERLACED,
(void *) 0);
fputs (" ", fp);
}

static void
fprint_struct_video_unit (FILE *fp, int rw __attribute__ ((unused)), const struct video_unit *t)
{
fprintf (fp, "video=%ld "
"vbi=%ld "
"radio=%ld "
"audio=%ld "
"teletext=%ld ",
(long) t->video, 
(long) t->vbi, 
(long) t->radio, 
(long) t->audio, 
(long) t->teletext);
}

static void
fprint_struct_video_tuner (FILE *fp, int rw __attribute__ ((unused)), const struct video_tuner *t)
{
fprintf (fp, "tuner=%ld "
"name=\"%.*s\" "
"rangelow=%lu "
"rangehigh=%lu "
"flags=",
(long) t->tuner, 
32, (const char *) t->name, 
(unsigned long) t->rangelow, 
(unsigned long) t->rangehigh);
fprint_symbolic (fp, 2, t->flags,
(void *) 0);
fputs (" mode=", fp);
fprint_symbolic (fp, 0, t->mode,
(void *) 0);
fprintf (fp, " signal=%lu ",
(unsigned long) t->signal);
}

static void
fprint_struct_video_channel (FILE *fp, int rw __attribute__ ((unused)), const struct video_channel *t)
{
fprintf (fp, "channel=%ld "
"name=\"%.*s\" "
"tuners=%ld "
"flags=",
(long) t->channel, 
32, (const char *) t->name, 
(long) t->tuners);
fprint_symbolic (fp, 2, t->flags,
"TUNER", (unsigned long) VIDEO_VC_TUNER,
(void *) 0);
fputs (" type=", fp);
fprint_symbolic (fp, 0, t->type,
"TV", (unsigned long) VIDEO_TYPE_TV,
(void *) 0);
fprintf (fp, " norm=%lu ",
(unsigned long) t->norm);
}

static void
fprint_struct_video_capability (FILE *fp, int rw __attribute__ ((unused)), const struct video_capability *t)
{
fprintf (fp, "name=\"%.*s\" "
"type=",
32, (const char *) t->name);
fprint_symbolic (fp, 0, t->type,
"CAPTURE", (unsigned long) VID_TYPE_CAPTURE,
"TELETEXT", (unsigned long) VID_TYPE_TELETEXT,
(void *) 0);
fprintf (fp, " channels=%ld "
"audios=%ld "
"maxwidth=%ld "
"maxheight=%ld "
"minwidth=%ld "
"minheight=%ld ",
(long) t->channels, 
(long) t->audios, 
(long) t->maxwidth, 
(long) t->maxheight, 
(long) t->minwidth, 
(long) t->minheight);
}

static void
fprint_ioctl_arg (FILE *fp, unsigned int cmd, int rw, void *arg)
{
switch (cmd) {
case VIDIOCGVBIFMT:
if (!arg) { fputs ("VIDIOCGVBIFMT", fp); return; }
case VIDIOCSVBIFMT:
if (!arg) { fputs ("VIDIOCSVBIFMT", fp); return; }
 fprint_struct_vbi_format (fp, rw, arg);
break;
case VIDIOCGFREQ:
if (!arg) { fputs ("VIDIOCGFREQ", fp); return; }
case VIDIOCSFREQ:
if (!arg) { fputs ("VIDIOCSFREQ", fp); return; }
 fprintf (fp, "%lu", (unsigned long) * (unsigned long *) arg);
break;
case VIDIOCGUNIT:
if (!arg) { fputs ("VIDIOCGUNIT", fp); return; }
 fprint_struct_video_unit (fp, rw, arg);
break;
case VIDIOCGTUNER:
if (!arg) { fputs ("VIDIOCGTUNER", fp); return; }
case VIDIOCSTUNER:
if (!arg) { fputs ("VIDIOCSTUNER", fp); return; }
 fprint_struct_video_tuner (fp, rw, arg);
break;
case VIDIOCGCHAN:
if (!arg) { fputs ("VIDIOCGCHAN", fp); return; }
case VIDIOCSCHAN:
if (!arg) { fputs ("VIDIOCSCHAN", fp); return; }
 fprint_struct_video_channel (fp, rw, arg);
break;
case VIDIOCGCAP:
if (!arg) { fputs ("VIDIOCGCAP", fp); return; }
 fprint_struct_video_capability (fp, rw, arg);
break;
	default:
		if (!arg) { fprint_unknown_ioctl (fp, cmd, arg); return; }
		break;
	}
}

static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCGCAP (struct video_capability *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCGCHAN (struct video_channel *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCSCHAN (const struct video_channel *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCGTUNER (struct video_tuner *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCSTUNER (const struct video_tuner *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCGFREQ (unsigned long *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCSFREQ (const unsigned long *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCGUNIT (struct video_unit *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCGVBIFMT (struct vbi_format *arg __attribute__ ((unused))) {}
static __inline__ void IOCTL_ARG_TYPE_CHECK_VIDIOCSVBIFMT (const struct vbi_format *arg __attribute__ ((unused))) {}

