/*
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

#include "libavformat/http.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "id3v2.h"

typedef struct PLSEntry {
    char url[MAX_URL_SIZE];
    char title[MAX_URL_SIZE];
    int length;
} PLSEntry;

typedef struct PLSContext {
    AVClass *class;
    int version;
    int numOfEntries;
    PLSEntry entry;
    AVFormatContext *ctx;
} PLSContext;

static int ff_get_chomp_line(AVIOContext *s, char *buf, int maxlen)
{
    int len = ff_get_line(s, buf, maxlen);
    while (len > 0 && av_isspace(buf[len - 1]))
        buf[--len] = '\0';
    return len;
}

static int parse_playlist(PLSContext *c, AVIOContext *in)
{
    int ret = 0;
    const char *ptr;
    char line[MAX_URL_SIZE];
    ff_get_chomp_line(in, line, sizeof(line));
    if (strcmp(line, "[playlist]")) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    while (!avio_feof(in)) {
        ff_get_chomp_line(in, line, sizeof(line));
        if (av_strstart(line, "Version=", &ptr)){
            if (ptr != NULL ) {
                c->version = (int)strtol(ptr, NULL, 10);
            } else {
                c->version = 2;
            }
        } else if (av_strstart(line, "NumberOfEntries=", &ptr)){
            if (ptr != NULL ) {
                c->numOfEntries = (int)strtol(ptr, NULL, 10);
            } else {
                c->numOfEntries = 1;
            }
        } else if (av_strstart(line, "File", &ptr)) {
            if (ptr != NULL) {
                if (ptr = strchr(ptr, '=')) {
                    strncpy(c->entry.url, ptr + 1, sizeof(c->entry.url));
                }
            }
        } else if (av_strstart(line, "Title", &ptr)) {
            if (ptr != NULL) {
                if (!(ptr = strchr(ptr, '='))) {
                    strncpy(c->entry.title, ptr + 1, sizeof(c->entry.title));
                }
            }
        } else if (av_strstart(line, "Length", &ptr)) {
            if (ptr != NULL) {
                if (!(ptr = strchr(ptr, '='))) {
                    c->entry.length = (int)strtol(ptr, NULL, 10);
                }
            }
        }
    }
fail:
    return ret;
}

static int pls_read_close(AVFormatContext *s)
{
    PLSContext *c = s->priv_data;
    if (c->ctx) {
        avformat_close_input(&c->ctx);
    }
    return 0;
}

static int pls_read_header(AVFormatContext *s)
{
    PLSContext *c = s->priv_data;
    int ret = parse_playlist(c, s->pb);
    if (ret >= 0 && strlen(c->entry.url) > 0) {

        c->ctx = avformat_alloc_context();
        if (!c->ctx) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        c->ctx->interrupt_callback = s->interrupt_callback;

        ret = ff_copy_whiteblacklists(c->ctx, s);
        if (ret < 0) {
            goto fail;
        }

        //attempt to open inner url
        ret = avformat_open_input(&c->ctx, c->entry.url, NULL, NULL );
        if (ret < 0) {
            av_log(c->ctx, AV_LOG_ERROR, "Failed to open %s due to \'%s\'.\n", c->entry.url, av_err2str(ret));
            goto fail;
        }

        ret = avformat_find_stream_info(c->ctx, NULL);
        if (ret < 0) {
            goto fail;
        }

        s->bit_rate = c->ctx->bit_rate;

        //copy codec parameters
        for (int i = 0; i < c->ctx->nb_streams; i++) {
            AVStream* out = avformat_new_stream(s, NULL);
            if (!out) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            AVStream *st = c->ctx->streams[i];
            ret = avcodec_parameters_copy(out->codecpar, st->codecpar);
            if (ret < 0) {
                goto fail;
            }
            out->index = st->index;
            out->start_time = st->start_time;
            out->duration = st->duration;
            out->time_base = st->time_base;
            out->disposition = st->disposition;
            out->discard = st->discard;
            av_packet_ref(&out->attached_pic, &st->attached_pic);
        }
    }
    return 0;
fail:
    pls_read_close(s);
    return ret;
}

static int pls_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    PLSContext *c = s->priv_data;
    int ret = 0;
    if (c->ctx) {
        ret = av_read_frame(c->ctx, pkt);
    }
    return ret;
}

static int pls_read_seek(AVFormatContext *s, int stream_index,
                               int64_t timestamp, int flags)
{
    int ret = 0;
    PLSContext *c = s->priv_data;
    if (c->ctx) {
        ret = avformat_seek_file(c->ctx, 0, timestamp, timestamp, timestamp, flags );
        if (ret < 0) {
            av_log(c->ctx, AV_LOG_ERROR, "Seek failed due to \'%s\'", av_err2str(ret));
        }
    }
    return ret;
}

static int pls_probe(AVProbeData *p)
{
    if (strncmp(p->buf, "[playlist]", 10))
        return 0;

    if (strstr(p->buf, "File")     ||
        strstr(p->buf, "Length") ||
        strstr(p->buf, "Title"))
        return AVPROBE_SCORE_MAX;
    return 0;
}

#define OFFSET(x) offsetof(PLSContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM
static const AVOption pls_options[] = {
    {NULL}
};

static const AVClass pls_class = {
    .class_name = "pls",
    .item_name  = av_default_item_name,
    .option     = pls_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_pls_demuxer = {
    .name           = "pls",
    .long_name      = NULL_IF_CONFIG_SMALL("WinAmp playlist"),
    .priv_class     = &pls_class,
    .priv_data_size = sizeof(PLSContext),
    .flags          = AVFMT_NOGENSEARCH,
    .read_probe     = pls_probe,
    .read_header    = pls_read_header,
    .read_packet    = pls_read_packet,
    .read_close     = pls_read_close,
    .read_seek      = pls_read_seek,
    .extensions     = "pls",
};
