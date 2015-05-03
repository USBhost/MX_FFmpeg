/*
 *  libzvbi test
 *
 *  Copyright (C) 2005 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

/* $Id: sliced.c,v 1.19 2009/12/14 23:43:46 mschimek Exp $ */

/* For libzvbi version 0.2.x / 0.3.x. */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* Misc. helper functions. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <locale.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/dvb_mux.h"
#include "src/dvb_demux.h"
#include "src/io.h"
#include "src/io-sim.h"
#include "src/raw_decoder.h"
#include "src/vbi.h"
#include "sliced.h"

#if 2 == VBI_VERSION_MINOR
#  include "src/proxy-msg.h"
#  include "src/proxy-client.h"
#  define sp_sample_format sampling_format
#  define sp_samples_per_line bytes_per_line
#  define VBI_PIXFMT_Y8 VBI_PIXFMT_YUV420
#  define vbi_pixfmt_name(x) "Y8"
#  define vbi_pixfmt_bytes_per_pixel(x) 1
#elif 3 == VBI_VERSION_MINOR
#  define sp_sample_format sample_format
#  define sp_samples_per_line samples_per_line
#else
#  error VBI_VERSION_MINOR == ?
#endif

#undef _
#define _(x) x /* later */

typedef vbi_bool
read_loop_fn			(struct stream *	st);

typedef vbi_bool
write_fn			(struct stream *	st,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time);

struct stream {
	uint8_t			buffer[4096];

	uint8_t			b64_buffer[4096];

	vbi_sliced		sliced[64];
	vbi_sliced		sliced2[64];

	uint8_t *		raw;

	const uint8_t *		bp;
	const uint8_t *		end;

	stream_callback_fn *	callback;

	read_loop_fn *		loop;
	write_fn *		write_func;

	vbi_dvb_mux *		mx;
	vbi_dvb_demux *	dx;
#if 2 == VBI_VERSION_MINOR
        vbi_proxy_client *	proxy;
#endif
	vbi_capture *		cap;
	vbi_raw_decoder *	rd;
	vbi_sampling_par	sp;

	vbi_bool		raw_valid;
	vbi_bool		decode_raw;
	vbi_bool		debug;

	unsigned int		sliced2_lines;

	double			sample_time;
	int64_t			stream_time;

	unsigned int		interfaces;
	unsigned int		system;
	vbi_bool		read_not_pull;

	int			fd;
	vbi_bool		close_fd;
};

#ifndef HAVE_PROGRAM_INVOCATION_NAME
char *				program_invocation_name;
char *				program_invocation_short_name;
#endif

const char *			option_dev_name;
unsigned int			option_dvb_pid;
vbi_bool			option_quiet;
unsigned int			option_log_mask;

static vbi_bool		have_dev_name;

void
vprint_error			(const char *		template,
				 va_list		ap)
{
	if (option_quiet)
		return;

	fprintf (stderr, "%s: ", program_invocation_short_name);

	vfprintf (stderr, template, ap);

	fputc ('\n', stderr);
}

void
error_msg			(const char *		template,
				 ...)
{
	va_list ap;

	va_start (ap, template);
	vprint_error (template, ap);
	va_end (ap);
}

void
error_exit			(const char *		template,
				 ...)
{
	va_list ap;

	va_start (ap, template);
	vprint_error (template, ap);
	va_end (ap);

	exit (EXIT_FAILURE);
}

void
write_error_exit		(const char *		msg)
{
	if (NULL == msg)
		msg = strerror (errno);

	error_exit (_("Write error: %s."), msg);
}

void
read_error_exit			(const char *		msg)
{
	if (NULL == msg)
		msg = strerror (errno);

	error_exit (_("Read error: %s."), msg);
}

void
no_mem_exit			(void)
{
	error_exit (_("Out of memory."));
}

static void
premature_exit			(void)
{
	error_exit (_("Premature end of input file."));
}

static void
bad_format_exit			(void)
{
	error_exit (_("Invalid data in input file."));
}

void
stream_delete			(struct stream *	st)
{
	if (NULL == st)
		return;

	if (st->close_fd) {
		if (-1 == close (st->fd)) {
			if (NULL != st->write_func)
				write_error_exit (/* msg: errno */ NULL);
		}
	}

	vbi_capture_delete (st->cap);
#if 3 == VBI_VERSION_MINOR
	vbi_raw_decoder_delete (st->rd);
#endif

	free (st->raw);

	CLEAR (*st);

	free (st);
}

vbi_bool
stream_loop			(struct stream *	st)
{
	return st->loop (st);
}

static vbi_bool
pes_ts_cb			(vbi_dvb_mux *		mx,
				 void *			user_data,
				 const uint8_t *	packet,
				 unsigned int		packet_size)
{
	struct stream *st = (struct stream *) user_data;
	ssize_t actual;

	mx = mx; /* unused */

	assert (packet_size < 66000);

	actual = write (st->fd, packet, packet_size);
	if (actual != (ssize_t) packet_size)
		write_error_exit (/* msg: errno */ NULL);

	return TRUE;
}

