/*
 *  Template for export modules
 */

/* $Id: exp-templ.c,v 1.10 2007/11/27 18:26:32 mschimek Exp $ */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "export.h"

typedef struct tmpl_instance
{
	/* Common to all export modules */
	vbi_export		export;

	/* Our private stuff */

	/* Options */
	int			flip;
	int			day;
	int			prime;
	double			quality;
	char *			comment;
	int			weekday;

	int			counter;
} tmpl_instance;

/* Safer than tmpl_instance *tmpl = (tmpl_instance *)(vbi_export *) e */
#define TMPL(e)	PARENT(e, tmpl_instance, export);

static vbi_export *
tmpl_new(void)
{
	tmpl_instance *tmpl;

	if (!(tmpl = calloc(1, sizeof(*tmpl))))
		return NULL;

	/*
	 *  The caller will initialize tmpl->export.class for us
	 *  and reset all options to their defaults, we only
	 *  have to initialize our private stuff.
	 */

	tmpl->counter = 0;

	return &tmpl->export;
}

static void
tmpl_delete(vbi_export *e)
{
	tmpl_instance *tmpl = TMPL(e);

	/* Uninitialize our private stuff and options */

	if (tmpl->comment)
		free(tmpl->comment);

	free(tmpl);
}

/* convenience */
#define elements(array) (sizeof(array) / sizeof(array[0]))

/* N_(), _() are NLS functions, see info gettext. */
static const char *
string_menu_items[] = {
	N_("Sunday"), N_("Monday"), N_("Tuesday"),
	N_("Wednesday"), N_("Thursday"), N_("Friday"), N_("Saturday")
};

static int
int_menu_items[] = {
	1, 3, 5, 7, 11, 13, 17, 19
};

static vbi_option_info
tmpl_options[] = {
	VBI_OPTION_BOOL_INITIALIZER
	  /*
	   *  Option keywords must be unique within their module
	   *  and shall contain only "AZaz09_" (be filesystem safe that is).
	   *  Note "network", "creator" and "reveal" are reserved generic
	   *  options, filtered by the export api functions.
	   */
	  ("flip", N_("Boolean option"),
           FALSE, N_("This is a boolean option")),
	VBI_OPTION_INT_RANGE_INITIALIZER
	  ("day", N_("Select a month day"),
	   /* default, min, max, step, has no tooltip */
	      13,       1,   31,  1,      NULL),
	VBI_OPTION_INT_MENU_INITIALIZER
	  ("prime", N_("Select a prime"),
	   0, int_menu_items, elements(int_menu_items),
	   N_("Default is the first, '1'")),
	VBI_OPTION_REAL_RANGE_INITIALIZER
	  ("quality", N_("Compression quality"),
	   100, 1, 100, 0.01, NULL),
	/* VBI_OPTION_REAL_MENU_INITIALIZER like int */
	VBI_OPTION_STRING_INITIALIZER
	  ("comment", N_("Add a comment"),
	   "default comment", N_("Another tooltip")),
	VBI_OPTION_MENU_INITIALIZER
	  ("weekday", N_("Select a weekday"),
	   2, string_menu_items, 7, N_("Default is Tuesday"))
};

/*
 *  Enumerate our options (optional if we have no options).
 *  Instead of using a table one could also dynamically create
 *  vbi_option_info's in tmpl_instance.
 */
static vbi_option_info *
option_enum(vbi_export *e, int index)
{
	e = e;

	/* Enumeration 0 ... n */
	if (index < 0 || index >= (int) elements(tmpl_options))
		return NULL;

	return tmpl_options + index;
}

#define KEYWORD(str) (strcmp(keyword, str) == 0)

/*
 *  Get an option (optional if we have no options).
 */
