/*
 *  libzvbi test
 *
 *  Copyright (C) 2000, 2001 Michael H. Schimek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

/* $Id: explist.c,v 1.13 2008/03/01 07:36:54 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif
#include <locale.h>

#include "src/libzvbi.h"

#ifndef _
#ifdef ENABLE_NLS
#    include <libintl.h>
#    define _(String) dgettext (PACKAGE, String)
#    ifdef gettext_noop
#        define N_(String) gettext_noop (String)
#    else
#        define N_(String) (String)
#    endif
#else
/* Stubs that do something close enough.  */
#    define textdomain(String) (String)
#    define gettext(String) (String)
#    define dgettext(Domain,Message) (Message)
#    define dcgettext(Domain,Message,Type) (Message)
#    define bindtextdomain(Domain,Directory) (Domain)
#    define _(String) (String)
#    define N_(String) (String)
#endif
#endif

vbi_bool check = FALSE;

#define TYPE_STR(type) case type : type_str = #type ; break

#define INT_TYPE(oi)    ((oi)->type == VBI_OPTION_BOOL			\
			|| (oi)->type == VBI_OPTION_INT			\
			|| (oi)->type == VBI_OPTION_MENU)

#define REAL_TYPE(oi)   ((oi)->type == VBI_OPTION_REAL)

#define MENU_TYPE(oi)   ((oi)->menu.str != NULL)

#define ASSERT_ERRSTR(expr)						\
do {									\
	if (!(expr)) {							\
		printf("Assertion '" #expr "' failed; errstr=\"%s\"\n",	\
		        vbi_export_errstr(ex));				\
		exit(EXIT_FAILURE);					\
	}								\
} while (0)

#define BOUNDS_CHECK(type)						\
do {									\
	if (oi->menu.type) {						\
		assert(oi->def.num >= 0);				\
		assert(oi->def.num <= oi->max.num);			\
		assert(oi->min.num == 0);				\
		assert(oi->max.num > 0);				\
		assert(oi->step.num == 1);				\
	} else {							\
	      	assert(oi->max.type >= oi->min.type);			\
		assert(oi->step.type > 0);				\
		assert(oi->def.type >= oi->min.type			\
		       && oi->def.type <= oi->max.type);		\
	}								\
} while (0)

#define STRING_CHECK(type)						\
do {									\
	if (oi->menu.type) {						\
		assert(oi->def.num >= 0);				\
		assert(oi->def.num <= oi->max.num);			\
		assert(oi->min.num == 0);				\
		assert(oi->max.num > 0);				\
		assert(oi->step.num == 1);				\
	} else {							\
		assert(oi->def.str != NULL);				\
	}								\
} while (0)

static void
keyword_check(char *keyword)
{
	int i, l;

	assert(keyword != NULL);
	l = strlen(keyword);
	assert(strlen(keyword) > 0);

	for (i = 0; i < l; i++) {
		if (isalnum(keyword[i]))
			continue;
		if (strchr("_", keyword[i]))
			continue;
		fprintf(stderr, "Bad keyword: '%s'\n", keyword);
		exit(EXIT_FAILURE);
	}
}

static void
print_current(vbi_option_info *oi, vbi_option_value current)
{
	if (REAL_TYPE(oi)) {
		printf("    current value=%f\n", current.dbl);
		if (!oi->menu.dbl)
			assert(current.dbl >= oi->min.dbl
			       && current.dbl <= oi->max.dbl);
	} else {
		printf("    current value=%d\n", current.num);
		if (!oi->menu.num)
			assert(current.num >= oi->min.num
			       && current.num <= oi->max.num);
	}
}

static void
test_modified(vbi_option_info *oi, vbi_option_value old, vbi_option_value new)
{
	if (REAL_TYPE(oi)) {
		/* XXX unsafe */
		if (old.dbl != new.dbl) {
			printf("but modified current value to %f\n", new.dbl);
			exit(EXIT_FAILURE);
		}
	} else {
		if (old.num != new.num) {
			printf("but modified current value to %d\n", new.num);
			exit(EXIT_FAILURE);
		}
	}
}