static vbi_bool
write_func_pes_ts		(struct stream *	st,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	vbi_bool success;

	assert (NULL != sliced);
	assert (n_lines <= 32);
	assert (stream_time >= 0);

	raw = raw; /* unused */
	sp = sp;
	sample_time = sample_time;

	success = vbi_dvb_mux_feed (st->mx,
				     sliced, n_lines,
				     (VBI_SLICED_CAPTION_625 |
				      VBI_SLICED_TELETEXT_B_625 |
				      VBI_SLICED_VPS |
				      VBI_SLICED_WSS_625),
				     /* raw */ NULL,
				     /* sp */ NULL,
				     /* pts */ stream_time);
	if (!success) {
		/* Probably. */
		error_exit (_("Maximum PES packet size %u bytes "
			      "is too small for this input stream."),
			    vbi_dvb_mux_get_max_pes_packet_size (st->mx));
	}

	return TRUE;
}

struct service {
	const char *		name;
	vbi_service_set	id;
	unsigned int		n_bytes;
};

static const struct service
service_map [] = {
	{ "TELETEXT_B",		VBI_SLICED_TELETEXT_B,			42 },
	{ "CAPTION_625",	VBI_SLICED_CAPTION_625,		2 },
	{ "VPS",		VBI_SLICED_VPS | VBI_SLICED_VPS_F2,	13 },
	{ "WSS_625",		VBI_SLICED_WSS_625,			2 },
	{ "WSS_CPR1204",	VBI_SLICED_WSS_CPR1204,		3 },
	{ NULL, 0, 0 },
	{ NULL, 0, 0 },
	{ "CAPTION_525",	VBI_SLICED_CAPTION_525,		2 },
};

static void
st_printf			(struct stream *	st,
				 const char *		templ,
				 ...)
{
	va_list ap;
	int n;

	va_start (ap, templ);
	n = vsnprintf ((char *) st->buffer, sizeof (st->buffer), templ, ap);
	va_end (ap);

	if (n < 1 || n >= (int) sizeof (st->buffer))
		error_exit (_("Buffer overflow."));

	if (n != write (st->fd, st->buffer, n))
		write_error_exit (/* msg: errno */ NULL);
}

static const uint8_t
base64 [] = ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	     "abcdefghijklmnopqrstuvwxyz"
	     "0123456789+/");

static void
encode_base64			(uint8_t *		out,
				 const uint8_t *	in,
				 unsigned int		n_bytes)
{
	unsigned int block;

	for (; n_bytes >= 3; n_bytes -= 3) {
		block = in[0] * 65536 + in[1] * 256 + in[2];
		in += 3;
		out[0] = base64[block >> 18];
		out[1] = base64[(block >> 12) & 0x3F];
		out[2] = base64[(block >> 6) & 0x3F];
		out[3] = base64[block & 0x3F];
		out += 4;
	}

	switch (n_bytes) {
	case 2:
		block = in[0] * 256 + in[1];
		out[0] = base64[block >> 10];
		out[1] = base64[(block >> 4) & 0x3F];
		out[2] = base64[(block << 2) & 0x3F];
		out[3] = '=';
		out += 4;
		break;

	case 1:
		block = in[0];
		out[0] = base64[block >> 2];
		out[1] = base64[(block << 4) & 0x3F];
		out[2] = '=';
		out[3] = '=';
		out += 4;
		break;
	}

	*out = 0;
}

static void
write_xml_sliced		(struct stream *	st,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines)
{
	const vbi_sliced *sliced_end;

	sliced_end = sliced + n_lines;

	assert (N_ELEMENTS (st->b64_buffer)
		>= (sizeof (sliced->data) + 2) * 4 / 3 + 1);

	for (; sliced < sliced_end; ++sliced) {
		unsigned int i;

		if (VBI_SLICED_VBI_525 == sliced->id
		    || VBI_SLICED_VBI_625 == sliced->id)
			continue;

		for (i = 0; i < N_ELEMENTS (service_map); ++i) {
			if (sliced->id & service_map[i].id)
				break;
		}

		if (i >= N_ELEMENTS (service_map))
			error_exit (_("Unknown data service."));

		assert (service_map[i].n_bytes
			<= sizeof (sliced->data));

		encode_base64 (st->b64_buffer,
			       sliced->data,
			       service_map[i].n_bytes);

		if (0 == sliced->line) {
			st_printf (st,
				   "<vbi-sliced service=\"%s\""
				   ">%s</vbi-sliced>\n",
				   service_map[i].name,
				   st->b64_buffer);
		} else {
			st_printf (st,
				   "<vbi-sliced service=\"%s\" "
				   "line=\"%u\">%s</vbi-sliced>\n",
				   service_map[i].name,
				   sliced->line,
				   st->b64_buffer);
		}
	}
}

