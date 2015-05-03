/*
 *  libzvbi -- Events
 *
 *  Copyright (C) 2000, 2001, 2002 Michael H. Schimek
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

/* $Id: event.h,v 1.16 2009/12/14 23:43:29 mschimek Exp $ */

#ifndef EVENT_H
#define EVENT_H

#include "bcd.h"
#include "pdc.h"

#ifndef VBI_DECODER
#define VBI_DECODER
typedef struct vbi_decoder vbi_decoder;
#endif

/* Public */

#include <inttypes.h>

/**
 * @addtogroup Event Events
 * @ingroup HiDec
 *
 * Typically the transmission of VBI data like a Teletext or Closed
 * Caption page spans several VBI lines or even video frames. So internally
 * the data service decoder maintains caches accumulating data. When a page
 * or other object is complete it calls the respective event handler to
 * notify the application.
 *
 * Clients can register any number of handlers needed, also different
 * handlers for the same event. They will be called in the order registered
 * from the vbi_decode() function. Since they block decoding, they should
 * return as soon as possible. The event structure and all data
 * pointed to from there must be read only. The data is only valid until
 * the handler returns.
 */

/**
 * @ingroup Event
 * @brief Unique network id (a libzvbi thing).
 *
 * 0 = unknown network, bit 31 reserved for preliminary nuids.
 * Other network codes are arbitrary.
 */
typedef unsigned int vbi_nuid;

/**
 * @ingroup Event
 * @brief Network description.
 *
 * All strings are ISO 8859-1 encoded (yes that's stupid, sorry)
 * and @c NUL terminated. Prepare for empty strings. Read only.
 */
typedef struct {
	vbi_nuid		nuid;

	/**
	 * Name of the network from XDS or from a table lookup of
	 * CNIs in Teletext packet 8/30 or VPS. 
	 */ 
	signed char		name[64];

	/**
	 * Network call letters, from XDS. Empty if unknown or
	 * not applicable.
	 */
	signed char		call[40];

	/**
	 * Tape delay in minutes, from XDS. Zero if unknown or
	 * not applicable.
	 **/
	int			tape_delay;

	/**
	 * The European Broadcasting Union (EBU) maintains several tables
	 * of Country and Network Identification (CNI) codes. CNIs of type
	 * VPS, 8/30/1 and 8/30/2 can be used to identify networks during
	 * a channel scan.
	 *
	 * This field contains the CNI of the network found in a VPS
	 * packet. It can be zero if unknown or CNI's are not applicable.
	 * Note VPS has room for only 4 lsb of the country code (0xCNN).
	 *
	 * For example ZDF: 0xDC2.
	 */
	int			cni_vps;

	/**
	 * CNI of the network from Teletext packet 8/30 format 1,
	 * zero if unknown or not applicable. The country code is
	 * stored in the MSB, the network code in the LSB (0xCCNN).
	 * Note these CNIs may use different country and network codes
	 * than the PDC (VPS, 8/30/2) CNIs.
	 *
	 * For example BBC1: 0x447F, ZDF: 0x4902.
	 */
	int			cni_8301;

	/**
	 * CNI of the network from Teletext packet 8/30 format 2,
	 * zero if unknown or not applicable. The country code is
	 * stored in the MSB, the network code in the LSB (0xCCNN).
	 *
	 * For example BBC1: 0x2C7F, ZDF: 0x1DC2.
	 */
	int			cni_8302;

	int			reserved;

	/** Private. */
	int			cycle;
} vbi_network;

/*
 *  Link
 */

/**
 * @ingroup Event
 * @brief Link type.
 */
