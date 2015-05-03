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

/* $Id: event.c,v 1.2 2009/12/14 23:43:35 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "misc.h"		/* CLEAR() */
#include "event-priv.h"

/**
 * @internal
 * @param es Event handler list.
 * @param ev The event to send.
 * 
 * Traverses the list of event handlers and calls each handler waiting
 * for the @a ev->type of event, passing @a ev as parameter.
 */
void
__vbi_event_handler_list_send	(_vbi_event_handler_list *el,
				 vbi_event *		ev)
{
	vbi_event_handler_rec *eh, **ehp;
	unsigned int ref_count;

	assert (NULL != el);
	assert (NULL != ev);

	if (0 == (el->event_mask & ev->type))
		return;

	ref_count = el->ref_count + 1;
	if (unlikely (0 == ref_count))
		return;
	el->ref_count = ref_count;

	for (eh = el->first; NULL != eh; eh = eh->next) {
		if (0 != (eh->event_mask & ev->type) && !eh->remove) {
			eh->callback (ev, eh->user_data);
		}
	}

	el->ref_count = --ref_count;
	if (ref_count > 0)
		return;

	ehp = &el->first;

	while (NULL != (eh = *ehp)) {
		if (eh->remove) {
			*ehp = eh->next;
			vbi_free (eh);
		} else {
			ehp = &eh->next;
		}
	}
}

/**
 * @internal
 * @param el Event handler list.
 * @param event_mask Event mask.
 *
 * Removes all handlers from the list which handle
 * only events given in the @a event_mask.
 */
void
_vbi_event_handler_list_remove_by_event
			    	(_vbi_event_handler_list *el,
				 vbi_event_mask		event_mask)
{
	vbi_event_handler_rec *eh, **ehp;
	vbi_event_mask clear_mask;

	assert (NULL != el);

	clear_mask = ~event_mask;

	ehp = &el->first;

	while (NULL != (eh = *ehp)) {
		if (0 == (eh->event_mask &= clear_mask)) {
			if (0 == el->ref_count) {
				*ehp = eh->next;
				vbi_free (eh);
			} else {
				eh->remove = TRUE;
				ehp = &eh->next;
			}
		} else {
			ehp = &eh->next;
		}
	}

	el->event_mask &= clear_mask;
}

/**
 * @param el Event handler list.
 * @param callback Function to be called on events.
 * @param user_data User pointer passed through to the @a callback function.
 * 
 * Removes all event handlers from the list with this @a callback and
 * @a user_data. You can safely call this function from a handler removing
 * itself or another handler.
 */
void
_vbi_event_handler_list_remove_by_callback
				(_vbi_event_handler_list *el,
				 vbi_event_handler	callback,
				 void *			user_data)
{
	_vbi_event_handler_list_add (el, 0, callback, user_data);
}

/**
 * @param el Event handler list.
 * @param eh Event handler.
 * 
 * Removes event handler @a eh if member of the list @a el. You can
 * safely call this function from a handler removing itself or another
 * handler.
 */
void
_vbi_event_handler_list_remove	(_vbi_event_handler_list *el,
				 vbi_event_handler_rec *eh)
{
	vbi_event_handler_rec *eh1, **ehp;
	vbi_event_mask event_union;

	assert (NULL != el);
	assert (NULL != eh);

	ehp = &el->first;
	event_union = 0;

	while (NULL != (eh1 = *ehp)) {
		if (eh1 == eh) {
			if (0 == el->ref_count) {
				*ehp = eh1->next;
				vbi_free (eh1);
			} else {
				eh1->remove = TRUE;
				ehp = &eh1->next;
			}
		} else {
			event_union |= eh1->event_mask;
			ehp = &eh1->next;
		}
	}

	el->event_mask = event_union;
}

/**
 * @param el Event handler list.
 * @param event_mask Set of events (@c VBI_EVENT_) the handler is waiting
 *   for, can be -1 for all and 0 for none.
 * @param callback Function to be called on events.
 * @param user_data User pointer passed through to the @a callback
 *   function.
 * 
 * Adds a new event handler to the list. When the @a callback with @a
 * user_data is already registered the function merely changes the set
 * of events it will receive in the future. When the @a event_mask is
 * zero the function does nothing or removes an already registered event
 * handler. You can safely call this function from an event handler.
 *
 * Any number of handlers can be added, also different handlers for the
 * same event which will be called in registration order.
 *
 * @return
 * Pointer to opaque vbi_event_handler object, @c NULL on failure or if
 * no handler has been added.
 */
vbi_event_handler_rec *
_vbi_event_handler_list_add	(_vbi_event_handler_list *el,
				 vbi_event_mask		event_mask,
				 vbi_event_handler	callback,
				 void *			user_data)
{
	vbi_event_handler_rec *eh, **ehp, *found;
	vbi_event_mask event_union;

	assert (NULL != el);

	ehp = &el->first;
	event_union = 0;
	found = NULL;

	while (NULL != (eh = *ehp)) {
		if (eh->callback == callback
		    && eh->user_data == user_data) {
			if (0 == event_mask) {
				if (0 == el->ref_count) {
					*ehp = eh->next;
					vbi_free (eh);
				} else {
					eh->remove = TRUE;
					ehp = &eh->next;
				}

				continue;
			} else {
				found = eh;
				eh->event_mask = event_mask;
			}
		}

		event_union |= eh->event_mask;
		ehp = &eh->next;
	}

	if (NULL == found && 0 != event_mask) {
		found = vbi_malloc (sizeof (*found));
		if (NULL != found) {
			CLEAR (*found);

			found->event_mask = event_mask;
			found->callback	= callback;
			found->user_data = user_data;

			event_union |= event_mask;

			*ehp = found;
		}
	}

	el->event_mask = event_union;

	return found;
}

void
_vbi_event_handler_list_destroy	(_vbi_event_handler_list *el)
{
	assert (NULL != el);

	_vbi_event_handler_list_remove_by_event (el, (vbi_event_mask) -1);

	CLEAR (*el);
}

vbi_bool
_vbi_event_handler_list_init	(_vbi_event_handler_list *el)
{
	assert (NULL != el);

	CLEAR (*el);

	return TRUE;
}

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
