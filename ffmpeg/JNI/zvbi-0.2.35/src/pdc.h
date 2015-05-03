/*
 *  libzvbi - Program Delivery Control
 *
 *  Copyright (C) 2000-2004 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id: pdc.h,v 1.6 2009/03/23 01:30:19 mschimek Exp $ */

#ifndef __ZVBI_PDC_H__
#define __ZVBI_PDC_H__

#include <stdio.h>		/* FILE */
#include <inttypes.h>		/* uint8_t */

#include "version.h"
#include "macros.h"
#include "bcd.h"		/* vbi_pgno */
#include "network.h"		/* vbi_cni_type */

VBI_BEGIN_DECLS

/* Public */

#include <time.h>		/* time_t */

/**
 * @addtogroup ProgramID
 * @{
 */

/**
 * @brief Program Identification Label.
 *
 * This is a packed representation of the originally announced start
 * date and time ("AT-2" in EN 300 231 parlance, "Scheduled Start
 * Time" in EIA 608-B) of a program.
 *
 * @since 0.2.34
 */
typedef unsigned int vbi_pil;

/**
 * @brief Macro to create a PIL.
 *
 * Valid values for @a month are 1 ... 12, for @a day 1 ... 31, for
 * @a hour 0 ... 23 and for @a minute 0 ... 59.
 *
 * Note in the PDC system (EN 300 231) networks may also transmit
 * unreal dates or times like 14-00 25:63. You can determine if a PIL
 * represents a valid date and time with the vbi_pil_is_valid_date()
 * function.
 *
 * @since 0.2.34
 */
#define VBI_PIL(month, day, hour, minute)				\
	(((day) << 15) | ((month) << 11) | ((hour) << 6) | (minute))

/** Extract the month from a PIL. Valid values are in range 1 ... 12. */
#define VBI_PIL_MONTH(pil)	(((pil) >> 11) & 15)

/** Extract the day from a PIL. Valid values are in range 1 ... 31. */
#define VBI_PIL_DAY(pil)	(((pil) >> 15) & 31)

/** Extract the hour from a PIL. Valid values are in range 0 ... 23. */
#define VBI_PIL_HOUR(pil)	(((pil) >> 6) & 31)

/** Extract the minute from a PIL. Valid values are in range 0 ... 59. */
#define VBI_PIL_MINUTE(pil)	((pil) & 63)

/**
 * @brief PIL Service Codes.
 *
 * PILs can be zero, or specify a valid date and time, or an unreal
 * time such as 31:00 if a new label has been assigned to a program
 * but no transmission time has been decided yet. Some PILs with
 * unreal date and time have a special meaning.
 *
 * These codes are defined in EN 300 231 Section 6.2, Annex E.3 and
 * Annex F, and in EIA 608-B Section 9.5.1.1.
 *
 * @since 0.2.34
 */
enum {
	/**
	 * Only in Teletext packets 8/30 format 2, VPS packets and DVB
	 * PDC descriptors: "Timer Control".
	 *
	 * No program IDs are available, use the timer to control
	 * recording.
	 */
	VBI_PIL_TIMER_CONTROL		= VBI_PIL (15, 0, 31, 63),

	/**
	 * Teletext, VPS, DVB: "Recording Inhibit/Terminate".
	 *
	 * When the PIL changes from a valid program label to @c
	 * VBI_PIL_INHIBIT_TERMINATE the current program has ended and
	 * the next program has not started yet. VCRs recording the
	 * current program shall stop recording and remove the program
	 * from their schedule. VCRs waiting for a new PIL shall
	 * continue waiting.
	 */
	VBI_PIL_INHIBIT_TERMINATE	= VBI_PIL (15, 0, 30, 63),

	/**
	 * Teletext, VPS, DVB: "Interruption".
	 *
	 * When the PIL changes from a valid program label to @c
	 * VBI_PIL_INTERRUPTION, the current program has stopped but
	 * will continue later. This code is transmitted for example
	 * at the start of a halftime pause or at a film break. VCRs
	 * recording the current program shall stop recording, but not
	 * delete the program from their schedule. The network may
	 * broadcast other programs with different PILs before the
	 * interrupted program continues. VCRs waiting for a new PIL
	 * shall continue waiting.
	 */
	VBI_PIL_INTERRUPTION		= VBI_PIL (15, 0, 29, 63),

