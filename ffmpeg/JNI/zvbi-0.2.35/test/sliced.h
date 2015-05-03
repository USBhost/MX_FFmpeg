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

/* $Id: sliced.h,v 1.14 2008/03/01 07:37:24 mschimek Exp $ */

/* For libzvbi version 0.2.x / 0.3.x. */

#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

#include "src/macros.h"
#include "src/sliced.h"
#include "src/sampling_par.h"
#include "src/bit_slicer.h"
#include "src/version.h"
#include "src/io-sim.h"

/* Helper functions. */

#ifndef CLEAR
#  define CLEAR(var) memset (&(var), 0, sizeof (var))
#endif

#ifndef N_ELEMENTS
#  define N_ELEMENTS(array) (sizeof (array) / sizeof (*(array)))
#endif

enum file_format {
	FILE_FORMAT_SLICED = 1,
	FILE_FORMAT_RAW,
	FILE_FORMAT_XML,
	FILE_FORMAT_DVB_PES,
	FILE_FORMAT_DVB_TS,
	FILE_FORMAT_NEW_SLICED,
};

enum interface {
	INTERFACE_SIM		= (1 << 0),
	INTERFACE_DVB		= (1 << 1),
	INTERFACE_V4L2		= (1 << 2),
	INTERFACE_V4L		= (1 << 3),
	INTERFACE_BKTR		= (1 << 4),
	INTERFACE_PROXY		= (1 << 5),
};

typedef vbi_bool
stream_callback_fn		(const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time);

struct stream;

#ifndef HAVE_PROGRAM_INVOCATION_NAME
extern char *			program_invocation_name;
extern char *			program_invocation_short_name;
#endif

extern const char *		option_dev_name;
extern unsigned int		option_dvb_pid;
extern vbi_bool		option_quiet;
extern unsigned int		option_log_mask;

extern void
vprint_error			(const char *		template,
				 va_list		ap);
extern void
error_msg			(const char *		template,
				 ...);
extern void
error_exit			(const char *		template,
				 ...);
extern void
write_error_exit		(const char *		msg);
extern void
read_error_exit			(const char *		msg);
extern void
no_mem_exit			(void);

extern void
stream_delete			(struct stream *	st);

extern vbi_bool
stream_loop			(struct stream *	st);
extern vbi_bool
write_stream_raw		(struct stream *	st,
				 uint8_t *		raw,
				 vbi_sampling_par *	sp,
				 double			sample_time,
				 int64_t		stream_time);
extern vbi_bool
write_stream_sliced		(struct stream *	st,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sp,
				 double			sample_time,
				 int64_t		stream_time);
extern void
write_stream_set_data_identifier
				(struct stream *	st,
				 unsigned int		data_identifier);
extern void
write_stream_set_pes_packet_size
				(struct stream *	st,
				 unsigned int		min,
				 unsigned int		max);
extern struct stream *
write_stream_new		(const char *		file_name,
				 enum file_format	file_format,
				 unsigned int		ts_pid,
				 unsigned int		system);

extern struct stream *
read_stream_new			(const char *		file_name,
				 enum file_format	file_format,
				 unsigned int		ts_pid,
				 stream_callback_fn *	callback);

#if 2 == VBI_VERSION_MINOR

typedef struct {
} vbi_bit_slicer_point;

#endif

extern void
capture_stream_sim_set_flags	(struct stream *	st,
				 unsigned int		flags);
extern void
capture_stream_sim_decode_raw	(struct stream *	st,
				 vbi_bool		enable);
extern vbi_bool
capture_stream_sim_load_caption	(struct stream *	st,
				 const char *		stream,
				 vbi_bool		append);
extern vbi_bool
capture_stream_get_point	(struct stream *	st,
				 vbi_bit_slicer_point *point,
				 unsigned int		row,
				 unsigned int		nth_bit);
extern vbi_bool
capture_stream_debug		(struct stream *	st,
				 vbi_bool		enable);
extern void
capture_stream_get_sampling_par	(struct stream *	st,
				 vbi_sampling_par *	sp);
extern struct stream *
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
				 stream_callback_fn *	callback);
extern void
parse_option_verbose		(void);
extern void
parse_option_quiet		(void);
extern unsigned int
parse_option_ts			(void);
extern void
parse_option_dvb_pid		(void);
extern void
parse_option_dev_name		(void);
extern void
init_helpers			(int			argc,
				 char **		argv);