static void
write_xml_raw			(struct stream *	st,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp)		 
{
	const char *format;
	unsigned int n_samples;
	unsigned int n_rows;
	unsigned int row;

	assert ((N_ELEMENTS (st->b64_buffer) - 1) * 3 / 4 - 2
		>= (unsigned int) sp->sp_samples_per_line);

	format = vbi_pixfmt_name (sp->sp_sample_format);

	if (NULL == format)
		error_exit (_("Unknown raw VBI format."));

	n_samples = sp->sp_samples_per_line
		* vbi_pixfmt_bytes_per_pixel (sp->sp_sample_format);

	n_rows = sp->count[0] + sp->count[1];

	if (sp->interlaced)
		assert (sp->count[0] == sp->count[1]);

	for (row = 0; row < n_rows; ++row) {
		unsigned int line;

		if (sp->interlaced) {
			line = sp->start[row & 1];
			if (line > 0)
				line += row >> 1;
		} else if (row < (unsigned int) sp->count[0]) {
			line = sp->start[0];
			if (line > 0)
				line += row;
		} else {
			line = sp->start[1];
			if (line > 0)
				line += row - sp->count[0];
		}

		encode_base64 (st->b64_buffer, raw, n_samples);

		if (0 == line) {
			st_printf (st,
				   "<vbi-raw format=\"%s\" "
				   "sampling-rate=\"%u\" "
				   "offset=\"%u\">%s</vbi-raw>\n",
				   format,
				   sp->sampling_rate,
				   sp->offset,
				   st->b64_buffer);
		} else {
			st_printf (st,
				   "<vbi-raw format=\"%s\" "
				   "sampling-rate=\"%u\" "
				   "offset=\"%u\" "
				   "line=\"%u\">%s</vbi-raw>\n",
				   format,
				   sp->sampling_rate,
				   sp->offset,
				   line,
				   st->b64_buffer);
		}

		raw += sp->bytes_per_line;
	}
}

static vbi_bool
write_func_xml			(struct stream *	st,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	struct timeval tv;
	double intpart;

	if (NULL == sliced && NULL == raw)
		return TRUE;

	if (NULL != sliced)
		assert (n_lines <= st->system);

	if (NULL != raw)
		assert (NULL != sp);

	assert (sample_time >= 0);
	assert (stream_time >= 0);

	tv.tv_usec = (int)(1e6 * modf (sample_time, &intpart));
	tv.tv_sec = (int) intpart;

	st_printf (st,
		   "<frame video-standard=\"%s\" "
		   "sample-time=\"%" PRId64 ".%06u\" "
		   "stream-time=\"%" PRId64 "\">\n",
		   (525 == st->system) ? "525_60" : "625_50",
		   (int64_t) tv.tv_sec,
		   (unsigned int) tv.tv_usec,
		   stream_time);

	if (NULL != sliced)
		write_xml_sliced (st, sliced, n_lines);

	if (NULL != raw)
		write_xml_raw (st, raw, sp);

	st_printf (st, "</frame>\n");

	return TRUE;
}

static vbi_bool
write_func_old_sliced		(struct stream *	st,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	if (NULL == sliced && NULL == raw)
		return TRUE;

	if (NULL != sliced)
		assert (n_lines <= 254);
	else
		n_lines = 0;

	if (NULL != raw)
		assert (NULL != sp);

	stream_time = stream_time;

	st_printf (st,
		   "%f\n%c",
		   sample_time - st->sample_time,
		   n_lines + (NULL != raw));

	while (n_lines > 0) {
		unsigned int i;

		for (i = 0; i < N_ELEMENTS (service_map); ++i) {
			if (sliced->id & service_map[i].id) {
				int n;

				st_printf (st, "%c%c%c",
					   /* service number */ i,
					   /* line number low/high */
					   sliced->line & 0xFF,
					   sliced->line >> 8);

				n = service_map[i].n_bytes;
				assert (n > 0
					&& n <= (int) sizeof (sliced->data));

				if (n != write (st->fd, sliced->data, n))
					write_error_exit (NULL);
			}
		}

		++sliced;
		--n_lines;
	}

	if (NULL != raw) {
		uint8_t *p = st->b64_buffer;
		int n;

#define w8(n) *p++ = n
#define w16(n) w8 ((n) & 0xFF); w8 (((n) >> 8) & 0xFF)
#define w32(n) w16 ((n) & 0xFFFF); w16 (((n) >> 16) & 0xFFFF)

		w8 (255); /* service number */
		w16 (0); /* line number */
		w16 (st->system);
		w32 (sp->sampling_rate);
		w16 (sp->sp_samples_per_line);
		w16 (sp->bytes_per_line);
		w16 (sp->offset);
		w16 (sp->start[0]);
		w16 (sp->start[1]);
		w16 (sp->count[0]);
		w16 (sp->count[1]);
		w8 (sp->interlaced);
		w8 (sp->synchronous);
#undef w8
#undef w16
#undef w32
		n = p - st->b64_buffer;
		assert (n > 0 && n <= (int) sizeof (st->b64_buffer));

		if (n != write (st->fd, st->b64_buffer, n))
			write_error_exit (NULL);

		n = (sp->count[0] + sp->count[1]) * sp->bytes_per_line;
		assert (n > 0 && n <= 625 * 4096);

		if (n != write (st->fd, raw, n))
			write_error_exit (NULL);
	}

	st->sample_time = sample_time;

	return TRUE;
}

vbi_bool
write_stream_sliced		(struct stream *	st,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time)
{
	return st->write_func (st, sliced, n_lines,
			       raw, sp,
			       sample_time, stream_time);
}

void
write_stream_set_data_identifier
				(struct stream *	st,
				 unsigned int		data_identifier)
{
	assert (NULL != st->mx);

	if (!vbi_dvb_mux_set_data_identifier (st->mx, data_identifier)) {
		error_exit (_("Invalid data identifier 0x%x."),
			    data_identifier);
	}
}

void
write_stream_set_pes_packet_size
				(struct stream *	st,
				 unsigned int		min,
				 unsigned int		max)
{
	assert (NULL != st->mx);

	if (!vbi_dvb_mux_set_pes_packet_size (st->mx, min, max))
		no_mem_exit ();
}

