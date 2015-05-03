/*
 *  libzvbi -- VBI decoding library
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
 *  Copyright (C) 2000, 2001 Iñaki García Etxebarria
 *
 *  Originally based on AleVT 1.5.1 by Edgar Toernig
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

/* Generated file, do not edit! */

#ifndef __LIBZVBI_H__
#define __LIBZVBI_H__

#define VBI_VERSION_MAJOR 0
#define VBI_VERSION_MINOR 2
#define VBI_VERSION_MICRO 35

#ifdef __cplusplus
extern "C" {
#endif


typedef struct vbi_decoder vbi_decoder;

/* macros.h */

#if __GNUC__ >= 4
#  define _vbi_sentinel __attribute__ ((__sentinel__(0)))
#  define _vbi_deprecated __attribute__ ((__deprecated__))
#else
#  define _vbi_sentinel
#  define _vbi_deprecated
#  define __restrict__
#endif

#if (__GNUC__ == 3 && __GNUC_MINOR__ >= 3) || __GNUC__ >= 4
#  define _vbi_nonnull(params) __attribute__ ((__nonnull__ params))
#  define _vbi_format(params) __attribute__ ((__format__ params))
#else
#  define _vbi_nonnull(params)
#  define _vbi_format(params)
#endif

#if __GNUC__ >= 3
#  define _vbi_pure __attribute__ ((__pure__))
#  define _vbi_alloc __attribute__ ((__malloc__))
#else
#  define _vbi_pure
#  define _vbi_alloc
#endif

#if __GNUC__ >= 2
#  define _vbi_unused __attribute__ ((__unused__))
#  define _vbi_const __attribute__ ((__const__))
#  define _vbi_inline static __inline__
#else
#  define _vbi_unused
#  define _vbi_const
#  define _vbi_inline static
#endif

#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

typedef int vbi_bool;


#ifndef NULL
#  ifdef __cplusplus
#    define NULL (0L)
#  else
#    define NULL ((void *) 0)
#  endif
#endif

/* XXX Document me - for variadic funcs. */
#define VBI_END ((void *) 0)

#if 0
typedef void
vbi_lock_fn			(void *			user_data);
typedef void
vbi_unlock_fn			(void *			user_data);
#endif

typedef enum {
	
	VBI_LOG_ERROR		= 1 << 3,

	VBI_LOG_WARNING		= 1 << 4,

	VBI_LOG_NOTICE		= 1 << 5,

	
	VBI_LOG_INFO		= 1 << 6,

	
	VBI_LOG_DEBUG		= 1 << 7,

	
	VBI_LOG_DRIVER		= 1 << 8,

	
	VBI_LOG_DEBUG2		= 1 << 9,
	VBI_LOG_DEBUG3		= 1 << 10
} vbi_log_mask;

typedef void
vbi_log_fn			(vbi_log_mask		level,
				 const char *		context,
				 const char *		message,
				 void *			user_data);

extern vbi_log_fn		vbi_log_on_stderr;


/* bcd.h */

/* XXX unsigned? */
typedef int vbi_pgno;

typedef int vbi_subno;

#define VBI_ANY_SUBNO 0x3F7F
#define VBI_NO_SUBNO 0x3F7F

_vbi_inline unsigned int
vbi_dec2bcd(unsigned int dec)
{
	return (dec % 10) + ((dec / 10) % 10) * 16 + ((dec / 100) % 10) * 256;
}

#define vbi_bin2bcd(n) vbi_dec2bcd(n)

_vbi_inline unsigned int
vbi_bcd2dec(unsigned int bcd)
{
	return (bcd & 15) + ((bcd >> 4) & 15) * 10 + ((bcd >> 8) & 15) * 100;
}

#define vbi_bcd2bin(n) vbi_bcd2dec(n)

_vbi_inline unsigned int
vbi_add_bcd(unsigned int a, unsigned int b)
{
	unsigned int t;

	a += 0x06666666;
	t  = a + b;
	b ^= a ^ t;
	b  = (~b & 0x11111110) >> 3;
	b |= b * 2;

	return t - b;
}

_vbi_inline vbi_bool
vbi_is_bcd(unsigned int bcd)
{
	static const unsigned int x = 0x06666666;

	return (((bcd + x) ^ (bcd ^ x)) & 0x11111110) == 0;
}

_vbi_inline vbi_bool
vbi_bcd_digits_greater		(unsigned int		bcd,
				 unsigned int		maximum)
{
	maximum ^= ~0;

	return 0 != (((bcd + maximum) ^ bcd ^ maximum) & 0x11111110);
}

/* conv.h */

#include <stdio.h>
#include <inttypes.h>		/* uint16_t */

 
#define VBI_NUL_TERMINATED -1

extern unsigned long
vbi_strlen_ucs2			(const uint16_t *	src);
extern char *
vbi_strndup_iconv		(const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
  _vbi_alloc;
extern char *
vbi_strndup_iconv_ucs2		(const char *		dst_codeset,
				 const uint16_t *	src,
				 long			src_length,
				 int			repl_char)
  _vbi_alloc;
extern char *
vbi_strndup_iconv_caption	(const char *		dst_codeset,
				 const char *		src,
				 long			src_length,
				 int			repl_char)
  _vbi_alloc;
#if 3 == VBI_VERSION_MINOR
extern char *
vbi_strndup_iconv_teletext	(const char *		dst_codeset,
				 const vbi_ttx_charset *cs,
				 const uint8_t *	src,
				 long			src_length,
				 int			repl_char)
  _vbi_alloc _vbi_nonnull ((2));
#endif
extern vbi_bool
vbi_fputs_iconv			(FILE *			fp,
				 const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_fputs_iconv_ucs2		(FILE *			fp,
				 const char *		dst_codeset,
				 const uint16_t *	src,
				 long			src_length,
				 int			repl_char)
  _vbi_nonnull ((1));
extern const char *
vbi_locale_codeset		(void);



/* network.h */

typedef enum {
	VBI_CNI_TYPE_NONE,
	VBI_CNI_TYPE_UNKNOWN = VBI_CNI_TYPE_NONE,

	VBI_CNI_TYPE_VPS,

	VBI_CNI_TYPE_8301,

	VBI_CNI_TYPE_8302,

	VBI_CNI_TYPE_PDC_A,

	VBI_CNI_TYPE_PDC_B
} vbi_cni_type;

/* pdc.h */

#include <time.h>		/* time_t */


typedef unsigned int vbi_pil;

#define VBI_PIL(month, day, hour, minute)				\
	(((day) << 15) | ((month) << 11) | ((hour) << 6) | (minute))


#define VBI_PIL_MONTH(pil)	(((pil) >> 11) & 15)


#define VBI_PIL_DAY(pil)	(((pil) >> 15) & 31)


#define VBI_PIL_HOUR(pil)	(((pil) >> 6) & 31)


#define VBI_PIL_MINUTE(pil)	((pil) & 63)

enum {
	VBI_PIL_TIMER_CONTROL		= VBI_PIL (15, 0, 31, 63),

	VBI_PIL_INHIBIT_TERMINATE	= VBI_PIL (15, 0, 30, 63),

	VBI_PIL_INTERRUPTION		= VBI_PIL (15, 0, 29, 63),

	VBI_PIL_CONTINUE		= VBI_PIL (15, 0, 28, 63),

	VBI_PIL_NSPV			= VBI_PIL (15, 15, 31, 63),

	VBI_PIL_END			= VBI_PIL (15, 15, 31, 63)
};

extern vbi_bool
vbi_pil_is_valid_date		(vbi_pil		pil);
extern time_t
vbi_pil_to_time			(vbi_pil		pil,
				 time_t			start,
				 const char *		tz);
extern time_t
vbi_pil_lto_to_time		(vbi_pil		pil,
				 time_t			start,
				 int			seconds_east);
extern vbi_bool
vbi_pty_validity_window		(time_t *		begin,
				 time_t *		end,
				 time_t			time,
				 const char *		tz)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
;
extern vbi_bool
vbi_pil_validity_window		(time_t *		begin,
				 time_t *		end,
				 vbi_pil		pil,
				 time_t			start,
				 const char *		tz)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
;
extern vbi_bool
vbi_pil_lto_validity_window	(time_t *		begin,
				 time_t *		end,
				 vbi_pil		pil,
				 time_t			start,
				 int			seconds_east)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
;


typedef enum {
	VBI_PID_CHANNEL_LCI_0 = 0,

	
	VBI_PID_CHANNEL_LCI_1,

	
	VBI_PID_CHANNEL_LCI_2,

	
	VBI_PID_CHANNEL_LCI_3,

	VBI_PID_CHANNEL_VPS,

	VBI_PID_CHANNEL_PDC_DESCRIPTOR,

	VBI_PID_CHANNEL_XDS_CURRENT,

	VBI_PID_CHANNEL_XDS_FUTURE,

	
	VBI_MAX_PID_CHANNELS
} vbi_pid_channel;

typedef enum {
	
	VBI_PCS_AUDIO_UNKNOWN = 0,

	
	VBI_PCS_AUDIO_MONO,

	
	VBI_PCS_AUDIO_STEREO,

	 
	VBI_PCS_AUDIO_BILINGUAL
} vbi_pcs_audio;

typedef struct {
	
	vbi_pid_channel			channel;

	vbi_cni_type			cni_type;

	unsigned int			cni;

	vbi_pil				pil;

	vbi_bool			luf;

	vbi_bool			mi;

	vbi_bool			prf;

	
	vbi_pcs_audio			pcs_audio;

	unsigned int			pty;

	vbi_bool			tape_delayed;

	void *				_reserved2[2];
	int				_reserved3[4];
} vbi_program_id;



/* event.h */

#include <inttypes.h>


typedef unsigned int vbi_nuid;

typedef struct {
	vbi_nuid		nuid;

	signed char		name[64];

	signed char		call[40];

	int			tape_delay;

	int			cni_vps;

	int			cni_8301;

	int			cni_8302;

	int			reserved;

	
	int			cycle;
} vbi_network;

/*
 *  Link
 */

typedef enum {
	VBI_LINK_NONE = 0,
	VBI_LINK_MESSAGE,
	VBI_LINK_PAGE,
	VBI_LINK_SUBPAGE,
	VBI_LINK_HTTP,
	VBI_LINK_FTP,
	VBI_LINK_EMAIL,
	
	VBI_LINK_LID,
	
	VBI_LINK_TELEWEB
} vbi_link_type;

typedef enum {
	VBI_WEBLINK_UNKNOWN = 0,
	VBI_WEBLINK_PROGRAM_RELATED,
	VBI_WEBLINK_NETWORK_RELATED,
	VBI_WEBLINK_STATION_RELATED,
	VBI_WEBLINK_SPONSOR_MESSAGE,
	VBI_WEBLINK_OPERATOR
} vbi_itv_type;

typedef struct vbi_link {
	vbi_link_type			type;
	vbi_bool			eacem;
	signed char 			name[80];
	signed char			url[256];
	signed char			script[256];
	vbi_nuid			nuid;
	vbi_pgno			pgno;
	vbi_subno			subno;
	double				expires;
	vbi_itv_type			itv_type;
	int				priority;
	vbi_bool			autoload;
} vbi_link;

/*
 *  Aspect ratio information.
 */

typedef enum {
	VBI_SUBT_NONE,		
	VBI_SUBT_ACTIVE,	
	VBI_SUBT_MATTE,		
	VBI_SUBT_UNKNOWN	
} vbi_subt;

typedef struct {
 	int			first_line;
	int			last_line;
	double			ratio;
 	vbi_bool		film_mode;
	vbi_subt		open_subtitles;
} vbi_aspect_ratio;

/*
 *  Program Info
 *
 *  ATTN this is new stuff and subject to change
 */

typedef enum {
	VBI_RATING_AUTH_NONE = 0,
	VBI_RATING_AUTH_MPAA,
	VBI_RATING_AUTH_TV_US,
	VBI_RATING_AUTH_TV_CA_EN,
	VBI_RATING_AUTH_TV_CA_FR
} vbi_rating_auth;

#define VBI_RATING_D 0x08 
#define VBI_RATING_L 0x04 
#define VBI_RATING_S 0x02 
#define VBI_RATING_V 0x01 


extern const char *	vbi_rating_string(vbi_rating_auth auth, int id);

typedef enum {
	VBI_PROG_CLASSF_NONE = 0,
	VBI_PROG_CLASSF_EIA_608,
	VBI_PROG_CLASSF_ETS_300231
} vbi_prog_classf;

extern const char *	vbi_prog_type_string(vbi_prog_classf classf, int id);


/* code depends on order, don't change */
typedef enum {
	VBI_AUDIO_MODE_NONE = 0,		
	VBI_AUDIO_MODE_MONO,			
	VBI_AUDIO_MODE_STEREO,			
	VBI_AUDIO_MODE_STEREO_SURROUND,		
	VBI_AUDIO_MODE_SIMULATED_STEREO,	
	VBI_AUDIO_MODE_VIDEO_DESCRIPTIONS,
	VBI_AUDIO_MODE_NON_PROGRAM_AUDIO,

	VBI_AUDIO_MODE_SPECIAL_EFFECTS,		
	VBI_AUDIO_MODE_DATA_SERVICE,		
	VBI_AUDIO_MODE_UNKNOWN
} vbi_audio_mode;

typedef struct vbi_program_info {
	/*
	 *  Refers to the current or next program.
	 *  (No [2] to allow clients filtering current data more easily.)
	 */
	unsigned int		future : 1;

	/* 01 Program Identification Number */

	/* If unknown all these fields are -1 */
	signed char		month;		/* 0 ... 11 */
	signed char		day;		/* 0 ... 30 */
	signed char		hour;		/* 0 ... 23 */
	signed char		min;		/* 0 ... 59 */

	/*
	 *  VD: "T indicates if a program is routinely tape delayed for the
	 *  Mountain and Pacific time zones."
	 */
	signed char		tape_delayed;

	/* 02 Program Length */

	/* If unknown all these fields are -1 */
	signed char		length_hour;	/* 0 ... 63 */
	signed char		length_min;	/* 0 ... 59 */

	signed char		elapsed_hour;	/* 0 ... 63 */
	signed char		elapsed_min;	/* 0 ... 59 */
	signed char		elapsed_sec;	/* 0 ... 59 */

	/* 03 Program name */

	/* If unknown title[0] == 0 */
	signed char		title[64];	/* ASCII + '\0' */

	/* 04 Program type */

	/*
	 *  If unknown type_classf == VBI_PROG_CLASSF_NONE.
	 *  VBI_PROG_CLASSF_EIA_608 can have up to 32 tags
	 *  identifying 96 keywords. Their numerical value
	 *  is given here instead of composing a string for
	 *  easier filtering. Use vbi_prog_type_str_by_id to
	 *  get the keywords. A zero marks the end.
	 */
	vbi_prog_classf		type_classf;
	int			type_id[33];

	/* 05 Program rating */

	/*
	 *  For details STFW for "v-chip"
	 *  If unknown rating_auth == VBI_RATING_NONE
	 */
	vbi_rating_auth		rating_auth;
	int			rating_id;

	/* Only valid when auth == VBI_RATING_TV_US */
	int			rating_dlsv;

	/* 06 Program Audio Services */

	/*
	 *  BTSC audio (two independent tracks) is flagged according to XDS,
	 *  Zweiton/NICAM/EIA-J audio is flagged mono/none, stereo/none or
	 *  mono/mono for bilingual transmissions.
	 */
	struct {
		/* If unknown mode == VBI_AUDIO_MODE_UNKNOWN */
		vbi_audio_mode		mode;
		/* If unknown language == NULL */
		unsigned char *		language; /* Latin-1 */
	}			audio[2];	/* primary and secondary */

	/* 07 Program Caption Services */

	/*
	 *  Bits 0...7 corresponding to Caption page 1...8.
	 *  Note for the current program this information is also
	 *  available via vbi_classify_page().
	 *
	 *  If unknown caption_services == -1, _language[] = NULL
	 */
	int			caption_services;
	unsigned char *		caption_language[8]; /* Latin-1 */

	/* 08 Copy Generation Management System */

	/* If unknown cgms_a == -1 */
	int			cgms_a; /* XXX */

	/* 09 Aspect Ratio */

	/*
	 *  Note for the current program this information is also
	 *  available via VBI_EVENT_ASPECT.
	 *
	 *  If unknown first_line == last_line == -1, ratio == 0.0
	 */
	vbi_aspect_ratio	aspect;

	/* 10 - 17 Program Description */

	/*
	 *  8 rows of 0...32 ASCII chars + '\0',
	 *  if unknown description[0...7][0] == 0
	 */
	signed char		description[8][33];
} vbi_program_info;

extern void		vbi_reset_prog_info(vbi_program_info *pi);



#define VBI_EVENT_NONE		0x0000
#define	VBI_EVENT_CLOSE		0x0001
#define	VBI_EVENT_TTX_PAGE	0x0002
#define VBI_EVENT_CAPTION	0x0004
#define	VBI_EVENT_NETWORK	0x0008
#define	VBI_EVENT_TRIGGER	0x0010
#define	VBI_EVENT_ASPECT	0x0040
#define	VBI_EVENT_PROG_INFO	0x0080
#define	VBI_EVENT_NETWORK_ID	0x0100
#define VBI_EVENT_LOCAL_TIME	0x0400
#define VBI_EVENT_PROG_ID	0x0800


typedef enum {
	
	VBI_DST_UNKNOWN = 0,

	VBI_DST_INCLUDED,

	
	VBI_DST_INACTIVE,

	VBI_DST_ACTIVE
} vbi_dst_state;

typedef struct {
	
	time_t			time;

	int			seconds_east;

	
	vbi_bool		seconds_east_valid;

	vbi_dst_state		dst_state;
} vbi_local_time;

/* Experimental CC608 decoder. */
#define _VBI_EVENT_CC608 0x1000
#define _VBI_EVENT_CC608_STREAM 0x2000
struct _vbi_event_cc608_page;
struct _vbi_event_cc608_stream;


#include <inttypes.h>

/* XXX network, aspect, prog_info: should only notify about
 * changes and provide functions to query current value.
 */
typedef struct vbi_event {
	int			type;
	union {
		struct {
		        int			pgno;
		        int			subno;
			uint8_t *		raw_header;
			int			pn_offset;
			unsigned int		roll_header : 1;
		        unsigned int		header_update : 1;
			unsigned int		clock_update : 1;
	        }			ttx_page;
		struct {
			int			pgno;
		}			caption;
		vbi_network		network;
                vbi_link *		trigger;
                vbi_aspect_ratio	aspect;
		vbi_program_info *	prog_info;
		vbi_local_time *	local_time;
		vbi_program_id *	prog_id;

		/* Experimental. */
		struct _vbi_event_cc608_page *		_cc608;
		struct _vbi_event_cc608_stream *	_cc608_stream;
	}			ev;
} vbi_event;

typedef void (* vbi_event_handler)(vbi_event *event, void *user_data);

extern vbi_bool		vbi_event_handler_add(vbi_decoder *vbi, int event_mask,
					      vbi_event_handler handler,
					      void *user_data);
extern void		vbi_event_handler_remove(vbi_decoder *vbi,
						 vbi_event_handler handler);
extern vbi_bool		vbi_event_handler_register(vbi_decoder *vbi, int event_mask,
						   vbi_event_handler handler,
						   void *user_data);
extern void		vbi_event_handler_unregister(vbi_decoder *vbi,
						     vbi_event_handler handler,
						     void *user_data);


/* format.h */

#include <inttypes.h>


/* Code depends on order, don't change. */
typedef enum {
	VBI_BLACK,
	VBI_RED,
	VBI_GREEN,
	VBI_YELLOW,
	VBI_BLUE,
	VBI_MAGENTA,
	VBI_CYAN,
	VBI_WHITE
} vbi_color;

typedef uint32_t vbi_rgba;


typedef enum {
	VBI_TRANSPARENT_SPACE,
	VBI_TRANSPARENT_FULL,
	VBI_SEMI_TRANSPARENT,
	VBI_OPAQUE
} vbi_opacity;

/* Code depends on order, don't change. */
typedef enum {
	VBI_NORMAL_SIZE, VBI_DOUBLE_WIDTH, VBI_DOUBLE_HEIGHT, VBI_DOUBLE_SIZE,
	VBI_OVER_TOP, VBI_OVER_BOTTOM, VBI_DOUBLE_HEIGHT2, VBI_DOUBLE_SIZE2
} vbi_size;

typedef struct vbi_char {
	unsigned	underline	: 1;	
	unsigned	bold		: 1;	
	unsigned	italic		: 1;	
	unsigned	flash		: 1;
	unsigned	conceal		: 1;
	unsigned	proportional	: 1;
	unsigned	link		: 1;
	unsigned	reserved	: 1;
	unsigned	size		: 8;
	unsigned	opacity		: 8;
	unsigned	foreground	: 8;
	unsigned	background	: 8;
	unsigned	drcs_clut_offs	: 8;
	unsigned	unicode		: 16;
} vbi_char;

struct vbi_font_descr;

typedef struct vbi_page {
	vbi_decoder *		vbi;

        vbi_nuid	       	nuid;
	/* FIXME this shouldn't be int */
 	int			pgno;
	/* FIXME this shouldn't be int */
	int			subno;
	int			rows;
	int			columns;
	vbi_char		text[1056];

	struct {
	     /* int			x0, x1; */
		int			y0, y1;
		int			roll;
	}			dirty;

	vbi_color		screen_color;
	vbi_opacity		screen_opacity;
	vbi_rgba 		color_map[40];

	uint8_t *		drcs_clut;		/* 64 entries */
	uint8_t *		drcs[32];

	struct {
		int			pgno, subno;
	}			nav_link[6];
	char			nav_index[64];

	struct vbi_font_descr *	font[2];
	unsigned int		double_height_lower;	/* legacy */

	vbi_opacity		page_opacity[2];
	vbi_opacity		boxed_opacity[2];
} vbi_page;

/* lang.h */

typedef struct vbi_font_descr vbi_font_descr;

_vbi_inline vbi_bool
vbi_is_print(unsigned int unicode)
{
	return unicode < 0xE600;
}

_vbi_inline vbi_bool
vbi_is_gfx(unsigned int unicode)
{
	return unicode >= 0xEE00 && unicode <= 0xEFFF;
}

_vbi_inline vbi_bool
vbi_is_drcs(unsigned int unicode)
{
	return unicode >= 0xF000;
}

extern unsigned int
vbi_caption_unicode		(unsigned int		c,
				 vbi_bool		to_upper);

/* export.h */

#include <stdio.h> /* FILE */
#include <sys/types.h> /* size_t, ssize_t */

typedef struct vbi_export vbi_export;

typedef struct vbi_export_info {
	char *			keyword;
	char *			label;
	char *			tooltip;
	char *			mime_type;
	char *			extension;
} vbi_export_info;

typedef enum {
	VBI_OPTION_BOOL = 1,

	VBI_OPTION_INT,

	VBI_OPTION_REAL,

	VBI_OPTION_STRING,

	VBI_OPTION_MENU
} vbi_option_type;

typedef union {
	int			num;
	double			dbl;
	char *			str;
} vbi_option_value;

typedef union {
	int *			num;
	double *		dbl;
	char **			str;
} vbi_option_value_ptr;

typedef struct {
  	vbi_option_type		type;	

	char *			keyword;

	char *			label;

	vbi_option_value	def;	
	vbi_option_value	min;	
	vbi_option_value	max;	
	vbi_option_value	step;	
	vbi_option_value_ptr	menu;	

	char *			tooltip;
} vbi_option_info;

extern vbi_export_info *	vbi_export_info_enum(int index);
extern vbi_export_info *	vbi_export_info_keyword(const char *keyword);
extern vbi_export_info *	vbi_export_info_export(vbi_export *);

extern vbi_export *		vbi_export_new(const char *keyword, char **errstr);
extern void			vbi_export_delete(vbi_export *);

extern vbi_option_info *	vbi_export_option_info_enum(vbi_export *, int index);
extern vbi_option_info *	vbi_export_option_info_keyword(vbi_export *, const char *keyword);

extern vbi_bool			vbi_export_option_set(vbi_export *, const char *keyword, ...);
extern vbi_bool			vbi_export_option_get(vbi_export *, const char *keyword,
						      vbi_option_value *value);
extern vbi_bool			vbi_export_option_menu_set(vbi_export *, const char *keyword, int entry);
extern vbi_bool			vbi_export_option_menu_get(vbi_export *, const char *keyword, int *entry);

extern ssize_t
vbi_export_mem			(vbi_export *		e,
				 void *			buffer,
				 size_t			buffer_size,
				 const vbi_page *	pg)
  _vbi_nonnull ((1)); /* sic */
extern void *
vbi_export_alloc		(vbi_export *		e,
				 void **		buffer,
				 size_t *		buffer_size,
				 const vbi_page *	pg)
  _vbi_nonnull ((1)); /* sic */
extern vbi_bool			vbi_export_stdio(vbi_export *, FILE *fp, vbi_page *pg);
extern vbi_bool			vbi_export_file(vbi_export *, const char *name, vbi_page *pg);

extern char *			vbi_export_errstr(vbi_export *);


/* cache.h */

extern void             vbi_unref_page(vbi_page *pg);
extern int              vbi_is_cached(vbi_decoder *, int pgno, int subno);
extern int              vbi_cache_hi_subno(vbi_decoder *vbi, int pgno);


/* search.h */

typedef enum {
	VBI_SEARCH_ERROR = -3,
	VBI_SEARCH_CACHE_EMPTY,
	VBI_SEARCH_CANCELED,
	VBI_SEARCH_NOT_FOUND = 0,
	VBI_SEARCH_SUCCESS
} vbi_search_status;

typedef struct vbi_search vbi_search;

extern vbi_search *	vbi_search_new(vbi_decoder *vbi,
				       vbi_pgno pgno, vbi_subno subno,
				       uint16_t *pattern,
				       vbi_bool casefold, vbi_bool regexp,
				       int (* progress)(vbi_page *pg));
extern void		vbi_search_delete(vbi_search *search);
extern vbi_search_status vbi_search_next(vbi_search *search, vbi_page **pg, int dir);


/* sliced.h */

#include <inttypes.h>



#define VBI_SLICED_NONE			0

#define VBI_SLICED_UNKNOWN              0

#define VBI_SLICED_ANTIOPE              0x00002000
#define VBI_SLICED_TELETEXT_A           0x00002000

#define VBI_SLICED_TELETEXT_B_L10_625	0x00000001
#define VBI_SLICED_TELETEXT_B_L25_625	0x00000002
#define VBI_SLICED_TELETEXT_B		(VBI_SLICED_TELETEXT_B_L10_625 | \
					 VBI_SLICED_TELETEXT_B_L25_625)
#define VBI_SLICED_TELETEXT_B_625	VBI_SLICED_TELETEXT_B

#define VBI_SLICED_TELETEXT_C_625       0x00004000

#define VBI_SLICED_TELETEXT_D_625       0x00008000

#define VBI_SLICED_VPS                  0x00000004

#define VBI_SLICED_VPS_F2               0x00001000

#define VBI_SLICED_CAPTION_625_F1       0x00000008
#define VBI_SLICED_CAPTION_625_F2       0x00000010
#define VBI_SLICED_CAPTION_625		(VBI_SLICED_CAPTION_625_F1 | \
                                         VBI_SLICED_CAPTION_625_F2)

#define VBI_SLICED_WSS_625              0x00000400

#define VBI_SLICED_CAPTION_525_F1	0x00000020
#define VBI_SLICED_CAPTION_525_F2	0x00000040
#define VBI_SLICED_CAPTION_525		(VBI_SLICED_CAPTION_525_F1 | \
					 VBI_SLICED_CAPTION_525_F2)
