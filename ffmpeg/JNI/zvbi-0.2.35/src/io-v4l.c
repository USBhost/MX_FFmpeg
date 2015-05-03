/*
 *  libzvbi -- Video For Linux driver interface
 *
 *  Copyright (C) 1999-2004 Michael H. Schimek
 *  Copyright (C) 2003, 2004 Tom Zoerner
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

static const char rcsid [] =
"$Id: io-v4l.c,v 1.39 2013/07/02 04:04:04 mschimek Exp $";

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "misc.h"
#include "vbi.h"
#include "io.h"

#ifdef ENABLE_V4L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>		/* read(), dup2(), getuid() */
#include <assert.h>
#include <sys/time.h>		/* timeval */
#include <sys/types.h>		/* fd_set, uid_t */
#include <sys/stat.h>		/* S_ISCHR */
#include <sys/ioctl.h>		/* for (_)videodev.h */
#include <pthread.h>

#include "videodev.h"
#include "_videodev.h"

/* This macro checks at compile time if the arg type is correct,
   device_ioctl() repeats the ioctl if interrupted (EINTR) and logs
   the args and result if sys_log_fp is non-zero. */
#define xioctl(v, cmd, arg)						\
(IOCTL_ARG_TYPE_CHECK_ ## cmd (arg),					\
 device_ioctl (v->capture.sys_log_fp, fprint_ioctl_arg, v->fd,		\
               cmd, (void *)(arg)))

#define xioctl_fd(v, fd, cmd, arg)					\
(IOCTL_ARG_TYPE_CHECK_ ## cmd (arg),					\
 device_ioctl (v->capture.sys_log_fp, fprint_ioctl_arg, fd,		\
               cmd, (void *)(arg)))

/* Custom ioctl of the bttv driver. */
#define BTTV_VBISIZE		_IOR('v' , BASE_VIDIOCPRIVATE+8, int)
static __inline__ void IOCTL_ARG_TYPE_CHECK_BTTV_VBISIZE
  (const int *arg _vbi_unused) {}

#undef REQUIRE_SELECT
#undef REQUIRE_SVBIFMT		/* else accept current parameters */
#undef REQUIRE_VIDEOSTD		/* if clueless, assume PAL/SECAM */

#define FLUSH_FRAME_COUNT       2

#define printv(format, args...)						\
do {									\
	if (v->do_trace) {						\
		fprintf(stderr, "libzvbi: " format ,##args);		\
		fflush(stderr);						\
	}								\
} while (0)

typedef struct vbi_capture_v4l {
	vbi_capture		capture;

	int			fd;
	vbi_bool		has_select;
	vbi_bool		read_active;
	vbi_bool		do_trace;
	signed char		has_s_fmt;
	struct video_capability vcap;
	char		      * p_dev_name;
	char		      * p_video_name;
        int                     fd_video;

	vbi_raw_decoder		dec;
        unsigned int            services; /* all services, including raw */

	double			time_per_frame;

	vbi_capture_buffer	*raw_buffer;
	int			num_raw_buffers;

	vbi_capture_buffer	sliced_buffer;
	int			flush_frame_count;

} vbi_capture_v4l;


static void
v4l_read_stop(vbi_capture_v4l *v)
{
	for (; v->num_raw_buffers > 0; v->num_raw_buffers--) {
		free(v->raw_buffer[v->num_raw_buffers - 1].data);
		v->raw_buffer[v->num_raw_buffers - 1].data = NULL;
	}

	free(v->raw_buffer);
	v->raw_buffer = NULL;
}


static int
v4l_suspend(vbi_capture_v4l *v)
{
	int    fd;

	v4l_read_stop(v);

	if (v->read_active) {
		printv("Suspending read: re-open device...\n");

		/* hack: cannot suspend read to allow SVBIFMT,
		   need to close device */
		fd = device_open (v->capture.sys_log_fp,
				  v->p_dev_name, O_RDWR, 0);
		if (-1 == fd) {
			printv ("v4l2-suspend: failed to re-open "
				"VBI device: %d: %s\n",
				errno, strerror(errno));
			return -1;
		}

		/* use dup2() to keep the same fd,
		   which may be used by our client */
		device_close (v->capture.sys_log_fp, v->fd);
		dup2 (fd, v->fd);
		device_close (v->capture.sys_log_fp, fd);

		v->read_active = FALSE;
	}
	return 0;
}


static int
v4l_read_alloc(vbi_capture_v4l *v, char ** errstr)
{
	assert(v->raw_buffer == NULL);

	v->raw_buffer = calloc(1, sizeof(v->raw_buffer[0]));

	if (v->raw_buffer == NULL) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->raw_buffer[0].size = (v->dec.count[0] + v->dec.count[1])
				* v->dec.bytes_per_line;

	v->raw_buffer[0].data = malloc(v->raw_buffer[0].size);

	if (v->raw_buffer[0].data == NULL) {
		asprintf(errstr, _("Not enough memory to allocate "
				   "vbi capture buffer (%d KB)."),
			 (v->raw_buffer[0].size + 1023) >> 10);
		goto failure;
	}

	v->num_raw_buffers = 1;

	printv("Capture buffer allocated: %d bytes\n", v->raw_buffer[0].size);

	return 0;

failure:
	v4l_read_stop(v);
	return -1;
}


static int
v4l_read_frame(vbi_capture_v4l *v, vbi_capture_buffer *raw, struct timeval *timeout)
{
	struct timeval tv;
	int r;

	if (v->has_select) {
		tv = *timeout;

		r = vbi_capture_io_select(v->fd, &tv);
		if (r <= 0)
			return r;
	}

	v->read_active = TRUE;

	for (;;) {
		pthread_testcancel();

		r = read(v->fd, raw->data, raw->size);

		if (r == -1 && (errno == EINTR || errno == ETIME))
			continue;
		if (r == -1)
                        return -1;
		if (r != raw->size) {
			errno = EIO;
			return -1;
		}
		else
			break;
	}
	return 1;
}

static int
v4l_read(vbi_capture *vc, vbi_capture_buffer **raw,
	 vbi_capture_buffer **sliced, const struct timeval *timeout_orig)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);
	vbi_capture_buffer *my_raw = v->raw_buffer;
	struct timeval tv;
	int r;

	if (my_raw == NULL) {
		printv("read buffer not allocated (must add services first)\n");
		errno = EINVAL;
		return -1;
	}

	if (raw == NULL)
		raw = &my_raw;
	if (*raw == NULL)
		*raw = v->raw_buffer;
	else
		(*raw)->size = v->raw_buffer[0].size;

	tv = *timeout_orig;
	while (1)
	{
		r = v4l_read_frame(v, *raw, &tv);
		if (r <= 0)
			return r;

		if (v->flush_frame_count > 0) {
			v->flush_frame_count -= 1;
			printv("Skipping frame (%d remaining)\n", v->flush_frame_count);
		}
		else
			break;
	}

	gettimeofday(&tv, NULL);
	(*raw)->timestamp = tv.tv_sec + tv.tv_usec * (1 / 1e6);

	if (sliced) {
		int lines;

		if (*sliced) {
			lines = vbi_raw_decode(&v->dec, (*raw)->data,
					       (vbi_sliced *)(*sliced)->data);
		} else {
			*sliced = &v->sliced_buffer;
			lines = vbi_raw_decode(&v->dec, (*raw)->data,
					       (vbi_sliced *)(v->sliced_buffer.data));
		}

		(*sliced)->size = lines * sizeof(vbi_sliced);
		(*sliced)->timestamp = (*raw)->timestamp;
	}

	return 1;
}

