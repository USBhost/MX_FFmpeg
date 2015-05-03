/*
 *  libzvbi -- Extended Data Service demultiplexer
 *
 *  Copyright (C) 2000-2005 Michael H. Schimek
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

/* $Id: xds_demux.c,v 1.11 2008/02/19 00:35:23 mschimek Exp $ */

#include "site_def.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "misc.h"		/* vbi_log_printf() */
#include "hamm.h"		/* vbi_ipar8() */
#include "tables.h"		/* vbi_rating/prog_type_string() */
#include "xds_demux.h"

/**
 * @addtogroup XDSDemux Extended Data Service (XDS) demultiplexer
 * @ingroup LowDec
 * @brief Separating XDS data from a Closed Caption stream
 *   (EIA 608).
 */

#ifndef XDS_DEMUX_LOG
#define XDS_DEMUX_LOG 0
#endif

/* LOG (level, format, args...) */
#define log(format, args...)						\
do {									\
	if (XDS_DEMUX_LOG)						\
		fprintf (stderr, format , ##args);			\
} while (0)

static void
xdump				(const vbi_xds_packet *	xp,
				 FILE *			fp)
{
	unsigned int i;

	for (i = 0; i < xp->buffer_size; ++i)
		fprintf (fp, " %02x", xp->buffer[i]);

	fputs (" '", fp);

	for (i = 0; i < xp->buffer_size; ++i)
		fputc (_vbi_to_ascii (xp->buffer[i]), fp);

	fputc ('\'', fp);
}

/** @internal */
void
_vbi_xds_packet_dump		(const vbi_xds_packet *	xp,
				 FILE *			fp)
{
	static const char *month_names [] = {
		"0?", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
		"Sep", "Oct", "Nov", "Dec", "13?", "14?", "15?"
	};
	static const char *map_type [] = {
		"unknown", "mono", "simulated stereo", "stereo",
		"stereo surround", "data service", "unknown", "none"
	};
	static const char *sap_type [] = {
		"unknown", "mono", "video descriptions", "non-program audio",
		"special effects", "data service", "unknown", "none"
	};
	static const char *language [] = {
		"unknown", "English", "Spanish", "French", "German",
		"Italian", "Other", "none"
	};
	static const char *cgmsa [] = {
		"copying permitted", "-", "one copy allowed",
		"no copying permitted"
	};
	static const char *scrambling [] = {
		"no pseudo-sync pulse",
		"pseudo-sync pulse on; color striping off",
		"pseudo-sync pulse on; 2-line color striping on",
		"pseudo-sync pulse on; 4-line color striping on"
	};
	static const char *day_names [] = {
		"0?", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	unsigned int i;

	assert (NULL != xp);
	assert (NULL != fp);

	fprintf (fp, "XDS packet 0x%02x%02x ",
		 xp->xds_class * 2 + 1, xp->xds_subclass);

	switch (xp->xds_class) {
	case VBI_XDS_CLASS_CURRENT:
		fputs ("(cur. program ", fp);

		/* fall through */

	case VBI_XDS_CLASS_FUTURE:
		if (VBI_XDS_CLASS_FUTURE == xp->xds_class)
			fputs ("(fut. program ", fp);

		switch (xp->xds_subclass) {
		case VBI_XDS_PROGRAM_ID:
		{
			unsigned int month, day, hour, min;

			fputs ("id)", fp);
			xdump (xp, fp);

			if (4 != xp->buffer_size) {
			invalid:
				fputs (" (invalid)", fp);
				break;
			}

			month	= xp->buffer[3] & 15;
			day	= xp->buffer[2] & 31;
			hour	= xp->buffer[1] & 31;
			min	= xp->buffer[0] & 63;

			if (month == 0 || month > 12
			    || day == 0 || day > 31
			    || hour > 23 || min > 59)
				goto invalid;

			fprintf (fp, " (%d %s %02d:%02d UTC,",
				 day, month_names[month], hour, min);

			fprintf (fp, " D=%d L=%d Z=%d T=%d)",
				 !!(xp->buffer[1] & 0x20),
				 !!(xp->buffer[2] & 0x20),
				 !!(xp->buffer[3] & 0x20),
				 !!(xp->buffer[3] & 0x10));

			break;
		}

		case VBI_XDS_PROGRAM_LENGTH:
		{
			unsigned int lhour, lmin;

			fputs ("length)", fp);
			xdump (xp, fp);

			switch (xp->buffer_size) {
			case 2:
			case 4:
			case 5:
				break;

			default:
				goto invalid;
			}

			lhour	= xp->buffer[1] & 63;
			lmin	= xp->buffer[0] & 63;

			if (lmin > 59)
				goto invalid;

			fprintf (fp, " (%02d:%02d", lhour, lmin);

			if (xp->buffer_size >= 4) {
				unsigned int ehour, emin;

				ehour	= xp->buffer[3] & 63;
				emin	= xp->buffer[2] & 63;

				if (emin > 59)
					goto invalid;

				fprintf (fp, " elapsed=%02d:%02d",
					 ehour, emin);

				if (xp->buffer_size >= 5) {
					unsigned int esec;

					esec = xp->buffer[4] & 63;

					if (esec > 59)
						goto invalid;

					fprintf (fp, ":%02d", esec);
				}
			}

			fputc (')', fp);

			break;
		}

		case VBI_XDS_PROGRAM_NAME:
			fputs ("name)", fp);
			xdump (xp, fp);
			break;

		case VBI_XDS_PROGRAM_TYPE:
		{
			unsigned int i;

			fputs ("type)", fp);
			xdump (xp, fp);

			if (xp->buffer_size < 1)
				goto invalid;

			fputs (" (", fp);

			for (i = 0; i < xp->buffer_size; ++i) {
				fprintf (fp, (i > 0) ? ", %s" : "%s",
					 vbi_prog_type_string
						(VBI_PROG_CLASSF_EIA_608,
						 xp->buffer[i]));
			}

			fputc (')', fp);

			break;
		}

		case VBI_XDS_PROGRAM_RATING:
		{
			unsigned int r, g;

			fputs ("rating)", fp);
			xdump (xp, fp);
			
			if (2 != xp->buffer_size)
				goto invalid;

			r	= xp->buffer[0] & 7;
			g	= xp->buffer[1] & 7;

			fprintf (fp, " (movie: %s, tv: ",
				 vbi_rating_string (VBI_RATING_AUTH_MPAA, r));

			if (xp->buffer[0] & 0x10) {
				const char *s;
			
				if (xp->buffer[0] & 0x20)
					s = vbi_rating_string
						(VBI_RATING_AUTH_TV_CA_FR, g);
				else
					s = vbi_rating_string
						(VBI_RATING_AUTH_TV_CA_EN, g);

				fputs (s, fp);
			} else {
				fprintf (fp, "%s D=%d L=%d S=%d V=%d",
					 vbi_rating_string
					    (VBI_RATING_AUTH_TV_US, g),
					 !!(xp->buffer[0] & 0x20),
					 !!(xp->buffer[1] & 0x08),
					 !!(xp->buffer[1] & 0x10),
					 !!(xp->buffer[1] & 0x20));
			}

			fputc (')', fp);

			break;
		}

		case VBI_XDS_PROGRAM_AUDIO_SERVICES:
			fputs ("audio services)", fp);
			xdump (xp, fp);

			if (2 != xp->buffer_size)
				goto invalid;

			fprintf (fp, " (main: %s, %s; second: %s, %s)",
				 map_type[xp->buffer[0] & 7],
				 language[(xp->buffer[0] >> 3) & 7],
				 sap_type[xp->buffer[1] & 7],
				 language[(xp->buffer[1] >> 3) & 7]);

			break;

		case VBI_XDS_PROGRAM_CAPTION_SERVICES:
			fputs ("caption services)", fp);
			xdump (xp, fp);

			if (xp->buffer_size < 1
			    || xp->buffer_size > 8)
				goto invalid;

			fputc ('(', fp);

			for (i = 0; i < xp->buffer_size; ++i) {
				fprintf (fp, "%sline=%u channel=%u %s %s",
					 (0 == i) ? "" : ", ",
					 (xp->buffer[i] & 4) ? 284 : 21,
					 (xp->buffer[i] & 2) ? 2 : 1,
					 (xp->buffer[i] & 1) ?
					 "text" : "captioning",
					 language[(xp->buffer[i] >> 3) & 7]);
			}

			fputc (')', fp);

			break;

		case VBI_XDS_PROGRAM_CGMS:
			fputs ("cgms)", fp);
			xdump (xp, fp);

			if (1 != xp->buffer_size)
				goto invalid;

			fprintf (fp, " (%s", cgmsa[(xp->buffer[0] >> 3) & 3]);

			if (xp->buffer[0] & 0x18)
				fprintf (fp, ", %s",
					 scrambling[(xp->buffer[0] >> 1) & 3]);

			fprintf (fp, ", analog_source=%u)", xp->buffer[0] & 1);

			break;

		case VBI_XDS_PROGRAM_ASPECT_RATIO:
		{
			unsigned int first_line, last_line;

			fputs ("aspect)", fp);
			xdump (xp, fp);

			if (2 != xp->buffer_size
			    && 3 != xp->buffer_size)
				goto invalid;

			first_line	= 22 + (xp->buffer[0] & 63);
			last_line	= 262 - (xp->buffer[1] & 63);

			fprintf (fp, " (active picture %u ... %u%s)",
				 first_line, last_line,
				 (3 == xp->buffer_size
				  && (xp->buffer[2] & 1)) ?
				 " anamorphic" : "");

			break;
		}

		case VBI_XDS_PROGRAM_DATA:
		{
			unsigned int rating;
			unsigned int lhour, lmin;
			unsigned int ehour, emin;

			fputs ("data)", fp);
			xdump (xp, fp);

			/* XXX ok? */
			if (xp->buffer_size < 10)
				goto invalid;

			rating	= xp->buffer[5] & 7;

			lhour	= xp->buffer[7] & 63;
			lmin	= xp->buffer[6] & 63;

			if (lmin > 59)
				goto invalid;

			ehour	= xp->buffer[9] & 63;
			emin	= xp->buffer[8] & 63;

			if (emin > 59)
				goto invalid;

			fputs (" (type: ", fp);

			for (i = 0; i < 5; ++i) {
				fprintf (fp, (i > 0) ? ", %s" : "%s",
					 vbi_prog_type_string
						(VBI_PROG_CLASSF_EIA_608,
						 xp->buffer[i]));
			}

			fprintf (fp, "; rating: %s; "
				 "length: %02d:%02d; "
				 "elapsed: %02d:%02d)",
				 vbi_rating_string (VBI_RATING_AUTH_MPAA,
						    rating),
				 lhour, lmin, ehour, emin);

			/* program name: buffer[10 ... 31] (xdump'ed) */

			break;
		}

		case VBI_XDS_PROGRAM_MISC_DATA:
		{
			unsigned int month, day, hour, min;

			fputs ("misc data)", fp);
			xdump (xp, fp);

			/* XXX ok? */
			if (14 != xp->buffer_size)
				goto invalid;

			month	= xp->buffer[3] & 15;
			day	= xp->buffer[2] & 31;
			hour	= xp->buffer[1] & 31;
			min	= xp->buffer[0] & 63;

			if (month == 0 || month > 12
			    || day == 0 || day > 31
			    || hour > 23 || min > 59)
				goto invalid;

			fprintf (fp, " (%d %s %02d:%02d UTC, ",
				 day, month_names[month], hour, min);

			fprintf (fp, " D=%d L=%d Z=%d T=%d",
				 !!(xp->buffer[1] & 0x20),
				 !!(xp->buffer[2] & 0x20),
				 !!(xp->buffer[3] & 0x20),
				 !!(xp->buffer[3] & 0x10));

			fprintf (fp, ", main audio: %s, %s; second: %s, %s;",
				 map_type[xp->buffer[4] & 7],
				 language[(xp->buffer[4] >> 3) & 7],
				 sap_type[xp->buffer[5] & 7],
				 language[(xp->buffer[5] >> 3) & 7]);

			for (i = 6; i < 8; ++i) {
				fprintf (fp, "%sline=%u channel=%u %s %s",
					 (6 == i) ? " caption: " : ", ",
					 (xp->buffer[i] & 4) ? 284 : 21,
					 (xp->buffer[i] & 2) ? 2 : 1,
					 (xp->buffer[i] & 1) ?
					 "text" : "captioning",
					 language[(xp->buffer[i] >> 3) & 7]);
			}

			fprintf (fp, ", call letters: ");
			for (i = 8; i < 12; ++i) {
				fputc (_vbi_to_ascii (xp->buffer[i]), fp);
			}

			/* 02 to 69 or 0x20 0x20 */
			fprintf (fp, ", channel: ");
			for (i = 12; i < 14; ++i) {
				fputc (_vbi_to_ascii (xp->buffer[i]), fp);
			}

			fputc (')', fp);

			break;
		}

		case VBI_XDS_PROGRAM_DESCRIPTION_BEGIN ...
		     VBI_XDS_PROGRAM_DESCRIPTION_END - 1:
			fprintf (fp, "description %u)",
				 (unsigned int) xp->xds_subclass
				 - (unsigned int)
				   VBI_XDS_PROGRAM_DESCRIPTION_BEGIN);
			xdump (xp, fp);
			break;

		default:
			fputs ("?)", fp);
			xdump (xp, fp);
			break;
		}

		break;

	case VBI_XDS_CLASS_CHANNEL:
		fputs ("(channel ", fp);

		switch (xp->xds_subclass) {
		case VBI_XDS_CHANNEL_NAME:
			fputs ("name)", fp);
			xdump (xp, fp);
			break;

		case VBI_XDS_CHANNEL_CALL_LETTERS:
			fputs ("call letters)", fp);
			xdump (xp, fp);
			break;

		case VBI_XDS_CHANNEL_TAPE_DELAY:
		{
			unsigned int hour, min;

			fputs ("tape delay)", fp);
			xdump (xp, fp);

			if (2 != xp->buffer_size)
				goto invalid;

			hour	= xp->buffer[1] & 31;
			min	= xp->buffer[0] & 63;

			if (min > 59)
				goto invalid;

			fprintf (fp, " (%02d:%02d)", hour, min);

			break;
		}

		case VBI_XDS_CHANNEL_TSID:
		{
			unsigned int tsid;

			fputs ("transmission signal identifier)", fp);
			xdump (xp, fp);

			if (4 != xp->buffer_size)
				goto invalid;

			tsid  = (xp->buffer[3] & 15) << 0;
			tsid += (xp->buffer[2] & 15) << 4;
			tsid += (xp->buffer[1] & 15) << 8;
			tsid += (xp->buffer[0] & 15) << 12;

			if (0 == tsid)
				goto invalid;

			fprintf (fp, " (0x%04x)", tsid);

			break;
		}

		default:
			fputs ("?)", fp);
			xdump (xp, fp);
			break;
		}

		break;

	case VBI_XDS_CLASS_MISC:
		fputs ("(misc: ", fp);

		switch (xp->xds_subclass) {
		case VBI_XDS_TIME_OF_DAY:
			fputs ("time of day)", fp);
			xdump (xp, fp);

			if (6 != xp->buffer_size)
				goto invalid;

			fprintf (fp, " (%s, %d %s %d",
				 day_names [xp->buffer[4] & 7],
				 xp->buffer[2] & 31,
				 month_names[xp->buffer[3] & 15],
				 1990 + (xp->buffer[5] & 63));

			fprintf (fp, " %02d:%02d UTC",
				 xp->buffer[1] & 31,
				 xp->buffer[0] & 63);

			fprintf (fp, " D=%u L=%u Z=%u T=%u)",
				 !!(xp->buffer[1] & 0x20),
				 !!(xp->buffer[2] & 0x20),
				 !!(xp->buffer[3] & 0x20),
				 !!(xp->buffer[3] & 0x10));

			break;

		case VBI_XDS_IMPULSE_CAPTURE_ID:
			fputs ("capture id)", fp);
			xdump (xp, fp);
			
			if (6 != xp->buffer_size)
				goto invalid;

			fprintf (fp, " (%d %s",
				 xp->buffer[2] & 31,
				 month_names[xp->buffer[3] & 15]);

			fprintf (fp, " %02d:%02d",
				 xp->buffer[1] & 31,
				 xp->buffer[0] & 63);

			fprintf (fp, " length=%02d:%02d",
				 xp->buffer[5] & 63,
				 xp->buffer[4] & 63);

			fprintf (fp, " D=%u L=%u Z=%u T=%u)",
				 !!(xp->buffer[1] & 0x20),
				 !!(xp->buffer[2] & 0x20),
				 !!(xp->buffer[3] & 0x20),
				 !!(xp->buffer[3] & 0x10));

			break;

		case VBI_XDS_SUPPLEMENTAL_DATA_LOCATION:
		{
			unsigned int i;

			fputs ("supplemental data)", fp);
			xdump (xp, fp);

			if (xp->buffer_size < 1)
				goto invalid;

			fputc ('(', fp);

			for (i = 0; i < xp->buffer_size; ++i) {
				fprintf (fp, "%sfield=%u line=%u",
					 (0 == i) ? "" : ", ",
					 !!(xp->buffer[i] & 0x20),
					 xp->buffer[i] & 31);
			}

			fputc (')', fp);

			break;
		}

		case VBI_XDS_LOCAL_TIME_ZONE:
			fputs ("time zone)", fp);
			xdump (xp, fp);
			
			if (1 != xp->buffer_size)
				goto invalid;

			fprintf (fp, " (UTC%+05d ods=%u)",
				 (xp->buffer[0] & 31) * -100,
				 !!(xp->buffer[0] & 0x20));

			break;

		case VBI_XDS_OUT_OF_BAND_CHANNEL:
		{
			unsigned int channel;

			fputs ("out of band channel number)", fp);
			xdump (xp, fp);

			if (2 != xp->buffer_size)
				goto invalid;

			channel = (xp->buffer[0] & 63)
				+ ((xp->buffer[1] & 63) << 6);

			fprintf (fp, " (%u)", channel);

			break;
		}

		case VBI_XDS_CHANNEL_MAP_POINTER:
		{
			unsigned int channel;

			fputs ("channel map pointer)", fp);
			xdump (xp, fp);

			if (2 != xp->buffer_size)
				goto invalid;

			channel = (xp->buffer[0] & 63)
				+ ((xp->buffer[1] & 63) << 6);

			fprintf (fp, " (%u)", channel);

			break;
		}

		case VBI_XDS_CHANNEL_MAP_HEADER:
		{
			unsigned int n_channels;
			unsigned int version;

			fputs ("channel map header)", fp);
			xdump (xp, fp);

			if (4 != xp->buffer_size)
				goto invalid;

			n_channels = (xp->buffer[0] & 63)
				+ ((xp->buffer[1] & 63) << 6);

			version = xp->buffer[2] & 63;

			fprintf (fp, " (n_channels: %u, version: %u)",
				 n_channels, version);

			break;
		}

		case VBI_XDS_CHANNEL_MAP:
		{
			unsigned int channel;
			vbi_bool remapped;

			fputs ("channel map)", fp);
			xdump (xp, fp);

			channel = (xp->buffer[0] & 63)
				+ ((xp->buffer[1] & 31) << 6);

			fprintf (fp, " (channel: %u)", channel);

			remapped = !!(xp->buffer[1] & 0x20);

			if (remapped) {
				unsigned int tune_channel;

				tune_channel = (xp->buffer[2] & 63)
					+ ((xp->buffer[3] & 63) << 6);

				fprintf (fp, ", remapped to: %u)",
					 tune_channel);
			}

			/* channel id: buffer[2 or 4 ... 31] (xdump'ed) */

			fputc (')', fp);

			break;
		}

		default:
			fputs ("?)", fp);
			xdump (xp, fp);
			break;
		}

		break;

	case VBI_XDS_CLASS_PUBLIC_SERVICE:
		fputs ("(pub. service ", fp);

		switch (xp->xds_subclass) {
		case VBI_XDS_WEATHER_BULLETIN:
		{
			unsigned int duration;

			fputs ("weather bulletin)", fp);
			xdump (xp, fp);

			fprintf (fp, " (event category: ");
			for (i = 0; i < 3; ++i) {
				fputc (_vbi_to_ascii (xp->buffer[i]), fp);
			}

			/* 3 digit FIPS number. */
			fprintf (fp, ", state: ");
			for (i = 3; i < 6; ++i) {
				fputc (_vbi_to_ascii (xp->buffer[i]), fp);
			}

			/* 3 digit FIPS number. */
			fprintf (fp, ", county: ");
			for (i = 6; i < 9; ++i) {
				fputc (_vbi_to_ascii (xp->buffer[i]), fp);
			}

			/* 2 digit number of quarter hours */
			duration = (xp->buffer[9] & 15) * 150
				+ (xp->buffer[10] & 15) * 15;

			fprintf (fp, ", duration: %02d:%02d)",
				 duration / 60, duration % 60);

			break;
		}

		case VBI_XDS_WEATHER_MESSAGE:
			fputs ("weather message)", fp);
			xdump (xp, fp);
			break;
		}

		break;

	case VBI_XDS_CLASS_RESERVED:
		fputs ("(reserved)", fp);
		xdump (xp, fp);
		break;

	case VBI_XDS_CLASS_UNDEFINED:
		fputs ("(undefined)", fp);
		xdump (xp, fp);
		break;

	default:
		fputs ("(?)", fp);
		xdump (xp, fp);
		break;
	}

	fputc ('\n', fp);
}

/**
 * @param xd XDS demultiplexer context allocated with vbi_xds_demux_new().
 *
 * Resets the XDS demux, useful for example after a channel change.
 *
 * @since 0.2.16
 */
void
vbi_xds_demux_reset		(vbi_xds_demux *	xd)
{
	unsigned int n;
	unsigned int i;

	assert (NULL != xd);

	n = N_ELEMENTS (xd->subpacket) * N_ELEMENTS (xd->subpacket[0]);

	for (i = 0; i < n; ++i)
		xd->subpacket[0][i].count = 0;

	xd->curr_sp = NULL;
}

/**
 * @param xd XDS demultiplexer context allocated with vbi_xds_demux_new().
 * @param buffer Closed Caption character pair, as in struct vbi_sliced.
 *
 * This function takes two successive bytes of a raw Closed Caption
 * stream, filters out XDS data and calls the output function given to
 * vbi_xds_demux_new() when a new packet is complete.
 *
 * You should feed only data from NTSC line 284.
 *
 * @returns
 * @c FALSE if the buffer contained parity errors.
 *
 * @since 0.2.16
 */
vbi_bool
vbi_xds_demux_feed		(vbi_xds_demux *	xd,
				 const uint8_t		buffer[2])
{
	_vbi_xds_subpacket *sp;
	vbi_bool r;
	int c1, c2;

	assert (NULL != xd);
	assert (NULL != buffer);

	r = TRUE;

	sp = xd->curr_sp;

	log ("XDS demux %02x %02x\n", buffer[0], buffer[1]);

	c1 = vbi_unpar8 (buffer[0]);
	c2 = vbi_unpar8 (buffer[1]);

	if ((c1 | c2) < 0) {
		log ("XDS tx error, discard current packet\n");
 
		if (sp) {
			sp->count = 0;
			sp->checksum = 0;
		}

		xd->curr_sp = NULL;

		return FALSE;
	}

	switch (c1) {
	case 0x00:
		/* Stuffing. */

		break;

	case 0x01 ... 0x0E:
	{
		vbi_xds_class xds_class;
		vbi_xds_subclass xds_subclass;
		unsigned int i;

		/* Packet header. */

		xds_class = (c1 - 1) >> 1;
		xds_subclass = c2;

		i = xds_subclass;

		/* MISC subclass 0x4n */
		if (i >= 0x40)
			i += 0x10 - 0x40;

		if (xds_class > VBI_XDS_CLASS_MISC
		    || i > N_ELEMENTS (xd->subpacket[0])) {
			log ("XDS ignore packet 0x%x/0x%02x, "
			     "unknown class or subclass\n",
			     xds_class, xds_subclass);
			goto discard;
		}

		sp = &xd->subpacket[xds_class][i];

		xd->curr_sp = sp;
		xd->curr.xds_class = xds_class;
		xd->curr.xds_subclass = xds_subclass;

		if (c1 & 1) {
			/* Start packet. */
			sp->checksum = c1 + c2;
			sp->count = 2;
		} else {
			/* Continue packet. */
			if (0 == sp->count) {
				log ("XDS can't continue packet "
				     "0x%x/0x%02x, missed start\n",
				     xd->curr.xds_class,
				     xd->curr.xds_subclass);
				goto discard;
			}
		}

		break;
	}

	case 0x0F:
		/* Packet terminator. */

		if (!sp) {
			log ("XDS can't finish packet, missed start\n");
			break;
		}

		sp->checksum += c1 + c2;

		if (0 != (sp->checksum & 0x7F)) {
			log ("XDS ignore packet 0x%x/0x%02x, "
			     "checksum error\n",
			     xd->curr.xds_class, xd->curr.xds_subclass);
		} else if (sp->count <= 2) {
			log ("XDS ignore empty packet 0x%x/0x%02x\n",
			     xd->curr.xds_class, xd->curr.xds_subclass);
		} else {
			memcpy (xd->curr.buffer, sp->buffer, 32);

			xd->curr.buffer_size = sp->count - 2;
			xd->curr.buffer[sp->count - 2] = 0;

			if (XDS_DEMUX_LOG)
				_vbi_xds_packet_dump (&xd->curr, stderr);

			r = xd->callback (xd, &xd->curr, xd->user_data);
		}

		/* fall through */

	discard:
		if (sp) {
			sp->count = 0;
			sp->checksum = 0;
		}

		/* fall through */

	case 0x10 ... 0x1F:
		/* Closed Caption. */

		xd->curr_sp = NULL;

		break;

	case 0x20 ... 0x7F:
		/* Packet contents. */

		if (!sp) {
			log ("XDS can't store packet, missed start\n");
			break;
		}

		if (sp->count >= sizeof (sp->buffer) + 2) {
			log ("XDS discard packet 0x%x/0x%02x, "
			     "buffer overflow\n",
			     xd->curr.xds_class, xd->curr.xds_subclass);
			goto discard;
		}

		sp->buffer[sp->count - 2] = c1;
		sp->buffer[sp->count - 1] = c2;

		sp->checksum += c1 + c2;
		sp->count += 1 + (0 != c2);

		break;
	}

	return r;
}

/**
 * @param xd XDS demultiplexer context allocated with vbi_xds_demux_new().
 * @param sliced Sliced VBI data.
 * @param n_lines Number of lines in the @a sliced array.
 *
 * This function works like vbi_xds_demux_feed() but operates on sliced
 * VBI data and filters out @c VBI_SLICED_CAPTION_525 on NTSC line 284.
 *
 * @returns
 * @c FALSE if any of the Caption lines contained parity errors.
 *
 * @since 0.2.26
 */
vbi_bool
vbi_xds_demux_feed_frame	(vbi_xds_demux *	xd,
				 const vbi_sliced *	sliced,
				 unsigned int		n_lines)
{
	const vbi_sliced *end;

	assert (NULL != xd);
	assert (NULL != sliced);

	for (end = sliced + n_lines; sliced < end; ++sliced) {
		switch (sliced->id) {
		case VBI_SLICED_CAPTION_525:
		case VBI_SLICED_CAPTION_525_F2:
			if (284 != sliced->line
			    && 0 != sliced->line)
				continue;

			if (!vbi_xds_demux_feed (xd, sliced->data))
				return FALSE;

			break;

		default:
			break;
		}
	}

	return TRUE;
}

#if 0 /* ideas */
const vbi_xds_packet *
vbi_xds_demux_get_packet	(vbi_xds_demux *	xd,
				 vbi_xds_class		xds_class,
				 vbi_xds_subclass	xds_subclass)
{
	/* most recently received packet of this class. */
}

const vbi_xds_packet *
vbi_xds_demux_cor		(vbi_xds_demux *	xd,
				 const uint8_t		buffer[2])
{
	...
}

void
vbi_xds_demux_set_log		(vbi_xds_demux *	xd,
				 vbi_log_fn *		callback,
				 unsigned int		pri_mask)
{
	...
}
#endif

/** @internal */
void
_vbi_xds_demux_destroy		(vbi_xds_demux *	xd)
{
	assert (NULL != xd);

	CLEAR (*xd);
}

/** @internal */
vbi_bool
_vbi_xds_demux_init		(vbi_xds_demux *	xd,
				 vbi_xds_demux_cb *	callback,
				 void *			user_data)
{
	assert (NULL != xd);
	assert (NULL != callback);

	vbi_xds_demux_reset (xd);

	xd->callback = callback;
	xd->user_data = user_data;

	return TRUE;
}

/**
 * @param xd XDS demultiplexer context allocated with
 *   vbi_xds_demux_new(), can be @c NULL.
 *
 * Frees all resources associated with @a xd.
 *
 * @since 0.2.16
 */
void
vbi_xds_demux_delete		(vbi_xds_demux *	xd)
{
	if (NULL == xd)
		return;

	_vbi_xds_demux_destroy (xd);

	free (xd);		
}

/**
 * @param callback Function to be called by vbi_xds_demux_feed() when
 *   a new packet is available.
 * @param user_data User pointer passed through to @a callback function.
 *
 * Allocates a new Extended Data Service (EIA 608) demultiplexer.
 *
 * @returns
 * Pointer to newly allocated XDS demux context which must be
 * freed with vbi_xds_demux_delete() when done. @c NULL on failure
 * (out of memory).
 *
 * @since 0.2.16
 */
vbi_xds_demux *
vbi_xds_demux_new		(vbi_xds_demux_cb *	callback,
				 void *			user_data)
{
	vbi_xds_demux *xd;

	assert (NULL != callback);

	if (!(xd = malloc (sizeof (*xd)))) {
		return NULL;
	}

	_vbi_xds_demux_init (xd, callback, user_data);

	return xd;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