#define VBI_SLICED_2xCAPTION_525	0x00000080

#define VBI_SLICED_TELETEXT_B_525       0x00010000

#define VBI_SLICED_NABTS                0x00000100

#define VBI_SLICED_TELETEXT_C_525       0x00000100

#define VBI_SLICED_TELETEXT_BD_525	0x00000200

#define VBI_SLICED_TELETEXT_D_525       0x00020000


#define VBI_SLICED_WSS_CPR1204		0x00000800

#define VBI_SLICED_VBI_625		0x20000000

#define VBI_SLICED_VBI_525		0x40000000



typedef unsigned int vbi_service_set;

typedef struct {
	uint32_t		id;
	uint32_t		line;
	uint8_t			data[56];
} vbi_sliced;

extern const char *
vbi_sliced_name			(vbi_service_set	service)
  _vbi_const;
extern unsigned int
vbi_sliced_payload_bits		(vbi_service_set	service)
  _vbi_const;


/* decoder.h */

#include <pthread.h>

/* Bit slicer */

/* Attn: keep this in sync with rte, don't change order */
typedef enum {
	VBI_PIXFMT_YUV420 = 1,
	VBI_PIXFMT_YUYV,
	VBI_PIXFMT_YVYU,
	VBI_PIXFMT_UYVY,
	VBI_PIXFMT_VYUY,
        VBI_PIXFMT_PAL8,
	VBI_PIXFMT_RGBA32_LE = 32,
	VBI_PIXFMT_RGBA32_BE,
	VBI_PIXFMT_BGRA32_LE,
	VBI_PIXFMT_BGRA32_BE,
	VBI_PIXFMT_ABGR32_BE = 32, /* synonyms */
	VBI_PIXFMT_ABGR32_LE,
	VBI_PIXFMT_ARGB32_BE,
	VBI_PIXFMT_ARGB32_LE,
	VBI_PIXFMT_RGB24,
	VBI_PIXFMT_BGR24,
	VBI_PIXFMT_RGB16_LE,
	VBI_PIXFMT_RGB16_BE,
	VBI_PIXFMT_BGR16_LE,
	VBI_PIXFMT_BGR16_BE,
	VBI_PIXFMT_RGBA15_LE,
	VBI_PIXFMT_RGBA15_BE,
	VBI_PIXFMT_BGRA15_LE,
	VBI_PIXFMT_BGRA15_BE,
	VBI_PIXFMT_ARGB15_LE,
	VBI_PIXFMT_ARGB15_BE,
	VBI_PIXFMT_ABGR15_LE,
	VBI_PIXFMT_ABGR15_BE
} vbi_pixfmt;