static void
test_set_int(vbi_export *ex, vbi_option_info *oi,
	     vbi_option_value *current, int value)
{
	vbi_option_value new_current;
	vbi_bool r;

	printf("    try to set %d: ", value);
	r = vbi_export_option_set(ex, oi->keyword, value);

	if (r)
		printf("success.");
	else
		printf("failed, errstr=\"%s\".", vbi_export_errstr(ex));

	new_current.num = 0x54321;

	if (!vbi_export_option_get(ex, oi->keyword, &new_current)) {
		printf("vbi_export_option_get failed, errstr==\"%s\"\n",
		       vbi_export_errstr(ex));
		if (new_current.num != 0x54321)
			printf("but modified destination to %d\n",
			       new_current.num);
		exit(EXIT_FAILURE);
	}

	if (!r)
		test_modified(oi, *current, new_current);

	print_current(oi, *current = new_current);
}

static void
test_set_real(vbi_export *ex, vbi_option_info *oi,
	      vbi_option_value *current, double value)
{
	vbi_option_value new_current;
	vbi_bool r;

	printf("    try to set %f: ", value);
	r = vbi_export_option_set(ex, oi->keyword, value);

	if (r)
		printf("success.");
	else
		printf("failed, errstr=\"%s\".", vbi_export_errstr(ex));

	new_current.dbl = 8192.0;

	if (!vbi_export_option_get(ex, oi->keyword, &new_current)) {
		printf("vbi_export_option_get failed, errstr==\"%s\"\n",
		       vbi_export_errstr(ex));
		/* XXX unsafe */
		if (new_current.dbl != 8192.0)
			printf("but modified destination to %f\n",
			       new_current.dbl);
		exit(EXIT_FAILURE);
	}

	if (!r)
		test_modified(oi, *current, new_current);

	print_current(oi, *current = new_current);
}

static void
test_set_entry(vbi_export *ex, vbi_option_info *oi,
	       vbi_option_value *current, int entry)
{
	vbi_option_value new_current;
	int new_entry;
	vbi_bool r0, r1;
	vbi_bool valid;

	valid = (MENU_TYPE(oi)
		 && entry >= oi->min.num
		 && entry <= oi->max.num);

	printf("    try to set menu entry %d: ", entry);
	r0 = vbi_export_option_menu_set(ex, oi->keyword, entry);

	switch (r0 = r0 * 2 + valid) {
	case 0:
		printf("failed as expected, errstr=\"%s\".", vbi_export_errstr(ex));
		break;
	case 1:
		printf("failed, errstr=\"%s\".", vbi_export_errstr(ex));
		break;
	case 2:
		printf("unexpected success.");
		break;
	default:
		printf("success.");
	}

	ASSERT_ERRSTR(vbi_export_option_get(ex, oi->keyword, &new_current));
	if (r0 == 0 || r0 == 1)
		test_modified(oi, *current, new_current);

	valid = MENU_TYPE(oi);

	new_entry = 0x33333;
	r1 = vbi_export_option_menu_get(ex, oi->keyword, &new_entry);

	switch (r1 = r1 * 2 + valid) {
	case 1:
		printf("\nvbi_export_option_menu_get failed, errstr==\"%s\"\n",
		       vbi_export_errstr(ex));
		break;
	case 2:
		printf("\nvbi_export_option_menu_get: unexpected success.\n");
		break;
	default:
		break;
	}

	if ((r1 == 0 || r1 == 1) && new_entry != 0x33333) {
		printf("vbi_export_option_menu_get failed, "
		       "but modified destination to %d\n",
		       new_current.num);
		exit(EXIT_FAILURE);
	}

	if (r0 == 1 || r0 == 2 || r1 == 1 || r1 == 2)
		exit(EXIT_FAILURE);

	switch (oi->type) {
	case VBI_OPTION_BOOL:
	case VBI_OPTION_INT:
		if (oi->menu.num)
			assert(new_current.num == oi->menu.num[new_entry]);
		else
			test_modified(oi, *current, new_current);
		print_current(oi, *current = new_current);
		break;

	case VBI_OPTION_REAL:
		if (oi->menu.dbl) {
			/* XXX unsafe */
			assert(new_current.dbl == oi->menu.dbl[new_entry]);
		} else {
			test_modified(oi, *current, new_current);
		}
		print_current(oi, *current = new_current);
		break;

	case VBI_OPTION_MENU:
		print_current(oi, *current = new_current);
		break;

	default:
		assert(!"reached");
		break;
	}
}

