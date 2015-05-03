/*
 *  VBI proxy test client
 *
 *  Copyright (C) 2003, 2004 Tom Zoerner
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
 *    This is a small demo application for the VBI proxy and libzvbi.
 *    It will read VBI data from the device given on the command line
 *    and dump requested services' data to standard output.  See below
 *    for a list of possible options.
 *
 *  $Log: proxy-test.c,v $
 *  Revision 1.21  2008/07/26 06:22:19  mschimek
 *  Changed the license to GPLv2+ with Tom's permission.
 *
 *  Revision 1.20  2008/03/01 07:36:32  mschimek
 *  Line feed cosmetics.
 *
 *  Revision 1.19  2007/11/27 18:26:48  mschimek
 *  Updated the FSF address in the copyright notice.
 *
 *  Revision 1.18  2007/11/03 21:15:38  tomzo
 *  Bugfix setup of raw capture handling
 *
 *  Revision 1.17  2006/02/10 06:25:38  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.16  2004/12/31 06:05:20  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.15  2004/11/07 10:52:47  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.4  2004/11/07 08:18:53  michael
 *  *** empty log message ***
 *
 *  Revision 1.14  2004/11/03 17:07:40  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.3  2004/10/29 01:49:04  michael
 *  *** empty log message ***
 *
 *  Revision 1.13  2004/10/25 16:56:30  mschimek
 *  *** empty log message ***
 *
 *  Revision 1.12  2004/10/24 18:20:00  tomzo
 *  Added test support for norm change handling
 *
 *  Revision 1.7  2003/06/07 09:43:23  tomzo
 *  Added test for proxy with select() and zero timeout (#if 0'ed)
 *
 *  Revision 1.6  2003/06/01 19:36:42  tomzo
 *  Added tests for TV channel switching
 *  - added new command line options -channel, -freq, -chnprio
 *  - use new func vbi_capture_channel_change()
 *
 *  Revision 1.5  2003/05/24 12:19:57  tomzo
 *  - added dynamic service switch to test add_service() interface: new function
 *    read_service_string() reads service requests from stdin
 *  - added new service closed caption
 *
 *  Revision 1.4  2003/05/10 13:31:23  tomzo
 *  - bugfix main loop: check for 0 result from vbi_capture_pull_sliced()
 *    and for NULL pointer for sliced buffer
 *  - added new "-debug 0..2" option: old "-trace" could only set level 1
 *  - split off argv parsing from main function
 *
 *  Revision 1.3  2003/05/03 12:07:48  tomzo
 *  - use vbi_capture_pull_sliced() instead of vbi_capture_read_sliced()
 *  - fixed copyright headers, added description to file headers
 *
 */

static const char
rcsid [] = "$Id: proxy-test.c,v 1.21 2008/07/26 06:22:19 mschimek Exp $";

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <inttypes.h>

#define USE_LIBZVBI
#include "src/libzvbi.h"

#ifdef ENABLE_V4L
#include "src/videodev.h"
#endif


#define DEVICE_PATH   "/dev/vbi0"
#define BUFFER_COUNT  5

typedef enum
{
   TEST_API_V4L,
   TEST_API_V4L2,
   TEST_API_PROXY,
} PROXY_TEST_API;

typedef enum
{
   TEST_SCANNING_BOTH  = 0,
   TEST_SCANNING_625   = 625,
   TEST_SCANNING_525   = 525,
} PROXY_TEST_SCANNING;

static char         * p_dev_name = DEVICE_PATH;
static PROXY_TEST_API opt_api = TEST_API_PROXY;
static PROXY_TEST_SCANNING opt_scanning = TEST_SCANNING_BOTH;
static unsigned int   opt_services;
static int            opt_strict;
static int            opt_debug_level;
static int            opt_channel;
static int            opt_frequency;
static int            opt_chnprio;
static int            opt_subprio;

static int            update_services;


#define ALL_SERVICES_625 ( VBI_SLICED_TELETEXT_B | \
                           VBI_SLICED_VPS | \
                           VBI_SLICED_CAPTION_625 | \
                           VBI_SLICED_WSS_625 | \
                           VBI_SLICED_VBI_625 )
#define ALL_SERVICES_525 ( VBI_SLICED_CAPTION_525 | \
                           VBI_SLICED_2xCAPTION_525 | \
                           VBI_SLICED_TELETEXT_BD_525 | \
                           VBI_SLICED_VBI_525 )