static void v4l_flush(vbi_capture *vc)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);
	struct timeval tv;
	int fd_flags = 0;
	int r;

	if ( (v->raw_buffer == NULL) || (v->read_active == FALSE) )
		return;

	v->flush_frame_count = FLUSH_FRAME_COUNT;

	if (v->has_select) {
		memset(&tv, 0, sizeof(tv));

		r = vbi_capture_io_select(v->fd, &tv);
		if (r <= 0)
			return;
	}

	if (v->has_select == FALSE) {
		fd_flags = fcntl(v->fd, F_GETFL, NULL);
		if (fd_flags == -1)
			return;
		/* no select supported by driver -> make read non-blocking */
		if ((fd_flags & O_NONBLOCK) == 0) {
			fcntl(v->fd, F_SETFL, fd_flags | O_NONBLOCK);
		}
	}

	r = read(v->fd, v->raw_buffer->data, v->raw_buffer->size);

	if ((v->has_select == FALSE) && ((fd_flags & O_NONBLOCK) == 0)) {
		fcntl(v->fd, F_SETFL, fd_flags);
	}
}


/* Molto rumore per nulla. */

#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static void
perm_check(vbi_capture_v4l *v, const char *name)
{
	struct stat st;
	int old_errno = errno;
	uid_t uid = geteuid();
	gid_t gid = getegid();

	if (stat(name, &st) == -1) {
		printv("stat %s failed: %d, %s\n", name, errno, strerror(errno));
		errno = old_errno;
		return;
	}

	printv("%s permissions: user=%d.%d mode=0%o, I am %d.%d\n",
		name, st.st_uid, st.st_gid, st.st_mode, uid, gid);

	errno = old_errno;
}

static vbi_bool
reverse_lookup(vbi_capture_v4l *v, int fd, struct stat *vbi_stat)
{
	struct video_capability vcap;
	struct video_unit vunit;

	CLEAR (vcap);

	if (-1 == xioctl_fd (v, fd, VIDIOCGCAP, &vcap)) {
		printv ("Driver doesn't support VIDIOCGCAP, "
			"probably not V4L API\n");
		return FALSE;
	}

	if (!(vcap.type & VID_TYPE_CAPTURE)) {
		printv("Driver is no video capture device\n");
		return FALSE;
	}

	CLEAR (vunit);

	if (-1 == xioctl_fd (v, fd, VIDIOCGUNIT, &vunit)) {
		printv ("Driver doesn't support VIDIOCGUNIT\n");
		return FALSE;
	}

	if (vunit.vbi != (int) minor(vbi_stat->st_rdev)) {
		printv("Driver reports vbi minor %d, need %d\n",
			vunit.vbi, minor(vbi_stat->st_rdev));
		return FALSE;
	}

	printv("Matched\n");
	return TRUE;
}

static void
set_scanning_from_mode(vbi_capture_v4l *v, int mode, int * strict)
{
	switch (mode) {
	case VIDEO_MODE_NTSC:
		printv("Videostandard is NTSC\n");
		v->dec.scanning = 525;
		break;

	case VIDEO_MODE_PAL:
	case VIDEO_MODE_SECAM:
		printv("Videostandard is PAL/SECAM\n");
		v->dec.scanning = 625;
		break;

	default:
		/*
		 *  One last chance, we'll try to guess
		 *  the scanning if GVBIFMT is available.
		 */
		printv("Videostandard unknown (%d)\n", mode);
		v->dec.scanning = 0;
		*strict = TRUE;
		break;
	}
}

