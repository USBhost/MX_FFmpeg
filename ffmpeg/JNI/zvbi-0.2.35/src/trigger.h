/*
 *  libzvbi -- Triggers
 *
 *  Copyright (C) 2001 Michael H. Schimek
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

/* $Id: trigger.h,v 1.5 2008/02/19 00:35:22 mschimek Exp $ */

#ifndef TRIGGER_H
#define TRIGGER_H

#ifndef VBI_DECODER
#define VBI_DECODER
typedef struct vbi_decoder vbi_decoder;
#endif

/* Private */

typedef struct vbi_trigger vbi_trigger;

extern void		vbi_trigger_flush(vbi_decoder *vbi);
extern void		vbi_deferred_trigger(vbi_decoder *vbi);
extern void		vbi_eacem_trigger(vbi_decoder *vbi, unsigned char *s);
extern void		vbi_atvef_trigger(vbi_decoder *vbi, unsigned char *s);

#endif /* TRIGGER_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