/* ---------------------------------------------------------------------------
** Switch channel and frequency (Video 4 Linux #1 API)
*/
static vbi_bool SwitchTvChannel( vbi_proxy_client * vpc, int channel, int freq )
{
#ifdef ENABLE_V4L
   struct video_channel vchan;
   long lfreq;
   vbi_bool result;

   result = TRUE;

   // get current config of the selected chanel
   if (channel != -1)
   {
      result = FALSE;

      memset(&vchan, 0, sizeof(vchan));
      vchan.channel = channel;
      if (opt_scanning == TEST_SCANNING_625)
         vchan.norm = VIDEO_MODE_PAL;
      else if (opt_scanning == TEST_SCANNING_525)
         vchan.norm = VIDEO_MODE_NTSC;

      if (vbi_proxy_client_device_ioctl(vpc, VIDIOCGCHAN, &vchan) == 0)
      {
         vchan.channel = channel;

         if (vbi_proxy_client_device_ioctl(vpc, VIDIOCSCHAN, &vchan) == 0)
         {
            result = TRUE;
         }
         else
            perror("ioctl VIDIOCSCHAN");
      }
      else
         perror("ioctl VIDIOCGCHAN");
   }

   if (freq != -1)
   {
      result = FALSE;

      if ( (channel == -1) ||
           ((vchan.type & VIDEO_TYPE_TV) && (vchan.flags & VIDEO_VC_TUNER)) )
      {
         lfreq = freq;
         if (vbi_proxy_client_device_ioctl(vpc, VIDIOCSFREQ, &lfreq) == 0)
         {
            result = TRUE;
         }
         else
            perror("ioctl VIDIOCSFREQ");
      }
      else
         fprintf(stderr, "cannot tune frequency: channel has no tuner\n");
   }
   return result;
#else
   return FALSE;
#endif
}

/* ----------------------------------------------------------------------------
** Callback for proxy events
*/
static void ProxyEventCallback( void * p_client_data, VBI_PROXY_EV_TYPE ev_mask )
{
   vbi_proxy_client * pProxyClient;
   VBI_PROXY_CHN_FLAGS  flags;

   if (p_client_data != NULL)
   {
      pProxyClient = *(vbi_proxy_client **) p_client_data;
      if (pProxyClient != NULL)
      {
         if (ev_mask & VBI_PROXY_EV_CHN_RECLAIMED)
         {
            fprintf(stderr, "ProxyEventCallback: token was reclaimed\n");

            vbi_proxy_client_channel_notify(pProxyClient, VBI_PROXY_CHN_TOKEN, 0);
         }
         else if (ev_mask & VBI_PROXY_EV_CHN_GRANTED)
         {
            fprintf(stderr, "ProxyEventCallback: token granted\n");

            if ((opt_channel != -1) || (opt_frequency != -1))
            {
               if (SwitchTvChannel(pProxyClient, opt_channel, opt_frequency))
                  flags = VBI_PROXY_CHN_TOKEN | VBI_PROXY_CHN_FLUSH;
               else
                  flags = VBI_PROXY_CHN_RELEASE | VBI_PROXY_CHN_FAIL | VBI_PROXY_CHN_FLUSH;

               if (opt_scanning != TEST_SCANNING_BOTH)
                  flags |= VBI_PROXY_CHN_NORM;
            }
            else
               flags = VBI_PROXY_CHN_RELEASE;

            vbi_proxy_client_channel_notify(pProxyClient, flags, opt_scanning);
         }
         if (ev_mask & VBI_PROXY_EV_NORM_CHANGED)
         {
            fprintf(stderr, "ProxyEventCallback: TV norm changed\n");
            update_services = TRUE;
         }
      }
   }
}