static vbi_bool
get_videostd(vbi_capture_v4l *v, int fd, int *mode)
{
	struct video_tuner vtuner;
	struct video_channel vchan;

	CLEAR (vtuner);
	CLEAR (vchan);

	if (0 == xioctl_fd (v, fd, VIDIOCGTUNER, &vtuner)) {
		printv ("Driver supports VIDIOCGTUNER: "
			"mode %d (0=PAL, 1=NTSC, 2=SECAM)\n", vtuner.mode);
		*mode = vtuner.mode;
		return TRUE;
	} else if (0 == xioctl_fd (v, fd, VIDIOCGCHAN, &vchan)) {
		printv ("Driver supports VIDIOCGCHAN: norm %d\n", vchan.norm);
		*mode = vchan.norm;
		return TRUE;
	} else
		printv("Driver doesn't support VIDIOCGTUNER or VIDIOCGCHAN\n");

	return FALSE;
}

static int
probe_video_device(vbi_capture_v4l *v, const char *name, struct stat *vbi_stat )
{
	struct stat vid_stat;
	int video_fd;

	if (stat(name, &vid_stat) == -1) {
		printv("stat failed: %d, %s\n",	errno, strerror(errno));
		return -1;
	}

	if (!S_ISCHR(vid_stat.st_mode)) {
		printv("%s is no character special file\n", name);
		return -1;
	}

	if (major(vid_stat.st_rdev) != major(vbi_stat->st_rdev)) {
		printv("Mismatch of major device number: "
			"%s: %d, %d; vbi: %d, %d\n", name,
			major(vid_stat.st_rdev), minor(vid_stat.st_rdev),
			major(vbi_stat->st_rdev), minor(vbi_stat->st_rdev));
		return -1;
	}

	/* when radio device is opened a running video capture is destroyed (v4l2) @TZO@ */
	if (minor(vid_stat.st_rdev) >= 64) {
		printv("Not a v4l video minor device number (i.e. >= 64): "
			"%s: %d, %d\n", name,
			major(vid_stat.st_rdev), minor(vid_stat.st_rdev));
		return -1;
	}

	video_fd = device_open (v->capture.sys_log_fp, name, O_RDWR, 0);
	if (-1 == video_fd) {
		printv ("Cannot open %s: %d, %s\n",
			name, errno, strerror(errno));
		perm_check(v, name);
		return -1;
	}

	if (!reverse_lookup(v, video_fd, vbi_stat)) {
		device_close (v->capture.sys_log_fp, video_fd);
		return -1;
	}

	return video_fd;
}

static vbi_bool
xopendir			(const char *		name,
				 DIR **			dir,
				 struct dirent **	dirent)
{
	int saved_errno;
	long int size;
	int fd;

	*dir = opendir (name);
	if (NULL == *dir)
		return FALSE;

	fd = dirfd (*dir);
	if (-1 == fd)
		goto failure;

	size = fpathconf (fd, _PC_NAME_MAX);
	if (size <= 0)
		goto failure;

	size = MAX (size, (long int) sizeof ((*dirent)->d_name));
	size += sizeof (**dirent) - sizeof ((*dirent)->d_name) + 1;
	*dirent = calloc (1, size);
	if (NULL == *dirent)
		goto failure;

	return TRUE;

 failure:
	saved_errno = errno;

	closedir (*dir);
	*dir = NULL;

	errno = saved_errno;

	return FALSE;
}

static int
open_video_dev(vbi_capture_v4l *v, struct stat *p_vbi_stat, vbi_bool do_dev_scan)
{
	static const char * const video_devices[] = {
		"/dev/video",
		"/dev/video0",
		"/dev/video1",
		"/dev/video2",
		"/dev/video3",
		"/dev/v4l/video",
		"/dev/v4l/video0",
		"/dev/v4l/video1",
		"/dev/v4l/video2",
		"/dev/v4l/video3",
	};
	struct dirent *dirent;
	struct dirent *pdirent;
	DIR *dir;
	int video_fd;
	unsigned int i;

	video_fd = -1;

	for (i = 0; i < sizeof(video_devices) / sizeof(video_devices[0]); i++) {
		printv("Try %s: ", video_devices[i]);

		video_fd = probe_video_device(v, video_devices[i], p_vbi_stat);
		if (video_fd != -1) {
			v->p_video_name = strdup(video_devices[i]);
			goto done;
		}
	}

	if (do_dev_scan) {
		/* @TOMZO@ note: this is insane - dev directory has typically ~4000 nodes */

		printv("Traversing /dev\n");

		if (!xopendir ("/dev", &dir, &dirent)) {
			printv ("Cannot open /dev: %d, %s\n",
				errno, strerror (errno));
			perm_check (v, "/dev");
			goto done;
		}

		while (0 == readdir_r (dir, dirent, &pdirent)
		       && pdirent == dirent) {
			char name[256];

			snprintf (name, sizeof(name),
				  "/dev/%s", dirent->d_name);

			printv("Try %s: ", name);

			video_fd = probe_video_device(v, name, p_vbi_stat);
			if (video_fd != -1) {
				v->p_video_name = strdup(name);
				free (dirent);
				closedir (dir);
				goto done;
			}
		}
		printv("Traversing finished\n");

		free (dirent);
		closedir (dir);
	}
	errno = ENOENT;

 done:
	return video_fd;
}

