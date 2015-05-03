/*
 *  libzvbi - Export modules
 *
 *  Copyright (C) 2001, 2002, 2007 Michael H. Schimek
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

/* $Id: export.h,v 1.18 2008/02/24 14:17:37 mschimek Exp $ */

#ifndef EXPORT_H
#define EXPORT_H

#include "bcd.h" /* vbi_bool */
#include "event.h" /* vbi_network */
#include "format.h" /* vbi_page */

/* Public */

#include <stdio.h> /* FILE */
#include <sys/types.h> /* size_t, ssize_t */

/**
 * @ingroup Export
 * @brief Export module instance, an opaque object.
 *
 * Allocate with vbi_export_new().
 */
typedef struct vbi_export vbi_export;

/**
 * @ingroup Export
 * @brief Information about an export module.
 *
 * Although export modules can be accessed by a static keyword (see
 * vbi_export_new()) they are by definition opaque. The client
 * can list export modules for the user and manipulate them without knowing
 * about their availability or purpose. To do so, information
 * about the module is necessary, given in this structure.
 *
 * You can obtain this information with vbi_export_info_enum().
 */
typedef struct vbi_export_info {
	/**
	 * Unique (within this library) keyword to identify
	 * this export module. Can be stored in configuration files.
	 */
	char *			keyword;
	/**
	 * Name of the export module to be shown to the user.
	 * Can be @c NULL indicating the module shall not be listed.
	 * Clients are encouraged to localize this with dgettext("zvbi", label).
	 */
	char *			label;
	/**
	 * A brief description (or @c NULL) for the user.
	 * Clients are encouraged to localize this with dgettext("zvbi", tooltip).
	 */
	char *			tooltip;
	/**
	 * Description of the export format as MIME type,
	 * for example "text/html". May be @c NULL.
	 */
	char *			mime_type;
	/**
	 * Suggested filename extension. Multiple strings are
	 * possible, separated by comma. The first string is preferred.
	 * Example: "html,htm". May be @c NULL.
	 */
	char *			extension;
} vbi_export_info;

/**
 * @ingroup Export
 */
