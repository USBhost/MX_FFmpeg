
/*
 * downloadhttp.c
 *
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


#include <stdio.h>
#include "config.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/parseutils.h"

#include "avformat.h"
#include "internal.h"
#include "os_support.h"
#include "url.h"
#include "downloadhttp_wrap.h"

/* XXX: POST protocol is not completely implemented because ffmpeg uses
 * only a subset of it. */

/* The IO buffer size is unrelated to the max URL size in itself, but needs
 * to be large enough to fit the full request headers (including long
 * path names). */
#define BUFFER_SIZE   MAX_URL_SIZE
#define MAX_REDIRECTS 8
#define HTTP_SINGLE   1
#define HTTP_MUTLI    2
#define MAX_EXPIRY    19
#define WHITESPACES " \n\t\r"

typedef struct HTTPContext {
    const AVClass *class;
    URLContext *hd;
    FILE* file;
    AVDictionary *chained_options;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    /* Used if "Transfer-Encoding: chunked" otherwise -1. */
    uint64_t chunksize;
    int chunkend;
    uint64_t off, end_off, filesize;
    char *location;
    char *http_proxy;
    char *headers;
    char *mime_type;
    char *http_version;
    char *user_agent;
    char *referer;
    char *content_type;
    /* Set if the server correctly handles Connection: close and will close
     * the connection after feeding us the content. */
    int willclose;
    int seekable;           /**< Control seekability, 0 = disable, 1 = enable, -1 = probe. */
    int chunked_post;
    /* A flag which indicates if the end of chunked encoding has been sent. */
    int end_chunked_post;
    /* A flag which indicates we have finished to read POST reply. */
    int end_header;
    /* A flag which indicates if we use persistent connections. */
    int multiple_requests;
    uint8_t *post_data;
    int post_datalen;
    int is_akamai;
    int is_mediagateway;
    char *cookies;          ///< holds newline (\n) delimited Set-Cookie header field values (without the "Set-Cookie: " field name)
    /* A dictionary containing cookies keyed by cookie name */
    AVDictionary *cookie_dict;
    int icy;
    /* how much data was read since the last ICY metadata packet */
    uint64_t icy_data_read;
    /* after how many bytes of read data a new metadata packet will be found */
    uint64_t icy_metaint;
    char *icy_metadata_headers;
    char *icy_metadata_packet;
    AVDictionary *metadata;
    /* -1 = try to send if applicable, 0 = always disabled, 1 = always enabled */
    int send_expect_100;
    char *method;
    int reconnect;
    int reconnect_at_eof;
    int reconnect_streamed;
    int reconnect_delay_max;
    int listen;
    char *resource;
    int reply_code;
    int is_multi_client;
    int is_connected_server;

} HTTPContext;

#define OFFSET(x) offsetof(HTTPContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
#ifdef MXTECHS
#define DEFAULT_USER_AGENT "MXPlayer/1.10 (Linux; Android)"
#else
#define DEFAULT_USER_AGENT "Lavf/" AV_STRINGIFY(LIBAVFORMAT_VERSION)
#endif

static int http_open(URLContext *h, const char *uri, int flags,
                     AVDictionary **options)
{
    HTTPContext *s = h->priv_data;
    int ret;

    s->seekable = 0;
    h->is_streamed = 0;
    h->is_streamed = 1;

    s->location = av_strdup(uri);

    if (options)
        av_dict_copy(&s->chained_options, *options, 0);

    av_log(h, AV_LOG_INFO,
           "download_http_open() open %s %d.\n", uri, __LINE__);

    ret = download_http_open(s, uri + 8, flags);
    if (ret >= 0) {
        fseek(s->file, 0L, SEEK_END);
        s->filesize = ftell(s->file);

        fseek(s->file, 0L, SEEK_SET);

        av_log(h, AV_LOG_INFO,
               "download_http_open() done %s %ld %d. \n", uri, s->filesize, __LINE__);
        return 0;
    } else {
        return ret;
    }
}