/* ----------------------------------------------------------------------------
** Resolve parity on an array in-place
** - errors are ignored, the character just replaced by a blank
** - non-printable characters are replaced by blanks
*/
static const signed char parityTab[256] =
{
   //0x80, 0x01, 0x02, 0x83, 0x04, 0x85, 0x86, 0x07,  // non-printable
   //0x08, 0x89, 0x8a, 0x0b, 0x8c, 0x0d, 0x0e, 0x8f,  // non-printable
   //0x10, 0x91, 0x92, 0x13, 0x94, 0x15, 0x16, 0x97,  // non-printable
   //0x98, 0x19, 0x1a, 0x9b, 0x1c, 0x9d, 0x9e, 0x1f,  // non-printable
   0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
   0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
   0x20, 0xa1, 0xa2, 0x23, 0xa4, 0x25, 0x26, 0xa7,
   0xa8, 0x29, 0x2a, 0xab, 0x2c, 0xad, 0xae, 0x2f,
   0xb0, 0x31, 0x32, 0xb3, 0x34, 0xb5, 0xb6, 0x37,
   0x38, 0xb9, 0xba, 0x3b, 0xbc, 0x3d, 0x3e, 0xbf,
   0x40, 0xc1, 0xc2, 0x43, 0xc4, 0x45, 0x46, 0xc7,
   0xc8, 0x49, 0x4a, 0xcb, 0x4c, 0xcd, 0xce, 0x4f,
   0xd0, 0x51, 0x52, 0xd3, 0x54, 0xd5, 0xd6, 0x57,
   0x58, 0xd9, 0xda, 0x5b, 0xdc, 0x5d, 0x5e, 0xdf,
   0xe0, 0x61, 0x62, 0xe3, 0x64, 0xe5, 0xe6, 0x67,
   0x68, 0xe9, 0xea, 0x6b, 0xec, 0x6d, 0x6e, 0xef,
   0x70, 0xf1, 0xf2, 0x73, 0xf4, 0x75, 0x76, 0xf7,
   0xf8, 0x79, 0x7a, 0xfb, 0x7c, 0xfd, 0xfe,       0x20, // 0x7f,

   //0x00, 0x81, 0x82, 0x03, 0x84, 0x05, 0x06, 0x87,  // non-printable
   //0x88, 0x09, 0x0a, 0x8b, 0x0c, 0x8d, 0x8e, 0x0f,  // non-printable
   //0x90, 0x11, 0x12, 0x93, 0x14, 0x95, 0x96, 0x17,  // non-printable
   //0x18, 0x99, 0x9a, 0x1b, 0x9c, 0x1d, 0x1e, 0x9f,  // non-printable
   0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
   0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,

   0xa0, 0x21, 0x22, 0xa3, 0x24, 0xa5, 0xa6, 0x27,
   0x28, 0xa9, 0xaa, 0x2b, 0xac, 0x2d, 0x2e, 0xaf,
   0x30, 0xb1, 0xb2, 0x33, 0xb4, 0x35, 0x36, 0xb7,
   0xb8, 0x39, 0x3a, 0xbb, 0x3c, 0xbd, 0xbe, 0x3f,
   0xc0, 0x41, 0x42, 0xc3, 0x44, 0xc5, 0xc6, 0x47,
   0x48, 0xc9, 0xca, 0x4b, 0xcc, 0x4d, 0x4e, 0xcf,
   0x50, 0xd1, 0xd2, 0x53, 0xd4, 0x55, 0x56, 0xd7,
   0xd8, 0x59, 0x5a, 0xdb, 0x5c, 0xdd, 0xde, 0x5f,
   0x60, 0xe1, 0xe2, 0x63, 0xe4, 0x65, 0x66, 0xe7,
   0xe8, 0x69, 0x6a, 0xeb, 0x6c, 0xed, 0xee, 0x6f,
   0xf0, 0x71, 0x72, 0xf3, 0x74, 0xf5, 0xf6, 0x77,
   0x78, 0xf9, 0xfa, 0x7b, 0xfc, 0x7d, 0x7e, 0xff,
};
static int UnHamParityArray( const unsigned char *pin, char *pout, int byteCount )
{
   int errCount;
   signed char c1;

   errCount = 0;
   for (; byteCount > 0; byteCount--)
   {
      c1 = (char)parityTab[*(pin++)];
      if (c1 > 0)
      {
         *(pout++) = c1;
      }
      else
      {
         *(pout++) = 0xA0;  /* Latin-1 space character */
         errCount += 1;
      }
   }

   return errCount;
}

static const unsigned char unhamTab[256] =
{
   0x01, 0xff, 0x01, 0x01, 0xff, 0x00, 0x01, 0xff,
   0xff, 0x02, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x07,
   0xff, 0x00, 0x01, 0xff, 0x00, 0x00, 0xff, 0x00,
   0x06, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x03, 0xff,
   0xff, 0x0c, 0x01, 0xff, 0x04, 0xff, 0xff, 0x07,
   0x06, 0xff, 0xff, 0x07, 0xff, 0x07, 0x07, 0x07,
   0x06, 0xff, 0xff, 0x05, 0xff, 0x00, 0x0d, 0xff,
   0x06, 0x06, 0x06, 0xff, 0x06, 0xff, 0xff, 0x07,
   0xff, 0x02, 0x01, 0xff, 0x04, 0xff, 0xff, 0x09,
   0x02, 0x02, 0xff, 0x02, 0xff, 0x02, 0x03, 0xff,
   0x08, 0xff, 0xff, 0x05, 0xff, 0x00, 0x03, 0xff,
   0xff, 0x02, 0x03, 0xff, 0x03, 0xff, 0x03, 0x03,
   0x04, 0xff, 0xff, 0x05, 0x04, 0x04, 0x04, 0xff,
   0xff, 0x02, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x07,
   0xff, 0x05, 0x05, 0x05, 0x04, 0xff, 0xff, 0x05,
   0x06, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x03, 0xff,
   0xff, 0x0c, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x09,
   0x0a, 0xff, 0xff, 0x0b, 0x0a, 0x0a, 0x0a, 0xff,
   0x08, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x0d, 0xff,
   0xff, 0x0b, 0x0b, 0x0b, 0x0a, 0xff, 0xff, 0x0b,
   0x0c, 0x0c, 0xff, 0x0c, 0xff, 0x0c, 0x0d, 0xff,
   0xff, 0x0c, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x07,
   0xff, 0x0c, 0x0d, 0xff, 0x0d, 0xff, 0x0d, 0x0d,
   0x06, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x0d, 0xff,
   0x08, 0xff, 0xff, 0x09, 0xff, 0x09, 0x09, 0x09,
   0xff, 0x02, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x09,
   0x08, 0x08, 0x08, 0xff, 0x08, 0xff, 0xff, 0x09,
   0x08, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x03, 0xff,
   0xff, 0x0c, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x09,
   0x0f, 0xff, 0x0f, 0x0f, 0xff, 0x0e, 0x0f, 0xff,
   0x08, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x0d, 0xff,
   0xff, 0x0e, 0x0f, 0xff, 0x0e, 0x0e, 0xff, 0x0e
};
#define UnHam84Byte(P,V)   (( *(V) = ((int)unhamTab[*((const unsigned char *)(P))] | ((int)unhamTab[*(((const unsigned char *)(P))+1)] << 4)) ) >= 0 )