typedef enum {
	/**
	 * A boolean value, either @c TRUE (1) or @c FALSE (0).
	 * <table>
	 * <tr><td>Type:</td><td>int</td></tr>
	 * <tr><td>Default:</td><td>vbi_option_info.def.num</td></tr>
	 * <tr><td>Bounds:</td><td>vbi_option_info.min.num (0) ... max.num (1),
	 * step.num (1)</td></tr>
	 * <tr><td>Menu:</td><td>NULL</td></tr>
	 * </table>
	 */
	VBI_OPTION_BOOL = 1,

	/**
	 * A signed integer value.
	 * <table>
	 * <tr><td>Type:</td><td>int</td></tr>
	 * <tr><td>Default:</td><td>vbi_option_info.def.num</td></tr>
	 * <tr><td>Bounds:</td><td>vbi_option_info.min.num ... max.num, step.num</td></tr>
	 * <tr><td>Menu:</td><td>NULL</td></tr>
	 * </table>
	 * When only a few discrete values rather than a range of values are permitted @p
	 * vbi_option_info.menu points to a vector of integers. However you must still
	 * set the option by value, not by menu index. If the value is invalid
	 * vbi_export_option_set() may fail or pick the closest possible value instead.
	 * <table>
	 * <tr><td>Type:</td><td>int</td></tr>
	 * <tr><td>Default:</td><td>vbi_option_info.menu.num[vbi_option_info.def.num]</td></tr>
	 * <tr><td>Bounds:</td><td>See vbi_option_info.menu.num[] for valid values</td></tr>
	 * <tr><td>Menu:</td><td>vbi_option_info.menu.num[min.num (0) ... max.num],
	 * step.num (1)</td></tr>
	 * </table>
	 */
	VBI_OPTION_INT,

	/**
	 * A real value.
	 * <table>
	 * <tr><td>Type:</td><td>double</td></tr>
	 * <tr><td>Default:</td><td>vbi_option_info.def.dbl</td></tr>
	 * <tr><td>Bounds:</td><td>vbi_option_info.min.dbl ... max.dbl,
	 * step.dbl</td></tr>
	 * <tr><td>Menu:</td><td>NULL</td></tr>
	 * </table>
	 * As with @c VBI_OPTION_INT @p vbi_option_info.menu may point to a set
	 * of valid values:
	 * <table>
	 * <tr><td>Type:</td><td>double</td></tr>
	 * <tr><td>Default:</td><td>vbi_option_info.menu.dbl[vbi_option.info.def.num]</td></tr>
	 * <tr><td>Bounds:</td><td>See vbi_option_info.menu.dbl[] for valid values</td></tr>
	 * <tr><td>Menu:</td><td>vbi_option_info.menu.dbl[min.num (0) ... max.num],
	 * step.num (1)</td></tr>
	 * </table>
	 */
	VBI_OPTION_REAL,

	/**
	 * A null terminated string.
	 * <table>
	 * <tr><td>Type:</td><td>char *</td></tr>
	 * <tr><td>Default:</td><td>vbi_option_info.def.str</td></tr>
	 * <tr><td>Bounds:</td><td>Not applicable</td></tr>
	 * <tr><td>Menu:</td><td>NULL</td></tr>
	 * </table>
	 * As with @c VBI_OPTION_INT @p vbi_option_info.menu may point to a set
	 * of valid strings. Note that vbi_export_option_set() always expects a
	 * string for this kind of option, and it may accept strings which are
	 * not in the menu. Contrast this with @c VBI_OPTION_MENU, where a menu
	 * index is expected.
	 * <table>
	 * <tr><td>Type:</td><td>char *</td></tr>
	 * <tr><td>Default:</td><td>vbi_option_info.menu.str[vbi_option_info.def.num]</td></tr>
	 * <tr><td>Bounds:</td><td>Not applicable</td></tr>
	 * <tr><td>Menu:</td><td>vbi_option_info.menu.str[min.num (0) ... max.num],
	 * step.num (1)</td></tr>
	 * </table>
	 */
	VBI_OPTION_STRING,

	/**
	 * Choice between a number of named options. The value of this kind
	 * of option is the menu index. The menu strings can be localized
	 * with a dgettext("zvbi", menu.str[n]) call. For details see
	 * gettext info file.
	 * <table>
	 * <tr><td>Type:</td><td>int</td></tr>
	 * <tr><td>Default:</td><td>vbi_option_info.def.num</td></tr>
	 * <tr><td>Bounds:</td><td>vbi_option_info.min.num (0) ... max.num, 
	 *    step.num (1)</td></tr>
	 * <tr><td>Menu:</td><td>vbi_option_info.menu.str[vbi_option_info.min.num ... max.num],
	 *    step.num (1).
	 * </td></tr>
	 * </table>
	 */
	VBI_OPTION_MENU
} vbi_option_type;

/**
 * @ingroup Export
 * @brief Result of an option query.
 */
typedef union {
	int			num;
	double			dbl;
	char *			str;
} vbi_option_value;

/**
 * @ingroup Export
 * @brief Option menu types.
 */
typedef union {
	int *			num;
	double *		dbl;
	char **			str;
} vbi_option_value_ptr;

/**
 * @ingroup Export
 * @brief Information about an export option.
 *
 * Although export options can be accessed by a static keyword they are
 * by definition opaque: the client can present them to the user and
 * manipulate them without knowing about their presence or purpose.
 * To do so, some information about the option is necessary,
 * given in this structure.
 * 
 * You can obtain this information with vbi_export_option_info_enum().
 */