static int http_close(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    av_log(h, AV_LOG_INFO,
           "download_http_close() %s %d.\n", s->location, __LINE__);
    int ret = 0;

    ret = download_http_close(s);

    av_dict_free(&s->chained_options);

    return ret;
}

static int64_t http_seek(URLContext *h, int64_t off, int whence)
{
    HTTPContext *s = h->priv_data;
    av_log(h, AV_LOG_INFO,
           "download_http_seek() %s, %ld, %d, %d.\n", s->location, off, whence, __LINE__);

    if (whence == AVSEEK_SIZE)
        return s->filesize;

    int64_t current = ftell(s->file);

    if ((whence == SEEK_CUR && off == 0) ||
              (whence == SEEK_SET && off == current))
        return current;


    if (whence == SEEK_CUR)
        off += current;
    else if (whence == SEEK_END)
        off += s->filesize;
    else if (whence != SEEK_SET)
        return AVERROR(EINVAL);
    if (off < 0)
        return AVERROR(EINVAL);
    current = off;

    int ret = fseek(s->file, current, SEEK_SET);
    if (ret == 0) {
        int64_t c = ftell(s->file);
        av_log(h, AV_LOG_INFO,
               "download_http_seek() done %s, %ld, %d, %ld, %ld, %d.\n", s->location, off, whence, c, current, __LINE__);
        return c;
    }

    return AVERROR(EINVAL);
}

static int http_get_file_handle(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    av_log(h, AV_LOG_INFO,
           "http_get_file_handle() %s %d.\n", s->location, __LINE__);

    return -1;
}

static int http_get_short_seek(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    av_log(h, AV_LOG_INFO,
           "http_get_short_seek() %s %d.\n", s->location, __LINE__);
    return -1;
}

static int http_read(URLContext *h, uint8_t *buf, int size) {
    HTTPContext *s = h->priv_data;

    int64_t current = ftell(s->file);

    av_log(h, AV_LOG_INFO,
           "download_http_read() %d, %ld, %s %d.\n", size, current, s->location, __LINE__);

    if (current == s->filesize) {
        return AVERROR_EOF;
    }

    int64_t remain = s->filesize - current;
    int64_t max_buffer = 1024 * 64;
    if (size > remain) {
        size = remain;
    }

    if (size > max_buffer) {
        size = max_buffer;
    }

    int ret = fread(buf, 1, size, s->file);
    if (ret <= 0) {
        return AVERROR_EOF;
    }
    return ret;
}

#define HTTP_CLASS(flavor)                          \
static const AVClass flavor ## _context_class = {   \
    .class_name = # flavor,                         \
    .item_name  = av_default_item_name,             \
    .version    = LIBAVUTIL_VERSION_INT,            \
}

HTTP_CLASS(download_http);

const URLProtocol ff_download_http_protocol = {
    .name                = "downloadhttp",
    .url_open2           = http_open,
    .url_accept          = 0,
    .url_handshake       = 0,
    .url_read            = http_read,
//    .url_write           = http_write,
//    .url_seek            = http_seek,
    .url_close           = http_close,
//    .url_get_file_handle = http_get_file_handle,
    .priv_data_size      = sizeof(HTTPContext),
    .priv_data_class     = &download_http_context_class,
    .default_whitelist   = "downloadhttp,http,https,tls,rtp,tcp,udp,crypto,httpproxy,data"
};

const URLProtocol ff_download_https_protocol = {
    .name                = "downloadhttps",
    .url_open2           = http_open,
    .url_accept          = 0,
    .url_handshake       = 0,
    .url_read            = http_read,
//    .url_write           = http_write,
//    .url_seek            = http_seek,
    .url_close           = http_close,
//    .url_get_file_handle = http_get_file_handle,
    .priv_data_size      = sizeof(HTTPContext),
    .priv_data_class     = &download_http_context_class,
    .default_whitelist   = "downloadhttps,http,https,tls,rtp,tcp,udp,crypto,httpproxy,data"
};