	/**
	 * Teletext, VPS, DVB: "Continuation code".
	 *
	 * This code is transmitted during a service interruption,
	 * the PDC service should resume operation shortly. VCRs
	 * recording the current program shall continue recording,
	 * VCRs waiting for a new PIL shall continue waiting.
	 */
	VBI_PIL_CONTINUE		= VBI_PIL (15, 0, 28, 63),

	/**
	 * Teletext, VPS, DVB: "No Specific PIL Value".
	 *
	 * Networks may transmit this label with an unplanned program
	 * such as an emergency message. The program type (PTY) field
	 * may still be valid.
	 */
	VBI_PIL_NSPV			= VBI_PIL (15, 15, 31, 63),

	/**
	 * Only in XDS Current Program ID packets:
	 *
	 * The current program has ended and the next program not
	 * started yet.
	 */
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

/* Private */

/** @} */

extern void
_vbi_pil_dump			(vbi_pil		pil,
				 FILE *			fp)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((2))
#endif
;
extern vbi_bool
_vbi_pil_from_string		(vbi_pil *		pil,
				 const char **		inout_s)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
;

/**
 * @addtogroup ProgramID
 * @{
 */

/* Public */

/**
 * @brief Sources of PIDs.
 *
 * A program identification can be transmitted on different logical
 * channels. Teletext packet 8/30 format 2 contains a Label Channel
 * Identifier. XDS Program ID packets can refer to the current or next
 * program.
 *
 * This information is returned in struct @a vbi_program_id by the low
 * level functions vbi_decode_teletext_8302_pdc(),
 * vbi_decode_vps_pdc(), vbi_decode_dvb_pdc_descriptor() and the @a
 * vbi_decoder event @c VBI_EVENT_PROG_ID.
 *
 * @since 0.2.34
 */
typedef enum {
	/**
	 * Data from Teletext packet 8/30 format 2, Label Channel 0
	 * (EN 300 706 section 9.8.2, EN 300 231 section 8.2.1).
	 *
	 * Teletext packets contain a CNI, PIL, LUF, MI, PRF, PCS and
	 * PTY. They are transmitted once per second for each Label
	 * Channel in use.
	 *
	 * The purpose of Label Channels is to transmit overlapping
	 * PIDs, for example one referring the current program and
	 * another announcing the imminent start of the next
	 * program. Programs can also have multiple PIDs, for example
	 * a sports magazine with several segments, where the entire
	 * program has a PID and each segment has its own PID,
	 * transmitted on a different Label Channel.
	 *
	 * Label Channels are used in no particular order or
	 * hierarchy.
	 */
	VBI_PID_CHANNEL_LCI_0 = 0,

	/** Data from Teletext packet 8/30 format 2, Label Channel 1. */
	VBI_PID_CHANNEL_LCI_1,

	/** Data from Teletext packet 8/30 format 2, Label Channel 2. */
	VBI_PID_CHANNEL_LCI_2,

	/** Data from Teletext packet 8/30 format 2, Label Channel 3. */
	VBI_PID_CHANNEL_LCI_3,

	/**
	 * Data from a VPS packet (EN 300 231).
	 *
	 * These packets contain a CNI, PIL, PCS and PTY. They are
	 * transmitted once in each frame, i.e. 25 times per second.
	 */
	VBI_PID_CHANNEL_VPS,

	/**
	 * Data from a DVB PDC descriptor (EN 300 468 Section 6.2.29).
	 *
	 * DVB PDC descriptors contain the same PIL as a VPS packet,
	 * but no CNI, PCS or PTY.
	 */
	VBI_PID_CHANNEL_PDC_DESCRIPTOR,

	/**
	 * Data from an XDS Current Program ID packet (EIA 608-B
	 * Section 9).
	 *
	 * XDS Current/Future Program ID packets contain a PIL and
	 * tape-delayed flag. Current class packets refer to the
	 * currently transmitted program, Future class packets to the
	 * next program.
	 *
	 * Decoding of XDS Current/Future Program ID packets is not
	 * implemented yet.
	 */
	VBI_PID_CHANNEL_XDS_CURRENT,

	/**
	 * Data from an XDS Future Program ID packet.
	 */
	VBI_PID_CHANNEL_XDS_FUTURE,

	/** Note this value may change. */
	VBI_MAX_PID_CHANNELS
} vbi_pid_channel;

/**
 * @brief PDC Program Control Status - Audio.
 *
 * This information is available with Teletext and VPS program IDs and
 * returned in struct vbi_program_id by the low level functions
 * vbi_decode_teletext_8302_pdc(), vbi_decode_vps_pdc() and the @a
 * vbi_decoder event @c VBI_EVENT_PROG_ID.
 *
 * @since 0.2.34
 */
typedef enum {
	/** Nothing known about audio channels. */
	VBI_PCS_AUDIO_UNKNOWN = 0,

	/** Mono audio is broadcast. */
	VBI_PCS_AUDIO_MONO,

	/** Stereo audio. */
	VBI_PCS_AUDIO_STEREO,

	/** Primary language on left channel, secondary on right. */ 
	VBI_PCS_AUDIO_BILINGUAL
} vbi_pcs_audio;

/**
 * @brief Program Identification.
 *
 * This structure contains a Program ID received via Teletext packet
 * 8/30 format 2, VPS, a DVB PDC descriptor or an XDS Current/Future
 * Program ID packet. When the source does not provide all this
 * information, libzvbi initializes the respective fields with an
 * appropriate value.
 *
 * @since 0.2.34
 */
typedef struct {
	/** Source of this PID. */
	vbi_pid_channel			channel;

	/**
	 * Network identifier type, one of
	 * - @c VBI_CNI_TYPE_NONE,
	 * - @c VBI_CNI_TYPE_8302 or
	 * - @c VBI_CNI_TYPE_VPS.
	 */
	vbi_cni_type			cni_type;

	/**
	 * Country and Network Identifier provided by Teletext packet
	 * 8/30 format 2 and VPS. Note when the source is Teletext and
	 * the LUF flag is set, this CNI may refer to a different
	 * network than the one transmitting the PID.
	 */
	unsigned int			cni;

	/**
	 * Program Identification Label. This is the only information
	 * available from all PID sources.
	 */
	vbi_pil				pil;

	/**
	 * PDC Label Update Flag (only transmitted in Teletext
	 * packets). When this flag is set, the PID is intended to
	 * update VCR memory, it does not refer to the current
	 * program. According to the examples in EN 300 231 Annex E.3
	 * however VCRs should probably also handle the PID as if a @c
	 * VBI_PIL_INHIBIT_TERMINATE service code was transmitted.
	 *
	 * This flag is used to announce a new PIL for the current
	 * program. The CNI may refer to a different network than the
	 * one transmitting the PID, for example when a program is
	 * about to overrun and will continue on a different network.
	 * If a program is postponed and no transmission time has been
	 * decided yet the new PIL may contain an arbitrary or unreal
	 * time.
	 */
	vbi_bool			luf;

	/**
	 * PDC Mode Identifier (Teletext). When @c TRUE the end of
	 * transmission of this PIL coincides exactly with the end of
	 * the program. When @c FALSE the program ends 30 seconds
	 * after the PIL is no longer transmitted. Note the flag
	 * applies to all valid PILs as well as the @c
	 * VBI_PIL_INHIBIT_TERMINATE and @c VBI_PIL_INTERRUPTION
	 * service codes.
	 */
	vbi_bool			mi;

	/**
	 * PDC Prepare to Record Flag (Teletext). When @c TRUE the
	 * program identified by this PID is about to start. A
	 * transition to @c FALSE indicates the immediate start of the
	 * program, regardless of the state of the MI flag.
	 */
	vbi_bool			prf;

	/** PDC Program Control Status - Audio (Teletext and VPS). */
	vbi_pcs_audio			pcs_audio;

	/**
	 * PDC Program Type code (Teletext and VPS), can be 0 or 0xFF
	 * if none or unknown.
	 */
	unsigned int			pty;

	/**
	 * XDS T flag. @c TRUE if a program is routinely tape delayed
	 * (for mountain and pacific time zones). @c FALSE if not or
	 * this is unknown.
	 *
	 * This flag is used to determine if an offset is necessary
	 * because of local station tape delays. The amount of tape
	 * delay used for a given time zone is transmitted in a XDS
	 * Channel Tape Delay packet.
	 */
	vbi_bool			tape_delayed;

	void *				_reserved2[2];
	int				_reserved3[4];
} vbi_program_id;

/** @} */

/* Private */

extern void
_vbi_program_id_dump		(const vbi_program_id *	pid,
				 FILE *			fp)
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  _vbi_nonnull ((1, 2))
#endif
;


VBI_END_DECLS

#endif /* __ZVBI_PDC_H__ */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