typedef struct {
  	vbi_option_type		type;	/**< @see vbi_option_type */

	/**
	 * Unique (within the respective export module) keyword to identify
	 * this option. Can be stored in configuration files.
	 */
	char *			keyword;

	/**
	 * Name of the option to be shown to the user.
	 * This can be @c NULL to indicate this option shall not be listed.
	 * Can be localized with dgettext("zvbi", label).
	 */
	char *			label;

	vbi_option_value	def;	/**< @see vbi_option_type */
	vbi_option_value	min;	/**< @see vbi_option_type */
	vbi_option_value	max;	/**< @see vbi_option_type */
	vbi_option_value	step;	/**< @see vbi_option_type */
	vbi_option_value_ptr	menu;	/**< @see vbi_option_type */

	/**
	 * A brief description (or @c NULL) for the user.
	 *  Can be localized with dgettext("zvbi", tooltip).
	 */
	char *			tooltip;
} vbi_option_info;

/**
 * @addtogroup Export
 * @{
 */
extern vbi_export_info *	vbi_export_info_enum(int index);
extern vbi_export_info *	vbi_export_info_keyword(const char *keyword);
extern vbi_export_info *	vbi_export_info_export(vbi_export *);

extern vbi_export *		vbi_export_new(const char *keyword, char **errstr);
extern void			vbi_export_delete(vbi_export *);

extern vbi_option_info *	vbi_export_option_info_enum(vbi_export *, int index);
extern vbi_option_info *	vbi_export_option_info_keyword(vbi_export *, const char *keyword);

extern vbi_bool			vbi_export_option_set(vbi_export *, const char *keyword, ...);
extern vbi_bool			vbi_export_option_get(vbi_export *, const char *keyword,
						      vbi_option_value *value);
extern vbi_bool			vbi_export_option_menu_set(vbi_export *, const char *keyword, int entry);
extern vbi_bool			vbi_export_option_menu_get(vbi_export *, const char *keyword, int *entry);

extern ssize_t
vbi_export_mem			(vbi_export *		e,
				 void *			buffer,
				 size_t			buffer_size,
				 const vbi_page *	pg)
  _vbi_nonnull ((1)); /* sic */
extern void *
vbi_export_alloc		(vbi_export *		e,
				 void **		buffer,
				 size_t *		buffer_size,
				 const vbi_page *	pg)
  _vbi_nonnull ((1)); /* sic */
extern vbi_bool			vbi_export_stdio(vbi_export *, FILE *fp, vbi_page *pg);
extern vbi_bool			vbi_export_file(vbi_export *, const char *name, vbi_page *pg);

extern char *			vbi_export_errstr(vbi_export *);
/** @} */

/* Private */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#include <stdarg.h>
#include <stddef.h>

extern const char _zvbi_intl_domainname[];

#include "version.h"
#include "intl-priv.h"

#endif /* !DOXYGEN_SHOULD_SKIP_THIS */

typedef struct vbi_export_class vbi_export_class;

/** The export target. */
enum _vbi_export_target {
	/** Exporting to a client supplied buffer in memory. */
	VBI_EXPORT_TARGET_MEM = 1,

	/** Exporting to a newly allocated buffer. */
	VBI_EXPORT_TARGET_ALLOC,

	/** Exporting to a client supplied file pointer. */
	VBI_EXPORT_TARGET_FP,

	/** Exporting to a client supplied file descriptor. */
	VBI_EXPORT_TARGET_FD,

	/** Exporting to a file. */
	VBI_EXPORT_TARGET_FILE,
};

typedef vbi_bool
_vbi_export_write_fn		(vbi_export *		e,
				 const void *		s,
				 size_t			n_bytes);

/**
 * @ingroup Exmod
 *
 * Structure representing an export module instance, part of the private
 * export module interface.
 *
 * Export modules can read, but do not normally write its fields, as
 * they are maintained by the public libzvbi export functions.
 */
struct vbi_export {
	/**
	 * Points back to export module description.
	 */
	vbi_export_class *	_class;
	char *			errstr;		/**< Frontend private. */