struct stream *
write_stream_new		(const char *		file_name,
				 enum file_format	file_format,
				 unsigned int		ts_pid,
				 unsigned int		system)
{
	struct stream *st;

	assert (525 == system || 625 == system);

	st = calloc (1, sizeof (*st));
	if (NULL == st)
		no_mem_exit ();

	if (NULL == file_name
	    || 0 == strcmp (file_name, "-")) {
		st->fd = STDOUT_FILENO;

		if (isatty (STDOUT_FILENO)) {
			error_exit (_("Output of this program is binary "
				      "data. You should pipe it to another "
				      "tool or redirect to a file.\n"));
		}
	} else {
		st->fd = open (file_name,
			       O_WRONLY | O_CREAT | O_EXCL,
			       (S_IRUSR | S_IWUSR |
				S_IRGRP | S_IWGRP |
				S_IROTH | S_IWOTH));
		if (-1 == st->fd) {
			error_exit (_("Cannot open '%s' for writing: %s."),
				    file_name, strerror (errno));
		}

		st->close_fd = TRUE;
	}

	switch (file_format) {
	case FILE_FORMAT_SLICED:
		st->write_func = write_func_old_sliced;
		break;

	case FILE_FORMAT_XML:
		st->write_func = write_func_xml;
		break;

	case FILE_FORMAT_DVB_PES:
		st->write_func = write_func_pes_ts;

		st->mx = vbi_dvb_pes_mux_new (pes_ts_cb,
					       /* user_data */ st);
		if (NULL == st->mx)
			no_mem_exit ();

		break;

	case FILE_FORMAT_DVB_TS:
		st->write_func = write_func_pes_ts;

		st->mx = vbi_dvb_ts_mux_new (ts_pid,
					      pes_ts_cb,
					      /* user_data */ st);
		if (NULL == st->mx)
			no_mem_exit ();

		break;

	default:
		error_exit (_("Unknown output file format."));
		break;
	}

	st->sample_time = 0.0;
	st->stream_time = 0;

	st->system = system;

	return st;
}

static vbi_bool
read_more			(struct stream *	st)
{
	unsigned int retry;
	uint8_t *s;
	uint8_t *e;

	s = (uint8_t *) st->end;
	e = st->buffer + sizeof (st->buffer);

	if (s >= e)
		s = st->buffer;

	retry = 100;

        do {
                ssize_t actual;
		int saved_errno;

                actual = read (st->fd, s, e - s);
                if (0 == actual)
			return FALSE; /* EOF */

		if (actual > 0) {
			st->bp = s;
			st->end = s + actual;
			return TRUE;
		}

		saved_errno = errno;

		if (EINTR != saved_errno) {
			read_error_exit (/* msg: errno */ NULL);
		}
        } while (--retry > 0);

	read_error_exit (/* msg: errno */ NULL);

	return FALSE;
}

static vbi_bool
read_loop_pes_ts		(struct stream *	st)
{
	for (;;) {
		double sample_time;
		int64_t pts;
		unsigned int left;
		unsigned int n_lines;

		if (st->bp >= st->end) {
			if (!read_more (st))
				break; /* EOF */
		}

		left = st->end - st->bp;

		n_lines = vbi_dvb_demux_cor (st->dx,
					     st->sliced,
					     N_ELEMENTS (st->sliced),
					     &pts,
					     &st->bp,
					     &left);

		if (0 == n_lines)
			continue;

		if (pts < 0) {
			/* XXX WTF? */
			continue;
		}

		sample_time = pts * (1 / 90000.0);

		if (!st->callback (st->sliced, n_lines,
				   /* raw */ NULL,
				   /* sp */ NULL,
				   sample_time, pts))
			return FALSE;
	}

	return TRUE;
}

static vbi_bool
next_byte			(struct stream *	st,
				 int *			c)
{
	do {
		if (st->bp < st->end) {
			*c = *st->bp++;
			return TRUE;
		}
	} while (read_more (st));

	return FALSE; /* EOF */
}

static void
next_block			(struct stream *	st,
				 uint8_t *		buffer,
				 unsigned int		buffer_size)
{
	do {
		unsigned int available;

		available = st->end - st->bp;

		if (buffer_size <= available) {
			memcpy (buffer, st->bp, buffer_size);
			st->bp += buffer_size;
			return;
		}

		memcpy (buffer, st->bp, available);

		st->bp += available;

		buffer += available;
		buffer_size -= available;

	} while (read_more (st));

	premature_exit ();
}

