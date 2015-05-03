/*
 *  libzvbi -- WSS decoder
 *
 *  Copyright (C) 2001, 2002 Michael H. Schimek
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

/* $Id: wss.h,v 1.4 2008/02/19 00:35:23 mschimek Exp $ */

extern void		vbi_decode_wss_625(vbi_decoder *vbi, uint8_t *buf, double time);
extern void		vbi_decode_wss_cpr1204(vbi_decoder *vbi, uint8_t *buf);

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
