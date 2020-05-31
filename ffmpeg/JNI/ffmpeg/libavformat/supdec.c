/*
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

#include "avformat.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"

#define SUP_PGS_MAGIC 0x5047 /* "PG", big endian */

#ifdef MXTECHS
#include "libavutil/opt.h"
enum SegmentType {
    PALETTE_SEGMENT      = 0x14,
    OBJECT_SEGMENT       = 0x15,
    PRESENTATION_SEGMENT = 0x16,
    WINDOW_SEGMENT       = 0x17,
    DISPLAY_SEGMENT      = 0x80,
};

typedef struct PGSSegmentHeader
{
    uint16_t magic;
    int64_t  pts;
    int64_t  dts;
    uint8_t  type;
    uint16_t size;
} PGSSegmentHeader;

typedef struct {
    AVClass *class;
    int scan;
    PGSSegmentHeader start;
    PGSSegmentHeader end;
} SUPDecContext;

static const char* get_segment_type_string(uint8_t type)
{
    const char* result = NULL;
    switch(type)
    {
        case PALETTE_SEGMENT:
            result = "PDS";
        break;

        case OBJECT_SEGMENT:
            result = "ODS";
        break;

        case PRESENTATION_SEGMENT:
            result = "PCS";
        break;

        case WINDOW_SEGMENT:
            result = "WDS";
        break;

        case DISPLAY_SEGMENT:
            result = "END";
        break;

        default:
            result = "WDS";
        break;
    }
    return result;
}

static int sup_read_segment_header(AVFormatContext *s, PGSSegmentHeader *header)
{
    header->magic = avio_rb16(s->pb);
    if (header->magic != SUP_PGS_MAGIC) {
        return avio_feof(s->pb) ? AVERROR_EOF : AVERROR_INVALIDDATA;
    }
    header->pts = avio_rb32(s->pb);
    header->dts = avio_rb32(s->pb);
    header->type = avio_r8(s->pb);
    header->size = avio_rb16(s->pb);

    //skip over segment
    avio_skip(s->pb, header->size);

    ff_dlog(s, "pts:%lld %f type:%s size:%d\n", header->pts, header->pts / 90000.0f, get_segment_type_string(header->type), header->size);

    return avio_feof(s->pb) ? AVERROR_EOF : 0;
}

static int sup_read_scan(AVFormatContext *s, AVStream *st)
{
    SUPDecContext* c = s->priv_data;
    int ret;
    int64_t pos = avio_tell(s->pb);

    avio_seek(s->pb, 0, SEEK_SET);

    int64_t pos2;
    do
    {
        pos2 = avio_tell(s->pb);
        ret = sup_read_segment_header(s, &c->start);
        if (c->start.type == PRESENTATION_SEGMENT)
        {
            av_add_index_entry(st, pos2, c->start.pts, 0, 0, AVINDEX_KEYFRAME);
            break;
        }
    } while(0 == ret);

    do
    {
        pos2 = avio_tell(s->pb);
        ret = sup_read_segment_header(s, &c->end);
        if (c->end.type == PRESENTATION_SEGMENT)
        {
            av_add_index_entry(st, pos2, c->end.pts, 0, 0, AVINDEX_KEYFRAME);
        }
    } while(0 == ret);

    avio_seek(s->pb, pos, SEEK_SET);
    return 0;
}
#endif

static int sup_read_header(AVFormatContext *s)
{
#ifdef MXTECHS
    SUPDecContext *c = s->priv_data;
#endif
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id = AV_CODEC_ID_HDMV_PGS_SUBTITLE;
    avpriv_set_pts_info(st, 32, 1, 90000);
#ifdef MXTECHS
    if (c->scan && 0 == sup_read_scan(s, st)){
        st->duration = c->end.pts - c->start.pts;
    }
#endif
    return 0;
}

#ifdef MXTECHS
static int sup_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    int index = av_index_search_timestamp(st, timestamp, flags);
    if (index < 0)
        return -1;

    if (avio_seek(s->pb, st->index_entries[index].pos, SEEK_SET) < 0)
        return -1;

    return 0;
}
#endif

static int sup_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int64_t pts, dts, pos;
    int ret;

    pos = avio_tell(s->pb);

    if (avio_rb16(s->pb) != SUP_PGS_MAGIC)
        return avio_feof(s->pb) ? AVERROR_EOF : AVERROR_INVALIDDATA;

    pts = avio_rb32(s->pb);
    dts = avio_rb32(s->pb);

    if ((ret = av_get_packet(s->pb, pkt, 3)) < 0)
        return ret;

    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->pos = pos;
    pkt->pts = pts;
    // Many files have DTS set to 0 for all packets, so assume 0 means unset.
    pkt->dts = dts ? dts : AV_NOPTS_VALUE;

    if (pkt->size >= 3) {
        // The full packet size is stored as part of the packet.
        size_t len = AV_RB16(pkt->data + 1);

        if ((ret = av_append_packet(s->pb, pkt, len)) < 0)
            return ret;
    }

    return 0;
}

static int sup_probe(const AVProbeData *p)
{
    unsigned char *buf = p->buf;
    size_t buf_size = p->buf_size;
    int nb_packets;

    for (nb_packets = 0; nb_packets < 10; nb_packets++) {
        size_t full_packet_size;
        if (buf_size < 10 + 3)
            break;
        if (AV_RB16(buf) != SUP_PGS_MAGIC)
            return 0;
        full_packet_size = AV_RB16(buf + 10 + 1) + 10 + 3;
        if (buf_size < full_packet_size)
            break;
        buf += full_packet_size;
        buf_size -= full_packet_size;
    }
    if (!nb_packets)
        return 0;
    if (nb_packets < 2)
        return AVPROBE_SCORE_RETRY / 2;
    if (nb_packets < 4)
        return AVPROBE_SCORE_RETRY;
    if (nb_packets < 10)
        return AVPROBE_SCORE_EXTENSION;
    return AVPROBE_SCORE_MAX;
}

#ifdef MXTECHS
#define OFFSET(x) offsetof(SUPDecContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_SUBTITLE_PARAM
static const AVOption pgs_options[] = {
    {"scan",
    "Scan all the display set for duration and speed up seek performance",
    OFFSET(scan), AV_OPT_TYPE_BOOL, { .i64 = 1 },
    0, 1, FLAGS},
    { NULL },
};

static const AVClass pgs_class = {
    .class_name = "sup",
    .item_name  = av_default_item_name,
    .option     = pgs_options,
    .version    = LIBAVUTIL_VERSION_INT,
};
#endif

AVInputFormat ff_sup_demuxer = {
    .name           = "sup",
    .long_name      = NULL_IF_CONFIG_SMALL("raw HDMV Presentation Graphic Stream subtitles"),
#ifdef MXTECHS
    .priv_class     = &pgs_class,
    .priv_data_size = sizeof(SUPDecContext),
#endif
    .extensions     = "sup",
    .mime_type      = "application/x-pgs",
    .read_probe     = sup_probe,
    .read_header    = sup_read_header,
#ifdef MXTECHS
    .read_seek      = sup_read_seek,
#endif
    .read_packet    = sup_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
};
