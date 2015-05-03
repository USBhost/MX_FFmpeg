/*
 *  libzvbi -- Video For Linux API definitions
 *
 *  Author Michael H. Schimek
 *
 *  This file was not copied from the Linux kernel sources and in
 *  author's opinion contains only uncopyrightable facts which are
 *  necessary for interoperability.
 */

#ifndef VIDEODEV_H
#define VIDEODEV_H

#include <inttypes.h>

#ifndef __LINUX_VIDEODEV2_H
/* type */
#  define VID_TYPE_CAPTURE 0x0001
#  define VID_TYPE_TELETEXT 0x0004
#endif

struct video_capability {
	char			name[32];
	int			type;
	int			channels;
	int			audios;
	int			maxwidth;
	int			maxheight;
	int			minwidth;
	int			minheight;
};

/* flags */
#define	VIDEO_VC_TUNER 0x0001

/* type */
#define VIDEO_TYPE_TV 0x0001

struct video_channel {
	int			channel;
	char			name[32];
	int			tuners;
	uint32_t		flags;
	uint16_t		type;
	uint16_t		norm;
};

/* mode */
enum {
	VIDEO_MODE_PAL = 0,
	VIDEO_MODE_NTSC,
	VIDEO_MODE_SECAM
};

struct video_tuner {
	int			tuner;
	char			name[32];
	unsigned long		rangelow;
	unsigned long		rangehigh;
	uint32_t		flags;
	uint16_t		mode;
	uint16_t		signal;
};

struct video_unit {
	int			video;
	int			vbi;
	int			radio;
	int			audio;
	int			teletext;
};

/* sample_format */
#define VIDEO_PALETTE_RAW 12

/* flags */
#define VBI_UNSYNC 0x0001
#define VBI_INTERLACED 0x0002

struct vbi_format {
	uint32_t		sampling_rate;
	uint32_t		samples_per_line;
	uint32_t		sample_format;
	int32_t			start[2];
	uint32_t		count[2];
	uint32_t		flags;
};

#define VIDIOCGCAP		_IOR ('v', 1, struct video_capability)
#define VIDIOCGCHAN		_IOWR ('v', 2, struct video_channel)
#define VIDIOCSCHAN		_IOW ('v', 3, struct video_channel)
#define VIDIOCGTUNER		_IOWR ('v', 4, struct video_tuner)
#define VIDIOCSTUNER		_IOW ('v', 5, struct video_tuner)
#define VIDIOCGFREQ		_IOR ('v', 14, unsigned long)
#define VIDIOCSFREQ		_IOW ('v', 15, unsigned long)
#define VIDIOCGUNIT		_IOR ('v', 21, struct video_unit)
#define VIDIOCGVBIFMT		_IOR ('v', 28, struct vbi_format)
#define VIDIOCSVBIFMT		_IOW ('v', 29, struct vbi_format)

#define BASE_VIDIOCPRIVATE	192

#endif

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
