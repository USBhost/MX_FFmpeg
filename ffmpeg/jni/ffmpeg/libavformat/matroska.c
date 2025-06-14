/*
 * Matroska common data
 * Copyright (c) 2003-2004 The FFmpeg project
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

#include "libavutil/stereo3d.h"

#include "matroska.h"

/* If you add a tag here that is not in ff_codec_bmp_tags[]
   or ff_codec_wav_tags[], add it also to additional_audio_tags[]
   or additional_video_tags[] in matroskaenc.c */
const CodecTags ff_mkv_codec_tags[]={
    {"A_AAC"            , AV_CODEC_ID_AAC},
    {"A_AC3"            , AV_CODEC_ID_AC3},
    {"A_ALAC"           , AV_CODEC_ID_ALAC},
    {"A_DTS"            , AV_CODEC_ID_DTS},
    {"A_EAC3"           , AV_CODEC_ID_EAC3},
    {"A_FLAC"           , AV_CODEC_ID_FLAC},
    {"A_MLP"            , AV_CODEC_ID_MLP},
    {"A_MPEG/L2"        , AV_CODEC_ID_MP2},
    {"A_MPEG/L1"        , AV_CODEC_ID_MP1},
    {"A_MPEG/L3"        , AV_CODEC_ID_MP3},
    {"A_OPUS"           , AV_CODEC_ID_OPUS},
    {"A_OPUS/EXPERIMENTAL",AV_CODEC_ID_OPUS},
    {"A_PCM/FLOAT/IEEE" , AV_CODEC_ID_PCM_F32LE},
    {"A_PCM/FLOAT/IEEE" , AV_CODEC_ID_PCM_F64LE},
    {"A_PCM/INT/BIG"    , AV_CODEC_ID_PCM_S16BE},
    {"A_PCM/INT/BIG"    , AV_CODEC_ID_PCM_S24BE},
    {"A_PCM/INT/BIG"    , AV_CODEC_ID_PCM_S32BE},
    {"A_PCM/INT/LIT"    , AV_CODEC_ID_PCM_S16LE},
    {"A_PCM/INT/LIT"    , AV_CODEC_ID_PCM_S24LE},
    {"A_PCM/INT/LIT"    , AV_CODEC_ID_PCM_S32LE},
    {"A_PCM/INT/LIT"    , AV_CODEC_ID_PCM_U8},
    {"A_QUICKTIME/QDMC" , AV_CODEC_ID_QDMC},
    {"A_QUICKTIME/QDM2" , AV_CODEC_ID_QDM2},
    {"A_REAL/14_4"      , AV_CODEC_ID_RA_144},
    {"A_REAL/28_8"      , AV_CODEC_ID_RA_288},
    {"A_REAL/ATRC"      , AV_CODEC_ID_ATRAC3},
    {"A_REAL/COOK"      , AV_CODEC_ID_COOK},
    {"A_REAL/SIPR"      , AV_CODEC_ID_SIPR},
    {"A_TRUEHD"         , AV_CODEC_ID_TRUEHD},
    {"A_TTA1"           , AV_CODEC_ID_TTA},
    {"A_VORBIS"         , AV_CODEC_ID_VORBIS},
    {"A_WAVPACK4"       , AV_CODEC_ID_WAVPACK},

    {"D_WEBVTT/SUBTITLES"   , AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/CAPTIONS"    , AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/DESCRIPTIONS", AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/METADATA"    , AV_CODEC_ID_WEBVTT},

    {"S_TEXT/UTF8"      , AV_CODEC_ID_SUBRIP},
    {"S_TEXT/UTF8"      , AV_CODEC_ID_TEXT},
    {"S_TEXT/ASCII"     , AV_CODEC_ID_TEXT},
    {"S_TEXT/ASS"       , AV_CODEC_ID_ASS},
    {"S_TEXT/SSA"       , AV_CODEC_ID_ASS},
    {"S_ASS"            , AV_CODEC_ID_ASS},
    {"S_SSA"            , AV_CODEC_ID_ASS},
    {"S_VOBSUB"         , AV_CODEC_ID_DVD_SUBTITLE},
    {"S_DVBSUB"         , AV_CODEC_ID_DVB_SUBTITLE},
    {"S_HDMV/PGS"       , AV_CODEC_ID_HDMV_PGS_SUBTITLE},
    {"S_HDMV/TEXTST"    , AV_CODEC_ID_HDMV_TEXT_SUBTITLE},

    {"V_AV1"            , AV_CODEC_ID_AV1},
    {"V_DIRAC"          , AV_CODEC_ID_DIRAC},
    {"V_FFV1"           , AV_CODEC_ID_FFV1},
    {"V_MJPEG"          , AV_CODEC_ID_MJPEG},
    {"V_MPEG1"          , AV_CODEC_ID_MPEG1VIDEO},
    {"V_MPEG2"          , AV_CODEC_ID_MPEG2VIDEO},
    {"V_MPEG4/ISO/ASP"  , AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/AP"   , AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/SP"   , AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/AVC"  , AV_CODEC_ID_H264},
    {"V_MPEGH/ISO/HEVC" , AV_CODEC_ID_HEVC},
    {"V_MPEG4/MS/V3"    , AV_CODEC_ID_MSMPEG4V3},
    {"V_PRORES"         , AV_CODEC_ID_PRORES},
    {"V_REAL/RV10"      , AV_CODEC_ID_RV10},
    {"V_REAL/RV20"      , AV_CODEC_ID_RV20},
    {"V_REAL/RV30"      , AV_CODEC_ID_RV30},
    {"V_REAL/RV40"      , AV_CODEC_ID_RV40},
    {"V_SNOW"           , AV_CODEC_ID_SNOW},
    {"V_THEORA"         , AV_CODEC_ID_THEORA},
    {"V_UNCOMPRESSED"   , AV_CODEC_ID_RAWVIDEO},
    {"V_VP8"            , AV_CODEC_ID_VP8},
    {"V_VP9"            , AV_CODEC_ID_VP9},

    {""                 , AV_CODEC_ID_NONE}
};

const CodecTags ff_webm_codec_tags[] = {
    {"V_VP8"            , AV_CODEC_ID_VP8},
    {"V_VP9"            , AV_CODEC_ID_VP9},
    {"V_AV1"            , AV_CODEC_ID_AV1},

    {"A_VORBIS"         , AV_CODEC_ID_VORBIS},
    {"A_OPUS"           , AV_CODEC_ID_OPUS},

    {"D_WEBVTT/SUBTITLES"   , AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/CAPTIONS"    , AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/DESCRIPTIONS", AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/METADATA"    , AV_CODEC_ID_WEBVTT},

    {""                 , AV_CODEC_ID_NONE}
};

const CodecMime ff_mkv_image_mime_tags[] = {
    {"image/gif"                  , AV_CODEC_ID_GIF},
    {"image/jpeg"                 , AV_CODEC_ID_MJPEG},
    {"image/png"                  , AV_CODEC_ID_PNG},
    {"image/tiff"                 , AV_CODEC_ID_TIFF},

    {""                           , AV_CODEC_ID_NONE}
};

const CodecMime ff_mkv_mime_tags[] = {
    {"text/plain"                 , AV_CODEC_ID_TEXT},
    {"application/x-truetype-font", AV_CODEC_ID_TTF},
    {"application/x-font"         , AV_CODEC_ID_TTF},
    {"application/vnd.ms-opentype", AV_CODEC_ID_OTF},
    {"binary"                     , AV_CODEC_ID_BIN_DATA},

    {""                           , AV_CODEC_ID_NONE}
};

const AVMetadataConv ff_mkv_metadata_conv[] = {
    { "LEAD_PERFORMER", "performer" },
    { "PART_NUMBER"   , "track"  },
    { 0 }
};

const char * const ff_matroska_video_stereo_mode[MATROSKA_VIDEO_STEREOMODE_TYPE_NB] = {
    "mono",
    "left_right",
    "bottom_top",
    "top_bottom",
    "checkerboard_rl",
    "checkerboard_lr",
    "row_interleaved_rl",
    "row_interleaved_lr",
    "col_interleaved_rl",
    "col_interleaved_lr",
    "anaglyph_cyan_red",
    "right_left",
    "anaglyph_green_magenta",
    "block_lr",
    "block_rl",
};

const char * const ff_matroska_video_stereo_plane[MATROSKA_VIDEO_STEREO_PLANE_COUNT] = {
    "left",
    "right",
    "background",
};

int ff_mkv_stereo3d_conv(AVStream *st, MatroskaVideoStereoModeType stereo_mode)
{
    AVStereo3D *stereo;
    int ret;

    stereo = av_stereo3d_alloc();
    if (!stereo)
        return AVERROR(ENOMEM);

    // note: the missing breaks are intentional
    switch (stereo_mode) {
    case MATROSKA_VIDEO_STEREOMODE_TYPE_MONO:
        stereo->type = AV_STEREO3D_2D;
        break;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_RIGHT_LEFT:
        stereo->flags |= AV_STEREO3D_FLAG_INVERT;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_LEFT_RIGHT:
        stereo->type = AV_STEREO3D_SIDEBYSIDE;
        break;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_BOTTOM_TOP:
        stereo->flags |= AV_STEREO3D_FLAG_INVERT;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_TOP_BOTTOM:
        stereo->type = AV_STEREO3D_TOPBOTTOM;
        break;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_CHECKERBOARD_RL:
        stereo->flags |= AV_STEREO3D_FLAG_INVERT;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_CHECKERBOARD_LR:
        stereo->type = AV_STEREO3D_CHECKERBOARD;
        break;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_ROW_INTERLEAVED_RL:
        stereo->flags |= AV_STEREO3D_FLAG_INVERT;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_ROW_INTERLEAVED_LR:
        stereo->type = AV_STEREO3D_LINES;
        break;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_COL_INTERLEAVED_RL:
        stereo->flags |= AV_STEREO3D_FLAG_INVERT;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_COL_INTERLEAVED_LR:
        stereo->type = AV_STEREO3D_COLUMNS;
        break;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_BOTH_EYES_BLOCK_RL:
        stereo->flags |= AV_STEREO3D_FLAG_INVERT;
    case MATROSKA_VIDEO_STEREOMODE_TYPE_BOTH_EYES_BLOCK_LR:
        stereo->type = AV_STEREO3D_FRAMESEQUENCE;
        break;
    }

    ret = av_stream_add_side_data(st, AV_PKT_DATA_STEREO3D, (uint8_t *)stereo,
                                  sizeof(*stereo));
    if (ret < 0) {
        av_freep(&stereo);
        return ret;
    }

    return 0;
}

#ifdef MX_DEBUG
const char* ff_mkv_get_id_string(uint32_t id)
{
    switch (id) {
    case EBML_ID_HEADER:
        return "HEADER";

        case EBML_ID_EBMLVERSION:
            return "EBMLVERSION";
        case EBML_ID_EBMLREADVERSION:
            return "EBMLREADVERSION";
        case EBML_ID_EBMLMAXIDLENGTH:
            return "EBMLMAXIDLENGTH";
        case EBML_ID_EBMLMAXSIZELENGTH:
            return "EBMLMAXSIZELENGTH";
        case EBML_ID_DOCTYPE:
            return "DOCTYPE";
        case EBML_ID_DOCTYPEVERSION:
            return "DOCTYPEVERSION";
        case EBML_ID_DOCTYPEREADVERSION:
            return "DOCTYPEREADVERSION";

        /* general EBML types */
        case EBML_ID_VOID:
            return "VOID";
        case EBML_ID_CRC32:
            return "CRC32";

    /*
     * Matroska element IDs, max. 32 bits
     */

    /* toplevel segment */
        case MATROSKA_ID_SEGMENT:
            return "SEGMENT";

    /* Matroska top-level master IDs */
        case MATROSKA_ID_INFO :
            return "INFO";
        case MATROSKA_ID_TRACKS:
            return "TRACKS";
        case MATROSKA_ID_CUES:
            return "CUES";
        case MATROSKA_ID_TAGS:
            return "TAGS";
        case MATROSKA_ID_SEEKHEAD:
            return "SEEKHEAD";
        case MATROSKA_ID_ATTACHMENTS:
            return "ATTACHMENTS";
        case MATROSKA_ID_CLUSTER:
            return "CLUSTER";
        case MATROSKA_ID_CHAPTERS:
            return "CHAPTERS";

        /* IDs in the info master */
        case MATROSKA_ID_TIMECODESCALE:
            return "TIMECODESCALE";
        case MATROSKA_ID_DURATION:
            return "DURATION";
        case MATROSKA_ID_TITLE:
            return "TITLE";
        case MATROSKA_ID_WRITINGAPP:
            return "WRITINGAPP";
        case MATROSKA_ID_MUXINGAPP:
            return "MUXINGAPP";
        case MATROSKA_ID_DATEUTC:
            return "DATEUTC";
        case MATROSKA_ID_SEGMENTUID:
            return "SEGMENTUID";

        /* ID in the tracks master */
        case MATROSKA_ID_TRACKENTRY:
            return "TRACKENTRY";

        /* IDs in the trackentry master */
        case MATROSKA_ID_TRACKNUMBER:
            return "TRACKNUMBER";
        case MATROSKA_ID_TRACKUID:
            return "TRACKUID";
        case MATROSKA_ID_TRACKTYPE:
            return "TRACKTYPE";
        case MATROSKA_ID_TRACKVIDEO:
            return "TRACKVIDEO";
        case MATROSKA_ID_TRACKAUDIO:
            return "TRACKAUDIO";
        case MATROSKA_ID_TRACKOPERATION:
            return "TRACKOPERATION";
        case MATROSKA_ID_TRACKCOMBINEPLANES:
            return "TRACKCOMBINEPLANES";
        case MATROSKA_ID_TRACKPLANE:
            return "TRACKPLANE";
        case MATROSKA_ID_TRACKPLANEUID:
            return "TRACKPLANEUID";
        case MATROSKA_ID_TRACKPLANETYPE:
            return "TRACKPLANETYPE";
        case MATROSKA_ID_CODECID:
            return "CODECID";
        case MATROSKA_ID_CODECPRIVATE:
            return "CODECPRIVATE";
        case MATROSKA_ID_CODECNAME:
            return "CODECNAME";
        case MATROSKA_ID_CODECINFOURL:
            return "CODECINFOURL";
        case MATROSKA_ID_CODECDOWNLOADURL:
            return "CODECDOWNLOADURL";
        case MATROSKA_ID_CODECDECODEALL:
            return "CODECDECODEALL";
        case MATROSKA_ID_CODECDELAY:
            return "CODECDELAY";
        case MATROSKA_ID_SEEKPREROLL:
            return "SEEKPREROLL";
        case MATROSKA_ID_TRACKNAME:
            return "TRACKNAME";
        case MATROSKA_ID_TRACKLANGUAGE:
            return "TRACKLANGUAGE";
        case MATROSKA_ID_TRACKFLAGENABLED:
            return "TRACKFLAGENABLED";
        case MATROSKA_ID_TRACKFLAGDEFAULT:
            return "TRACKFLAGDEFAULT";
        case MATROSKA_ID_TRACKFLAGFORCED:
            return "TRACKFLAGFORCED";
        case MATROSKA_ID_TRACKFLAGLACING:
            return "TRACKFLAGLACING";
        case MATROSKA_ID_TRACKMINCACHE:
            return "TRACKMINCACHE";
        case MATROSKA_ID_TRACKMAXCACHE:
            return "TRACKMAXCACHE";
        case MATROSKA_ID_TRACKDEFAULTDURATION:
            return "TRACKDEFAULTDURATION";
        case MATROSKA_ID_TRACKCONTENTENCODINGS:
            return "TRACKCONTENTENCODINGS";
        case MATROSKA_ID_TRACKCONTENTENCODING:
            return "TRACKCONTENTENCODING";
        case MATROSKA_ID_TRACKTIMECODESCALE:
            return "TRACKTIMECODESCALE";
        case MATROSKA_ID_TRACKMAXBLKADDID:
            return "TRACKMAXBLKADDID";

        /* IDs in the trackvideo master */
        case MATROSKA_ID_VIDEOFRAMERATE:
            return "VIDEOFRAMERATE";
        case MATROSKA_ID_VIDEODISPLAYWIDTH:
            return "VIDEODISPLAYWIDTH";
        case MATROSKA_ID_VIDEODISPLAYHEIGHT:
            return "VIDEODISPLAYHEIGHT";
        case MATROSKA_ID_VIDEOPIXELWIDTH:
            return "VIDEOPIXELWIDTH";
        case MATROSKA_ID_VIDEOPIXELHEIGHT:
            return "VIDEOPIXELHEIGHT";
        case MATROSKA_ID_VIDEOPIXELCROPB:
            return "VIDEOPIXELCROPB";
        case MATROSKA_ID_VIDEOPIXELCROPT:
            return "VIDEOPIXELCROPT";
        case MATROSKA_ID_VIDEOPIXELCROPL:
            return "VIDEOPIXELCROPL";
        case MATROSKA_ID_VIDEOPIXELCROPR:
            return "VIDEOPIXELCROPR";
        case MATROSKA_ID_VIDEODISPLAYUNIT:
            return "VIDEODISPLAYUNIT";
        case MATROSKA_ID_VIDEOFLAGINTERLACED:
            return "VIDEOFLAGINTERLACED";
        case MATROSKA_ID_VIDEOFIELDORDER:
            return "VIDEOFIELDORDER";
        case MATROSKA_ID_VIDEOSTEREOMODE:
            return "VIDEOSTEREOMODE";
        case MATROSKA_ID_VIDEOALPHAMODE:
            return "VIDEOALPHAMODE";
        case MATROSKA_ID_VIDEOASPECTRATIO:
            return "VIDEOASPECTRATIO";
        case MATROSKA_ID_VIDEOCOLORSPACE:
            return "VIDEOCOLORSPACE";
        case MATROSKA_ID_VIDEOCOLOR:
            return "VIDEOCOLOR";

        case MATROSKA_ID_VIDEOCOLORMATRIXCOEFF:
            return "VIDEOCOLORMATRIXCOEFF";
        case MATROSKA_ID_VIDEOCOLORBITSPERCHANNEL:
            return "VIDEOCOLORBITSPERCHANNEL";
        case MATROSKA_ID_VIDEOCOLORCHROMASUBHORZ:
            return "VIDEOCOLORCHROMASUBHORZ";
        case MATROSKA_ID_VIDEOCOLORCHROMASUBVERT:
            return "VIDEOCOLORCHROMASUBVERT";
        case MATROSKA_ID_VIDEOCOLORCBSUBHORZ:
            return "VIDEOCOLORCBSUBHORZ";
        case MATROSKA_ID_VIDEOCOLORCBSUBVERT:
            return "VIDEOCOLORCBSUBVERT";
        case MATROSKA_ID_VIDEOCOLORCHROMASITINGHORZ:
            return "VIDEOCOLORCHROMASITINGHORZ";
        case MATROSKA_ID_VIDEOCOLORCHROMASITINGVERT:
            return "VIDEOCOLORCHROMASITINGVERT";
        case MATROSKA_ID_VIDEOCOLORRANGE:
            return "VIDEOCOLORRANGE";
        case MATROSKA_ID_VIDEOCOLORTRANSFERCHARACTERISTICS:
            return "VIDEOCOLORTRANSFERCHARACTERISTICS";

        case MATROSKA_ID_VIDEOCOLORPRIMARIES:
            return "VIDEOCOLORPRIMARIES";
        case MATROSKA_ID_VIDEOCOLORMAXCLL:
            return "VIDEOCOLORMAXCLL";
        case MATROSKA_ID_VIDEOCOLORMAXFALL:
            return "VIDEOCOLORMAXFALL";

        case MATROSKA_ID_VIDEOCOLORMASTERINGMETA:
            return "VIDEOCOLORMASTERINGMETA";
        case MATROSKA_ID_VIDEOCOLOR_RX:
            return "VIDEOCOLOR_RX";
        case MATROSKA_ID_VIDEOCOLOR_RY:
            return "VIDEOCOLOR_RY";
        case MATROSKA_ID_VIDEOCOLOR_GX:
            return "VIDEOCOLOR_GX";
        case MATROSKA_ID_VIDEOCOLOR_GY:
            return "VIDEOCOLOR_GY";
        case MATROSKA_ID_VIDEOCOLOR_BX:
            return "VIDEOCOLOR_BX";
        case MATROSKA_ID_VIDEOCOLOR_BY:
            return "VIDEOCOLOR_BY";
        case MATROSKA_ID_VIDEOCOLOR_WHITEX:
            return "VIDEOCOLOR_WHITEX";
        case MATROSKA_ID_VIDEOCOLOR_WHITEY:
            return "VIDEOCOLOR_WHITEY";
        case MATROSKA_ID_VIDEOCOLOR_LUMINANCEMAX:
            return "VIDEOCOLOR_LUMINANCEMAX";
        case MATROSKA_ID_VIDEOCOLOR_LUMINANCEMIN:
            return "VIDEOCOLOR_LUMINANCEMIN";

        case MATROSKA_ID_VIDEOPROJECTION:
            return "VIDEOPROJECTION";
        case MATROSKA_ID_VIDEOPROJECTIONTYPE:
            return "VIDEOPROJECTIONTYPE";
        case MATROSKA_ID_VIDEOPROJECTIONPRIVATE:
            return "VIDEOPROJECTIONPRIVATE";
        case MATROSKA_ID_VIDEOPROJECTIONPOSEYAW:
            return "VIDEOPROJECTIONPOSEYAW";
        case MATROSKA_ID_VIDEOPROJECTIONPOSEPITCH:
            return "VIDEOPROJECTIONPOSEPITCH";
        case MATROSKA_ID_VIDEOPROJECTIONPOSEROLL:
            return "VIDEOPROJECTIONPOSEROLL";

        /* IDs in the trackaudio master */
        case MATROSKA_ID_AUDIOSAMPLINGFREQ:
            return "AUDIOSAMPLINGFREQ";
        case MATROSKA_ID_AUDIOOUTSAMPLINGFREQ:
            return "AUDIOOUTSAMPLINGFREQ";

        case MATROSKA_ID_AUDIOBITDEPTH:
            return "AUDIOBITDEPTH";
        case MATROSKA_ID_AUDIOCHANNELS:
            return "AUDIOCHANNELS";

        /* IDs in the content encoding master */
        case MATROSKA_ID_ENCODINGORDER:
            return "ENCODINGORDER";
        case MATROSKA_ID_ENCODINGSCOPE:
            return "ENCODINGORDER";
        case MATROSKA_ID_ENCODINGTYPE:
            return "ENCODINGTYPE";
        case MATROSKA_ID_ENCODINGCOMPRESSION:
            return "ENCODINGCOMPRESSION";
        case MATROSKA_ID_ENCODINGCOMPALGO:
            return "ENCODINGCOMPALGO";
        case MATROSKA_ID_ENCODINGCOMPSETTINGS:
            return "ENCODINGCOMPSETTINGS";

        case MATROSKA_ID_ENCODINGENCRYPTION:
            return "ENCODINGENCRYPTION";
        case MATROSKA_ID_ENCODINGENCAESSETTINGS:
            return "ENCODINGENCAESSETTINGS";
        case MATROSKA_ID_ENCODINGENCALGO:
            return "ENCODINGENCALGO";
        case MATROSKA_ID_ENCODINGENCKEYID:
            return "ENCODINGENCKEYID";
        case MATROSKA_ID_ENCODINGSIGALGO:
            return "ENCODINGSIGALGO";
        case MATROSKA_ID_ENCODINGSIGHASHALGO:
            return "ENCODINGSIGHASHALGO";
        case MATROSKA_ID_ENCODINGSIGKEYID:
            return "ENCODINGSIGKEYID";
        case MATROSKA_ID_ENCODINGSIGNATURE:
            return "ENCODINGSIGNATURE";

        /* ID in the cues master */
        case MATROSKA_ID_POINTENTRY:
            return "POINTENTRY";

        /* IDs in the pointentry master */
        case MATROSKA_ID_CUETIME:
            return "CUETIME";
        case MATROSKA_ID_CUETRACKPOSITION:
            return "CUETRACKPOSITION";

        /* IDs in the cuetrackposition master */
        case MATROSKA_ID_CUETRACK:
            return "CUETRACK";
        case MATROSKA_ID_CUECLUSTERPOSITION:
            return "CUECLUSTERPOSITION";
        case MATROSKA_ID_CUERELATIVEPOSITION:
            return "CUERELATIVEPOSITION";
        case MATROSKA_ID_CUEDURATION:
            return "CUEDURATION";
        case MATROSKA_ID_CUEBLOCKNUMBER:
            return "CUEBLOCKNUMBER";

        /* IDs in the tags master */
        case MATROSKA_ID_TAG:
            return "TAG";
        case MATROSKA_ID_SIMPLETAG:
            return "ID_SIMPLETAG";
        case MATROSKA_ID_TAGNAME:
            return "TAGNAME";
        case MATROSKA_ID_TAGSTRING:
            return "TAGSTRING";
        case MATROSKA_ID_TAGLANG:
            return "TAGLANG";
        case MATROSKA_ID_TAGDEFAULT:
            return "TAGDEFAULT";
        case MATROSKA_ID_TAGDEFAULT_BUG:
            return "TAGDEFAULT_BUG";
        case MATROSKA_ID_TAGTARGETS:
            return "TAGTARGETS";
        case MATROSKA_ID_TAGTARGETS_TYPE:
            return "TAGTARGETS_TYPE";
        case MATROSKA_ID_TAGTARGETS_TYPEVALUE:
            return "TAGTARGETS_TYPEVALUE";
        case MATROSKA_ID_TAGTARGETS_TRACKUID:
            return "TAGTARGETS_TRACKUID";
        case MATROSKA_ID_TAGTARGETS_CHAPTERUID:
            return "TAGTARGETS_CHAPTERUID";
        case MATROSKA_ID_TAGTARGETS_ATTACHUID:
            return "TAGTARGETS_ATTACHUID";

        /* IDs in the seekhead master */
        case MATROSKA_ID_SEEKENTRY:
            return "SEEKENTRY";

        /* IDs in the seekpoint master */
        case MATROSKA_ID_SEEKID:
            return "SEEKID";
        case MATROSKA_ID_SEEKPOSITION:
            return "SEEKPOSITION";

        /* IDs in the cluster master */
        case MATROSKA_ID_CLUSTERTIMECODE:
            return "CLUSTERTIMECODE";
        case MATROSKA_ID_CLUSTERPOSITION:
            return "CLUSTERPOSITION";
        case MATROSKA_ID_CLUSTERPREVSIZE:
            return "CLUSTERPREVSIZE";
        case MATROSKA_ID_BLOCKGROUP:
            return "BLOCKGROUP";
        case MATROSKA_ID_BLOCKADDITIONS:
            return "BLOCKADDITIONS";
        case MATROSKA_ID_BLOCKMORE:
            return "BLOCKMORE";
        case MATROSKA_ID_BLOCKADDID:
            return "BLOCKADDID";
        case MATROSKA_ID_BLOCKADDITIONAL:
            return "BLOCKADDITIONAL";
        case MATROSKA_ID_SIMPLEBLOCK:
            return "SIMPLEBLOCK";

        /* IDs in the blockgroup master */
        case MATROSKA_ID_BLOCK:
            return "BLOCK";
        case MATROSKA_ID_BLOCKDURATION:
            return "BLOCKDURATION";
        case MATROSKA_ID_BLOCKREFERENCE:
            return "BLOCKREFERENCE";
        case MATROSKA_ID_CODECSTATE:
            return "CODECSTATE";
        case MATROSKA_ID_DISCARDPADDING:
            return "DISCARDPADDING";

        /* IDs in the attachments master */
        case MATROSKA_ID_ATTACHEDFILE:
            return "ATTACHEDFILE";
        case MATROSKA_ID_FILEDESC:
            return "FILEDESC";
        case MATROSKA_ID_FILENAME:
            return "FILENAME";
        case MATROSKA_ID_FILEMIMETYPE:
            return "FILEMIMETYPE";
        case MATROSKA_ID_FILEDATA:
            return "FILEDATA";
        case MATROSKA_ID_FILEUID:
            return "FILEUID";

        /* IDs in the chapters master */
        case MATROSKA_ID_EDITIONENTRY:
            return "EDITIONENTRY";
        case MATROSKA_ID_CHAPTERATOM:
            return "CHAPTERATOM";
        case MATROSKA_ID_CHAPTERTIMESTART:
            return "CHAPTERTIMESTART";
        case MATROSKA_ID_CHAPTERTIMEEND:
            return "CHAPTERTIMEEND";
        case MATROSKA_ID_CHAPTERDISPLAY:
            return "CHAPTERDISPLAY";
        case MATROSKA_ID_CHAPSTRING:
            return "CHAPSTRING";
        case MATROSKA_ID_CHAPLANG:
            return "CHAPLANG";
        case MATROSKA_ID_CHAPCOUNTRY:
            return "CHAPCOUNTRY";
        case MATROSKA_ID_EDITIONUID:
            return "EDITIONUID";
        case MATROSKA_ID_EDITIONFLAGHIDDEN:
            return "EDITIONFLAGHIDDEN";
        case MATROSKA_ID_EDITIONFLAGDEFAULT:
            return "EDITIONFLAGDEFAULT";
        case MATROSKA_ID_EDITIONFLAGORDERED:
            return "EDITIONFLAGORDERED";
        case MATROSKA_ID_CHAPTERUID:
            return "CHAPTERUID";
        case MATROSKA_ID_CHAPTERFLAGHIDDEN:
            return "CHAPTERFLAGHIDDEN";
        case MATROSKA_ID_CHAPTERFLAGENABLED:
            return "CHAPTERFLAGENABLED";
        case MATROSKA_ID_CHAPTERPHYSEQUIV:
            return "CHAPTERPHYSEQUIV";
    }
    return "UNKNOWN";
}
#endif