/* ---------------------------------------------------------------------------
** Decode a teletext data line
*/
static void PrintTeletextData( const unsigned char * data, int line, int id )
{
   char  tmparr[46];
   int   tmp1, tmp2, tmp3;
   uint  mag, pkgno, pageNo, sub;

   mag    =    0xF;
   pkgno  =   0xFF;
   pageNo =  0xFFF;
   sub    = 0xFFFF;
   if (UnHam84Byte(data, &tmp1))
   {
      pkgno = (tmp1 >> 3) & 0x1f;
      mag   = tmp1 & 7;
      if (mag == 0)
         mag = 8;
      if (UnHam84Byte(data + 2, &tmp1) &&
          UnHam84Byte(data + 4, &tmp2) &&
          UnHam84Byte(data + 6, &tmp3))
      {  /* it's a page header: decode page number and sub-page code */
         pageNo = tmp1 | ((uint)mag << 8);
         sub    = (tmp2 | (tmp3 << 8)) & 0x3f7f;
      }
   }

   if (pkgno != 0)
   {
      UnHamParityArray(data+2, tmparr, 40);
      tmparr[40] = 0;
      printf("line %3d id=%d pkg %X.%03X: '%s'\n", line, id, mag, pkgno, tmparr);
   }
   else
   {
      UnHamParityArray(data+2+8, tmparr, 40-8);
      tmparr[40-8] = 0;
      printf("line %3d id=%d page %03X.%04X: '%s'\n", line, id, pageNo, sub, tmparr);
   }
}

/* ---------------------------------------------------------------------------
** Decode a VPS data line
** - bit fields are defined in "VPS Richtlinie 8R2" from August 1995
** - called by the VBI decoder for every received VPS line
*/
static void PrintVpsData( const unsigned char * data )
{
   uint mday, month, hour, minute;
   uint cni;

#define VPSOFF -3

   cni = ((data[VPSOFF+13] & 0x3) << 10) | ((data[VPSOFF+14] & 0xc0) << 2) |
         ((data[VPSOFF+11] & 0xc0)) | (data[VPSOFF+14] & 0x3f);

   if ((cni != 0) && (cni != 0xfff))
   {
      if (cni == 0xDC3)
      {  /* special case: "ARD/ZDF Gemeinsames Vormittagsprogramm" */
         cni = (data[VPSOFF+5] & 0x20) ? 0xDC1 : 0xDC2;
      }

      /* decode VPS PIL */
      mday   =  (data[VPSOFF+11] & 0x3e) >> 1;
      month  = ((data[VPSOFF+12] & 0xe0) >> 5) | ((data[VPSOFF+11] & 1) << 3);
      hour   =  (data[VPSOFF+12] & 0x1f);
      minute =  (data[VPSOFF+13] >> 2);

      printf("VPS %d.%d. %02d:%02d CNI 0x%04X\n", mday, month, hour, minute, cni);
   }
}