static vbi_bool
guess_bttv_v4l(vbi_capture_v4l *v, int *strict,
	       int given_fd, int scanning)
{
	struct stat vbi_stat;
	int video_fd;
	int mode = -1;

	if (scanning) {
		v->dec.scanning = scanning;
		return TRUE;
	}

	printv("Attempt to guess the videostandard\n");

	if (get_videostd(v, v->fd, &mode))
		goto finish;

	/*
	 *  Bttv v4l has no VIDIOCGUNIT pointing back to
	 *  the associated video device, now it's getting
	 *  dirty. We'll walk /dev, first level of, and
	 *  assume v4l major is still 81. Not tested with devfs.
	 */
	printv("Attempt to find a reverse VIDIOCGUNIT\n");

	if (fstat(v->fd, &vbi_stat) == -1) {
		printv("fstat failed: %d, %s\n", errno, strerror(errno));
		goto finish;
	}

	if (!S_ISCHR(vbi_stat.st_mode)) {
		printv("VBI device is no character special file, reject\n");
		return FALSE;
	}

	if (major(vbi_stat.st_rdev) != 81) {
		printv("VBI device CSF has major number %d, expect 81\n"
			"Warning: will assume this is still a v4l device\n",
			major(vbi_stat.st_rdev));
		goto finish;
	}

	printv("VBI device type verified\n");

	if (given_fd > -1) {
		printv("Try suggested corresponding video fd\n");

		if (reverse_lookup(v, given_fd, &vbi_stat)) {
			if (get_videostd(v, given_fd, &mode))
                                v->fd_video = given_fd;
				goto finish;
		}
	}

	/* find video device path and open the device */
	video_fd = open_video_dev(v, &vbi_stat, TRUE);
	if (video_fd != -1) {
		if (get_videostd(v, video_fd, &mode)) {
			device_close (v->capture.sys_log_fp, video_fd);
			return FALSE;
		}
		device_close (v->capture.sys_log_fp, video_fd);
	}


 finish:
	set_scanning_from_mode(v, mode, strict);

	return TRUE;
}

static vbi_bool
v4l_update_scanning(vbi_capture_v4l *v, int * p_strict)
{
	int video_fd;
	int mode = -1;
        vbi_bool result = FALSE;

	if ( get_videostd(v, v->fd, &mode) ) {

                result = TRUE;

        } else if (v->p_video_name != NULL) {

                video_fd = device_open (v->capture.sys_log_fp,
					v->p_video_name, O_RDWR, 0);
                if (-1 != video_fd) {

                        if (get_videostd(v, video_fd, &mode)) {
                                result = TRUE;
                        }
                        device_close (v->capture.sys_log_fp, video_fd);
                } else {
                        printv("Failed to open video device '%d': %s", errno, strerror(errno));
                }

        } else if (v->fd_video != -1) {

                if (get_videostd(v, v->fd_video, &mode)) {
                        result = TRUE;
                }
        }

        if (result)
	        set_scanning_from_mode(v, mode, p_strict);

	return result;
}

static int
v4l_get_scanning(vbi_capture *vc)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);
        int strict;
        int old_scanning = v->dec.scanning;
        int new_scanning = -1;

        if ( v4l_update_scanning(v, &strict) ) {

                new_scanning = v->dec.scanning;
        }
        v->dec.scanning = old_scanning;

	printv("Guessed video standard %d\n", new_scanning);

        return new_scanning;
}

