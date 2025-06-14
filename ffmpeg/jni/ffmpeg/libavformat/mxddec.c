/*
 * MX Dynamic Adaptive Streaming over Http demuxer
 * Copyright (c) 2020 zheng.lin@mxplayer.in 
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
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/parseutils.h"
#include "internal.h"
#include "avio_internal.h"

#include "mxd_wrap.h"
static int mxd_wrapper_read_header(AVFormatContext *s)
{
   return mxd_read_header(s);
}

static int mxd_wrapper_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return mxd_read_packet(s, pkt);
}

static int mxd_wrapper_read_close(AVFormatContext *s)
{
    return mxd_read_close(s);
}

static int mxd_wrapper_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    return mxd_read_seek(s, stream_index, timestamp, flags);
}

static int mxd_wrapper_read_probe(const AVProbeData *p)
{
    return mxd_read_probe(p);
}

#define OFFSET(x) offsetof(MXDASHContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM
static const AVOption mxd_options[] = {
    {NULL}
};

static const AVClass mxd_class = {
    .class_name = "mxd",
    .item_name  = av_default_item_name,
    .option     = mxd_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_mxd_demuxer = {
    .name           = "mxd",
    .long_name      = NULL_IF_CONFIG_SMALL("VMD (VM DASH Format)"),
    .flags          = AVFMT_SEEK_TO_PTS,
    .priv_class     = &mxd_class,
    .priv_data_size = 10240,
    .read_probe     = mxd_wrapper_read_probe,
    .read_header    = mxd_wrapper_read_header,
    .read_packet    = mxd_wrapper_read_packet,
    .read_close     = mxd_wrapper_read_close,
    .read_seek      = mxd_wrapper_read_seek,
};
