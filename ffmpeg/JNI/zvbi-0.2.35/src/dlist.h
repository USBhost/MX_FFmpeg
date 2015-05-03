/*
 *  libzvbi - Double linked wheel, reinvented
 *
 *  Copyright (C) 2004 Michael H. Schimek
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

/* $Id: dlist.h,v 1.2 2008/02/19 00:35:15 mschimek Exp $ */

#ifndef DLIST_H
#define DLIST_H

#include <assert.h>
#include "macros.h"
#include "misc.h"

#ifndef DLIST_CONSISTENCY
#  define DLIST_CONSISTENCY 0
#endif

struct node {
	struct node *		_succ;
	struct node *		_pred;
};

/* A ring: struct node n1.succ -> n2, n2.succ -> n3, n3.succ -> n1.
           struct node n1.pred -> n3, n2.pred -> n1, n3.pred -> n2.
   A ring with one element: n.succ -> n.
                            n.pred -> n.
   A list: struct node list.succ -> n1 (head). n1.succ -> n2, n2.succ -> list.
	   struct node list.pred -> n2 (tail). n1.pred -> list, n2.pred -> n1.
   An empty list: list.succ -> list.
                  list.pred -> list. */

_vbi_inline void
verify_ring			(const struct node *	n)
{
	unsigned int counter;
	const struct node *start;

	if (!DLIST_CONSISTENCY)
		return;

	start = n;
	counter = 0;

	do {
		const struct node *_succ = n->_succ;

		assert (counter++ < 30000);
		assert (n == _succ->_pred);
		n = _succ;
	} while (n != start);
}

_vbi_inline struct node *
_remove_nodes			(struct node *		before,
				 struct node *		after,
				 struct node *		first,
				 struct node *		last,
				 vbi_bool		close_ring,
				 vbi_bool		return_ring)
{
	verify_ring (before);

	if (close_ring) {
		before->_succ = after;
		after->_pred = before;
	} else {
		before->_succ = NULL;
		after->_pred = NULL;
	}

	if (return_ring) {
		first->_pred = last;
		last->_succ = first;
	} else {
		first->_pred = NULL;
		last->_succ = NULL;
	}

	return first;
}

_vbi_inline struct node *
_insert_nodes			(struct node *		before,
				 struct node *		after,
				 struct node *		first,
				 struct node *		last)
{
	verify_ring (before);

	first->_pred = before;
        last->_succ = after;

	after->_pred = last;
	before->_succ = first;

	return first;
}

/**
 * @internal
 * Adds struct node n to a list or ring after struct node a.
 * @returns
 * n.
 */
_vbi_inline struct node *
insert_after			(struct node *		a,
				 struct node *		n)
{
	return _insert_nodes (a, a->_succ, n, n);
}

/**
 * @internal
 * Adds struct node n to a list or ring before struct node b.
 * @returns
 * n.
 */
_vbi_inline struct node *
insert_before			(struct node *		b,
				 struct node *		n)
{
	return _insert_nodes (b->_pred, b, n, n);
}

/**
 * @internal
 * Removes struct node n from its list or ring.
 * Call rem_node(l, n) to remove a node only if it is a
 * member of list l.
 * @returns
 * n.
 */
_vbi_inline struct node *
unlink_node			(struct node *		n)
{
	return _remove_nodes (n->_pred, n->_succ, n, n, TRUE, FALSE);
}

/**
 * @internal
 *
 * Traverses a list. p points to the parent structure of a node. p1 is
 * a pointer of same type as p, used to remember the _succ struct node in the
 * list. This permits unlink_node(p) in the loop. Resist the temptation
 * to unlink p->_succ or p->_pred. l points to the list to traverse.
 * _node is the name of the struct node element. Example:
 *
 * struct mystruct { struct node foo; int bar; };
 *
 * list mylist; // assumed initialized
 * struct mystruct *p, *p1;
 *
 * FOR_ALL_NODES (p, p1, &mylist, foo)
 *   do_something (p);
 */
#define FOR_ALL_NODES(p, p1, l, _node)					\
for (verify_ring (l), p = PARENT ((l)->_succ, __typeof__ (* p), _node);	\
     p1 = PARENT (p->_node._succ, __typeof__ (* p), _node), 		\
     &p->_node != (l); p = p1)

#define FOR_ALL_NODES_REVERSE(p, p1, l, _node)				\
for (verify_ring (l), p = PARENT ((l)->_pred, __typeof__ (* p), _node);	\
     p1 = PARENT (p->_node._pred, __typeof__ (* p), _node),		\
     &p->_node != (l); p = p1)

