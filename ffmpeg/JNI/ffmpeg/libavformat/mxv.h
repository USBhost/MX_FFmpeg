//
//  mxv.h
/*
 * Copyright (c) 2024 linlizh@amazon.com
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

#ifndef mxv_h
#define mxv_h

#include "libavcodec/avcodec.h"
#include "metadata.h"
#include "internal.h"


///Change EBML id in correct interval according to this document https://github.com/cellar-wg/ebml-specification/blob/master/specification.markdown
///   Element ID Octet Length    Range of Valid Element IDs    Number of Valid Element IDs
///             1                        0x81 - 0xFE                    126
///             2                        0x407F - 0x7FFE                16256
///             3                        0x203FFF - 0x3FFFFE            2,080,768
///             4                        0x101FFFFF - 0x1FFFFFFE        268,338,304
///                                                                     2^n*7 - 2^(n-1)*7
/* EBML version supported */
#define EBML_VERSION 1

/* top-level master-IDs */
#define EBML_ID_HEADER                                             0x1954EEB2               //0x1A45DFA3

/* IDs in the HEADER master */
#define EBML_ID_EBMLVERSION                                        0x5195                   //0x4286
#define EBML_ID_EBMLREADVERSION                                    0x5106                   //0x42F7
#define EBML_ID_EBMLMAXIDLENGTH                                    0x5101                   //0x42F2
#define EBML_ID_EBMLMAXSIZELENGTH                                  0x5102                   //0x42F3
#define EBML_ID_DOCTYPE                                            0x5191                   //0x4282
#define EBML_ID_DOCTYPEVERSION                                     0x5196                   //0x4287
#define EBML_ID_DOCTYPEREADVERSION                                 0x5194                   //0x4285

/* general EBML types */
#define EBML_ID_VOID                                               0xFB                     //0xEC
#define EBML_ID_CRC32                                              0xCE                     //0xBF

/*
 * MXV element IDs, max. 32 bits
 */

/* toplevel segment */
#define MXV_ID_SEGMENT                                             0x17629F76               //0x18538067

/* MXV top-level master IDs */
#define MXV_ID_INFO                                                0x1458B875               //0x1549A966
#define MXV_ID_TRACKS                                              0x1564BD7A               //0x1654AE6B
#define MXV_ID_CUES                                                0x1B62CA7A               //0x1C53BB6B
#define MXV_ID_TAGS                                                0x1163D276               //0x1254C367
#define MXV_ID_SEEKHEAD                                            0x105CAA83               //0x114D9B74
#define MXV_ID_ATTACHMENTS                                         0x1850B378               //0x1941A469
#define MXV_ID_CLUSTER                                             0x1E52C584               //0x1F43B675
#define MXV_ID_CHAPTERS                                            0x1F52B68F               //0x1043A770

/* IDs in the info master */
#define MXV_ID_TIMECODESCALE                                       0x39E6C0                 //0x2AD7B1
#define MXV_ID_DURATION                                            0x5398                   //0x4489
#define MXV_ID_TITLE                                               0x4AB8                   //0x7BA9
#define MXV_ID_WRITINGAPP                                          0x6650                   //0x5741
#define MXV_ID_MUXINGAPP                                           0x5C9F                   //0x4D80
#define MXV_ID_DATEUTC                                             0x5370                   //0x4461
#define MXV_ID_SEGMENTUID                                          0x42B3                   //0x73A4

/* ID in the tracks master */
#define MXV_ID_TRACKENTRY                                          0xBD                     //0xAE