static vbi_bool
set_parameters(vbi_capture_v4l *v, struct vbi_format *p_vfmt, int *p_max_rate,
	       unsigned int *services, int strict,
	       char **errstr)
{
	struct vbi_format vfmt_temp;
	vbi_raw_decoder dec_temp;
	unsigned int sup_services;

	/* check if the driver supports CSVBIFMT: try with unchanged parameters */
	if (v->has_s_fmt == -1) {
		vfmt_temp = *p_vfmt;
		v->has_s_fmt = (0 == xioctl (v, VIDIOCSVBIFMT, &vfmt_temp)
				|| errno == EBUSY);
		printv ("Driver does%s support VIDIOCSVBIFMT\n",
			v->has_s_fmt ? "" : " not");
	}

	if (v->has_s_fmt == 0)
		return TRUE;

	/* Speculative, vbi_format is not documented */

	printv("Attempt to set vbi capture parameters\n");

	memset(&dec_temp, 0, sizeof(dec_temp));
	sup_services = vbi_raw_decoder_parameters(&dec_temp, *services | v->services,
					          dec_temp.scanning, p_max_rate);

	if ((sup_services & *services) == 0) {
		asprintf(errstr, _("Sorry, %s (%s) cannot capture any of the "
					 "requested data services."),
			     v->p_dev_name, v->vcap.name);
		return FALSE;
	}

	*services &= sup_services;

	vfmt_temp = *p_vfmt;
	memset(p_vfmt, 0, sizeof(*p_vfmt));

	p_vfmt->sample_format		= VIDEO_PALETTE_RAW;
	p_vfmt->sampling_rate		= dec_temp.sampling_rate;
	p_vfmt->samples_per_line	= dec_temp.bytes_per_line;
	p_vfmt->start[0]		= dec_temp.start[0];
	p_vfmt->count[0]		= dec_temp.count[1];
	p_vfmt->start[1]		= dec_temp.start[0];
	p_vfmt->count[1]		= dec_temp.count[1];

	/* Single field allowed? */

	if (!p_vfmt->count[0]) {
		p_vfmt->start[0] = (dec_temp.scanning == 625) ? 6 : 10;
		p_vfmt->count[0] = 1;
	} else if (!p_vfmt->count[1]) {
		p_vfmt->start[1] = (dec_temp.scanning == 625) ? 318 : 272;
		p_vfmt->count[1] = 1;
	}

	if (0 == xioctl (v, VIDIOCSVBIFMT, p_vfmt))
		return TRUE;

	p_vfmt->sampling_rate		= vfmt_temp.sampling_rate;
	p_vfmt->samples_per_line	= vfmt_temp.samples_per_line;
	if (0 == xioctl (v, VIDIOCSVBIFMT, p_vfmt))
		return TRUE;

	/* XXX correct count */
	p_vfmt->start[0]		= vfmt_temp.start[0];
	p_vfmt->start[1]		= vfmt_temp.start[1];
	if (0 == xioctl (v, VIDIOCSVBIFMT, p_vfmt))
		return TRUE;

	switch (errno) {
	case EBUSY:
#ifndef REQUIRE_SVBIFMT
		printv("VIDIOCSVBIFMT returned EBUSY, "
		       "will try the current parameters\n");
		*p_vfmt = vfmt_temp;
		return TRUE;
#endif
		asprintf(errstr, _("Cannot initialize %s (%s), "
				   "the device is already in use."),
			 v->p_dev_name, v->vcap.name);
		break;

	case EINVAL:
                if (strict < 2) {
		        printv("VIDIOCSVBIFMT returned EINVAL, "
		               "will try the current parameters\n");
                        *p_vfmt = vfmt_temp;
                        return TRUE;
                }
		break;
	default:
		asprintf(errstr, _("Could not set the vbi "
				   "capture parameters for %s (%s): %s."),
			     v->p_dev_name, v->vcap.name, strerror(errno));
		/* guess = _("Maybe a bug in the driver or libzvbi."); */
		break;
	}

	return FALSE;
}

static vbi_raw_decoder *
v4l_parameters(vbi_capture *vc)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);

	return &v->dec;
}

static void
v4l_delete(vbi_capture *vc)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);

	v4l_read_stop(v);

	vbi_raw_decoder_destroy(&v->dec);

	if (v->sliced_buffer.data)
		free(v->sliced_buffer.data);

	if (v->p_dev_name != NULL)
		free(v->p_dev_name);

	if (v->p_video_name != NULL)
		free(v->p_video_name);

	if (-1 != v->fd)
		device_close (v->capture.sys_log_fp, v->fd);

	free(v);
}

static VBI_CAPTURE_FD_FLAGS
v4l_get_fd_flags(vbi_capture *vc)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);
	VBI_CAPTURE_FD_FLAGS result;

        result = VBI_FD_IS_DEVICE;
        if (v->has_select)
                result |= VBI_FD_HAS_SELECT;

        return result;
}

static vbi_bool
v4l_set_video_path(vbi_capture *vc, const char * p_dev_video)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);

	if (v->p_video_name != NULL)
		free(v->p_video_name);

	v->p_video_name = strdup(p_dev_video);

        return TRUE;
}

static int
v4l_get_fd(vbi_capture *vc)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);

	return v->fd;
}

static void
print_vfmt(const char *s, struct vbi_format *vfmt)
{
	fprintf(stderr, "%sformat %08x, %d Hz, %d bpl, "
		"F1 %d+%d, F2 %d+%d, flags %08x\n", s,
		vfmt->sample_format,
		vfmt->sampling_rate, vfmt->samples_per_line,
		vfmt->start[0], vfmt->count[0],
		vfmt->start[1], vfmt->count[1],
		vfmt->flags);
}