static uint8_t *
next_raw_data			(struct stream *	st,
				 vbi_sampling_par *	sp)
{
	uint8_t sp_buffer[32];
	unsigned int system;
	unsigned int size;
	uint8_t *p;

	next_block (st, sp_buffer, 22);

	p = sp_buffer;

	CLEAR (*sp);

#define r8(v) v = *p++
#define r16(v) v = p[0] | (p[1] << 8); p += 2
#define r32(v) v = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); p += 4
	r16 (system);
	sp->sp_sample_format = VBI_PIXFMT_Y8;
	r32 (sp->sampling_rate);
	r16 (sp->sp_samples_per_line);
	r16 (sp->bytes_per_line);
	r16 (sp->offset);
	r16 (sp->start[0]);
	r16 (sp->start[1]);
	r16 (sp->count[0]);
	r16 (sp->count[1]);
	r8 (sp->interlaced);
	r8 (sp->synchronous);
#undef r8
#undef r16
#undef r32

	assert (22 == p - sp_buffer);

#if 2 == VBI_VERSION_MINOR

	switch (system) {
	case 525:
	case 625:
		sp->scanning = system;

	default:
		bad_format_exit ();
		break;
	}

#elif 3 == VBI_VERSION_MINOR

	switch (system) {
	case 525:
		sp->videostd_set = VBI_VIDEOSTD_SET_525_60;
		break;

	case 625:
		sp->videostd_set = VBI_VIDEOSTD_SET_625_50;
		break;

	default:
		bad_format_exit ();
		break;
	}

#else
#  error VBI_VERSION_MINOR == ?
#endif

	size = (sp->count[0] + sp->count[1]) * sp->bytes_per_line;

	p = malloc (size);
	if (NULL == p)
		no_mem_exit ();

	next_block (st, p, size);

#if 3 == VBI_VERSION_MINOR

	if (st->debug) {
		if (NULL == st->rd) {
			vbi_bool success;

			st->rd = vbi_raw_decoder_new (sp);
			if (NULL == st->rd)
				return p;
			vbi_raw_decoder_add_services (st->rd, -1, 1);
			success = vbi_raw_decoder_debug (st->rd, TRUE);
			assert (success);
		}

		st->sliced2_lines =
			vbi_raw_decoder_decode (st->rd,
						 st->sliced2,
						 N_ELEMENTS (st->sliced2),
						 p);
	}

#endif /* 3 == VBI_VERSION_MINOR */

	return p;
}

static vbi_bool
next_time_delta			(struct stream *	st,
				 double *		dt)
{
	char buffer[32];
	unsigned int i;

	for (i = 0; i < N_ELEMENTS (buffer); ++i) {
		int c;

		if (!next_byte (st, &c)) {
			if (i > 0)
				premature_exit ();
			else
				return FALSE;
		}

		if ('\n' == c) {
			if (0 == i) {
				bad_format_exit ();
			} else {
				buffer[i] = 0;
				*dt = strtod (buffer, NULL);
				return TRUE;
			}
		}

		if ('-' != c && '.' != c && !isdigit (c))
			bad_format_exit ();

		buffer[i] = c;
	}

	return FALSE;
}

static vbi_bool
read_loop_old_sliced		(struct stream *	st)
{
	for (;;) {
		vbi_sliced *s;
		uint8_t *raw;
		vbi_sampling_par sp;
		double dt;
		vbi_bool success;
		int n_lines;
		int count;

		if (!next_time_delta (st, &dt))
			break; /* EOF */

		/* Time in seconds since last frame. */
		if (dt < 0.0)
			dt = -dt;

		st->sample_time += dt;

		if (!next_byte (st, &n_lines))
			bad_format_exit ();

		if ((unsigned int) n_lines > N_ELEMENTS (st->sliced))
			bad_format_exit ();

		s = st->sliced;
		raw = NULL;
		st->raw_valid = FALSE;

		for (count = n_lines; count > 0; --count) {
			int index;
			int line;

			if (!next_byte (st, &index))
				premature_exit ();

			if (!next_byte (st, &line))
				premature_exit ();
			s->line = line;

			if (!next_byte (st, &line))
				premature_exit ();
			s->line += (line & 15) * 256;

			switch (index) {
			case 0:
				s->id = VBI_SLICED_TELETEXT_B;
				next_block (st, s->data, 42);
				break;

			case 1:
				s->id = VBI_SLICED_CAPTION_625;
				next_block (st, s->data, 2);
				break; 

			case 2:
				s->id = VBI_SLICED_VPS;
				next_block (st, s->data, 13);
				break;

			case 3:
				s->id = VBI_SLICED_WSS_625;
				next_block (st, s->data, 2);
				break;

			case 4:
				s->id = VBI_SLICED_WSS_CPR1204;
				next_block (st, s->data, 3);
				break;

			case 7:
				s->id = VBI_SLICED_CAPTION_525;
				next_block (st, s->data, 2);
				break;

			case 255:
				raw = next_raw_data (st, &sp);
				st->raw_valid = TRUE;
				break;

			default:
				bad_format_exit ();
				break;
			}

			++s;
		}

		st->stream_time = st->sample_time * 90000;

		if (st->raw_valid && st->decode_raw) {
			success = st->callback (st->sliced2,
						st->sliced2_lines,
						raw, &sp,
						st->sample_time,
						st->stream_time);
		} else {
			success = st->callback (st->sliced, n_lines,
						raw, &sp,
						st->sample_time,
						st->stream_time);
		}

		free (raw);
		raw = NULL;

		if (!success)
			return FALSE;
	}

	return TRUE;
}

static vbi_bool
look_ahead			(struct stream *	st,
				 unsigned int		n_bytes)
{
	assert (n_bytes <= sizeof (st->buffer));

	do {
		unsigned int available;
		const uint8_t *end;

		available = st->end - st->bp;
		if (available >= n_bytes)
			return TRUE;

		end = st->buffer + sizeof (st->buffer);

		if (n_bytes > (unsigned int)(end - st->bp)) {
			memmove (st->buffer, st->bp, available);

			st->bp = st->buffer;
			st->end = st->buffer + available;
		}
	} while (read_more (st));

	return FALSE; /* EOF */
}

static vbi_bool
is_old_sliced_format		(const uint8_t		s[8])
{
	unsigned int i;

	if ('0' != s[0] || '.' != s[1])
		return FALSE;

	for (i = 2; i < 8; ++i) {
		if (!isdigit (s[i]))
			return FALSE;
	}

	return TRUE;
}