/* IDs in the trackentry master */
#define MXV_ID_TRACKNUMBER                                         0xE6                     //0xD7
#define MXV_ID_TRACKUID                                            0x42D4                   //0x73C5
#define MXV_ID_TRACKTYPE                                           0x92                     //0x83
#define MXV_ID_TRACKVIDEO                                          0xFE                     //0xE0
#define MXV_ID_TRACKAUDIO                                          0xF0                     //0xE1
#define MXV_ID_TRACKOPERATION                                      0xF1                     //0xE2
#define MXV_ID_TRACKCOMBINEPLANES                                  0xF2                     //0xE3
#define MXV_ID_TRACKPLANE                                          0xF3                     //0xE4
#define MXV_ID_TRACKPLANEUID                                       0xF4                     //0xE5
#define MXV_ID_TRACKPLANETYPE                                      0xF5                     //0xE6
#define MXV_ID_CODECID                                             0x95                     //0x86
#define MXV_ID_CODECPRIVATE                                        0x72B1                   //0x63A2
#define MXV_ID_CODECNAME                                           0x349597                 //0x258688
#define MXV_ID_CODECINFOURL                                        0x2A5F5F                 //0x3B4040
#define MXV_ID_CODECDOWNLOADURL                                    0x35C15F                 //0x26B240
#define MXV_ID_CODECDECODEALL                                      0xB9                     //0xAA
#define MXV_ID_CODECDELAY                                          0x65B9                   //0x56AA
#define MXV_ID_SEEKPREROLL                                         0x65CA                   //0x56BB
#define MXV_ID_TRACKNAME                                           0x627D                   //0x536E
#define MXV_ID_TRACKLANGUAGE                                       0x31C4AB                 //0x22B59C
#define MXV_ID_TRACKFLAGENABLED                                    0xC8                     //0xB9
#define MXV_ID_TRACKFLAGDEFAULT                                    0x97                     //0x88
#define MXV_ID_TRACKFLAGFORCED                                     0x64B9                   //0x55AA
#define MXV_ID_TRACKFLAGLACING                                     0xAB                     //0x9C
#define MXV_ID_TRACKMINCACHE                                       0x7CF6                   //0x6DE7
#define MXV_ID_TRACKMAXCACHE                                       0x7C07                   //0x6DF8
#define MXV_ID_TRACKDEFAULTDURATION                                0x32F292                 //0x23E383
#define MXV_ID_TRACKCONTENTENCODINGS                               0x7C9F                   //0x6D80
#define MXV_ID_TRACKCONTENTENCODING                                0x715F                   //0x6240
#define MXV_ID_TRACKTIMECODESCALE                                  0x32405E                 //0x23314F
#define MXV_ID_TRACKMAXBLKADDID                                    0x64FD                   //0x55EE

/* IDs in the trackvideo master */
#define MXV_ID_VIDEOFRAMERATE                                      0x3292F2                 //0x2383E3
#define MXV_ID_VIDEODISPLAYWIDTH                                   0x63CF                   //0x54B0
#define MXV_ID_VIDEODISPLAYHEIGHT                                  0x63C9                   //0x54BA
#define MXV_ID_VIDEOPIXELWIDTH                                     0xCF                     //0xB0
#define MXV_ID_VIDEOPIXELHEIGHT                                    0xC9                     //0xBA
#define MXV_ID_VIDEOPIXELCROPB                                     0x63B9                   //0x54AA
#define MXV_ID_VIDEOPIXELCROPT                                     0x63CA                   //0x54BB
#define MXV_ID_VIDEOPIXELCROPL                                     0x63DB                   //0x54CC
#define MXV_ID_VIDEOPIXELCROPR                                     0x63EC                   //0x54DD
#define MXV_ID_VIDEODISPLAYUNIT                                    0x63C1                   //0x54B2
#define MXV_ID_VIDEOFLAGINTERLACED                                 0xA9                     //0x9A
#define MXV_ID_VIDEOFIELDORDER                                     0xAC                     //0x9D
#define MXV_ID_VIDEOSTEREOMODE                                     0x62C7                   //0x53B8
#define MXV_ID_VIDEOALPHAMODE                                      0x62DF                   //0x53C0
#define MXV_ID_VIDEOASPECTRATIO                                    0x63C2                   //0x54B3
#define MXV_ID_VIDEOCOLORSPACE                                     0x3FC433                 //0x2EB524
#define MXV_ID_VIDEOCOLOR                                          0x64CF                   //0x55B0