typedef enum {
	VBI_MODULATION_NRZ_LSB,
	VBI_MODULATION_NRZ_MSB,
	VBI_MODULATION_BIPHASE_LSB,
	VBI_MODULATION_BIPHASE_MSB
} vbi_modulation;

typedef struct vbi_bit_slicer {
	vbi_bool	(* func)(struct vbi_bit_slicer *slicer,
				 uint8_t *raw, uint8_t *buf);
	unsigned int	cri;
	unsigned int	cri_mask;
	int		thresh;
	int		cri_bytes;
	int		cri_rate;
	int		oversampling_rate;
	int		phase_shift;
	int		step;
	unsigned int	frc;
	int		frc_bits;
	int		payload;
	int		endian;
	int		skip;
} vbi_bit_slicer;

extern void		vbi_bit_slicer_init(vbi_bit_slicer *slicer,
					    int raw_samples, int sampling_rate,
					    int cri_rate, int bit_rate,
					    unsigned int cri_frc, unsigned int cri_mask,
					    int cri_bits, int frc_bits, int payload,
					    vbi_modulation modulation, vbi_pixfmt fmt);
_vbi_inline vbi_bool
vbi_bit_slice(vbi_bit_slicer *slicer, uint8_t *raw, uint8_t *buf)
{
	return slicer->func(slicer, raw, buf);
}


