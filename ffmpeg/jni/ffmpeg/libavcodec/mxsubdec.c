/*
 * mx subtitle decoder
 * Copyright (c) 2019  Zheng Lin <zheng.lin@mxplayer.in>
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

#ifdef MXTECHS
#include "avcodec.h"
static int put_text_rect(AVSubtitle *sub, const char *text,
                         size_t length, const AVCodecContext *avctx, const AVPacket *avpkt)
{
    AVSubtitleRect **rects = (AVSubtitleRect**)av_malloc(sizeof(AVSubtitleRect*));
    AVSubtitleRect *rect = (AVSubtitleRect*)av_mallocz( sizeof(AVSubtitleRect) );
    char *dest = (char*)av_malloc(length + 1);
    if(!rects || !rect || !dest) {
        av_free(dest);
        av_free(rect);
        av_free(rects);
        return AVERROR(ENOMEM);
    }

    memcpy(dest, text, length);
    dest[length] = '\0';

    rect->type = SUBTITLE_TEXT;
    rect->text = dest;
    sub->format = 1;	// text/ass
    // The use of XXX AVCodecContext.time_base has been deprecated, but it is used in libavcodec/textdec.c and will remain for the time being.
    sub->end_display_time = (uint32_t)(avpkt->duration > 0 ? av_rescale_q(avpkt->duration, avctx->time_base, (AVRational){1,1000}) : 0);
    sub->num_rects = 1;
    sub->rects = rects;
    sub->rects[0] = rect;
    return 0;
}

static int mx_decode_frame(AVCodecContext *avctx,
                        void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    const char *text = (const char*)avpkt->data;
    if (!text) {
        return AVERROR_INVALIDDATA;
    }

    size_t length = strlen(text);
    if ( length > 0 ) {
        int ret = put_text_rect((AVSubtitle*)data, text, length, avctx, avpkt);
        if ( ret < 0 ) {
            return ret;
        }
        *got_sub_ptr = 1;
    }

    return avpkt->size;
}
#endif