static unsigned int
v4l_update_services(vbi_capture *vc,
		    vbi_bool reset, vbi_bool commit,
		    unsigned int services, int strict,
		    char ** errstr)
{
	vbi_capture_v4l *v = PARENT(vc, vbi_capture_v4l, capture);
	struct vbi_format vfmt;
	int max_rate;

	max_rate = 0;

	/* suspend capturing, or driver will return EBUSY */
	v4l_suspend(v);

	if (reset) {
                v4l_update_scanning(v, &strict);

		vbi_raw_decoder_reset(&v->dec);
                v->services = 0;
        }

	CLEAR (vfmt);

	if (0 == xioctl (v, VIDIOCGVBIFMT, &vfmt)) {
		if (vfmt.start[1] > 0 && vfmt.count[1]) {
			if (vfmt.start[1] >= 286)
				v->dec.scanning = 625;
			else
				v->dec.scanning = 525;
		}

		printv("Driver supports VIDIOCGVBIFMT, "
		       "guessed videostandard %d\n", v->dec.scanning);

		if (v->do_trace)
			print_vfmt("VBI capture parameters supported: ", &vfmt);

		if (strict >= 0 && v->dec.scanning)
			if (!set_parameters(v, &vfmt, &max_rate,
					    &services, strict,
					    errstr))
				goto failure;

		if (v->do_trace)
			print_vfmt("VBI capture parameters granted: ", &vfmt);

		printv("Accept current vbi parameters\n");

		if (vfmt.sample_format != VIDEO_PALETTE_RAW) {
			asprintf(errstr, _("%s (%s) offers unknown vbi "
					   "sampling format #%d. "
					   "This may be a driver bug "
					   "or libzvbi is too old."),
				 v->p_dev_name, v->vcap.name,
				 vfmt.sample_format);
			goto io_error;
		}

		/* grow pattern array if necessary
		** note: must do this even if service add fails later, to stay in sync with driver */
		vbi_raw_decoder_resize(&v->dec, vfmt.start, vfmt.count);

		v->dec.sampling_rate		= vfmt.sampling_rate;
		v->dec.bytes_per_line 		= vfmt.samples_per_line;
		if (v->dec.scanning == 625)
			/* v->dec.offset 		= (int)(10.2e-6 * vfmt.sampling_rate); */
			v->dec.offset           = (int)(6.8e-6 * vfmt.sampling_rate);
		else if (v->dec.scanning == 525)
			v->dec.offset		= (int)(9.2e-6 * vfmt.sampling_rate);
		else /* we don't know */
			v->dec.offset		= (int)(9.7e-6 * vfmt.sampling_rate);
		v->dec.start[0] 		= vfmt.start[0];
		v->dec.count[0] 		= vfmt.count[0];
		v->dec.start[1] 		= vfmt.start[1];
		v->dec.count[1] 		= vfmt.count[1];
		v->dec.interlaced		= !!(vfmt.flags & VBI_INTERLACED);
		v->dec.synchronous		= !(vfmt.flags & VBI_UNSYNC);
		v->time_per_frame 		= (v->dec.scanning == 625) ?
						  1.0 / 25 : 1001.0 / 30000;

		/* Unknown. */
		v->has_select = FALSE;
	} else { 
		int size;

		/*
		 *  If a more reliable method exists to identify the bttv
		 *  driver I'll be glad to hear about it. Lesson: Don't
		 *  call a v4l private IOCTL without knowing who's
		 *  listening. All we know at this point: It's a csf, and
		 *  it may be a v4l device.
		 *  garetxe: This isn't reliable, bttv doesn't return
		 *  anything useful in vcap.name.
		 */
		printv("Driver doesn't support VIDIOCGVBIFMT (errno %d), "
		       "will assume bttv interface\n", errno);

		/* bttv 0.7.x has no select. 0.8+ supports VIDIOCGVBIFMT. */
		v->has_select = FALSE;

		if (0 && !strstr(v->vcap.name, "bttv")
		      && !strstr(v->vcap.name, "BTTV")) {
			asprintf(errstr, _("Cannot capture with %s (%s), "
					   "has no standard vbi interface."),
				 v->p_dev_name, v->vcap.name);
			goto io_error;
		}

		v->dec.bytes_per_line 		= 2048;
		v->dec.interlaced		= FALSE;
		v->dec.synchronous		= TRUE;

		printv("Attempt to determine vbi frame size\n");

		size = xioctl (v, BTTV_VBISIZE, 0);
		if (-1 == size) {
			printv ("Driver does not support BTTV_VBISIZE, "
				"assume old BTTV driver\n");
			v->dec.count[0] = 16;
			v->dec.count[1] = 16;
		} else if (size % 2048) {
			asprintf (errstr,
				  _("Cannot identify %s (%s), reported "
				    "vbi frame size suggests this is "
				    "not a bttv driver."),
				  v->p_dev_name, v->vcap.name);
			goto io_error;
		} else {
			printv ("Driver supports BTTV_VBISIZE: %d bytes, "
				"assume top field dominance and 2048 bpl\n",
				size);
			size /= 2048;
			v->dec.count[0] = size >> 1;
			v->dec.count[1] = size - v->dec.count[0];
		}

		switch (v->dec.scanning) {
		default:
#ifdef REQUIRE_VIDEOSTD
			asprintf(errstr, _("Cannot set or determine current "
					   "videostandard of %s (%s)."),
				 v->p_dev_name, v->vcap.name);
			goto io_error;
#endif
			printv("Warning: Videostandard not confirmed, "
			       "will assume PAL/SECAM\n");

			v->dec.scanning = 625;

			/* fall through */

		case 625:
			/* Not confirmed */
			v->dec.sampling_rate = 35468950;
			v->dec.offset = (int)(9.2e-6 * 35468950);
			v->dec.start[0] = 22 + 1 - v->dec.count[0];
			v->dec.start[1] = 335 + 1 - v->dec.count[1];
			break;

		case 525:
			/* Confirmed for bttv 0.7.52 */
			v->dec.sampling_rate = 28636363;
			v->dec.offset = (int)(9.2e-6 * 28636363);
			v->dec.start[0] = 10;
			v->dec.start[1] = 273;
			break;
		}

		v->time_per_frame =
			(v->dec.scanning == 625) ? 1.0 / 25 : 1001.0 / 30000;
	}

	v->dec.sampling_format = VBI_PIXFMT_YUV420;

	if (services & ~(VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625)) {
		/* Nyquist */

		if (v->dec.sampling_rate < max_rate * 3 / 2) {
			asprintf(errstr, _("Cannot capture the requested "
					   "data services with "
					   "%s (%s), the sampling frequency "
					   "%.2f MHz is too low."),
				 v->p_dev_name, v->vcap.name,
				 v->dec.sampling_rate / 1e6);
                        services = 0;
			goto failure;
		}

		printv("Nyquist check passed\n");

		printv("Request decoding of services 0x%08x, strict level %d\n", services, strict);

		/* those services which are already set must be checked for strictness */
		if ( (strict > 0) && ((services & v->dec.services) != 0) ) {
			unsigned int tmp_services;
			tmp_services = vbi_raw_decoder_check_services(&v->dec, services & v->dec.services, strict);
			/* mask out unsupported services */
			services &= tmp_services | ~(services & v->dec.services);
		}

		if ( (services & ~v->dec.services) != 0 )
		        services &= vbi_raw_decoder_add_services(&v->dec,
							         services & ~ v->dec.services,
							         strict);

		if (services == 0) {
			asprintf(errstr, _("Sorry, %s (%s) cannot "
					   "capture any of "
					   "the requested data services."),
				 v->p_dev_name, v->vcap.name);
			goto failure;
		}

		if (v->sliced_buffer.data != NULL)
			free(v->sliced_buffer.data);

		v->sliced_buffer.data =
			malloc((v->dec.count[0] + v->dec.count[1])
			       * sizeof(vbi_sliced));

		if (!v->sliced_buffer.data) {
			asprintf(errstr, _("Virtual memory exhausted."));
			errno = ENOMEM;
			goto failure;
		}
	}

failure:
        v->services |= services;
	printv("Will capture services 0x%08x, added 0x%0x commit:%d\n", v->services, services, commit);

	if (commit && (v->services != 0))
		v4l_read_alloc(v, errstr);

	return services;

io_error:
	return 0;
}