typedef enum {
	/**
	 * vbi_resolve_link() may return a link of this type on failure.
	 */
	VBI_LINK_NONE = 0,
	/**
	 * Not really a link, only vbi_link->name will be set. (Probably
	 * something like "Help! Help! The station is on fire!")
	 */
	VBI_LINK_MESSAGE,
	/**
	 * Points to a Teletext page, vbi_link->pgno and vbi_link->subno,
	 * eventually vbi_link->nuid and a descriptive text in vbi_link->name.
	 */
	VBI_LINK_PAGE,
	/**
	 * Also a Teletext page link, but this one is used exclusively
	 * to link subpages of the page containing the link.
	 */
	VBI_LINK_SUBPAGE,
	/**
	 * vbi_link->url is a HTTP URL (like "http://zapping.sf.net"),
	 * eventually accompanied by a descriptive text vbi_link->name.
	 */
	VBI_LINK_HTTP,
	/**
	 * vbi_link->url is a FTP URL (like "ftp://foo.bar.com/baz"),
	 * eventually accompanied by a descriptive text vbi_link->name.
	 */
	VBI_LINK_FTP,
	/**
	 * vbi_link->url is an e-mail address (like "mailto:foo@bar"),
	 * eventually accompanied by a descriptive text vbi_link->name.
	 */
	VBI_LINK_EMAIL,
	/** Is a trigger link id. Not useful, just ignore. */
	VBI_LINK_LID,
	/** Is a SuperTeletext link, ignore. */
	VBI_LINK_TELEWEB
} vbi_link_type;

/**
 * @ingroup Event
 * @brief ITV link type.
 *
 * Some ITV (WebTV, ATVEF) triggers include a type id intended
 * to filter relevant information. The names should speak for
 * themselves. EACEM triggers always have type @c VBI_WEBLINK_UNKNOWN.
 **/
typedef enum {
	VBI_WEBLINK_UNKNOWN = 0,
	VBI_WEBLINK_PROGRAM_RELATED,
	VBI_WEBLINK_NETWORK_RELATED,
	VBI_WEBLINK_STATION_RELATED,
	VBI_WEBLINK_SPONSOR_MESSAGE,
	VBI_WEBLINK_OPERATOR
} vbi_itv_type;

/**
 * @ingroup Event
 *
 * General purpose link description for ATVEF (ITV, WebTV in the
 * United States) and EACEM (SuperTeletext et al in Europe) triggers,
 * Teletext TOP and FLOF navigation, and for links "guessed" by
 * libzvbi from the text (e. g. page numbers and URLs). Usually
 * not all fields will be used.
 */
typedef struct vbi_link {
	/**
	 * See vbi_link_type.
	 */
	vbi_link_type			type;
	/**
	 * Links can be obtained two ways, via @ref VBI_EVENT_TRIGGER,
	 * then it arrived either through the EACEM or ATVEF transport
	 * method as flagged by this field. Or it is a navigational link
	 * returned by vbi_resolve_link(), then this field does not apply.
	 */
	vbi_bool			eacem;
	/**
	 * Some descriptive text, Latin-1, possibly blank.
	 */
	signed char 			name[80];
	signed char			url[256];
	/**
	 * A piece of ECMA script (Javascript), this may be
	 * used on WebTV or SuperTeletext pages to trigger some action.
	 * Usually blank.
	 */
	signed char			script[256];
	/**
	 * Teletext page links (no Closed Caption counterpart) can
	 * can actually reach across networks. That happens for example
	 * when vbi_resolve_link() picked up a link on a page after we
	 * switch away from that channel, or with EACEM triggers
	 * deliberately pointing to a page on another network (sic!).
	 * So the network id (if known, otherwise 0) is part of the
	 * page number. See vbi_nuid.
	 */
	vbi_nuid			nuid;
	/**
	 * @a pgno and @a subno Teletext page number, see vbi_pgno, vbi_subno.
	 */
	vbi_pgno			pgno;
	vbi_subno			subno;
	/**
	 * The time in seconds and fractions since
	 * 1970-01-01 00:00 when the link should no longer be offered
	 * to the user, similar to a HTTP cache expiration date.
	 */
	double				expires;
	/**
	 * See vbi_itv_type. This field applies only to
	 * ATVEF triggers, is otherwise @c VBI_WEBLINK_UNKNOWN.
	 */
	vbi_itv_type			itv_type;
	/**
	 * Trigger priority. 0 = emergency, should never be
	 * blocked. 1 or 2 = "high", 3 ... 5 = "medium", 6 ... 9 =
	 * "low". The default is 9. Apart of filtering triggers, this
	 * is also used to determine at which priority multiple links
	 * should be presented to the user. This field applies only to
	 * EACEM triggers, is otherwise 9.
	 */
	int				priority;
	/**
	 * Open the target without user confirmation. (Supposedly
	 * this flag will be used to trigger scripts, not to open pages,
	 * but I have yet to see such a trigger.)
	 */
	vbi_bool			autoload;
} vbi_link;