	/**
	 * If @c target is @c VBI_EXPORT_FILE the name of the file
	 * we are writing to, as supplied by the client. Otherwise
	 * @c NULL. This is intended for debugging and error messages.
	 */
	const char *		name;

	/**
	 * Generic option: Network name or @c NULL.
	 */
	char *			network;	/* network name or NULL */
	/**
	 * Generic option: Creator name [by default "libzvbi"] or @c NULL.
	 */
	char *			creator;
	/**
	 * Generic option: Reveal hidden characters.
	 */
	vbi_bool		reveal;

	/**
	 * The export target. Note _vbi_export_grow_buffer_space() may
	 * change the target from TARGET_MEM to TARGET_ALLOC if the
	 * buffer supplied by the application is too small.
	 */
	enum _vbi_export_target	target;

	/**
	 * If @a target is @c VBI_EXPORT_TARGET_FP or
	 * @c VBI_EXPORT_TARGET_FD the file pointer or file descriptor
	 * supplied by the client. If @c VBI_EXPORT_TARGET_FILE the
	 * file descriptor of the file we opened. Otherwise undefined.
	 *
	 * Private field. Not to be accessed by export modules.
	 */
	union {
		FILE *			fp;
		int			fd;
	}			_handle;

	/**
	 * Function to write data into @a _handle.
	 *
	 * Private field. Not to be accessed by export modules.
	 */
	_vbi_export_write_fn *	_write;

	/**
	 * Output buffer. Export modules can write into this buffer
	 * directly after ensuring sufficient capacity, and/or call
	 * the vbi_export_putc() etc functions. Keep in mind these
	 * functions may call realloc(), changing the @a data pointer,
	 * and/or vbi_export_flush(), changing the @a offset.
	 */
	struct {
		/**
		 * Pointer to the start of the buffer in memory.
		 * @c NULL if @a capacity is zero.
		 */
		char *			data;

		/**
		 * The number of bytes written into the buffer
		 * so far. Must be <= @c capacity.
		 */
		size_t			offset;

		/**
		 * Number of bytes we can store in the buffer, may be
		 * zero.
		 *
		 * Call _vbi_export_grow_buffer_space() to increase the
		 * capacity. Keep in mind this may change the @a data
		 * pointer.
		 */
		size_t			capacity;
	}			buffer;

	/** A write error occurred (like ferror()). */
	vbi_bool		write_error;
};

/**
 * @ingroup Exmod
 *
 * Structure describing an export module, part of the private
 * export module interface. One required for each module.
 *
 * Export modules must initialize these fields (except @a next, see
 * exp-tmpl.c for a detailed discussion) and call vbi_export_register_module()
 * to become accessible.
 */
struct vbi_export_class {
	vbi_export_class *	next;
	vbi_export_info	*	_public;

	vbi_export *		(* _new)(void);
	void			(* _delete)(vbi_export *);

	vbi_option_info *	(* option_enum)(vbi_export *, int index);
	vbi_bool		(* option_set)(vbi_export *, const char *keyword,
					       va_list);
	vbi_bool		(* option_get)(vbi_export *, const char *keyword,
					       vbi_option_value *value);

	vbi_bool		(* export)(vbi_export *, vbi_page *pg);
};

/**
 * @example src/exp-templ.c
 * @ingroup Exmod
 *
 * Template for internal export module.
 */

/*
 *  Helper functions
 */

/* Output functions. */

extern vbi_bool
_vbi_export_grow_buffer_space	(vbi_export *		e,
				 size_t			min_space)
  _vbi_nonnull ((1));

extern vbi_bool
vbi_export_flush		(vbi_export *		e)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_export_putc			(vbi_export *		e,
				 int			c)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_export_write		(vbi_export *		e,
				 const void *		src,
				 size_t			src_size)
  _vbi_nonnull ((1, 2));