static vbi_capture *
v4l_new(const char *dev_name, int given_fd, int scanning,
	unsigned int *services, int strict,
	char **errstr, vbi_bool trace)
{
	char *error = NULL;

	vbi_capture_v4l *v;

	pthread_once (&vbi_init_once, vbi_init);

	if (!errstr)
		errstr = &error;
	*errstr = NULL;

	if (scanning != 525 && scanning != 625)
		scanning = 0;

	if (!(v = (vbi_capture_v4l *) calloc(1, sizeof(*v)))) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	vbi_raw_decoder_init (&v->dec);

	v->do_trace = trace;

	printv ("Try to open v4l vbi device, "
		"libzvbi interface rev.\n  %s\n", rcsid);

	v->capture.parameters = v4l_parameters;
	v->capture._delete = v4l_delete;
	v->capture.get_fd = v4l_get_fd;
	v->capture.get_fd_flags = v4l_get_fd_flags;
	v->capture.read = v4l_read;
	v->capture.update_services = v4l_update_services;
	v->capture.get_scanning = v4l_get_scanning;
	v->capture.flush = v4l_flush;
	v->capture.set_video_path = v4l_set_video_path;

	if (0)
		v->capture.sys_log_fp = stderr;

	v->p_dev_name = strdup(dev_name);

	if (v->p_dev_name == NULL) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->fd = device_open (v->capture.sys_log_fp,
			     v->p_dev_name, O_RDONLY, 0);
	if (-1 == v->fd) {
		asprintf (errstr, _("Cannot open '%s': %d, %s."),
			  v->p_dev_name, errno, strerror(errno));
		perm_check(v, v->p_dev_name);
		goto io_error;
	}

	printv("Opened %s\n", v->p_dev_name);

        /* used to store given_fd if necessary */
	v->fd_video = -1;

	if (-1 == xioctl (v, VIDIOCGCAP, &v->vcap)) {
		/*
		 *  Older bttv drivers don't support any
		 *  v4l ioctls, let's see if we can guess the beast.
		 */
		printv("Driver doesn't support VIDIOCGCAP\n");
		strlcpy(v->vcap.name, _("driver unknown"), sizeof(v->vcap.name) - 1);
		v->vcap.name[sizeof(v->vcap.name) - 1] = 0;

		if (!guess_bttv_v4l(v, &strict, given_fd, scanning))
			goto failure;
	} else {
		if (v->vcap.name[0] != 0) {
			printv("Driver name '%s'\n", v->vcap.name);
		} else {
			strlcpy(v->vcap.name, _("driver unknown"), sizeof(v->vcap.name) - 1);
			v->vcap.name[sizeof(v->vcap.name) - 1] = 0;
		}

		if (!(v->vcap.type & VID_TYPE_TELETEXT)) {
			asprintf(errstr,
				 _("%s (%s) is not a raw vbi device."),
				 v->p_dev_name, v->vcap.name);
			goto failure;
		}

		guess_bttv_v4l(v, &strict, given_fd, scanning);
	}

	printv("%s (%s) is a v4l vbi device\n", v->p_dev_name, v->vcap.name);

	v->has_select = FALSE; /* FIXME if possible */
	v->has_s_fmt = -1;

	v->read_active = FALSE;

	printv("Hinted video standard %d, guessed %d\n",
	       scanning, v->dec.scanning);

#ifdef REQUIRE_SELECT
	if (!v->select) {
		asprintf(errstr, _("%s (%s) does not support "
				   "the select() function."),
			 v->p_dev_name, v->vcap.name);
		goto failure;
	}
#endif

        v->services = 0;

        if (services != NULL) {
                assert(*services != 0);

                v->services = v4l_update_services(&v->capture, FALSE, TRUE,
                                                  *services, strict, errstr);
                if (v->services == 0) {
                        goto failure;
                }
                *services = v->services;

                if (!v->dec.scanning && strict >= 1) {
                        printv("Try to guess video standard from vbi bottom field "
                                "boundaries: start=%d, count=%d\n",
                               v->dec.start[1], v->dec.count[1]);

                        if (v->dec.start[1] <= 0 || !v->dec.count[1]) {
                                /*
                                 *  We may have requested single field capture
                                 *  ourselves, but then we had guessed already.
                                 */
#ifdef REQUIRE_VIDEOSTD
                                asprintf(errstr, _("Cannot set or determine current "
						   "videostandard of %s (%s)."),
					 v->p_dev_name, v->vcap.name);
                                goto failure;
#endif
                                printv("Warning: Videostandard not confirmed, "
                                       "will assume PAL/SECAM\n");

                                v->dec.scanning = 625;
                                v->time_per_frame = 1.0 / 25;
                        } else if (v->dec.start[1] < 286) {
                                v->dec.scanning = 525;
                                v->time_per_frame = 1001.0 / 30000;
                        } else {
                                v->dec.scanning = 625;
                                v->time_per_frame = 1.0 / 25;
                        }
                }

                printv("Guessed videostandard %d\n", v->dec.scanning);
        }

	if (!v->has_select)
		printv("Warning: no read select, reading will block\n");

	printv("Successful opened %s (%s)\n",
	       v->p_dev_name, v->vcap.name);

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return &v->capture;

failure:
io_error:
	if (v)
		v4l_delete(&v->capture);

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return NULL;
}