/*
 *  Aspect ratio information.
 */

/**
 * @ingroup Event
 * @brief Open subtitle information.
 *
 * Open because they have been inserted into the picture, as
 * opposed to closed subtitles (closed caption) encoded in the vbi.
 */
typedef enum {
	VBI_SUBT_NONE,		/**< No open subtitles. */
	VBI_SUBT_ACTIVE,	/**< Inserted in active picture. */
	VBI_SUBT_MATTE,		/**< Inserted in upper or lower letterbox bar. */
	VBI_SUBT_UNKNOWN	/**< Presence of open subtitles unknown. */
} vbi_subt;

/**
 * @ingroup Event
 * @brief Information about the picture aspect ratio and open subtitles.
 *
 * This is available via @ref VBI_EVENT_ASPECT.
 */
typedef struct {
	/**
	 * @a first_line and @a last_line, inclusive, describe the bounds of active
	 * video, i. e. without the black bars in letterbox mode. These are
	 * <em>first field</em> line numbers according to the ITU-R line
	 * numbering scheme, see vbi_sliced. For example PAL 23 ... 310 (288 lines),
	 * NTSC 22 ... 262 (240 lines).
	 */
 	int			first_line;
	int			last_line;
	/**
	 * The picture aspect ratio in <em>anamorphic</em> mode,
	 * 16/9 for example. Normal or letterboxed video has aspect ratio 1/1.
	 */
	double			ratio;
	/**
	 * @c TRUE when the source is known to be film transferred to
	 * video, as opposed to interlaced video from a video camera. (This is
	 * actually a helper flag for PALPlus decoders, but it may assist
	 * deinterlacers too.)
	 */
 	vbi_bool		film_mode;
	/**
	 * Describes how subtitles are inserted into the picture,
	 * see vbi_subt for details.
	 */
	vbi_subt		open_subtitles;
} vbi_aspect_ratio;

/*
 *  Program Info
 *
 *  ATTN this is new stuff and subject to change
 */

/**
 * @ingroup Event
 * @brief Program rating source.
 *
 * If program rating information is available (also known in the
 * U. S. as V-Chip data), this describes which rating scheme is
 * being used: U. S. film, U. S. TV, Canadian English or French TV. 
 * You can convert the rating code to a string with
 * vbi_rating_string().
 *
 * When the scheme is @c VBI_RATING_TV_US, additionally the
 * DLSV rating flags will be set.
 */
typedef enum {
	VBI_RATING_AUTH_NONE = 0,
	VBI_RATING_AUTH_MPAA,
	VBI_RATING_AUTH_TV_US,
	VBI_RATING_AUTH_TV_CA_EN,
	VBI_RATING_AUTH_TV_CA_FR
} vbi_rating_auth;

/**
 * @ingroup Event
 * @name US TV rating flags
 * @{
 */
#define VBI_RATING_D 0x08 /**< "sexually suggestive dialog" */
#define VBI_RATING_L 0x04 /**< "indecent language" */
#define VBI_RATING_S 0x02 /**< "sexual situations" */
#define VBI_RATING_V 0x01 /**< "violence" */
/** @} */

extern const char *	vbi_rating_string(vbi_rating_auth auth, int id);

/**
 * @ingroup Event
 * @brief Program classification schemes.
 *
 * libzvbi understands two different program classification schemes,
 * the EIA-608 based in the United States and the ETS 300 231 based
 * one in Europe. You can convert the program type code into a
 * string with vbi_prog_type_string().
 **/
typedef enum {
	VBI_PROG_CLASSF_NONE = 0,
	VBI_PROG_CLASSF_EIA_608,
	VBI_PROG_CLASSF_ETS_300231
} vbi_prog_classf;

/**
 * @addtogroup Event
 * @{
 */
extern const char *	vbi_prog_type_string(vbi_prog_classf classf, int id);
/** @} */

/**
 * @ingroup Event
 * @brief Type of audio transmitted on one (mono or stereo)
 * audio track.
 */
