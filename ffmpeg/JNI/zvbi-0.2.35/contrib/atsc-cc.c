/*
 *  atsc-cc -- ATSC Closed Caption decoder
 *
 *  Copyright (C) 2008 Michael H. Schimek <mschimek@users.sf.net>
 *
 *  Contains code from zvbi-ntsc-cc closed caption decoder written by
 *  <timecop@japan.co.jp>, Mike Baker <mbm@linux.com>,
 *  Mark K. Kim <dev@cbreak.org>.
 *
 *  Thanks to Karol Zapolski for his support.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE 1


#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include "src/libzvbi.h"

/* Linux DVB driver interface. */
#include "src/dvb/dmx.h"
#include "src/dvb/frontend.h"

#undef PROGRAM
#define PROGRAM "ATSC-CC"
#undef VERSION
#define VERSION "0.5"

#if __GNUC__ < 3
#  define likely(expr) (expr)
#  define unlikely(expr) (expr)
#else
#  define likely(expr) __builtin_expect(expr, 1)
#  define unlikely(expr) __builtin_expect(expr, 0)
#endif

#define N_ELEMENTS(array) (sizeof (array) / sizeof ((array)[0]))
#define CLEAR(var) memset (&(var), 0, sizeof (var))

/* FIXME __typeof__ is a GCC extension. */
#undef SWAP
#define SWAP(x, y)							\
do {									\
	__typeof__ (x) _x = x;						\
	x = y;								\
	y = _x;								\
} while (0)

#undef MIN
#define MIN(x, y) ({							\
	__typeof__ (x) _x = (x);					\
	__typeof__ (y) _y = (y);					\
	(void)(&_x == &_y); /* warn if types do not match */		\
	/* return */ (_x < _y) ? _x : _y;				\
})

#undef MAX
#define MAX(x, y) ({							\
	__typeof__ (x) _x = (x);					\
	__typeof__ (y) _y = (y);					\
	(void)(&_x == &_y); /* warn if types do not match */		\
	/* return */ (_x > _y) ? _x : _y;				\
})

#undef PARENT
#define PARENT(_ptr, _type, _member) ({					\
	__typeof__ (&((_type *) 0)->_member) _p = (_ptr);		\
	(_p != 0) ? (_type *)(((char *) _p) - offsetof (_type,		\
	  _member)) : (_type *) 0;					\
})

/* These should be defined in inttypes.h. */
#ifndef PRId64
#  define PRId64 "lld"
#endif
#ifndef PRIu64
#  define PRIu64 "llu"
#endif
#ifndef PRIx64
#  define PRIx64 "llx"
#endif

/* EIA 608-B decoder. */

enum field_num {
	FIELD_1 = 0,
	FIELD_2,
	MAX_FIELDS
};

enum cc_mode {
	CC_MODE_UNKNOWN,
	CC_MODE_ROLL_UP,
	CC_MODE_POP_ON,
	CC_MODE_PAINT_ON,
	CC_MODE_TEXT
};

/* EIA 608-B Section 4.1. */
#define VBI_CAPTION_CC1 1 /* primary synchronous caption service (F1) */
#define VBI_CAPTION_CC2 2 /* special non-synchronous use captions (F1) */
#define VBI_CAPTION_CC3 3 /* secondary synchronous caption service (F2) */
#define VBI_CAPTION_CC4 4 /* special non-synchronous use captions (F2) */

#define VBI_CAPTION_T1 5 /* first text service (F1) */
#define VBI_CAPTION_T2 6 /* second text service (F1) */
#define VBI_CAPTION_T3 7 /* third text service (F2) */
#define VBI_CAPTION_T4 8 /* fourth text service (F2) */

#define UNKNOWN_CC_CHANNEL 0
#define MAX_CC_CHANNELS 8

/* 47 CFR 15.119 (d) Screen format. */
#define CC_FIRST_ROW		0
#define CC_LAST_ROW		14
#define CC_MAX_ROWS		15

#define CC_FIRST_COLUMN		1
#define CC_LAST_COLUMN		32
#define CC_MAX_COLUMNS		32

#define CC_ALL_ROWS_MASK ((1 << CC_MAX_ROWS) - 1)

#define VBI_TRANSLUCENT VBI_SEMI_TRANSPARENT

struct cc_timestamp {
	/* System time when the event occured, zero if no event
	   occured yet. */
	struct timeval		sys;

	/* Presentation time stamp of the event. Only the 33 least
	   significant bits are valid. < 0 if no event occured yet. */
	int64_t			pts;
};

struct cc_channel {
	/**
	 * [0] and [1] are the displayed and non-displayed buffer as
	 * defined in 47 CFR 15.119, and selected by displayed_buffer
	 * below. [2] is a snapshot of the displayed buffer at the
	 * last stream event.
	 *
	 * XXX Text channels don't need buffer[2] and buffer[3], we're
	 * wasting memory.
	 */
	uint16_t		buffer[3][CC_MAX_ROWS][1 + CC_MAX_COLUMNS];

	/**
	 * For buffer[0 ... 2], if bit 1 << row is set this row
	 * contains displayable characters, spacing or non-spacing
	 * attributes. (Special character 0x1139 "transparent space"
	 * is not a displayable character.) This information is
	 * intended to speed up copying, erasing and formatting.
	 */
	unsigned int		dirty[3];

	/** Index of displayed buffer, 0 or 1. */
	unsigned int		displayed_buffer;

	/**
	 * Cursor position: FIRST_ROW ... LAST_ROW and
	 * FIRST_COLUMN ... LAST_COLUMN.
	 */
	unsigned int		curr_row;
	unsigned int		curr_column;

	/**
	 * Text window height in CC_MODE_ROLL_UP. The first row of the
	 * window is curr_row - window_rows + 1, the last row is
	 * curr_row.
	 *
	 * Note: curr_row - window_rows + 1 may be < FIRST_ROW, this
	 * must be clipped before using window_rows:
	 *
	 * actual_rows = MIN (curr_row - FIRST_ROW + 1, window_rows);
	 *
	 * We won't do that at the RUx command because usually a PAC
	 * follows which may change curr_row.
	 */
	unsigned int		window_rows;

	/* Most recently received PAC command. */
	unsigned int		last_pac;

	/**
	 * This variable counts successive transmissions of the
	 * letters A to Z. It is reset to zero on reception of any
	 * letter a to z.
	 *
	 * Some stations do not transmit EIA 608-B extended characters
	 * and except for N with tilde the standard and special
	 * character sets contain only lower case accented
	 * characters. We force these characters to upper case if this
	 * variable indicates live caption, which is usually all upper
	 * case.
	 */
	unsigned int		uppercase_predictor;

	/** Current caption mode or CC_MODE_UNKNOWN. */
	enum cc_mode		mode;

	/**
	 * The time when we last received data for this
	 * channel. Intended to detect if this caption channel is
	 * active.
	 */
	struct cc_timestamp	timestamp;

	/**
	 * The time when we received the first (but not necessarily
	 * leftmost) character in the current row. Unless the mode is
	 * CC_MODE_POP_ON the next stream event will carry this
	 * timestamp.
	 */
	struct cc_timestamp	timestamp_c0;
};

struct cc_decoder {
	/**
	 * Decoder state. We decode all channels in parallel, this way
	 * clients can switch between channels without data loss, or
	 * capture multiple channels with a single decoder instance.
	 *
	 * Also 47 CFR 15.119 and EIA 608-C require us to remember the
	 * cursor position on each channel.
	 */
	struct cc_channel	channel[MAX_CC_CHANNELS];

	/**
	 * Current channel, switched by caption control codes. Can be
	 * one of @c VBI_CAPTION_CC1 ... @c VBI_CAPTION_CC4 or @c
	 * VBI_CAPTION_T1 ... @c VBI_CAPTION_T4 or @c
	 * UNKNOWN_CC_CHANNEL if no channel number was received yet.
	 */
	vbi_pgno		curr_ch_num[MAX_FIELDS];

	/**
	 * Caption control codes (two bytes) may repeat once for error
	 * correction. -1 if no repeated control code can be expected.
	 */
	int			expect_ctrl[MAX_FIELDS][2];

	/** Receiving XDS data, as opposed to caption / ITV data. */
	vbi_bool		in_xds[MAX_FIELDS];

	/**
	 * Pointer into the channel[] array if a display update event
	 * shall be sent at the end of this iteration, %c NULL
	 * otherwise. Purpose is to suppress an event for the first of
	 * two displayable characters in a caption byte pair.
	 */
	struct cc_channel *	event_pending;

	/**
	 * Remembers past parity errors: One bit for each call of
	 * cc_feed(), most recent result in lsb. The idea is to
	 * disable the decoder if we detect too many errors.
	 */
	unsigned int		error_history;

	/**
	 * The time when we last received data, including NUL bytes.
	 * Intended to detect if the station transmits any data on
	 * line 21 or 284 at all.
	 */
	struct cc_timestamp	timestamp;
};

/* CEA 708-C decoder. */

enum justify {
	JUSTIFY_LEFT = 0,
	JUSTIFY_RIGHT,
	JUSTIFY_CENTER,
	JUSTIFY_FULL
};

enum direction {
	DIR_LEFT_RIGHT = 0,
	DIR_RIGHT_LEFT,
	DIR_TOP_BOTTOM,
	DIR_BOTTOM_TOP
};

enum display_effect {
	DISPLAY_EFFECT_SNAP = 0,
	DISPLAY_EFFECT_FADE,
	DISPLAY_EFFECT_WIPE
};

enum opacity {
	OPACITY_SOLID = 0,
	OPACITY_FLASH,
	OPACITY_TRANSLUCENT,
	OPACITY_TRANSPARENT
};

enum edge {
	EDGE_NONE = 0,
	EDGE_RAISED,
	EDGE_DEPRESSED,
	EDGE_UNIFORM,
	EDGE_SHADOW_LEFT,
	EDGE_SHADOW_RIGHT
};

enum pen_size {
	PEN_SIZE_SMALL = 0,
	PEN_SIZE_STANDARD,
	PEN_SIZE_LARGE
};

enum font_style {
	FONT_STYLE_DEFAULT = 0,
	FONT_STYLE_MONO_SERIF,
	FONT_STYLE_PROP_SERIF,
	FONT_STYLE_MONO_SANS,
	FONT_STYLE_PROP_SANS,
	FONT_STYLE_CASUAL,
	FONT_STYLE_CURSIVE,
	FONT_STYLE_SMALL_CAPS
};

enum text_tag {
	TEXT_TAG_DIALOG = 0,
	TEXT_TAG_SOURCE_ID,
	TEXT_TAG_DEVICE,
	TEXT_TAG_DIALOG_2,
	TEXT_TAG_VOICEOVER,
	TEXT_TAG_AUDIBLE_TRANSL,
	TEXT_TAG_SUBTITLE_TRANSL,
	TEXT_TAG_VOICE_DESCR,
	TEXT_TAG_LYRICS,
	TEXT_TAG_EFFECT_DESCR,
	TEXT_TAG_SCORE_DESCR,
	TEXT_TAG_EXPLETIVE,
	TEXT_TAG_NOT_DISPLAYABLE = 15
};

enum offset {
	OFFSET_SUBSCRIPT = 0,
	OFFSET_NORMAL,
	OFFSET_SUPERSCRIPT
};

/* RGB 2:2:2 (lsb = B). */
typedef uint8_t			dtvcc_color;

/* Lsb = window 0, msb = window 7. */
typedef uint8_t			dtvcc_window_map;

struct dtvcc_pen_style {
	enum pen_size			pen_size;
	enum font_style			font_style;
	enum offset			offset;
	vbi_bool			italics;
	vbi_bool			underline;

	enum edge			edge_type;

	dtvcc_color			fg_color;
	enum opacity			fg_opacity;

	dtvcc_color			bg_color;
	enum opacity			bg_opacity;

	dtvcc_color			edge_color;
};

struct dtvcc_pen {
	enum text_tag			text_tag;
	struct dtvcc_pen_style		style;
};

struct dtvcc_window_style {
	enum justify			justify;
	enum direction			print_direction;
	enum direction			scroll_direction;
	vbi_bool			wordwrap;

	enum display_effect		display_effect;
	enum direction			effect_direction;
	unsigned int			effect_speed; /* 1/10 sec */

	dtvcc_color			fill_color;
	enum opacity			fill_opacity;

	enum edge			border_type;
	dtvcc_color			border_color;
};

struct dtvcc_window {
	/* EIA 708-C window state. */

	uint16_t			buffer[16][42];

	vbi_bool			visible;

	/* 0 = highest ... 7 = lowest. */
	unsigned int			priority;

	unsigned int			anchor_point;
	unsigned int			anchor_horizontal;
	unsigned int			anchor_vertical;
	vbi_bool			anchor_relative;

	unsigned int			row_count;
	unsigned int			column_count;

	vbi_bool			row_lock;
	vbi_bool			column_lock;

	unsigned int			curr_row;
	unsigned int			curr_column;

	struct dtvcc_pen		curr_pen;

	struct dtvcc_window_style	style;

	/* Our stuff. */

	/**
	 * If bit 1 << row is set we already sent a stream event for
	 * this row.
	 */
	unsigned int			streamed;

	/**
	 * The time when we received the first (but not necessarily
	 * leftmost) character in the current row. Unless a
	 * DisplayWindow or ToggleWindow command completed the line
	 * the next stream event will carry this timestamp.
	 */
	struct cc_timestamp		timestamp_c0;
};

struct dtvcc_service {
	/* Interpretation Layer. */

	struct dtvcc_window		window[8];

	struct dtvcc_window *		curr_window;

	dtvcc_window_map		created;

	/* For debugging. */
	unsigned int			error_line;

	/* Service Layer. */

	uint8_t				service_data[128];
	unsigned int			service_data_in;

	/** The time when we last received data for this service. */
	struct cc_timestamp		timestamp;
};

struct dtvcc_decoder {
	struct dtvcc_service		service[2];

	/* Packet Layer. */

	uint8_t				packet[128];
	unsigned int			packet_size;

	/* Next expected DTVCC packet sequence_number. Only the two
	   most significant bits are valid. < 0 if no sequence_number
	   has been received yet. */
	int				next_sequence_number;

	/** The time when we last received data. */
	struct cc_timestamp		timestamp;
};

/* ATSC A/53 Part 4:2007 Closed Caption Data decoder. */

enum cc_type {
	NTSC_F1 = 0,
	NTSC_F2 = 1,
	DTVCC_DATA = 2,
	DTVCC_START = 3,
};

struct cc_data_decoder {
	/* Test tap. */
	const char *			option_cc_data_tap_file_name;

	FILE *				cc_data_tap_fp;

	/* For debugging. */
	int64_t				last_pts;
};

/* Caption recorder. */

enum cc_attr {
	VBI_UNDERLINE	= (1 << 0),
	VBI_ITALIC	= (1 << 2),
	VBI_FLASH	= (1 << 3)
};

struct cc_pen {
	uint8_t				attr;

	uint8_t				fg_color;
	uint8_t				fg_opacity;

	uint8_t				bg_color;
	uint8_t				bg_opacity;

	uint8_t				edge_type;
	uint8_t				edge_color;
	uint8_t				edge_opacity;

	uint8_t				pen_size;
	uint8_t				font_style;

	uint8_t				reserved[6];
};

enum caption_format {
	FORMAT_PLAIN,
	FORMAT_VT100,
	FORMAT_NTSC_CC
};

struct caption_recorder {
	/* Caption stream filter:
	   NTSC CC1 ... CC4	(1 << 0 ... 1 << 3),
	   NTSC T1 ... T4	(1 << 4 ... 1 << 7),
	   ATSC Service 1 ... 2	(1 << 8 ... 1 << 9). */
	unsigned int			option_caption_mask;

	/* Output file name for caption stream. */
	const char *			option_caption_file_name[10];

	/* Output file name for XDS data. */
	const char *			option_xds_output_file_name;

	vbi_bool			option_caption_timestamps;

	enum caption_format		option_caption_format;

/* old options */
char usexds;
char usecc;
char usesen;
char usewebtv;

	struct cc_data_decoder		ccd;

	struct cc_decoder		cc;

	struct dtvcc_decoder		dtvcc;

//XDSdecode
unsigned int field;
struct {
	char				packet[34];
	uint8_t				length;
	int				print : 1;
}				info[2][8][25];
char	newinfo[2][8][25][34];
char	*infoptr;
int	mode,type;
char	infochecksum;
const char *		xds_info_prefix;
const char *		xds_info_suffix;
FILE *			xds_fp;

	uint16_t *			ucs_buffer;
	unsigned int			ucs_buffer_length;
	unsigned int			ucs_buffer_capacity;

	FILE *				caption_fp[10];

	int				minicut_min[10];
};

/* Video recorder. */

enum start_code {
	PICTURE_START_CODE = 0x00,
	/* 0x01 ... 0xAF slice_start_code */
	/* 0xB0 reserved */
	/* 0xB1 reserved */
	USER_DATA_START_CODE = 0xB2,
	SEQUENCE_HEADER_CODE = 0xB3,
	SEQUENCE_ERROR_CODE = 0xB4,
	EXTENSION_START_CODE = 0xB5,
	/* 0xB6 reserved */
	SEQUENCE_END_CODE = 0xB7,
	GROUP_START_CODE = 0xB8,
	/* 0xB9 ... 0xFF system start codes */
	PRIVATE_STREAM_1 = 0xBD,
	PADDING_STREAM = 0xBE,
	PRIVATE_STREAM_2 = 0xBF,
	AUDIO_STREAM_0 = 0xC0,
	AUDIO_STREAM_31 = 0xDF,
	VIDEO_STREAM_0 = 0xE0,
	VIDEO_STREAM_15 = 0xEF,
};

enum extension_start_code_identifier {
	/* 0x0 reserved */
	SEQUENCE_EXTENSION_ID = 0x1,
	SEQUENCE_DISPLAY_EXTENSION_ID = 0x2,
	QUANT_MATRIX_EXTENSION_ID = 0x3,
	COPYRIGHT_EXTENSION_ID = 0x4,
	SEQUENCE_SCALABLE_EXTENSION_ID = 0x5,
	/* 0x6 reserved */
	PICTURE_DISPLAY_EXTENSION_ID = 0x7,
	PICTURE_CODING_EXTENSION_ID = 0x8,
	PICTURE_SPATIAL_SCALABLE_EXTENSION_ID = 0x9,
	PICTURE_TEMPORAL_SCALABLE_EXTENSION_ID = 0xA,
	/* 0xB ... 0xF reserved */
};

enum picture_coding_type {
	/* 0 forbidden */
	I_TYPE = 1,
	P_TYPE = 2,
	B_TYPE = 3,
	D_TYPE = 4,
	/* 5 ... 7 reserved */
};

enum picture_structure {
	/* 0 reserved */
	TOP_FIELD = 1,
	BOTTOM_FIELD = 2,
	FRAME_PICTURE = 3
};

/* PTSs and DTSs are 33 bits wide. */
#define TIMESTAMP_MASK (((int64_t) 1 << 33) - 1)

struct packet {
	/* Offset in bytes from the buffer start. */
	unsigned int			offset;

	/* Packet and payload size in bytes. */
	unsigned int			size;
	unsigned int			payload;

	/* Decoding and presentation time stamp and display duration
	   of this packet. */
	int64_t				dts;
	int64_t				pts;
	int64_t				duration;

	/* Cut the stream at this packet. */
	vbi_bool			splice;

	/* Data is missing before this packet but the producer will
	   correct that later. */
	vbi_bool			data_lost;
};

struct buffer {
	uint8_t *			base;

	/* Capacity of the buffer in bytes. */
	unsigned int			capacity;

	/* Offset in bytes from base where the next data will be
	   stored. */
	unsigned int			in;

	/* Offset in bytes from base where the next data will be
	   removed. */ 
	unsigned int			out;
};

#define MAX_PACKETS 64

struct pes_buffer {
	uint8_t *			base;

	/* Capacity of the buffer in bytes. */
	unsigned int			capacity;

	/* Offset in bytes from base where the next data will be stored. */
	unsigned int			in;

	/* Information about the packets in the buffer.
	   packet[0].offset is the offset in bytes from base
	   where the next data will be removed. */
	struct packet			packet[MAX_PACKETS];

	/* Number of packets in the packet[] array. packet[0] must not
	   be removed unless n_packets >= 2. */
	unsigned int			n_packets;
};

struct pes_multiplexer {
	vbi_bool			new_file;
	unsigned int			b_state;

	time_t				minicut_end;

	FILE *				minicut_fp;
};

struct audio_es_packetizer {
	/* Test tap. */
	const char *			option_audio_es_tap_file_name;
	FILE *				audio_es_tap_fp;

	struct buffer			ac3_buffer;
	struct pes_buffer		pes_buffer;

	/* Number of bytes we want to examine at pes_buffer.in. */
	unsigned int			need;

	/* Estimated PTS of the next/current AC3 frame (pts or
	   pes_packet_pts plus previous frame duration), -1 if no PTS
	   was received yet. */
	int64_t				pts;

	/* Next/current AC3 frame is the first received frame. */
	vbi_bool			first_frame;

	/* Data may have been lost between the previous and
	   next/current AC3 frame. */
	vbi_bool			data_lost;

	uint64_t			pes_audio_bit_rate;
};

struct video_es_packetizer {
	struct pes_buffer		pes_buffer;
	int				sequence_header_offset;
	unsigned int			packet_filled;
	uint8_t				pes_packet_header_6;
	uint64_t			pes_video_bit_rate;
	vbi_bool			aligned;
};

struct audio_pes_decoder {
	struct buffer			buffer;

	unsigned int			need;
	unsigned int			look_ahead;
};

struct video_recorder {
	struct audio_pes_decoder	apesd;

	struct video_es_packetizer	vesp;

	struct audio_es_packetizer	aesp;

	struct pes_multiplexer		pm;

	/* TS recorder */

	unsigned int			pat_cc;
	unsigned int			pmt_cc;

	time_t				minicut_end;
	FILE *				minicut_fp;
};

enum received_blocks {
	RECEIVED_PES_PACKET		= (1 << 0),
	RECEIVED_PICTURE		= (1 << 1),
	RECEIVED_PICTURE_EXT		= (1 << 2),
	RECEIVED_MPEG_CC_DATA		= (1 << 3)
};

struct video_es_decoder {
	/* Test tap. */
	const char *			option_video_es_all_tap_file_name;
	const char *			option_video_es_tap_file_name;

	FILE *				video_es_tap_fp;

	/* Video elementary stream buffer. */
	struct buffer			buffer;

	unsigned int			min_bytes_valid;

	/* Number of bytes after buffer.out which have already
	   been scanned for a start code prefix. */
	unsigned int			skip;

	/* Last received start code, < 0 if none. If valid
	   buffer.out points at the first start code prefix
	   byte. */
	enum start_code			last_start_code;

	/* The decoding and presentation time stamp of the
	   current picture. Only the lowest 33 bits are
	   valid. < 0 if no PTS or DTS was received or the PES
	   packet header was malformed. */
	int64_t				pts;
	int64_t				dts;

	/* For debugging. */
	uint64_t			n_pictures_received;

	/* Parameters of the current picture. */
	enum picture_coding_type	picture_coding_type;
	enum picture_structure		picture_structure;
	unsigned int			picture_temporal_reference;

	/* Set of the data blocks we received so far. */
	enum received_blocks		received_blocks;

	/* Describes the contents of the reorder_buffer[]:
	   Bit 0 - a top field in reorder_buffer[0],
	   Bit 1 - a bottom field in reorder_buffer[1],
	   Bit 2 - a frame in reorder_buffer[0]. Only the
	   combinations 0, 1, 2, 3, 4 are valid. */
	unsigned int			reorder_pictures;

	/* The PTS (as above) of the data in the reorder_buffer. */
	int64_t				reorder_pts[2];

	unsigned int			reorder_n_bytes[2];

	/* Buffer to convert picture user data from coded
	   order to display order, for the top and bottom
	   field. Maximum size required: 11 + cc_count * 3,
	   where cc_count = 0 ... 31. */
	uint8_t				reorder_buffer[2][128];
};

struct ts_decoder {
	/* TS PID of the video [0] and audio [1] stream. */
	unsigned int			pid[2];

	/* Next expected video and audio TS packet continuity
	   counter. Only the lowest four bits are valid. < 0
	   if no continuity counter has been received yet. */
	int				next_ts_cc[2];

	/* One or more TS packets were lost. */
	vbi_bool			data_lost;
}					tsd;

struct program {
	const char *			option_station_name;

	/* Convert the captured video and audio data to PES format,
	   cut the stream into one minute long fragments and save them
	   with file name
	   <option_minicut_dir_name>/yyyymmddhh0000/yyyymmddhhmm00.mpg
	   One minute is measured in real time, yyyymmddhhmm is the
	   system time in the UTC zone when the data was received and
	   decoded. */
	const char *			option_minicut_dir_name;

	struct timeval			now;

	int64_t				first_dts;

	struct ts_decoder		tsd;

	struct video_es_decoder		vesd;

	struct video_recorder		vr;

	struct caption_recorder		cr;
};

struct station {
	struct station *		next;
	char *				name;
	enum fe_type			type;
	unsigned long			frequency;
	unsigned int			video_pid;
	unsigned int			audio_pid;
	union {
		struct {
			enum fe_modulation		modulation;
		}				atsc;
		struct {
			enum fe_spectral_inversion	inversion;
			enum fe_bandwidth		bandwidth;
			enum fe_code_rate		code_rate_HP;
			enum fe_code_rate		code_rate_LP;
			enum fe_modulation		constellation;
			enum fe_transmit_mode		transm_mode;
			enum fe_guard_interval		guard_interval;
			enum fe_hierarchy		hierarchy;
		}				dvb_t;
	}				u;
};

enum debug {
	DEBUG_VESD_START_CODE		= (1 << 0),
	DEBUG_VESD_PES_PACKET		= (1 << 1),
	DEBUG_VESD_PIC_HDR		= (1 << 2),
	DEBUG_VESD_PIC_EXT		= (1 << 3),
	DEBUG_VESD_USER_DATA		= (1 << 5),
	DEBUG_VESD_CC_DATA		= (1 << 6),
	DEBUG_CC_DATA			= (1 << 7),
	DEBUG_CC_F1			= (1 << 8),
	DEBUG_CC_F2			= (1 << 9),
	DEBUG_CC_DECODER		= (1 << 10),
	DEBUG_DTVCC_PACKET		= (1 << 11),
	DEBUG_DTVCC_SE			= (1 << 12),
	DEBUG_DTVCC_PUT_CHAR		= (1 << 13),
	DEBUG_DTVCC_STREAM_EVENT	= (1 << 14),
	DEBUG_CONFIG			= (1 << 15)
};

enum source {
	SOURCE_DVB_DEVICE = 1,
	SOURCE_STDIN_TS,

	/* Not implemented yet. */
	SOURCE_STDIN_PES,

	/* For tests only. */
	SOURCE_STDIN_VIDEO_ES,
	SOURCE_STDIN_CC_DATA
};

static const char *		my_name;

static unsigned int		option_verbosity;
static unsigned int		option_debug;

/* Input. */

static enum source		option_source;

/* DVB device. */

static enum fe_type		option_dvb_type;
static unsigned long		option_dvb_adapter_num;
static unsigned long		option_dvb_frontend_id;
static unsigned long		option_dvb_demux_id;
static unsigned long		option_dvb_dvr_id;

static const char *		option_channel_conf_file_name;

/* Test taps. */
const char *			option_ts_all_tap_file_name;
const char *			option_ts_tap_file_name;

static vbi_bool			option_minicut_test;

static const char *		locale_codeset;

/* DVB devices. */
static int			fe_fd;		/* frontend */
static int			dvr_fd;		/* data stream */
static int			dmx_fd;		/* demultiplexer */

/* Capture thread. */

static pthread_t		capture_thread_id;

/* The read buffer of the capture thread. */
static uint8_t *		ct_buffer;
static unsigned int		ct_buffer_capacity;

/* For debugging. */
static uint64_t			ct_n_bytes_in;

/* Transport stream decoder. */

/* static pthread_t		demux_thread_id; */

/* Transport stream buffer. The capture thread stores TS packets at
   ts_buffer_in, the TS decoder removes packets from ts_buffer_out,
   and increments the respective pointer by the number of bytes
   transferred. The input and output pointers jump back to the start
   of the buffer before they would exceed ts_buffer_capacity (wraps at
   packet, not byte granularity).  The buffer is empty if ts_buffer_in
   equals ts_buffer_out. */
static uint8_t *		ts_buffer;
static unsigned int		ts_buffer_capacity;
static volatile unsigned int	ts_buffer_in;
static volatile unsigned int	ts_buffer_out;

static uint8_t			ts_error;

/* For debugging. */
static uint64_t			ts_n_packets_in;

/* Test tap into the transport stream. */
static FILE *			ts_tap_fp;

/* Notifies the TS decoder when data is available in the ts_buffer. */
static pthread_mutex_t		dx_mutex;
static pthread_cond_t		dx_cond;

/* If pid_map[].program < 0, no TS packet with PID n is needed and the
   capture thread will drop the packet. Otherwise pid_map[].program is
   an index into the programs[] table. This information is used by the
   TS decoder to separate multiple video streams. */
static struct {
	int8_t				program;
} 				pid_map[0x2000];

/* The programs we want to record. */
static struct program		program_table[12];
static unsigned int		n_programs;

/* A list of stations found in the channel.conf file. */
static struct station *		station_list;

/* Any station on the selected transponder. */
static struct station *		station;

static void
list_stations			(void);
static void
init_cc_decoder			(struct cc_decoder *	cd);
static void
init_dtvcc_decoder		(struct dtvcc_decoder *	dc);
static void
init_cc_data_decoder		(struct cc_data_decoder *cd);

#define CASE(x) case x: return #x;

static const char *
picture_coding_type_name	(enum picture_coding_type t)
{
	switch (t) {
	CASE (I_TYPE)
	CASE (P_TYPE)
	CASE (B_TYPE)
	CASE (D_TYPE)
	}

	return "invalid";
}

static const char *
picture_structure_name		(enum picture_structure	t)
{
	switch (t) {
	CASE (TOP_FIELD)
	CASE (BOTTOM_FIELD)
	CASE (FRAME_PICTURE)
	}

	return "invalid";
}

static const char *
cc_type_name			(enum cc_type		t)
{
	switch (t) {
	CASE (NTSC_F1)
	CASE (NTSC_F2)
	CASE (DTVCC_DATA)
	CASE (DTVCC_START)
	}

	return "invalid";
}

#undef CASE

static int
printable			(int			c)
{
	if ((c & 0x7F) < 0x20)
		return '.';
	else
		return c & 0x7F;
}

static void
dump				(FILE *			fp,
				 const uint8_t *	buf,
				 unsigned int		n_bytes)
  _vbi_unused;

static void
dump				(FILE *			fp,
				 const uint8_t *	buf,
				 unsigned int		n_bytes)
{
	const unsigned int width = 16;
	unsigned int i;

	for (i = 0; i < n_bytes; i += width) {
		unsigned int end;
		unsigned int j;

		end = MIN (i + width, n_bytes);
		for (j = i; j < end; ++j)
			fprintf (fp, "%02x ", buf[j]);
		for (; j < i + width; ++j)
			fputs ("   ", fp);
		fputc (' ', fp);
		for (j = i; j < end; ++j) {
			int c = buf[j];
			fputc (printable (c), fp);
		}
		fputc ('\n', fp);
	}
}