static vbi_bool
is_xml_format			(const uint8_t		s[6])
{
	unsigned int i;

	if ('<' != s[0])
		return FALSE;

	for (i = 1; i < 6; ++i) {
		if (!isalpha (s[i]))
			return FALSE;
	}

	return TRUE;
}

static vbi_bool
is_pes_format			(const uint8_t		s[4])
{
	return (0x00 == s[0] &&
		0x00 == s[1] &&
		0x01 == s[2] &&
		0xBD == s[3]);
}

static vbi_bool
is_ts_format			(const uint8_t		s[1])
{
	return (0x47 == s[0]);
}

static enum file_format
detect_file_format		(struct stream *	st)
{
	if (!look_ahead (st, 8))
		return 0; /* unknown format */

	if (is_old_sliced_format (st->bp))
		return FILE_FORMAT_SLICED;

	if (is_xml_format (st->buffer))
		return FILE_FORMAT_XML;

	/* Can/shall we guess a PID? */
	if (0) {
		/* Somewhat unreliable and works only if the
		   packets are aligned. */
		if (is_ts_format (st->buffer))
			return FILE_FORMAT_DVB_TS;
	}

	/* Works only if the packets are aligned. */
	if (is_pes_format (st->buffer))
		return FILE_FORMAT_DVB_PES;

	return 0; /* unknown format */
}

struct stream *
read_stream_new			(const char *		file_name,
				 enum file_format	file_format,
				 unsigned int		ts_pid,
				 stream_callback_fn *	callback)
{
	struct stream *st;

	st = calloc (1, sizeof (*st));
	if (NULL == st)
		no_mem_exit ();

	if (NULL == file_name
	    || 0 == strcmp (file_name, "-")) {
		st->fd = STDIN_FILENO;

		if (isatty (STDIN_FILENO))
			error_exit (_("No VBI data on standard input."));
	} else {
		st->fd = open (file_name, O_RDONLY, 0);
		if (-1 == st->fd) {
			error_exit (_("Cannot open '%s' for reading: %s."),
				    file_name, strerror (errno));
		}

		st->close_fd = TRUE;
	}

	if (0 == file_format)
		file_format = detect_file_format (st);

	switch (file_format) {
	case FILE_FORMAT_SLICED:
		st->loop = read_loop_old_sliced;
		break;

	case FILE_FORMAT_XML:
		st->loop = NULL;
		error_exit ("XML read function "
			    "not implemented yet.");
		break;

	case FILE_FORMAT_DVB_PES:
		st->loop = read_loop_pes_ts;

		st->dx = vbi_dvb_pes_demux_new (/* callback */ NULL,
						/* user_data */ NULL);
		if (NULL == st->dx)
			no_mem_exit ();

		break;

	case FILE_FORMAT_DVB_TS:
		st->loop = read_loop_pes_ts;

		st->dx = _vbi_dvb_ts_demux_new (/* callback */ NULL,
						 /* user_data */ NULL,
						 ts_pid);
		if (NULL == st->dx)
			no_mem_exit ();

		break;

	default:
		error_exit (_("Unknown input file format."));
		break;
	}

	st->callback		= callback;

	st->sample_time		= 0.0;
	st->stream_time		= 0;

	st->bp			= st->buffer;
	st->end			= st->buffer;

	return st;
}

static vbi_bool
capture_loop			(struct stream *	st)
{
	struct timeval timeout;

	timeout.tv_sec = 2;
	timeout.tv_usec = 0;

	for (;;) {
		vbi_capture_buffer *raw_buffer;
		vbi_capture_buffer *sliced_buffer;
		uint8_t	*raw;
		vbi_sliced *sliced;
		int n_lines;
		double sample_time;
		int64_t stream_time;
		int r;

		if (st->read_not_pull) {
			r = vbi_capture_read (st->cap,
					       st->raw,
					       st->sliced,
					       &n_lines,
					       &sample_time,
					       &timeout);
		} else {
			r = vbi_capture_pull (st->cap,
					       &raw_buffer,
					       &sliced_buffer,
					       &timeout);
		}

		switch (r) {
		case -1:
			read_error_exit (/* msg: errno */ NULL);

		case 0:
			error_exit (_("Read timeout.")); 

		case 1:
			break;

		default:
			assert (0);
		}

		if (st->read_not_pull) {
			raw = st->raw;
			sliced = st->sliced;
		} else {
			if (NULL != raw_buffer)
				raw = raw_buffer->data;
			else
				raw = NULL;

			sliced = sliced_buffer->data;
			n_lines = sliced_buffer->size / sizeof (vbi_sliced);

			sample_time = sliced_buffer->timestamp;
		}

		if (st->interfaces & INTERFACE_DVB) {
			stream_time = vbi_capture_dvb_last_pts (st->cap);
		} else {
			stream_time = sample_time * 90000;
		}

		if (!st->callback (sliced, n_lines, raw, &st->sp,
				   sample_time, stream_time))
			return FALSE;
	}

	return TRUE;
}