/**
 * @internal
 * Destroys list l. This will cause a segmentation fault on any attempts
 * to traverse or modify the list. You should ensure the list is empty
 * and all nodes have been properly deleted before calling this function.
  * @returns
 * l.
 */
_vbi_inline struct node *
list_destroy			(struct node *		l)
{
	struct node *n = l;

	verify_ring (l);

	do {
		struct node *_succ = n->_succ;

		n->_succ = NULL;
		n->_pred = NULL;
		n = _succ;
	} while (n != l);

	return l;
}

/**
 * @internal
 * Initializes list l.
 * @returns
 * l.
 */
_vbi_inline struct node *
list_init			(struct node *		l)
{
	l->_succ = l;
	l->_pred = l;

	return l;
}

/**
 * @internal
 * @returns
 * @c TRUE if node n is the first node of list l.
 */
_vbi_inline vbi_bool
is_head				(const struct node *	l,
				 const struct node *	n)
{
	verify_ring (l);

	return (NULL != n && n == l->_succ);
}

/**
 * @internal
 * @returns
 * @c TRUE if node n is the last node of list l.
 */
_vbi_inline vbi_bool
is_tail				(const struct node *	l,
				 const struct node *	n)
{
	verify_ring (l);

	return (NULL != n && n == l->_pred);
}

/**
 * @internal
 * @returns
 * @c TRUE if list l is empty.
 */
_vbi_inline vbi_bool
is_empty			(const struct node *	l)
{
	verify_ring (l);

	return (l == l->_succ);
}

/**
 * @internal
 * @returns
 * @c TRUE if node n is a member of list l.
 */
_vbi_inline vbi_bool
is_member			(const struct node *	l,
				 const struct node *	n)
{
	const struct node *q;

	verify_ring (l);

	if (NULL == n)
		return FALSE;

	for (q = l->_succ; q != l; q = q->_succ) {
		if (unlikely (q == n)) {
			return TRUE;
		}
	}

	return FALSE;
}

/**
 * @internal
 * Inserts node n at the begin of list l.
 * @returns
 * n.
 */
_vbi_inline struct node *
add_head			(struct node *		l,
				 struct node *		n)
{
	return _insert_nodes (l, l->_succ, n, n);
}

/**
 * @internal
 * Inserts node n at the end of list l.
 * @returns
 * n.
 */
_vbi_inline struct node *
add_tail			(struct node *		l,
				 struct node *		n)
{
	return _insert_nodes (l->_pred, l, n, n);
}

/**
 * @internal
 * Removes all nodes from list l2 and inserts them in the
 * same order at the end of list l1.
 * @returns
 * First node of l2, or @c NULL if l2 is empty.
 */
_vbi_inline struct node *
add_tail_list			(struct node *		l1,
				 struct node *		l2)
{
	struct node *h2 = l2->_succ;

	verify_ring (l2);

	if (unlikely (l2 == h2)) {
		/* l2 is empty. */
		return NULL;
	}

	_insert_nodes (l1->_pred, l1, h2, l2->_pred);

	l2->_succ = l2;
	l2->_pred = l2;

	return h2;
}

/**
 * @internal
 * Removes node n from list l if it is a member of list l.
 * Call unlink_node(n) to remove a node unconditionally.
 * @returns
 * n if it is a member of l, @c NULL otherwise.
 */
_vbi_inline struct node *
rem_node			(struct node *		l,
				 struct node *		n)
{
	if (is_member (l, n)) {
		return unlink_node (n);
	} else {
		return NULL;
	}
}

/**
 * @internal
 * Removes the first node from list l.
 * @returns
 * First node of l, or @c NULL if l is empty.
 */
_vbi_inline struct node *
rem_head			(struct node *		l)
{
	struct node *n = l->_succ;

	if (likely (n != l)) {
		return _remove_nodes (l, n->_succ, n, n, TRUE, FALSE);
	} else {
		return NULL;
	}
}

/**
 * @internal
 * Removes the last node from list l.
 * @returns
 * Last node of l, or @c NULL if l is empty.
 */
_vbi_inline struct node *
rem_tail			(struct node *		l)
{
	struct node *n = l->_pred;

	if (likely (n != l)) {
		return _remove_nodes (n->_pred, l, n, n, TRUE, FALSE);
	} else {
		return NULL;
	}
}

/**
 * @internal
 * @returns
 * Number of nodes in list l.
 */
_vbi_inline unsigned int
list_length			(struct node *		l)
{
	unsigned int count = 0;
	struct node *n;

	verify_ring (l);

	for (n = l->_succ; n != l; n = n->_succ)
		++count;

	return count;
}

#endif /* DLIST_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