#define MXV_ID_VIDEOCOLORMATRIXCOEFF                               0x64C0                   //0x55B1
#define MXV_ID_VIDEOCOLORBITSPERCHANNEL                            0x64C1                   //0x55B2
#define MXV_ID_VIDEOCOLORCHROMASUBHORZ                             0x64C2                   //0x55B3
#define MXV_ID_VIDEOCOLORCHROMASUBVERT                             0x64C3                   //0x55B4
#define MXV_ID_VIDEOCOLORCBSUBHORZ                                 0x64C4                   //0x55B5
#define MXV_ID_VIDEOCOLORCBSUBVERT                                 0x64C5                   //0x55B6
#define MXV_ID_VIDEOCOLORCHROMASITINGHORZ                          0x64C6                   //0x55B7
#define MXV_ID_VIDEOCOLORCHROMASITINGVERT                          0x64C7                   //0x55B8
#define MXV_ID_VIDEOCOLORRANGE                                     0x64C8                   //0x55B9
#define MXV_ID_VIDEOCOLORTRANSFERCHARACTERISTICS                   0x64C9                   //0x55BA

#define MXV_ID_VIDEOCOLORPRIMARIES                                 0x64CA                   //0x55BB
#define MXV_ID_VIDEOCOLORMAXCLL                                    0x64CB                   //0x55BC
#define MXV_ID_VIDEOCOLORMAXFALL                                   0x64CC                   //0x55BD

#define MXV_ID_VIDEOCOLORMASTERINGMETA                             0x64EF                   //0x55D0
#define MXV_ID_VIDEOCOLOR_RX                                       0x64E0                   //0x55D1
#define MXV_ID_VIDEOCOLOR_RY                                       0x64E1                   //0x55D2
#define MXV_ID_VIDEOCOLOR_GX                                       0x64E2                   //0x55D3
#define MXV_ID_VIDEOCOLOR_GY                                       0x64E3                   //0x55D4
#define MXV_ID_VIDEOCOLOR_BX                                       0x64E4                   //0x55D5
#define MXV_ID_VIDEOCOLOR_BY                                       0x64E5                   //0x55D6
#define MXV_ID_VIDEOCOLOR_WHITEX                                   0x64E6                   //0x55D7
#define MXV_ID_VIDEOCOLOR_WHITEY                                   0x64E7                   //0x55D8
#define MXV_ID_VIDEOCOLOR_LUMINANCEMAX                             0x64E8                   //0x55D9
#define MXV_ID_VIDEOCOLOR_LUMINANCEMIN                             0x64E9                   //0x55DA

#define MXV_ID_VIDEOPROJECTION                                     0x458F                   //0x7670
#define MXV_ID_VIDEOPROJECTIONTYPE                                 0x4580                   //0x7671
#define MXV_ID_VIDEOPROJECTIONPRIVATE                              0x4581                   //0x7672
#define MXV_ID_VIDEOPROJECTIONPOSEYAW                              0x4582                   //0x7673
#define MXV_ID_VIDEOPROJECTIONPOSEPITCH                            0x4583                   //0x7674
#define MXV_ID_VIDEOPROJECTIONPOSEROLL                             0x4584                   //0x7675

/* IDs in the trackaudio master */
#define MXV_ID_AUDIOSAMPLINGFREQ                                   0xC4                     //0xB5
#define MXV_ID_AUDIOOUTSAMPLINGFREQ                                0x47C4                   //0x78B5

#define MXV_ID_AUDIOBITDEPTH                                       0x7173                   //0x6264
#define MXV_ID_AUDIOCHANNELS                                       0xAE                     //0x9F

/* IDs in the content encoding master */
#define MXV_ID_ENCODINGORDER                                       0x6F20                   //0x5031
#define MXV_ID_ENCODINGSCOPE                                       0x6F21                   //0x5032
#define MXV_ID_ENCODINGTYPE                                        0x6F22                   //0x5033
#define MXV_ID_ENCODINGCOMPRESSION                                 0x6F23                   //0x5034
#define MXV_ID_ENCODINGCOMPALGO                                    0x5163                   //0x4254
#define MXV_ID_ENCODINGCOMPSETTINGS                                0x5164                   //0x4255