typedef struct vbi_raw_decoder {
	/* Sampling parameters */

	int			scanning;
	vbi_pixfmt		sampling_format;
	int			sampling_rate;		/* Hz */
	int			bytes_per_line;
	int			offset;			/* 0H, samples */
	int			start[2];		/* ITU-R numbering */
	int			count[2];		/* field lines */
	vbi_bool		interlaced;
	vbi_bool		synchronous;

	/*< private >*/

	pthread_mutex_t		mutex;

	unsigned int		services;
	int			num_jobs;

	int8_t *		pattern;
	struct _vbi_raw_decoder_job {
		unsigned int		id;
		int			offset;
		vbi_bit_slicer		slicer;
	}			jobs[8];
} vbi_raw_decoder;

extern void		vbi_raw_decoder_init(vbi_raw_decoder *rd);
extern void		vbi_raw_decoder_reset(vbi_raw_decoder *rd);
extern void		vbi_raw_decoder_destroy(vbi_raw_decoder *rd);
extern unsigned int	vbi_raw_decoder_add_services(vbi_raw_decoder *rd,
						     unsigned int services,
						     int strict);
extern unsigned int     vbi_raw_decoder_check_services(vbi_raw_decoder *rd,
						     unsigned int services, int strict);
extern unsigned int	vbi_raw_decoder_remove_services(vbi_raw_decoder *rd,
							unsigned int services);