void
capture_stream_sim_set_flags	(struct stream *	st,
				 unsigned int		flags)
{
	if (NULL == st->cap)
		return;

	if (st->interfaces & INTERFACE_SIM) {
		_vbi_capture_sim_set_flags (st->cap, flags);

		if (flags & _VBI_RAW_NOISE_2) {
			vbi_capture_sim_add_noise (st->cap,
						    /* min_freq */ 0,
						    /* max_freq */ 5000000,
						    /* amplitude */ 25);
		} else {
			vbi_capture_sim_add_noise (st->cap, 0, 0, 0);
		}
	}
}

void
capture_stream_sim_decode_raw	(struct stream *	st,
				 vbi_bool		enable)
{
	st->decode_raw = !!enable;

	if (NULL == st->cap)
		return;

	if (st->interfaces & INTERFACE_SIM)
		vbi_capture_sim_decode_raw (st->cap, enable);
}

vbi_bool
capture_stream_sim_load_caption	(struct stream *	st,
				 const char *		stream,
				 vbi_bool		append)
{
	if (NULL == st->cap)
		return FALSE;

	if (0 == (st->interfaces & INTERFACE_SIM))
		return FALSE;

	return vbi_capture_sim_load_caption (st->cap, stream, append);
}

vbi_bool
capture_stream_get_point	(struct stream *	st,
				 vbi_bit_slicer_point *point,
				 unsigned int		row,
				 unsigned int		nth_bit)
{
#if 2 == VBI_VERSION_MINOR
	st = st; /* unused */
	point = point;
	row = row;
	nth_bit = nth_bit;

	return FALSE;
#elif 3 == VBI_VERSION_MINOR
	if (NULL != st->cap) {
		return vbi_capture_sampling_point
			(st->cap, point, row, nth_bit);
	} else if (NULL != st->rd) {
		if (!st->raw_valid)
			return FALSE;
		return vbi_raw_decoder_sampling_point
			(st->rd, point, row, nth_bit);
	} else {
		return FALSE;
	}
#else
#  error VBI_VERSION_MINOR == ?
#endif
}

vbi_bool
capture_stream_debug		(struct stream *	st,
				 vbi_bool		enable)
{
#if 2 == VBI_VERSION_MINOR
	st = st; /* unused */
	enable = enable;

	return FALSE;
#elif 3 == VBI_VERSION_MINOR
	st->debug = TRUE;

	if (NULL != st->cap) {
		return vbi_capture_debug (st->cap, enable);
	} else if (NULL != st->rd) {
		return vbi_raw_decoder_debug (st->rd, enable);
	} else {
		return FALSE;
	}
#else
#  error VBI_VERSION_MINOR == ?
#endif
}

void
capture_stream_get_sampling_par	(struct stream *	st,
				 vbi_sampling_par *	sp)
{
	assert (NULL != sp);

	*sp = st->sp;
}

static void
capture_error_msg		(const char *		interface_name,
				 const char *		errstr)
{
	error_msg (_("Cannot capture VBI data "
		     "with %s interface: %s."),
		   interface_name, errstr);
}