static void
dump_option_info(vbi_export *ex, vbi_option_info *oi)
{
	vbi_option_value val;
	const char *type_str;
	int i;

	switch (oi->type) {
	TYPE_STR(VBI_OPTION_BOOL);
	TYPE_STR(VBI_OPTION_INT);
	TYPE_STR(VBI_OPTION_REAL);
	TYPE_STR(VBI_OPTION_STRING);
	TYPE_STR(VBI_OPTION_MENU);
	default:
		printf("  * Option %s has invalid type %d\n", oi->keyword, oi->type);
		exit(EXIT_FAILURE);
	}

	printf("  * type=%s keyword=%s label=\"%s\" tooltip=\"%s\"\n",
	       type_str, oi->keyword, _(oi->label), _(oi->tooltip));

	keyword_check(oi->keyword);

	switch (oi->type) {
	case VBI_OPTION_BOOL:
	case VBI_OPTION_INT:
		BOUNDS_CHECK(num);
		if (oi->menu.num) {
			printf("    %d menu entries, default=%d: ",
			       oi->max.num - oi->min.num + 1, oi->def.num);
			for (i = oi->min.num; i <= oi->max.num; i++)
				printf("%d%s", oi->menu.num[i],
				       (i < oi->max.num) ? ", " : "");
			printf("\n");
		} else
			printf("    default=%d, min=%d, max=%d, step=%d\n",
			       oi->def.num, oi->min.num, oi->max.num, oi->step.num);

		ASSERT_ERRSTR(vbi_export_option_get(ex, oi->keyword, &val));
		print_current(oi, val);
		if (check) {
			if (oi->menu.num) {
				test_set_entry(ex, oi, &val, oi->min.num);
				test_set_entry(ex, oi, &val, oi->max.num);
				test_set_entry(ex, oi, &val, oi->min.num - 1);
				test_set_entry(ex, oi, &val, oi->max.num + 1);
				test_set_int(ex, oi, &val, oi->menu.num[oi->min.num]);
				test_set_int(ex, oi, &val, oi->menu.num[oi->max.num]);
				test_set_int(ex, oi, &val, oi->menu.num[oi->min.num] - 1);
				test_set_int(ex, oi, &val, oi->menu.num[oi->max.num] + 1);
			} else {
				test_set_entry(ex, oi, &val, 0);
				test_set_int(ex, oi, &val, oi->min.num);
				test_set_int(ex, oi, &val, oi->max.num);
				test_set_int(ex, oi, &val, oi->min.num - 1);
				test_set_int(ex, oi, &val, oi->max.num + 1);
			}
		}
		break;

	case VBI_OPTION_REAL:
		BOUNDS_CHECK(dbl);
		if (oi->menu.dbl) {
			printf("    %d menu entries, default=%d: ",
			       oi->max.num - oi->min.num + 1, oi->def.num);
			for (i = oi->min.num; i <= oi->max.num; i++)
				printf("%f%s", oi->menu.dbl[i],
				       (i < oi->max.num) ? ", " : "");
		} else
			printf("    default=%f, min=%f, max=%f, step=%f\n",
			       oi->def.dbl, oi->min.dbl, oi->max.dbl, oi->step.dbl);
		ASSERT_ERRSTR(vbi_export_option_get(ex, oi->keyword, &val));
		print_current(oi, val);
		if (check) {
			if (oi->menu.num) {
				test_set_entry(ex, oi, &val, oi->min.num);
				test_set_entry(ex, oi, &val, oi->max.num);
				test_set_entry(ex, oi, &val, oi->min.num - 1);
				test_set_entry(ex, oi, &val, oi->max.num + 1);
				test_set_real(ex, oi, &val, oi->menu.dbl[oi->min.num]);
				test_set_real(ex, oi, &val, oi->menu.dbl[oi->max.num]);
				test_set_real(ex, oi, &val, oi->menu.dbl[oi->min.num] - 1);
				test_set_real(ex, oi, &val, oi->menu.dbl[oi->max.num] + 1);
			} else {
				test_set_entry(ex, oi, &val, 0);
				test_set_real(ex, oi, &val, oi->min.dbl);
				test_set_real(ex, oi, &val, oi->max.dbl);
				test_set_real(ex, oi, &val, oi->min.dbl - 1);
				test_set_real(ex, oi, &val, oi->max.dbl + 1);
			}
		}
		break;

	case VBI_OPTION_STRING:
		if (oi->menu.str) {
			STRING_CHECK(str);
			printf("    %d menu entries, default=%d: ",
			       oi->max.num - oi->min.num + 1, oi->def.num);
			for (i = oi->min.num; i <= oi->max.num; i++)
				printf("%s%s", oi->menu.str[i],
				       (i < oi->max.num) ? ", " : "");
		} else
			printf("    default=\"%s\"\n", oi->def.str);
		ASSERT_ERRSTR(vbi_export_option_get(ex, oi->keyword, &val));
		printf("    current value=\"%s\"\n", val.str);
		assert(val.str);
		free(val.str);
		if (check) {
			printf("    try to set \"foobar\": ");
			if (vbi_export_option_set(ex, oi->keyword, "foobar"))
				printf("success.");
			else
				printf("failed, errstr=\"%s\".", vbi_export_errstr(ex));
			ASSERT_ERRSTR(vbi_export_option_get(ex, oi->keyword, &val));
			printf("    current value=\"%s\"\n", val.str);
			assert(val.str);
			free(val.str);
		}
		break;

	case VBI_OPTION_MENU:
		printf("    %d menu entries, default=%d: ",
		       oi->max.num - oi->min.num + 1, oi->def.num);
		for (i = oi->min.num; i <= oi->max.num; i++) {
			assert(oi->menu.str[i] != NULL);
			printf("%s%s", _(oi->menu.str[i]),
			       (i < oi->max.num) ? ", " : "");
		}
		printf("\n");
		ASSERT_ERRSTR(vbi_export_option_get(ex, oi->keyword, &val));
		print_current(oi, val);
		if (check) {
			test_set_entry(ex, oi, &val, oi->min.num);
			test_set_entry(ex, oi, &val, oi->max.num);
			test_set_entry(ex, oi, &val, oi->min.num - 1);
			test_set_entry(ex, oi, &val, oi->max.num + 1);
		}
		break;

	default:
		assert(!"reached");
		break;
	}
}