static vbi_bool
option_get(vbi_export *e, const char *keyword, vbi_option_value *value)
{
	tmpl_instance *tmpl = TMPL(e);

	if (KEYWORD("flip")) {
		value->num = tmpl->flip;
	} else if (KEYWORD("day")) {
		value->num = tmpl->day;
	} else if (KEYWORD("prime")) {
		value->num = tmpl->prime;
	} else if (KEYWORD("quality")) {
		value->dbl = tmpl->quality;
	} else if (KEYWORD("comment")) {
		if (!(value->str = vbi_export_strdup(e, NULL,
			tmpl->comment ? tmpl->comment : "")))
			return FALSE;
	} else if (KEYWORD("weekday")) {
		value->num = tmpl->weekday;
	} else {
		vbi_export_unknown_option(e, keyword);
		return FALSE;
	}

	return TRUE; /* success */
}

/*
 *  Set an option (optional if we have no options).
 */
static vbi_bool
option_set(vbi_export *e, const char *keyword, va_list args)
{
	tmpl_instance *tmpl = TMPL(e);

	if (KEYWORD("flip")) {
		tmpl->flip = !!va_arg(args, int);
	} else if (KEYWORD("day")) {
		int day = va_arg(args, int);

		/* or clamp */
		if (day < 1 || day > 31) {
			vbi_export_invalid_option(e, keyword, day);
			return FALSE;
		}

		tmpl->day = day;

	} else if (KEYWORD("prime")) {
		unsigned int i;
		unsigned int d, dmin = UINT_MAX;
		int value = va_arg(args, int);

		/* or return an error */
		for (i = 0; i < elements(int_menu_items); i++)
			if ((d = abs(int_menu_items[i] - value)) < dmin) {
				tmpl->prime = int_menu_items[i];
				dmin = d;
			}

	} else if (KEYWORD("quality")) {
		double quality = va_arg(args, double);

		/* or return an error */
		if (quality < 1)
			quality = 1;
		else if (quality > 100)
			quality = 100;

		tmpl->quality = quality;
	} else if (KEYWORD("comment")) {
		char *comment = va_arg(args, char *);

		/* Note the option remains unchanged in case of error */
		if (!vbi_export_strdup(e, &tmpl->comment, comment))
			return FALSE;
	} else if (KEYWORD("weekday")) {
		int day = va_arg(args, int);

		/* or return an error */
		tmpl->weekday = day % 7;
	} else {
		vbi_export_unknown_option(e, keyword);
		return FALSE;
	}

	return TRUE; /* success */
}

/*
 *  The output function, mandatory.
 */
static vbi_bool
export(vbi_export *e, vbi_page *pg)
{
	tmpl_instance *tmpl = TMPL(e);

	/* Write pg to target, that's all. */
	vbi_export_printf (e, "Page %x.%x\n", pg->pgno, pg->subno);

	tmpl->counter++; /* just for fun */

	/*
	 *  Should any of the module functions return unsuccessful
	 *  they better post a description of the problem.
	 *  Parameters like printf, no linefeeds '\n' please.
	 */
	/*
	vbi_export_error_printf(_("Writing failed: %s"), strerror(errno));
         */

	return FALSE; /* no success (since we didn't write anything) */
}

/*
 *  Let's describe us.
 *  You can leave away assignments unless mandatory.
 */
static vbi_export_info
info_tmpl = {
	/* The mandatory keyword must be unique and shall
           contain only "AZaz09_" */
	.keyword	= "templ",
	/* When omitted this module can still be used by
	   libzvbi clients but won't be listed in a UI. */
	.label		= N_("Template"),
	.tooltip	= N_("This is just an export template"),

	.mime_type	= "misc/example",
	.extension	= "tmpl",
};

vbi_export_class
vbi_export_class_tmpl = {
	._public		= &info_tmpl,

	/* Functions to allocate and free a tmpl_class vbi_export instance.
	   When you omit these, libzvbi will allocate a bare struct vbi_export */
	._new			= tmpl_new,
	._delete		= tmpl_delete,

	/* Functions to enumerate, read and write options */
	.option_enum		= option_enum,
	.option_get		= option_get,
	.option_set		= option_set,

	/* Function to export a page, mandatory */
	.export			= export
};

/*
 *  This is a constructor calling vbi_register_export_module().
 *  (Commented out since we don't want to register the example module.)
 */
#if 0
VBI_AUTOREG_EXPORT_MODULE(vbi_export_class_tmpl)
#endif

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