/* ---------------------------------------------------------------------------
** Check stdin for services change requests
** - syntax: ["+"|"-"|"="]keyword, e.g. "+vps-ttx" or "=wss"
*/
static unsigned int read_service_string( void )
{
   unsigned int    services;
   unsigned int    tmp_services;
   struct timeval  timeout;
   vbi_bool substract;
   fd_set   rd;
   char  buf[100];
   char  *p_inp;
   int   ret;

   services = opt_services;
   timeout.tv_sec  = 0;
   timeout.tv_usec = 0;
   FD_ZERO(&rd);
   FD_SET(0, &rd);

   ret = select(1, &rd, NULL, NULL, &timeout);
   if (ret == 1)
   {
      ret = read(0, buf, sizeof(buf));
      if (ret > 0)
      {
         p_inp = buf;
         while (*p_inp != 0)
         {
            while (*p_inp == ' ')
               p_inp += 1;

            substract = FALSE;
            if (*p_inp == '=')
            {
               services = 0;
               p_inp += 1;
            }
            else if (*p_inp == '-')
            {
               substract = TRUE;
               p_inp += 1;
            }
            else if (*p_inp == '+')
               p_inp += 1;

            if ( (strncasecmp(p_inp, "ttx", 3) == 0) ||
                 (strncasecmp(p_inp, "teletext", 8) == 0) )
            {
               tmp_services = VBI_SLICED_TELETEXT_B | VBI_SLICED_TELETEXT_BD_525;
            }
            else if (strncasecmp(p_inp, "vps", 3) == 0)
            {
               tmp_services = VBI_SLICED_VPS;
            }
            else if (strncasecmp(p_inp, "wss", 3) == 0)
            {
               tmp_services = VBI_SLICED_WSS_625 | VBI_SLICED_WSS_CPR1204;
            }
            else if ( (strncasecmp(p_inp, "cc", 2) == 0) ||
                      (strncasecmp(p_inp, "caption", 7) == 0) )
            {
               tmp_services = VBI_SLICED_CAPTION_625 | VBI_SLICED_CAPTION_525;
            }
            else if (strncasecmp(p_inp, "raw", 3) == 0)
            {
               tmp_services = VBI_SLICED_VBI_625 | VBI_SLICED_VBI_525;
            }
            else
               tmp_services = 0;

            if (substract == FALSE)
               services |= tmp_services;
            else
               services &= ~ tmp_services;

            while ((*p_inp != 0) && (*p_inp != '+') && (*p_inp != '-'))
               p_inp += 1;
         }
      }
      else if ((ret < 0) && (errno != EINTR) && (errno != EAGAIN))
      {
         fprintf(stderr, "read_service_string: read: %d (%s)\n", errno, strerror(errno));
      }
   }
   else if ((ret < 0) && (errno != EINTR) && (errno != EAGAIN))
   {
      fprintf(stderr, "read_service_string: select: %d (%s)\n", errno, strerror(errno));
   }
   return services;
}

/* ---------------------------------------------------------------------------
** Print usage and exit
*/
static void usage_exit( const char *argv0, const char *argvn, const char * reason )
{
   fprintf(stderr, "%s: %s: %s\n"
                   "Usage: %s [ Options ] service ...\n"
                   "Supported services         : ttx | vps | wss | cc | raw\n"
                   "Supported options:\n"
                   "       -dev <path>         : device path\n"
                   "       -api <type>         : v4l API: proxy|v4l2|v4l\n"
                   "       -strict <level>     : service strictness level: 0..2\n"
                   "       -channel <index>    : switch video input channel\n"
                   "       -freq <kHz * 16>    : switch TV tuner frequency\n"
                   "       -chnprio <1..3>     : channel switch priority\n"
                   "       -subprio <0..4>     : background scheduling priority\n"
                   "       -debug <level>      : enable debug output: 1=warnings, 2=all\n"
                   "       -help               : this message\n"
                   "You can also type service requests to stdin at runtime:\n"
                   "Format: [\"+\"|\"-\"|\"=\"]<service>, e.g. \"+vps -ttx\" or \"=wss\"\n",
                   argv0, reason, argvn, argv0);

   exit(1);
}

/* ---------------------------------------------------------------------------
** Parse numeric value in command line options
*/
static vbi_bool parse_argv_numeric( char * p_number, int * p_value )
{
   char * p_num_end;

   if (*p_number != 0)
   {
      *p_value = strtol(p_number, &p_num_end, 0);

      return (*p_num_end == 0);
   }
   else
      return FALSE;
}