#define MXV_ID_ENCODINGENCRYPTION                                  0x6F44                   //0x5035
#define MXV_ID_ENCODINGENCAESSETTINGS                              0x56D6                   //0x47E7
#define MXV_ID_ENCODINGENCALGO                                     0x56D0                   //0x47E1
#define MXV_ID_ENCODINGENCKEYID                                    0x56D1                   //0x47E2
#define MXV_ID_ENCODINGSIGALGO                                     0x56D4                   //0x47E5
#define MXV_ID_ENCODINGSIGHASHALGO                                 0x56D5                   //0x47E6
#define MXV_ID_ENCODINGSIGKEYID                                    0x56D3                   //0x47E4
#define MXV_ID_ENCODINGSIGNATURE                                   0x56D2                   //0x47E3


/* ID in the encoding aes setting master*/
#define MXV_ID_ENCODINGENCAESSettingsCipherMode                    0x56F7                   //0x47E8

/* ID in the cues master */
#define MXV_ID_POINTENTRY                                          0xCA                     //0xBB

/* IDs in the pointentry master */
#define MXV_ID_CUETIME                                             0xC2                     //0xB3
#define MXV_ID_CUETRACKPOSITION                                    0xC6                     //0xB7

/* IDs in the cuetrackposition master */
#define MXV_ID_CUETRACK                                            0x86                     //0xF7
#define MXV_ID_CUECLUSTERPOSITION                                  0x80                     //0xF1
#define MXV_ID_CUERELATIVEPOSITION                                 0x8F                     //0xF0
#define MXV_ID_CUEDURATION                                         0xC1                     //0xB2
#define MXV_ID_CUEBLOCKNUMBER                                      0x6287                   //0x5378

/* IDs in the tags master */
#define MXV_ID_TAG                                                 0x4282                   //0x7373
#define MXV_ID_SIMPLETAG                                           0x76D7                   //0x67C8
#define MXV_ID_TAGNAME                                             0x54B2                   //0x45A3
#define MXV_ID_TAGSTRING                                           0x5396                   //0x4487
#define MXV_ID_TAGLANG                                             0x5389                   //0x447A
#define MXV_ID_TAGDEFAULT                                          0x5393                   //0x4484
#define MXV_ID_TAGDEFAULT_BUG                                      0x53C3                   //0x44B4
#define MXV_ID_TAGTARGETS                                          0x72DF                   //0x63C0
#define MXV_ID_TAGTARGETS_TYPE                                     0x72D9                   //0x63CA
#define MXV_ID_TAGTARGETS_TYPEVALUE                                0x77D9                   //0x68CA
#define MXV_ID_TAGTARGETS_TRACKUID                                 0x72D4                   //0x63C5
#define MXV_ID_TAGTARGETS_CHAPTERUID                               0x72D3                   //0x63C4
#define MXV_ID_TAGTARGETS_ATTACHUID                                0x72D5                   //0x63C6

/* IDs in the seekhead master */
#define MXV_ID_SEEKENTRY                                           0x5CCA                   //0x4DBB

/* IDs in the seekpoint master */
#define MXV_ID_SEEKID                                              0x62BA                   //0x53AB
#define MXV_ID_SEEKPOSITION                                        0x62BB                   //0x53AC

/* IDs in the cluster master */
#define MXV_ID_CLUSTERTIMECODE                                     0xF6                     //0xE7
#define MXV_ID_CLUSTERPOSITION                                     0xB6                     //0xA7
#define MXV_ID_CLUSTERPREVSIZE                                     0xBA                     //0xAB
#define MXV_ID_BLOCKGROUP                                          0xBF                     //0xA0
#define MXV_ID_BLOCKADDITIONS                                      0x44B0                   //0x75A1
#define MXV_ID_BLOCKMORE                                           0xB5                     //0xA6
#define MXV_ID_BLOCKADDID                                          0xFD                     //0xEE
#define MXV_ID_BLOCKADDITIONAL                                     0xB4                     //0xA5
#define MXV_ID_SIMPLEBLOCK                                         0xB2                     //0xA3