extern vbi_bool
vbi_export_puts			(vbi_export *		e,
				 const char *		src)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_export_puts_iconv		(vbi_export *		e,
				 const char *		dst_codeset,
				 const char *		src_codeset,
				 const char *		src,
				 unsigned long		src_size,
				 int			repl_char)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_export_puts_iconv_ucs2	(vbi_export *		e,
				 const char *		dst_codeset,
				 const uint16_t *	src,
				 long			src_length,
				 int			repl_char)
  _vbi_nonnull ((1));
extern vbi_bool
vbi_export_vprintf		(vbi_export *		e,
				 const char *		templ,
				 va_list		ap)
  _vbi_nonnull ((1, 2));
extern vbi_bool
vbi_export_printf		(vbi_export *		e,
				 const char *		templ,
				 ...)
  _vbi_nonnull ((1, 2)) _vbi_format ((printf, 2, 3));

/**
 * @addtogroup Exmod
 * @{
 */
extern void			vbi_register_export_module(vbi_export_class *);

extern void
_vbi_export_malloc_error	(vbi_export *		e);
extern void			vbi_export_write_error(vbi_export *);
extern void			vbi_export_unknown_option(vbi_export *, const char *keyword);
extern void			vbi_export_invalid_option(vbi_export *, const char *keyword, ...);
extern char *			vbi_export_strdup(vbi_export *, char **d, const char *s);
extern void			vbi_export_error_printf(vbi_export *, const char *templ, ...);

extern int			vbi_ucs2be(void);

/* Option info building */

#define VBI_OPTION_BOUNDS_INITIALIZER_(type_, def_, min_, max_, step_)	\
  { type_ = def_ }, { type_ = min_ }, { type_ = max_ }, { type_ = step_ }

/**
 * Helper macro for export modules to build option lists. Use like this:
 *
 * @code
 * vbi_option_info myinfo = VBI_OPTION_BOOL_INITIALIZER
 *   ("mute", N_("Switch sound on/off"), FALSE, N_("I am a tooltip"));
 * @endcode
 *
 * N_() marks the string for i18n, see info gettext for details.
 */
#define VBI_OPTION_BOOL_INITIALIZER(key_, label_, def_, tip_)		\
  { VBI_OPTION_BOOL, key_, label_, VBI_OPTION_BOUNDS_INITIALIZER_(	\
  .num, def_, 0, 1, 1),	{ .num = NULL }, tip_ }

/**
 * Helper macro for export modules to build option lists. Use like this:
 *
 * @code
 * vbi_option_info myinfo = VBI_OPTION_INT_RANGE_INITIALIZER
 *   ("sampling", N_("Sampling rate"), 44100, 8000, 48000, 100, NULL);
 * @endcode
 *
 * Here we have no tooltip (@c NULL).
 */
#define VBI_OPTION_INT_RANGE_INITIALIZER(key_, label_, def_, min_,	\
  max_,	step_, tip_) { VBI_OPTION_INT, key_, label_,			\
  VBI_OPTION_BOUNDS_INITIALIZER_(.num, def_, min_, max_, step_),	\
  { .num = NULL }, tip_ }

/**
 * Helper macro for export modules to build option lists. Use like this:
 *
 * @code
 * int mymenu[] = { 29, 30, 31 };
 *
 * vbi_option_info myinfo = VBI_OPTION_INT_MENU_INITIALIZER
 *   ("days", NULL, 1, mymenu, 3, NULL);
 * @endcode
 *
 * No label and tooltip (@c NULL), i. e. this option is not to be
 * listed in the user interface. Default is entry 1 ("30") of 3 entries. 
 */
#define VBI_OPTION_INT_MENU_INITIALIZER(key_, label_, def_,		\
  menu_, entries_, tip_) { VBI_OPTION_INT, key_, label_,		\
  VBI_OPTION_BOUNDS_INITIALIZER_(.num, def_, 0, (entries_) - 1, 1),	\
  { .num = menu_ }, tip_ }

