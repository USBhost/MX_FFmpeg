/*
 *  libzvbi -- Video For Linux Two API 0.20 definitions
 *
 *  Author Michael H. Schimek
 *
 *  This file was not copied from the Linux kernel sources and in
 *  author's opinion contains only uncopyrightable facts which are
 *  necessary for interoperability.
 */

#ifndef VIDEODEV2_H
#define VIDEODEV2_H

#include <inttypes.h>

struct v4l2_capability {
	char			name[32];
	int			type;
	int			inputs;
	int			outputs;
	int			audios;
	int			maxwidth;
	int			maxheight;
	int			minwidth;
	int			minheight;
	int			maxframerate;
	uint32_t		flags;
	uint32_t		reserved[4];
};

#define VIDIOC_QUERYCAP _IOR ('V', 0, struct v4l2_capability)

#endif

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
