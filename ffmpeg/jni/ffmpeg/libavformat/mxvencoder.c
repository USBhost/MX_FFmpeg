//
//  mxvencoder.c
//  FFmpeg-xcode
//
//  Created by Wang Shaoqi on 2020/2/3.
//  Copyright © 2020 Wang Shaoqi. All rights reserved.
//

#include "avformat.h"
#include "internal.h"
#include "riff.h"
#include "libavcodec/avcodec.h"

static const AVCodecTag additional_audio_tags[] = {
    { AV_CODEC_ID_ALAC,      0XFFFFFFFF },
    { AV_CODEC_ID_MLP,       0xFFFFFFFF },
    { AV_CODEC_ID_OPUS,      0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S16BE, 0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S24BE, 0xFFFFFFFF },
    { AV_CODEC_ID_PCM_S32BE, 0xFFFFFFFF },
    { AV_CODEC_ID_QDMC,      0xFFFFFFFF },
    { AV_CODEC_ID_QDM2,      0xFFFFFFFF },
    { AV_CODEC_ID_RA_144,    0xFFFFFFFF },
    { AV_CODEC_ID_RA_288,    0xFFFFFFFF },
    { AV_CODEC_ID_COOK,      0xFFFFFFFF },
    { AV_CODEC_ID_TRUEHD,    0xFFFFFFFF },
    { AV_CODEC_ID_NONE,      0xFFFFFFFF }
};

static const AVCodecTag additional_video_tags[] = {
    { AV_CODEC_ID_RV10,      0xFFFFFFFF },
    { AV_CODEC_ID_RV20,      0xFFFFFFFF },
    { AV_CODEC_ID_RV30,      0xFFFFFFFF },
    { AV_CODEC_ID_NONE,      0xFFFFFFFF }
};

static const AVCodecTag additional_subtitle_tags[] = {
    { AV_CODEC_ID_DVB_SUBTITLE,      0xFFFFFFFF },
    { AV_CODEC_ID_DVD_SUBTITLE,      0xFFFFFFFF },
    { AV_CODEC_ID_HDMV_PGS_SUBTITLE, 0xFFFFFFFF },
    { AV_CODEC_ID_NONE,              0xFFFFFFFF }
};

#if CONFIG_MXV_MUXER
#include "mxv_wrap.h"

static int wrapper_mxv_init( AVFormatContext *s )
{
    return mxv_init( s );
}

static int wrapper_mxv_write_header( AVFormatContext *s )
{
    return mxv_write_header( s );
}

static int wrapper_mxv_write_flush_packet( AVFormatContext *s, AVPacket *pkt )
{
    return mxv_write_flush_packet( s, pkt );
}

static int wrapper_mxv_write_trailer( AVFormatContext *s )
{
    return mxv_write_trailer( s );
}

static int wrapper_mxv_query_codec( enum AVCodecID codec_id, int std_compliance )
{
    return mxv_query_codec( codec_id, std_compliance );
}

static int wrapper_mxv_check_bitstream( AVFormatContext *s, const AVPacket *pkt )
{
    return mxv_check_bitstream( s, pkt );
}

AVOutputFormat ff_mxv_muxer = {
    .name              = "mxv",
    .long_name         = NULL_IF_CONFIG_SMALL("MXV"),
    .mime_type         = "video/x-mxv",
    .extensions        = "mxv",
    .priv_data_size    = 10240,
    .audio_codec       = CONFIG_LIBVORBIS_ENCODER ?
                         AV_CODEC_ID_VORBIS : AV_CODEC_ID_AC3,
    .video_codec       = CONFIG_LIBX264_ENCODER ?
                         AV_CODEC_ID_H264 : AV_CODEC_ID_MPEG4,
    .init              = wrapper_mxv_init,
    .write_header      = wrapper_mxv_write_header,
    .write_packet      = wrapper_mxv_write_flush_packet,
    .write_trailer     = wrapper_mxv_write_trailer,
    .flags             = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS |
                         AVFMT_TS_NONSTRICT | AVFMT_ALLOW_FLUSH,
    .codec_tag         = (const AVCodecTag* const []){
         ff_codec_bmp_tags, ff_codec_wav_tags,
         additional_audio_tags, additional_video_tags, additional_subtitle_tags, 0
    },
    .subtitle_codec    = AV_CODEC_ID_ASS,
    .query_codec       = wrapper_mxv_query_codec,
    .check_bitstream   = wrapper_mxv_check_bitstream,
    //.priv_class        = &mxv_class,
};
#endif
