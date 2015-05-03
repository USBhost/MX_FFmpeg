/*
 *  libzvbi test
 *
 *  Copyright (C) 2000-2002, 2004 Michael H. Schimek
 *  Copyright (C) 2003 James Mastros
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

/* $Id: osc.c,v 1.35 2008/03/01 07:36:41 mschimek Exp $ */

#undef NDEBUG

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

#include "src/io.h"
#include "src/decoder.h"
#include "src/misc.h"
#include "src/hamm.h"
#include "src/io-sim.h"
#include "src/raw_decoder.h"	/* _vbi_service_table[] */
#include "src/proxy-msg.h"
#include "src/proxy-client.h"

#ifndef X_DISPLAY_MISSING

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

vbi_capture *		cap;
vbi_raw_decoder *	par;
vbi_proxy_client *      pxc = NULL;
int			src_w, src_h;
vbi_sliced *		sliced;
int			slines;
vbi_bool		quit;

int			do_sim;
int			ignore_error;
int			desync;

Display *		display;
int			screen;
Colormap		cmap;
Window			window;
int			dst_w, dst_h;
GC			gc;
XEvent			event;
XImage *		ximage;
void *			ximgdata;
unsigned char 		*raw1, *raw2;
int			palette[256];
int			depth;
int			draw_row, draw_offset;
int			draw_count = -1;
int                     cur_x, cur_y;

extern void
vbi_capture_set_log_fp		(vbi_capture *		capture,
				 FILE *			fp);
extern vbi_bool vbi_capture_force_read_mode;

#define PIL(day, mon, hour, min) \
	(((day) << 15) + ((mon) << 11) + ((hour) << 6) + ((min) << 0))

/* Return value must be free()d by caller! */
static char *
decode_ttx(uint8_t *buf, int line)
{
        char *text, *text_start;
	int packet_address;
	int magazine, packet;
	int j;

        text_start = text = malloc(255);
        memset(text, 0, 255);
	packet_address = vbi_unham16p (buf + 0);

	if (packet_address < 0)
		return text; /* hamming error */

	magazine = packet_address & 7;
	packet = packet_address >> 3;

        text += snprintf(text, 255,
			 "pg %x%02d ln %03d >", magazine, packet, line);

        for (j = 0; j < 42; j++) {
	   char c = _vbi_to_ascii (buf[j]);
	   
	   *text = c;
	   text++;
	}

        *text='<';
        *text=0;

        return text_start;
}

static char *
dump_pil(int pil)
{
	int day, mon, hour, min;
        static char text[255];
   
        memset(text, 0, 255);

	day = pil >> 15;
	mon = (pil >> 11) & 0xF;
	hour = (pil >> 6) & 0x1F;
	min = pil & 0x3F;

	if (pil == PIL(0, 15, 31, 63))
		snprintf(text, 255, " PDC: Timer-control (no PDC)\n");
	else if (pil == PIL(0, 15, 30, 63))
		snprintf(text, 255, " PDC: Recording inhibit/terminate\n");
	else if (pil == PIL(0, 15, 29, 63))
		snprintf(text, 255, " PDC: Interruption\n");
	else if (pil == PIL(0, 15, 28, 63))
		snprintf(text, 255, " PDC: Continue\n");
	else if (pil == PIL(31, 15, 31, 63))
		snprintf(text, 255, " PDC: No time\n");
	else
		snprintf(text, 255, " PDC: %05x, 200X-%02d-%02d %02d:%02d\n",
			pil, mon, day, hour, min);
        return text;
}

static char *
decode_vps(uint8_t *buf)
{
        char *text, *text_start;
	static char pr_label[20];
	static char label[20];
	static int l = 0;
	int cni, pcs, pty, pil;
	int c;

        text_start=text=malloc(255);
        memset(text, 0, 255);
        
	text += snprintf(text, 255, "VPS: ");

	c = vbi_rev8 (buf[1]);

	if ((int8_t) c < 0) {
		label[l] = 0;
		memcpy(pr_label, label, sizeof(pr_label));
		l = 0;
	}

	c &= 0x7F;

	label[l] = _vbi_to_ascii (c);

	l = (l + 1) % 16;

	text += snprintf(text, 250, " 3-10: %02x %02x %02x %02x %02x %02x %02x %02x (\"%s\")\n",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], pr_label);

	pcs = buf[2] >> 6;

	cni = + ((buf[10] & 3) << 10)
	      + ((buf[11] & 0xC0) << 2)
	      + ((buf[8] & 0xC0) << 0)
	      + (buf[11] & 0x3F);

	pil = ((buf[8] & 0x3F) << 14) + (buf[9] << 6) + (buf[10] >> 2);

	pty = buf[12];

	/* FIXME use real buffer size. */
	text += snprintf(text, 100, " CNI: %04x PCS: %d PTY: %d ", cni, pcs, pty);

	text += snprintf(text, 50, " %s", dump_pil(pil));
   
        return(text_start);
}


