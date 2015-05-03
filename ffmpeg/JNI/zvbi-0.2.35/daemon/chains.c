/*
 *  VBI proxy wrapper for proxy-unaware clients
 *
 *  Copyright (C) 2004 Tom Zoerner
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
 *
 *
 *  Description:
 *
 *    This is a small wrapper which executes the VBI application given
 *    on the command line while overloading several C library calls
 *    (such as open(2) and read(2)) so that the application can be forced
 *    to access VBI devices via the VBI proxy instead of device files
 *    directly.
 *
 *    LD_PRELOAD is used to intercept C library calls and call functions
 *    in the libvbichain shared library instead.  Parameters given on the
 *    command line (e.g. device path) are passed to the library by means
 *    of environment variables.
 *
 *  $Log: chains.c,v $
 *  Revision 1.4  2008/07/26 06:22:28  mschimek
 *  Changed the license to GPLv2+ with Tom's permission.
 *
 *  Revision 1.3  2007/11/27 17:39:34  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.2  2006/05/22 09:02:43  mschimek
 *  s/vbi_asprintf/asprintf.
 *
 *  Revision 1.1  2004/10/25 16:52:43  mschimek
 *  main: Replaced sprintf by asprintf and fixed p_env3.
 *  Added from proxy-18.bak.
 *
 */

static const char rcsid[] = "$Id: chains.c,v 1.4 2008/07/26 06:22:28 mschimek Exp $";

#include "config.h"

#ifdef ENABLE_PROXY

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include "src/misc.h"		/* asprintf() */

#define dprintf1(fmt, arg...)    do {if (opt_debug_level >= 1) fprintf(stderr, "proxyd: " fmt, ## arg);} while (0)
#define dprintf2(fmt, arg...)    do {if (opt_debug_level >= 2) fprintf(stderr, "proxyd: " fmt, ## arg);} while (0)


static char * opt_vbi_device = "";
static int    opt_debug_level = 0;

/* ---------------------------------------------------------------------------
** Print usage and exit
*/
static void
usage_exit( const char *argv0, const char *argvn, const char * reason )
{
   fprintf(stderr, "%s: %s: %s\n"
                   "Usage: %s [options ...] command ...\n"
                   "       -dev <path>         : VBI device path (default: any VBI device)\n"
                   "       -debug <level>      : enable debug output: 1=warnings, 2=all\n"
                   "       -help               : this message\n"
                   "       --                  : stop option processing\n",
                   argv0, reason, argvn, argv0);

   exit(1);
}

/* ---------------------------------------------------------------------------
** Parse numeric value in command line options
*/
static int
parse_argv_numeric( char * p_number, int * p_value )
{
   char * p_num_end;

   if (*p_number != 0)
   {
      *p_value = strtol(p_number, &p_num_end, 0);

      return (*p_num_end == 0);
   }
   else
      return 0;
}

/* ---------------------------------------------------------------------------
** Parse command line options
*/
static void
parse_argv( int argc, char * argv[], int * p_arg_off )
{
   struct stat stb;
   int arg_val;
   int arg_idx = 1;

   while (arg_idx < argc)
   {
      if (strcasecmp(argv[arg_idx], "-dev") == 0)
      {
         if (arg_idx + 1 < argc)
         {
            if (stat(argv[arg_idx + 1], &stb) == -1)
               usage_exit(argv[0], argv[arg_idx +1], strerror(errno));
            if (!S_ISCHR(stb.st_mode))
               usage_exit(argv[0], argv[arg_idx +1], "not a character device");
            if (access(argv[arg_idx + 1], R_OK | W_OK) == -1)
               usage_exit(argv[0], argv[arg_idx +1], strerror(errno));

            opt_vbi_device = argv[arg_idx + 1];
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing mode keyword after");
      }
      else if (strcasecmp(argv[arg_idx], "-debug") == 0)
      {
         if ((arg_idx + 1 < argc) && parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_debug_level = arg_val;
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing debug level after");
      }
      else if (strcasecmp(argv[arg_idx], "-help") == 0)
      {
         usage_exit(argv[0], "", "the following options are available");
      }
      else if (strcmp(argv[arg_idx], "--") == 0)
      {
         arg_idx += 1;
         break;
      }
      else if (*argv[arg_idx] == '-')
      {
         usage_exit(argv[0], argv[arg_idx], "unknown option or argument");
      }
      else
         break;
   }

   if (arg_idx >= argc)
   {
      usage_exit(argv[0], "", "name of application to launch is missing");
   }

   * p_arg_off = arg_idx;
}

/* ----------------------------------------------------------------------------
** Main
*/

#define putenv_printf(bp, tmpl, args...)				\
do {									\
	asprintf (&(bp), tmpl ,##args );				\
	assert (NULL != (bp));						\
	putenv (bp);							\
} while (0)

int
main( int argc, char ** argv )
{
   int    arg_off;
   char * p_old_preload;
   char * p_env1;
   char * p_env2;
   char * p_env3;
   char * p_env4;

   parse_argv(argc, argv, &arg_off);

   putenv_printf(p_env1, "VBIPROXY_DEVICE=%s", opt_vbi_device);
   putenv_printf(p_env2, "VBIPROXY_DEBUG=%d", opt_debug_level);
   putenv_printf(p_env3, "VBIPROXY_CLIENT=%s [vbi-chains]", argv[arg_off]);

   p_old_preload = getenv("LD_PRELOAD");
   if (p_old_preload == NULL)
   {  /* no preload defined yet */
      putenv_printf(p_env4, "LD_PRELOAD=%s", LIBZVBI_CHAINS_PATH);
   }
   else
   {  /* prepend preload to existing definition */
      putenv_printf(p_env4, "LD_PRELOAD=%s:%s",
		    LIBZVBI_CHAINS_PATH, p_old_preload);
   }

   if (opt_debug_level > 0)
   {
      fprintf(stderr, "vbi-chains: Environment set-up:\n"
	      "\t%s\n\t%s\n\t%s\n\t%s\n",
	      p_env1, p_env2, p_env3, p_env4);
   }

   execvp(argv[arg_off], argv + arg_off);
   fprintf(stderr, "vbi_chains: Failed to start %s: %s\n",
	   argv[arg_off], strerror(errno));

   exit(-1);
   return -1;
}

#endif  /* ENABLE_PROXY */