/* IDs in the blockgroup master */
#define MXV_ID_BLOCK                                               0xB0                     //0xA1
#define MXV_ID_BLOCKDURATION                                       0xAA                     //0x9B
#define MXV_ID_BLOCKREFERENCE                                      0x8A                     //0xFB
#define MXV_ID_CODECSTATE                                          0xB3                     //0xA4
#define MXV_ID_DISCARDPADDING                                      0x44B1                   //0x75A2

/* IDs in the attachments master */
#define MXV_ID_ATTACHEDFILE                                        0x70b6                   //0x61A7
#define MXV_ID_FILEDESC                                            0x558D                   //0x467E
#define MXV_ID_FILENAME                                            0x557D                   //0x466E
#define MXV_ID_FILEMIMETYPE                                        0x557F                   //0x4660
#define MXV_ID_FILEDATA                                            0x556B                   //0x465C
#define MXV_ID_FILEUID                                             0x55BD                   //0x46AE

/* IDs in the chapters master */
#define MXV_ID_EDITIONENTRY                                        0x54CA                   //0x45B9
#define MXV_ID_CHAPTERATOM                                         0xC5                     //0xB6
#define MXV_ID_CHAPTERTIMESTART                                    0xA0                     //0x91
#define MXV_ID_CHAPTERTIMEEND                                      0xA1                     //0x92
#define MXV_ID_CHAPTERDISPLAY                                      0x9F                     //0x80
#define MXV_ID_CHAPSTRING                                          0x94                     //0x85
#define MXV_ID_CHAPLANG                                            0x528B                   //0x437C
#define MXV_ID_CHAPCOUNTRY                                         0x528D                   //0x437E
#define MXV_ID_EDITIONUID                                          0x54CB                   //0x45BC
#define MXV_ID_EDITIONFLAGHIDDEN                                   0x54CC                   //0x45BD
#define MXV_ID_EDITIONFLAGDEFAULT                                  0x54EA                   //0x45DB
#define MXV_ID_EDITIONFLAGORDERED                                  0x54EC                   //0x45DD
#define MXV_ID_CHAPTERUID                                          0x42D3                   //0x73C4
#define MXV_ID_CHAPTERFLAGHIDDEN                                   0xA7                     //0x98
#define MXV_ID_CHAPTERFLAGENABLED                                  0x54A7                   //0x4598
#define MXV_ID_CHAPTERPHYSEQUIV                                    0x72D2                   //0x63C3

typedef enum {
  MXV_TRACK_TYPE_NONE     = 0x0,
  MXV_TRACK_TYPE_VIDEO    = 0x1,
  MXV_TRACK_TYPE_AUDIO    = 0x2,
  MXV_TRACK_TYPE_COMPLEX  = 0x3,
  MXV_TRACK_TYPE_LOGO     = 0x10,
  MXV_TRACK_TYPE_SUBTITLE = 0x11,
  MXV_TRACK_TYPE_CONTROL  = 0x20,
  MXV_TRACK_TYPE_METADATA = 0x21,
} MXVTrackType;

typedef enum {
  MXV_TRACK_ENCODING_TYPE_COMPRESSION = 0,
  MXV_TRACK_ENCODING_TYPE_ENCRYPTION  = 1,
} MXVTrackEncodingType;

typedef enum {
  MXV_TRACK_ENCODING_SCOPE_ALL           = 1,
  MXV_TRACK_ENCODING_SCOPE_TRACKPRIVATE  = 2,
  MXV_TRACK_ENCODING_SCOPE_NEXTENCODING  = 4,
} MXVTrackEncodingScope;

typedef enum {
  MXV_TRACK_ENCODING_ENC_NONE      = 0,
  MXV_TRACK_ENCODING_ENC_DES       = 1,
  MXV_TRACK_ENCODING_ENC_TRIPLEDES = 2,
  MXV_TRACK_ENCODING_ENC_TWOFISH   = 3,
  MXV_TRACK_ENCODING_ENC_BLOWFISH  = 4,
  MXV_TRACK_ENCODING_ENC_AES       = 5,
} MXVTrackEncodingENCAlgo;