/* End from capture.c */

static void
draw(unsigned char *raw)
{
	int rem = src_w - draw_offset;
	char buf[256];
	unsigned char *data = raw;
	int i, v, h0, field, end, line;
        XTextItem xti;
	int x;

	if (draw_count == 0)
		return;

	if (draw_count > 0)
		draw_count--;

	memcpy(raw2, raw, src_w * src_h);

	if (depth == 24) {
		unsigned int *p = ximgdata;
		
		for (i = src_w * src_h; i >= 0; i--)
			*p++ = palette[(int) *data++];
	} else {
		unsigned short *p = ximgdata; // 64 bit safe?

		for (i = src_w * src_h; i >= 0; i--)
			*p++ = palette[(int) *data++];
	}

	XPutImage(display, window, gc, ximage,
		draw_offset, 0, 0, 0, rem, src_h);
//XSync(display, False);

	XSetForeground(display, gc, 0);
//XSync(display, False);

	if (rem < dst_w) {
//		fprintf(stderr, "%u: %p %u %u  %u %u %u %u\n",__LINE__,
//			display, window, gc,
//			rem, 0, dst_w, src_h);
		XFillRectangle(display, window, gc,
			rem, 0, dst_w, src_h);
//XSync(display, False);
	}

	if ((v = dst_h - src_h) <= 0)
		return;

	XSetForeground(display, gc, 0);
//XSync(display, False);
//		fprintf(stderr, "%u: %p %u %u  %u %u %u %u\n",__LINE__,
//display, window, gc,0, src_h, dst_w, dst_h);
	XFillRectangle(display, window, gc,
		0, src_h, dst_w, dst_h);
//XSync(display, False);

	XSetForeground(display, gc, ~0);
//XSync(display, False);

	field = (draw_row >= par->count[0]);

	if (par->start[field] < 0) {
		xti.nchars = snprintf(buf, 255, "Row %d Line ?", draw_row);
		line = -1;
	} else if (field == 0) {
		line = draw_row + par->start[0];
		xti.nchars = snprintf(buf, 255, "Row %d Line %d", draw_row, line);
	} else {
		line = draw_row - par->count[0] + par->start[1];
		xti.nchars = snprintf(buf, 255, "Row %d Line %d", draw_row, line);
	}

	for (i = 0; i < slines; i++)
		if (sliced[i].line == (unsigned int) line)
			break;
	if (i < slines) {
	   int svc_idx=0;
	   while (_vbi_service_table[svc_idx].id !=0 && 
		  _vbi_service_table[svc_idx].id != sliced[i].id)
	     svc_idx++;
	   
	   if (_vbi_service_table[svc_idx].id == sliced[i].id) {
	      struct _vbi_service_par service;
	      service = _vbi_service_table[svc_idx];
	      
	      xti.nchars += snprintf(buf + xti.nchars, 255 - xti.nchars,
				     " %s (%x) +%dns",
				     service.label,
				     service.id,
				     service.offset
				    );
	      if (service.id & VBI_SLICED_TELETEXT_B) {
		 char *text = decode_ttx(sliced[i].data, sliced[i].line);
		 xti.nchars += snprintf(buf + xti.nchars, 255 - xti.nchars,
					": %s", text);
		 free(text);
	      } else if (service.id & VBI_SLICED_VPS) {
		 char *text = decode_vps(sliced[i].data);
		 xti.nchars += snprintf(buf + xti.nchars, 255 - xti.nchars,
					": %s", text);
		 free(text);
	      }
	   } else {
	      xti.nchars += snprintf(buf + xti.nchars, 255 - xti.nchars,
				     " %s (%d)", 
				     vbi_sliced_name(sliced[i].id) ?: "???", 
				     sliced[i].id);
	   }
	} else {
		int s = 0, sd = 0;

		data = raw + draw_row * src_w;

		for (i = 0; i < src_w; i++)
			s += data[i];
		s /= src_w;

		for (i = 0; i < src_w; i++)
			sd += abs(data[i] - s);

		sd /= src_w;

		xti.nchars += snprintf(buf + xti.nchars, 255 - xti.nchars,
				       (sd < 5) ? " Blank" : " Unknown signal");
	        xti.nchars += snprintf(buf + xti.nchars, 255 - xti.nchars,
				       " (%d)", sd);
	}

/*
        XSetForeground(display, gc, 0x00FFFF00);
        XFillRectangle(display, window, gc,
		       0xc0-draw_offset, 
		       src_h, 1, dst_h);
        XFillRectangle(display, window, gc,
		       0x19b-draw_offset,
		       src_h, 1, dst_h);
*/
        /* 50% grey */
        XSetForeground(display, gc, 0xAAAAAAAA);  
//XSync(display, False);
        x=draw_offset;
        while (x<src_w && (x-draw_offset)<dst_w) {
//		fprintf(stderr, "%u: %p %u %u  %u %u %u %u\n",__LINE__,
//display, window, gc,
//			  x-draw_offset, /* x,y, w,h */
//			  src_h, 1, dst_h);
	   XFillRectangle(display, window, gc,
			  x-draw_offset, /* x,y, w,h */
			  src_h, 1, dst_h);
//XSync(display, False);
	   x+=10;
	}
        XSetForeground(display, gc, ~0);
//XSync(display, False);

        xti.chars = buf;
	xti.delta = 0;
	xti.font = 0;

	XDrawText(display, window, gc, 4, src_h + 12, &xti, 1);
//XSync(display, False);
        xti.nchars = snprintf(buf, 255, "(%d, %3d)", cur_x+draw_offset,
	  (dst_h - cur_y) * 256 / v);
//	  (1000*(dst_h-cur_y))/(dst_h-src_h));
        XDrawText(display, window, gc, 4, src_h + 24, &xti, 1);
//XSync(display, False);

	data = raw + draw_offset + draw_row * src_w;
	h0 = dst_h - (data[0] * v) / 256;
	end = src_w - draw_offset;
	if (dst_w < end)
		end = dst_w;

	for (i = 1; i < end; i++) {
		int h = dst_h - (data[i] * v) / 256;

		XDrawLine(display, window, gc, i - 1, h0, i, h);
//XSync(display, False);
		h0 = h;
	}
}