/* code depends on order, don't change */
typedef enum {
	VBI_AUDIO_MODE_NONE = 0,		/**< No sound. */
	VBI_AUDIO_MODE_MONO,			/**< Mono audio. */
	VBI_AUDIO_MODE_STEREO,			/**< Stereo audio. */
	VBI_AUDIO_MODE_STEREO_SURROUND,		/**< Surround. */
	VBI_AUDIO_MODE_SIMULATED_STEREO,	/**< ? */
	/**
	 * Spoken descriptions of the program for the blind, on a secondary audio track.
	 */
	VBI_AUDIO_MODE_VIDEO_DESCRIPTIONS,
	/**
	 * Unrelated to the current program.
	 */
	VBI_AUDIO_MODE_NON_PROGRAM_AUDIO,

	VBI_AUDIO_MODE_SPECIAL_EFFECTS,		/**< ? */
	VBI_AUDIO_MODE_DATA_SERVICE,		/**< ? */
	/**
	 * We have no information what is transmitted.
	 */
	VBI_AUDIO_MODE_UNKNOWN
} vbi_audio_mode;

/**
 * @ingroup Event
 *
 * Information about the current program, preliminary.
 */
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

/**
 * @addtogroup Event
 * @{
 */
extern void		vbi_reset_prog_info(vbi_program_info *pi);
/** @} */

/**
 * @ingroup Event
 * @name Event types.
 * @{
 */

/**
 * @anchor VBI_EVENT_
 * No event.
 */
#define VBI_EVENT_NONE		0x0000
/**
 * The vbi decoding context is about to be closed. This event is
 * sent by vbi_decoder_delete() and can be used to clean up
 * event handlers.
 */
#define	VBI_EVENT_CLOSE		0x0001
/**
 * The vbi decoder received and cached another Teletext page
 * designated by ev.ttx_page.pgno and ev.ttx_page.subno.
 * 
 * ev.ttx_page.roll_header flags the page header as suitable for
 * rolling page numbers, e. g. excluding pages transmitted out
 * of order.
 * 
 * The ev.ttx_page.header_update flag is set when the header,
 * excluding the page number and real time clock, changed since the
 * last @c VBI_EVENT_TTX_PAGE. Note this may happen at midnight when the
 * date string changes. The ev.ttx_page.clock_update flag is set when
 * the real time clock changed since the last @c VBI_EVENT_TTX_PAGE (that is
 * at most once per second). They are both set at the first
 * @c VBI_EVENT_TTX_PAGE sent and unset while the received header
 * or clock field is corrupted.
 * 
 * If any of the roll_header, header_update or clock_update flags
 * are set ev.ttx_page.raw_header is a pointer to the raw header data
 * (40 bytes), which remains valid until the event handler returns.
 * ev.ttx_page.pn_offset will be the offset (0 ... 37) of the three
 * digit page number in the raw or formatted header. Allways call
 * vbi_fetch_vt_page() for proper translation of national characters
 * and character attributes, the raw header is only provided here
 * as a means to quickly detect changes.
 */
#define	VBI_EVENT_TTX_PAGE	0x0002
/**
 * A Closed Caption page has changed and needs visual update.
 * The page or "CC channel" is designated by ev.caption.pgno,
 * see vbi_pgno for details.
 * 
 * When the client is monitoring this page, the expected action is
 * to call vbi_fetch_cc_page(). To speed up rendering more detailed
 * update information is provided in vbi_page.dirty, see #vbi_page.
 * The vbi_page will be a snapshot of the status at fetch time
 * and not event time, vbi_page.dirty accumulates all changes since
 * the last fetch.
 */
#define VBI_EVENT_CAPTION	0x0004
/**
 * Some station/network identifier has been received or is no longer
 * transmitted (vbi_network all zero, eg. after a channel switch).
 * ev.network is a vbi_network object, read only. The event will not
 * repeat*) unless a different identifier has been received and confirmed.
 *
 * Minimum time to identify network, when data service is transmitted:
 * <table>
 * <tr><td>VPS (DE/AT/CH only):</td><td>0.08 s</td></tr>
 * <tr><td>Teletext PDC, 8/30:</td><td>2 s</td></tr>
 * <tr><td>XDS (US only):</td><td>unknown, between 0.1x to 10x seconds</td></tr>
 * </table>
 *
 * *) VPS/TTX and XDS will not combine in real life, feeding the decoder
 *    with artificial data can confuse the logic.
 */