vbi_capture *
vbi_capture_v4l_sidecar_new(const char *dev_name, int video_fd,
			    unsigned int *services, int strict,
			    char **errstr, vbi_bool trace)
{
	return v4l_new(dev_name, video_fd, 0,
		       services, strict, errstr, trace);
}

vbi_capture *
vbi_capture_v4l_new(const char *dev_name, int scanning,
		    unsigned int *services, int strict,
		    char **errstr, vbi_bool trace)
{
	return v4l_new(dev_name, -1, scanning,
		       services, strict, errstr, trace);
}

#else

/**
 * @param dev_name Name of the device to open, usually one of
 *   @c /dev/vbi or @c /dev/vbi0 and up.
 * @param given_fd File handle of an already open video device,
 *   usually one of @c /dev/video or @c /dev/video0 and up.
 *   Must be assorted with the named vbi device, i.e. refer to
 *   the same driver instance and hardware.
 * @param services This must point to a set of @ref VBI_SLICED_
 *   symbols describing the
 *   data services to be decoded. On return the services actually
 *   decodable will be stored here. See vbi_raw_decoder_add()
 *   for details. If you want to capture raw data only, set to
 *   @c VBI_SLICED_VBI_525, @c VBI_SLICED_VBI_625 or both.
 *   If this parameter is @c NULL, no services will be installed.
 *   You can do so later with vbi_capture_update_services(); note the
 *   reset parameter must be set to @c TRUE in this case.
 * @param strict Will be passed to vbi_raw_decoder_add().
 * @param errstr If not @c NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace If @c TRUE print progress messages on stderr.
 *
 * This functions behaves much like vbi_capture_v4l_new, with the sole
 * difference that it uses the given file handle to determine the current
 * video standard if such queries aren't supported by the VBI device.
 * 
 * @return
 * Initialized vbi_capture context, @c NULL on failure.
 */
vbi_capture *
vbi_capture_v4l_sidecar_new(const char *dev_name, int given_fd,
			    unsigned int *services, int strict,
			    char **errstr, vbi_bool trace)
{
	pthread_once (&vbi_init_once, vbi_init);

	if (errstr)
		asprintf(errstr, _("V4L driver interface not compiled."));

	return NULL;
}

/**
 * @param dev_name Name of the device to open, usually one of
 *   @c /dev/vbi or @c /dev/vbi0 and up.
 * @param scanning Can be used to specify the current TV norm for
 *   old drivers which don't support ioctls to query the current
 *   norm.  Value is 625 (PAL/SECAM family) or 525 (NTSC family).
 *   Set to 0 if you don't know the norm.
 * @param services This must point to a set of @ref VBI_SLICED_
 *   symbols describing the
 *   data services to be decoded. On return the services actually
 *   decodable will be stored here. See vbi_raw_decoder_add()
 *   for details. If you want to capture raw data only, set to
 *   @c VBI_SLICED_VBI_525, @c VBI_SLICED_VBI_625 or both.
 *   If this parameter is @c NULL, no services will be installed.
 *   You can do so later with vbi_capture_update_services(); note the
 *   reset parameter must be set to @c TRUE in this case.
 * @param strict Will be passed to vbi_raw_decoder_add().
 * @param errstr If not @c NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace If @c TRUE print progress messages on stderr.
 * 
 * @return
 * Initialized vbi_capture context, @c NULL on failure.
 */
vbi_capture *
vbi_capture_v4l_new(const char *dev_name, int scanning,
		     unsigned int *services, int strict,
		     char **errstr, vbi_bool trace)
{
	dev_name = dev_name;
	scanning = scanning;
	services = services;
	strict = strict;

	pthread_once (&vbi_init_once, vbi_init);

	if (trace)
		fprintf (stderr, "Libzvbi V4L interface rev.\n  %s\n", rcsid);

	if (errstr)
		asprintf (errstr, _("V4L driver interface not compiled."));

	return NULL;
}

#endif /* !ENABLE_V4L */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