static void
xevent(void)
{
	while (XPending(display)) {
		XNextEvent(display, &event);

		switch (event.type) {
		case KeyPress:
		{
			switch (XLookupKeysym(&event.xkey, 0)) {
			case 'g':
				draw_count = 1;
				break;

			case 'l':
				draw_count = -1;
				break;

			case 'q':
			case 'c':
			case XK_Escape:
				quit = TRUE;
				break;

			case XK_Up:
			    if (draw_row > 0)
				    draw_row--;
			    goto redraw;

			case XK_Down:
			    if (draw_row < (src_h - 1))
				    draw_row++;
			    goto redraw;

			case XK_Left:
			    if (draw_offset > 0)
				    draw_offset -= 10;
			    goto redraw;

			case XK_Right:
			    if (draw_offset < (src_w - 10))
				    draw_offset += 10;
			    goto redraw;  
			}

			break;
		}

		case ConfigureNotify:
			dst_w = event.xconfigurerequest.width;
			dst_h = event.xconfigurerequest.height;
redraw:
			if (draw_count == 0) {
				draw_count = 1;
				draw(raw2);
			}

			break;

		case MotionNotify:
		       cur_x = event.xmotion.x;
		       cur_y = event.xmotion.y;
		       // printf("Got MotionNotify: (%d, %d)\n", event.xmotion.x, event.xmotion.y);
		       break;
		   
		case ClientMessage:
			exit(EXIT_SUCCESS);
		}
	}
}