#define	VBI_EVENT_NETWORK	0x0008
/**
 * @anchor VBI_EVENT_TRIGGER
 * 
 * Triggers are sent by broadcasters to start some action on the
 * user interface of modern TVs. Until libzvbi implements all ;-) of
 * WebTV and SuperTeletext the information available are program
 * related (or unrelated) URLs, short messages and Teletext
 * page links.
 * 
 * This event is sent when a trigger has fired, ev.trigger
 * points to a vbi_link structure describing the link in detail.
 * The structure must be read only.
 */
#define	VBI_EVENT_TRIGGER	0x0010
/**
 * @anchor VBI_EVENT_ASPECT
 *
 * The vbi decoder received new information (potentially from
 * PAL WSS, NTSC XDS or EIA-J CPR-1204) about the program
 * aspect ratio. ev.ratio is a pointer to a vbi_ratio structure.
 * The structure must be read only.
 */
#define	VBI_EVENT_ASPECT	0x0040
/**
 * We have new information about the current or next program.
 * ev.prog_info is a vbi_program_info pointer (due to size), read only.
 *
 * Preliminary.
 *
 * XXX Info from Teletext not implemented yet.
 * XXX Change to get_prog_info. network ditto?
 */
#define	VBI_EVENT_PROG_INFO	0x0080
/**
 * Like @a VBI_EVENT_NETWORK, but this event will also be sent
 * when the decoder cannot determine a network name.
 *
 * @since 0.2.20
 */
#define	VBI_EVENT_NETWORK_ID	0x0100
/**
 * A new local time has been received. ev.local_time points to a
 * vbi_local_time structure with details.
 */
#define VBI_EVENT_LOCAL_TIME	0x0400
/**
 * A new Program ID (VPS or PDC) has been received. ev.prog_id points
 * to a vbi_program_id structure with details.
 */
#define VBI_EVENT_PROG_ID	0x0800
/** @} */

/**
 * Specifies if daylight-saving time is in effect in the time zone of
 * the intended audience of the network.
 */
typedef enum {
	/** The network does not provide any DST information. */
	VBI_DST_UNKNOWN = 0,

	/**
	 * A DST offset (+0 or +1 hour) has been added to the time
	 * zone offset.
	 */
	VBI_DST_INCLUDED,

	/** Daylight-saving time is not in effect. */
	VBI_DST_INACTIVE,

	/**
	 * Daylight-saving time is in effect, and +1 hour has been
	 * added to the time zone offset.
	 */
	VBI_DST_ACTIVE
} vbi_dst_state;

typedef struct {
	/** The current time in the UTC zone. */
	time_t			time;

	/**
	 * The offset of the time zone of the intended audience of the
	 * network in seconds east of UTC. For example the EST zone is
	 * offset by -18000, GMT by 0, and CET by +3600 seconds. An
	 * additional +3600 second daylight-saving time offset may
	 * have been added as specified by @a dst_state, giving for
	 * example an EDT offset of -14400 seconds.
	 */
	int			seconds_east;

	/** If FALSE, the network does not provide a time zone offset. */
	vbi_bool		seconds_east_valid;

	/**
	 * Whether daylight-saving time is currently in effect in the
	 * time zone of the intended audience of the network.
	 */
	vbi_dst_state		dst_state;
} vbi_local_time;

/* Experimental CC608 decoder. */
#define _VBI_EVENT_CC608 0x1000
#define _VBI_EVENT_CC608_STREAM 0x2000
struct _vbi_event_cc608_page;
struct _vbi_event_cc608_stream;

/**
 * @example examples/network.c
 * Network identification example.
 */

#include <inttypes.h>

/**
 * @ingroup Event
 * @brief Event union.
 */
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

/**
 * @addtogroup Event
 * @{
 */
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
/** @} */

/* Private */

extern void		vbi_send_event(vbi_decoder *vbi, vbi_event *ev);

#endif /* EVENT_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
