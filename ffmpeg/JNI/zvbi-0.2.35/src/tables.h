/*
 *  libzvbi -- Tables
 *
 *  Copyright (C) 1999-2002 Michael H. Schimek
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

/* $Id: tables.h,v 1.10 2008/02/19 00:35:22 mschimek Exp $ */

#ifndef TABLES_H
#define TABLES_H

#include <inttypes.h>

#include "event.h" /* vbi_rating_auth, vbi_prog_classf */

extern const char *vbi_country_names_en[];

struct vbi_cni_entry {
	int16_t			id; /* arbitrary */
	const char *		country; /* RFC 1766 / ISO 3166-1 alpha-2 */
	const char *		name; /* UTF-8 */
	uint16_t		cni1; /* Teletext packet 8/30 format 1 */
	uint16_t		cni2; /* Teletext packet 8/30 format 2 */
	uint16_t		cni3; /* PDC Method B */
	uint16_t		cni4; /* VPS */
};

extern const struct vbi_cni_entry vbi_cni_table[];

/* Public */

/**
 * @addtogroup Event
 * @{
 */
extern const char *	vbi_rating_string(vbi_rating_auth auth, int id);
extern const char *	vbi_prog_type_string(vbi_prog_classf classf, int id);
/** @} */

/* Private */

#endif /* TABLES_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