extern void             vbi_raw_decoder_resize( vbi_raw_decoder *rd,
						int * start, unsigned int * count );
extern unsigned int	vbi_raw_decoder_parameters(vbi_raw_decoder *rd, unsigned int services,
						   int scanning, int *max_rate);
extern int		vbi_raw_decode(vbi_raw_decoder *rd, uint8_t *raw, vbi_sliced *out);


/* sampling_par.h */

typedef vbi_raw_decoder vbi_sampling_par;

#define VBI_VIDEOSTD_SET_EMPTY 0
#define VBI_VIDEOSTD_SET_PAL_BG 1
#define VBI_VIDEOSTD_SET_625_50 1
#define VBI_VIDEOSTD_SET_525_60 2
#define VBI_VIDEOSTD_SET_ALL 3
typedef uint64_t vbi_videostd_set;

/* dvb_demux.h */


typedef struct _vbi_dvb_demux vbi_dvb_demux;

typedef vbi_bool
vbi_dvb_demux_cb		(vbi_dvb_demux *	dx,
				 void *			user_data,
				 const vbi_sliced *	sliced,
				 unsigned int		sliced_lines,
				 int64_t		pts);

extern void
vbi_dvb_demux_reset		(vbi_dvb_demux *	dx);
extern unsigned int
vbi_dvb_demux_cor		(vbi_dvb_demux *	dx,
				 vbi_sliced *		sliced,
				 unsigned int 		sliced_lines,
				 int64_t *		pts,
				 const uint8_t **	buffer,
				 unsigned int *		buffer_left);
extern vbi_bool
vbi_dvb_demux_feed		(vbi_dvb_demux *	dx,
				 const uint8_t *	buffer,
				 unsigned int		buffer_size);
extern void
vbi_dvb_demux_set_log_fn	(vbi_dvb_demux *	dx,
				 vbi_log_mask		mask,
				 vbi_log_fn *		log_fn,
				 void *			user_data);
extern void
vbi_dvb_demux_delete		(vbi_dvb_demux *	dx);
extern vbi_dvb_demux *
vbi_dvb_pes_demux_new		(vbi_dvb_demux_cb *	callback,
				 void *			user_data);



/* dvb_mux.h */


extern vbi_bool
vbi_dvb_multiplex_sliced	(uint8_t **		packet,
				 unsigned int *		packet_left,
				 const vbi_sliced **	sliced,
				 unsigned int *		sliced_left,
				 vbi_service_set	service_mask,
				 unsigned int		data_identifier,
				 vbi_bool		stuffing)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2, 3, 4))
#endif
  ;
extern vbi_bool
vbi_dvb_multiplex_raw		(uint8_t **		packet,
				 unsigned int *		packet_left,
				 const uint8_t **	raw,
				 unsigned int *		raw_left,
				 unsigned int		data_identifier,
				 vbi_videostd_set	videostd_set,
				 unsigned int		line,
				 unsigned int		first_pixel_position,
				 unsigned int		n_pixels_total,
				 vbi_bool		stuffing)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2, 3, 4))
#endif
  ;

typedef struct _vbi_dvb_mux vbi_dvb_mux;

typedef vbi_bool
vbi_dvb_mux_cb			(vbi_dvb_mux *		mx,
				 void *			user_data,
				 const uint8_t *	packet,
				 unsigned int		packet_size);

extern void
vbi_dvb_mux_reset		(vbi_dvb_mux *		mx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_dvb_mux_cor		(vbi_dvb_mux *		mx,
				 uint8_t **		buffer,
				 unsigned int *		buffer_left,
				 const vbi_sliced **	sliced,
				 unsigned int *		sliced_lines,
				 vbi_service_set	service_mask,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sampling_par,	 
				 int64_t		pts)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2, 3, 4, 5))
#endif
  ;
extern vbi_bool
vbi_dvb_mux_feed		(vbi_dvb_mux *		mx,
				 const vbi_sliced *	sliced,
				 unsigned int		sliced_lines,
				 vbi_service_set	service_mask,
				 const uint8_t *	raw,
				 const vbi_sampling_par *sampling_par,
				 int64_t		pts)
  _vbi_nonnull ((1));
