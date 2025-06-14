/*
 * Copyright (c) 2020 zheng.lin <zheng.lin@mxplayer.in>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"
#include "url.h"

#include "usb_wrap.h"

static av_cold int usb_wrapper_open(URLContext *h, const char *url, int flags)
{
    return usb_open(h, url, flags);
}

static int64_t usb_wrapper_seek(URLContext *h, int64_t pos, int whence)
{
    return usb_seek(h, pos, whence);
}

static int usb_wrapper_read(URLContext *h, unsigned char *buf, int size)
{
    return usb_read(h, buf, size);
}

static int usb_wrapper_write(URLContext *h, const unsigned char *buf, int size)
{
    return usb_write(h, buf, size);
}

static int usb_wrapper_delete(URLContext *h)
{
    return usb_delete(h);
}

static int usb_wrapper_move(URLContext *h_src, URLContext *h_dst)
{
    return usb_move(h_src, h_dst);
}

static av_cold int usb_wrapper_close(URLContext *h)
{
    return usb_close(h);
}

static int usb_wrapper_open_dir(URLContext *h)
{
    return usb_open_dir(h);
}

static int usb_wrapper_read_dir(URLContext *h, AVIODirEntry **next)
{
    return usb_read_dir(h, (void**)next);
}

static int usb_wrapper_close_dir(URLContext *h)
{
    return usb_close_dir(h);
}


const URLProtocol ff_usb_protocol = {
    .name                = "usb",
    .url_open            = usb_wrapper_open,
    .url_read            = usb_wrapper_read,
    .url_write           = usb_wrapper_write,
    .url_seek            = usb_wrapper_seek,
    .url_close           = usb_wrapper_close,
    .url_delete          = usb_wrapper_delete,
    .url_move            = usb_wrapper_move,
    .url_open_dir        = usb_wrapper_open_dir,
    .url_read_dir        = usb_wrapper_read_dir,
    .url_close_dir       = usb_wrapper_close_dir,
    .priv_data_size      = 1024,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