/**
 * Helper macro for export modules to build option lists. Use like
 * VBI_OPTION_INT_RANGE_INITIALIZER(), just with doubles but ints.
 */
#define VBI_OPTION_REAL_RANGE_INITIALIZER(key_, label_, def_, min_,	\
  max_, step_, tip_) { VBI_OPTION_REAL, key_, label_,			\
  VBI_OPTION_BOUNDS_INITIALIZER_(.dbl, def_, min_, max_, step_),	\
  { .dbl = NULL }, tip_ }

/**
 * Helper macro for export modules to build option lists. Use like
 * VBI_OPTION_INT_MENU_INITIALIZER(), just with an array of doubles but ints.
 */
#define VBI_OPTION_REAL_MENU_INITIALIZER(key_, label_, def_,		\
  menu_, entries_, tip_) { VBI_OPTION_REAL, key_, label_,		\
  VBI_OPTION_BOUNDS_INITIALIZER_(.num, def_, 0, (entries_) - 1, 1),	\
  { .dbl = menu_ }, tip_ }

/**
 * Helper macro for export modules to build option lists. Use like this:
 *
 * @code
 * vbi_option_info myinfo = VBI_OPTION_STRING_INITIALIZER
 *   ("comment", N_("Comment"), "bububaba", "Please enter a string");
 * @endcode
 */
#define VBI_OPTION_STRING_INITIALIZER(key_, label_, def_, tip_)		\
  { VBI_OPTION_STRING, key_, label_, VBI_OPTION_BOUNDS_INITIALIZER_(	\
  .str, def_, NULL, NULL, NULL), { .str = NULL }, tip_ }

/**
 * Helper macro for export modules to build option lists. Use like this:
 *
 * @code
 * char *mymenu[] = { "txt", "html" };
 *
 * vbi_option_info myinfo = VBI_OPTION_STRING_MENU_INITIALIZER
 *   ("extension", "Ext", 0, mymenu, 2, N_("Select an extension"));
 * @endcode
 *
 * Remember this is like VBI_OPTION_STRING_INITIALIZER() in the sense
 * that the vbi client can pass any string as option value, not just those
 * proposed in the menu. In contrast a plain menu option as with
 * VBI_OPTION_MENU_INITIALIZER() expects menu indices as input.
 */
#define VBI_OPTION_STRING_MENU_INITIALIZER(key_, label_, def_,		\
  menu_, entries_, tip_) { VBI_OPTION_STRING, key_, label_,		\
  VBI_OPTION_BOUNDS_INITIALIZER_(.str, def_, 0, (entries_) - 1, 1),	\
  { .str = menu_ }, tip_ }

/**
 * Helper macro for export modules to build option lists. Use like this:
 *
 * @code
 * char *mymenu[] = { N_("Monday"), N_("Tuesday") };
 *
 * vbi_option_info myinfo = VBI_OPTION_MENU_INITIALIZER
 *   ("weekday", "Weekday", 0, mymenu, 2, N_("Select a weekday"));
 * @endcode
 */
#define VBI_OPTION_MENU_INITIALIZER(key_, label_, def_, menu_,		\
  entries_, tip_) { VBI_OPTION_MENU, key_, label_,			\
  VBI_OPTION_BOUNDS_INITIALIZER_(.num, def_, 0, (entries_) - 1, 1),	\
  { .str = (char **)(menu_) }, tip_ }

/* See exp-templ.c for an example */

/** Doesn't work, sigh. */
#define VBI_AUTOREG_EXPORT_MODULE(name)
/*
#define VBI_AUTOREG_EXPORT_MODULE(name)					\
static void vbi_autoreg_##name(void) _vbi_attribute ((constructor));	\
static void vbi_autoreg_##name(void) {					\
	vbi_register_export_module(&name);				\
}
*/

/** @} */

#endif /* EXPORT_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