static void
list_options(vbi_export *ex)
{
	vbi_option_info *oi;
	int i;

	puts("  List of options:");

	for (i = 0; (oi = vbi_export_option_info_enum(ex, i)); i++) {
		assert(oi->keyword != NULL);
		ASSERT_ERRSTR(oi == vbi_export_option_info_keyword(ex, oi->keyword));

		dump_option_info(ex, oi);
	}
}

static void
list_modules(void)
{
	vbi_export_info *xi;
	vbi_export *ex;
	char *errstr;
	int i;

	puts("List of export modules:");

	for (i = 0; (xi = vbi_export_info_enum(i)); i++) {
		assert(xi->keyword != NULL);
		assert(xi == vbi_export_info_keyword(xi->keyword));

		printf("* keyword=%s label=\"%s\"\n"
		       "  tooltip=\"%s\" mime_type=%s extension=%s\n",
		       xi->keyword, _(xi->label),
		       _(xi->tooltip), xi->mime_type, xi->extension);

		keyword_check(xi->keyword);

		if (!(ex = vbi_export_new(xi->keyword, &errstr))) {
			printf("Could not open '%s': %s\n",
			       xi->keyword, errstr);
			exit(EXIT_FAILURE);
		}

		ASSERT_ERRSTR(xi == vbi_export_info_export(ex));

		list_options(ex);

		vbi_export_delete(ex);
	}

	puts("-- end of list --");
}

static const char short_options[] = "c";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options[] = {
	{ "check",	no_argument,		NULL,		'c' },
	{ 0, 0, 0, 0 }
};
#else
#define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

int
main(int argc, char **argv)
{
	int index, c;

	setlocale (LC_ALL, "");
	textdomain ("foobar"); /* we are not the library */

	while ((c = getopt_long(argc, argv, short_options,
				long_options, &index)) != -1)
		switch (c) {
		case 'c':
			check = TRUE;
			break;
		default:
			fprintf(stderr, "Unknown option\n");
			exit(EXIT_FAILURE);
		}

	list_modules();

	exit(EXIT_SUCCESS);
}
