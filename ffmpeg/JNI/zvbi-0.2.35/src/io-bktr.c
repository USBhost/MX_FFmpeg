/*
 *  libzvbi -- FreeBSD/OpenBSD bktr driver interface
 *
 *  Copyright (C) 2002 Michael H. Schimek
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
"$Id: io-bktr.c,v 1.17 2008/02/19 00:35:20 mschimek Exp $";

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "vbi.h"
#include "io.h"

#define printv(format, args...)						\
do {									\
	if (trace) {							\
		fprintf(stderr, format ,##args);			\
		fflush(stderr);						\
	}								\
} while (0)

#ifdef ENABLE_BKTR

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>		/* timeval */
#include <sys/select.h>		/* fd_set */
#include <pthread.h>

typedef struct vbi_capture_bktr {
	vbi_capture		capture;

	int			fd;
	vbi_bool		select;

	vbi_raw_decoder		dec;

	double			time_per_frame;

	vbi_capture_buffer	*raw_buffer;
	int			num_raw_buffers;

	vbi_capture_buffer	sliced_buffer;

} vbi_capture_bktr;

static int
bktr_read			(vbi_capture *		vc,
				 vbi_capture_buffer **	raw,
				 vbi_capture_buffer **	sliced,
				 const struct timeval *	timeout)
{
	vbi_capture_bktr *v = PARENT(vc, vbi_capture_bktr, capture);
	vbi_capture_buffer *my_raw = v->raw_buffer;
	struct timeval tv;
	int r;

	while (v->select) {
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(v->fd, &fds);

		tv = *timeout; /* kernel overwrites this? */

		r = select(v->fd + 1, &fds, NULL, NULL, &tv);

		if (r < 0 && errno == EINTR)
			continue;

		if (r <= 0)
			return r; /* timeout or error */

		break;
	}

	if (!raw)
		raw = &my_raw;
	if (!*raw)
		*raw = v->raw_buffer;
	else
		(*raw)->size = v->raw_buffer[0].size;

	for (;;) {
		/* from zapping/libvbi/v4lx.c */
		pthread_testcancel();

		r = read(v->fd, (*raw)->data, (*raw)->size);

		if (r == -1 && errno == EINTR)
			continue;

		if (r == (*raw)->size)
			break;
		else
			return -1;
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

static vbi_raw_decoder *
bktr_parameters(vbi_capture *vc)
{
	vbi_capture_bktr *v = PARENT(vc, vbi_capture_bktr, capture);

	return &v->dec;
}

static void
bktr_delete(vbi_capture *vc)
{
	vbi_capture_bktr *v = PARENT(vc, vbi_capture_bktr, capture);

	if (v->sliced_buffer.data)
		free(v->sliced_buffer.data);

	for (; v->num_raw_buffers > 0; v->num_raw_buffers--)
		free(v->raw_buffer[v->num_raw_buffers - 1].data);

	vbi_raw_decoder_destroy (&v->dec);

	if (-1 != v->fd)
		device_close (v->capture.sys_log_fp, v->fd);

	free(v);
}

static int
bktr_fd(vbi_capture *vc)
{
	vbi_capture_bktr *v = PARENT(vc, vbi_capture_bktr, capture);

	return v->fd;
}

vbi_capture *
vbi_capture_bktr_new		(const char *		dev_name,
				 int			scanning,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
{
	char *error = NULL;
	char *driver_name = _("BKTR driver");
	vbi_capture_bktr *v;

	pthread_once (&vbi_init_once, vbi_init);

	assert(services && *services != 0);

	if (!errstr)
		errstr = &error;
	*errstr = NULL;

	printv ("Try to open bktr vbi device, "
		"libzvbi interface rev.\n  %s\n", rcsid);

	if (!(v = (vbi_capture_bktr *) calloc(1, sizeof(*v)))) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	vbi_raw_decoder_init (&v->dec);

	v->capture.parameters = bktr_parameters;
	v->capture._delete = bktr_delete;
	v->capture.get_fd = bktr_fd;

	v->fd = device_open (v->capture.sys_log_fp, dev_name, O_RDONLY, 0);
	if (-1 == v->fd) {
		asprintf(errstr, _("Cannot open '%s': %s."),
			 dev_name, strerror(errno));
		goto io_error;
	}

	printv("Opened %s\n", dev_name);

	/*
	 *  XXX
	 *  Can we somehow verify this really is /dev/vbiN (bktr) and not
	 *  /dev/hcfr (halt and catch fire on read) ?
	 */

	v->dec.bytes_per_line = 2048;
	v->dec.interlaced = FALSE;
	v->dec.synchronous = TRUE;

	v->dec.count[0]	= 16;
	v->dec.count[1] = 16;

	switch (scanning) {
	default:
		/* fall through */

	case 625:
		/* Not confirmed */
		v->dec.scanning = 625;
		v->dec.sampling_rate = 35468950;
		v->dec.offset = (int)(10.2e-6 * 35468950);
		v->dec.start[0] = 22 + 1 - v->dec.count[0];
		v->dec.start[1] = 335 + 1 - v->dec.count[1];
		break;

	case 525:
		/* Not confirmed */
		v->dec.scanning = 525;
		v->dec.sampling_rate = 28636363;
		v->dec.offset = (int)(9.2e-6 * 28636363);
		v->dec.start[0] = 10;
		v->dec.start[1] = 273;
		break;
	}

	v->time_per_frame =
		(v->dec.scanning == 625) ? 1.0 / 25 : 1001.0 / 30000;

	v->select = FALSE; /* XXX ? */

	printv("Guessed videostandard %d\n", v->dec.scanning);

	v->dec.sampling_format = VBI_PIXFMT_YUV420;

	if (*services & ~(VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625)) {
		*services = vbi_raw_decoder_add_services (&v->dec, *services, strict);

		if (*services == 0) {
			asprintf(errstr, _("Sorry, %s (%s) cannot "
					   "capture any of "
					   "the requested data services."),
				 dev_name, driver_name);
			goto failure;
		}

		v->sliced_buffer.data =
			malloc((v->dec.count[0] + v->dec.count[1])
			       * sizeof(vbi_sliced));

		if (!v->sliced_buffer.data) {
			asprintf(errstr, _("Virtual memory exhausted."));
			errno = ENOMEM;
			goto failure;
		}
	}

	printv("Will decode services 0x%08x\n", *services);

	/* Read mode */

	if (!v->select)
		printv("Warning: no read select, reading will block\n");

	v->capture.read = bktr_read;

	v->raw_buffer = calloc(1, sizeof(v->raw_buffer[0]));

	if (!v->raw_buffer) {
		asprintf(errstr, _("Virtual memory exhausted."));
		errno = ENOMEM;
		goto failure;
	}

	v->raw_buffer[0].size = (v->dec.count[0] + v->dec.count[1])
		* v->dec.bytes_per_line;

	v->raw_buffer[0].data = malloc(v->raw_buffer[0].size);

	if (!v->raw_buffer[0].data) {
		asprintf(errstr, _("Not enough memory to allocate "
				   "vbi capture buffer (%d KB)."),
			 (v->raw_buffer[0].size + 1023) >> 10);
		goto failure;
	}

	v->num_raw_buffers = 1;

	printv("Capture buffer allocated\n");

	printv("Successful opened %s (%s)\n",
	       dev_name, driver_name);

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return &v->capture;

failure:
io_error:
	if (v)
		bktr_delete(&v->capture);

	if (errstr == &error) {
		free (error);
		error = NULL;
	}

	return NULL;
}

#else

/**
 * @param dev_name Name of the device to open.
 * @param scanning The current video standard. Value is 625
 *   (PAL/SECAM family) or 525 (NTSC family).
 * @param services This must point to a set of @ref VBI_SLICED_
 *   symbols describing the
 *   data services to be decoded. On return the services actually
 *   decodable will be stored here. See vbi_raw_decoder_add()
 *   for details. If you want to capture raw data only, set to
 *   @c VBI_SLICED_VBI_525, @c VBI_SLICED_VBI_625 or both.
 * @param strict Will be passed to vbi_raw_decoder_add().
 * @param errstr If not @c NULL this function stores a pointer to an error
 *   description here. You must free() this string when no longer needed.
 * @param trace If @c TRUE print progress messages on stderr.
 * 
 * @bug You must enable continuous video capturing to read VBI data from
 * the bktr driver, using an RGB video format, and the VBI device must be
 * opened before video capturing starts (METEORCAPTUR).
 * 
 * @return
 * Initialized vbi_capture context, @c NULL on failure.
 */
vbi_capture *
vbi_capture_bktr_new		(const char *		dev_name,
				 int			scanning,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
{
	dev_name = dev_name;
	scanning = scanning;
	services = services;
	strict = strict;
	trace = trace;

	pthread_once (&vbi_init_once, vbi_init);

	printv ("Libzvbi bktr interface rev.\n  %s\n", rcsid);

	if (errstr)
		asprintf (errstr,
			  _("BKTR driver interface not compiled."));

	return NULL;
}

#endif /* !ENABLE_BKTR */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
