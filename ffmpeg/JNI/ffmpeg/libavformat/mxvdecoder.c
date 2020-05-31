//
//  mxvdecoder.c
//  FFmpeg-xcode
//
//  Created by Wang Shaoqi on 2020/2/3.
//  Copyright © 2020 Wang Shaoqi. All rights reserved.
//

#include "avformat.h"
#include "internal.h"
#include "mxv_wrap.h"

static int wraper_mxv_probe(const AVProbeData *p)
{
    return mxv_probe(p);
}
static int wraper_mxv_read_header(AVFormatContext *s)
{
    return mxv_read_header(s);
}

static int wraper_mxv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return mxv_read_packet(s, pkt);
}

static int wraper_mxv_read_close(AVFormatContext *s)
{
    return mxv_read_close(s);
}

static int wraper_mxv_read_seek(AVFormatContext *s, int stream_index,
                              int64_t timestamp, int flags)
{
    return mxv_read_seek(s, stream_index, timestamp, flags);
}

AVInputFormat ff_mxv_demuxer = {
    .name           = "mxv",
    .long_name      = NULL_IF_CONFIG_SMALL("MXV Container"),
    .extensions     = "mxv",
    .priv_data_size = 10240,
    .read_probe     = wraper_mxv_probe,
    .read_header    = wraper_mxv_read_header,
    .read_packet    = wraper_mxv_read_packet,
    .read_close     = wraper_mxv_read_close,
    .read_seek      = wraper_mxv_read_seek,
    .mime_type      = "audio/x-mxv,video/x-mxv"
};