typedef enum {
  MXV_TRACK_ENCODING_ENC_AES_SETTINNG_CTR = 0,
  MXV_TRACK_ENCODING_ENC_AES_SETTINNG_CBC = 1,
} MXVTrackEncodingAESSetting;

typedef enum {
  MXV_TRACK_ENCODING_ENC_CONTENT_SIGN_NONE = 0,
  MXV_TRACK_ENCODING_ENC_CONTENT_SIGN_RSA  = 1,
} MXVTrackEncodingENCContentSignAlgo;

typedef enum {
  MXV_TRACK_ENCODING_ENC_CONTENT_SIGN_HASH_NONE = 0,
  MXV_TRACK_ENCODING_ENC_CONTENT_SIGN_HASH_SHA1 = 1,
  MXV_TRACK_ENCODING_ENC_CONTENT_SIGN_HASH_MD5  = 2,
} MXVTrackEncodingENCContentSignHashAlgo;

typedef enum {
  MXV_TRACK_ENCODING_COMP_ZLIB        = 0,
  MXV_TRACK_ENCODING_COMP_BZLIB       = 1,
  MXV_TRACK_ENCODING_COMP_LZO         = 2,
  MXV_TRACK_ENCODING_COMP_HEADERSTRIP = 3,
} MXVTrackEncodingCompAlgo;

typedef enum {
    MXV_VIDEO_INTERLACE_FLAG_UNDETERMINED = 0,
    MXV_VIDEO_INTERLACE_FLAG_INTERLACED   = 1,
    MXV_VIDEO_INTERLACE_FLAG_PROGRESSIVE  = 2
} MXVVideoInterlaceFlag;

typedef enum {
    MXV_VIDEO_FIELDORDER_PROGRESSIVE  = 0,
    MXV_VIDEO_FIELDORDER_UNDETERMINED = 2,
    MXV_VIDEO_FIELDORDER_TT           = 1,
    MXV_VIDEO_FIELDORDER_BB           = 6,
    MXV_VIDEO_FIELDORDER_TB           = 9,
    MXV_VIDEO_FIELDORDER_BT           = 14,
} MXVVideoFieldOrder;

typedef enum {
  MXV_VIDEO_STEREOMODE_TYPE_MONO               = 0,
  MXV_VIDEO_STEREOMODE_TYPE_LEFT_RIGHT         = 1,
  MXV_VIDEO_STEREOMODE_TYPE_BOTTOM_TOP         = 2,
  MXV_VIDEO_STEREOMODE_TYPE_TOP_BOTTOM         = 3,
  MXV_VIDEO_STEREOMODE_TYPE_CHECKERBOARD_RL    = 4,
  MXV_VIDEO_STEREOMODE_TYPE_CHECKERBOARD_LR    = 5,
  MXV_VIDEO_STEREOMODE_TYPE_ROW_INTERLEAVED_RL = 6,
  MXV_VIDEO_STEREOMODE_TYPE_ROW_INTERLEAVED_LR = 7,
  MXV_VIDEO_STEREOMODE_TYPE_COL_INTERLEAVED_RL = 8,
  MXV_VIDEO_STEREOMODE_TYPE_COL_INTERLEAVED_LR = 9,
  MXV_VIDEO_STEREOMODE_TYPE_ANAGLYPH_CYAN_RED  = 10,
  MXV_VIDEO_STEREOMODE_TYPE_RIGHT_LEFT         = 11,
  MXV_VIDEO_STEREOMODE_TYPE_ANAGLYPH_GREEN_MAG = 12,
  MXV_VIDEO_STEREOMODE_TYPE_BOTH_EYES_BLOCK_LR = 13,
  MXV_VIDEO_STEREOMODE_TYPE_BOTH_EYES_BLOCK_RL = 14,
  MXV_VIDEO_STEREOMODE_TYPE_NB,
} MXVVideoStereoModeType;