struct stream *
capture_stream_new		(unsigned int		interfaces,
				 const char *		dev_name,
				 unsigned int		system,
				 vbi_service_set	services,
				 unsigned int		n_buffers,
				 unsigned int		ts_pid,
				 vbi_bool		sim_interlaced,
				 vbi_bool		sim_synchronous,
				 vbi_bool		capture_raw_data,
				 vbi_bool		read_not_pull,
				 unsigned int		strict,
				 stream_callback_fn *	callback)
{
	struct stream *st;
	vbi_bool trace;

	assert (0 != interfaces);
	assert (525 == system || 625 == system);
	assert (0 != services);
	assert (NULL != callback);

	st = calloc (1, sizeof (*st));
	if (NULL == st)
		no_mem_exit ();

	trace = (0 != (option_log_mask & VBI_LOG_INFO));

	if (interfaces & INTERFACE_SIM) {
		st->cap = vbi_capture_sim_new (system,
						&services,
						sim_interlaced,
						sim_synchronous);
		if (NULL == st->cap)
			no_mem_exit ();

		interfaces = INTERFACE_SIM;
	}

	if (NULL == dev_name
	    && (interfaces & (INTERFACE_DVB |
			      INTERFACE_V4L2 |
			      INTERFACE_V4L |
			      INTERFACE_BKTR))) {
		error_exit (_("No device name specified."));
	}

	if (interfaces & INTERFACE_DVB) {
		char *errstr;

		assert (0 == (interfaces & (INTERFACE_V4L2 |
					    INTERFACE_V4L |
					    INTERFACE_BKTR |
					    INTERFACE_PROXY)));

#if 2 == VBI_VERSION_MINOR /* not ported to 0.3 yet */
		assert (NULL != dev_name);

		if (capture_raw_data) {
			error_exit (_("Cannot capture raw VBI data "
				      "from a DVB device."));
		}

		st->cap = vbi_capture_dvb_new2 (dev_name,
						ts_pid,
						&errstr,
						trace);
		if (NULL == st->cap) {
			interfaces &= ~INTERFACE_DVB;
			capture_error_msg ("DVB", errstr);
		} else {
			interfaces = INTERFACE_DVB;
		}
#elif 3 == VBI_VERSION_MINOR
		ts_pid = ts_pid;
		capture_raw_data = capture_raw_data;
		errstr = NULL;

		error_exit ("Sorry, no DVB support yet.");
#else
#  error VBI_VERSION_MINOR == ?
#endif
	}

	if (interfaces & INTERFACE_PROXY) {
#if 2 == VBI_VERSION_MINOR
		char *errstr = NULL;

		st->proxy = vbi_proxy_client_create(dev_name,
						"test/capture",
						0, /* no flags */
						&errstr,
						trace);
		if (NULL != st->proxy) {
			st->cap = vbi_capture_proxy_new(st->proxy,
							 n_buffers,
							 system,
							 &services,
							 strict,
							 &errstr );
			if (NULL == st->cap) {
				interfaces &= ~INTERFACE_PROXY;
				capture_error_msg ("PROXY", errstr);
				free (errstr);
			} else {
				interfaces = INTERFACE_PROXY;
			}
		} else {
			capture_error_msg ("PROXY", errstr);
			free (errstr);
		}
#else
		error_exit ("Sorry, the proxy interface is not "
			    "available yet.\n");
#endif
        }

	if (interfaces & INTERFACE_V4L2) {
		char *errstr;

		st->cap = vbi_capture_v4l2_new (dev_name,
						 n_buffers,
						 &services,
						 strict,
						 &errstr,
						 trace);
		if (NULL == st->cap) {
			interfaces &= ~INTERFACE_V4L2;
			capture_error_msg ("V4L2", errstr);
			free (errstr);
		} else {
			interfaces = INTERFACE_V4L2;
		}
	}

	if (interfaces & INTERFACE_V4L) {
		char *errstr;

		st->cap = vbi_capture_v4l_new (dev_name,
						system,
						&services,
						strict,
						&errstr,
						trace);
		if (NULL == st->cap) {
			interfaces &= ~INTERFACE_V4L;
			capture_error_msg ("V4L", errstr);
			free (errstr);
		} else {
			interfaces = INTERFACE_V4L;
		}
	}

	if (interfaces & INTERFACE_BKTR) {
		char *errstr;

		st->cap = vbi_capture_bktr_new (dev_name,
						 system,
						 &services,
						 strict,
						 &errstr,
						 trace);
		if (NULL == st->cap) {
			interfaces &= ~INTERFACE_BKTR;
			capture_error_msg ("BKTR", errstr);
			free (errstr);
		} else {
			interfaces = INTERFACE_BKTR;
		}
	}

	if (0 == interfaces)
		exit (EXIT_FAILURE);

	if (interfaces & (INTERFACE_SIM |
			  INTERFACE_V4L2 |
			  INTERFACE_V4L |
			  INTERFACE_BKTR |
			  INTERFACE_PROXY)) {
		unsigned int max_lines;
		unsigned int raw_size;

		st->sp = *vbi_capture_parameters (st->cap);

		max_lines = st->sp.count[0] + st->sp.count[1];
		assert (N_ELEMENTS (st->sliced) >= max_lines);

		raw_size = st->sp.bytes_per_line * max_lines;
		assert (raw_size > 0);

		st->raw = malloc (raw_size);
		if (NULL == st->raw)
			no_mem_exit ();
	} else if (interfaces & INTERFACE_DVB) {
		assert (N_ELEMENTS (st->sliced) >= 2 * 32);

		/* XXX We should have sampling parameters because
		   DVB VBI can transmit raw VBI samples.
		   For now let's just make write_stream_new() happy. */
		CLEAR (st->sp);
#if 2 == VBI_VERSION_MINOR
		st->sp.scanning = 625;
#else
		st->sp.videostd_set = VBI_VIDEOSTD_SET_625_50;
#endif
	}

	st->loop = capture_loop;

	st->interfaces = interfaces;

	st->read_not_pull = read_not_pull;

	st->callback = callback;

	return st;
}

void
parse_option_verbose		(void)
{
	option_log_mask = option_log_mask * 2 + 1;

	vbi_set_log_fn (option_log_mask,
			 vbi_log_on_stderr,
			 /* user_data */ NULL);
}

void
parse_option_quiet		(void)
{
	option_quiet = TRUE;

	option_log_mask = 0;

	vbi_set_log_fn (option_log_mask,
			 /* log_function */ NULL,
			 /* user_data */ NULL);
}

unsigned int
parse_option_ts			(void)
{
	unsigned long int value;
	const char *s = optarg;
	char *end;

	assert (NULL != optarg);

	value = strtoul (s, &end, 0);

	if (value <= 0x000F || value >= 0x1FFF) {
		error_exit (_("Invalid PID %u."), value);
	}

	return value;
}

void
parse_option_dvb_pid		(void)
{
	if (!have_dev_name) {
		/* Change default. */
		option_dev_name = "/dev/dvb/adapter0/demux0";
	}

	option_dvb_pid = parse_option_ts ();
}

void
parse_option_dev_name		(void)
{
	assert (NULL != optarg);

	option_dev_name = optarg;

	have_dev_name = TRUE;
}

void
init_helpers			(int			argc,
				 char **		argv)
{
	argc = argc;
	argv = argv;

#ifndef HAVE_PROGRAM_INVOCATION_NAME

	{
		unsigned int i;

		for (i = strlen (argv[0]); i > 0; --i) {
			if ('/' == argv[0][i - 1])
				break;
		}

		program_invocation_name = argv[0];
		program_invocation_short_name = &argv[0][i];
	}

#endif

	setlocale (LC_ALL, "");

	option_dev_name = "/dev/vbi";

	option_log_mask = VBI_LOG_NOTICE * 2 - 1;

	vbi_set_log_fn (option_log_mask,
			 vbi_log_on_stderr,
			 /* user_data */ NULL);
}