/* ---------------------------------------------------------------------------
** Parse command line options
*/
static void parse_argv( int argc, char * argv[] )
{
   int arg_val;
   int arg_idx = 1;
   int have_service = 0;

   opt_debug_level = 0;
   opt_services = 0;
   opt_strict = 0;
   opt_channel = -1;
   opt_frequency = -1;
   opt_chnprio = VBI_CHN_PRIO_INTERACTIVE;
   opt_subprio = 0;

   while (arg_idx < argc)
   {
      if ( (strcasecmp(argv[arg_idx], "ttx") == 0) ||
           (strcasecmp(argv[arg_idx], "teletext") == 0) )
      {
         opt_services |= VBI_SLICED_TELETEXT_B | VBI_SLICED_TELETEXT_BD_525;
         have_service = 1;
         arg_idx += 1;
      }
      else if (strcasecmp(argv[arg_idx], "vps") == 0)
      {
         opt_services |= VBI_SLICED_VPS;
         have_service = 1;
         arg_idx += 1;
      }
      else if (strcasecmp(argv[arg_idx], "wss") == 0)
      {
         opt_services |= VBI_SLICED_WSS_625 | VBI_SLICED_WSS_CPR1204;
         have_service = 1;
         arg_idx += 1;
      }
      else if ( (strcasecmp(argv[arg_idx], "cc") == 0) ||
                (strcasecmp(argv[arg_idx], "caption") == 0) )
      {
         opt_services |= VBI_SLICED_CAPTION_625 | VBI_SLICED_CAPTION_525;
         have_service = 1;
         arg_idx += 1;
      }
      else if (strcasecmp(argv[arg_idx], "raw") == 0)
      {
         opt_services |= VBI_SLICED_VBI_625 | VBI_SLICED_VBI_525;
         have_service = 1;
         arg_idx += 1;
      }
      else if (strcasecmp(argv[arg_idx], "null") == 0)
      {
         have_service = 1;
         arg_idx += 1;
      }
      else if (strcasecmp(argv[arg_idx], "-dev") == 0)
      {
         if (arg_idx + 1 < argc)
         {
            p_dev_name = argv[arg_idx + 1];
	    if (access(p_dev_name, R_OK | W_OK) == -1)
               usage_exit(argv[0], argv[arg_idx +1], "failed to access device");
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing mode keyword after");
      }
      else if (strcasecmp(argv[arg_idx], "-api") == 0)
      {
         if (arg_idx + 1 < argc)
         {
            if (strcasecmp(argv[arg_idx + 1], "proxy") == 0)
               opt_api = TEST_API_PROXY;
#ifdef USE_LIBZVBI
            else if ( (strcasecmp(argv[arg_idx + 1], "v4l") == 0) ||
                      (strcasecmp(argv[arg_idx + 1], "v4l1") == 0) )
               opt_api = TEST_API_V4L;
            else if (strcasecmp(argv[arg_idx + 1], "v4l2") == 0)
               opt_api = TEST_API_V4L2;
#endif
            else
               usage_exit(argv[0], argv[arg_idx +1], "unknown API keyword");
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing mode keyword after");
      }
      else if (strcasecmp(argv[arg_idx], "-norm") == 0)
      {
         if (arg_idx + 1 < argc)
         {
            if ( (strcasecmp("PAL", argv[arg_idx + 1]) == 0) ||
                 (strcasecmp("SECAM", argv[arg_idx + 1]) == 0) )
               opt_scanning = TEST_SCANNING_625;
            else if (strcasecmp("NTSC", argv[arg_idx + 1]) == 0)
               opt_scanning = TEST_SCANNING_525;
            else
               usage_exit(argv[0], argv[arg_idx], "missing mode keyword after");
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing mode keyword after");
      }
      else if (strcasecmp(argv[arg_idx], "-trace") == 0)
      {
         opt_debug_level = 1;
         arg_idx += 1;
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
      else if (strcasecmp(argv[arg_idx], "-strict") == 0)
      {
         if ((arg_idx + 1 < argc) && parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_strict = arg_val;
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing strict level after");
      }
      else if (strcasecmp(argv[arg_idx], "-channel") == 0)
      {
         if ((arg_idx + 1 < argc) && parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_channel = arg_val;
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing channel index after");
      }
      else if (strcasecmp(argv[arg_idx], "-freq") == 0)
      {
         if ((arg_idx + 1 < argc) && parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_frequency = arg_val;
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing frequency value after");
      }
      else if (strcasecmp(argv[arg_idx], "-chnprio") == 0)
      {
         if ((arg_idx + 1 < argc) && parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_chnprio = arg_val;
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing priority level after");
      }
      else if (strcasecmp(argv[arg_idx], "-subprio") == 0)
      {
         if ((arg_idx + 1 < argc) && parse_argv_numeric(argv[arg_idx + 1], &arg_val))
         {
            opt_subprio = arg_val;
            arg_idx += 2;
         }
         else
            usage_exit(argv[0], argv[arg_idx], "missing priority level after");
      }
      else if (strcasecmp(argv[arg_idx], "-help") == 0)
      {
         usage_exit(argv[0], "", "the following options are available");
      }
      else
         usage_exit(argv[0], argv[arg_idx], "unknown option or argument");
   }

   if (have_service == 0)
   {
      usage_exit(argv[0], "no service given", "Must specify at least one service");
   }

   if (opt_scanning == TEST_SCANNING_625)
      opt_services &= ALL_SERVICES_625;
   else if (opt_scanning == TEST_SCANNING_525)
      opt_services &= ALL_SERVICES_525;
}


/* ----------------------------------------------------------------------------
** Main entry point
*/
int main ( int argc, char ** argv )
{
   vbi_proxy_client   * pProxyClient;
   vbi_capture        * pVbiCapt;
   vbi_sliced         * pVbiData;
   vbi_capture_buffer * pVbiBuf;
   vbi_raw_decoder      raw_dec;
   char               * pErr;
   struct timeval       timeout;
   unsigned int         new_services;
   unsigned int         cur_services;
   unsigned int       * p_services;
   uint    lineCount;
   uint    lastLineCount;
   uint    line;
   int     res;

   parse_argv(argc, argv);

   fcntl(0, F_SETFL, O_NONBLOCK);

   if ((opt_services != 0) && (opt_scanning == 0))
   {
      cur_services = opt_services;
      p_services = &cur_services;
   }
   else
   {
      cur_services = 0;
      p_services = NULL;
   }

   pProxyClient = NULL;
   pVbiCapt = NULL;
#ifdef USE_LIBZVBI
   if (opt_api == TEST_API_V4L2)
      pVbiCapt = vbi_capture_v4l2_new(p_dev_name, BUFFER_COUNT, p_services, opt_strict, &pErr, opt_debug_level);
   if (opt_api == TEST_API_V4L)
      pVbiCapt = vbi_capture_v4l_new(p_dev_name, 0, p_services, opt_strict, &pErr, opt_debug_level);
#endif
   if (opt_api == TEST_API_PROXY)
   {
      pProxyClient = vbi_proxy_client_create(p_dev_name, "proxy-test", 0,
                                             &pErr, opt_debug_level);
      if (pProxyClient != NULL)
      {
         pVbiCapt = vbi_capture_proxy_new(pProxyClient, BUFFER_COUNT, 0, p_services, opt_strict, &pErr );
      }
      vbi_proxy_client_set_callback(pProxyClient, ProxyEventCallback, &pProxyClient);
   }

   if (pVbiCapt != NULL)
   {
      lastLineCount = -1;

      /* switch to the requested channel */
      if ( (opt_channel != -1) || (opt_frequency != -1) ||
           (opt_chnprio != VBI_CHN_PRIO_INTERACTIVE) )
      {
         vbi_channel_profile  chn_profile;

         memset(&chn_profile, 0, sizeof(chn_profile));

         if ( (opt_chnprio == VBI_CHN_PRIO_BACKGROUND) &&
              ((opt_channel != -1) || (opt_frequency != -1)) )
         {
            chn_profile.is_valid      = TRUE;
            chn_profile.sub_prio      = opt_subprio;
            chn_profile.min_duration  = 10;
         }
         vbi_proxy_client_channel_request(pProxyClient, opt_chnprio, &chn_profile);

         if (opt_chnprio != VBI_CHN_PRIO_BACKGROUND) {
            SwitchTvChannel(pProxyClient, opt_channel, opt_frequency);
         }
      }

      if ((opt_services & (VBI_SLICED_VBI_625 | VBI_SLICED_VBI_525)) != 0)
      {
         vbi_raw_decoder * p_dec;

         /* initialize services for raw capture */
         p_dec = vbi_capture_parameters(pVbiCapt);
         if (p_dec != NULL)
         {
            vbi_raw_decoder_init(&raw_dec);

            raw_dec.scanning = p_dec->scanning; 
            raw_dec.sampling_format = p_dec->sampling_format; 
            raw_dec.sampling_rate = p_dec->sampling_rate; 
            raw_dec.bytes_per_line = p_dec->bytes_per_line; 
            raw_dec.offset = p_dec->offset; 
            raw_dec.start[0] = p_dec->start[0]; 
            raw_dec.start[1] = p_dec->start[1]; 
            raw_dec.count[0] = p_dec->count[0]; 
            raw_dec.count[1] = p_dec->count[1]; 
            raw_dec.interlaced = p_dec->interlaced; 
            raw_dec.synchronous = p_dec->synchronous; 

            vbi_raw_decoder_add_services(&raw_dec, ALL_SERVICES_525|ALL_SERVICES_625, 0);
         }
      }

      update_services = (opt_scanning != 0);

      while(1)
      {
         fd_set rd;
         int vbi_fd = vbi_capture_fd(pVbiCapt);
         if (vbi_fd == -1)
            break;

         FD_ZERO(&rd);
         FD_SET(vbi_fd, &rd);
         FD_SET(0, &rd);
         select(vbi_fd + 1, &rd, NULL, NULL, NULL);

         if (FD_ISSET(0, &rd))
         {
            new_services = read_service_string();
            if (opt_scanning == TEST_SCANNING_625)
               new_services &= ALL_SERVICES_625;
            else if (opt_scanning == TEST_SCANNING_525)
               new_services &= ALL_SERVICES_525;
            if (new_services != opt_services)
            {
               fprintf(stderr, "switching service from 0x%X to 0x%X...\n", opt_services, new_services);
               opt_services = new_services;
               update_services = TRUE;
            }
         }
         if (update_services)
         {
            cur_services = vbi_capture_update_services(pVbiCapt, TRUE, TRUE,
                                                       opt_services, opt_strict, &pErr);
            if ((cur_services != 0) || (opt_services == 0))
               fprintf(stderr, "...got granted services 0x%X.\n", cur_services);
            else
               fprintf(stderr, "...failed: %s\n", ((pErr != NULL) ? pErr : ""));
            lastLineCount = 0;
            update_services = FALSE;
         }

         if (FD_ISSET(vbi_fd, &rd))
         {
            timeout.tv_sec  = 0;
            timeout.tv_usec = 1000;

            if ((opt_services & (VBI_SLICED_VBI_625 | VBI_SLICED_VBI_525)) == 0)
            {
               res = vbi_capture_pull_sliced(pVbiCapt, &pVbiBuf, &timeout);
               if (res < 0)
               {
                  fprintf(stderr, "VBI read error: %d (%s)\n", errno, strerror(errno));
                  break;
               }
               else if ((res > 0) && (pVbiBuf != NULL))
               {
                  lineCount = ((unsigned int) pVbiBuf->size) / sizeof(vbi_sliced);
                  pVbiData  = pVbiBuf->data;

                  if (lastLineCount != lineCount)
                  {
                     fprintf(stderr, "%d lines\n", lineCount);
                     lastLineCount = lineCount;
                  }

                  for (line=0; line < lineCount; line++)
                  {
                     if ((pVbiData[line].id & (VBI_SLICED_TELETEXT_B | VBI_SLICED_TELETEXT_BD_525)) != 0)
                     {
                        PrintTeletextData(pVbiData[line].data, pVbiData[line].line, pVbiData[line].id);
                     }
                     else if (pVbiData[line].id == VBI_SLICED_VPS)
                     {
                        PrintVpsData(pVbiData[line].data);
                     }
                     else if (pVbiData[line].id == VBI_SLICED_WSS_625)
                     {
                        printf("WSS 0x%02X%02X%02X\n", pVbiData[line].data[0], pVbiData[line].data[1], pVbiData[line].data[2]);
                     }
                  }
               }
               else
                  fprintf(stderr, "proxy-test: timeout in VBI read\n");
            }
            else
            {
               res = vbi_capture_pull_raw(pVbiCapt, &pVbiBuf, &timeout);
               if ((res < 0) && (errno != EAGAIN))
               {
                  fprintf(stderr, "VBI read error: %d (%s)\n", errno, strerror(errno));
                  break;
               }
               else if ((res > 0) && (pVbiBuf != NULL))
               {
#ifdef USE_LIBZVBI
                  pVbiData = malloc(32 * sizeof(vbi_sliced));
                  lineCount = vbi_raw_decode(&raw_dec, pVbiBuf->data, pVbiData);

                  if (lastLineCount != lineCount)
                  {
                     fprintf(stderr, "%d lines\n", lineCount);
                     lastLineCount = lineCount;
                  }

                  for (line=0; line < lineCount; line++)
                  {
                     if ((pVbiData[line].id & (VBI_SLICED_TELETEXT_B|VBI_SLICED_TELETEXT_BD_525)) != 0)
                     {
                        PrintTeletextData(pVbiData[line].data, pVbiData[line].line, pVbiData[line].id);
                     }
                     else if (pVbiData[line].id == VBI_SLICED_VPS)
                     {
                        PrintVpsData(pVbiData[line].data);
                     }
                     else if (pVbiData[line].id == VBI_SLICED_WSS_625)
                     {
                        printf("WSS 0x%02X%02X%02X\n", pVbiData[line].data[0], pVbiData[line].data[1], pVbiData[line].data[2]);
                     }
                  }
                  free(pVbiData);
#endif  /* USE_LIBZVBI */
               }
               else if (opt_debug_level > 0)
               {
                  fprintf(stderr, "VBI read timeout\n");
               }
            }
         }
      }

      vbi_capture_delete(pVbiCapt);
   }
   else
   {
      if (pErr != NULL)
      {
         fprintf(stderr, "libzvbi error: %s\n", pErr);
         free(pErr);
      }
      else
         fprintf(stderr, "error starting acquisition\n");
   }
   if (pProxyClient != NULL)
   {
      vbi_proxy_client_destroy(pProxyClient);
      pProxyClient = NULL;
   }
   exit(0);
   return(0);
}