typedef enum {
  MXV_VIDEO_DISPLAYUNIT_PIXELS      = 0,
  MXV_VIDEO_DISPLAYUNIT_CENTIMETERS = 1,
  MXV_VIDEO_DISPLAYUNIT_INCHES      = 2,
  MXV_VIDEO_DISPLAYUNIT_DAR         = 3,
  MXV_VIDEO_DISPLAYUNIT_UNKNOWN     = 4,
} MXVVideoDisplayUnit;

typedef enum {
  MXV_COLOUR_CHROMASITINGHORZ_UNDETERMINED     = 0,
  MXV_COLOUR_CHROMASITINGHORZ_LEFT             = 1,
  MXV_COLOUR_CHROMASITINGHORZ_HALF             = 2,
  MXV_COLOUR_CHROMASITINGHORZ_NB
} MXVColourChromaSitingHorz;

typedef enum {
  MXV_COLOUR_CHROMASITINGVERT_UNDETERMINED     = 0,
  MXV_COLOUR_CHROMASITINGVERT_TOP              = 1,
  MXV_COLOUR_CHROMASITINGVERT_HALF             = 2,
  MXV_COLOUR_CHROMASITINGVERT_NB
} MXVColourChromaSitingVert;

typedef enum {
  MXV_VIDEO_PROJECTION_TYPE_RECTANGULAR        = 0,
  MXV_VIDEO_PROJECTION_TYPE_EQUIRECTANGULAR    = 1,
  MXV_VIDEO_PROJECTION_TYPE_CUBEMAP            = 2,
  MXV_VIDEO_PROJECTION_TYPE_MESH               = 3,
} MXVVideoProjectionType;

/*
 * MXV Codec IDs, strings
 */

typedef struct CodecTags{
    char str[22];
    enum AVCodecID id;
}CodecTags;

/* max. depth in the EBML tree structure */
#define EBML_MAX_DEPTH 16

#define MXV_VIDEO_STEREO_PLANE_COUNT  3

extern const CodecTags ff_mxv_codec_tags[];
//extern const CodecTags ff_webm_codec_tags[];
extern const CodecMime ff_mxv_mime_tags[];
extern const CodecMime ff_mxv_image_mime_tags[];
extern const AVMetadataConv ff_mxv_metadata_conv[];
extern const char * const ff_mxv_video_stereo_mode[MXV_VIDEO_STEREOMODE_TYPE_NB];
extern const char * const ff_mxv_video_stereo_plane[MXV_VIDEO_STEREO_PLANE_COUNT];

/* AVStream Metadata tag keys for WebM Dash Manifest */
#define INITIALIZATION_RANGE "webm_dash_manifest_initialization_range"
#define CUES_START "webm_dash_manifest_cues_start"
#define CUES_END "webm_dash_manifest_cues_end"
#define FILENAME "webm_dash_manifest_file_name"
#define BANDWIDTH "webm_dash_manifest_bandwidth"
#define DURATION "webm_dash_manifest_duration"
#define CLUSTER_KEYFRAME "webm_dash_manifest_cluster_keyframe"
#define CUE_TIMESTAMPS "webm_dash_manifest_cue_timestamps"
#define TRACK_NUMBER "webm_dash_manifest_track_number"
#define CODEC_PRIVATE_SIZE "webm_dash_manifest_codec_priv_size"

#define TRACK_ENCRYPTION_KEY_SIZE (16)

int ff_mxv_stereo3d_conv(AVStream *st, MXVVideoStereoModeType stereo_mode);
void ff_mxv_generate_aes_key(uint8_t *key, int key_size);
void ff_mxv_encrypt_aes128(uint8_t *output, const uint8_t *key, const uint8_t *input, int size);
void ff_mxv_decrypt_aes128(uint8_t *output, const uint8_t *key, const uint8_t *input, int size);
void printBuffer( const uint8_t* buffer, int size );

#endif /* mxv_h */