static void
init_window(int ac, char **av, const char *dev_name)
{
	char buf[256];
	Atom delete_window_atom;
	XWindowAttributes wa;
	int i;

	ac = ac;
	av = av;

	if (!(display = XOpenDisplay(NULL))) {
		fprintf(stderr, "No display\n");
		exit(EXIT_FAILURE);
	}

	screen = DefaultScreen(display);
	cmap = DefaultColormap(display, screen);
 
	window = XCreateSimpleWindow(display,
		RootWindow(display, screen),
		0, 0,		// x, y
		dst_w = 768, dst_h = src_h + 110,
				// w, h
		2,		// borderwidth
		0xffffffff,	// fgd
		0x00000000);	// bgd 

	if (!window) {
		fprintf(stderr, "No window\n");
		exit(EXIT_FAILURE);
	}
			
	XGetWindowAttributes(display, window, &wa);
	depth = wa.depth;
			
	if (depth != 15 && depth != 16 && depth != 24) {
		fprintf(stderr, "Sorry, cannot run at colour depth %d\n", depth);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < 256; i++) {
		switch (depth) {
		case 15:
			palette[i] = ((i & 0xF8) << 7)
				   + ((i & 0xF8) << 2)
				   + ((i & 0xF8) >> 3);
				break;

		case 16:
			palette[i] = ((i & 0xF8) << 8)
				   + ((i & 0xFC) << 3)
				   + ((i & 0xF8) >> 3);
				break;

		case 24:
			palette[i] = (i << 16) + (i << 8) + i;
				break;
		}
	}

	if (depth == 24) {
		if (!(ximgdata = malloc(src_w * src_h * 4))) {
			fprintf(stderr, "Virtual memory exhausted\n");
			exit(EXIT_FAILURE);
		}
	} else {
		if (!(ximgdata = malloc(src_w * src_h * 2))) {
			fprintf(stderr, "Virtual memory exhausted\n");
			exit(EXIT_FAILURE);
		}
	}

	if (!(raw1 = malloc(src_w * src_h))) {
		fprintf(stderr, "Virtual memory exhausted\n");
		exit(EXIT_FAILURE);
	}

	if (!(raw2 = malloc(src_w * src_h))) {
		fprintf(stderr, "Virtual memory exhausted\n");
		exit(EXIT_FAILURE);
	}

	ximage = XCreateImage(display,
		DefaultVisual(display, screen),
		DefaultDepth(display, screen),
		ZPixmap, 0, (char *) ximgdata,
		src_w, src_h,
		8, 0);

	if (!ximage) {
		fprintf(stderr, "No ximage\n");
		exit(EXIT_FAILURE);
	}

	delete_window_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);

	XSelectInput(display, window, PointerMotionMask | KeyPressMask | ExposureMask | StructureNotifyMask);
	XSetWMProtocols(display, window, &delete_window_atom, 1);
	snprintf(buf, sizeof(buf) - 1, "%s - [cursor] [g]rab [l]ive", dev_name);
	XStoreName(display, window, buf);

	gc = XCreateGC(display, window, 0, NULL);

	XMapWindow(display, window);
	       
	XSync(display, False);
}

static void
mainloop(void)
{
	double timestamp;
	struct timeval tv;

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	assert((sliced = malloc(sizeof(vbi_sliced) * src_h)));

	for (quit = FALSE; !quit;) {
		int r;

		r = vbi_capture_read(cap, raw1, sliced,
				     &slines, &timestamp, &tv);

		switch (r) {
		case -1:
			fprintf(stderr, "VBI read error: %d, %s%s\n",
				errno, strerror(errno),
				ignore_error ? " (ignored)" : "");
			if (ignore_error)
				continue;
			else
				exit(EXIT_FAILURE);
		case 0: 
			fprintf(stderr, "VBI read timeout%s\n",
				ignore_error ? " (ignored)" : "");
			if (ignore_error || (pxc != NULL))
				continue;
			else
				exit(EXIT_FAILURE);
		case 1:
			break;
		default:
			assert(!"reached");
		}

		draw(raw1);

/*		printf("raw: %f; sliced: %d\n", timestamp, slines); */

		xevent();
	}
}

static const char short_options[] = "1234cd:enpsv";

#ifdef HAVE_GETOPT_LONG
static const struct option
long_options[] = {
	{ "desync",	no_argument,		NULL,		'c' },
	{ "device",	required_argument,	NULL,		'd' },
	{ "ignore-error", no_argument,		NULL,		'e' },
	{ "ntsc",	no_argument,		NULL,		'n' },
	{ "pal",	no_argument,		NULL,		'p' },
	{ "sim",	no_argument,		NULL,		's' },
	{ "v4l",	no_argument,		NULL,		'1' },
	{ "v4l2-read",	no_argument,		NULL,		'2' },
	{ "v4l2-mmap",	no_argument,		NULL,		'3' },
	{ "proxy",	no_argument,		NULL,		'4' },
	{ "verbose",	no_argument,		NULL,		'v' },
	{ 0, 0, 0, 0 }
};
#else
#define getopt_long(ac, av, s, l, i) getopt(ac, av, s)
#endif

