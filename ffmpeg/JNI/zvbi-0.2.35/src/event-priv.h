/*
 *  libzvbi - Events
 *
 *  Copyright (C) 2004, 2008 Michael H. Schimek
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

/* $Id: event-priv.h,v 1.2 2009/12/14 23:43:38 mschimek Exp $ */

#ifndef EVENT_PRIV_H
#define EVENT_PRIV_H

#include "event.h"

#ifndef EVENT_PRIV_LOG
#define EVENT_PRIV_LOG 0
#endif

/** @internal */
typedef unsigned int vbi_event_mask;

/** @internal */
typedef struct _vbi_event_handler_rec vbi_event_handler_rec;

/** @internal */
struct _vbi_event_handler_rec {
	vbi_event_handler_rec *	next;
	vbi_event_handler	callback;
	void *			user_data;
	vbi_event_mask		event_mask;
	vbi_bool		remove;
};

/** @internal */
typedef struct {
	vbi_event_handler_rec *	first;

	/* Union of the event_mask of all handlers in the list. */
	vbi_event_mask		event_mask;

	/* > 0 if _vbi_event_handler_list_send() currently traverses
           this list. */
	unsigned int		ref_count;
} _vbi_event_handler_list;

#if EVENT_PRIV_LOG
#define _vbi_event_handler_list_send(es, ev)				\
do {									\
	fprintf (stderr, "%s:%u event %s\n",				\
		 __FILE__, __LINE__, _vbi_event_name ((ev)->type));	\
        __vbi_event_handler_list_send (es, ev);				\
} while (0)
#else
#define _vbi_event_handler_list_send(es, ev)				\
	__vbi_event_handler_list_send (es, ev)
#endif

extern void
__vbi_event_handler_list_send	(_vbi_event_handler_list *es,
				 vbi_event *		ev);
extern void
_vbi_event_handler_list_remove_by_event
			    	(_vbi_event_handler_list *es,
				 vbi_event_mask	event_mask);
extern void
_vbi_event_handler_list_remove_by_callback
				(_vbi_event_handler_list *es,
				 vbi_event_handler	callback,
				 void *			user_data);
extern void
_vbi_event_handler_list_remove	(_vbi_event_handler_list *es,
				 vbi_event_handler_rec *eh);
extern vbi_event_handler_rec *
_vbi_event_handler_list_add	(_vbi_event_handler_list *es,
				 vbi_event_mask	event_mask,
				 vbi_event_handler	callback,
				 void *			user_data);
extern void
_vbi_event_handler_list_destroy	(_vbi_event_handler_list *es);
extern vbi_bool
_vbi_event_handler_list_init	(_vbi_event_handler_list *es);

#endif /* EVENT_PRIV_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