#define log(verb, templ, args...) \
	log_message (verb, /* print_errno */ FALSE, templ , ##args)

#define log_errno(verb, templ, args...) \
	log_message (verb, /* print_errno */ TRUE, templ , ##args)

#define bug(templ, args...) \
	log_message (1, /* print_errno */ FALSE, "BUG: " templ , ##args)

static void
log_message			(unsigned int		verbosity,
				 vbi_bool		print_errno,
				 const char *		templ,
				 ...)
  _vbi_format ((printf, 3, 4));

static void
log_message			(unsigned int		verbosity,
				 vbi_bool		print_errno,
				 const char *		templ,
				 ...)
{
	if (verbosity <= option_verbosity) {
		va_list ap;

		va_start (ap, templ);

		fprintf (stderr, "%s: ", my_name);
		vfprintf (stderr, templ, ap);

		if (print_errno) {
			fprintf (stderr, ": %s.\n",
				 strerror (errno));
		}

		va_end (ap);
	}
}

#define error_exit(templ, args...) \
	error_message_exit (/* print_errno */ FALSE, templ , ##args)

#define errno_exit(templ, args...) \
	error_message_exit (/* print_errno */ TRUE, templ , ##args)

static void
error_message_exit		(vbi_bool		print_errno,
				 const char *		templ,
				 ...)
  _vbi_format ((printf, 2, 3));

static void
error_message_exit		(vbi_bool		print_errno,
				 const char *		templ,
				 ...)
{
	if (option_verbosity > 0) {
		va_list ap;

		va_start (ap, templ);

		fprintf (stderr, "%s: ", my_name);
		vfprintf (stderr, templ, ap);

		if (print_errno) {
			fprintf (stderr, ": %s.\n",
				 strerror (errno));
		}

		va_end (ap);
	}

	exit (EXIT_FAILURE);
}

static void
no_mem_exit			(void)
{
	error_exit ("Out of memory.");
}

static void *
xmalloc				(size_t			size)
{
	void *p;

	p = malloc (size);
	if (NULL == p) {
		no_mem_exit ();
	}

	return p;
}

static char *
xasprintf			(const char *		templ,
				 ...)
  _vbi_format ((printf, 1, 2));

static char *
xasprintf			(const char *		templ,
				 ...)
{
	va_list ap;
	char *s;
	int r;

	va_start (ap, templ);

	r = vasprintf (&s, templ, ap);
	if (r < 0 || NULL == s) {
		no_mem_exit ();
	}

	va_end (ap);

	return s;
}

static int
xioctl_may_fail			(int			fd,
				 int			request,
				 void *			arg)
{
	int r;

	do r = ioctl (fd, request, arg);
	while (-1 == r && EINTR == errno);

	return r;
}

#define xioctl(fd, request, arg)					\
do {									\
	int r;								\
									\
	r = xioctl_may_fail (fd, request, arg);				\
	if (-1 == r) {							\
		errno_exit (#request " failed");			\
	}								\
} while (0)

static FILE *
open_output_file		(const char *		name)
{
	FILE *fp;

	if (NULL == name || 0 == strcmp (name, "-")) {
		fp = stdout;
	} else {
		fp = fopen (name, "a");
		if (NULL == fp) {
			errno_exit ("Cannot open output file '%s'",
				    name);
		}
	}

	return fp;
}

static FILE *
open_test_file			(const char *		name)
{
	FILE *fp;

	if (NULL == name || 0 == strcmp (name, "-")) {
		fp = stdin;
	} else {
		fp = fopen (name, "r");
		if (NULL == fp) {
			errno_exit ("Cannot open test file '%s'",
				    name);
		}
	}

	return fp;
}

static FILE *
open_minicut_file		(struct program *	pr,
				 const struct tm *	tm,
				 const char *		file_name,
				 const char *		extension)
{
	char dir_name[32];
	size_t base_len;
	size_t dir_len;
	char *buf;
	struct stat st;
	unsigned int i;
	FILE *fp;

	base_len = strlen (pr->option_minicut_dir_name);

	dir_len = snprintf (dir_name, sizeof (dir_name),
			    "/%04u%02u%02u%02u0000",
			    tm->tm_year + 1900,
			    tm->tm_mon + 1,
			    tm->tm_mday,
			    tm->tm_hour);

	buf = xmalloc (base_len + dir_len
		       + strlen (file_name) + 2
		       + strlen (extension) + 1);

	strcpy (buf, pr->option_minicut_dir_name);
	if (0 != stat (buf, &st)) {
		errno_exit ("Cannot open '%s'", buf);
	} else if (!S_ISDIR(st.st_mode)) {
		error_exit ("'%s' is not a directory.\n", buf);
	}

	strcpy (buf + base_len, dir_name);
	if (0 != stat (buf, &st)) {
		if (ENOENT != errno)
			errno_exit ("Cannot open '%s'", buf);
		if (-1 == mkdir (buf, /* mode */ 0777))
			errno_exit ("Cannot create '%s'", buf);
	} else if (!S_ISDIR(st.st_mode)) {
		error_exit ("'%s' is not a directory.\n", buf);
	}

	for (i = 0; i < 100; ++i) {
		int fd;
		
		if (0 == i) {
			sprintf (buf + base_len + dir_len,
				 "%s%s",
				 file_name, extension);
		} else {
			sprintf (buf + base_len + dir_len,
				 "%s-%u%s",
				 file_name, i, extension);
		}

		fd = open64 (buf, (O_CREAT | O_EXCL |
				   O_LARGEFILE | O_WRONLY), 0666);
		if (fd >= 0) {
			fp = fdopen (fd, "w");
			if (NULL == fp)
				goto failed;

			log (2, "Opened '%s'.\n", buf);

			free (buf);

			return fp;
		}

		if (EEXIST == errno)
			continue;
		if (ENOSPC == errno)
			break;

	failed:
		errno_exit ("Cannot open output file '%s'", buf);
	}

	free (buf);

	/* Will try again later. */
	log_errno (1, "Cannot open output file '%s'", buf);

	return NULL;
}

static unsigned int
station_num			(struct program *	pr)
{
	return (pr - program_table) + 1;
}

/* Caption recorder */

static const char *
cr_file_name_suffix [10] = {
	"-cc1", "-cc2", "-cc3", "-cc4",
	"-t1", "-t2", "-t3", "-t4",
	"-s1", "-s2"
};

static void
cr_grow_buffer			(struct caption_recorder *cr,
				 unsigned int		n_chars)
{
	uint16_t *new_buffer;
	size_t min_size;
	size_t new_size;

	if (likely (cr->ucs_buffer_length + n_chars
		    <= cr->ucs_buffer_capacity))
		return;

	min_size = (cr->ucs_buffer_length + n_chars) * 2;
	min_size = MAX ((size_t) 64, min_size);
	new_size = MAX (min_size, (size_t) cr->ucs_buffer_capacity * 4);

	new_buffer = realloc (cr->ucs_buffer, new_size);
	if (NULL == new_buffer)
		no_mem_exit ();

	cr->ucs_buffer = new_buffer;
	cr->ucs_buffer_capacity = new_size / 2;
}

static void
cr_putuc			(struct caption_recorder *cr,
				 uint16_t		uc)
{
	cr_grow_buffer (cr, 1);
	cr->ucs_buffer[cr->ucs_buffer_length++] = uc;
}

static void
cr_puts				(struct caption_recorder *cr,
				 const char *		s)
{
	while (0 != *s)
		cr_putuc (cr, *s++);
}

_vbi_inline void
vbi_char_copy_attr		(struct vbi_char *	cp1,
				 struct vbi_char *	cp2,
				 unsigned int		attr)
{
	if (attr & VBI_UNDERLINE)
		cp1->underline = cp2->underline;
	if (attr & VBI_ITALIC)
		cp1->italic = cp2->italic;
	if (attr & VBI_FLASH)
		cp1->flash = cp2->flash;
}

_vbi_inline void
vbi_char_clear_attr		(struct vbi_char *	cp,
				 unsigned int		attr)
{
	if (attr & VBI_UNDERLINE)
		cp->underline = 0;
	if (attr & VBI_ITALIC)
		cp->italic = 0;
	if (attr & VBI_FLASH)
		cp->flash = 0;
}

_vbi_inline void
vbi_char_set_attr		(struct vbi_char *	cp,
				 unsigned int		attr)
{
	if (attr & VBI_UNDERLINE)
		cp->underline = 1;
	if (attr & VBI_ITALIC)
		cp->italic = 1;
	if (attr & VBI_FLASH)
		cp->flash = 1;
}

_vbi_inline unsigned int
vbi_char_has_attr		(struct vbi_char *	cp,
				 unsigned int		attr)
{
	attr &= (VBI_UNDERLINE | VBI_ITALIC | VBI_FLASH);

	if (0 == cp->underline)
		attr &= ~VBI_UNDERLINE;
	if (0 == cp->italic)
		attr &= ~VBI_ITALIC;
	if (0 == cp->flash)
		attr &= ~VBI_FLASH;

	return attr;
}

_vbi_inline unsigned int
vbi_char_xor_attr		(struct vbi_char *	cp1,
				 struct vbi_char *	cp2,
				 unsigned int		attr)
{
	attr &= (VBI_UNDERLINE | VBI_ITALIC | VBI_FLASH);

	if (0 == (cp1->underline ^ cp2->underline))
		attr &= ~VBI_UNDERLINE;
	if (0 == (cp1->italic ^ cp2->italic))
		attr &= ~VBI_ITALIC;
	if (0 == (cp1->flash ^ cp2->flash))
		attr &= ~VBI_FLASH;

	return attr;
}

static vbi_bool
cr_put_attr			(struct caption_recorder *cr,
				 vbi_char *		prev,
				 vbi_char		curr)
{
	uint16_t *d;

	switch (cr->option_caption_format) {
	case FORMAT_PLAIN:
		return TRUE;

	case FORMAT_NTSC_CC:
		/* Same output as the [zvbi-]ntsc-cc app. */

		curr.opacity = VBI_OPAQUE;

		/* Use the default foreground and background color of
		   the terminal. */
		curr.foreground = -1;
		curr.background = -1;

		if (vbi_char_has_attr (&curr, VBI_ITALIC))
			curr.foreground = VBI_CYAN;

		vbi_char_clear_attr (&curr, VBI_ITALIC | VBI_FLASH);

		break;

	case FORMAT_VT100:
		break;
	}

	cr_grow_buffer (cr, 32);

	d = cr->ucs_buffer + cr->ucs_buffer_length;

	/* Control sequences based on ECMA-48,
	   http://www.ecma-international.org/ */

	/* SGR sequence */

	d[0] = 27; /* CSI */
	d[1] = '[';
	d += 2;

	switch (curr.opacity) {
	case VBI_TRANSPARENT_SPACE:
		vbi_char_clear_attr (&curr, -1);
		curr.foreground = -1;
		curr.background = -1;
		break;

	case VBI_TRANSPARENT_FULL:
		curr.background = -1;
		break;

	case VBI_SEMI_TRANSPARENT:
	case VBI_OPAQUE:
		break;
	}

	if ((prev->foreground != curr.foreground
	     && (uint8_t) -1 == curr.foreground)
	    || (prev->background != curr.background
		&& (uint8_t) -1 == curr.background)) {
		*d++ = ';'; /* "[0m;" reset */
		vbi_char_clear_attr (prev, -1);
		prev->foreground = -1;
		prev->background = -1;
	}

	if (vbi_char_xor_attr (prev, &curr, VBI_ITALIC)) {
		if (!vbi_char_has_attr (&curr, VBI_ITALIC))
			*d++ = '2'; /* off */
		d[0] = '3'; /* italic */
		d[1] = ';';
		d += 2;
	}

	if (vbi_char_xor_attr (prev, &curr, VBI_UNDERLINE)) {
		if (!vbi_char_has_attr (&curr, VBI_UNDERLINE))
			*d++ = '2'; /* off */
		d[0] = '4'; /* underline */
		d[1] = ';';
		d += 2;
	}

	if (vbi_char_xor_attr (prev, &curr, VBI_FLASH)) {
		if (!vbi_char_has_attr (&curr, VBI_FLASH))
			*d++ = '2'; /* steady */
		d[0] = '5'; /* slowly blinking */
		d[1] = ';';
		d += 2;
	}

	if (prev->foreground != curr.foreground) {
		d[0] = '3';
		d[1] = curr.foreground + '0';
		d[2] = ';';
		d += 3;
	}

	if (prev->background != curr.background) {
		d[0] = '4';
		d[1] = curr.background + '0';
		d[2] = ';';
		d += 3;
	}

	vbi_char_copy_attr (prev, &curr, -1);
	prev->foreground = curr.foreground;
	prev->background = curr.background;

	if ('[' == d[-1])
		d -= 2; /* no change, remove CSI */
	else
		d[-1] = 'm'; /* replace last semicolon */

	cr->ucs_buffer_length = d - cr->ucs_buffer;

	return TRUE;
}

static void
cr_timestamp			(struct caption_recorder *cr,
				 struct tm *		tm,
				 time_t			t)
{
	char time_str[32];

	if (!cr->option_caption_timestamps)
		return;

	if (tm->tm_mday <= 0) {
		if (NULL == gmtime_r (&t, tm)) {
			/* Should not happen. */
			error_exit ("System time invalid.\n");
		}
	}
		
	snprintf (time_str, sizeof (time_str),
		  "%04u%02u%02u%02u%02u%02u|",
		  tm->tm_year + 1900, tm->tm_mon + 1,
		  tm->tm_mday, tm->tm_hour,
		  tm->tm_min, tm->tm_sec);

	cr_puts (cr, time_str);
}

static void
cr_minicut			(struct caption_recorder *cr,
				 struct tm *		tm,
				 time_t			t,
				 vbi_pgno		channel)
{
	struct program *pr = PARENT (cr, struct program, cr);

	if (NULL == pr->option_minicut_dir_name)
		return;

	if (NULL != cr->option_caption_file_name[channel - 1])
		return;

	if (tm->tm_mday <= 0) {
		if (NULL == gmtime_r (&t, tm)) {
			/* Should not happen. */
			error_exit ("System time invalid.\n");
		}
	}

	if (tm->tm_min != cr->minicut_min[channel - 1]) {
		char file_name[32];
		FILE *fp;

		fp = cr->caption_fp[channel - 1];
		if (NULL != fp) {
			if (0 != fclose (fp)) {
				log_errno (1, "Station %u CC file "
					   "write error",
					   station_num (pr));
			}
		}

		snprintf (file_name, sizeof (file_name),
			  "/%04u%02u%02u%02u%02u00%s",
			  tm->tm_year + 1900, tm->tm_mon + 1,
			  tm->tm_mday, tm->tm_hour, tm->tm_min,
			  cr_file_name_suffix [channel - 1]);

		/* Note: May be NULL. */
		cr->caption_fp[channel - 1] =
			open_minicut_file (pr, tm, file_name, ".txt");

		cr->minicut_min[channel - 1] = tm->tm_min;
	}
}

static void
cr_new_line			(struct caption_recorder *cr,
				 struct cc_timestamp *	ts,
				 vbi_pgno		channel,
				 const vbi_char		text[42],
				 unsigned int		length)
{
	FILE *fp;

	if (0 == (ts->sys.tv_sec | ts->sys.tv_usec)
	    || channel >= 10 || length < 32)
		return;

	if (0 == (cr->option_caption_mask & (1 << (channel - 1))))
		return;

	cr->ucs_buffer_length = 0;
	
	if (cr->usesen) {
		unsigned int uc[3];
		unsigned int column;
		unsigned int end;
		unsigned int separator;

		end = length;

		/* Eat trailing spaces. */
		while (end > 0 && ' ' == text[end - 1].unicode)
			--end;

		uc[2] = ' ';
		separator = 0;

		for (column = 0; column < end + 1; ++column) {
			uc[0] = uc[1];
			uc[1] = uc[2];
			uc[2] = 0;
			if (column < end)
				uc[2] = text[column].unicode;

			if (0 == separator && ' ' == uc[1])
				continue;
			cr_putuc (cr, uc[1]);
			separator = ' ';

			switch (uc[1]) {
			case '"':
				if ('.' != uc[0] && '!' != uc[0]
				    && '?' != uc[0])
					continue;
				break;

			case '.':
				if ('.' == uc[0] || '.' == uc[2])
					continue;
				/* fall through */

			case '!':
			case '?':
				if ('"' == uc[2])
					continue;
				if (0 != uc[2] && ' ' != uc[2])
					continue;
				break;

			default:
				continue;
			}

			cr_putuc (cr, '\n');
			separator = 0;
		}

		if (0 != separator)
			cr_putuc (cr, separator);
	} else {
		struct tm tm;
		vbi_char prev_char;
		unsigned int column;

		tm.tm_mday = 0;
		cr_minicut (cr, &tm, (time_t) ts->sys.tv_sec, channel);
		cr_timestamp (cr, &tm, (time_t) ts->sys.tv_sec);

		vbi_char_clear_attr (&prev_char, -1);
		prev_char.foreground = -1;
		prev_char.background = -1;

		for (column = 0; column < length; ++column) {
			cr_put_attr (cr, &prev_char, text[column]);
			cr_putuc (cr, text[column].unicode);
		}

		if (0 != vbi_char_has_attr (&prev_char, -1)
		    || (uint8_t) -1 != prev_char.foreground
		    || (uint8_t) -1 != prev_char.background) {
			static const char end_seq[] = {
				27, '[', 'm', '\n', 0
			};

			cr_puts (cr, end_seq);
		} else {
			cr_putuc (cr, '\n');
		}
	}

	fp = cr->caption_fp[channel - 1];

	/* May be NULL due to a bug or a temporary failure of
	   open_minicut_file(). */
	if (NULL != fp) {
		vbi_fputs_iconv_ucs2 (fp, locale_codeset,
				      cr->ucs_buffer,
				      cr->ucs_buffer_length,
				      /* repl_char */ '?');
	}
}

static void
init_caption_recorder		(struct caption_recorder *cr)
{
	CLEAR (*cr);

	cr->option_xds_output_file_name = "-";

	init_cc_data_decoder (&cr->ccd);
	init_cc_decoder (&cr->cc);
	init_dtvcc_decoder (&cr->dtvcc);

	cr->infoptr = cr->newinfo[0][0][0];
	cr->xds_info_prefix = "\33[33m% ";
	cr->xds_info_suffix = "\33[0m\n";
	cr->usewebtv = 1;

	memset (cr->minicut_min, -1, sizeof (cr->minicut_min));
}

/* EIA 608-B Closed Caption decoder. */

static void
cc_timestamp_reset		(struct cc_timestamp *	ts)
{
	CLEAR (ts->sys);
	ts->pts = -1;
}

static vbi_bool
cc_timestamp_isset		(struct cc_timestamp *	ts)
{
	return (ts->pts >= 0 || 0 != (ts->sys.tv_sec | ts->sys.tv_usec));
}

static const vbi_color
cc_color_map [8] = {
	VBI_WHITE, VBI_GREEN, VBI_BLUE, VBI_CYAN,
	VBI_RED, VBI_YELLOW, VBI_MAGENTA, VBI_BLACK
};

static const int8_t
cc_pac_row_map [16] = {
	/* 0 */ 10,			/* 0x1040 */
	/* 1 */ -1,			/* no function */
	/* 2 */ 0, 1, 2, 3,		/* 0x1140 ... 0x1260 */
	/* 6 */ 11, 12, 13, 14,		/* 0x1340 ... 0x1460 */
	/* 10 */ 4, 5, 6, 7, 8, 9	/* 0x1540 ... 0x1760 */
};

static void
dump_cc				(FILE *			fp,
				 unsigned int		index,
				 unsigned int		cc_count,
				 unsigned int		cc_valid,
				 unsigned int		cc_type,
				 unsigned int		c1,
				 unsigned int		c2)
{
	uint16_t ucs2_str[2];
	unsigned int ch;
	unsigned int a7;
	unsigned int f;
	unsigned int b7;
	unsigned int u;

	fprintf (fp,
		 "%s%u/%u %d %s %02X%02X %02X%c%02X%c",
		 (NTSC_F2 == cc_type) ? "\t\t\t\t\t\t\t\t" : "",
		 index, cc_count, !!cc_valid,
		 cc_type_name (cc_type), c1, c2,
		 c1 & 0x7F, vbi_unpar8 (c1) < 0 ? '*' : ' ',
		 c2 & 0x7F, vbi_unpar8 (c2) < 0 ? '*' : ' ');

	c1 &= 0x7F;
	c2 &= 0x7F;

	if (0 == c1) {
		fputs (" null\n", fp);
		return;
	} else if (c1 < 0x10) {
		fputc ('\n', fp);
		return;
	} else if (c1 >= 0x20) {
		fputs (" '", fp);
		ucs2_str[0] = vbi_caption_unicode (c1, /* to_upper */ FALSE);
		ucs2_str[1] = vbi_caption_unicode (c2, /* to_upper */ FALSE);
		vbi_fputs_iconv_ucs2 (fp, locale_codeset,
				      ucs2_str, 2,
				      /* repl_char */ '?');
		fputs ("'\n", fp);
		return;
	} else if (c2 < 0x20) {
		fputs (" INVALID\n", fp);
		return;
	}

	/* Some common bit groups. */

	ch = (c1 >> 3) & 1; /* channel */
	a7 = c1 & 7;
	f = c1 & 1; /* field */
	b7 = (c2 >> 1) & 7;
	u = c2 & 1; /* underline */

	if (c2 >= 0x40) {
		unsigned int row;

		/* Preamble Address Codes -- 001 crrr  1ri bbbu */
  
		row = cc_pac_row_map [a7 * 2 + ((c2 >> 5) & 1)];
		if (c2 & 0x10)
			fprintf (fp, " PAC ch=%u row=%d column=%u u=%u\n",
				 ch, row, b7 * 4, u);
		else
			fprintf (fp, " PAC ch=%u row=%d color=%u u=%u\n",
				 ch, row, b7, u);
		return;
	}

	/* Control codes -- 001 caaa  01x bbbu */

	switch (a7) {
	case 0:
		if (c2 < 0x30) {
			static const char mnemo [16 * 4] =
				"BWO\0BWS\0BGO\0BGS\0"
				"BBO\0BBS\0BCO\0BCS\0"
				"BRO\0BRS\0BYO\0BYS\0"
				"BMO\0BMS\0BAO\0BAS";

			fprintf (fp, " %s ch=%u\n",
				 mnemo + (c2 & 0xF) * 4, ch);
			return;
		}
		break;

	case 1:
		if (c2 < 0x30) {
			fprintf (fp, " mid-row ch=%u color=%u u=%u\n",
				 ch, b7, u);
		} else {
			fprintf (fp, " special character ch=%u '", ch);
			ucs2_str[0] = vbi_caption_unicode
				(0x1100 | c2, /* to_upper */ FALSE);
			vbi_fputs_iconv_ucs2 (fp, locale_codeset,
					      ucs2_str, 1,
					      /* repl_char */ '?');
			fputs ("'\n", fp);
		}
		return;

	case 2: /* first group */
	case 3: /* second group */
		fprintf (fp, " extended character ch=%u '", ch);
		ucs2_str[0] = vbi_caption_unicode
			(c1 * 256 + c2, /* to_upper */ FALSE);
		vbi_fputs_iconv_ucs2 (fp, locale_codeset,
				      ucs2_str, 1,
				      /* repl_char */ '?');
		fputs ("'\n", fp);
		return;

	case 4: /* f=0 */
	case 5: /* f=1 */
		if (c2 < 0x30) {
			static const char mnemo [16 * 4] =
				"RCL\0BS \0AOF\0AON\0"
				"DER\0RU2\0RU3\0RU4\0"
				"FON\0RDC\0TR \0RTD\0"
				"EDM\0CR \0ENM\0EOC";

			fprintf (fp, " %s ch=%u f=%u\n",
				 mnemo + (c2 & 0xF) * 4, ch, f);
			return;
		}
		break;

	case 6:
		fprintf (fp, " reserved\n");
		return;

	case 7:
		switch (c2) {
		case 0x21 ... 0x23:
			fprintf (fp, " TO%u ch=%u\n", c2 - 0x20, ch);
			return;

		case 0x2D:
			fprintf (fp, " BT ch=%u\n", ch);
			return;

		case 0x2E:
			fprintf (fp, " FA ch=%u\n", ch);
			return;

		case 0x2F:
			fprintf (fp, " FAU ch=%u\n", ch);
			return;

		default:
			break;
		}
		break;
	}

	fprintf (fp, " unknown\n");
}

static void
cc_reset			(struct cc_decoder *	cd);

static vbi_pgno
cc_channel_num			(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	return (ch - cd->channel) + 1;
}

/* Note 47 CFR 15.119 (h) Character Attributes: "(1) Transmission of
   Attributes. A character may be transmitted with any or all of four
   attributes: Color, italics, underline, and flash. All of these
   attributes are set by control codes included in the received
   data. An attribute will remain in effect until changed by another
   control code or until the end of the row is reached. Each row
   begins with a control code which sets the color and underline
   attributes. (White non-underlined is the default display attribute
   if no Preamble Address Code is received before the first character
   on an empty row.) Attributes are not affected by transparent spaces
   within a row. (i) All Mid-Row Codes and the Flash On command are
   spacing attributes which appear in the display just as if a
   standard space (20h) had been received. Preamble Address Codes are
   non-spacing and will not alter any attributes when used to position
   the cursor in the midst of a row of characters. (ii) The color
   attribute has the highest priority and can only be changed by the
   Mid-Row Code of another color. Italics has the next highest
   priority. If characters with both color and italics are desired,
   the italics Mid-Row Code must follow the color assignment. Any
   color Mid-Row Code will turn off italics. If the least significant
   bit of a Preamble Address Code or of a color or italics Mid-Row
   Code is a 1 (high), underlining is turned on. If that bit is a 0
   (low), underlining is off. (iii) The flash attribute is transmitted
   as a Miscellaneous Control Code. The Flash On command will not
   alter the status of the color, italics, or underline
   attributes. However, any coloror italics Mid-Row Code will turn off
   flash. (iv) Thus, for example, if a red, italicized, underlined,
   flashing character is desired, the attributes must be received in
   the following order: a red Mid-Row or Preamble Address Code, an
   italics Mid-Row Code with underline bit, and the Flash On
   command. The character will then be preceded by three spaces (two
   if red was assigned via a Preamble Address Code)."

   EIA 608-B Annex C.7 Preamble Address Codes and Tab Offsets
   (Regulatory/Preferred): "In general, Preamble Address Codes (PACs)
   have no immediate effect on the display. A major exception is the
   receipt of a PAC during roll-up captioning. In that case, if the
   base row designated in the PAC is not the same as the current base
   row, the display shall be moved immediately to the new base
   row. [...] An indenting PAC carries the attributes of white,
   non-italicized, and it sets underlining on or off. Tab Offset
   commands do not change these attributes. If an indenting PAC with
   underline ON is received followed by a Tab Offset and by text, the
   text shall be underlined (except as noted below). When a
   displayable character is received, it is deposited at the current
   cursor position. If there is already a displayable character in the
   column immediately to the left, the new character assumes the
   attributes of that character. The new character may be arriving as
   the result of an indenting PAC (with or without a Tab Offset), and
   that PAC may designate other attributes, but the new character is
   forced to assume the attributes of the character immediately to its
   left, and the PAC's attributes are ignored. If, when a displayable
   character is received, it overwrites an existing PAC or mid-row
   code, and there are already characters to the right of the new
   character, these existing characters shall assume the same
   attributes as the new character. This adoption can result in a
   whole caption row suddenly changing color, underline, italics,
   and/or flash attributes."

   EIA 608-B Annex C.14 Special Cases Regarding Attributes
   (Normative): "In most cases, Preamble Address Codes shall set
   attributes for the caption elements they address. It is
   theoretically possible for a service provider to use an indenting
   PAC to start a row at Column 5 or greater, and then to use
   Backspace to move the cursor to the left of the PAC into an area to
   which no attributes have been assigned. It is also possible for a
   roll-up row, having been created by a Carriage Return, to receive
   characters with no PAC used to set attributes. In these cases, and
   in any other case where no explicit attributes have been assigned,
   the display shall be white, non-underlined, non-italicized, and
   non-flashing. In case new displayable characters are received
   immediately after a Delete to End of Row (DER), the display
   attributes of the first deleted character shall remain in effect if
   there is a displayable character to the left of the cursor;
   otherwise, the most recently received PAC shall set the display
   attributes."

   47 CFR 15.119 (n) Glossary of terms: "(6) Displayable character:
   Any letter, number or symbol which is defined for on-screen
   display, plus the 20h space. [...] (13) Special characters:
   Displayable characters (except for "transparent space") [...]" */

static void
cc_format_row			(struct cc_decoder *	cd,
				 struct vbi_char *	cp,
				 struct cc_channel *	ch,
				 unsigned int		buffer,
				 unsigned int		row,
				 vbi_bool		to_upper,
				 vbi_bool		padding)
{
	struct vbi_char ac;
	unsigned int i;

	cd = cd; /* unused */

	/* 47 CFR 15.119 (h)(1). EIA 608-B Section 6.4. */
	CLEAR (ac);
	ac.foreground = VBI_WHITE;
	ac.background = VBI_BLACK;

	/* Shortcut. */
	if (0 == (ch->dirty[buffer] & (1 << row))) {
		vbi_char *end;

		ac.unicode = 0x20;
		ac.opacity = VBI_TRANSPARENT_SPACE;

		end = cp + CC_MAX_COLUMNS;
		if (padding)
			end += 2;

		while (cp < end)
			*cp++ = ac;

		return;
	}

	if (padding) {
		ac.unicode = 0x20;
		ac.opacity = VBI_TRANSPARENT_SPACE;
		*cp++ = ac;
	}

	/* EIA 608-B Section 6.4. */
	ac.opacity = VBI_OPAQUE;

	for (i = CC_FIRST_COLUMN - 1; i <= CC_LAST_COLUMN; ++i) {
		unsigned int color;
		unsigned int c;

		ac.unicode = 0x20;

		c = ch->buffer[buffer][row][i];
		if (0 == c) {
			if (padding
			    && VBI_TRANSPARENT_SPACE != cp[-1].opacity
			    && 0x20 != cp[-1].unicode) {
				/* Append a space with the same colors
				   and opacity (opaque or
				   transp. backgr.) as the text to the
				   left of it. */
				*cp++ = ac;
				/* We don't underline spaces, see
				   below. */
				vbi_char_clear_attr (cp - 1, -1);
			} else if (i > 0) {
				*cp++ = ac;
				cp[-1].opacity = VBI_TRANSPARENT_SPACE;
			}

			continue;
		} else if (c < 0x1040) {
			if (padding
			    && VBI_TRANSPARENT_SPACE == cp[-1].opacity) {
				/* Prepend a space with the same
				   colors and opacity (opaque or
				   transp. backgr.) as the text to the
				   right of it. */
				cp[-1] = ac;
				/* We don't underline spaces, see
				   below. */
				vbi_char_clear_attr (cp - 1, -1);
			}

			if ((c >= 'a' && c <= 'z')
			    || 0x7E == c /* n with tilde */) {
				/* We do not force these characters to
				   upper case because the standard
				   character set includes upper case
				   versions of these characters and
				   lower case was probably
				   deliberately transmitted. */
				ac.unicode = vbi_caption_unicode
					(c, /* to_upper */ FALSE);
			} else {
				ac.unicode = vbi_caption_unicode
					(c, to_upper);
			}
		} else if (c < 0x1120) {
			/* Preamble Address Codes -- 001 crrr  1ri xxxu */

			/* PAC is a non-spacing attribute and only
			   stored in the buffer at the addressed
			   column minus one if it replaces a
			   transparent space (EIA 608-B Annex C.7,
			   C.14). There's always a transparent space
			   to the left of the first column but we show
			   this zeroth column only if padding is
			   enabled. */
			if (padding
			    && VBI_TRANSPARENT_SPACE != cp[-1].opacity
			    && 0x20 != cp[-1].unicode) {
				/* See 0 == c. */
				*cp++ = ac;
				vbi_char_clear_attr (cp - 1, -1);
			} else if (i > 0) {
				*cp++ = ac;
				cp[-1].opacity = VBI_TRANSPARENT_SPACE;
			}

			vbi_char_clear_attr (&ac, VBI_UNDERLINE | VBI_ITALIC);
			if (c & 0x0001)
				vbi_char_set_attr (&ac, VBI_UNDERLINE);
			if (c & 0x0010) {
				ac.foreground = VBI_WHITE;
			} else {
				color = (c >> 1) & 7;
				if (7 == color) {
					ac.foreground = VBI_WHITE;
					vbi_char_set_attr (&ac, VBI_ITALIC);
				} else {
					ac.foreground = cc_color_map[color];
				}
			}

			continue;
		} else if (c < 0x1130) {
			/* Mid-Row Codes -- 001 c001  010 xxxu */
			/* 47 CFR 15.119 Mid-Row Codes table,
			   (h)(1)(ii), (h)(1)(iii). */

			/* 47 CFR 15.119 (h)(1)(i), EIA 608-B Section
			   6.2: Mid-Row codes, FON, BT, FA and FAU are
			   set-at spacing attributes. */

			vbi_char_clear_attr (&ac, -1);
			if (c & 0x0001)
				vbi_char_set_attr (&ac, VBI_UNDERLINE);
			color = (c >> 1) & 7;
			if (7 == color) {
				vbi_char_set_attr (&ac, VBI_ITALIC);
			} else {
				ac.foreground = cc_color_map[color];
			}
		} else if (c < 0x1220) {
			/* Special Characters -- 001 c001  011 xxxx */
			/* 47 CFR 15.119 Character Set Table. */

			if (padding
			    && VBI_TRANSPARENT_SPACE == cp[-1].opacity) {
				cp[-1] = ac;
				vbi_char_clear_attr (cp - 1, -1);
			}

			/* Note we already stored 0 instead of 0x1139
			   (transparent space) in the ch->buffer. */
			ac.unicode = vbi_caption_unicode (c, to_upper);
		} else if (c < 0x1428) {
			/* Extended Character Set -- 001 c01x  01x xxxx */
			/* EIA 608-B Section 6.4.2 */

			if (padding
			    && VBI_TRANSPARENT_SPACE == cp[-1].opacity) {
				cp[-1] = ac;
				vbi_char_clear_attr (cp - 1, -1);
			}

			/* We do not force these characters to upper
			   case because the extended character set
			   includes upper case versions of all letters
			   and lower case was probably deliberately
			   transmitted. */
			ac.unicode = vbi_caption_unicode
				(c, /* to_upper */ FALSE);
		} else if (c < 0x172D) {
			/* FON Flash On -- 001 c10f  010 1000 */
			/* 47 CFR 15.119 (h)(1)(iii). */

			vbi_char_set_attr (&ac, VBI_FLASH);
		} else if (c < 0x172E) {
			/* BT Background Transparent -- 001 c111  010 1101 */
			/* EIA 608-B Section 6.4. */

			ac.opacity = VBI_TRANSPARENT_FULL;
		} else if (c <= 0x172F) {
			/* FA Foreground Black -- 001 c111  010 111u */
			/* EIA 608-B Section 6.4. */

			if (c & 0x0001)
				vbi_char_set_attr (&ac, VBI_UNDERLINE);
			ac.foreground = VBI_BLACK;
		}

		*cp++ = ac;

		/* 47 CFR 15.119 and EIA 608-B are silent about
		   underlined spaces, but considering the example in
		   47 CFR (h)(1)(iv) which would produce something
		   ugly like "__text" I suppose we should not
		   underline them. For good measure we also clear the
		   invisible italic and flash attribute. */
		if (0x20 == ac.unicode)
			vbi_char_clear_attr (cp - 1, -1);
	}

	if (padding) {
		ac.unicode = 0x20;
		vbi_char_clear_attr (&ac, -1);

		if (VBI_TRANSPARENT_SPACE != cp[-1].opacity
		    && 0x20 != cp[-1].unicode) {
			*cp = ac;
		} else {
			ac.opacity = VBI_TRANSPARENT_SPACE;
			*cp = ac;
		}
	}
}

typedef enum {
	/**
	 * 47 CFR Section 15.119 requires caption decoders to roll
	 * caption smoothly: Nominally each character cell has a
	 * height of 13 field lines. When this flag is set the current
	 * caption should be displayed with a vertical offset of 12
	 * field lines, and after every 1001 / 30000 seconds the
	 * caption overlay should move up by one field line until the
	 * offset is zero. The roll rate should be no more than 0.433
	 * seconds/row for other character cell heights.
	 *
	 * The flag may be set again before the offset returned to
	 * zero. The caption overlay should jump to offset 12 in this
	 * case regardless.
	 */
	VBI_START_ROLLING	= (1 << 0)
} vbi_cc_page_flags;

static void
cc_display_event		(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 vbi_cc_page_flags	flags)
{
	cd = cd; /* unused */
	ch = ch;
	flags = flags;
}

/* This decoder is mainly designed to overlay caption onto live video,
   but to create transcripts we also offer an event every time a line
   of caption is complete. The event occurs when certain control codes
   are received.

   In POP_ON mode we send the event upon reception of EOC, which swaps
   the displayed and non-displayed memory.

   In ROLL_UP and TEXT mode captioners are not expected to display new
   text by erasing and overwriting a row with PAC, TOx, BS and DER so
   we ignore these codes. In ROLL_UP mode CR, EDM, EOC, RCL and RDC
   complete a line. CR moves the cursor to a new row, EDM erases the
   displayed memory. The remaining codes switch to POP_ON or PAINT_ON
   mode. In TEXT mode CR and TR are our line completion indicators. CR
   works as above and TR erases the displayed memory. EDM, EOC, RDC,
   RCL and RUx have no effect on TEXT buffers.

   In PAINT_ON mode RDC never erases the displayed memory and CR has
   no function. Instead captioners can freely position the cursor and
   erase or overwrite (parts of) rows with PAC, TOx, BS and DER, or
   erase all rows with EDM. We send an event on PAC, EDM, EOC, RCL and
   RUx, provided the characters (including spacing attributes) in the
   current row changed since the last event. PAC is the only control
   code which can move the cursor to the left and/or to a new row, and
   likely to introduce a new line. EOC, RCL and RUx switch to POP_ON
   or ROLL_UP mode. */

static void
cc_stream_event			(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 unsigned int		first_row,
				 unsigned int		last_row)
{
	vbi_pgno channel;
	unsigned int row;

	channel = cc_channel_num (cd, ch);

	for (row = first_row; row <= last_row; ++row) {
		struct vbi_char text[36];
		unsigned int end;

		cc_format_row (cd, text, ch,
			       ch->displayed_buffer,
			       row, /* to_upper */ FALSE,
			       /* padding */ FALSE);

		for (end = 32; end > 0; --end) {
			if (VBI_TRANSPARENT_SPACE != text[end - 1].opacity)
				break;
		}

		if (0 == end)
			continue;

		{
			struct program *pr;

			pr = PARENT (cd, struct program, cr.cc);
			cr_new_line (&pr->cr, &ch->timestamp_c0,
				     channel, text, /* length */ 32);
		}
	}

	cc_timestamp_reset (&ch->timestamp_c0);
}

static void
cc_put_char			(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 int			c,
				 vbi_bool		displayable,
				 vbi_bool		backspace)
{
	uint16_t *text;
	unsigned int curr_buffer;
	unsigned int row;
	unsigned int column;

	/* 47 CFR Section 15.119 (f)(1), (f)(2), (f)(3). */
	curr_buffer = ch->displayed_buffer
		^ (CC_MODE_POP_ON == ch->mode);

	row = ch->curr_row;
	column = ch->curr_column;

	if (unlikely (backspace)) {
		/* 47 CFR 15.119 (f)(1)(vi), (f)(2)(ii),
		   (f)(3)(i). EIA 608-B Section 6.4.2, 7.4. */
		if (column > CC_FIRST_COLUMN)
			--column;
	} else {
		/* 47 CFR 15.119 (f)(1)(v), (f)(1)(vi), (f)(2)(ii),
		   (f)(3)(i). EIA 608-B Section 7.4. */
		if (column < CC_LAST_COLUMN)
			ch->curr_column = column + 1;
	}

	text = &ch->buffer[curr_buffer][row][0];
	text[column] = c;

	/* Send a display update event when the displayed buffer of
	   the current channel changed, but no more than once for each
	   pair of Closed Caption bytes. */
	/* XXX This may not be a visible change, but such cases are
	   rare and we'd need something close to format_row() to be
	   sure. */
	if (CC_MODE_POP_ON != ch->mode) {
		cd->event_pending = ch;
	}

	if (likely (displayable)) {
		/* Note EIA 608-B Annex C.7, C.14. */
		if (CC_FIRST_COLUMN == column
		    || 0 == text[column - 1]) {
			/* Note last_pac may be 0 as well. */
			text[column - 1] = ch->last_pac;
		}

		if (c >= 'a' && c <= 'z') {
			ch->uppercase_predictor = 0;
		} else if (c >= 'A' && c <= 'Z') {
			unsigned int up;

			up = ch->uppercase_predictor + 1;
			if (up > 0)
				ch->uppercase_predictor = up;
		}
	} else if (unlikely (0 == c)) {
		unsigned int i;

		/* This is Special Character "Transparent space". */

		for (i = CC_FIRST_COLUMN; i <= CC_LAST_COLUMN; ++i)
			c |= ch->buffer[curr_buffer][row][i];

		ch->dirty[curr_buffer] &= ~((0 == c) << row);

		return;
	}

	assert (sizeof (ch->dirty[0]) * 8 - 1 >= CC_MAX_ROWS);
	ch->dirty[curr_buffer] |= 1 << row;

	if (ch->timestamp_c0.pts < 0
	    && 0 == (ch->timestamp_c0.sys.tv_sec
		     | ch->timestamp_c0.sys.tv_usec)) {
		ch->timestamp_c0 = cd->timestamp;
	}
}

static void
cc_ext_control_code		(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 unsigned int		c2)
{
	unsigned int column;

	switch (c2) {
	case 0x21: /* TO1 */
	case 0x22: /* TO2 */
	case 0x23: /* TO3 Tab Offset -- 001 c111  010 00xx */
		/* 47 CFR 15.119 (e)(1)(ii). EIA 608-B Section 7.4,
		   Annex C.7. */
		column = ch->curr_column + (c2 & 3);
		ch->curr_column = MIN (column,
				       (unsigned int) CC_LAST_COLUMN);
		break;

	case 0x24: /* Select standard character set in normal size */
	case 0x25: /* Select standard character set in double size */
	case 0x26: /* Select first private character set */
	case 0x27: /* Select second private character set */
	case 0x28: /* Select character set GB 2312-80 (Chinese) */
	case 0x29: /* Select character set KSC 5601-1987 (Korean) */
	case 0x2A: /* Select first registered character set. */
		/* EIA 608-B Section 6.3 Closed Group Extensions. */
		break;

	case 0x2D: /* BT Background Transparent -- 001 c111  010 1101 */
	case 0x2E: /* FA Foreground Black -- 001 c111  010 1110 */
	case 0x2F: /* FAU Foregr. Black Underl. -- 001 c111  010 1111 */
		/* EIA 608-B Section 6.2. */
		cc_put_char (cd, ch, 0x1700 | c2,
			     /* displayable */ FALSE,
			     /* backspace */ TRUE);
		break;

	default:
		/* 47 CFR Section 15.119 (j): Ignore. */
		break;
	}
}

/* Send a stream event if the current row has changed since the last
   stream event. This is necessary in paint-on mode where CR has no
   function and captioners can freely position the cursor to erase or
   overwrite (parts of) rows. */
static void
cc_stream_event_if_changed	(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;
	unsigned int i;

	curr_buffer = ch->displayed_buffer;
	row = ch->curr_row;

	if (0 == (ch->dirty[curr_buffer] & (1 << row)))
		return;

	for (i = CC_FIRST_COLUMN; i <= CC_LAST_COLUMN; ++i) {
		unsigned int c1;
		unsigned int c2;

		c1 = ch->buffer[curr_buffer][row][i];
		if (c1 >= 0x1040) {
			if (c1 < 0x1120) {
				c1 = 0; /* PAC -- non-spacing */
			} else if (c1 < 0x1130 || c1 >= 0x1428) {
				/* MR, FON, BT, FA, FAU -- spacing */
				c1 = 0x20;
			}
		}

		c2 = ch->buffer[2][row][i];
		if (c2 >= 0x1040) {
			if (c2 < 0x1120) {
				c2 = 0;
			} else if (c2 < 0x1130 || c2 >= 0x1428) {
				c1 = 0x20;
			}
		}

		if (c1 != c2) {
			cc_stream_event (cd, ch, row, row);

			memcpy (ch->buffer[2][row],
				ch->buffer[curr_buffer][row],
				sizeof (ch->buffer[0][0]));

			ch->dirty[2] = ch->dirty[curr_buffer];

			return;
		}
	}
}

static void
cc_end_of_caption		(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;

	/* EOC End Of Caption -- 001 c10f  010 1111 */

	curr_buffer = ch->displayed_buffer;

	switch (ch->mode) {
	case CC_MODE_UNKNOWN:
	case CC_MODE_POP_ON:
		break;

	case CC_MODE_ROLL_UP:
		row = ch->curr_row;
		if (0 != (ch->dirty[curr_buffer] & (1 << row)))
			cc_stream_event (cd, ch, row, row);
		break;

	case CC_MODE_PAINT_ON:
		cc_stream_event_if_changed (cd, ch);
		break;

	case CC_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}

	ch->displayed_buffer = curr_buffer ^= 1;

	/* 47 CFR Section 15.119 (f)(2). */
	ch->mode = CC_MODE_POP_ON;

	if (0 != ch->dirty[curr_buffer]) {
		ch->timestamp_c0 = cd->timestamp;

		cc_stream_event (cd, ch,
				 CC_FIRST_ROW,
				 CC_LAST_ROW);

		cc_display_event (cd, ch, 0);
	}
}

static void
cc_carriage_return		(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;
	unsigned int window_rows;
	unsigned int first_row;

	/* CR Carriage Return -- 001 c10f  010 1101 */

	curr_buffer = ch->displayed_buffer;
	row = ch->curr_row;

	switch (ch->mode) {
	case CC_MODE_UNKNOWN:
		return;

	case CC_MODE_ROLL_UP:
		/* 47 CFR Section 15.119 (f)(1)(iii). */
		ch->curr_column = CC_FIRST_COLUMN;

		/* 47 CFR 15.119 (f)(1): "The cursor always remains on
		   the base row." */

		/* XXX Spec? */
		ch->last_pac = 0;

		/* No event if the buffer contains only
		   TRANSPARENT_SPACEs. */
		if (0 == ch->dirty[curr_buffer])
			return;

		window_rows = MIN (row + 1 - CC_FIRST_ROW,
				   ch->window_rows);
		break;

	case CC_MODE_POP_ON:
	case CC_MODE_PAINT_ON:
		/* 47 CFR 15.119 (f)(2)(i), (f)(3)(i): No effect. */
		return;

	case CC_MODE_TEXT:
		/* 47 CFR Section 15.119 (f)(1)(iii). */
		ch->curr_column = CC_FIRST_COLUMN;

		/* XXX Spec? */
		ch->last_pac = 0;

		/* EIA 608-B Section 7.4: "When Text Mode has
		   initially been selected and the specified Text
		   memory is empty, the cursor starts at the topmost
		   row, Column 1, and moves down to Column 1 on the
		   next row each time a Carriage Return is received
		   until the last available row is reached. A variety
		   of methods may be used to accomplish the scrolling,
		   provided that the text is legible while moving. For
		   example, as soon as all of the available rows of
		   text are on the screen, Text Mode switches to the
		   standard roll-up type of presentation." */

		if (CC_LAST_ROW != row) {
			if (0 != (ch->dirty[curr_buffer] & (1 << row))) {
				cc_stream_event (cd, ch, row, row);
			}

			ch->curr_row = row + 1;

			return;
		}

		/* No event if the buffer contains all
		   TRANSPARENT_SPACEs. */
		if (0 == ch->dirty[curr_buffer])
			return;

		window_rows = CC_MAX_ROWS;

		break;
	}

	/* 47 CFR Section 15.119 (f)(1)(iii). In roll-up mode: "Each
	   time a Carriage Return is received, the text in the top row
	   of the window is erased from memory and from the display or
	   scrolled off the top of the window. The remaining rows of
	   text are each rolled up into the next highest row in the
	   window, leaving the base row blank and ready to accept new
	   text." */

	if (0 != (ch->dirty[curr_buffer] & (1 << row))) {
		cc_stream_event (cd, ch, row, row);
	}

	first_row = row + 1 - window_rows;
	memmove (ch->buffer[curr_buffer][first_row],
		 ch->buffer[curr_buffer][first_row + 1],
		 (window_rows - 1) * sizeof (ch->buffer[0][0]));

	ch->dirty[curr_buffer] >>= 1;

	memset (ch->buffer[curr_buffer][row], 0,
		sizeof (ch->buffer[0][0]));

	cc_display_event (cd, ch, VBI_START_ROLLING);
}

static void
cc_erase_memory			(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 unsigned int		buffer)
{
	if (0 != ch->dirty[buffer]) {
		CLEAR (ch->buffer[buffer]);

		ch->dirty[buffer] = 0;

		if (buffer == ch->displayed_buffer)
			cc_display_event (cd, ch, 0);
	}
}

static void
cc_erase_displayed_memory	(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int row;

	/* EDM Erase Displayed Memory -- 001 c10f  010 1100 */

	switch (ch->mode) {
	case CC_MODE_UNKNOWN:
		/* We have not received EOC, RCL, RDC or RUx yet, but
		   ch is valid. */
		break;

	case CC_MODE_ROLL_UP:
		row = ch->curr_row;
		if (0 != (ch->dirty[ch->displayed_buffer] & (1 << row)))
			cc_stream_event (cd, ch, row, row);
		break;

	case CC_MODE_PAINT_ON:
		cc_stream_event_if_changed (cd, ch);
		break;

	case CC_MODE_POP_ON:
		/* Nothing to do. */
		break;

	case CC_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}

	/* May send a display event. */
	cc_erase_memory (cd, ch, ch->displayed_buffer);
}

static void
cc_text_restart			(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;

	/* TR Text Restart -- 001 c10f  010 1010 */

	curr_buffer = ch->displayed_buffer;
	row = ch->curr_row;

	/* ch->mode is invariably CC_MODE_TEXT. */

	if (0 != (ch->dirty[curr_buffer] & (1 << row))) {
		cc_stream_event (cd, ch, row, row);
	}

	/* EIA 608-B Section 7.4. */
	/* May send a display event. */
	cc_erase_memory (cd, ch, ch->displayed_buffer);

	/* EIA 608-B Section 7.4. */
	ch->curr_row = CC_FIRST_ROW;
	ch->curr_column = CC_FIRST_COLUMN;
}

static void
cc_resume_direct_captioning	(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;

	/* RDC Resume Direct Captioning -- 001 c10f  010 1001 */

	/* 47 CFR 15.119 (f)(1)(x), (f)(2)(vi) and EIA 608-B Annex
	   B.7: Does not erase memory, does not move the cursor when
	   resuming after a Text transmission.

	   XXX If ch->mode is unknown, roll-up or pop-on, what shall
	   we do if no PAC is received between RDC and the text? */

	curr_buffer = ch->displayed_buffer;
	row = ch->curr_row;

	switch (ch->mode) {
	case CC_MODE_ROLL_UP:
		if (0 != (ch->dirty[curr_buffer] & (1 << row)))
			cc_stream_event (cd, ch, row, row);

		/* fall through */

	case CC_MODE_UNKNOWN:
	case CC_MODE_POP_ON:
		/* No change since last stream_event(). */
		memcpy (ch->buffer[2], ch->buffer[curr_buffer],
			sizeof (ch->buffer[2]));
		break;

	case CC_MODE_PAINT_ON:
		/* Mode continues. */
		break;

	case CC_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}

	ch->mode = CC_MODE_PAINT_ON;
}

static void
cc_resize_window		(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 unsigned int		new_rows)
{
	unsigned int curr_buffer;
	unsigned int max_rows;
	unsigned int old_rows;
	unsigned int row1;

	curr_buffer = ch->displayed_buffer;

	/* No event if the buffer contains all TRANSPARENT_SPACEs. */
	if (0 == ch->dirty[curr_buffer])
		return;

	row1 = ch->curr_row + 1;
	max_rows = row1 - CC_FIRST_ROW;
	old_rows = MIN (ch->window_rows, max_rows);
	new_rows = MIN (new_rows, max_rows);

	/* Nothing to do unless the window shrinks. */
	if (0 == new_rows || new_rows >= old_rows)
		return;

	memset (&ch->buffer[curr_buffer][row1 - old_rows][0], 0,
		(old_rows - new_rows)
		* sizeof (ch->buffer[0][0]));

	ch->dirty[curr_buffer] &= -1 << (row1 - new_rows);

	cc_display_event (cd, ch, 0);
}

static void
cc_roll_up_caption		(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 unsigned int		c2)
{
	unsigned int window_rows;

	/* Roll-Up Captions -- 001 c10f  010 01xx */

	window_rows = (c2 & 7) - 3; /* 2, 3, 4 */

	switch (ch->mode) {
	case CC_MODE_ROLL_UP:
		/* 47 CFR 15.119 (f)(1)(iv). */
		/* May send a display event. */
		cc_resize_window (cd, ch, window_rows);

		/* fall through */

	case CC_MODE_UNKNOWN:
		ch->mode = CC_MODE_ROLL_UP;
		ch->window_rows = window_rows;

		/* 47 CFR 15.119 (f)(1)(ix): No cursor movements,
		   no memory erasing. */

		break;

	case CC_MODE_PAINT_ON:
		cc_stream_event_if_changed (cd, ch);

		/* fall through */

	case CC_MODE_POP_ON:
		ch->mode = CC_MODE_ROLL_UP;
		ch->window_rows = window_rows;

		/* 47 CFR 15.119 (f)(1)(ii). */
		ch->curr_row = CC_LAST_ROW;
		ch->curr_column = CC_FIRST_COLUMN;

		/* 47 CFR 15.119 (f)(1)(x). */
		/* May send a display event. */
		cc_erase_memory (cd, ch, ch->displayed_buffer);
		cc_erase_memory (cd, ch, ch->displayed_buffer ^ 1);

		break;

	case CC_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}
}

static void
cc_delete_to_end_of_row		(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;

	/* DER Delete To End Of Row -- 001 c10f  010 0100 */

	/* 47 CFR 15.119 (f)(1)(vii), (f)(2)(iii), (f)(3)(ii) and EIA
	   608-B Section 7.4: In all caption modes and Text mode
	   "[the] Delete to End of Row command will erase from memory
	   any characters or control codes starting at the current
	   cursor location and in all columns to its right on the same
	   row." */

	curr_buffer = ch->displayed_buffer
		^ (CC_MODE_POP_ON == ch->mode);

	row = ch->curr_row;

	/* No event if the row contains only TRANSPARENT_SPACEs. */
	if (0 != (ch->dirty[curr_buffer] & (1 << row))) {
		unsigned int column;
		unsigned int i;
		uint16_t c;

		column = ch->curr_column;

		memset (&ch->buffer[curr_buffer][row][column], 0,
			(CC_LAST_COLUMN - column + 1)
			* sizeof (ch->buffer[0][0][0]));

		c = 0;
		for (i = CC_FIRST_COLUMN; i < column; ++i)
			c |= ch->buffer[curr_buffer][row][i];

		ch->dirty[curr_buffer] &= ~((0 == c) << row);

		cc_display_event (cd, ch, 0);
	}
}

static void
cc_backspace			(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int curr_buffer;
	unsigned int row;
	unsigned int column;

	/* BS Backspace -- 001 c10f  010 0001 */

	/* 47 CFR Section 15.119 (f)(1)(vi), (f)(2)(ii), (f)(3)(i) and
	   EIA 608-B Section 7.4. */
	column = ch->curr_column;
	if (column <= CC_FIRST_COLUMN)
		return;

	ch->curr_column = --column;

	curr_buffer = ch->displayed_buffer
		^ (CC_MODE_POP_ON == ch->mode);

	row = ch->curr_row;

	/* No event if there's no visible effect. */
	if (0 != ch->buffer[curr_buffer][row][column]) {
		unsigned int i;
		uint16_t c;

		/* 47 CFR 15.119 (f), (f)(1)(vi), (f)(2)(ii) and EIA
		   608-B Section 7.4. */
		ch->buffer[curr_buffer][row][column] = 0;

		c = 0;
		for (i = CC_FIRST_COLUMN; i <= CC_LAST_COLUMN; ++i)
			c |= ch->buffer[curr_buffer][row][i];

		ch->dirty[curr_buffer] &= ~((0 == c) << row);

		cc_display_event (cd, ch, 0);
	}
}

static void
cc_resume_caption_loading	(struct cc_decoder *	cd,
				 struct cc_channel *	ch)
{
	unsigned int row;

	/* RCL Resume Caption Loading -- 001 c10f  010 0000 */

	switch (ch->mode) {
	case CC_MODE_UNKNOWN:
	case CC_MODE_POP_ON:
		break;

	case CC_MODE_ROLL_UP:
		row = ch->curr_row;
		if (0 != (ch->dirty[ch->displayed_buffer] & (1 << row)))
			cc_stream_event (cd, ch, row, row);
		break;

	case CC_MODE_PAINT_ON:
		cc_stream_event_if_changed (cd, ch);
		break;

	case CC_MODE_TEXT:
		/* Not reached. (ch is a caption channel.) */
		return;
	}

	/* 47 CFR 15.119 (f)(1)(x): Does not erase memory.
	   (f)(2)(iv): Cursor position remains unchanged. */

	ch->mode = CC_MODE_POP_ON;
}



/* Note curr_ch is invalid if UNKNOWN_CC_CHANNEL == cd->cc.curr_ch_num. */
static struct cc_channel *
cc_switch_channel		(struct cc_decoder *	cd,
				 struct cc_channel *	curr_ch,
				 vbi_pgno		new_ch_num,
				 enum field_num		f)
{
	struct cc_channel *new_ch;

	if (UNKNOWN_CC_CHANNEL != cd->curr_ch_num[f]
	    && CC_MODE_UNKNOWN != curr_ch->mode) {
		/* XXX Force a display update if we do not send events
		   on every display change. */
	}

	cd->curr_ch_num[f] = new_ch_num;
	new_ch = &cd->channel[new_ch_num - VBI_CAPTION_CC1];

	return new_ch;
}

/* Note ch is invalid if UNKNOWN_CC_CHANNEL == cd->cc.curr_ch_num[f]. */
static void
cc_misc_control_code		(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 unsigned int		c2,
				 unsigned int		ch_num0,
				 enum field_num		f)
{
	unsigned int new_ch_num;

	/* Misc Control Codes -- 001 c10f  010 xxxx */

	/* c = channel (0 -> CC1/CC3/T1/T3, 1 -> CC2/CC4/T2/T4)
	     -- 47 CFR Section 15.119, EIA 608-B Section 7.7.
	   f = field (0 -> F1, 1 -> F2)
	     -- EIA 608-B Section 8.4, 8.5. */

	/* XXX The f flag is intended to detect accidential field
	   swapping and we should use it for that purpose. */

	switch (c2 & 15) {
	case 0:	/* RCL Resume Caption Loading -- 001 c10f  010 0000 */
		/* 47 CFR 15.119 (f)(2) and EIA 608-B Section 7.7. */
		new_ch_num = VBI_CAPTION_CC1 + (ch_num0 & 3);
		ch = cc_switch_channel (cd, ch, new_ch_num, f);
		cc_resume_caption_loading (cd, ch);
		break;

	case 1: /* BS Backspace -- 001 c10f  010 0001 */
		if (UNKNOWN_CC_CHANNEL == cd->curr_ch_num[f]
		    || CC_MODE_UNKNOWN == ch->mode)
			break;
		cc_backspace (cd, ch);
		break;

	case 2: /* reserved (formerly AOF Alarm Off) */
	case 3: /* reserved (formerly AON Alarm On) */
		break;

	case 4: /* DER Delete To End Of Row -- 001 c10f  010 0100 */
		if (UNKNOWN_CC_CHANNEL == cd->curr_ch_num[f]
		    || CC_MODE_UNKNOWN == ch->mode)
			break;
		cc_delete_to_end_of_row (cd, ch);
		break;

	case 5: /* RU2 */
	case 6: /* RU3 */
	case 7: /* RU4 Roll-Up Captions -- 001 c10f  010 01xx */
		/* 47 CFR 15.119 (f)(1) and EIA 608-B Section 7.7. */
		new_ch_num = VBI_CAPTION_CC1 + (ch_num0 & 3);
		ch = cc_switch_channel (cd, ch, new_ch_num, f);
		cc_roll_up_caption (cd, ch, c2);
		break;

	case 8: /* FON Flash On -- 001 c10f  010 1000 */
		if (UNKNOWN_CC_CHANNEL == cd->curr_ch_num[f]
		    || CC_MODE_UNKNOWN == ch->mode)
			break;

		/* 47 CFR 15.119 (h)(1)(i): Spacing attribute. */
		cc_put_char (cd, ch, 0x1428,
			     /* displayable */ FALSE,
			     /* backspace */ FALSE);
		break;

	case 9: /* RDC Resume Direct Captioning -- 001 c10f  010 1001 */
		/* 47 CFR 15.119 (f)(3) and EIA 608-B Section 7.7. */
		new_ch_num = VBI_CAPTION_CC1 + (ch_num0 & 3);
		ch = cc_switch_channel (cd, ch, new_ch_num, f);
		cc_resume_direct_captioning (cd, ch);
		break;

	case 10: /* TR Text Restart -- 001 c10f  010 1010 */
		/* EIA 608-B Section 7.4. */
		new_ch_num = VBI_CAPTION_T1 + (ch_num0 & 3);
		ch = cc_switch_channel (cd, ch, new_ch_num, f);
		cc_text_restart (cd, ch);
		break;

	case 11: /* RTD Resume Text Display -- 001 c10f  010 1011 */
		/* EIA 608-B Section 7.4. */
		new_ch_num = VBI_CAPTION_T1 + (ch_num0 & 3);
		ch = cc_switch_channel (cd, ch, new_ch_num, f);
		/* ch->mode is invariably CC_MODE_TEXT. */
		break;

	case 12: /* EDM Erase Displayed Memory -- 001 c10f  010 1100 */
		/* 47 CFR 15.119 (f). EIA 608-B Section 7.7 and Annex
		   B.7: "[The] command shall be acted upon as
		   appropriate for caption processing without
		   terminating the Text Mode data stream." */

		/* We need not check cd->curr_ch_num because bit 2 is
		   implied, bit 1 is the known field number and bit 0
		   is coded in the control code. */
		ch = &cd->channel[ch_num0 & 3];

		cc_erase_displayed_memory (cd, ch);

		break;

	case 13: /* CR Carriage Return -- 001 c10f  010 1101 */
		if (UNKNOWN_CC_CHANNEL == cd->curr_ch_num[f])
			break;
		cc_carriage_return (cd, ch);
		break;

	case 14: /* ENM Erase Non-Displayed Memory -- 001 c10f  010 1110 */
		/* 47 CFR 15.119 (f)(2)(v). EIA 608-B Section 7.7 and
		   Annex B.7: "[The] command shall be acted upon as
		   appropriate for caption processing without
		   terminating the Text Mode data stream." */

		/* See EDM. */
		ch = &cd->channel[ch_num0 & 3];

		cc_erase_memory (cd, ch, ch->displayed_buffer ^ 1);

		break;

	case 15: /* EOC End Of Caption -- 001 c10f  010 1111 */
		/* 47 CFR 15.119 (f), (f)(2), (f)(3)(iv) and EIA 608-B
		   Section 7.7, Annex C.11. */
		new_ch_num = VBI_CAPTION_CC1 + (ch_num0 & 3);
		ch = cc_switch_channel (cd, ch, new_ch_num, f);
		cc_end_of_caption (cd, ch);
		break;
	}
}

static void
cc_move_window			(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 unsigned int		new_base_row)
{
	uint8_t *base;
	unsigned int curr_buffer;
	unsigned int bytes_per_row;
	unsigned int old_max_rows;
	unsigned int new_max_rows;
	unsigned int copy_bytes;
	unsigned int erase_begin;
	unsigned int erase_end;

	curr_buffer = ch->displayed_buffer;

	/* No event if we do not move the window or the buffer
	   contains only TRANSPARENT_SPACEs. */
	if (new_base_row == ch->curr_row
	    || 0 == ch->dirty[curr_buffer])
		return;

	base = (void *) &ch->buffer[curr_buffer][CC_FIRST_ROW][0];
	bytes_per_row = sizeof (ch->buffer[0][0]);

	old_max_rows = ch->curr_row + 1 - CC_FIRST_ROW;
	new_max_rows = new_base_row + 1 - CC_FIRST_ROW;
	copy_bytes = MIN (MIN (old_max_rows, new_max_rows),
			  ch->window_rows) * bytes_per_row;

	if (new_base_row < ch->curr_row) {
		erase_begin = (new_base_row + 1) * bytes_per_row;
		erase_end = (ch->curr_row + 1) * bytes_per_row;

		memmove (base + erase_begin - copy_bytes,
			 base + erase_end - copy_bytes, copy_bytes);

		ch->dirty[curr_buffer] >>= ch->curr_row - new_base_row;
	} else {
		erase_begin = (ch->curr_row + 1) * bytes_per_row
			- copy_bytes;
		erase_end = (new_base_row + 1) * bytes_per_row
			- copy_bytes;

		memmove (base + erase_end,
			 base + erase_begin, copy_bytes);

		ch->dirty[curr_buffer] <<= new_base_row - ch->curr_row;
		ch->dirty[curr_buffer] &= CC_ALL_ROWS_MASK;
	}

	memset (base + erase_begin, 0, erase_end - erase_begin);

	cc_display_event (cd, ch, 0);
}

static void
cc_preamble_address_code	(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 unsigned int		c1,
				 unsigned int		c2)
{
	unsigned int row;

	/* PAC Preamble Address Codes -- 001 crrr  1ri xxxu */

	row = cc_pac_row_map[(c1 & 7) * 2 + ((c2 >> 5) & 1)];
	if ((int) row < 0)
		return;

	switch (ch->mode) {
	case CC_MODE_UNKNOWN:
		return;

	case CC_MODE_ROLL_UP:
		/* EIA 608-B Annex C.4. */
		if (ch->window_rows > row + 1)
			row = ch->window_rows - 1;

		/* 47 CFR Section 15.119 (f)(1)(ii). */
		/* May send a display event. */
		cc_move_window (cd, ch, row);

		ch->curr_row = row;

		break;

	case CC_MODE_PAINT_ON:
		cc_stream_event_if_changed (cd, ch);

		/* fall through */

	case CC_MODE_POP_ON:
		/* XXX 47 CFR 15.119 (f)(2)(i), (f)(3)(i): In Pop-on
		   and paint-on mode "Preamble Address Codes can be
		   used to move the cursor around the screen in random
		   order to place captions on Rows 1 to 15." We do not
		   have a limit on the number of displayable rows, but
		   as EIA 608-B Annex C.6 points out, if more than
		   four rows must be displayed they were probably
		   received in error and we should respond
		   accordingly. */

		/* 47 CFR Section 15.119 (d)(1)(i) and EIA 608-B Annex
		   C.7. */
		ch->curr_row = row;

		break;

	case CC_MODE_TEXT:
		/* 47 CFR 15.119 (e)(1) and EIA 608-B Section 7.4:
		   Does not change the cursor row. */
		break;
	}

	if (c2 & 0x10) {
		/* 47 CFR 15.119 (e)(1)(i) and EIA 608-B Table 71. */
		ch->curr_column = CC_FIRST_COLUMN + (c2 & 0x0E) * 2;
	}

	/* PAC is a non-spacing attribute for the next character, see
	   cc_put_char(). */
	ch->last_pac = 0x1000 | c2;
}

static void
cc_control_code			(struct cc_decoder *	cd,
				 unsigned int		c1,
				 unsigned int		c2,
				 enum field_num		f)
{
	struct cc_channel *ch;
	unsigned int ch_num0;

	if (option_debug & DEBUG_CC_DECODER) {
		fprintf (stderr, "%s %02x %02x %d\n",
			 __FUNCTION__, c1, c2, f);
	}

	/* Caption / text, field 1 / 2, primary / secondary channel. */
	ch_num0 = (((cd->curr_ch_num[f] - VBI_CAPTION_CC1) & 4)
		   + f * 2
		   + ((c1 >> 3) & 1));

	/* Note ch is invalid if UNKNOWN_CC_CHANNEL ==
	   cd->curr_ch_num[f]. */
	ch = &cd->channel[ch_num0];

	if (c2 >= 0x40) {
		/* Preamble Address Codes -- 001 crrr  1ri xxxu */
		if (UNKNOWN_CC_CHANNEL != cd->curr_ch_num[f])
			cc_preamble_address_code (cd, ch, c1, c2);
		return;
	}

	switch (c1 & 7) {
	case 0:
		if (UNKNOWN_CC_CHANNEL == cd->curr_ch_num[f]
		    || CC_MODE_UNKNOWN == ch->mode)
			break;

		if (c2 < 0x30) {
			/* Backgr. Attr. Codes -- 001 c000  010 xxxt */
			/* EIA 608-B Section 6.2. */
			cc_put_char (cd, ch, 0x1000 | c2,
				     /* displayable */ FALSE,
				     /* backspace */ TRUE);
		} else {
			/* Undefined. */
		}

		break;

	case 1:
		if (UNKNOWN_CC_CHANNEL == cd->curr_ch_num[f]
		    || CC_MODE_UNKNOWN == ch->mode)
			break;

		if (c2 < 0x30) {
			/* Mid-Row Codes -- 001 c001  010 xxxu */
			/* 47 CFR 15.119 (h)(1)(i): Spacing attribute. */
			cc_put_char (cd, ch, 0x1100 | c2,
				     /* displayable */ FALSE,
				     /* backspace */ FALSE);
		} else {
			/* Special Characters -- 001 c001  011 xxxx */
			if (0x39 == c2) {
				/* Transparent space. */
				cc_put_char (cd, ch, 0,
					     /* displayable */ FALSE,
					     /* backspace */ FALSE);
			} else {
				cc_put_char (cd, ch, 0x1100 | c2,
					     /* displayable */ TRUE,
					     /* backspace */ FALSE);
			}
		}

		break;

	case 2:
	case 3: /* Extended Character Set -- 001 c01x  01x xxxx */
		if (UNKNOWN_CC_CHANNEL == cd->curr_ch_num[f]
		    || CC_MODE_UNKNOWN == ch->mode)
			break;

		/* EIA 608-B Section 6.4.2. */
		cc_put_char (cd, ch, (c1 * 256 + c2) & 0x777F,
			     /* displayable */ TRUE,
			     /* backspace */ TRUE);
		break;

	case 4:
	case 5:
		if (c2 < 0x30) {
			/* Misc. Control Codes -- 001 c10f  010 xxxx */
			cc_misc_control_code (cd, ch, c2, ch_num0, f);
		} else {
			/* Undefined. */
		}

		break;

	case 6: /* reserved */
		break;

	case 7:	/* Extended control codes -- 001 c111  01x xxxx */
		if (UNKNOWN_CC_CHANNEL == cd->curr_ch_num[f]
		    || CC_MODE_UNKNOWN == ch->mode)
			break;

		cc_ext_control_code (cd, ch, c2);

		break;
	}
}

static vbi_bool
cc_characters			(struct cc_decoder *	cd,
				 struct cc_channel *	ch,
				 int			c)
{
	if (option_debug & DEBUG_CC_DECODER) {
		fprintf (stderr, "%s %02x '%c'\n",
			 __FUNCTION__, c, printable (c));
	}

	if (0 == c) {
		if (CC_MODE_UNKNOWN == ch->mode)
			return TRUE;

		/* XXX After x NUL characters (presumably a caption
		   pause), force a display update if we do not send
		   events on every display change. */

		return TRUE;
	}

	if (c < 0x20) {
		/* Parity error or invalid data. */

		if (c < 0 && CC_MODE_UNKNOWN != ch->mode) {
			/* 47 CFR Section 15.119 (j)(1). */
			cc_put_char (cd, ch, 0x7F,
				     /* displayable */ TRUE,
				     /* backspace */ FALSE);
		}

		return FALSE;
	}

	if (CC_MODE_UNKNOWN != ch->mode) {
		cc_put_char (cd, ch, c,
			     /* displayable */ TRUE,
			     /* backspace */ FALSE);
	}

	return TRUE;
}

static vbi_bool
cc_feed				(struct cc_decoder *	cd,
				 const uint8_t		buffer[2],
				 unsigned int		line,
				 const struct timeval *	tv,
				 int64_t		pts)
{
	int c1, c2;
	enum field_num f;
	vbi_bool all_successful;

	assert (NULL != cd);

	if (option_debug & DEBUG_CC_DECODER) {
		fprintf (stderr, "%s %02x %02x '%c%c' "
			 "%3d %f %" PRId64 "\n",
			 __FUNCTION__,
			 buffer[0] & 0x7F,
			 buffer[1] & 0x7F,
			 printable (buffer[0]),
			 printable (buffer[1]),
			 line,
			 tv->tv_sec + tv->tv_usec * (1 / 1e6),
			 pts);
	}

	f = FIELD_1;

	switch (line) {
	case 21: /* NTSC */
	case 22: /* PAL/SECAM */
		break;

	case 284: /* NTSC */
		f = FIELD_2;
		break;

	default:
		return FALSE;
	}

	cd->timestamp.sys = *tv;
	cd->timestamp.pts = pts;

	/* FIXME deferred reset here */

	c1 = vbi_unpar8 (buffer[0]);
	c2 = vbi_unpar8 (buffer[1]);

	all_successful = TRUE;

	/* 47 CFR 15.119 (2)(i)(4): "If the first transmission of a
	   control code pair passes parity, it is acted upon within
	   one video frame. If the next frame contains a perfect
	   repeat of the same pair, the redundant code is ignored. If,
	   however, the next frame contains a different but also valid
	   control code pair, this pair, too, will be acted upon (and
	   the receiver will expect a repeat of this second pair in
	   the next frame).  If the first byte of the expected
	   redundant control code pair fails the parity check and the
	   second byte is identical to the second byte in the
	   immediately preceding pair, then the expected redundant
	   code is ignored. If there are printing characters in place
	   of the redundant code, they will be processed normally."

	   EIA 608-B Section 8.3: Caption control codes on field 2 may
	   repeat as on field 1. Section 8.6.2: XDS control codes
	   shall not repeat. */

	if (unlikely (c1 < 0)) {
		goto parity_error;
	} else if (c1 == cd->expect_ctrl[f][0]
		   && c2 == cd->expect_ctrl[f][1]) {
		/* Already acted upon. */
		cd->expect_ctrl[f][0] = -1;
		goto finish;
	}

	if (c1 >= 0x10 && c1 < 0x20) {
		/* Caption control code. */

		/* There's no XDS on field 1, we just
		   use an array to save a branch. */
		cd->in_xds[f] = FALSE;

		/* 47 CFR Section 15.119 (i)(1), (i)(2). */
		if (c2 < 0x20) {
			/* Parity error or invalid control code.
			   Let's hope it repeats. */
			goto parity_error;
		}

		cc_control_code (cd, c1, c2, f);

		if (cd->event_pending) {
			cc_display_event (cd, cd->event_pending, 0);
			cd->event_pending = NULL;
		}

		cd->expect_ctrl[f][0] = c1;
		cd->expect_ctrl[f][1] = c2;
	} else {
		cd->expect_ctrl[f][0] = -1;

		if (c1 < 0x10) {
			if (FIELD_1 == f) {
				/* 47 CFR Section 15.119 (i)(1): "If the
				   non-printing character in the pair is
				   in the range 00h to 0Fh, that character
				   alone will be ignored and the second
				   character will be treated normally." */
				c1 = 0;
			} else if (0x0F == c1) {
				/* XDS packet terminator. */
				cd->in_xds[FIELD_2] = FALSE;
				goto finish;
			} else if (c1 >= 0x01) {
				/* XDS packet start or continuation.
				   EIA 608-B Section 7.7, 8.5: Also
				   interrupts a Text mode
				   transmission. */
				cd->in_xds[FIELD_2] = TRUE;
				goto finish;
			}
		}

		{
			struct cc_channel *ch;
			vbi_pgno ch_num;

			ch_num = cd->curr_ch_num[f];
			if (UNKNOWN_CC_CHANNEL == ch_num)
				goto finish;

			ch_num = ((ch_num - VBI_CAPTION_CC1) & 5) + f * 2;
			ch = &cd->channel[ch_num];

			all_successful &= cc_characters (cd, ch, c1);
			all_successful &= cc_characters (cd, ch, c2);

			if (cd->event_pending) {
				cc_display_event (cd, cd->event_pending, 0);
				cd->event_pending = NULL;
			}
		}
	}

 finish:
	cd->error_history = cd->error_history * 2 + all_successful;

	return all_successful;

 parity_error:
	cd->expect_ctrl[f][0] = -1;

	/* XXX Some networks stupidly transmit 0x0000 instead of
	   0x8080 as filler. Perhaps we shouldn't take that as a
	   serious parity error. */
	cd->error_history *= 2;

	return FALSE;
}

static void
cc_reset			(struct cc_decoder *	cd)
{
	unsigned int ch_num;

	assert (NULL != cd);

	if (option_debug & DEBUG_CC_DECODER) {
		fprintf (stderr, "%s\n", __FUNCTION__);
	}

	for (ch_num = 0; ch_num < MAX_CC_CHANNELS; ++ch_num) {
		struct cc_channel *ch;

		ch = &cd->channel[ch_num];

		if (ch_num <= 3) {
			ch->mode = CC_MODE_UNKNOWN;

			/* Something suitable for roll-up mode. */
			ch->curr_row = CC_LAST_ROW;
			ch->curr_column = CC_FIRST_COLUMN;
			ch->window_rows = 4;
		} else {
			ch->mode = CC_MODE_TEXT; /* invariable */

			/* EIA 608-B Section 7.4: "When Text Mode has
			   initially been selected and the specified
			   Text memory is empty, the cursor starts at
			   the topmost row, Column 1." */
			ch->curr_row = CC_FIRST_ROW;
			ch->curr_column = CC_FIRST_COLUMN;
			ch->window_rows = 0; /* n/a */
		}

		ch->displayed_buffer = 0;

		ch->last_pac = 0;

		CLEAR (ch->buffer);
		CLEAR (ch->dirty);

		cc_timestamp_reset (&ch->timestamp);
		cc_timestamp_reset (&ch->timestamp_c0);
	}

	cd->curr_ch_num[0] = UNKNOWN_CC_CHANNEL;
	cd->curr_ch_num[1] = UNKNOWN_CC_CHANNEL;

	memset (cd->expect_ctrl, -1, sizeof (cd->expect_ctrl));

	CLEAR (cd->in_xds);

	cd->event_pending = NULL;
}

static void
init_cc_decoder			(struct cc_decoder *	cd)
{
	cc_reset (cd);

	cd->error_history = 0;

	cc_timestamp_reset (&cd->timestamp);
}

/* Some code left over from ntsc-cc, to be replaced. */

const char * const ratings[] = {
	"(NOT RATED)","TV-Y","TV-Y7","TV-G",
	"TV-PG","TV-14","TV-MA","(NOT RATED)"};
const char * const modes[]={
	"current","future","channel","miscellaneous","public service",
	"reserved","invalid","invalid","invalid","invalid"};

static void
print_xds_info			(struct caption_recorder *cr,
				 unsigned int		mode,
				 unsigned int		type)
{
	const char *infoptr;

	if (!cr->info[0][mode][type].print)
		return;

	infoptr = cr->info[cr->field][mode][type].packet;

	switch ((mode << 8) + type) {
	case 0x0101:
		fprintf (cr->xds_fp,
			 "%sTIMECODE: %d/%02d %d:%02d%s",
			 cr->xds_info_prefix,
			 infoptr[3]&0x0f,infoptr[2]&0x1f,
			 infoptr[1]&0x1f,infoptr[0]&0x3f,
			 cr->xds_info_suffix);
	case 0x0102:
		if ((infoptr[1]&0x3f)>5)
			break;
		fprintf (cr->xds_fp,
			 "%s  LENGTH: %d:%02d:%02d of %d:%02d:00%s",
			 cr->xds_info_prefix,
			 infoptr[3]&0x3f,infoptr[2]&0x3f,
			 infoptr[4]&0x3f,infoptr[1]&0x3f,
			 infoptr[0]&0x3f,
			 cr->xds_info_suffix);
		break;
	case 0x0103:
		fprintf (cr->xds_fp,
			 "%s   TITLE: %s%s",
			 cr->xds_info_prefix,
			 infoptr,
			 cr->xds_info_suffix);
		break;
	case 0x0105:
		fprintf (cr->xds_fp,
			 "%s  RATING: %s (%d)",
			 cr->xds_info_prefix,
			 ratings[infoptr[0]&0x07],infoptr[0]);
		if ((infoptr[0]&0x07)>0) {
			if (infoptr[0]&0x20) fputs (" VIOLENCE", cr->xds_fp);
			if (infoptr[0]&0x10) fputs (" SEXUAL", cr->xds_fp);
			if (infoptr[0]&0x08) fputs (" LANGUAGE", cr->xds_fp);
		}
		fputs (cr->xds_info_suffix, cr->xds_fp);
		break;
	case 0x0501:
		fprintf (cr->xds_fp,
			 "%s NETWORK: %s%s",
			 cr->xds_info_prefix,
			 infoptr,
			 cr->xds_info_suffix);
		break;
	case 0x0502:
		fprintf (cr->xds_fp,
			 "%s    CALL: %s%s",
			 cr->xds_info_prefix,
			 infoptr,
			 cr->xds_info_suffix);
		break;
	case 0x0701:
		fprintf (cr->xds_fp,
			 "%sCUR.TIME: %d:%02d %d/%02d/%04d UTC%s",
			 cr->xds_info_prefix,
			 infoptr[1]&0x1F,infoptr[0]&0x3f,
			 infoptr[3]&0x0f,infoptr[2]&0x1f,
			 (infoptr[5]&0x3f)+1990,
			 cr->xds_info_suffix);
		break;
	case 0x0704: //timezone
		fprintf (cr->xds_fp,
			 "%sTIMEZONE: UTC-%d%s",
			 cr->xds_info_prefix,
			 infoptr[0]&0x1f,
			 cr->xds_info_suffix);
		break;
	case 0x0104: //program genere
		break;
	case 0x0110:
	case 0x0111:
	case 0x0112:
	case 0x0113:
	case 0x0114:
	case 0x0115:
	case 0x0116:
	case 0x0117:
		fprintf (cr->xds_fp,
			 "%s    DESC: %s%s",
			 cr->xds_info_prefix,
			 infoptr,
			 cr->xds_info_suffix);
		break;
	}

	fflush (cr->xds_fp);
}

static int XDSdecode(struct caption_recorder *cr, int data)
{
	static vbi_bool in_xds[2];
	int b1, b2, length;

	if (data == -1)
		return -1;
	
	b1 = data & 0x7F;
	b2 = (data>>8) & 0x7F;

	if (0 == b1) {
		/* Filler, discard. */
		return -1;
	}
	else if (b1 < 15) // start packet 
	{
		cr->mode = b1;
		cr->type = b2;
		cr->infochecksum = b1 + b2 + 15;
		if (cr->mode > 8 || cr->type > 20)
		{
//			printf("%% Unsupported mode %s(%d) [%d]\n",modes[(mode-1)>>1],mode,type);
			cr->mode=0; cr->type=0;
		}
		cr->infoptr = cr->newinfo[cr->field][cr->mode][cr->type];
		in_xds[cr->field] = TRUE;
	}
	else if (b1 == 15) // eof (next byte is checksum)
	{
#if 0 //debug
		if (mode == 0)
		{
			length=infoptr - newinfo[cr->field][0][0];
			infoptr[1]=0;
			printf("LEN: %d\n",length);
			for (y=0;y<length;y++)
				printf(" %03d",newinfo[cr->field][0][0][y]);
			printf(" --- %s\n",newinfo[cr->field][0][0]);
		}
#endif
		if (cr->mode == 0) return 0;
		if (b2 != 128-((cr->infochecksum%128)&0x7F)) return 0;

		length = cr->infoptr - cr->newinfo[cr->field][cr->mode][cr->type];

		//don't bug the user with repeated data
		//only parse it if it's different
		if (cr->info[cr->field][cr->mode][cr->type].length != length
		    || 0 != memcmp (cr->info[cr->field][cr->mode][cr->type].packet,
				    cr->newinfo[cr->field][cr->mode][cr->type],
				    length))
		{
			memcpy (cr->info[cr->field][cr->mode][cr->type].packet,
				cr->newinfo[cr->field][cr->mode][cr->type], 32);
			cr->info[cr->field][cr->mode][cr->type].packet[length] = 0;
			cr->info[cr->field][cr->mode][cr->type].length = length;
			if (0)
				fprintf (stderr, "XDS %d %d %d %d %d\n",
					 cr->field, cr->mode, cr->type, length,
					 cr->info[0][cr->mode][cr->type].print);
			print_xds_info (cr, cr->mode, cr->type);
		}
		cr->mode = 0; cr->type = 0;
		in_xds[cr->field] = FALSE;
	} else if (b1 <= 31) {
		/* Caption control code. */
		in_xds[cr->field] = FALSE;
	} else if (in_xds[cr->field]) {
		if (cr->infoptr >= &cr->newinfo[cr->field][cr->mode][cr->type][32]) {
			/* Bad packet. */
			cr->mode = 0;
			cr->type = 0;
			in_xds[cr->field] = 0;
		} else {
			cr->infoptr[0] = b1; cr->infoptr++;
			cr->infoptr[0] = b2; cr->infoptr++;
			cr->infochecksum += b1 + b2;
		}
	}
	return 0;
}

#if 0 /* to be replaced */

static int webtv_check(struct caption_recorder *cr, char * buf,int len)
{
	unsigned long   sum;
	unsigned long   nwords;
	unsigned short  csum=0;
	char temp[9];
	int nbytes=0;
	
	while (buf[0]!='<' && len > 6)  //search for the start
	{
		buf++; len--;
	}
	
	if (len == 6) //failure to find start
		return 0;
				
	
	while (nbytes+6 <= len)
	{
		//look for end of object checksum, it's enclosed in []'s and there shouldn't be any [' after
		if (buf[nbytes] == '[' && buf[nbytes+5] == ']' && buf[nbytes+6] != '[')
			break;
		else
			nbytes++;
	}
	if (nbytes+6>len) //failure to find end
		return 0;
	
	nwords = nbytes >> 1; sum = 0;

	//add up all two byte words
	while (nwords-- > 0) {
		sum += *buf++ << 8;
		sum += *buf++;
	}
	if (nbytes & 1) {
		sum += *buf << 8;
	}
	csum = (unsigned short)(sum >> 16);
	while(csum !=0) {
		sum = csum + (sum & 0xffff);
		csum = (unsigned short)(sum >> 16);
	}
	sprintf(temp,"%04X\n",(int)~sum&0xffff);
	buf++;
	if(!strncmp(buf,temp,4))
	{
		buf[5]=0;
		if (cr->cur_ch[cr->field] >= 0 && cr->cc_fp[cr->cur_ch[cr->field]]) {
		if (!cr->plain)
			fprintf(cr->cc_fp[cr->cur_ch[cr->field]], "\33[35mWEBTV: %s\33[0m\n",buf-nbytes-1);
		else
			fprintf(cr->cc_fp[cr->cur_ch[cr->field]], "WEBTV: %s\n",buf-nbytes-1);
		fflush (cr->cc_fp[cr->cur_ch[cr->field]]);
		}
	}
	return 0;
}

#endif /* 0 */

static void
xds_filter_option		(struct caption_recorder *cr,
				 const char *		optarg)
{
	const char *s;

	/* Attention: may be called repeatedly. */

	if (NULL == optarg
	    || 0 == strcasecmp (optarg, "all")) {
		unsigned int i;

		for (i = 0; i < (N_ELEMENTS (cr->info[0])
				 * N_ELEMENTS (cr->info[0][0])); ++i) {
			cr->info[0][0][i].print = TRUE;
		}

		return;
	}

	s = optarg;

	while (0 != *s) {
		char buf[16];
		unsigned int len;

		for (;;) {
			if (0 == *s)
				return;
			if (isalnum (*s))
				break;
			++s;
		}

		for (len = 0; len < N_ELEMENTS (buf) - 1; ++len) {
			if (!isalnum (*s))
				break;
			buf[len] = *s++;
		}

		buf[len] = 0;

		if (0 == strcasecmp (buf, "timecode")) {
			cr->info[0][1][1].print = TRUE;
		} else if (0 == strcasecmp (buf, "length")) {
			cr->info[0][1][2].print = TRUE;
		} else if (0 == strcasecmp (buf, "title")) {
			cr->info[0][1][3].print = TRUE;
		} else if (0 == strcasecmp (buf, "rating")) {
			cr->info[0][1][5].print = TRUE;
		} else if (0 == strcasecmp (buf, "network")) {
			cr->info[0][5][1].print = TRUE;
		} else if (0 == strcasecmp (buf, "call")) {
			cr->info[0][5][2].print = TRUE;
		} else if (0 == strcasecmp (buf, "time")) {
			cr->info[0][7][1].print = TRUE;
		} else if (0 == strcasecmp (buf, "timezone")) {
			cr->info[0][7][4].print = TRUE;
		} else if (0 == strcasecmp (buf, "desc")) {
			cr->info[0][1][0x10].print = TRUE;
			cr->info[0][1][0x11].print = TRUE;
			cr->info[0][1][0x12].print = TRUE;
			cr->info[0][1][0x13].print = TRUE;
			cr->info[0][1][0x14].print = TRUE;
			cr->info[0][1][0x15].print = TRUE;
			cr->info[0][1][0x16].print = TRUE;
			cr->info[0][1][0x17].print = TRUE;
		} else {
			fprintf (stderr, "Unknown XDS info '%s'\n", buf);
		}
	}
}

/* CEA 708-C Digital TV Closed Caption decoder. */

static const uint8_t
dtvcc_c0_length [4] = {
	1, 1, 2, 3
};

static const uint8_t
dtvcc_c1_length [32] = {
	/* 0x80 CW0 ... CW7 */ 1, 1, 1, 1,  1, 1, 1, 1,
	/* 0x88 CLW */ 2,
	/* 0x89 DSW */ 2,
	/* 0x8A HDW */ 2,
	/* 0x8B TGW */ 2,

	/* 0x8C DLW */ 2,
	/* 0x8D DLY */ 2,
	/* 0x8E DLC */ 1,
	/* 0x8F RST */ 1,

	/* 0x90 SPA */ 3,
	/* 0x91 SPC */ 4,
	/* 0x92 SPL */ 3,
	/* CEA 708-C Section 7.1.5.1: 0x93 ... 0x96 are
	   reserved one byte codes. */ 1, 1, 1, 1,
	/* 0x97 SWA */ 5,
	/* 0x98 DF0 ... DF7 */ 7, 7, 7, 7,  7, 7, 7, 7
};

static const uint16_t
dtvcc_g2 [96] = {
	/* Note Unicode defines no transparent spaces. */
	0x0020, /* 0x1020 Transparent space */
	0x00A0, /* 0x1021 Non-breaking transparent space */

	0,      /* 0x1022 reserved */
	0,
	0,
	0x2026, /* 0x1025 Horizontal ellipsis */
	0,
	0,
	0,
	0,
	0x0160, /* 0x102A S with caron */
	0,
	0x0152, /* 0x102C Ligature OE */
	0,
	0,
	0,

	/* CEA 708-C Section 7.1.8: "The character (0x30) is a solid
	   block which fills the entire character position with the
	   text foreground color." */
	0x2588, /* 0x1030 Full block */

	0x2018, /* 0x1031 Left single quotation mark */
	0x2019, /* 0x1032 Right single quotation mark */
	0x201C, /* 0x1033 Left double quotation mark */
	0x201D, /* 0x1034 Right double quotation mark */
	0,
	0,
	0,
	0x2122, /* 0x1039 Trademark sign */
	0x0161, /* 0x103A s with caron */
	0,
	0x0153, /* 0x103C Ligature oe */
	0x2120, /* 0x103D Service mark */
	0,
	0x0178, /* 0x103F Y with diaeresis */

	/* Code points 0x1040 ... 0x106F reserved. */
	0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
	0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
	0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,

	0,      /* 0x1070 reserved */
	0,
	0,
	0,
	0,
	0,
	0x215B, /* 0x1076 1/8 */
	0x215C, /* 0x1077 3/8 */
	0x215D, /* 0x1078 5/8 */
	0x215E, /* 0x1079 7/8 */
	0x2502, /* 0x107A Box drawings vertical */
	0x2510, /* 0x107B Box drawings down and left */
	0x2514, /* 0x107C Box drawings up and right */
	0x2500, /* 0x107D Box drawings horizontal */
	0x2518, /* 0x107E Box drawings up and left */
	0x250C  /* 0x107F Box drawings down and right */
};

static unsigned int
dtvcc_unicode			(unsigned int		c)
{
	if (unlikely (0 == (c & 0x60))) {
		/* C0, C1, C2, C3 */
		return 0;
	} else if (likely (c < 0x100)) {
		/* G0, G1 */
		if (unlikely (0x7F == c))
			return 0x266A; /* music note */
		else
			return c;
	} else if (c < 0x1080) {
		if (unlikely (c < 0x1020))
			return 0;
		else
			return dtvcc_g2[c - 0x1020];
	} else if (0x10A0 == c) {
		/* We map all G2/G3 characters which are not
		   representable in Unicode to private code U+E900
		   ... U+E9FF. */
		return 0xE9A0; /* caption icon */
	}

	return 0;
}

static void
dump_dtvcc_se			(FILE *			fp,
				 const uint8_t *	buf,
				 unsigned int		n_bytes)
{
	uint16_t ucs2_str[1];
	unsigned int se_length;
	unsigned int c;
	unsigned int i;

	if (0 == n_bytes)
		return;

	c = buf[0];
	if (0 != (c & 0x60)) {
		ucs2_str[0] = dtvcc_unicode (c);
		fprintf (fp, "G0/G1 0x%02X U+%04X '",
			 c, ucs2_str[0]);
		vbi_fputs_iconv_ucs2 (fp, locale_codeset,
				      ucs2_str, 1,
				      /* repl_char */ '?');
		fputs ("'\n", fp);
		return;
	} else if ((int8_t) c < 0) {
		static const char *mnemo [32] = {
			"CW0", "CW1", "CW2", "CW3",
			"CW4", "CW5", "CW6", "CW7",
			"CLW", "DSW", "HDW", "TGW",
			"DLW", "DLY", "DLC", "RST",
			"SPA", "SPC", "SPL", "93",
			"reserved", "reserved",
			"reserved", "SWA",
			"DF0", "DF1", "DF2", "DF3",
			"DF4", "DF5", "DF6", "DF7"
		};
		static const char *opacity_name [4] = {
			"Solid", "Flash", "Transl", "Transp"
		};
		static const char *edge_name [8] = {
			"None", "Raised", "Depressed",
			"Uniform", "ShadowL", "ShadowR",
			"INVALID", "INVALID"
		};

		fprintf (fp, "C1 0x%02X %s", c, mnemo[c & 31]);

		se_length = dtvcc_c1_length[c - 0x80];
		if (n_bytes < se_length) {
			fputs (" incomplete\n", fp);
			return;
		}

		switch (c) {
		case 0x80 ... 0x87: /* CWx */
		case 0x8E: /* DLC */
		case 0x8F: /* RST */
		case 0x93 ... 0x96: /* reserved */
			fputc ('\n', fp);
			return;

		case 0x88: /* CLW */
		case 0x89: /* DSW */
		case 0x8A: /* HDW */
		case 0x8B: /* TGW */
		case 0x8C: /* DLW */
			fputs (" 0b", fp);
			for (i = 0; i < 8; ++i) {
				unsigned int bit;

				bit = !!(buf[1] & (0x80 >> i));
				fputc ('0' + bit, fp);
			}
			fputc ('\n', fp);
			return;

		case 0x8D: /* DLY */
			fprintf (fp, " t=%u\n", buf[1]);
			return;

		case 0x90: /* SPA */
		{
			static const char *s_name [4] = {
				"Small", "Std", "Large", "INVALID"
			};
			static const char *fs_name [8] = {
				"Default", "MonoSerif", "PropSerif",
				"MonoSans", "PropSans", "Casual",
				"Cursive", "SmallCaps"
			};
			static const char *tt_name [16] = {
				"Dialog", "SourceID", "Device",
				"Dialog2", "Voiceover", "AudTransl",
				"SubTransl", "VoiceDescr", "Lyrics",
				"EffectDescr", "ScoreDescr",
				"Expletive", "INVALID", "INVALID",
				"INVALID", "NotDisplayable"
			};
			static const char *o_name [4] = {
				"Sub", "Normal", "Super", "INVALID"
			};

			fprintf (fp, " s=%s fs=%s tt=%s o=%s i=%u "
				 "u=%u et=%s\n",
				 s_name[buf[1] & 3],
				 fs_name[buf[2] & 7],
				 tt_name[(buf[1] >> 4) & 15],
				 o_name[(buf[1] >> 2) & 3],
				 !!(buf[2] & 0x80),
				 !!(buf[2] & 0x40),
				 edge_name[(buf[2] >> 3) & 7]);
			return;
		}

		case 0x91: /* SPC */
		{
			fprintf (fp, " fg=%u%u%u fo=%s bg=%u%u%u bo=%s "
				 "edge=%u%u%u\n",
				 (buf[1] >> 4) & 3, (buf[1] >> 2) & 3,
				 buf[1] & 3,
				 opacity_name[(buf[1] >> 6) & 3],
				 (buf[2] >> 4) & 3, (buf[2] >> 2) & 3,
				 buf[2] & 3,
				 opacity_name[(buf[2] >> 6) & 3],
				 (buf[3] >> 4) & 3, (buf[3] >> 2) & 3,
				 buf[3] & 3);
			return;
		}

		case 0x92: /* SPL */
			fprintf (fp, " r=%u c=%u\n",
				 buf[1] & 0x0F,
				 buf[2] & 0x3F);
			return;

		case 0x97: /* SWA */
		{
			static const char *j_name [4] = {
				"L", "R", "C", "F"
			};
			static const char *pd_sd_ed_name [4] = {
				"LR", "RL", "TB", "BT"
			};
			static const char *de_name [4] = {
				"Snap", "Fade", "Wipe", "INVALID"
			};

			fprintf (fp, " j=%s pd=%s sd=%s ww=%u de=%s "
				 "ed=%s es=%u fill=%u%u%u fo=%s "
				 "bt=%s border=%u%u%u\n",
				 j_name [buf[3] & 3],
				 pd_sd_ed_name [(buf[3] >> 4) & 3],
				 pd_sd_ed_name [(buf[3] >> 2) & 3],
				 !!(buf[3] & 0x40),
				 de_name [buf[4] & 3],
				 pd_sd_ed_name [(buf[4] >> 2) & 3],
				 (buf[4] >> 4) & 15,
				 (buf[1] >> 4) & 3, (buf[1] >> 2) & 3,
				 buf[1] & 3,
				 opacity_name[(buf[1] >> 6) & 3],
				 edge_name[(buf[2] >> 6) & 3],
				 (buf[2] >> 4) & 3, (buf[2] >> 2) & 3,
				 buf[2] & 3);
			return;
		}

		case 0x98 ... 0x9F: /* DFx */
		{
			static const char *ap_name [16] = {
				"TL", "TC", "TR",
				"CL", "C", "CR",
				"BL", "BC", "BR",
				"INVALID", "INVALID", "INVALID",
				"INVALID", "INVALID", "INVALID",
				"INVALID"
			};
			static const char *ws_name [8] = {
				"0", "PopUp", "TranspPopUp",
				"CentPopUp", "RollUp", "TranspRollUp",
				"CentRollUp", "Ticker"
			};
			static const char *ps_name [8] = {
				"0", "NTSC", "NTSCMonoSerif",
				"NTSCPropSerif", "NTSCMonoSans",
				"NTSCPropSans", "MonoSans",
				"PropSans"
			};

			fprintf (fp, " p=%u ap=%s rp=%u av=%u ah=%u "
				 "rc=%u cc=%u rl=%u cl=%u v=%u "
				 "ws=%s ps=%s\n",
				 buf[1] & 7,
				 ap_name[(buf[4] >> 4) & 15],
				 !!(buf[2] & 0x80),
				 buf[2] & 0x7F,
				 buf[3],
				 buf[4] & 0x0F,
				 buf[5] & 0x3F,
				 !!(buf[1] & 0x10),
				 !!(buf[1] & 0x08),
				 !!(buf[1] & 0x20),
				 ws_name [(buf[6] >> 3) & 7],
				 ps_name [buf[6] & 7]);
			return;
		}

		} /* switch */
	} else {
		static const char *mnemo [32] = {
			"NUL", "reserved", "reserved",
			"ETX", "reserved", "reserved",
			"reserved", "reserved",
			"BS", "reserved", "reserved",
			"reserved", "FF", "CR", "HCR",
			"reserved", "EXT1", "reserved",
			"reserved", "reserved",	"reserved",
			"reserved", "reserved", "reserved",
			"P16", "reserved", "reserved",
			"reserved", "reserved", "reserved",
			"reserved", "reserved"
		};

		/* C0 code. */

		fprintf (fp, "C0 0x%02X %s", c, mnemo [c]);

		se_length = dtvcc_c0_length[c >> 3];
		if (n_bytes < se_length) {
			fputs (" incomplete\n", fp);
			return;
		}

		if (0x10 != c) {
			if (se_length > 1)
				fprintf (fp, " 0x%02X", buf[1]);
			if (se_length > 2)
				fprintf (fp, " 0x%02X", buf[2]);
			fputc ('\n', fp);
			return;
		}
	}

	/* Two-byte codes. */

	c = buf[1];
	if (0 != (c & 0x60)) {
		ucs2_str[0] = dtvcc_unicode (0x1000 | c);
		fprintf (fp, "G2/G3 0x10%02X U+%04X '",
			 c, ucs2_str[0]);
		vbi_fputs_iconv_ucs2 (fp, locale_codeset,
				      ucs2_str, 1,
				      /* repl_char */ '?');
		fputs ("'\n", fp);
		return;
	} else if ((int8_t) c >= 0) {
		/* C2 code. */

		se_length = (c >> 3) + 2;
		fprintf (fp, "C2 0x10%02X reserved", c);
	} else if (c < 0x90) {
		/* C3 Fixed Length Commands. */

		se_length = (c >> 3) - 10;
		fprintf (fp, "C3 0x10%02X reserved", c);
	} else {
		/* C3 Variable Length Commands. */

		if (n_bytes < 3) {
			fprintf (fp, "C3 0x10%02X incomplete\n", c);
			return;
		}

		/* type [2], zero_bit [1],
		   length [5] */
		se_length = (buf[2] & 0x1F) + 3;

		fprintf (fp, "C3 0x10%02X%02X reserved",
			 c, buf[2]);
	}

	for (i = 2; i < se_length; ++i)
		fprintf (fp, " 0x%02X", buf[i]);

	fputc ('\n', fp);
}

static void
dump_dtvcc_buffer		(FILE *			fp,
				 struct dtvcc_window *	dw)
{
	unsigned int row;
	unsigned int column;

	for (row = 0; row < dw->row_count; ++row) {
		uint16_t ucs2_str[42];

		fprintf (fp, "%02u '", row);

		for (column = 0; column < dw->column_count; ++column) {
			unsigned int c;

			c = dw->buffer[row][column];
			if (0 == c) {
				ucs2_str[column] = 0x20;
				continue;
			}
			c = dtvcc_unicode (c);
			if (0 == c) {
				ucs2_str[column] = '?';
				continue;
			}
			ucs2_str[column] = c;
		}

		vbi_fputs_iconv_ucs2 (fp, locale_codeset,
				      ucs2_str, dw->column_count,
				      /* repl_char */ '?');
		fputs ("'\n", fp);
	}
}

static void
dtvcc_reset			(struct dtvcc_decoder *	cd);
static void
dtvcc_reset_service		(struct dtvcc_service *	ds);

static unsigned int
dtvcc_window_id			(struct dtvcc_service *	ds,
				 struct dtvcc_window *	dw)
{
	return dw - ds->window;
}

static unsigned int
dtvcc_service_num		(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds)
{
	return ds - dc->service + 1;
}

/* Up to eight windows can be visible at once, so which one displays
   the caption? Let's take a guess. */
static struct dtvcc_window *
dtvcc_caption_window		(struct dtvcc_service *	ds)
{
	struct dtvcc_window *dw;
	unsigned int max_priority;
	unsigned int window_id;

	dw = NULL;
	max_priority = 8;

	for (window_id = 0; window_id < 8; ++window_id) {
		if (0 == (ds->created & (1 << window_id)))
			continue;
		if (!ds->window[window_id].visible)
			continue;
		if (DIR_BOTTOM_TOP
		    != ds->window[window_id].style.scroll_direction)
			continue;
		if (ds->window[window_id].priority < max_priority) {
			dw = &ds->window[window_id];
			max_priority = ds->window[window_id].priority;
		}
	}

	return dw;
}

static void
dtvcc_stream_event		(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 struct dtvcc_window *	dw,
				 unsigned int		row)
{
	vbi_char text[48];
	vbi_char ac;
	unsigned int column;

	if (NULL == dw || dw != dtvcc_caption_window (ds))
		return;

	if (option_debug & DEBUG_DTVCC_STREAM_EVENT) {
		fprintf (stderr, "%s row=%u streamed=%08x\n",
			 __FUNCTION__, row, dw->streamed);
		dump_dtvcc_buffer (stderr, dw);
	}

	/* Note we only stream windows with scroll direction
	   upwards. */
	if (0 != (dw->streamed & (1 << row))
	    || !cc_timestamp_isset (&dw->timestamp_c0))
		return;

	dw->streamed |= 1 << row;

	for (column = 0; column < dw->column_count; ++column) {
		if (0 != dw->buffer[row][column])
			break;
	}

	/* Row contains only transparent spaces. */
	if (column >= dw->column_count)
		return;

	/* TO DO. */
	CLEAR (ac);
	ac.foreground = VBI_WHITE;
	ac.background = VBI_BLACK;
	ac.opacity = VBI_OPAQUE;

	for (column = 0; column < dw->column_count; ++column) {
		unsigned int c;

		c = dw->buffer[row][column];
		if (0 == c) {
			ac.unicode = 0x20;
		} else {
			ac.unicode = dtvcc_unicode (c);
			if (0 == ac.unicode) {
				ac.unicode = 0x20;
			}
		}
		text[column] = ac;
	}

	{
		struct program *pr;

		pr = PARENT (dc, struct program, cr.dtvcc);
		cr_new_line (&pr->cr, &dw->timestamp_c0,
			     /* channel */ dtvcc_service_num (dc, ds) + 8,
			     text, /* length */ dw->column_count);
	}

	cc_timestamp_reset (&dw->timestamp_c0);
}

static vbi_bool
dtvcc_put_char			(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 unsigned int		c)
{
	struct dtvcc_window *dw;
	unsigned int row;
	unsigned int column;

	dc = dc; /* unused */

	dw = ds->curr_window;
	if (NULL == dw) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	row = dw->curr_row;
	column = dw->curr_column;

	/* FIXME how should we handle TEXT_TAG_NOT_DISPLAYABLE? */

	dw->buffer[row][column] = c;

	if (option_debug & DEBUG_DTVCC_PUT_CHAR) {
		fprintf (stderr, "%s row=%u/%u column=%u/%u\n",
			 __FUNCTION__,
			 row, dw->row_count,
			 column, dw->column_count);
		dump_dtvcc_buffer (stderr, dw);
	}

	switch (dw->style.print_direction) {
	case DIR_LEFT_RIGHT:
		dw->streamed &= ~(1 << row);
		if (!cc_timestamp_isset (&dw->timestamp_c0))
			dw->timestamp_c0 = ds->timestamp;
		if (++column >= dw->column_count)
			return TRUE;
		break;

	case DIR_RIGHT_LEFT:
		dw->streamed &= ~(1 << row);
		if (!cc_timestamp_isset (&dw->timestamp_c0))
			dw->timestamp_c0 = ds->timestamp;
		if (column-- <= 0)
			return TRUE;
		break;

	case DIR_TOP_BOTTOM:
		dw->streamed &= ~(1 << column);
		if (!cc_timestamp_isset (&dw->timestamp_c0))
			dw->timestamp_c0 = ds->timestamp;
		if (++row >= dw->row_count)
			return TRUE;
		break;

	case DIR_BOTTOM_TOP:
		dw->streamed &= ~(1 << column);
		if (!cc_timestamp_isset (&dw->timestamp_c0))
			dw->timestamp_c0 = ds->timestamp;
		if (row-- <= 0)
			return TRUE;
		break;
	}

	dw->curr_row = row;
	dw->curr_column = column;

	return TRUE;
}

static vbi_bool
dtvcc_set_pen_location		(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 const uint8_t *	buf)
{
	struct dtvcc_window *dw;
	unsigned int row;
	unsigned int column;

	dw = ds->curr_window;
	if (NULL == dw) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	row = buf[1];
	/* We check the top four zero bits. */
	if (row >= 16) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	column = buf[2];
	/* We also check the top two zero bits. */
	if (column >= 42) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	if (row > dw->row_count)
		row = dw->row_count - 1;
	if (column > dw->column_count)
		column = dw->column_count - 1;

	if (row != dw->curr_row) {
		dtvcc_stream_event (dc, ds, dw, dw->curr_row);
	}

	/* FIXME there's more. */
	dw->curr_row = row;
	dw->curr_column = column;

	return TRUE;
}

static vbi_bool
dtvcc_set_pen_color		(struct dtvcc_service *	ds,
				 const uint8_t *	buf)
{
	struct dtvcc_window *dw;
	unsigned int c;

	dw = ds->curr_window;
	if (NULL == dw) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	c = buf[3];
	if (0 != (c & 0xC0)) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	dw->curr_pen.style.edge_color = c;
	c = buf[1];
	dw->curr_pen.style.fg_opacity = c >> 6;
	dw->curr_pen.style.fg_color = c & 0x3F;
	c = buf[2];
	dw->curr_pen.style.bg_opacity = c >> 6;
	dw->curr_pen.style.bg_color = c & 0x3F;

	return TRUE;
}

static vbi_bool
dtvcc_set_pen_attributes	(struct dtvcc_service *	ds,
				 const uint8_t *	buf)
{
	struct dtvcc_window *dw;
	unsigned int c;
	enum pen_size pen_size;
	enum offset offset;
	enum edge edge_type;

	dw = ds->curr_window;
	if (NULL == dw) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	c = buf[1];
	offset = (c >> 2) & 3;
	pen_size = c & 3;
	if ((offset | pen_size) >= 3) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	c = buf[2];
	edge_type = (c >> 3) & 7;
	if (edge_type >= 6) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	c = buf[1];
	dw->curr_pen.text_tag = c >> 4;
	dw->curr_pen.style.offset = offset;
	dw->curr_pen.style.pen_size = pen_size;
	c = buf[2];
	dw->curr_pen.style.italics = c >> 7;
	dw->curr_pen.style.underline = (c >> 6) & 1;
	dw->curr_pen.style.edge_type = edge_type;
	dw->curr_pen.style.font_style = c & 7;

	return TRUE;
}

static vbi_bool
dtvcc_set_window_attributes	(struct dtvcc_service *	ds,
				 const uint8_t *	buf)
{
	struct dtvcc_window *dw;
	unsigned int c;
	enum edge border_type;
	enum display_effect display_effect;

	dw = ds->curr_window;
	if (NULL == dw)
		return FALSE;

	c = buf[2];
	border_type = ((buf[3] >> 5) & 0x04) | (c >> 6);
	if (border_type >= 6)
		return FALSE;

	c = buf[4];
	display_effect = c & 3;
	if (display_effect >= 3)
		return FALSE;

	c = buf[1];
	dw->style.fill_opacity = c >> 6;
	dw->style.fill_color = c & 0x3F;
	c = buf[2];
	dw->style.border_type = border_type;
	dw->style.border_color = c & 0x3F;
	c = buf[3];
	dw->style.wordwrap = (c >> 6) & 1;
	dw->style.print_direction = (c >> 4) & 3;
	dw->style.scroll_direction = (c >> 2) & 3;
	dw->style.justify = c & 3;
	c = buf[4];
	dw->style.effect_speed = c >> 4;
	dw->style.effect_direction = (c >> 2) & 3;
	dw->style.display_effect = display_effect;

	return TRUE;
}

static vbi_bool
dtvcc_clear_windows		(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 dtvcc_window_map	window_map)
{
	unsigned int i;

	window_map &= ds->created;

	for (i = 0; i < 8; ++i) {
		struct dtvcc_window *dw;

		if (0 == (window_map & (1 << i)))
			continue;

		dw = &ds->window[i];

		dtvcc_stream_event (dc, ds, dw, dw->curr_row);

		memset (dw->buffer, 0, sizeof (dw->buffer));

		dw->streamed = 0;

		/* FIXME CEA 708-C Section 7.1.4 (Form Feed)
		   and 8.10.5.3 confuse me. */
		if (0) {
			dw->curr_column = 0;
			dw->curr_row = 0;
		}
	}

	return TRUE;
}

static vbi_bool
dtvcc_define_window		(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 uint8_t *		buf)
{
	static const struct dtvcc_window_style window_styles [7] = {
		{
			JUSTIFY_LEFT, DIR_LEFT_RIGHT, DIR_BOTTOM_TOP,
			FALSE, DISPLAY_EFFECT_SNAP, 0, 0, 0,
			OPACITY_SOLID, EDGE_NONE, 0
		}, {
			JUSTIFY_LEFT, DIR_LEFT_RIGHT, DIR_BOTTOM_TOP,
			FALSE, DISPLAY_EFFECT_SNAP, 0, 0, 0,
			OPACITY_TRANSPARENT, EDGE_NONE, 0
		}, {
			JUSTIFY_CENTER, DIR_LEFT_RIGHT, DIR_BOTTOM_TOP,
			FALSE, DISPLAY_EFFECT_SNAP, 0, 0, 0,
			OPACITY_SOLID, EDGE_NONE, 0
		}, {
			JUSTIFY_LEFT, DIR_LEFT_RIGHT, DIR_BOTTOM_TOP,
			TRUE, DISPLAY_EFFECT_SNAP, 0, 0, 0,
			OPACITY_SOLID, EDGE_NONE, 0
		}, {
			JUSTIFY_LEFT, DIR_LEFT_RIGHT, DIR_BOTTOM_TOP,
			TRUE, DISPLAY_EFFECT_SNAP, 0, 0, 0,
			OPACITY_TRANSPARENT, EDGE_NONE, 0
		}, {
			JUSTIFY_CENTER, DIR_LEFT_RIGHT, DIR_BOTTOM_TOP,
			TRUE, DISPLAY_EFFECT_SNAP, 0, 0, 0,
			OPACITY_SOLID, EDGE_NONE, 0
		}, {
			JUSTIFY_LEFT, DIR_TOP_BOTTOM, DIR_RIGHT_LEFT,
			FALSE, DISPLAY_EFFECT_SNAP, 0, 0, 0,
			OPACITY_SOLID, EDGE_NONE, 0
		}
	};
	static const struct dtvcc_pen_style pen_styles [7] = {
		{
			PEN_SIZE_STANDARD, 0, OFFSET_NORMAL, FALSE,
			FALSE, EDGE_NONE, 0x3F, OPACITY_SOLID,
			0x00, OPACITY_SOLID, 0
		}, {
			PEN_SIZE_STANDARD, 1, OFFSET_NORMAL, FALSE,
			FALSE, EDGE_NONE, 0x3F, OPACITY_SOLID,
			0x00, OPACITY_SOLID, 0
		}, {
			PEN_SIZE_STANDARD, 2, OFFSET_NORMAL, FALSE,
			FALSE, EDGE_NONE, 0x3F, OPACITY_SOLID,
			0x00, OPACITY_SOLID, 0
		}, {
			PEN_SIZE_STANDARD, 3, OFFSET_NORMAL, FALSE,
			FALSE, EDGE_NONE, 0x3F, OPACITY_SOLID,
			0x00, OPACITY_SOLID, 0
		}, {
			PEN_SIZE_STANDARD, 4, OFFSET_NORMAL, FALSE,
			FALSE, EDGE_NONE, 0x3F, OPACITY_SOLID,
			0x00, OPACITY_SOLID, 0
		}, {
			PEN_SIZE_STANDARD, 3, OFFSET_NORMAL, FALSE,
			FALSE, EDGE_UNIFORM, 0x3F, OPACITY_SOLID,
			0, OPACITY_TRANSPARENT, 0x00
		}, {
			PEN_SIZE_STANDARD, 4, OFFSET_NORMAL, FALSE,
			FALSE, EDGE_UNIFORM, 0x3F, OPACITY_SOLID,
			0, OPACITY_TRANSPARENT, 0x00
		}
	};
	struct dtvcc_window *dw;
	dtvcc_window_map window_map;
	vbi_bool anchor_relative;
	unsigned int anchor_vertical;
	unsigned int anchor_horizontal;
	unsigned int anchor_point;
	unsigned int column_count_m1;
	unsigned int window_id;
	unsigned int window_style_id;
	unsigned int pen_style_id;
	unsigned int c;

	if (0 != ((buf[1] | buf[6]) & 0xC0)) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	c = buf[2];
	anchor_relative = (c >> 7) & 1;
	anchor_vertical = c & 0x7F;
	anchor_horizontal = buf[3];
	if (0 == anchor_relative) {
		if (unlikely (anchor_vertical >= 75
			      || anchor_horizontal >= 210)) {
			ds->error_line = __LINE__;
			return FALSE;
		}
	} else {
		if (unlikely (anchor_vertical >= 100
			      || anchor_horizontal >= 100)) {
			ds->error_line = __LINE__;
			return FALSE;
		}
	}

	c = buf[4];
	anchor_point = c >> 4;
	if (unlikely (anchor_point >= 9)) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	column_count_m1 = buf[5];
	/* We also check the top two zero bits. */
	if (unlikely (column_count_m1 >= 41)) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	window_id = buf[0] & 7;
	dw = &ds->window[window_id];
	window_map = 1 << window_id;

	ds->curr_window = dw;

	c = buf[1];
	dw->visible = (c >> 5) & 1;
	dw->row_lock = (c >> 4) & 1;
	dw->column_lock = (c >> 4) & 1;
	dw->priority = c & 7;

	dw->anchor_relative = anchor_relative;
	dw->anchor_vertical = anchor_vertical;
	dw->anchor_horizontal = anchor_horizontal;
	dw->anchor_point = anchor_point;

	c = buf[4];
	dw->row_count = (c & 15) + 1;
	dw->column_count = column_count_m1 + 1;

	c = buf[6];
	window_style_id = (c >> 3) & 7;
	pen_style_id = c & 7;

	if (window_style_id > 0) {
		dw->style = window_styles[window_style_id];
	} else if (0 == (ds->created & window_map)) {
		dw->style = window_styles[1];
	}

	if (pen_style_id > 0) {
		dw->curr_pen.style = pen_styles[pen_style_id];
	} else if (0 == (ds->created & window_map)) {
		dw->curr_pen.style = pen_styles[1];
	}

	if (0 != (ds->created & window_map))
		return TRUE;

	/* Has to be something, no? */
	dw->curr_pen.text_tag = TEXT_TAG_NOT_DISPLAYABLE;

	dw->curr_column = 0;
	dw->curr_row = 0;

	dw->streamed = 0;

	cc_timestamp_reset (&dw->timestamp_c0);

	ds->created |= window_map;

	return dtvcc_clear_windows (dc, ds, window_map);
}

static vbi_bool
dtvcc_display_windows		(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 unsigned int		c,
				 dtvcc_window_map	window_map)
{
	unsigned int i;

	window_map &= ds->created;

	for (i = 0; i < 8; ++i) {
		struct dtvcc_window *dw;
		vbi_bool was_visible;

		if (0 == (window_map & (1 << i)))
			continue;

		dw = &ds->window[i];
		was_visible = dw->visible;

		switch (c) {
		case 0x89: /* DSW DisplayWindows */
			dw->visible = TRUE;
			break;

		case 0x8A: /* HDW HideWindows */
			dw->visible = FALSE;
			break;

		case 0x8B: /* TGW ToggleWindows */
			dw->visible = was_visible ^ TRUE;
			break;
		}

		if (!was_visible) {
			unsigned int row;

			dw->timestamp_c0 = ds->timestamp;
			for (row = 0; row < dw->row_count; ++row) {
				dtvcc_stream_event (dc, ds, dw, row);
			}
		}
	}

	return TRUE;
}

static vbi_bool
dtvcc_carriage_return		(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds)
{
	struct dtvcc_window *dw;
	unsigned int row;
	unsigned int column;

	dw = ds->curr_window;
	if (NULL == dw) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	dtvcc_stream_event (dc, ds, dw, dw->curr_row);

	row = dw->curr_row;
	column = dw->curr_column;

	switch (dw->style.scroll_direction) {
	case DIR_LEFT_RIGHT:
		dw->curr_row = 0;
		if (column > 0) {
			dw->curr_column = column - 1;
			break;
		}
		dw->streamed = (dw->streamed << 1)
			& ~(1 << dw->column_count);
		for (row = 0; row < dw->row_count; ++row) {
			for (column = dw->column_count - 1;
			     column > 0; --column) {
				dw->buffer[row][column] =
					dw->buffer[row][column - 1];
			}
			dw->buffer[row][column] = 0;
		}
		break;

	case DIR_RIGHT_LEFT:
		dw->curr_row = 0;
		if (column + 1 < dw->row_count) {
			dw->curr_column = column + 1;
			break;
		}
		dw->streamed >>= 1;
		for (row = 0; row < dw->row_count; ++row) {
			for (column = 0;
			     column < dw->column_count - 1; ++column) {
				dw->buffer[row][column] =
					dw->buffer[row][column + 1];
			}
			dw->buffer[row][column] = 0;
		}
		break;

	case DIR_TOP_BOTTOM:
		dw->curr_column = 0;
		if (row > 0) {
			dw->curr_row = row - 1;
			break;
		}
		dw->streamed = (dw->streamed << 1)
			& ~(1 << dw->row_count);
		memmove (&dw->buffer[1], &dw->buffer[0],
			 sizeof (dw->buffer[0]) * (dw->row_count - 1));
		memset (&dw->buffer[0], 0, sizeof (dw->buffer[0]));
		break;

	case DIR_BOTTOM_TOP:
		dw->curr_column = 0;
		if (row + 1 < dw->row_count) {
			dw->curr_row = row + 1;
			break;
		}
		dw->streamed >>= 1;
		memmove (&dw->buffer[0], &dw->buffer[1],
			 sizeof (dw->buffer[0]) * (dw->row_count - 1));
		memset (&dw->buffer[row], 0, sizeof (dw->buffer[0]));
		break;
	}

	return TRUE;
}

static vbi_bool
dtvcc_form_feed			(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds)
{
	struct dtvcc_window *dw;
	dtvcc_window_map window_map;

	dw = ds->curr_window;
	if (NULL == dw) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	window_map = 1 << dtvcc_window_id (ds, dw);

	if (!dtvcc_clear_windows (dc, ds, window_map))
		return FALSE;

	dw->curr_row = 0;
	dw->curr_column = 0;

	return TRUE;
}

static vbi_bool
dtvcc_backspace			(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds)
{
	struct dtvcc_window *dw;
	unsigned int row;
	unsigned int column;
	unsigned int mask;

	dc = dc; /* unused */

	dw = ds->curr_window;
	if (NULL == dw) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	row = dw->curr_row;
	column = dw->curr_column;

	switch (dw->style.print_direction) {
	case DIR_LEFT_RIGHT:
		mask = 1 << row;
		if (column-- <= 0)
			return TRUE;
		break;

	case DIR_RIGHT_LEFT:
		mask = 1 << row;
		if (++column >= dw->column_count)
			return TRUE;
		break;

	case DIR_TOP_BOTTOM:
		mask = 1 << column;
		if (row-- <= 0)
			return TRUE;
		break;

	case DIR_BOTTOM_TOP:
		mask = 1 << column;
		if (++row >= dw->row_count)
			return TRUE;
		break;
	}

	if (0 != dw->buffer[row][column]) {
		dw->streamed &= ~mask;
		dw->buffer[row][column] = 0;
	}

	dw->curr_row = row;
	dw->curr_column = column;

	return TRUE;
}

static vbi_bool
dtvcc_hor_carriage_return	(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds)
{
	struct dtvcc_window *dw;
	unsigned int row;
	unsigned int column;
	unsigned int mask;

	dc = dc; /* unused */

	dw = ds->curr_window;
	if (NULL == dw) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	row = dw->curr_row;
	column = dw->curr_column;

	switch (dw->style.print_direction) {
	case DIR_LEFT_RIGHT:
	case DIR_RIGHT_LEFT:
		mask = 1 << row;
		memset (&dw->buffer[row][0], 0,
			sizeof (dw->buffer[0]));
		if (DIR_LEFT_RIGHT == dw->style.print_direction)
			dw->curr_column = 0;
		else
			dw->curr_column = dw->column_count - 1;
		break;

	case DIR_TOP_BOTTOM:
	case DIR_BOTTOM_TOP:
		mask = 1 << column;
		for (row = 0; row < dw->column_count; ++row)
			dw->buffer[row][column] = 0;
		if (DIR_TOP_BOTTOM == dw->style.print_direction)
			dw->curr_row = 0;
		else
			dw->curr_row = dw->row_count - 1;
		break;
	}

	dw->streamed &= ~mask;

	return TRUE;
}

static vbi_bool
dtvcc_delete_windows		(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 dtvcc_window_map	window_map)
{
	struct dtvcc_window *dw;

	dw = ds->curr_window;
	if (NULL != dw) {
		unsigned int window_id;
		
		window_id = dtvcc_window_id (ds, dw);
		if (0 != (window_map & (1 << window_id))) {
			dtvcc_stream_event (dc, ds, dw, dw->curr_row);
			ds->curr_window = NULL;
		}
	}

	ds->created &= ~window_map;

	return TRUE;
}

static vbi_bool
dtvcc_command			(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 unsigned int *		se_length,
				 uint8_t *		buf,
				 unsigned int		n_bytes)
{
	unsigned int c;
	unsigned int window_id;

	c = buf[0];
	if ((int8_t) c < 0) {
		*se_length = dtvcc_c1_length[c - 0x80];
	} else {
		*se_length = dtvcc_c0_length[c >> 3];
	}

	if (*se_length > n_bytes) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	switch (c) {
	case 0x08: /* BS Backspace */
		return dtvcc_backspace (dc, ds);

	case 0x0C: /* FF Form Feed */
		return dtvcc_form_feed (dc, ds);

	case 0x0D: /* CR Carriage Return */
		return dtvcc_carriage_return (dc, ds);

	case 0x0E: /* HCR Horizontal Carriage Return */
		return dtvcc_hor_carriage_return (dc, ds);

	case 0x80 ... 0x87: /* CWx SetCurrentWindow */
		window_id = c & 7;
		if (0 == (ds->created & (1 << window_id))) {
			ds->error_line = __LINE__;
			return FALSE;
		}
		ds->curr_window = &ds->window[window_id];
		return TRUE;

	case 0x88: /* CLW ClearWindows */
		return dtvcc_clear_windows (dc, ds, buf[1]);

	case 0x89: /* DSW DisplayWindows */
		return dtvcc_display_windows (dc, ds, c, buf[1]);

	case 0x8A: /* HDW HideWindows */
		return dtvcc_display_windows (dc, ds, c, buf[1]);

	case 0x8B: /* TGW ToggleWindows */
		return dtvcc_display_windows (dc, ds, c, buf[1]);

	case 0x8C: /* DLW DeleteWindows */
		return dtvcc_delete_windows (dc, ds, buf[1]);

	case 0x8F: /* RST Reset */
		dtvcc_reset_service (ds);
		return TRUE;

	case 0x90: /* SPA SetPenAttributes */
		return dtvcc_set_pen_attributes (ds, buf);

	case 0x91: /* SPC SetPenColor */
		return dtvcc_set_pen_color (ds, buf);

	case 0x92: /* SPL SetPenLocation */
		return dtvcc_set_pen_location (dc, ds, buf);

	case 0x97: /* SWA SetWindowAttributes */
		return dtvcc_set_window_attributes (ds, buf);

	case 0x98 ... 0x9F: /* DFx DefineWindow */
		return dtvcc_define_window (dc, ds, buf);

	default:
		return TRUE;
	}
}

static vbi_bool
dtvcc_decode_se			(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 unsigned int *		se_length,
				 uint8_t *		buf,
				 unsigned int		n_bytes)
{
	unsigned int c;

	c = buf[0];
	if (likely (0 != (c & 0x60))) {
		/* G0/G1 character. */
		*se_length = 1;
		return dtvcc_put_char (dc, ds, c);
	}

	if (0x10 != c) {
		/* C0/C1 control code. */
		return dtvcc_command (dc, ds, se_length,
				      buf, n_bytes);
	}

	if (unlikely (n_bytes < 2)) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	c = buf[1];
	if (likely (0 != (c & 0x60))) {
		/* G2/G3 character. */
		*se_length = 2;
		return dtvcc_put_char (dc, ds, 0x1000 | c);
	}

	/* CEA 708-C defines no C2 or C3 commands. */

	if ((int8_t) c >= 0) {
		/* C2 code. */
		*se_length = (c >> 3) + 2;
	} else if (c < 0x90) {
		/* C3 Fixed Length Commands. */
		*se_length = (c >> 3) - 10;
	} else {
		/* C3 Variable Length Commands. */

		if (unlikely (n_bytes < 3)) {
			ds->error_line = __LINE__;
			return FALSE;
		}

		/* type [2], zero_bit [1],
		   length [5] */
		*se_length = (buf[2] & 0x1F) + 3;
	}

	if (unlikely (n_bytes < *se_length)) {
		ds->error_line = __LINE__;
		return FALSE;
	}

	return TRUE;
}

static vbi_bool
dtvcc_decode_syntactic_elements	(struct dtvcc_decoder *	dc,
				 struct dtvcc_service *	ds,
				 uint8_t *		buf,
				 unsigned int		n_bytes)
{
	ds->timestamp = dc->timestamp;

	while (n_bytes > 0) {
		unsigned int se_length;

		if (option_debug & DEBUG_DTVCC_SE) {
			fprintf (stderr, "S%u ",
				 dtvcc_service_num (dc, ds));
			dump_dtvcc_se (stderr, buf, n_bytes);
		}

		if (0x8D /* DLY */ == *buf
		    || 0x8E /* DLC */ == *buf) {
			/* FIXME ignored for now. */
			++buf;
			--n_bytes;
			continue;
		}

		if (!dtvcc_decode_se (dc, ds,
				      &se_length,
				      buf, n_bytes)) {
			return FALSE;
		}

		buf += se_length;
		n_bytes -= se_length;
	}

	return TRUE;
}

static void
dtvcc_decode_packet		(struct dtvcc_decoder *	dc,
				 const struct timeval *	tv,
				 int64_t		pts)
{
	unsigned int packet_size_code;
	unsigned int packet_size;
	unsigned int i;

	dc->timestamp.sys = *tv;
	dc->timestamp.pts = pts;

	/* Packet Layer. */

	/* sequence_number [2], packet_size_code [6],
	   packet_data [n * 8] */

	if (dc->next_sequence_number >= 0
	    && 0 != ((dc->packet[0] ^ dc->next_sequence_number) & 0xC0)) {
		struct program *pr;

		pr = PARENT (dc, struct program, cr.dtvcc);
		log (4, "Station %u DTVCC packet lost.\n",
		     station_num (pr));
		dtvcc_reset (dc);
		return;
	}

	dc->next_sequence_number = dc->packet[0] + 0x40;

	packet_size_code = dc->packet[0] & 0x3F;
	packet_size = 128;
	if (packet_size_code > 0)
		packet_size = packet_size_code * 2;

	if (option_debug & DEBUG_DTVCC_PACKET) {
		unsigned int sequence_number;

		sequence_number = (dc->packet[0] >> 6) & 3;
		fprintf (stderr, "DTVCC packet packet_size=%u "
			 "(transmitted %u), sequence_number %u\n",
			 packet_size, dc->packet_size,
			 sequence_number);
		dump (stderr, dc->packet, dc->packet_size);
	}

	/* CEA 708-C Section 5: Apparently packet_size need not be
	   equal to the actually transmitted amount of data. */
	if (packet_size > dc->packet_size) {
		struct program *pr;

		pr = PARENT (dc, struct program, cr.dtvcc);
		log (4, "Station %u DTVCC packet incomplete (%u/%u).\n",
		     station_num (pr),
		     dc->packet_size, packet_size);
		dtvcc_reset (dc);
		return;
	}

	/* Service Layer. */

	/* CEA 708-C Section 6.2.5, 6.3: Service Blocks and syntactic
	   elements must not cross Caption Channel Packet
	   boundaries. */

	for (i = 1; i < packet_size;) {
		unsigned int service_number;
		unsigned int block_size;
		unsigned int header_size;
		unsigned int c;

		header_size = 1;

		/* service_number [3], block_size [5],
		   (null_fill [2], extended_service_number [6]),
		   (Block_data [n * 8]) */

		c = dc->packet[i]; 
		service_number = (c & 0xE0) >> 5;

		/* CEA 708-C Section 6.3: Ignore block_size if
		   service_number is zero. */
		if (0 == service_number) {
			/* NULL Service Block Header, no more data in
			   this Caption Channel Packet. */
			break;
		}

		/* CEA 708-C Section 6.2.1: Apparently block_size zero
		   is valid, although properly it should only occur in
		   NULL Service Block Headers. */
		block_size = c & 0x1F;

		if (7 == service_number) {
			if (i + 1 > packet_size)
				goto service_block_incomplete;

			header_size = 2;
			c = dc->packet[i + 1];

			/* We also check the null_fill bits. */
			if (c < 7 || c > 63)
				goto invalid_service_block;

			service_number = c;
		}

		if (i + header_size + block_size > packet_size)
			goto service_block_incomplete;

		if (service_number <= 2) {
			struct dtvcc_service *ds;
			unsigned int in;

			ds = &dc->service[service_number - 1];
			in = ds->service_data_in;
			memcpy (ds->service_data + in,
				dc->packet + i + header_size,
				block_size);
			ds->service_data_in = in + block_size;
		}

		i += header_size + block_size;
	}

	for (i = 0; i < 2; ++i) {
		struct dtvcc_service *ds;
		struct program *pr;
		vbi_bool success;

		ds = &dc->service[i];
		if (0 == ds->service_data_in)
			continue;

		success = dtvcc_decode_syntactic_elements
			(dc, ds, ds->service_data, ds->service_data_in);

		ds->service_data_in = 0;

		if (success)
			continue;

		pr = PARENT (dc, struct program, cr.dtvcc);
		log (4, "Station %u DTVCC invalid "
		     "syntactic element (%u).\n",
		     station_num (pr), ds->error_line);

		if (option_debug & DEBUG_DTVCC_PACKET) {
			fprintf (stderr, "Packet (%d/%d):\n",
				 packet_size, dc->packet_size);
			dump (stderr, dc->packet, packet_size);
			fprintf (stderr, "Service Data:\n");
			dump (stderr, ds->service_data,
			      ds->service_data_in);
		}

		dtvcc_reset_service (ds);
	}

	return;

 invalid_service_block:
	{
		struct program *pr;

		pr = PARENT (dc, struct program, cr.dtvcc);
		log (4, "Station %u DTVCC invalid "
		     "service block (%u).\n",
		     station_num (pr), i);
		if (option_debug & DEBUG_DTVCC_PACKET) {
			fprintf (stderr, "Packet (%d/%d):\n",
				 packet_size, dc->packet_size);
			dump (stderr, dc->packet, packet_size);
		}
		dtvcc_reset (dc);
		return;
	}

 service_block_incomplete:
	{
		struct program *pr;

		pr = PARENT (dc, struct program, cr.dtvcc);
		log (4, "Station %u DTVCC incomplete "
		     "service block (%u).\n",
		     station_num (pr), i);
		if (option_debug & DEBUG_DTVCC_PACKET) {
			fprintf (stderr, "Packet (%d/%d):\n",
				 packet_size, dc->packet_size);
			dump (stderr, dc->packet, packet_size);
		}
		dtvcc_reset (dc);
		return;
	}

}

static void
dtvcc_reset_service		(struct dtvcc_service *	ds)
{
	ds->curr_window = NULL;
	ds->created = 0;

	cc_timestamp_reset (&ds->timestamp);
}

static void
dtvcc_reset			(struct dtvcc_decoder *	dc)
{
	dtvcc_reset_service (&dc->service[0]);
	dtvcc_reset_service (&dc->service[1]);

	dc->packet_size = 0;
	dc->next_sequence_number = -1;
}

static void
init_dtvcc_decoder		(struct dtvcc_decoder *	dc)
{
	dtvcc_reset (dc);

	cc_timestamp_reset (&dc->timestamp);
}

/* ATSC A/53 Part 4:2007 Closed Caption Data decoder */

static void
dump_cc_data_pair		(FILE *			fp,
				 unsigned int		index,
				 const uint8_t		buf[3])
{
	unsigned int one_bit;
	unsigned int reserved;
	unsigned int cc_valid;
	enum cc_type cc_type;
	unsigned int cc_data_1;
	unsigned int cc_data_2;

	/* Was marker_bits: "11111". */
	one_bit = (buf[0] >> 7) & 1;
	reserved = (buf[0] >> 3) & 15;

	cc_valid = (buf[0] >> 2) & 1;
	cc_type = (enum cc_type)(buf[0] & 3);
	cc_data_1 = buf[1];
	cc_data_2 = buf[2];

	fprintf (fp, "  %2u '1F'=%u%X%s valid=%u type=%s "
		 "%02x %02x '%c%c'\n",
		 index, one_bit, reserved,
		 (1 != one_bit || 0xF != reserved) ? "*" : "",
		 cc_valid, cc_type_name (cc_type),
		 cc_data_1, cc_data_2,
		 printable (cc_data_1), printable (cc_data_2));
}

static void
dump_cc_data			(FILE *			fp,
				 const uint8_t *	buf,
				 unsigned int		n_bytes,
				 int64_t		pts,
				 int64_t		last_pts)
{
	unsigned int reserved1;
	unsigned int process_cc_data_flag;
	unsigned int zero_bit;
	unsigned int cc_count;
	unsigned int reserved2;
	unsigned int same;
	unsigned int marker_bits;
	unsigned int i;

	/* Was process_em_data_flag: "This flag is set to
	   indicate whether it is necessary to process the em_data. If
	   it is set to 1, the em_data has to be parsed and its
	   meaning has to be processed. When it is set to 0, the
	   em_data can be discarded." */
	reserved1 = (buf[9] >> 7) & 1;

	process_cc_data_flag = (buf[9] >> 6) & 1;

	/* Was: additional_cc_data. */
	zero_bit = (buf[9] >> 5) & 1;

	cc_count = buf[9] & 0x1F;

	/* Was em_data: "Eight bits for representing emergency
	   message." */
	reserved2 = buf[10];

	fprintf (fp, "cc_data pts=%" PRId64 " (%+" PRId64 ") "
		 "'1'=%u%s process_cc_data_flag=%u "
		 "'0'=%u%s cc_count=%u 'FF'=0x%02X%s:\n",
		 pts, pts - last_pts, reserved1,
		 (1 != reserved1) ? "*" : "", process_cc_data_flag,
		 zero_bit, (0 != zero_bit) ? "*" : "",
		 cc_count, reserved2,
		 (0xFF != reserved2) ? "*" : "");

	same = 0;
	for (i = 0; i <= cc_count; ++i) {
		if (i > 0 && i < cc_count
		    && 0 == memcmp (&buf[11 + i * 3],
				    &buf[ 8 + i * 3], 3)) {
			++same;
		} else {
			if (same > 1) {
				fprintf (fp, "  %2u-%u as above\n",
					 i - same, i - 1);
			} else if (same > 0) {
				dump_cc_data_pair (fp, i - 1, &buf[8 + i * 3]);
			}
			if (i < cc_count)
				dump_cc_data_pair (fp, i, &buf[11 + i * 3]);
			same = 0;
		}
	}

	marker_bits = buf[11 + cc_count * 3];

	fprintf (fp, "  marker_bits=0x%02X%s\n",
		 marker_bits, (0xFF != marker_bits) ? "*" : "");

	if (n_bytes > 12 + cc_count * 3) {
		fprintf (fp, "  extraneous");
		for (i = 12 + cc_count * 3; i < n_bytes; ++i)
			fprintf (stderr, " %02x", buf[i]);
		fputc ('\n', stderr);
	}
}

/* Note pts may be < 0 if no PTS was received. */
static void
decode_cc_data			(struct program *	pr,
				 int64_t		pts,
				 const uint8_t *	buf,
				 unsigned int		n_bytes)
{
	unsigned int process_cc_data_flag;
	unsigned int cc_count;
	unsigned int i;
	vbi_bool dtvcc;

	if (option_debug & DEBUG_CC_DATA) {
		static int64_t last_pts = 0; /* XXX */

		dump_cc_data (stderr, buf, n_bytes, pts, last_pts);
		last_pts = pts;
	}

	process_cc_data_flag = buf[9] & 0x40;
	if (!process_cc_data_flag)
		return;

	cc_count = buf[9] & 0x1F;
	dtvcc = FALSE;

	if (NULL != pr->cr.ccd.cc_data_tap_fp) {
		static uint8_t output_buffer [8 + 11 + 31 * 3];
		unsigned int in;
		unsigned int out;
		unsigned int n_bytes;

		for (in = 0; in < 8; ++in)
			output_buffer[in] = pts >> (56 - in * 8);
		n_bytes = 11 + cc_count * 3;
		memcpy (output_buffer + in, buf, n_bytes);
		in += n_bytes;
		out = sizeof (output_buffer);
		memset (output_buffer + in, 0, out - in);

		if (out != fwrite (output_buffer, 1, out,
				   pr->cr.ccd.cc_data_tap_fp)) {
			errno_exit ("cc_data tap write error");
		}
	}

	for (i = 0; i < cc_count; ++i) {
		unsigned int b0;
		unsigned int cc_valid;
		enum cc_type cc_type;
		unsigned int cc_data_1;
		unsigned int cc_data_2;
		unsigned int j;

		b0 = buf[11 + i * 3];
		cc_valid = b0 & 4;
		cc_type = (enum cc_type)(b0 & 3);
		cc_data_1 = buf[12 + i * 3];
		cc_data_2 = buf[13 + i * 3];

		switch (cc_type) {
		case NTSC_F1:
		case NTSC_F2:
			/* Note CEA 708-C Table 4: Only one NTSC pair
			   will be present in field picture user_data
			   or in progressive video pictures, and up to
			   three can occur if the frame rate < 30 Hz
			   or repeat_first_field = 1. */
			if (!cc_valid || i >= 3 || dtvcc) {
				/* Illegal, invalid or filler. */
				break;
			}

			if (option_debug & (DEBUG_CC_F1 | DEBUG_CC_F2)) {
				if ((NTSC_F1 == cc_type
				     && 0 != (option_debug & DEBUG_CC_F1))
				    || (NTSC_F2 == cc_type
					&& 0 != (option_debug & DEBUG_CC_F2)))
					dump_cc (stderr, i, cc_count,
						 cc_valid, cc_type,
						 cc_data_1, cc_data_2);
			}

			cc_feed (&pr->cr.cc, &buf[12 + i * 3],
				 /* line */ (NTSC_F1 == cc_type) ? 21 : 284,
				 &pr->now, pts);

			/* XXX replace me. */
			if (NTSC_F1 == cc_type) {
				pr->cr.field = 0;
				if (pr->cr.usexds) /* fields swapped? */
					XDSdecode(&pr->cr, cc_data_1
						  + cc_data_2 * 256);
			} else {
				pr->cr.field = 1;
				if (pr->cr.usexds)
					XDSdecode(&pr->cr, cc_data_1
						  + cc_data_2 * 256);
			}

			break;

		case DTVCC_DATA:
			j = pr->cr.dtvcc.packet_size;
			if (j <= 0) {
				/* Missed packet start. */
				break;
			} else if (!cc_valid) {
				/* End of DTVCC packet. */
				dtvcc_decode_packet (&pr->cr.dtvcc,
						     &pr->now, pts);
				pr->cr.dtvcc.packet_size = 0;
			} else if (j >= 128) {
				/* Packet buffer overflow. */
				dtvcc_reset (&pr->cr.dtvcc);
				pr->cr.dtvcc.packet_size = 0;
			} else {
				pr->cr.dtvcc.packet[j] = cc_data_1;
				pr->cr.dtvcc.packet[j + 1] = cc_data_2;
				pr->cr.dtvcc.packet_size = j + 2;
			}
			break;

		case DTVCC_START:
			dtvcc = TRUE;
			j = pr->cr.dtvcc.packet_size;
			if (j > 0) {
				/* End of DTVCC packet. */
				dtvcc_decode_packet (&pr->cr.dtvcc,
						     &pr->now, pts);
			}
			if (!cc_valid) {
				/* No new data. */
				pr->cr.dtvcc.packet_size = 0;
			} else {
				pr->cr.dtvcc.packet[0] = cc_data_1;
				pr->cr.dtvcc.packet[1] = cc_data_2;
				pr->cr.dtvcc.packet_size = 2;
			}
			break;
		}
	}
}

static void
init_cc_data_decoder		(struct cc_data_decoder *cd)
{
	CLEAR (*cd);
}

static void
cc_data_test_loop		(struct program *	pr,
				 const char *		test_file_name)
{
	FILE *test_fp;

	test_fp = open_test_file (test_file_name);

	for (;;) {
		static uint8_t buffer[8 + 11 + 31 * 3];
		size_t todo;
		size_t actual;

		todo = sizeof (buffer);
		actual = fread (buffer, 1, todo, test_fp);
		if (likely (actual == todo)) {
			int64_t pts;
			unsigned int i;

			pts = 0;
			for (i = 0; i < 8; ++i) {
				pts |= buffer[i] << (56 - i * 8);
			}
			decode_cc_data (pr, pts, &buffer[8], actual);
			continue;
		}

		if (ferror (test_fp)) {
			errno_exit ("CC data file read error");
		} else {
			log (1, "End of CC data file.\n");
			fclose (test_fp);
			return;
		}
	}
}

/* DVB capture functions and transport stream decoding. */

static void
init_buffer			(struct buffer *	b,
				 unsigned int		capacity)
{
	b->capacity = capacity;
	b->base = xmalloc (capacity);
	b->in = 0;
	b->out = 0;
}

static void
dump_pes_buffer			(FILE *			fp,
				 const struct pes_buffer *b,
				 const char *		name)
  _vbi_unused;

static void
dump_pes_buffer			(FILE *			fp,
				 const struct pes_buffer *b,
				 const char *		name)
{
	unsigned int i;

	fprintf (fp, "%s PES buffer:\n", name);

	for (i = 0; i < b->n_packets; ++i) {
		fprintf (fp, "%2u: offs=%5u size=%u/%u "
			 "dts=%" PRId64 " (%+" PRId64 ") "
			 "duration=%" PRId64 " splice=%d lost=%d\n",
			 i,
			 b->packet[i].offset,
			 b->packet[i].payload,
			 b->packet[i].size,
			 b->packet[i].dts,
			 (i > 0) ? (b->packet[i].dts
				    - b->packet[i - 1].dts) : 0,
			 b->packet[i].duration,
			 b->packet[i].splice,
			 b->packet[i].data_lost);
	}
}



static vbi_bool
decode_time_stamp		(int64_t *		ts,
				 const uint8_t *	buf,
				 unsigned int		marker)
{
	/* ISO 13818-1 Section 2.4.3.6 */

	if (0 != ((marker ^ buf[0]) & 0xF1))
		return FALSE;

	if (NULL != ts) {
		unsigned int a, b, c;

		/* marker [4], TS [32..30], marker_bit,
		   TS [29..15], marker_bit,
		   TS [14..0], marker_bit */
		a = (buf[0] >> 1) & 0x7;
		b = (buf[1] * 256 + buf[2]) >> 1;
		c = (buf[3] * 256 + buf[4]) >> 1;

		*ts = ((int64_t) a << 30) + (b << 15) + (c << 0);
	}

	return TRUE;
}


static void
dump_pes_packet_header		(FILE *			fp,
				 const uint8_t *	buf)
{
	unsigned int packet_start_code_prefix;
	unsigned int stream_id;
	unsigned int PES_packet_length;
	unsigned int PES_scrambling_control;
	unsigned int PES_priority;
	unsigned int data_alignment_indicator;
	unsigned int copyright;
	unsigned int original_or_copy;
	unsigned int PTS_DTS_flags;
	unsigned int ESCR_flag;
	unsigned int ES_rate_flag;
	unsigned int DSM_trick_mode_flag;
	unsigned int additional_copy_info_flag;
	unsigned int PES_CRC_flag;
	unsigned int PES_extension_flag;
	unsigned int PES_header_data_length;
	int64_t ts;

	/* ISO 13818-1 Section 2.4.3.6. */

	packet_start_code_prefix  = buf[0] * 65536 + buf[1] * 256 + buf[2];
	stream_id		  = buf[3];
	PES_packet_length	  = buf[4] * 256 + buf[5];
	/* '10' */
	PES_scrambling_control	  = (buf[6] & 0x30) >> 4;
	PES_priority		  = buf[6] & 0x08;
	data_alignment_indicator  = buf[6] & 0x04;
	copyright		  = buf[6] & 0x02;
	original_or_copy	  = buf[6] & 0x01;
	PTS_DTS_flags		  = (buf[7] & 0xC0) >> 6;
	ESCR_flag		  = buf[7] & 0x20;
	ES_rate_flag		  = buf[7] & 0x10;
	DSM_trick_mode_flag	  = buf[7] & 0x08;
	additional_copy_info_flag = buf[7] & 0x04;
	PES_CRC_flag		  = buf[7] & 0x02;
	PES_extension_flag	  = buf[7] & 0x01;
	PES_header_data_length	  = buf[8];

	fprintf (fp, "PES %06X%02X %5u "
		 "%u%u%u%c%c%c%c%u%c%c%c%c%c%c %u",
		 packet_start_code_prefix, stream_id,
		 PES_packet_length,
		 !!(buf[6] & 0x80),
		 !!(buf[6] & 0x40),
		 PES_scrambling_control,
		 PES_priority ? 'P' : '-',
		 data_alignment_indicator ? 'A' : '-',
		 copyright ? 'C' : '-',
		 original_or_copy ? 'O' : 'C',
		 PTS_DTS_flags,
		 ESCR_flag ? 'E' : '-',
		 ES_rate_flag ? 'E' : '-',
		 DSM_trick_mode_flag ? 'D' : '-',
		 additional_copy_info_flag ? 'A' : '-',
		 PES_CRC_flag ? 'C' : '-',
		 PES_extension_flag ? 'X' : '-',
		 PES_header_data_length);

	switch (PTS_DTS_flags) {
	case 0: /* no timestamps */
	case 1: /* forbidden */
		fputc ('\n', fp);
		break;

	case 2: /* PTS only */
		if (decode_time_stamp (&ts, &buf[9], 0x21))
			fprintf (fp, " %" PRId64 "\n", ts);
		else
			fputs (" PTS?\n", fp);
		break;

	case 3: /* PTS and DTS */
		if (decode_time_stamp (&ts, &buf[9], 0x31))
			fprintf (fp, " %" PRId64, ts);
		else
			fputs (" PTS?", fp);
		if (decode_time_stamp (&ts, &buf[14], 0x11))
			fprintf (fp, " %" PRId64 "\n", ts);
		else
			fputs (" DTS?\n", fp);
		break;
	}
}


static void
close_ts_file			(struct video_recorder *vr)
{
	if (NULL != vr->minicut_fp) {
		if (0 != fclose (vr->minicut_fp)) {
			struct program *pr;

			pr = PARENT (vr, struct program, vr);
			log_errno (1, "TS stream %u close error",
				   (unsigned int)(pr - program_table));
		}

		vr->minicut_fp = NULL;
	}
}

static unsigned int
mpeg2_crc			(const uint8_t *	buf,
				 unsigned int		n_bytes)
{
	static uint32_t crc_table[256];
	unsigned int crc;
	unsigned int i;

	/* ISO 13818-1 Annex B. */

	if (unlikely (0 == crc_table[255])) {
		const unsigned int poly = 
			((1 << 26) | (1 << 23) | (1 << 22) | (1 << 16) |
			 (1 << 12) | (1 << 11) | (1 << 10) | (1 << 8) |
			 (1 << 7) | (1 << 5) | (1 << 4) | (1 << 2) |
			 (1 << 1) | 1);
		unsigned int c, j;

		for (i = 0; i < 256; ++i) {
			c = i << 24;
			for (j = 0; j < 8; ++j) {
				if (c & (1 << 31))
					c = (c << 1) ^ poly;
				else
					c <<= 1;
			}
			crc_table[i] = c;
		}
		assert (0 != crc_table[255]);
	}

	crc = -1;
	for (i = 0; i < n_bytes; ++i)
		crc = crc_table[(buf[i] ^ (crc >> 24)) & 0xFF] ^ (crc << 8);

	return crc & 0xFFFFFFFFUL;
}

static const unsigned int	pmt_pid = 0x5A5;

static void
init_pmt			(uint8_t		buf[188],
				 struct video_recorder *vr,
				 const struct ts_decoder *td)
{
	uint32_t CRC_32;

	/* sync_byte [8], transport_error_indicator,
	   payload_unit_start_indicator, transport_priority, PID [13],
	   transport_scrambling_control [2], adaptation_field_control
	   [2], continuity_counter [4] */
	buf[0] = 0x47;
	buf[1] = 0x40 | ((pmt_pid & 0x1FFF) >> 8);
	buf[2] = pmt_pid;
	buf[3] = 0x10 | (vr->pmt_cc & 0x0F);
	++vr->pmt_cc;

	/* pointer_field */
	buf[4] = 0x00;

	/* table_id [8] */
	buf[5] = 0x02; /* TS_program_map_section */

	/* section_syntax_indicator, '0', reserved [2], section_length
	   [12] */
	buf[6] = 0xB0;
	buf[7] = 31 - 8;

	/* program_number [16] */
	buf[8] = 0x00;
	buf[9] = 0x01;

	/* reserved [2], version_number [5], current_next_indicator */
	buf[10] = 0xC1;

	/* section_number [8], last_section_number [8] */
	buf[11] = 0x00;
	buf[12] = 0x00;

	/* reserved [3], PCR_PID [13] */
	buf[13] = 0xE0 | (td->pid[0] >> 8);
	buf[14] = td->pid[0];

	/* reserved [4], program_info_length [12] */
	buf[15] = 0xF0;
	buf[16] = 0x00;

	/* stream_type [8], reserved [3], elementary_PID [13],
	   reserved [4], ES_info_length [12] */
	buf[17] = 0x02; /* MPEG-2 video */
	buf[18] = 0xE0 | (td->pid[0] >> 8);
	buf[19] = td->pid[0];
	buf[20] = 0xF0;
	buf[21] = 0x00;

	buf[22] = 0x81; /* AC3 audio */
	buf[23] = 0xE0 | (td->pid[1] >> 8);
	buf[24] = td->pid[1];
	buf[25] = 0xF0;
	buf[26] = 0x00;

	CRC_32 = mpeg2_crc (buf + 5, 27 - 5);
	buf[27] = CRC_32 >> 24;
	buf[28] = CRC_32 >> 16;
	buf[29] = CRC_32 >> 8;
	buf[30] = CRC_32;

	memset (buf + 31, -1, 188 - 31);
}

static void
init_pat			(uint8_t		buf[188],
				 struct video_recorder *vr)
{
	uint32_t CRC_32;

	/* sync_byte [8], transport_error_indicator,
	   payload_unit_start_indicator, transport_priority, PID [13],
	   transport_scrambling_control [2], adaptation_field_control
	   [2], continuity_counter [4] */
	buf[0] = 0x47;
	buf[1] = 0x40;
	buf[2] = 0x00;
	buf[3] = 0x10 | (vr->pat_cc & 0x0F);
	++vr->pat_cc;

	/* pointer_field [8] */
	buf[4] = 0x00;

	/* table_id [8] */
	buf[5] = 0x00; /* program_association_section */

	/* section_syntax_indicator, '0', reserved [2],
	   section_length [12] */
	buf[6] = 0xB0;
	buf[7] = 21 - 8;

	/* transport_stream_id [16] */
	buf[8] = 0x00;
	buf[9] = 0x01;

	/* reserved [2], version_number [5], current_next_indicator */
	buf[10] = 0xC1;

	/* section_number [8], last_section_number [8] */
	buf[11] = 0x00;
	buf[12] = 0x00;

	/* program_number [16] */
	buf[13] = 0x00;
	buf[14] = 0x01;

	/* reserved [3], program_map_PID [13] */
	buf[15] = 0xE0 | ((pmt_pid & 0x1FFF) >> 8);
	buf[16] = pmt_pid;

	CRC_32 = mpeg2_crc (buf + 5, 17 - 5);
	buf[17] = CRC_32 >> 24;
	buf[18] = CRC_32 >> 16;
	buf[19] = CRC_32 >> 8;
	buf[20] = CRC_32;

	memset (buf + 21, -1, 188 - 21);
}

static void
video_recorder			(struct video_recorder *vr,
				 const uint8_t		buf[188])
{
	struct program *pr = PARENT (vr, struct program, vr);
	size_t actual;

	if (NULL == pr->option_minicut_dir_name)
		return; /* no video recording */

	/* The TS packet rate is too high to call time() here. We do
	   that when a picture arrives, some 24 to 60 times/second. */
	if (0 == (pr->now.tv_sec | pr->now.tv_usec))
		return; /* no picture received yet */

	/* Note minicut_end is initially zero. */
	if (pr->now.tv_sec >= vr->minicut_end) {
		char file_name[32];
		struct tm tm;
		time_t t;

		t = pr->now.tv_sec;
		if (NULL == gmtime_r (&t, &tm)) {
			/* Should not happen. */
			error_exit ("System time invalid.\n");
		}

		vr->minicut_end = t + (60 - tm.tm_sec);

		if (option_minicut_test) {
			tm.tm_sec = 0;
		} else if (1) {
			tm.tm_sec = 0;
		} else {
			if (0 != tm.tm_sec)
				return;
		}

		close_ts_file (vr);

		snprintf (file_name, sizeof (file_name),
			  "/%04u%02u%02u%02u%02u%02u",
			  tm.tm_year + 1900,
			  tm.tm_mon + 1,
			  tm.tm_mday,
			  tm.tm_hour,
			  tm.tm_min,
			  tm.tm_sec);

		vr->minicut_fp = open_minicut_file (pr, &tm, file_name, ".ts");
		if (NULL != vr->minicut_fp) {
			uint8_t buf[2 * 188];

			init_pat (buf, vr);
			init_pmt (buf + 188, vr, &pr->tsd);

			actual = fwrite (buf, 1, 2 * 188, vr->minicut_fp);
			if (2 * 188 != actual) {
				log_errno (1, "TS stream %u write error",
					   station_num (pr));
			}
		}
	}

	if (NULL == vr->minicut_fp)
		return;

	actual = fwrite (buf, 1, 188, vr->minicut_fp);
	if (188 != actual) {
		log_errno (1, "TS stream %u write error",
			   station_num (pr));
	}
}


static void
init_video_recorder		(struct video_recorder *vr)
{





	vr->pat_cc = 0;
	vr->pmt_cc = 0;

	vr->minicut_end = 0;
	vr->minicut_fp = NULL;
}

/* Video elementary stream decoder. */

static void
vesd_reorder_decode_cc_data	(struct video_es_decoder *vd,
				 const uint8_t *	buf,
				 unsigned int		n_bytes)
{
	struct program *pr = PARENT (vd, struct program, vesd);

	n_bytes = MIN (n_bytes, (unsigned int)
		       sizeof (vd->reorder_buffer[0]));

	switch (vd->picture_structure) {
	case FRAME_PICTURE:
		if (0 != vd->reorder_pictures) {
			if (vd->reorder_pictures & 5) {
				/* Top field or top and bottom field. */
				decode_cc_data (pr, vd->reorder_pts[0],
						vd->reorder_buffer[0],
						vd->reorder_n_bytes[0]);
			}
			if (vd->reorder_pictures & 2) {
				/* Bottom field. */
				decode_cc_data (pr, vd->reorder_pts[1],
						vd->reorder_buffer[1],
						vd->reorder_n_bytes[1]);
			}
		}

		memcpy (vd->reorder_buffer[0], buf, n_bytes);
		vd->reorder_n_bytes[0] = n_bytes;
		vd->reorder_pts[0] = vd->pts;

		/* We have a frame. */
		vd->reorder_pictures = 4;

		break;

	case TOP_FIELD:
		if (vd->reorder_pictures >= 3) {
			/* Top field or top and bottom field. */
			decode_cc_data (pr, vd->reorder_pts[0],
					vd->reorder_buffer[0],
					vd->reorder_n_bytes[0]);

			vd->reorder_pictures &= 2;
		} else if (1 == vd->reorder_pictures) {
			/* Apparently we missed a bottom field. */
		}

		memcpy (vd->reorder_buffer[0], buf, n_bytes);
		vd->reorder_n_bytes[0] = n_bytes;
		vd->reorder_pts[0] = vd->pts;

		/* We have a top field. */
		vd->reorder_pictures |= 1;

		break;

	case BOTTOM_FIELD:
		if (vd->reorder_pictures >= 3) {
			if (vd->reorder_pictures >= 4) {
				/* Top and bottom field. */
				decode_cc_data (pr, vd->reorder_pts[0],
						vd->reorder_buffer[0],
						vd->reorder_n_bytes[0]);
			} else {
				/* Bottom field. */
				decode_cc_data (pr, vd->reorder_pts[1],
						vd->reorder_buffer[1],
						vd->reorder_n_bytes[1]);
			}

			vd->reorder_pictures &= 1;
		} else if (2 == vd->reorder_pictures) {
			/* Apparently we missed a top field. */
		}

		memcpy (vd->reorder_buffer[1], buf, n_bytes);
		vd->reorder_n_bytes[1] = n_bytes;
		vd->reorder_pts[1] = vd->pts;

		/* We have a bottom field. */
		vd->reorder_pictures |= 2;

		break;

	default: /* invalid */
		break;
	}
}

static void
vesd_user_data			(struct video_es_decoder *vd,
				 const uint8_t *	buf,
				 unsigned int		min_bytes_valid)
{
	unsigned int ATSC_identifier;
	unsigned int user_data_type_code;
	unsigned int cc_count;

	/* ATSC A/53 Part 4:2007 Section 6.2.2 */

	if (unlikely (option_debug & DEBUG_VESD_USER_DATA)) {
		unsigned int i;

		fprintf (stderr, "VES UD: %s %s ref=%u "
			 "dts=%" PRId64 " pts=%" PRId64,
			 picture_coding_type_name (vd->picture_coding_type),
			 picture_structure_name (vd->picture_structure),
			 vd->picture_temporal_reference,
			 vd->dts, vd->pts);
		for (i = 0; i < min_bytes_valid; ++i)
			fprintf (stderr, " %02x", buf[i]);
		fputc (' ', stderr);
		for (i = 0; i < min_bytes_valid; ++i)
			fputc (printable (buf[i]), stderr);
		fputc ('\n', stderr);
	}

	/* NB. the PES packet header is optional and we may receive
	   more than one user_data structure. */
	if ((RECEIVED_PICTURE |
	     RECEIVED_PICTURE_EXT)
	    != (vd->received_blocks & (RECEIVED_PICTURE |
				       RECEIVED_PICTURE_EXT))) {
		/* Either sequence or group user_data, or we missed
		   the picture_header. */
		vd->received_blocks &= ~RECEIVED_PES_PACKET;
		return;
	}

	if (NULL == buf) {
		/* No user_data received on this field or frame. */

		if (option_debug & DEBUG_VESD_CC_DATA) {
			fprintf (stderr, "DTVCC coding=%s structure=%s "
				 "pts=%" PRId64 " no data\n",
				 picture_coding_type_name
				 (vd->picture_coding_type),
				 picture_structure_name
				 (vd->picture_structure),
				 vd->pts);
		}
	} else {
		/* start_code_prefix [24], start_code [8],
		   ATSC_identifier [32], user_data_type_code [8] */
		if (min_bytes_valid < 9)
			return;

		ATSC_identifier = ((buf[4] << 24) | (buf[5] << 16) |
				   (buf[6] << 8) | buf[7]);
		if (0x47413934 != ATSC_identifier)
			return;

		user_data_type_code = buf[8];
		if (0x03 != user_data_type_code)
			return;

		/* ATSC A/53 Part 4:2007 Section 6.2.1: "No more than one
		   user_data() structure using the same user_data_type_code
		   [...] shall be present following any given picture
		   header." */
		if (vd->received_blocks & RECEIVED_MPEG_CC_DATA) {
			/* Too much data lost. */
			return;
		}

		vd->received_blocks |= RECEIVED_MPEG_CC_DATA;

		/* reserved, process_cc_data_flag, zero_bit, cc_count [5],
		   reserved [8] */
		if (min_bytes_valid < 11)
			return;

		/* one_bit, reserved [4], cc_valid, cc_type [2],
		   cc_data_1 [8], cc_data_2 [8] */
		cc_count = buf[9] & 0x1F;

		/* CEA 708-C Section 4.4 permits padding, so we have to see
		   all cc_data elements. */
		if (min_bytes_valid < 11 + cc_count * 3)
			return;

		if (option_debug & DEBUG_VESD_CC_DATA) {
			char text[0x1F * 2 + 1];
			unsigned int i;
			vbi_bool ooo;

			for (i = 0; i < cc_count; ++i) {
				text[i * 2 + 0] = printable (buf[12 + i * 3]);
				text[i * 2 + 1] = printable (buf[13 + i * 3]);
			}
			text[cc_count * 2] = 0;

			ooo = (B_TYPE == vd->picture_coding_type
			       && vd->reorder_pictures < 3);

			fprintf (stderr, "DTVCC coding=%s structure=%s "
				 "pts=%" PRId64 " cc_count=%u "
				 "n_bytes=%u '%s'%s\n",
				 picture_coding_type_name
				 (vd->picture_coding_type),
				 picture_structure_name
				 (vd->picture_structure),
				 vd->pts, cc_count, min_bytes_valid,
				 text, ooo ? " (out of order)" : "");
		}
	}

	/* CEA 708-C Section 4.4.1.1 */

	switch (vd->picture_coding_type) {
	case I_TYPE:
	case P_TYPE:
		vesd_reorder_decode_cc_data (vd, buf, min_bytes_valid);
		break;

	case B_TYPE:
		/* To prevent a gap in the caption stream we must not
		   decode B pictures until we have buffered both
		   fields of the temporally following I or P picture. */
		if (vd->reorder_pictures < 3) {
			vd->reorder_pictures = 0;
			break;
		}

		/* To do: If a B picture appears to have a higher
		   temporal_reference than the picture it forward
		   references we lost that I or P picture. */
		{
			struct program *pr;

			pr = PARENT (vd, struct program, vesd);
			decode_cc_data (pr, vd->pts, buf,
					min_bytes_valid);
		}

		break;

	default: /* invalid */
		break;
	}
}

static void
vesd_extension			(struct video_es_decoder *vd,
				 const uint8_t *	buf,
				 unsigned int		min_bytes_valid)
{
	enum extension_start_code_identifier extension_start_code_identifier;

	/* extension_start_code [32],
	   extension_start_code_identifier [4],
	   f_code [4][4], intra_dc_precision [2],
	   picture_structure [2], ... */
	if (min_bytes_valid < 7)
		return;

	extension_start_code_identifier =
		(enum extension_start_code_identifier)(buf[4] >> 4);
	if (PICTURE_CODING_EXTENSION_ID
	    != extension_start_code_identifier)
		return;

	if (0 == (vd->received_blocks & RECEIVED_PICTURE)) {
		/* We missed the picture_header. */
		vd->received_blocks = 0;
		return;
	}

	vd->picture_structure = (enum picture_structure)(buf[6] & 3);

	if (option_debug & DEBUG_VESD_PIC_EXT) {
		fprintf (stderr, "VES PIC EXT structure=%s\n",
			 picture_structure_name (vd->picture_structure));
	}

	vd->received_blocks |= RECEIVED_PICTURE_EXT;
}

static void
vesd_picture_header		(struct video_es_decoder *vd,
				 const uint8_t *	buf,
				 unsigned int		min_bytes_valid)
{
	unsigned int c;

	/* picture_start_code [32],
	   picture_temporal_reference [10],
	   picture_coding_type [3], ... */
	/* XXX consider estimating the PTS if none transmitted. */
 	if (min_bytes_valid < 6
	    /* || vd->received_blocks != RECEIVED_PES_PACKET */) {
		/* Too much data lost. */
		vd->received_blocks = 0;
		return;
	}

	c = buf[4] * 256 + buf[5];
	vd->picture_temporal_reference = (c >> 6) & 0x3FF;
	vd->picture_coding_type = (enum picture_coding_type)((c >> 3) & 7);

	if (option_debug & DEBUG_VESD_PIC_HDR) {
		fprintf (stderr, "VES PIC HDR ref=%d type=%ss\n",
			 vd->picture_temporal_reference,
			 picture_coding_type_name
			 (vd->picture_coding_type));
	}
 
 	++vd->n_pictures_received;

	vd->received_blocks |= RECEIVED_PICTURE;
}

static void
vesd_pes_packet_header		(struct video_es_decoder *vd,
				 const uint8_t *	buf,
				 unsigned int		min_bytes_valid)
{
	unsigned int PES_packet_length;
	unsigned int PTS_DTS_flags;
	int64_t pts;

	if (unlikely (option_debug & DEBUG_VESD_PES_PACKET)) {
		dump_pes_packet_header (stderr, buf);
	}

	vd->pts = -1;
	vd->dts = -1;

	vd->received_blocks = 0;

	/* packet_start_code_prefix [24], stream_id [8],
	   PES_packet_length [16],

	   '10', PES_scrambling_control [2], PES_priority,
	   data_alignment_indicator, copyright, original_or_copy,

	   PTS_DTS_flags [2], ESCR_flag, ES_rate_flag,
	   DSM_trick_mode_flag, additional_copy_info_flag,
	   PES_CRC_flag, PES_extension_flag,

	   PES_header_data_length [8] */
	if (min_bytes_valid < 9)
		return;

	PES_packet_length = buf[4] * 256 + buf[5];
	PTS_DTS_flags = (buf[7] & 0xC0) >> 6;

	/* ISO 13818-1 Section 2.4.3.7: In transport streams video PES
	   packets do not carry data, they only contain the DTS/PTS of
	   the following picture and PES_packet_length must be
	   zero. */
	if (0 != PES_packet_length)
		return;

	switch (PTS_DTS_flags) {
	case 0: /* no timestamps */
		return;

	case 1: /* forbidden */
		return;

	case 2: /* PTS only */
		if (min_bytes_valid < 14)
			return;
		if (!decode_time_stamp (&vd->pts, &buf[9], 0x21))
			return;
		break;

	case 3: /* PTS and DTS */
		if (min_bytes_valid < 19)
			return;
		if (!decode_time_stamp (&pts, &buf[9], 0x31))
			return;
		if (!decode_time_stamp (&vd->dts, &buf[14], 0x11))
			return;
		vd->pts = pts;
		break;
	}

	if (unlikely (option_minicut_test)) {
		struct program *pr;
		int64_t dts;

		pr = PARENT (vd, struct program, vesd);

		dts = vd->dts;
		if (dts < 0)
			dts = vd->pts;

		if (pr->first_dts < 0) {
			pr->first_dts = dts;
		} else if (dts < pr->first_dts) {
			dts += TIMESTAMP_MASK + 1;
		}

		pr->now.tv_sec = (dts - pr->first_dts) / 90000;
		pr->now.tv_usec = (dts - pr->first_dts) % 90000 * 100 / 9;
	} else {
		struct program *pr;

		pr = PARENT (vd, struct program, vesd);
		gettimeofday (&pr->now, /* tz */ NULL);
	}

	vd->received_blocks = RECEIVED_PES_PACKET;
}

static void
vesd_decode_block		(struct video_es_decoder *vd,
				 unsigned int		start_code,
				 const uint8_t *	buf,
				 unsigned int		n_bytes,
				 unsigned int		min_bytes_valid,
				 vbi_bool		data_lost)
{
	if (unlikely (option_debug & DEBUG_VESD_START_CODE)) {
		fprintf (stderr, "VES 0x000001%02X %u %u\n",
			 start_code, min_bytes_valid, n_bytes);
	}

	/* The CEA 608-C and 708-C Close Caption data is encoded in
	   picture user data fields. ISO 13818-2 requires the start
	   code sequence 0x00, 0xB5/8, (0xB5?, 0xB2?)*. To properly
	   convert from coded order to display order we also need the
	   picture_coding_type and picture_structure fields. */

	if (likely (start_code <= 0xAF)) {
		if (!data_lost
		    && (vd->received_blocks == (RECEIVED_PICTURE |
						RECEIVED_PICTURE_EXT)
			|| vd->received_blocks == (RECEIVED_PES_PACKET |
						   RECEIVED_PICTURE |
						   RECEIVED_PICTURE_EXT))) {
			/* No user data received for this picture. */
			vesd_user_data (vd, NULL, 0);
		}

		if (unlikely (0x00 == start_code) && !data_lost) {
			vesd_picture_header (vd, buf, min_bytes_valid);
		} else {
			/* slice_start_code, or data lost in or after
			   the picture_header. */

			/* For all we care the picture data is just
			   useless filler prior to the next PES packet
			   header, and we need an uninterrupted
			   sequence from there to the next picture
			   user_data to ensure the PTS, DTS,
			   picture_temporal_reference,
			   picture_coding_type, picture_structure and
			   cc_data belong together. */

			vd->received_blocks = 0;
			vd->pts = -1;
			vd->dts = -1;
		}
	} else if (USER_DATA_START_CODE == start_code) {
		vesd_user_data (vd, buf, min_bytes_valid);
	} else if (data_lost) {
		/* Data lost in or after this block. */
		vd->received_blocks = 0;
	} else if (EXTENSION_START_CODE == start_code) {
		vesd_extension (vd, buf, min_bytes_valid);
	} else if (start_code >= VIDEO_STREAM_0
		   && start_code <= VIDEO_STREAM_15) {
		if (!data_lost
		    && (vd->received_blocks == (RECEIVED_PICTURE |
						RECEIVED_PICTURE_EXT)
			|| vd->received_blocks == (RECEIVED_PES_PACKET |
						   RECEIVED_PICTURE |
						   RECEIVED_PICTURE_EXT))) {
			/* No user data received for previous picture. */
			vesd_user_data (vd, NULL, 0);
		}

		/* Start of a new picture. */
		vesd_pes_packet_header (vd, buf, min_bytes_valid);
	} else {
		/* Should be a sequence_header or
		   group_of_pictures_header. */

		vd->received_blocks &= RECEIVED_PES_PACKET;
	}

	/* Not all of this data is relevant for caption decoding but
	   we may need it to debug the video ES decoder. Without the
	   actual picture data it should be highly repetitive and
	   compress rather well. */
	if (unlikely (NULL != vd->video_es_tap_fp)) {
		unsigned int n = n_bytes;

		if (start_code >= 0x01 && start_code <= 0xAF) {
			if (NULL == vd->option_video_es_all_tap_file_name)
				n = MIN (n, 8u);
		}
		if (n != fwrite (buf, 1, n, vd->video_es_tap_fp)) {
			errno_exit ("Video ES tap write error");
		}
	}
}

static unsigned int
vesd_make_room			(struct video_es_decoder *vd,
				 unsigned int		required)
{
	struct buffer *b;
	unsigned int capacity;
	unsigned int in;

	b = &vd->buffer;
	capacity = b->capacity;
	in = b->in;

	if (unlikely (in + required > capacity)) {
		unsigned int consumed;
		unsigned int unconsumed;

		consumed = b->out;
		unconsumed = in - consumed;
		if (required > capacity - unconsumed) {
			/* XXX make this a recoverable error. */
			error_exit ("Video ES buffer overflow.\n");
		}
		memmove (b->base, b->base + consumed, unconsumed);
		in = unconsumed;
		b->out = 0;
	}

	return in;
}

static void
video_es_decoder		(struct video_es_decoder *vd,
				 const uint8_t *	buf,
				 unsigned int		n_bytes,
				 vbi_bool		data_lost)
{
	const uint8_t *s;
	const uint8_t *e;
	const uint8_t *e_max;
	unsigned int in;

	/* This code searches for a start code and then decodes the
	   data between the previous and the current start code. */

	in = vesd_make_room (vd, n_bytes);

	memcpy (vd->buffer.base + in, buf, n_bytes);
	vd->buffer.in = in + n_bytes;

	s = vd->buffer.base + vd->buffer.out + vd->skip;
	e = vd->buffer.base + in + n_bytes - 4;
	e_max = e;

	if (unlikely (data_lost)) {
		if (vd->min_bytes_valid >= UINT_MAX) {
			vd->min_bytes_valid =
				in - vd->buffer.out;
		}

		/* Data is missing after vd->buffer.base + in, so
		   we must ignore apparent start codes crossing that
		   boundary. */
		e -= n_bytes;
	}

	for (;;) {
		const uint8_t *b;
		enum start_code start_code;
		unsigned int n_bytes;
		unsigned int min_bytes_valid;

		for (;;) {
			if (s >= e) {
				/* Need more data. */

				if (unlikely (s < e_max)) {
					/* Skip over the lost data. */
					s = e + 4;
					e = e_max;
					continue;
				}

				/* In the next iteration skip the
				   bytes we already scanned. */
				vd->skip = s - vd->buffer.base
					- vd->buffer.out;

				return;
			}

			if (likely (0 != (s[2] & ~1))) {
				/* Not 000001 or xx0000 or xxxx00. */
				s += 3;
			} else if (0 != (s[0] | s[1]) || 1 != s[2]) {
				++s;
			} else {
				break;
			}
		}

		b = vd->buffer.base + vd->buffer.out;
		n_bytes = s - b;
		min_bytes_valid = n_bytes;
		data_lost = FALSE;

		if (unlikely (vd->min_bytes_valid < UINT_MAX)) {
			if (n_bytes < vd->min_bytes_valid) {
				/* We found a new start code before
				   the missing data. */
				vd->min_bytes_valid -= n_bytes;
			} else {
				min_bytes_valid = vd->min_bytes_valid;
				vd->min_bytes_valid = UINT_MAX;

				/* Need a flag in case we lost data just
				   before the next start code. */
				data_lost = TRUE;
			}
		}

		start_code = vd->last_start_code;
		if (likely ((int) start_code >= 0)) {
			vesd_decode_block (vd, start_code, b,
					   n_bytes, min_bytes_valid,
					   data_lost);
		}

		/* Remove the data we just decoded from the
		   buffer. Remember the position of the new start code
		   we found, skip it and continue the search. */
		vd->buffer.out = s - vd->buffer.base;
		vd->last_start_code = (enum start_code) s[3];
		s += 4;
	}
}

static void
reset_video_es_decoder		(struct video_es_decoder *vd)
{
	vd->buffer.in = 0;
	vd->buffer.out = 0;

	vd->min_bytes_valid = UINT_MAX;
	vd->skip = 0;
	vd->last_start_code = (enum start_code) -1;

	vd->pts = -1;
	vd->dts = -1;
	vd->picture_coding_type = (enum picture_coding_type) -1;
	vd->picture_structure = (enum picture_structure) -1;
	vd->received_blocks = 0;
	vd->reorder_pictures = 0;
}

static void
init_video_es_decoder		(struct video_es_decoder *vd)
{
	CLEAR (*vd);

	init_buffer (&vd->buffer, /* capacity */ 1 << 20);
	reset_video_es_decoder (vd);
}

static void
video_es_test_loop		(struct program *	pr,
				 const char *		test_file_name)
{
	FILE *test_fp;

	assert (NULL != ts_buffer);

	test_fp = open_test_file (test_file_name);

	for (;;) {
		size_t todo;
		size_t actual;

		todo = 4096;
		actual = fread (ts_buffer, 1, todo, test_fp);
		if (likely (actual == todo)) {
			video_es_decoder (&pr->vesd, ts_buffer,
					  /* n_bytes */ actual,
					  /* data_lost */ FALSE);
		} else if (ferror (test_fp)) {
			errno_exit ("Video ES read error");
		} else {
			log (1, "End of video ES file.\n");
			fclose (test_fp);
			return;
		}
	}
}

static void
dump_ts_packet_header		(FILE *			fp,
				 const uint8_t		buf[188])
{
	unsigned int sync_byte;
	unsigned int transport_error_indicator;
	unsigned int payload_unit_start_indicator;
	unsigned int transport_priority;
	unsigned int PID;
	unsigned int transport_scrambling_control;
	unsigned int adaptation_field_control;
	unsigned int continuity_counter;
	unsigned int header_length;

	sync_byte			= buf[0];
	transport_error_indicator	= buf[1] & 0x80;
	payload_unit_start_indicator	= buf[1] & 0x40;
	transport_priority		= buf[1] & 0x20;
	PID				= (buf[1] * 256 + buf[2]) & 0x1FFF;
	transport_scrambling_control	= (buf[3] & 0xC0) >> 6;
	adaptation_field_control	= (buf[3] & 0x30) >> 4;
	continuity_counter		= buf[3] & 0x0F;

	if (adaptation_field_control >= 2) {
		unsigned int adaptation_field_length;

		adaptation_field_length = buf[4];
		header_length = 5 + adaptation_field_length;
	} else {
		header_length = 4;
	}

	fprintf (fp,
		 "TS %02x %c%c%c %04x %u%u%x %u\n",
		 sync_byte,
		 transport_error_indicator ? 'E' : '-',
		 payload_unit_start_indicator ? 'S' : '-',
		 transport_priority ? 'P' : '-',
		 PID,
		 transport_scrambling_control,
		 adaptation_field_control,
		 continuity_counter,
		 header_length);
}

static void
tsd_program			(struct program *	pr,
				 const uint8_t		buf[188],
				 unsigned int		pid,
				 unsigned int		es_num)
{
	unsigned int adaptation_field_control;
	unsigned int header_length;
	unsigned int payload_length;
	vbi_bool data_lost;

	adaptation_field_control = (buf[3] & 0x30) >> 4;
	if (likely (1 == adaptation_field_control)) {
		header_length = 4;
	} else if (3 == adaptation_field_control) {
		unsigned int adaptation_field_length;

		adaptation_field_length = buf[4];

		/* Zero length is used for stuffing. */
		if (adaptation_field_length > 0) {
			unsigned int discontinuity_indicator;

			/* ISO 13818-1 Section 2.4.3.5. Also the code
			   below would be rather upset if
			   header_length > packet_size. */
			if (adaptation_field_length > 182) {
				log (2, "Invalid TS header "
				     "on station %u, stream %u.\n",
				     station_num (pr), pid);
				/* Possibly. */
				pr->tsd.data_lost = TRUE;
				return;
			}

			/* ISO 13818-1 Section 2.4.3.5 */
			discontinuity_indicator = buf[5] & 0x80;
			if (discontinuity_indicator)
				pr->tsd.next_ts_cc[es_num] = -1;
		}

		header_length = 5 + adaptation_field_length;
	} else {
		/* 0 == adaptation_field_control: invalid;
		   2 == adaptation_field_control: no payload. */
		/* ISO 13818-1 Section 2.4.3.3:
		   continuity_counter shall not increment. */
		return;
	}

	payload_length = 188 - header_length;

	data_lost = pr->tsd.data_lost;

	if (unlikely (0 != ((pr->tsd.next_ts_cc[es_num]
			     ^ buf[3]) & 0x0F))) {
		/* Continuity counter mismatch. */

		if (pr->tsd.next_ts_cc[es_num] < 0) {
			/* First TS packet. */
		} else if (0 == (((pr->tsd.next_ts_cc[es_num] - 1)
				  ^ buf[3]) & 0x0F)) {
			/* ISO 13818-1 Section 2.4.3.3: Repeated packet. */
			return;
		} else {
			log (2, "TS continuity error "
			     "on station %u, stream %u.\n",
			     station_num (pr), pid);
			data_lost = TRUE;
		}
	}

	pr->tsd.next_ts_cc[es_num] = buf[3] + 1;
	pr->tsd.data_lost = FALSE;

	if (NULL != pr->option_minicut_dir_name)
		video_recorder (&pr->vr, buf);

	if (0 == es_num) {
		video_es_decoder (&pr->vesd, buf + header_length,
				  payload_length, data_lost);
	}
}

static void
ts_decoder			(const uint8_t		buf[188])
{
	unsigned int pid;
	unsigned int i;

	if (0) {
		dump_ts_packet_header (stderr, buf);
	}

	if (unlikely (NULL != ts_tap_fp)) {
		if (188 != fwrite (buf, 1, 188, ts_tap_fp)) {
			errno_exit ("TS tap write error");
		}
	}

	if (unlikely (buf[1] & 0x80)) {
		log (2, "TS transmission error.\n");
		
		/* The PID may be wrong, we don't know how much data
		   was lost, and continuity counters match by chance
		   with 1:16 probability. */
		for (i = 0; i < n_programs; ++i) {
			video_recorder (&program_table[i].vr, buf);
			program_table[i].tsd.data_lost = TRUE;
		}

		return;
	}

	pid = (buf[1] * 256 + buf[2]) & 0x1FFF;

	/* Note two or more programs may share one elementary stream
	   (e.g. radio programs with a dummy video stream). */
	for (i = 0; i < n_programs; ++i) {
		struct program *pr;
		unsigned int es_num;

		pr = program_table + i;

		es_num = 0;
		if (pid == pr->tsd.pid[1]) {
			es_num = 1;
		} else if (pid != pr->tsd.pid[0]) {
			continue;
		}

		tsd_program (pr, buf, pid, es_num);
	}
}

static void
init_ts_decoder			(struct ts_decoder *	td)
{
	CLEAR (*td);

	memset (&td->next_ts_cc, -1, sizeof (td->next_ts_cc));
}

static void
ts_test_loop			(const char *		test_file_name)
{
	FILE *test_fp;

	assert (NULL != ts_buffer);

	test_fp = open_test_file (test_file_name);

	for (;;) {
		size_t todo;
		size_t actual;

		todo = 188;
		actual = fread (ts_buffer, 1, todo, test_fp);
		if (likely (actual == todo)) {
			ts_decoder (ts_buffer);
		} else if (ferror (test_fp)) {
			errno_exit ("TS read error");
		} else {
			log (1, "End of TS file.\n");
			fclose (test_fp);
			return;
		}
	}
}

static void *
demux_thread			(void *			arg)
{
	unsigned int in;
	unsigned int out;

	arg = arg; /* unused */
		
	assert (0 == ts_buffer_capacity % 188);

	out = ts_buffer_out;

	/* We don't actually need the mutex but pthread_cond_wait()
	   won't work without it. */
	pthread_mutex_lock (&dx_mutex);

	for (;;) {
		unsigned int avail;

		in = ts_buffer_in;

		avail = in - out;
		if (in < out)
			avail += ts_buffer_capacity;

		if (avail <= 0) {
			/* Yield the CPU if the buffer is empty. */
			pthread_cond_wait (&dx_cond, &dx_mutex);
			continue;
		}

		if (0) {
			fputc (',', stderr);
			fflush (stderr);
		} else {
			ts_decoder (ts_buffer + out);
		}

		out += 188;
		if (out >= ts_buffer_capacity)
			out = 0;

		ts_buffer_out = out;
	}

	pthread_mutex_unlock (&dx_mutex);

	return NULL;
}

static void
init_program			(struct program *	pr)
{
	CLEAR (*pr);

	pr->first_dts = -1;

	init_ts_decoder (&pr->tsd);
	init_video_es_decoder (&pr->vesd);
	init_video_recorder (&pr->vr);
	init_caption_recorder (&pr->cr);
}

static void
destroy_demux_state		(void)
{
	free (ts_buffer);

	ts_buffer = NULL;
	ts_buffer_capacity = 0;

	pthread_cond_destroy (&dx_cond);
	pthread_mutex_destroy (&dx_mutex);
}

static void
init_demux_state		(void)
{
	pthread_mutex_init (&dx_mutex, /* attr */ NULL);
	pthread_cond_init (&dx_cond, /* attr */ NULL);

	/* This buffer prevents data loss if disk activity blocks
	   a printf() in the caption decoder. Actually we need to
	   buffer only CC data which trickles in at 9600 bits/s but I
	   don't want to burden the capture thread with TS and PES
	   demultiplexing and avoid the overhead of another thread. */
	ts_buffer_capacity = 20000 * 188;
	ts_buffer = xmalloc (ts_buffer_capacity);

	/* Copy-on-write and lock the pages. */
	memset (ts_buffer, -1, ts_buffer_capacity);

	ts_error = 0x00;

	ts_buffer_in = 0;
	ts_buffer_out = 0;

	ts_n_packets_in = 0;
}

/* Capture thread */

static const char *
fe_type_name			(enum fe_type		t)
{
#undef CASE
#define CASE(x) case FE_##x: return #x;

	switch (t) {
	CASE (QPSK)
	CASE (QAM)
	CASE (OFDM)
	CASE (ATSC)
	}

	return "invalid";
}

static const char *
fe_spectral_inversion_name	(enum fe_spectral_inversion t)
{
#undef CASE
#define CASE(x) case INVERSION_##x: return #x;

	switch (t) {
	CASE (OFF)
	CASE (ON)
	CASE (AUTO)
	}

	return "invalid";
}

static const char *
fe_code_rate_name		(enum fe_code_rate	t)
{
#undef CASE
#define CASE(x) case FEC_##x: return #x;

	switch (t) {
	CASE (NONE)
	CASE (1_2)
	CASE (2_3)
	CASE (3_4)
	CASE (4_5)
	CASE (5_6)
	CASE (6_7)
	CASE (7_8)
	CASE (8_9)
	CASE (AUTO)
	}

	return "invalid";
}

static const char *
fe_modulation_name		(enum fe_modulation	t)
{
#undef CASE
#define CASE(x) case x: return #x;

	switch (t) {
	CASE (QPSK)
	CASE (QAM_16)
	CASE (QAM_32)
	CASE (QAM_64)
	CASE (QAM_128)
	CASE (QAM_256)
	CASE (QAM_AUTO)
	CASE (VSB_8)
	CASE (VSB_16)
	}

	return "invalid";
}

static const char *
fe_transmit_mode_name		(enum fe_transmit_mode t)
{
#undef CASE
#define CASE(x) case TRANSMISSION_MODE_##x: return #x;

	switch (t) {
	CASE (2K)
	CASE (8K)
	CASE (AUTO)
	}

	return "invalid";
}

static const char *
fe_bandwidth_name		(enum fe_bandwidth	t)
{
#undef CASE
#define CASE(x) case BANDWIDTH_##x: return #x;

	switch (t) {
	CASE (8_MHZ)
	CASE (7_MHZ)
	CASE (6_MHZ)
	CASE (AUTO)
	}

	return "invalid";
}

static const char *
fe_guard_interval_name		(enum fe_guard_interval	t)
{
#undef CASE
#define CASE(x) case GUARD_INTERVAL_##x: return #x;

	switch (t) {
	CASE (1_32)
	CASE (1_16)
	CASE (1_8)
	CASE (1_4)
	CASE (AUTO)
	}

	return "invalid";
}

static const char *
fe_hierarchy_name		(enum fe_hierarchy	t)
{
#undef CASE
#define CASE(x) case HIERARCHY_##x: return #x;

	switch (t) {
	CASE (NONE)
	CASE (1)
	CASE (2)
	CASE (4)
	CASE (AUTO)
	}

	return "invalid";
}

#undef CASE

static vbi_bool
same_transponder		(struct station *	s1,
				 struct station *	s2)
{
	if (s1->frequency != s2->frequency)
		return FALSE;

	if (s1->type != s2->type)
		return FALSE;

	switch (s1->type) {
	case FE_ATSC:
		if (s1->u.atsc.modulation != s1->u.atsc.modulation)
			return FALSE;
		break;

	case FE_OFDM:
		if (s1->u.dvb_t.inversion != s2->u.dvb_t.inversion ||
		    s1->u.dvb_t.bandwidth != s2->u.dvb_t.bandwidth ||
		    s1->u.dvb_t.code_rate_HP != s2->u.dvb_t.code_rate_HP ||
		    s1->u.dvb_t.code_rate_LP != s2->u.dvb_t.code_rate_LP ||
		    s1->u.dvb_t.constellation != s2->u.dvb_t.constellation ||
		    s1->u.dvb_t.transm_mode != s2->u.dvb_t.transm_mode ||
		    s1->u.dvb_t.guard_interval != s2->u.dvb_t.guard_interval ||
		    s1->u.dvb_t.hierarchy != s2->u.dvb_t.hierarchy)
			return FALSE;
		break;

	case FE_QPSK:
	case FE_QAM:
		assert (0);
	}

	return TRUE;
}

/* We use a FIFO because the caption decoder may block for
   extended periods due to disk activity, and buffering in
   the kernel driver is unreliable in my experience. */

static void
ct_filter			(const uint8_t		buf[188])
{
	unsigned int in;
	unsigned int out;
	unsigned int free;

	/* Supposedly all modern CPUs read and write ints
	   atomically so we can avoid the mutex locking
	   overhead. */
	in = ts_buffer_in;
	out = ts_buffer_out;

	assert (in < ts_buffer_capacity);
	assert (out < ts_buffer_capacity);

	if (likely (0 == (buf[1] & 0x80))) {
		unsigned int pid;

		pid = (buf[1] * 256 + buf[2]) & 0x1FFF;

		if (0) {
			fprintf (stderr, "CT TS 0x%04x = %u\n", pid, pid);
			return;
		}

		if (pid_map[pid].program < 0) {
			if (NULL == option_ts_all_tap_file_name)
				return;
		}
	}

	free = out - in;
	if (out <= in)
		free += ts_buffer_capacity;

	if (unlikely (free <= 188)) {
		ts_error = 0x80;
		return;
	}

	memcpy (ts_buffer + in, buf, 188);

	ts_buffer[in + 1] |= ts_error;
	ts_error = 0;

	++ts_n_packets_in;

	in += 188;
	if (in >= ts_buffer_capacity)
    		in = 0;

	ts_buffer_in = in;

	/* Hm. This delays the output. */
	if (1 || free < (ts_buffer_capacity / 2)) {
		pthread_cond_signal (&dx_cond);
	}
}

static unsigned int
ct_resync			(const uint8_t		buf[2 * 188])
{
	unsigned int i;

	for (i = 1; i < 188; ++i) {
		if (0x47 == buf[i] && 0x47 == buf[i + 188])
			return i;
	}

	log (1, "Capture thread cannot synchronize.\n");

	capture_thread_id = 0;
	pthread_exit (NULL);

	return 0;
}

static vbi_bool
ct_read				(uint8_t *		buffer,
				 ssize_t		todo)
{
	unsigned int retry = 100;

	do {
		ssize_t actual;

		actual = read (dvr_fd, buffer, todo);
		if (likely (actual == todo))
			return TRUE;

		if (actual > 0) {
			if (unlikely (actual >= todo)) {
				log (1, "DVB device read size "
				     "error.\n");
				return FALSE;
			}

			buffer += actual;
			todo -= actual;

			continue;
		} else if (actual < 0) {
			int saved_errno;

			saved_errno = errno;

			if (EINTR == saved_errno)
				continue;

			log_errno (1, "DVB device read error");

			errno = saved_errno;

			return FALSE;
		} else {
			log (2, "EOF from DVB device (ignored).\n");
			return FALSE;
		}
	} while (--retry > 0);

	log (2, "DVB device read error: EINTR or "
	     "read size problem.\n");

	errno = EINTR;

	return FALSE;
}

static void *
capture_thread			(void *			arg)
{
	uint8_t *start;
	uint8_t *end;
	const uint8_t *s;
	const uint8_t *e;
	unsigned int left;
	ssize_t size;

	arg = arg; /* unused */

	log (2, "Capture thread ready.\n");

	/* Don't swap out any code or data pages. If the capture
	   thread is delayed we may lose packets. Errors ignored. */
	mlockall (MCL_CURRENT | MCL_FUTURE);

	pthread_setcanceltype (PTHREAD_CANCEL_DEFERRED, /* old */ NULL);
	pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, /* old */ NULL);

	/* Page aligned reads are not required, but who knows, it
	   may allow some DMA magic or speed up copying. */
	start = ct_buffer + 4096;
	size = ct_buffer_capacity - 4096;
	left = 0;

	assert (ct_buffer_capacity > 4096);
	assert (0 == ct_buffer_capacity % 4096);

	xioctl (dmx_fd, DMX_START, 0);

	for (;;) {
		if (!ct_read (start, size))
			continue;

		ct_n_bytes_in += size;

		end = start + size;

		s = start - left;
		e = end - 4096;

		while (s < e) {
			if (0x47 != s[0] || 0x47 != s[188]) {
				if (ts_n_packets_in > 0) {
					log (2, "Capture thread "
					     "lost sync.\n");
				}

				s += ct_resync (s);
			}

			if (0) {
				fputc ('.', stderr);
				fflush (stderr);
			} else {
				ct_filter (s);
			}

			s += 188;
		}

		left = end - s;
		memcpy (start - left, s, left);
	}

	return NULL;
}

static void
destroy_capture_state		(void)
{
	/* FIXME this a glibc feature. */
	free (ct_buffer);

	ct_buffer = NULL;
	ct_buffer_capacity = 0;
}

#ifdef HAVE_POSIX_MEMALIGN

/* posix_memalign() was introduced in POSIX 1003.1d and may not be
   implemented on all systems. */
static void *
my_memalign			(size_t			boundary,
				 size_t			size)
{
	void *p;
	int err;

	/* boundary must be a power of two. */
	if (0 != (boundary & (boundary - 1)))
		return malloc (size);

	err = posix_memalign (&p, boundary, size);
	if (0 == err)
		return p;

	errno = err;
	return NULL;
}

#elif defined HAVE_MEMALIGN
/* memalign() is a GNU extension. Due to the DVB driver interface
   this program currently runs on Linux only, but it can't hurt
   to be prepared. */
#  define my_memalign memalign
#else
#  define my_memalign(boundary, size) malloc (size)
#endif

static void
init_capture_state		(void)
{
	ct_buffer_capacity = 32 * 1024;
	ct_buffer = my_memalign (4096, ct_buffer_capacity);

	if (NULL == ct_buffer) {
		no_mem_exit ();
	}

	/* Copy-on-write and lock the pages. */
	memset (ct_buffer, -1, ct_buffer_capacity);

	ct_n_bytes_in = 0;
}

static int
xopen_device			(const char *		dev_name,
				 int			flags)
{
	struct stat st; 
	int fd;

	if (-1 == stat (dev_name, &st)) {
		goto open_failed;
	}

	if (!S_ISCHR (st.st_mode)) {
		error_exit ("'%s' is not a DVB device.\n",
			    dev_name);
	}

	fd = open (dev_name, flags, /* mode */ 0);
	if (-1 == fd) {
		goto open_failed;
	}

	return fd;

 open_failed:
	errno_exit ("Cannot open '%s'", dev_name);

	return -1; /* not reached */
}

static void
close_device			(void)
{
	log (2, "Closing DVB device.\n");

	if (0 != capture_thread_id) {
		pthread_cancel (capture_thread_id);
		pthread_join (capture_thread_id, NULL);

		capture_thread_id = 0;
	}

	destroy_capture_state ();

	if (-1 != dmx_fd) {
		close (dmx_fd);
		dmx_fd = -1;
	}

	if (-1 != dvr_fd) {
		close (dvr_fd);
		dvr_fd = -1;
	}

	if (-1 != fe_fd) {
		close (fe_fd);
		fe_fd = -1;
	}
}

static void
open_device			(void)
{
	struct dvb_frontend_info fe_info;
	struct dvb_frontend_parameters fe_param;
	struct dmx_pes_filter_params filter;
	char *dev_name;
	unsigned int retry;

	dev_name = NULL;

	init_capture_state ();

	/* Front end. */

	log (2, "Opening dvb/adapter%lu.\n",
	     option_dvb_adapter_num);

	dev_name = xasprintf ("/dev/dvb/adapter%lu/frontend%lu",
			      option_dvb_adapter_num,
			      option_dvb_frontend_id);

	fe_fd = xopen_device (dev_name, O_RDWR);

	CLEAR (fe_info);

	xioctl (fe_fd, FE_GET_INFO, &fe_info);

	switch (fe_info.type) {
	case FE_ATSC:
	case FE_OFDM:
		if (fe_info.type != station->type) {
			error_exit ("'%s' is not %s device.\n",
				    dev_name,
				    (FE_ATSC == station->type) ?
				    "an ATSC" : "a DVB-T");
		}
		break;

	case FE_QPSK:
	case FE_QAM:
		error_exit ("'%s' is not an ATSC device.\n",
			    dev_name);
		break;
	}

	CLEAR (fe_param);

	fe_param.frequency = station->frequency;

	switch (fe_info.type) {
	case FE_ATSC:
		fe_param.u.vsb.modulation =
			station->u.atsc.modulation;
		break;

	case FE_OFDM:
		fe_param.inversion =
			station->u.dvb_t.inversion;
		fe_param.u.ofdm.bandwidth =
			station->u.dvb_t.bandwidth;
		fe_param.u.ofdm.code_rate_HP =
			station->u.dvb_t.code_rate_HP;
		fe_param.u.ofdm.code_rate_LP =
			station->u.dvb_t.code_rate_LP;
		fe_param.u.ofdm.constellation =
			station->u.dvb_t.constellation;
		fe_param.u.ofdm.transmission_mode =
			station->u.dvb_t.transm_mode;
		fe_param.u.ofdm.guard_interval =
			station->u.dvb_t.guard_interval;
		fe_param.u.ofdm.hierarchy_information =
			station->u.dvb_t.hierarchy;
		break;

	case FE_QPSK:
	case FE_QAM:
		assert (0);
	}

	xioctl (fe_fd, FE_SET_FRONTEND, &fe_param);

	for (retry = 0;;) {
		fe_status_t status;

		xioctl (fe_fd, FE_READ_STATUS, &status);

		if (status & FE_HAS_LOCK)
			break;

		if (++retry > 20) {
			error_exit ("No signal detected.\n");
		}

		if (7 == (retry & 7)) {
			log (2, "Waiting for a signal.\n");
		}

		usleep (250000);
	}

	log (2, "Signal detected.\n");

	free (dev_name);

	/* DVR. */

	dev_name = xasprintf ("/dev/dvb/adapter%lu/dvr%lu",
			      option_dvb_adapter_num,
			      option_dvb_dvr_id);

	dvr_fd = xopen_device (dev_name, O_RDONLY);

	/* Not implemented? Let's try anyway. */
	xioctl_may_fail (dvr_fd, DMX_SET_BUFFER_SIZE, (void *)(4 << 20));

	free (dev_name);

	/* Demultiplexer. */

	dev_name = xasprintf ("/dev/dvb/adapter%lu/demux%lu",
			      option_dvb_adapter_num,
			      option_dvb_demux_id);

	dmx_fd = xopen_device (dev_name, O_RDWR);

	xioctl_may_fail (dmx_fd, DMX_SET_BUFFER_SIZE, (void *)(4 << 20));

	CLEAR (filter);

	/* We capture the entire transport multiplex so we can receive
	   all stations on this transponder at once and properly handle
	   transmission errors in the video ES demultiplexer. */
	filter.pid = 0x2000;
	filter.input = DMX_IN_FRONTEND;
	filter.output = DMX_OUT_TS_TAP;
	filter.pes_type = DMX_PES_OTHER;

	xioctl (dmx_fd, DMX_SET_PES_FILTER, &filter);

	free (dev_name);

	/* Start capture thread. */

	if (0 != pthread_create (&capture_thread_id,
				 /* attr */ NULL,
				 capture_thread,
				 /* arg */ NULL)) {
		errno_exit ("Cannot start capture thread");
	}

	log (2, "Opened dvb/adapter%lu, tuned to %.3f MHz and "
	     "started capture thread.\n",
	     option_dvb_adapter_num,
	     station->frequency / 1e6);
}

static enum fe_type
device_type			(void)
{
	struct dvb_frontend_info fe_info;
	char *dev_name;
	int fd;

	dev_name = xasprintf ("/dev/dvb/adapter%lu/frontend%lu",
			      option_dvb_adapter_num,
			      option_dvb_frontend_id);

	fd = xopen_device (dev_name, O_RDWR);

	CLEAR (fe_info);

	xioctl (fd, FE_GET_INFO, &fe_info);

	close (fd);

	switch (fe_info.type) {
	case FE_ATSC:
	case FE_OFDM:
		break;

	case FE_QPSK:
	case FE_QAM:
		error_exit ("'%s' is not an ATSC device.\n",
			    dev_name);
		break;
	}

	free (dev_name);

	return fe_info.type;
}


static void
list_stations			(void)
{
	const struct station *st;
	size_t max_len;

	if (NULL == station_list) {
		printf ("The channel config file is empty.\n");
		return;
	}

	/* FIXME the encoding of station names is unknown,
	   could be a multi-byte coding like UTF-8. */
	max_len = 0;
	for (st = station_list; NULL != st; st = st->next) {
		size_t len;

		len = strlen (st->name);
		max_len = MAX (max_len, len);
	}

	for (st = station_list; NULL != st; st = st->next) {
		printf ("%-*s  %3.3f MHz\n",
			(int) max_len,
			st->name,
			st->frequency / 1e6);
	}
}

static struct station *
find_station			(const char *		station_name)
{
	struct station *st;

	for (st = station_list; NULL != st; st = st->next) {
		if (0 == strcmp (station_name, st->name))
			return st;
	}

	return NULL;
}

static char *
parse_station_name		(const char **		sp,
				 int			delimiter)
{
	const char *s;
	const char *s_name;
	char *station_name;
	size_t len;

	s = *sp;
	while (isspace (*s))
		++s;
	s_name = s;
	while (0 != *s && delimiter != *s)
		++s;
	*sp = s;
	while (s > s_name && isspace (s[-1]))
		--s;
	len = s - s_name;
	if (0 == len)
		return NULL;

	station_name = xmalloc (len + 1);

	memcpy (station_name, s_name, len);
	station_name[len] = 0;

	return station_name;
}

struct key_value {
	const char *		key;
	int			value;
};

static vbi_bool
parse_enum			(int *			value,
				 const char **		sp,
				 const struct key_value *table)
{
	const char *s;
	unsigned int i;

	s = *sp;

	while (isspace (*s))
		++s;

	for (i = 0; NULL != table[i].key; ++i) {
		size_t len = strlen (table[i].key);

		if (0 == strncmp (s, table[i].key, len)) {
			s += len;
			break;
		}
	}

	if (NULL == table[i].key)
		return FALSE;

	while (isspace (*s))
		++s;

	if (':' != *s++)
		return FALSE;

	*value = table[i].value;
	*sp = s;

	return TRUE;
}

static void
parse_tzap_channel_conf_line	(const char *		filename,
				 unsigned int		line_number,
				 const char *		buffer)
{
	static const struct key_value inversion [] = {
		{ "INVERSION_OFF",	INVERSION_OFF },
		{ "INVERSION_ON",	INVERSION_ON },
		{ "INVERSION_AUTO",	INVERSION_AUTO },
		{ NULL,			0 }
	};
	static const struct key_value bandwidth [] = {
		{ "BANDWIDTH_6_MHZ",	BANDWIDTH_6_MHZ },
		{ "BANDWIDTH_7_MHZ",	BANDWIDTH_7_MHZ },
		{ "BANDWIDTH_8_MHZ",	BANDWIDTH_8_MHZ },
		{ NULL,			0 }
	};
	static const struct key_value fec [] = {
		{ "FEC_1_2",		FEC_1_2 },
		{ "FEC_2_3",		FEC_2_3 },
		{ "FEC_3_4",		FEC_3_4 },
		{ "FEC_4_5",		FEC_4_5 },
		{ "FEC_5_6",		FEC_5_6 },
		{ "FEC_6_7",		FEC_6_7 },
		{ "FEC_7_8",		FEC_7_8 },
		{ "FEC_8_9",		FEC_8_9 },
		{ "FEC_AUTO",		FEC_AUTO },
		{ "FEC_NONE",		FEC_NONE },
		{ NULL,			0 }
	};
	static const struct key_value constellation [] = {
		{ "QPSK",		QPSK },
		{ "QAM_16",		QAM_16 },
		{ "QAM_32",		QAM_32 },
		{ "QAM_64",		QAM_64 },
		{ "QAM_128",		QAM_128 },
		{ "QAM_256",		QAM_256 },
		{ NULL,			0 }
	};
	static const struct key_value transmission_mode [] = {
		{ "TRANSMISSION_MODE_2K", TRANSMISSION_MODE_2K },
		{ "TRANSMISSION_MODE_8K", TRANSMISSION_MODE_8K },
		{ NULL,			  0 }
	};
	static const struct key_value guard_interval [] = {
		{"GUARD_INTERVAL_1_16",	GUARD_INTERVAL_1_16},
		{"GUARD_INTERVAL_1_32",	GUARD_INTERVAL_1_32},
		{"GUARD_INTERVAL_1_4",	GUARD_INTERVAL_1_4},
		{"GUARD_INTERVAL_1_8",	GUARD_INTERVAL_1_8},
		{ NULL,			0 }
	};
	static const struct key_value hierarchy [] = {
		{ "HIERARCHY_1",	HIERARCHY_1 },
		{ "HIERARCHY_2",	HIERARCHY_2 },
		{ "HIERARCHY_4",	HIERARCHY_4 },
		{ "HIERARCHY_NONE",	HIERARCHY_NONE },
		{ NULL,			0 }
	};
	struct station *st;
	struct station **stp;
	const struct station *st2;
	const char *detail;
	const char *s;
	char *s_end;
	int value;

	/* TZAP channel config file format:

	   A number of lines with the fields:
	   1. Station name (encoding?)
	   2. Transponder frequency in Hz
	   3. Inversion: INVERSION_(ON|OFF|AUTO)
	   4. Bandwidth: BANDWIDTH_(6|7|8)_MHZ
	   5. Code rate HP: FEC_(1_2|2_3|3_4|4_5|5_6|6_7|7_8|8_9|AUTO|NONE)
	   6. Code rate LP: as above
	   7. Constellation: QPSK, QAM_(16|32|64|128|256)
	   8. Transmission mode: TRANSMISSION_MODE_(2K|8K)
	   9. Guard interval: GUARD_INTERVAL_1_(4|8|16|32)
	   10. Hierarchy information: HIERARCHY_(1|2|4|NONE)
	   11. Video stream PID
	   12. Audio stream PID
	   13. ?

	   The fields are separated by one colon. We skip whitespace
	   at the beginning of a line, whitespace before and after
	   colons, empty lines, and lines starting with a number
	   sign. */

	st = xmalloc (sizeof (*st));

	CLEAR (*st);

	s = buffer;

	while (isspace (*s))
		++s;
	if (0 == *s || '#' == *s)
		return;

	detail = "station name";
	st->name = parse_station_name (&s, /* delimiter */ ':');
	if (NULL == st->name)
		goto invalid;
	if (':' != *s++)
		goto invalid;

	st2 = find_station (st->name);
	if (NULL != st2) {
		error_exit ("Duplicate station name '%s' "
			    "in %s line %u.\n",
			    st->name, filename, line_number);
	}

	st->type = FE_OFDM;

	detail = "frequency";
	/* NB. strtoul() skips leading whitespace. */
	st->frequency = strtoul (s, &s_end, 0);
	if (s_end == s || st->frequency < 1)
		goto invalid;
	s = s_end;
	while (isspace (*s))
		++s;
	if (':' != *s++)
		goto invalid;

	detail = "inversion";
	if (!parse_enum (&value, &s, inversion))
		goto invalid;
	st->u.dvb_t.inversion = value;

	detail = "bandwidth";
	if (!parse_enum (&value, &s, bandwidth))
		goto invalid;
	st->u.dvb_t.bandwidth = value;

	detail = "code rate HP";
	if (!parse_enum (&value, &s, fec))
		goto invalid;
	st->u.dvb_t.code_rate_HP = value;

	detail = "code rate LP";
	if (!parse_enum (&value, &s, fec))
		goto invalid;
	st->u.dvb_t.code_rate_LP = value;

	detail = "constellation";
	if (!parse_enum (&value, &s, constellation))
		goto invalid;
	st->u.dvb_t.constellation = value;

	detail = "transmission_mode";
	if (!parse_enum (&value, &s, transmission_mode))
		goto invalid;
	st->u.dvb_t.transm_mode = value;

	detail = "guard_interval";
	if (!parse_enum (&value, &s, guard_interval))
		goto invalid;
	st->u.dvb_t.guard_interval = value;

	detail = "hierarchy";
	if (!parse_enum (&value, &s, hierarchy))
		goto invalid;
	st->u.dvb_t.hierarchy = value;

	detail = "video PID";
	st->video_pid = strtoul (s, &s_end, 0);
	if (s_end == s || (unsigned int) st->video_pid > 0x1FFE)
		goto invalid;
	s = s_end;
	while (isspace (*s))
		++s;
	if (':' != *s++)
		goto invalid;

	detail = "audio PID";
	st->audio_pid = strtoul (s, &s_end, 0);
	if (s_end == s || (unsigned int) st->audio_pid > 0x1FFE)
		goto invalid;
	s = s_end;
	while (isspace (*s))
		++s;
	if (':' != *s++)
		goto invalid;
	if (0 == st->video_pid) {
		if (option_debug & DEBUG_CONFIG) {
			fprintf (stderr, "Skipping radio station '%s'.\n",
				 st->name);
		}
		free (st->name);
		free (st);
		return;
	}

	if (option_debug & DEBUG_CONFIG) {
		fprintf (stderr, "%3u: station_name='%s' frequency=%lu "
			 "inversion=%s bandwidth=%s code_rate=%s/%s "
			 "constellation=%s transm_mode=%s "
			 "guard_interval=%s hierarchy=%s "
			 "video_pid=%u audio_pid=%u.\n",
			 line_number,
			 st->name,
			 st->frequency,
			 fe_spectral_inversion_name (st->u.dvb_t.inversion),
			 fe_bandwidth_name (st->u.dvb_t.bandwidth),
			 fe_code_rate_name (st->u.dvb_t.code_rate_HP),
			 fe_code_rate_name (st->u.dvb_t.code_rate_LP),
			 fe_modulation_name (st->u.dvb_t.constellation),
			 fe_transmit_mode_name (st->u.dvb_t.transm_mode),
			 fe_guard_interval_name (st->u.dvb_t.guard_interval),
			 fe_hierarchy_name (st->u.dvb_t.hierarchy),
			 st->video_pid,
			 st->audio_pid);
	}

	/* Append to station list. */
	for (stp = &station_list; NULL != *stp; stp = &(*stp)->next)
		;
	*stp = st;

	return;

 invalid:
	error_exit ("Invalid %s field in '%s' line %u.\n",
		    detail, filename, line_number);
}

static void
parse_azap_channel_conf_line	(const char *		filename,
				 unsigned int		line_number,
				 const char *		buffer)
{
	static const struct key_value modulations [] = {
		{ "8VSB",	VSB_8 },
		{ "16VSB",	VSB_16 },
		{ "QAM_64",	QAM_64 },
		{ "QAM_256",	QAM_256 },
		{ NULL,		0 }
	};
	struct station *st;
	struct station **stp;
	const struct station *st2;
	const char *detail;
	const char *s;
	char *s_end;
	int value;

	/* AZAP channel config file format:

	   A number of lines with the fields:
	   1. Station name (encoding?)
	   2. Transponder frequency in Hz
	   3. Modulation: 8VSB, 16VSB, QAM_64, QAM_256
	   4. Video stream PID
	   5. Audio stream PID (one or more?)
	   6. Stream ID?

	   The fields are separated by one colon. We skip whitespace
	   at the beginning of a line, whitespace before and after
	   colons, empty lines, and lines starting with a number
	   sign. */

	st = xmalloc (sizeof (*st));

	CLEAR (*st);

	s = buffer;

	while (isspace (*s))
		++s;
	if (0 == *s || '#' == *s)
		return;

	detail = "station name";
	st->name = parse_station_name (&s, /* delimiter */ ':');
	if (NULL == st->name)
		goto invalid;
	if (':' != *s++)
		goto invalid;

	st2 = find_station (st->name);
	if (NULL != st2) {
		error_exit ("Duplicate station name '%s' "
			    "in %s line %u.\n",
			    st->name, filename, line_number);
	}

	st->type = FE_ATSC;

	detail = "frequency";
	/* NB. strtoul() skips leading whitespace. */
	st->frequency = strtoul (s, &s_end, 0);
	if (s_end == s || st->frequency < 1)
		goto invalid;
	s = s_end;
	while (isspace (*s))
		++s;
	if (':' != *s++)
		goto invalid;

	detail = "modulation";
	if (!parse_enum (&value, &s, modulations))
		goto invalid;
	st->u.atsc.modulation = value;

	detail = "video PID";
	st->video_pid = strtoul (s, &s_end, 0);
	if (s_end == s || (unsigned int) st->video_pid > 0x1FFE)
		goto invalid;
	s = s_end;
	while (isspace (*s))
		++s;
	if (':' != *s++)
		goto invalid;

	detail = "audio PID";
	st->audio_pid = strtoul (s, &s_end, 0);
	if (s_end == s || (unsigned int) st->audio_pid > 0x1FFE)
		goto invalid;
	s = s_end;
	while (isspace (*s))
		++s;
	if (':' != *s++)
		goto invalid;
	if (0 == st->video_pid) {
		if (option_debug & DEBUG_CONFIG) {
			fprintf (stderr, "Skipping radio station '%s'.\n",
				 st->name);
		}
		free (st->name);
		free (st);
		return;
	}

	if (option_debug & DEBUG_CONFIG) {
		fprintf (stderr, "%3u: station_name='%s' frequency=%lu "
			 "modulation=%s video_pid=%u audio_pid=%u.\n",
			 line_number,
			 st->name,
			 st->frequency,
			 fe_modulation_name (st->u.atsc.modulation),
			 st->video_pid,
			 st->audio_pid);
	}

	/* Append to station list. */
	for (stp = &station_list; NULL != *stp; stp = &(*stp)->next)
		;
	*stp = st;

	return;

 invalid:
	error_exit ("Invalid %s field in '%s' line %u.\n",
		    detail, filename, line_number);
}

static char *
get_channel_conf_name		(enum fe_type		type)
{
	const char *home;
	const char *s;

 	s = option_channel_conf_file_name;

	if (NULL == s) {
		switch (type) {
		case FE_ATSC:
			s = "~/.azap/channels.conf";
			break;

		case FE_OFDM:
			s = "~/.tzap/channels.conf";
			break;

		case FE_QPSK:
		case FE_QAM:
			assert (0);
			break;
		}
	}

	if (0 == strncmp (s, "~/", 2)) {
		home = getenv ("HOME");
		if (NULL == home) {
			error_exit ("Cannot open '%s' because the "
				    "HOME environment variable "
				    "is unset.\n",
				    s);
		}

		++s;
	} else {
		home = "";
	}

	return xasprintf ("%s%s", home, s);
}

static void
read_channel_conf		(void)
{
	char buffer[256];
	char *channel_conf_name;
	enum fe_type type;
	unsigned int line_number;
	FILE *fp;

	if (NULL != station_list)
		return;

	type = option_dvb_type;
	if ((enum fe_type) -1 == option_dvb_type)
		type = device_type ();

	channel_conf_name = get_channel_conf_name (type);

	fp = fopen (channel_conf_name, "r");
	if (NULL == fp) {
		errno_exit ("Cannot open '%s'",
			    channel_conf_name);
	}

	if (option_debug & DEBUG_CONFIG) {
		fprintf (stderr, "Opened '%s' (%s):\n",
			 channel_conf_name,
			 fe_type_name (type));
	}

	line_number = 1;

	while (NULL != fgets (buffer, sizeof (buffer), fp)) {
		const char *s;

		s = buffer;
		while (isspace (*s))
			++s;
		if (0 == *s || '#' == *s)
			continue;

		switch (type) {
		case FE_ATSC:
			parse_azap_channel_conf_line (channel_conf_name,
						      line_number,
						      buffer);
			break;

		case FE_OFDM:
			parse_tzap_channel_conf_line (channel_conf_name,
						      line_number,
						      buffer);
			break;

		case FE_QPSK:
		case FE_QAM:
			assert (0);
		}

		++line_number;
	}

	if (ferror (fp) || 0 != fclose (fp)) {
		errno_exit ("Error while reading '%s'",
			    channel_conf_name);
	}

	free (channel_conf_name);
}

static void
usage				(FILE *			fp)
{
	fprintf (fp, "\
" PROGRAM " " VERSION " -- ATSC Closed Caption and XDS decoder\n\
Copyright (C) 2008 Michael H. Schimek <mschimek@users.sf.net>\n\
Based on code by Mike Baker, Mark K. Kim and timecop@japan.co.jp.\n\
This program is licensed under GPL 2 or later. NO WARRANTIES.\n\n\
Usage: %s [options] [-n] station name\n\
Options:\n\
-? | -h | --help | --usage     Print this message, then terminate\n\
-1 ... -4 | --cc1-file ... --cc4-file file name\n\
                               Append CC1 ... CC4 to this file\n\
-5 ... -8 | --t1-file ... --t4-file file name\n\
                               Append T1 ... T4 to this file\n\
-9 ... -0 | --s1-file ... --s2-file file name\n\
                               Append DTVCC service 1 ... 2 to this file\n\
-a | --adapter-num number      DVB device adapter [%lu]\n\
-b | --no-webtv                Do not print WebTV links\n\
-c | --cc                      Print Closed Caption (includes WebTV)\n\
-d | --demux-id number         DVB device demultiplexer [%lu]\n\
-e | --channel-conf file name  Channel config. file [~/.azap/channels.conf]\n\
-f | --filter type[,type]*     Select XDS info: all, call, desc, length,\n\
                               network, rating, time, timecode, timezone,\n\
                               title. Multiple -f options accumulate. [all]\n\
-i | --frontend-id number      DVB device frontend [%lu]\n\
-j | --format type             Print caption in 'plain' encoding, with\n\
                               'vt100' control codes or like the 'ntsc-cc'\n\
                               tool [ntsc-cc].\n\
-l | --channel number          Select caption channel 1 ... 4 [nothing]\n\
-m | --timestamps              Prepend timestamps to caption lines\n\
-n | --station name            Station name. Usually the -n can be omitted\n\
-q | --quiet		       Suppress all progress and error messages\n\
-p | --plain                   Same as -j plain.\n\
-r | --dvr-id number           DVB device dvr [%lu]\n\
-s | --sentences               Decode caption by sentences\n\
-v | --verbose                 Increase verbosity\n\
-x | --xds                     Print XDS info\n\
-C | --cc-file file name       Append all caption to this file [stdout]\n\
-L | --list                    List all TV stations in the channel\n\
                               configuration file\n\
-T | --ts                      Decode a DVB Transport Stream on stdin\n\
                               instead of opening a DVB device\n\
-X | --xds-file file name      Append XDS info to this file [stdout]\n\
\n\
To record data from multiple stations sharing a transponder frequency\n\
you can specify caption options and a station name repeatedly.\n\
",
		 my_name,
		 option_dvb_adapter_num,
		 option_dvb_demux_id,
		 option_dvb_frontend_id,
		 option_dvb_dvr_id);
}

static const char
short_options [] = ("-?1:2:3:4:5:6:7:8:9:0:"
		    "a:bcd:e:f:hi:j:l:mn:pr:svx"
		    "C:DELM:PTX:");

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options [] = {
	{ "help",		no_argument,		NULL,	'?' },
	/* From ntsc-cc.c. */
	{ "cc1-file",		required_argument,	NULL,	'1' },
	{ "cc2-file",		required_argument,	NULL,	'2' },
	{ "cc3-file",		required_argument,	NULL,	'3' },
	{ "cc4-file",		required_argument,	NULL,	'4' },
	/* From ntsc-cc.c. */
	{ "t1-file",		required_argument,	NULL,	'5' },
	{ "t2-file",		required_argument,	NULL,	'6' },
	{ "t3-file",		required_argument,	NULL,	'7' },
	{ "t4-file",		required_argument,	NULL,	'8' },
	{ "s1-file",		required_argument,	NULL,	'9' },
	{ "s2-file",		required_argument,	NULL,	'0' },
	{ "adapter-num",	required_argument,	NULL,	'a' },
	/* From ntsc-cc.c. */
	{ "no-webtv",		no_argument,		NULL,	'b' },
	/* From ntsc-cc.c. */
	{ "cc",			no_argument,		NULL,	'c' },
	{ "demux-id",		required_argument,	NULL,	'd' },
	{ "conf-file",		required_argument,	NULL,	'e' },
	/* From ntsc-cc.c. */
	{ "filter",		required_argument,	NULL,	'f' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "usage",		no_argument,		NULL,	'h' },
	{ "frontend-id",	required_argument,	NULL,	'i' },
	{ "format",		required_argument,	NULL,	'j' },
	{ "channel",		required_argument,	NULL,	'l' },
	{ "timestamps",		no_argument,		NULL,	'm' },
	{ "station",		required_argument,	NULL,	'n' },
	/* From ntsc-cc.c. */
	{ "plain",		no_argument,		NULL,	'p' },
	/* From ntsc-cc.c. Actually the output was never limited to ASCII. */
	{ "plain-ascii",	no_argument,		NULL,	'p' },
	{ "quiet",		no_argument,		NULL,	'q' },
	{ "dvr-id",		required_argument,	NULL,	'r' },
	/* From ntsc-cc.c. */
	{ "sentences",		no_argument,		NULL,	's' },
	{ "verbose",		no_argument,		NULL,	'v' },
	/* From ntsc-cc.c. */
	{ "xds",		no_argument,		NULL,	'x' },
	/* From ntsc-cc.c. */
	{ "cc-file",		required_argument,	NULL,	'C' },
	/* 'E' - video elementary stream? */
	{ "list",		no_argument,		NULL,	'L' },
	{ "minicut",		required_argument,	NULL,	'M' },
	{ "pes",		no_argument,		NULL,	'P' },
	{ "ts",			no_argument,		NULL,	'T' },
	/* From ntsc-cc.c. */
	{ "xds-file",		required_argument,	NULL,	'X' },

	/* Test options, may change. */

	{ "atsc",		no_argument,		NULL,	301 },
	{ "dvb-t",		no_argument,		NULL,	302 },
	{ "ts-all-tap",		required_argument,	NULL,	303 },
	{ "ts-tap",		required_argument,	NULL,	304 },
	{ "video-all-tap",	required_argument,	NULL,	305 },
	{ "video-tap",		required_argument,	NULL,	306 },
	{ "cc-data-tap",	required_argument,	NULL,	308 },
	{ "debug",		required_argument,	NULL,	309 },
	{ "mtest",		no_argument,		NULL,	310 },
	{ "cc-data",		no_argument,		NULL,	311 },
	{ "es",			no_argument,		NULL,	312 },
	{ 0, 0, 0, 0 }
};
#else
#  define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

static int			option_index;

static void
list_programs			(void)
{
	const unsigned int ll = 1; /* log level */
	unsigned int i;

	for (i = 0; i < n_programs; ++i) {
		struct program *pr;
		unsigned int j;

		pr = &program_table[i];
		log (ll, "Station %u: '%s'\n",
		     station_num (pr),
		     pr->option_station_name);
		for (j = 0; j < 10; ++j) {
			const char *stream_name [10] = {
				"NTSC CC1", "NTSC CC2",
				"NTSC CC3", "NTSC CC4",
				"NTSC T1", "NTSC T2",
				"NTSC T3", "NTSC T4",
				"ATSC S1", "ATSC S2"
			};
			const char *file_name;

			if (0 == (pr->cr.option_caption_mask & (1 << j)))
				continue;

			file_name = pr->cr.option_caption_file_name[j];
			if (NULL != file_name) {
				log (ll, "  %s -> '%s'\n",
				     stream_name[j], file_name);
			} else if (NULL != pr->option_minicut_dir_name) {
				log (ll, "  %s -> '%s/"
				     "YYYYMMDDHH0000/"
				     "YYYYMMDDHHMM00%s.txt'\n",
				     stream_name[j],
				     pr->option_minicut_dir_name,
				     cr_file_name_suffix[j]);
			}
		}
		if (pr->cr.usexds) {
			log (ll, "  XDS -> '%s'\n",
			     pr->cr.option_xds_output_file_name);
		}
		if (NULL != pr->option_minicut_dir_name) {
				log (ll, "  TS -> '%s/"
				     "YYYYMMDDHH0000/"
				     "YYYYMMDDHHMM00.ts'\n",
				     pr->option_minicut_dir_name);
		}
		if (NULL != pr->vesd.video_es_tap_fp) {
			const char *name;

			name = pr->vesd.option_video_es_all_tap_file_name;
			if (NULL == name)
				name = pr->vesd.option_video_es_tap_file_name;
			log (ll, "  V-ES -> '%s'\n",
			     name);
		}
		if (NULL != pr->vr.aesp.audio_es_tap_fp) {
			log (ll, "  A-ES -> '%s'\n",
			     pr->vr.aesp.option_audio_es_tap_file_name);
		}
		if (NULL != pr->cr.ccd.cc_data_tap_fp) {
			log (ll, "  cc_data -> '%s'\n",
			     pr->cr.ccd.option_cc_data_tap_file_name);
		}
	}
}

static void
cr_open_xds_output_file		(struct caption_recorder *cr)
{
	cr->xds_fp = open_output_file (cr->option_xds_output_file_name);
}

static void
cr_open_caption_output_files	(struct caption_recorder *cr)
{
	unsigned int i;

	for (i = 0; i < 10; ++i) {
		const char *name;

		name = cr->option_caption_file_name[i];
		if (cr->option_caption_mask & (1 << i)
		    && NULL != name) {
			cr->caption_fp[i] = open_output_file (name);
		}
	}
}

static void
open_output_files		(void)
{
	unsigned int i;

	for (i = 0; i < n_programs; ++i) {
		struct program *pr;

		pr = &program_table[i];

		if (pr->cr.usecc)
			cr_open_caption_output_files (&pr->cr);

		if (pr->cr.usexds)
			cr_open_xds_output_file (&pr->cr);
	}
}

static void
look_up_station_names		(void)
{
	unsigned int i;

	read_channel_conf ();

	for (i = 0; i < n_programs; ++i) {
		struct program *pr;
		struct station *st;

		pr = &program_table[i];

		st = find_station (pr->option_station_name);
		if (NULL == st) {
			error_exit ("Station '%s' is unknown. List "
				    "all stations with the -L option.\n",
				    pr->option_station_name);
		}

		if (NULL == station) {
			station = st;
		} else if (!same_transponder (st, station)) {
			error_exit ("To receive multiple programs the "
				    "stations must share one "
				    "transponder frequency.\n");
		}

		pr->tsd.pid[0] = st->video_pid;
		pr->tsd.pid[1] = st->audio_pid;

		assert (st->video_pid <= N_ELEMENTS (pid_map));
		assert (st->audio_pid <= N_ELEMENTS (pid_map));

		pid_map[st->video_pid].program = i;
		pid_map[st->audio_pid].program = i;
	}
}

static void
finish_program_setup		(struct program *	pr,
				 vbi_bool		have_cc_filter_option,
				 vbi_bool		have_xds_filter_option)
{
	if (NULL != pr->option_minicut_dir_name) {
		pr->cr.usecc = TRUE;
		if (0 == pr->cr.option_caption_mask)
			pr->cr.option_caption_mask = 0x30F;
	} else {
		unsigned int i;

		if (!(pr->cr.usecc | pr->cr.usexds)) {
			error_exit ("Please give option -c or -x, "
				    "or -h for help.\n");
		}

		if (pr->cr.usecc && !have_cc_filter_option)
			pr->cr.option_caption_mask = 0x001;

		for (i = 0; i < 10; ++i) {
			if (0 != (pr->cr.option_caption_mask & (1 << i))
			    && NULL == pr->cr.option_caption_file_name[i]) {
				pr->cr.option_caption_file_name[i] = "-";
			}
		}
	}

	if (pr->cr.usexds && !have_xds_filter_option)
		xds_filter_option (&pr->cr, "all");

	if (NULL != pr->vesd.option_video_es_all_tap_file_name) {
		pr->vesd.video_es_tap_fp = open_output_file
			(pr->vesd.option_video_es_all_tap_file_name);
	} else if (NULL != pr->vesd.option_video_es_tap_file_name) {
		pr->vesd.video_es_tap_fp = open_output_file
			(pr->vesd.option_video_es_tap_file_name);
	}

	if (NULL != pr->vr.aesp.option_audio_es_tap_file_name) {
		pr->vr.aesp.audio_es_tap_fp = open_output_file
			(pr->vr.aesp.option_audio_es_tap_file_name);
	}

	if (NULL != pr->cr.ccd.option_cc_data_tap_file_name) {
		pr->cr.ccd.cc_data_tap_fp = open_output_file
			(pr->cr.ccd.option_cc_data_tap_file_name);
	}
}

static unsigned int
uint_option			(const char *		option_name,
				 const char *		optarg)
{
	const char *s;
	char *end;
	unsigned long ul;

	s = optarg;
	while (isspace (*s))
		++s;
	if (!isdigit (*s))
		goto failed;
	ul = strtoul (optarg, &end, 0);
	while (isspace (*end))
		++end;
	if (0 == *end && ul < UINT_MAX)
		return ul;

 failed:
	error_exit ("Invalid %s '%s'.\n", option_name, optarg);

	return 0;
}

static void
format_option			(struct caption_recorder *cr,
				 const char *		optarg)
{
	static const struct {
		const char *		name;
		enum caption_format	format;
	} formats [] = {
		{ "plain",		FORMAT_PLAIN },
		{ "vt100",		FORMAT_VT100 },
		{ "ntsc-cc",		FORMAT_NTSC_CC }
	};
	unsigned int i;

	for (i = 0; i < N_ELEMENTS (formats); ++i) {
		if (NULL != optarg
		    && 0 == strcmp (optarg, formats[i].name)) {
			cr->option_caption_format = formats[i].format;
			break;
		}
	}

	if (i >= N_ELEMENTS (formats)) {
		error_exit ("Invalid caption format '%s'. "
			    "Try 'plain', 'vt100' or 'ntsc-cc'.\n",
			    optarg);
	}

	if (FORMAT_PLAIN == cr->option_caption_format) {
		cr->xds_info_prefix = "% ";
		cr->xds_info_suffix = "\n";
	} else {
		cr->xds_info_prefix = "\33[33m% ";
		cr->xds_info_suffix = "\33[0m\n";
	}
}

static void
debug_option			(const char *		optarg)
{
	static const struct {
		const char *	name;
		unsigned int	flag;
	} flags [] = {
		{ "all",		-1 },
		{ "ccdata",		DEBUG_CC_DATA },
		{ "ccdec",		DEBUG_CC_DECODER },
		{ "ccf1",		DEBUG_CC_F1 },
		{ "ccf2",		DEBUG_CC_F2 },
		{ "conf",		DEBUG_CONFIG },
		{ "dtvccp",		DEBUG_DTVCC_PACKET },
		{ "dtvccpc",		DEBUG_DTVCC_PUT_CHAR },
		{ "dtvccse",		DEBUG_DTVCC_SE },
		{ "dtvccsev",		DEBUG_DTVCC_STREAM_EVENT },
		{ "vesdcc",		DEBUG_VESD_CC_DATA },
		{ "vesdpe",		DEBUG_VESD_PIC_EXT },
		{ "vesdph",		DEBUG_VESD_PIC_HDR },
 		{ "vesdpesp",		DEBUG_VESD_PES_PACKET },
 		{ "vesdsc",		DEBUG_VESD_START_CODE },
 		{ "vesdud",		DEBUG_VESD_USER_DATA },
	};
	const char *s;

	if (NULL == optarg) {
		option_debug = -1;
		return;
	} else if (0 == strcmp (optarg, "help")) {
		unsigned int i;

		printf ("Debugging switches:\n");

		for (i = 0; i < N_ELEMENTS (flags); ++i) {
			printf ("  %s\n", flags[i].name);
		}

		exit (EXIT_SUCCESS);
	}

	s = optarg;

	while (isspace (*s))
		++s;

	while (0 != *s) {
		unsigned int i;

		for (i = 0; i < N_ELEMENTS (flags); ++i) {
			const char *t;
			const char *u;

			t = s;
			u = flags[i].name;
			for (;;) {
				if (0 == *t || ',' == *t || ' ' == *t) {
					if (0 != *u)
						break;
					option_debug |= flags[i].flag;
					s = t;
					goto next_item;
				} else if ('-' == *t || '_' == *t) {
					++t;
					continue;
				} else if (*t++ != *u++) {
					break;
				}
			}
		}

		error_exit ("Invalid debugging switch '%s'. "
			    "Try --debug help.\n", optarg);

	next_item:
		while (',' == *s || isspace (*s))
			++s;
	}
}

static struct program *
add_program			(void)
{
	struct program *pr;

	if (n_programs >= N_ELEMENTS (program_table)) {
		error_exit ("Sorry, too many programs.\n");
	}

	pr = &program_table[n_programs++];

	init_program (pr);

	return pr;
}

static void
parse_args			(int			argc,
				 char **		argv)
{
	struct program *pr;
	vbi_bool have_cc_filter_option;
	vbi_bool have_xds_filter_option;
	unsigned int n_program_options;

	option_source = SOURCE_DVB_DEVICE;

	option_dvb_type = -1; /* query device */

	option_dvb_adapter_num = 0;
	option_dvb_frontend_id = 0;
	option_dvb_demux_id = 0;
	option_dvb_dvr_id = 0;

	option_verbosity = 1;

	pr = add_program ();

	format_option (&pr->cr, "ntsc-cc");

	have_xds_filter_option = FALSE;	
	have_cc_filter_option = FALSE;
	n_program_options = 0;

	for (;;) {
		int c;

		c = getopt_long (argc, argv, short_options,
				 long_options, &option_index);
		if (-1 == c)
			break;

		switch (c) {
			const char *name;
			unsigned int i;
			long ch;

		case '?':
		case 'h':
			usage (stdout);
			exit (EXIT_SUCCESS);

		case '0' ... '9':
			assert (NULL != optarg);
			if ('0' == c)
				i = 9;
			else
				i = c - '1';
			pr->cr.option_caption_file_name[i] = optarg;
			pr->cr.option_caption_mask |= 1 << i;
			have_cc_filter_option = TRUE;
			++n_program_options;
			pr->cr.usecc = 1;
			break;

		case 'a':
			assert (NULL != optarg);
			option_dvb_adapter_num =
				uint_option ("DVB adapter number",
					     optarg);
			break;

		case 'b':
			pr->cr.usewebtv = 0; /* sic, compatibility */
			++n_program_options;
			break;

		case 'c':
			pr->cr.usecc = 1;
			++n_program_options;
			break;

		case 'd':
			assert (NULL != optarg);
			option_dvb_demux_id =
				uint_option ("DVB demux device number",
					     optarg);
			break;

		case 'e':
			assert (NULL != optarg);
			option_channel_conf_file_name = optarg;
			break;

		case 'f':
			pr->cr.usexds = TRUE;
			xds_filter_option (&pr->cr, optarg);
			have_xds_filter_option = TRUE;
			++n_program_options;
			break;

		case 'i':
			assert (NULL != optarg);
			option_dvb_frontend_id =
				uint_option ("DVB frontend device number",
					     optarg);
			break;

		case 'j':
			assert (NULL != optarg);
			format_option (&pr->cr, optarg);
			++n_program_options;
			break;

		case 'l':
			assert (NULL != optarg);
			ch = strtol (optarg, NULL, 0);
			if (ch < 1 || ch > 10) {
				error_exit ("Invalid caption stream "
					    "number %ld. The valid "
					    "range is 1 ... 10.\n",
					    ch);
			}
			pr->cr.option_caption_mask |= 1 << (ch - 1);
			have_cc_filter_option = TRUE;
			++n_program_options;
			pr->cr.usecc = 1;
			break;

		case 'm':
			pr->cr.option_caption_timestamps = TRUE;
			++n_program_options;
			break;

		case '\1': /* not an option */
			/* NB this is a GNU extension, hence the
			   -n option. */

		case 'n':
			assert (NULL != optarg);
			name = optarg;
			if (NULL == pr->option_station_name) {
				pr->option_station_name = name;
				if (0 == n_program_options)
					break;
				name = NULL;
			}
			finish_program_setup (pr,
					      have_cc_filter_option,
					      have_xds_filter_option);
			pr = add_program ();
			pr->option_station_name = name;
			have_xds_filter_option = FALSE;	
			have_cc_filter_option = FALSE;
			n_program_options = 0;
			break;

		case 'p':
			format_option (&pr->cr, "plain");
			++n_program_options;
			break;

		case 'q':
			option_verbosity = 0;
			break;

		case 'r':
			assert (NULL != optarg);
			option_dvb_dvr_id =
				uint_option ("DVB DVR device number",
					     optarg);
			break;

		case 's':
			pr->cr.usesen = 1;
			++n_program_options;
			break;

		case 'v':
			++option_verbosity;
			break;

		case 'x':
			pr->cr.usexds = 1;
			++n_program_options;
			break;

		case 'C':
			assert (NULL != optarg);
			for (i = 0; i < 10; ++i) {
				pr->cr.option_caption_file_name[i] =
					optarg;
			}
			pr->cr.usecc = 1;
			++n_program_options;
			break;

		case 'L':
			read_channel_conf ();
			list_stations ();
			exit (EXIT_SUCCESS);

		case 'M':
			assert (NULL != optarg);
			pr->option_minicut_dir_name = optarg;
			++n_program_options;
			break;

		case 'P':
			option_source = SOURCE_STDIN_PES;
			break;

		case 'T':
			option_source = SOURCE_STDIN_TS;
			break;

		case 'X':
			assert (NULL != optarg);
			pr->cr.option_xds_output_file_name = optarg;
			++n_program_options;
			break;

			/* Test options. */

		case 301:
			option_dvb_type = FE_ATSC;
			break;

		case 302:
			option_dvb_type = FE_OFDM;
			break;

		case 303:
			assert (NULL != optarg);
			option_ts_all_tap_file_name = optarg;
			break;

		case 304:
			assert (NULL != optarg);
			option_ts_tap_file_name = optarg;
			break;

		case 305:
			assert (NULL != optarg);
			pr->vesd.option_video_es_all_tap_file_name = optarg;
			++n_program_options;
			break;

		case 306:
			assert (NULL != optarg);
			pr->vesd.option_video_es_tap_file_name = optarg;
			++n_program_options;
			break;


		case 307:
			assert (NULL != optarg);
			pr->vr.aesp.option_audio_es_tap_file_name = optarg;
			++n_program_options;
			break;

		case 308:
			assert (NULL != optarg);
			pr->cr.ccd.option_cc_data_tap_file_name = optarg;
			++n_program_options;
			break;

		case 309:
			assert (NULL != optarg);
			debug_option (optarg);
			break;

		case 310:
			option_minicut_test = TRUE;
			break;

		case 311:
			option_source = SOURCE_STDIN_CC_DATA;
			break;

		case 312:
			option_source = SOURCE_STDIN_VIDEO_ES;
			break;

		default:
			usage (stderr);
			exit (EXIT_FAILURE);
		}
	}

	if (pr->cr.usesen
	    && (pr->cr.option_caption_timestamps
		|| NULL != pr->option_minicut_dir_name)) {
		error_exit ("Sorry, option -s does not "
			    "combine with -m or -M.\n");
	}

	if (NULL == pr->option_station_name) {
		if (0 == n_program_options) {
			--n_programs;
			if (SOURCE_DVB_DEVICE == option_source
			    || SOURCE_STDIN_TS == option_source) {
				if (0 == n_programs)
					goto no_station;
			}
		} else {
			if (SOURCE_DVB_DEVICE == option_source
			    || SOURCE_STDIN_TS == option_source) {
				if (optind == argc)
					goto no_station;

				pr->option_station_name = argv[optind];
			}

			finish_program_setup (pr,
					      have_cc_filter_option,
					      have_xds_filter_option);
		}
	} else {
		finish_program_setup (pr,
				      have_cc_filter_option,
				      have_xds_filter_option);
	}

	if (SOURCE_DVB_DEVICE == option_source
	    || SOURCE_STDIN_TS == option_source) {
		look_up_station_names ();
	} else {
		if (n_programs > 1) {
			goto too_many_stations;
		} else if (NULL != program_table[0].option_station_name) {
			log (1, "Ignoring station name.\n");
		}
	}

	return;

 no_station:
	error_exit ("Please give a station name. "
		    "List all stations with the -L option.\n");

 too_many_stations:
	error_exit ("Sorry, only one program can be decoded with "
		    "the --cc-data, --es or\n"
		    "--pes option.\n");
}

int
main				(int			argc,
				 char **		argv)
{
	my_name = argv[0];

	setlocale (LC_ALL, "");

	locale_codeset = vbi_locale_codeset (),

	/* Don't swap out any code or data pages (if we have the
	   privilege). If the capture thread is delayed we may lose
	   packets. Errors ignored. */
	mlockall (MCL_CURRENT | MCL_FUTURE);

	memset (pid_map, -1, sizeof (pid_map));

	parse_args (argc, argv);

	init_demux_state ();

	if (NULL != option_ts_all_tap_file_name) {
		ts_tap_fp = open_output_file (option_ts_all_tap_file_name);
	} else if (NULL != option_ts_tap_file_name) {
		ts_tap_fp = open_output_file (option_ts_tap_file_name);
	}

	switch (option_source) {
	case SOURCE_DVB_DEVICE:
		open_device ();
		open_output_files ();
		if (n_programs > 1)
			list_programs ();
		demux_thread (/* arg */ NULL);
		close_device ();
		break;

	case SOURCE_STDIN_TS:
		open_output_files ();
		if (n_programs > 1)
			list_programs ();
		ts_test_loop ("-");
		break;

	case SOURCE_STDIN_PES:
		error_exit ("Sorry, the --pes option "
			    "is not implemented yet.\n");
		break;

	case SOURCE_STDIN_VIDEO_ES:
		/* For tests only. */
		open_output_files ();
		video_es_test_loop (&program_table[0], "-");
		break;

	case SOURCE_STDIN_CC_DATA:
		/* For tests only. */
		open_output_files ();
		cc_data_test_loop (&program_table[0], "-");
		break;
	}

	destroy_demux_state ();

	exit (EXIT_SUCCESS);
}