int
main(int argc, char **argv)
{
	const char *dev_name = "/dev/vbi";
	char *errstr;
	unsigned int services;
	int scanning = 625;
	int strict;
	int verbose = 0;
	int interface = 0;
	int c, index;

	while ((c = getopt_long(argc, argv, short_options,
				long_options, &index)) != -1)
		switch (c) {
		case 0: /* set flag */
			break;
		case '2':
			/* Preliminary hack for tests. */
			vbi_capture_force_read_mode = TRUE;
			/* fall through */
		case '1':
		case '3':
		case '4':
			interface = c - '0';
			break;
		case 'c':
			desync ^= TRUE;
			break;
		case 'd':
			dev_name = strdup (optarg);
			break;
		case 'e':
			ignore_error ^= TRUE;
			break;
		case 'n':
			scanning = 525;
			break;
		case 'p':
			scanning = 625;
			break;
		case 's':
			do_sim ^= TRUE;
			break;
		case 'v':
			++verbose;
			break;
		default:
			fprintf(stderr, "Unknown option\n");
			exit(EXIT_FAILURE);
		}

	services = VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625
		| VBI_SLICED_TELETEXT_B | VBI_SLICED_CAPTION_525
		| VBI_SLICED_CAPTION_625 | VBI_SLICED_VPS
		| VBI_SLICED_WSS_625 | VBI_SLICED_WSS_CPR1204;

	strict = 0;

	if (do_sim) {
		cap = vbi_capture_sim_new (scanning, &services,
					   /* interlaced */ FALSE, !desync);
		assert ((par = vbi_capture_parameters(cap)));
	} else {
		do {
			if ((2 == interface) || (3 == interface)) {
				cap = vbi_capture_v4l2k_new
					(dev_name, /* fd */ -1,
					 /* buffers */ 5, &services,
					 strict, &errstr,
					 /* trace */ !!verbose);

				if (cap)
					break;

				fprintf (stderr, "Cannot capture vbi data "
					 "with v4l2k interface:\n%s\n",
					 errstr);

				free (errstr);

				cap = vbi_capture_v4l2_new (dev_name,
							    /* buffers */ 5,
							    &services,
							    strict,
							    &errstr,
							    /* trace */
							    !!verbose);
				if (cap)
					break;

				fprintf (stderr, "Cannot capture vbi data "
					 "with v4l2 interface:\n%s\n", errstr);

				free (errstr);
			}

			if (interface < 2) {
				cap = vbi_capture_v4l_new (dev_name,
							   scanning,
							   &services,
							   strict,
							   &errstr,
							   /* trace */
							   !!verbose);
				if (cap)
					break;

				fprintf (stderr, "Cannot capture vbi data "
					 "with v4l interface:\n%s\n", errstr);

				free (errstr);
			}

			if (interface == 4) {
                                pxc = vbi_proxy_client_create(dev_name, "capture", 0,
                                                                &errstr, !!verbose);
                                if (pxc != NULL) {
                                        /* strip non-raw services, else request for raw is masked out */
                                        unsigned int sv = services & (VBI_SLICED_VBI_525 |
                                                                      VBI_SLICED_VBI_625);
                                        cap = vbi_capture_proxy_new(pxc, 5, 0, &sv,
                                                                    strict, &errstr );
                                        if (cap)
                                                break;

                                        fprintf (stderr, "Cannot capture vbi data "
                                                 "through proxy:\n%s\n", errstr);
                                }
				fprintf (stderr, "Cannot initialize proxy\n%s\n", errstr);
                        }

			/* BSD interface */
			if (1) {
				cap = vbi_capture_bktr_new (dev_name,
							    scanning,
							    &services,
							    strict,
							    &errstr,
							    /* trace */
							    !!verbose);
				if (cap)
					break;

				fprintf (stderr, "Cannot capture vbi data "
					 "with bktr interface:\n%s\n", errstr);

				free (errstr);
			}

			exit(EXIT_FAILURE);
		} while (0);

		assert ((par = vbi_capture_parameters(cap)));
	}

	if (verbose > 1) {
		vbi_capture_set_log_fp (cap, stderr);
	}

	assert (par->sampling_format == VBI_PIXFMT_YUV420);

	src_w = par->bytes_per_line / 1;
	src_h = par->count[0] + par->count[1];

	init_window(argc, argv, dev_name);

	mainloop();

	if (!do_sim)
		vbi_capture_delete(cap);

	exit(EXIT_SUCCESS);	
}


#else /* X_DISPLAY_MISSING */

int
main(int argc, char **argv)
{
	printf("Could not find X11 or has been disabled at configuration time\n");
	exit(EXIT_FAILURE);
}

#endif