extern unsigned int
vbi_dvb_mux_get_data_identifier (const vbi_dvb_mux *	mx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_dvb_mux_set_data_identifier (vbi_dvb_mux *	mx,
				  unsigned int		data_identifier)
  _vbi_nonnull ((1));
extern unsigned int
vbi_dvb_mux_get_min_pes_packet_size
				(vbi_dvb_mux *		mx)
  _vbi_nonnull ((1));
extern unsigned int
vbi_dvb_mux_get_max_pes_packet_size
				(vbi_dvb_mux *		mx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_dvb_mux_set_pes_packet_size (vbi_dvb_mux *	mx,
				  unsigned int		min_size,
				  unsigned int		max_size)
  _vbi_nonnull ((1));
extern void
vbi_dvb_mux_delete		(vbi_dvb_mux *		mx);
extern vbi_dvb_mux *
vbi_dvb_pes_mux_new		(vbi_dvb_mux_cb *	callback,
				 void *			user_data)
  _vbi_alloc;
extern vbi_dvb_mux *
vbi_dvb_ts_mux_new		(unsigned int		pid,
				 vbi_dvb_mux_cb *	callback,
				 void *			user_data)
  _vbi_alloc;



/* idl_demux.h */


typedef struct _vbi_idl_demux vbi_idl_demux;



#define VBI_IDL_DATA_LOST	(1 << 0)

#define VBI_IDL_DEPENDENT	(1 << 3)




typedef vbi_bool
vbi_idl_demux_cb		(vbi_idl_demux *	dx,
				 const uint8_t *	buffer,
				 unsigned int		n_bytes,
				 unsigned int		flags,
				 void *			user_data);

extern void
vbi_idl_demux_reset		(vbi_idl_demux *	dx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_idl_demux_feed		(vbi_idl_demux *	dx,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_idl_demux_feed_frame	(vbi_idl_demux *	dx,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern void
vbi_idl_demux_delete		(vbi_idl_demux *	dx);
extern vbi_idl_demux *
vbi_idl_a_demux_new		(unsigned int		channel,
				 unsigned int		address,
				 vbi_idl_demux_cb *	callback,
				 void *			user_data)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_alloc _vbi_nonnull ((3))
#endif
  ;



/* pfc_demux.h */


typedef struct {
	
	vbi_pgno		pgno;

	
	unsigned int		stream;

	
	unsigned int		application_id;

	
	unsigned int		block_size;

	
	uint8_t			block[2048];
} vbi_pfc_block;

typedef struct _vbi_pfc_demux vbi_pfc_demux;

typedef vbi_bool
vbi_pfc_demux_cb		(vbi_pfc_demux *	dx,
				 void *			user_data,
				 const vbi_pfc_block *	block);

extern void
vbi_pfc_demux_reset		(vbi_pfc_demux *	dx)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_pfc_demux_feed		(vbi_pfc_demux *	dx,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_pfc_demux_feed_frame	(vbi_pfc_demux *	dx,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern void
vbi_pfc_demux_delete		(vbi_pfc_demux *	dx);
extern vbi_pfc_demux *
vbi_pfc_demux_new		(vbi_pgno		pgno,
				 unsigned int		stream,
				 vbi_pfc_demux_cb *	callback,
				 void *			user_data)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_alloc _vbi_nonnull ((3))
#endif
  ;



/* xds_demux.h */


typedef enum {
	VBI_XDS_CLASS_CURRENT = 0x00,
	VBI_XDS_CLASS_FUTURE,
	VBI_XDS_CLASS_CHANNEL,
	VBI_XDS_CLASS_MISC,
	VBI_XDS_CLASS_PUBLIC_SERVICE,
	VBI_XDS_CLASS_RESERVED,
	VBI_XDS_CLASS_UNDEFINED
} vbi_xds_class;

#define VBI_XDS_MAX_CLASSES (VBI_XDS_CLASS_UNDEFINED + 1)

typedef enum {
	VBI_XDS_PROGRAM_ID = 0x01,
	VBI_XDS_PROGRAM_LENGTH,
	VBI_XDS_PROGRAM_NAME,
	VBI_XDS_PROGRAM_TYPE,
	VBI_XDS_PROGRAM_RATING,
	VBI_XDS_PROGRAM_AUDIO_SERVICES,
	VBI_XDS_PROGRAM_CAPTION_SERVICES,
	VBI_XDS_PROGRAM_CGMS,
	VBI_XDS_PROGRAM_ASPECT_RATIO,
	
	VBI_XDS_PROGRAM_DATA = 0x0C,
	
	VBI_XDS_PROGRAM_MISC_DATA,
	VBI_XDS_PROGRAM_DESCRIPTION_BEGIN = 0x10,
	VBI_XDS_PROGRAM_DESCRIPTION_END = 0x18
} vbi_xds_subclass_program;


typedef enum {
	VBI_XDS_CHANNEL_NAME = 0x01,
	VBI_XDS_CHANNEL_CALL_LETTERS,
	VBI_XDS_CHANNEL_TAPE_DELAY,
	
	VBI_XDS_CHANNEL_TSID
} vbi_xds_subclass_channel;


typedef enum {
	VBI_XDS_TIME_OF_DAY = 0x01,
	VBI_XDS_IMPULSE_CAPTURE_ID,
	VBI_XDS_SUPPLEMENTAL_DATA_LOCATION,
	VBI_XDS_LOCAL_TIME_ZONE,
	
	VBI_XDS_OUT_OF_BAND_CHANNEL = 0x40,
	
	VBI_XDS_CHANNEL_MAP_POINTER,
	
	VBI_XDS_CHANNEL_MAP_HEADER,
	
	VBI_XDS_CHANNEL_MAP
} vbi_xds_subclass_misc;

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/* Compatibility. */
#define VBI_XDS_MISC_TIME_OF_DAY VBI_XDS_TIME_OF_DAY
#define VBI_XDS_MISC_IMPULSE_CAPTURE_ID VBI_XDS_IMPULSE_CAPTURE_ID
#define VBI_XDS_MISC_SUPPLEMENTAL_DATA_LOCATION \
	VBI_XDS_SUPPLEMENTAL_DATA_LOCATION
#define VBI_XDS_MISC_LOCAL_TIME_ZONE VBI_XDS_LOCAL_TIME_ZONE

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

typedef enum {
	VBI_XDS_WEATHER_BULLETIN = 0x01,
	VBI_XDS_WEATHER_MESSAGE
} vbi_xds_subclass_public_service;

#define VBI_XDS_MAX_SUBCLASSES (0x18)

typedef unsigned int vbi_xds_subclass;

typedef struct {
	vbi_xds_class		xds_class;
	vbi_xds_subclass	xds_subclass;

	
	unsigned int		buffer_size;

	uint8_t			buffer[36];
} vbi_xds_packet;



extern void
_vbi_xds_packet_dump		(const vbi_xds_packet *	xp,
				 FILE *			fp);


typedef struct _vbi_xds_demux vbi_xds_demux;

typedef vbi_bool
vbi_xds_demux_cb		(vbi_xds_demux *	xd,
				 const vbi_xds_packet *	xp,
				 void *			user_data);

extern void
vbi_xds_demux_reset		(vbi_xds_demux *	xd);
extern vbi_bool
vbi_xds_demux_feed		(vbi_xds_demux *	xd,
				 const uint8_t		buffer[2]);
extern vbi_bool
vbi_xds_demux_feed_frame	(vbi_xds_demux *	xd,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines);
extern void
vbi_xds_demux_delete		(vbi_xds_demux *	xd);
extern vbi_xds_demux *
vbi_xds_demux_new		(vbi_xds_demux_cb *	callback,
				 void *			user_data)
  _vbi_alloc;



/* io.h */

#include <sys/time.h> /* struct timeval */

typedef struct vbi_capture_buffer {
	void *			data;
	int			size;
	double			timestamp;
} vbi_capture_buffer;

typedef struct vbi_capture vbi_capture;

typedef enum {
        VBI_FD_HAS_SELECT  = 1<<0,
        VBI_FD_HAS_MMAP    = 1<<1,
        VBI_FD_IS_DEVICE   = 1<<2
} VBI_CAPTURE_FD_FLAGS;

extern vbi_capture *	vbi_capture_v4l2_new(const char *dev_name, int buffers,
					     unsigned int *services, int strict,
					     char **errorstr, vbi_bool trace);
extern vbi_capture *	vbi_capture_v4l2k_new(const char *	dev_name,
					      int		fd,
					      int		buffers,
					      unsigned int *	services,
					      int		strict,
					      char **		errorstr,
					      vbi_bool		trace);
extern vbi_capture *	vbi_capture_v4l_new(const char *dev_name, int scanning,
					    unsigned int *services, int strict,
					    char **errorstr, vbi_bool trace);
extern vbi_capture *	vbi_capture_v4l_sidecar_new(const char *dev_name, int given_fd,
						    unsigned int *services,
						    int strict, char **errorstr, 
						    vbi_bool trace);
extern vbi_capture *	vbi_capture_bktr_new (const char *	dev_name,
					      int		scanning,
					      unsigned int *	services,
					      int		strict,
					      char **		errstr,
					      vbi_bool		trace);
extern int		vbi_capture_dvb_filter(vbi_capture *cap, int pid);

/* This function is deprecated. Use vbi_capture_dvb_new2() instead.
   See io-dvb.c or the Doxygen documentation for details. */
extern vbi_capture *
vbi_capture_dvb_new		(char *			dev,
				 int			scanning,
				 unsigned int *		services,
				 int			strict,
				 char **		errstr,
				 vbi_bool		trace)
  _vbi_deprecated;

extern int64_t
vbi_capture_dvb_last_pts	(const vbi_capture *	cap);
extern vbi_capture *
vbi_capture_dvb_new2		(const char *		device_name,
				 unsigned int		pid,
				 char **		errstr,
				 vbi_bool		trace);

struct vbi_proxy_client;
 
extern vbi_capture *
vbi_capture_proxy_new( struct vbi_proxy_client * vpc,
                        int buffers, int scanning,
                        unsigned int *p_services, int strict,
                        char **pp_errorstr );

extern int		vbi_capture_read_raw(vbi_capture *capture, void *data,
					     double *timestamp, struct timeval *timeout);
extern int		vbi_capture_read_sliced(vbi_capture *capture, vbi_sliced *data, int *lines,
						double *timestamp, struct timeval *timeout);
extern int		vbi_capture_read(vbi_capture *capture, void *raw_data,
					 vbi_sliced *sliced_data, int *lines,
					 double *timestamp, struct timeval *timeout);
extern int		vbi_capture_pull_raw(vbi_capture *capture, vbi_capture_buffer **buffer,
					     struct timeval *timeout);
extern int		vbi_capture_pull_sliced(vbi_capture *capture, vbi_capture_buffer **buffer,
						struct timeval *timeout);
extern int		vbi_capture_pull(vbi_capture *capture, vbi_capture_buffer **raw_buffer,
					 vbi_capture_buffer **sliced_buffer, struct timeval *timeout);
extern vbi_raw_decoder *vbi_capture_parameters(vbi_capture *capture);
extern int		vbi_capture_fd(vbi_capture *capture);
extern unsigned int     vbi_capture_update_services(vbi_capture *capture,
                                                    vbi_bool reset, vbi_bool commit,
                                                    unsigned int services, int strict,
                                                    char ** errorstr);
extern int              vbi_capture_get_scanning(vbi_capture *capture);
extern void             vbi_capture_flush(vbi_capture *capture);
extern void		vbi_capture_delete(vbi_capture *capture);

extern vbi_bool         vbi_capture_set_video_path(vbi_capture *capture, const char * p_dev_video);
extern VBI_CAPTURE_FD_FLAGS vbi_capture_get_fd_flags(vbi_capture *capture);


/* io-sim.h */

extern vbi_bool
vbi_raw_video_image		(uint8_t *		raw,
				 unsigned long		raw_size,
				 const vbi_sampling_par *sp,
				 int			blank_level,
				 int			black_level,
				 int			white_level,
				 unsigned int		pixel_mask,
				 vbi_bool		swap_fields,
				 const vbi_sliced *	sliced,
				 unsigned int		n_sliced_lines);
extern vbi_bool
vbi_raw_add_noise		(uint8_t *		raw,
				 const vbi_sampling_par *sp,
				 unsigned int		min_freq,
				 unsigned int		max_freq,
				 unsigned int		amplitude,
				 unsigned int		seed);
extern vbi_bool
vbi_raw_vbi_image		(uint8_t *		raw,
				 unsigned long		raw_size,
				 const vbi_sampling_par *sp,
				 int			blank_level,
				 int			white_level,
				 vbi_bool		swap_fields,
				 const vbi_sliced *	sliced,
				 unsigned int		n_sliced_lines);

extern void
vbi_capture_sim_add_noise	(vbi_capture *		cap,
				 unsigned int		min_freq,
				 unsigned int		max_freq,
				 unsigned int		amplitude);
extern vbi_bool
vbi_capture_sim_load_caption	(vbi_capture *		cap,
				 const char *		stream,
				 vbi_bool		append);
#if 3 == VBI_VERSION_MINOR
extern vbi_bool
vbi_capture_sim_load_vps	(vbi_capture *		cap,
				 const vbi_program_id *pid);
extern vbi_bool
vbi_capture_sim_load_wss_625	(vbi_capture *		cap,
				 const vbi_aspect_ratio *ar);
#endif
extern void
vbi_capture_sim_decode_raw	(vbi_capture *		cap,
				 vbi_bool		enable);
extern vbi_capture *
vbi_capture_sim_new		(int			scanning,
				 unsigned int *		services,
				 vbi_bool		interlaced,
				 vbi_bool		synchronous);


/* proxy-msg.h */

typedef enum
{
	VBI_CHN_PRIO_BACKGROUND  = 1,
	VBI_CHN_PRIO_INTERACTIVE = 2,
	VBI_CHN_PRIO_DEFAULT     = VBI_CHN_PRIO_INTERACTIVE,
	VBI_CHN_PRIO_RECORD      = 3

} VBI_CHN_PRIO;

typedef enum
{
	VBI_CHN_SUBPRIO_MINIMAL  = 0x00,
	VBI_CHN_SUBPRIO_CHECK    = 0x10,
	VBI_CHN_SUBPRIO_UPDATE   = 0x20,
	VBI_CHN_SUBPRIO_INITIAL  = 0x30,
	VBI_CHN_SUBPRIO_VPS_PDC  = 0x40

} VBI_CHN_SUBPRIO;

typedef struct
{
	uint8_t			is_valid;
	uint8_t			sub_prio;
	uint8_t			allow_suspend;

	uint8_t			reserved0;
	time_t			min_duration;
	time_t			exp_duration;

	uint8_t			reserved1[16];
} vbi_channel_profile;

typedef enum
{
        VBI_PROXY_DAEMON_NO_TIMEOUTS   = 1<<0

} VBI_PROXY_DAEMON_FLAGS;

typedef enum
{
        VBI_PROXY_CLIENT_NO_TIMEOUTS   = 1<<0,
        VBI_PROXY_CLIENT_NO_STATUS_IND = 1<<1

} VBI_PROXY_CLIENT_FLAGS;

typedef enum
{
        VBI_PROXY_CHN_RELEASE = 1<<0,
        VBI_PROXY_CHN_TOKEN   = 1<<1,
        VBI_PROXY_CHN_FLUSH   = 1<<2,
        VBI_PROXY_CHN_NORM    = 1<<3,
        VBI_PROXY_CHN_FAIL    = 1<<4,

        VBI_PROXY_CHN_NONE    = 0

} VBI_PROXY_CHN_FLAGS;

typedef enum
{
        VBI_API_UNKNOWN,
        VBI_API_V4L1,
        VBI_API_V4L2,
        VBI_API_BKTR
} VBI_DRIVER_API_REV;

#define VBIPROXY_VERSION                   0x00000100
#define VBIPROXY_COMPAT_VERSION            0x00000100

/* proxy-client.h */

#include <sys/time.h> /* struct timeval */

typedef struct vbi_proxy_client vbi_proxy_client;

typedef enum
{
   VBI_PROXY_EV_CHN_GRANTED   = 1<<0,
   VBI_PROXY_EV_CHN_CHANGED   = 1<<1,
   VBI_PROXY_EV_NORM_CHANGED  = 1<<2,
   VBI_PROXY_EV_CHN_RECLAIMED = 1<<3,
   VBI_PROXY_EV_NONE          = 0
} VBI_PROXY_EV_TYPE;

typedef void VBI_PROXY_CLIENT_CALLBACK ( void * p_client_data,
                                         VBI_PROXY_EV_TYPE ev_mask );

/* forward declaration from io.h */
struct vbi_capture_buffer;

extern vbi_proxy_client *
vbi_proxy_client_create( const char *dev_name,
                         const char *p_client_name,
                         VBI_PROXY_CLIENT_FLAGS client_flags,
                         char **pp_errorstr,
                         int trace_level );

extern void
vbi_proxy_client_destroy( vbi_proxy_client * vpc );

extern vbi_capture *
vbi_proxy_client_get_capture_if( vbi_proxy_client * vpc );

extern VBI_PROXY_CLIENT_CALLBACK *
vbi_proxy_client_set_callback( vbi_proxy_client * vpc,
                               VBI_PROXY_CLIENT_CALLBACK * p_callback,
                               void * p_data );

extern VBI_DRIVER_API_REV
vbi_proxy_client_get_driver_api( vbi_proxy_client * vpc );

extern int
vbi_proxy_client_channel_request( vbi_proxy_client * vpc,
                                  VBI_CHN_PRIO chn_prio,
                                  vbi_channel_profile * chn_profile );

extern int
vbi_proxy_client_channel_notify( vbi_proxy_client * vpc,
                                 VBI_PROXY_CHN_FLAGS notify_flags,
                                 unsigned int scanning );

typedef enum
{
   VBI_PROXY_SUSPEND_START,
   VBI_PROXY_SUSPEND_STOP
} VBI_PROXY_SUSPEND;

extern int
vbi_proxy_client_channel_suspend( vbi_proxy_client * vpc,
                                  VBI_PROXY_SUSPEND cmd );

int
vbi_proxy_client_device_ioctl( vbi_proxy_client * vpc,
                               int request,
                               void * p_arg );

extern int
vbi_proxy_client_get_channel_desc( vbi_proxy_client * vpc,
                                   unsigned int * p_scanning,
                                   vbi_bool * p_granted );

extern vbi_bool
vbi_proxy_client_has_channel_control( vbi_proxy_client * vpc );



/* exp-gfx.h */

extern void		vbi_draw_vt_page_region(vbi_page *pg, vbi_pixfmt fmt,
						void *canvas, int rowstride,
						int column, int row,
						int width, int height,
						int reveal, int flash_on);
_vbi_inline void
vbi_draw_vt_page(vbi_page *pg, vbi_pixfmt fmt, void *canvas,
		 int reveal, int flash_on)
{
	vbi_draw_vt_page_region(pg, fmt, canvas, -1, 0, 0,
				pg->columns, pg->rows, reveal, flash_on);
}

extern void		vbi_draw_cc_page_region(vbi_page *pg, vbi_pixfmt fmt,
						void *canvas, int rowstride,
						int column, int row,
						int width, int height);

_vbi_inline void
vbi_draw_cc_page(vbi_page *pg, vbi_pixfmt fmt, void *canvas)
{
	vbi_draw_cc_page_region(pg, fmt, canvas, -1, 0, 0, pg->columns, pg->rows);
}

extern void vbi_get_max_rendered_size(int *w, int *h);
extern void vbi_get_vt_cell_size(int *w, int *h);


/* exp-txt.h */

extern int		vbi_print_page_region(vbi_page *pg, char *buf, int size,
					      const char *format, vbi_bool table, vbi_bool ltr,
					      int column, int row, int width, int height);

_vbi_inline int
vbi_print_page(vbi_page *pg, char *buf, int size,
	       const char *format, vbi_bool table, vbi_bool ltr)
{
	return vbi_print_page_region(pg, buf, size,
				     format, table, ltr,
				     0, 0, pg->columns, pg->rows);
}


/* hamm.h */

extern const uint8_t		_vbi_bit_reverse [256];
extern const uint8_t		_vbi_hamm8_fwd [16];
extern const int8_t		_vbi_hamm8_inv [256];
extern const int8_t		_vbi_hamm24_inv_par [3][256];


_vbi_inline unsigned int
vbi_rev8			(unsigned int		c)
{
	return _vbi_bit_reverse[(uint8_t) c];
}

_vbi_inline unsigned int
vbi_rev16			(unsigned int		c)
{
	return _vbi_bit_reverse[(uint8_t) c] * 256
		+ _vbi_bit_reverse[(uint8_t)(c >> 8)];
}

_vbi_inline unsigned int
vbi_rev16p			(const uint8_t *	p)
{
	return _vbi_bit_reverse[p[0]] * 256
		+ _vbi_bit_reverse[p[1]];
}

_vbi_inline unsigned int
vbi_par8			(unsigned int		c)
{
	c &= 255;

	/* if 0 == (inv_par[] & 32) change bit 7 of c. */
	c ^= 128 & ~(_vbi_hamm24_inv_par[0][c] << 2);

	return c;
}

_vbi_inline int
vbi_unpar8			(unsigned int		c)
{
/* Disabled until someone finds a reliable way
   to test for cmov support at compile time. */
#if 0
	int r = c & 127;

	/* This saves cache flushes and an explicit branch. */
	__asm__ (" testb	%1,%1\n"
		 " cmovp	%2,%0\n"
		 : "+&a" (r) : "c" (c), "rm" (-1));
	return r;
#endif
	if (_vbi_hamm24_inv_par[0][(uint8_t) c] & 32) {
		return c & 127;
	} else {
		/* The idea is to OR results together to find a parity
		   error in a sequence, rather than a test and branch on
		   each byte. */
		return -1;
	}
}

extern void
vbi_par				(uint8_t *		p,
				 unsigned int		n);
extern int
vbi_unpar			(uint8_t *		p,
				 unsigned int		n);

_vbi_inline unsigned int
vbi_ham8			(unsigned int		c)
{
	return _vbi_hamm8_fwd[c & 15];
}

_vbi_inline int
vbi_unham8			(unsigned int		c)
{
	return _vbi_hamm8_inv[(uint8_t) c];
}

_vbi_inline int
vbi_unham16p			(const uint8_t *	p)
{
	return ((int) _vbi_hamm8_inv[p[0]])
	  | (((int) _vbi_hamm8_inv[p[1]]) << 4);
}

extern void
vbi_ham24p			(uint8_t *		p,
				 unsigned int		c);
extern int
vbi_unham24p			(const uint8_t *	p)
  _vbi_pure;



/* cc.h */

extern vbi_bool		vbi_fetch_cc_page(vbi_decoder *vbi, vbi_page *pg,
					  vbi_pgno pgno, vbi_bool reset);


/* teletext_decoder.h */

typedef enum {
	VBI_WST_LEVEL_1,   
	VBI_WST_LEVEL_1p5, 
	VBI_WST_LEVEL_2p5,
	VBI_WST_LEVEL_3p5  
} vbi_wst_level;


extern void		vbi_teletext_set_default_region(vbi_decoder *vbi, int default_region);
extern void		vbi_teletext_set_level(vbi_decoder *vbi, int level);

extern vbi_bool		vbi_fetch_vt_page(vbi_decoder *vbi, vbi_page *pg,
					  vbi_pgno pgno, vbi_subno subno,
					  vbi_wst_level max_level, int display_rows,
					  vbi_bool navigation);
extern int		vbi_page_title(vbi_decoder *vbi, int pgno, int subno, char *buf);

extern void		vbi_resolve_link(vbi_page *pg, int column, int row,
					 vbi_link *ld);
extern void		vbi_resolve_home(vbi_page *pg, vbi_link *ld);


/* tables.h */

extern const char *	vbi_rating_string(vbi_rating_auth auth, int id);
extern const char *	vbi_prog_type_string(vbi_prog_classf classf, int id);


/* packet-830.h */

extern vbi_bool
vbi_decode_teletext_8301_cni	(unsigned int *		cni,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_decode_teletext_8301_local_time
				(time_t *		utc_time,
				 int *			seconds_east,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2, 3))
#endif
  ;
extern vbi_bool
vbi_decode_teletext_8302_cni	(unsigned int *		cni,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_decode_teletext_8302_pdc	(vbi_program_id *	pid,
				 const uint8_t		buffer[42])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;


/* vps.h */

extern vbi_bool
vbi_decode_vps_cni		(unsigned int *		cni,
				 const uint8_t		buffer[13])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_encode_vps_cni		(uint8_t		buffer[13],
				 unsigned int		cni)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_decode_vps_pdc		(vbi_program_id *	pid,
				 const uint8_t		buffer[13])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
extern vbi_bool
vbi_encode_vps_pdc		(uint8_t		buffer[13],
				 const vbi_program_id *	pid)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
vbi_bool
vbi_decode_dvb_pdc_descriptor	(vbi_program_id *	pid,
				 const uint8_t		buffer[5])
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;
vbi_bool
vbi_encode_dvb_pdc_descriptor	(uint8_t		buffer[5],
				 const vbi_program_id *	pid)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
  ;



/* vbi.h */

typedef enum {
	VBI_NO_PAGE = 0x00,
	VBI_NORMAL_PAGE = 0x01,
	VBI_SUBTITLE_PAGE = 0x70,
	VBI_SUBTITLE_INDEX = 0x78,
	VBI_NONSTD_SUBPAGES = 0x79,
	VBI_PROGR_WARNING = 0x7A,
	VBI_CURRENT_PROGR = 0x7C,
	VBI_NOW_AND_NEXT = 0x7D,
	VBI_PROGR_INDEX = 0x7F,
	VBI_PROGR_SCHEDULE = 0x81,
	VBI_UNKNOWN_PAGE = 0xFF
} vbi_page_type;

extern void		vbi_set_brightness(vbi_decoder *vbi, int brightness);
extern void		vbi_set_contrast(vbi_decoder *vbi, int contrast);


extern vbi_decoder *	vbi_decoder_new(void);
extern void		vbi_decoder_delete(vbi_decoder *vbi);
extern void		vbi_decode(vbi_decoder *vbi, vbi_sliced *sliced,
				   int lines, double timestamp);
extern void             vbi_channel_switched(vbi_decoder *vbi, vbi_nuid nuid);
extern vbi_page_type	vbi_classify_page(vbi_decoder *vbi, vbi_pgno pgno,
					  vbi_subno *subno, char **language);
extern void		vbi_version(unsigned int *major, unsigned int *minor, unsigned int *micro);
extern void
vbi_set_log_fn			(vbi_log_mask		mask,
				 vbi_log_fn *		log_fn,
				 void *			user_data);



#ifdef __cplusplus
}
#endif

#endif /* __LIBZVBI_H__ */
