/*
 * Copyright (c) 2019 zheng.lin <zheng.lin@mxplayer.in>
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
#include "smb2/smb2.h"
#include "smb2/libsmb2.h"
#include "smb2/libsmb2-raw.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "urldecode.h"
#include "avformat.h"
#include "internal.h"
#include "url.h"

typedef struct {
    const AVClass *class;
    struct smb2_context *smb2;
    struct smb2_url *url;
    struct smb2fh *fh;
    uint32_t max_read_size;
    int64_t filesize;
    struct smb2dir *dir;
    struct smb2dirent *ent;
    int     status;
    uint8_t connected;
    uint8_t is_finished;
    int64_t bytes_read;
    int64_t bytes_written;
    int trunc;
    int timeout;
    char *user;
    char *password;
    char *workgroup;
} LIBSMB2Context;

static int wait_for_reply(LIBSMB2Context *libsmb2)
{
    libsmb2->is_finished = 0;
    int64_t time_elapsed = 0;
    while ( ( 0 == libsmb2->status ) && !libsmb2->is_finished) {
        struct pollfd pfd;
        pfd.fd = smb2_get_fd(libsmb2->smb2);
        pfd.events = smb2_which_events(libsmb2->smb2);

        if (poll(&pfd, 1, 1000) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Poll failed\n");
            return -1;
        }
        time_elapsed += 1000;
        if (pfd.revents == 0) {
            if (libsmb2->timeout != -1 && time_elapsed >= libsmb2->timeout) {
                return AVERROR(ETIMEDOUT);
            }
            continue;
        }
        if (smb2_service(libsmb2->smb2, pfd.revents) < 0) {
            av_log(NULL, AV_LOG_ERROR, "smb2_service failed with : %s\n", smb2_get_error(libsmb2->smb2));
            return -1;
        }
    }
    return libsmb2->status;
}

static void generic_callback(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    LIBSMB2Context* libsmb2 = private_data;
    if (libsmb2) {
        if (status < 0) {
            libsmb2->status = status;
        } else {
            libsmb2->is_finished = 1;
        }
    }
}

static void open_callback(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    LIBSMB2Context* libsmb2 = private_data;
    if (libsmb2) {
        if (status < 0) {
            libsmb2->status = status;
        } else {
            libsmb2->is_finished = 1;
            libsmb2->fh = command_data;
        }
    }
}

static void read_callback(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    LIBSMB2Context* libsmb2 = private_data;
    if (libsmb2) {
        if (status < 0) {
            libsmb2->status = status;
        } else {
            libsmb2->is_finished = 1;
            libsmb2->bytes_read = status;
        }
    }
}

static void write_callback(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    LIBSMB2Context* libsmb2 = private_data;
    if (libsmb2) {
        if (status < 0) {
            libsmb2->status = status;
        } else {
            libsmb2->is_finished = 1;
            libsmb2->bytes_written = status;
        }
    }
}

static void opendir_callback(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    LIBSMB2Context* libsmb2 = private_data;
    if (libsmb2) {
        if (status < 0) {
            libsmb2->status = status;
        } else {
            libsmb2->is_finished = 1;
             libsmb2->dir = command_data;
        }
    }
}

static av_cold int libsmb2_close(URLContext *h)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    if (libsmb2->smb2 != NULL) {
        if (libsmb2->fh != NULL) {
            smb2_close_async(libsmb2->smb2, libsmb2->fh, generic_callback, libsmb2);
            wait_for_reply(libsmb2);
            libsmb2->fh = NULL;
        }

        if (libsmb2->dir != NULL) {
            smb2_closedir(libsmb2->smb2, libsmb2->dir);
            libsmb2->dir = NULL;
        }

        if (libsmb2->connected) {
            smb2_disconnect_share_async(libsmb2->smb2, generic_callback, libsmb2);
            wait_for_reply(libsmb2);
            libsmb2->connected = 0;
        }

        smb2_destroy_context(libsmb2->smb2);
        libsmb2->smb2 = NULL;
    }

    if (libsmb2->url != NULL) {
        smb2_destroy_url(libsmb2->url);
        libsmb2->url = NULL;
    }
    return 0;
}

static av_cold int libsmb2_connect(URLContext *h)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    int ret = -1;
    const char* user = NULL;
    const char* password = NULL;
    const char* share = NULL;
    libsmb2->smb2 = smb2_init_context();
    if (!libsmb2->smb2) {
        av_log(h, AV_LOG_ERROR, "Failed to init context for smb2.\n");
        return ret;
    }

    libsmb2->url = smb2_parse_url(libsmb2->smb2, h->filename);
    if (libsmb2->url == NULL) {
        av_log(h, AV_LOG_ERROR, "Failed to parse url: %s\n",smb2_get_error(libsmb2->smb2));
        goto fail;
    }

    if (libsmb2->url->user) {
        user = ff_urldecode(libsmb2->url->user);
        password = ff_urldecode(libsmb2->url->password);
    } else if (libsmb2->user) {
        user = ff_urldecode(libsmb2->user);
        password = ff_urldecode(libsmb2->password);
    } else {
        user = av_strdup("Guest");
        password = av_strdup("");
    }
    smb2_set_user(libsmb2->smb2, user);
    smb2_set_password(libsmb2->smb2, password);
    if (libsmb2->url->domain) {
        smb2_set_domain(libsmb2->smb2, libsmb2->url->domain);
    } else if (libsmb2->workgroup) {
        smb2_set_domain(libsmb2->smb2, libsmb2->workgroup);
    }
    smb2_set_security_mode(libsmb2->smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);

    share = ff_urldecode(libsmb2->url->share);
    ff_dlog(h, "domain=%s server=%s share=%s user=%s\n", libsmb2->url->domain, libsmb2->url->server, share, user);
    ret = smb2_connect_share_async(libsmb2->smb2, libsmb2->url->server, share, user, generic_callback, libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "smb2_connect_share_async failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }
    ret = wait_for_reply(libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }
    libsmb2->connected = 1;
fail:
    if (user)
        av_freep(&user);
    if (password)
        av_freep(&password);
    if (share)
        av_freep(&share);
    return ret;
}

static av_cold int libsmb2_open(URLContext *h, const char *url, int flags)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    int access, ret;
    const char* path = NULL;
    if ((ret = libsmb2_connect(h)) < 0) {
        goto fail;
    }

    if ((flags & AVIO_FLAG_WRITE) && (flags & AVIO_FLAG_READ)) {
        access = O_CREAT | O_RDWR;
    } else if (flags & AVIO_FLAG_WRITE) {
        access = O_CREAT | O_WRONLY;
    } else
        access = O_RDONLY;

    path = ff_urldecode(libsmb2->url->path);
    ret = smb2_open_async(libsmb2->smb2, path, access, open_callback, libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "smb2_open_async failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }
    ret = wait_for_reply(libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }

    struct smb2_stat_64 st;
    ret = smb2_fstat_async(libsmb2->smb2, libsmb2->fh, &st, generic_callback, libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "smb2_fstat_async failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }
    ret = wait_for_reply(libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }

    libsmb2->filesize = st.smb2_size;
    libsmb2->max_read_size = smb2_get_max_read_size(libsmb2->smb2);
    if (path)
        av_freep(&path);
    return 0;
fail:
    if (path)
        av_freep(&path);
    libsmb2_close(h);
    return ret;
}

static int64_t libsmb2_seek(URLContext *h, int64_t pos, int whence)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    if (whence == AVSEEK_SIZE) {
        if (libsmb2->filesize == -1) {
            av_log(h, AV_LOG_ERROR, "Error during seeking: filesize is unknown.\n");
            return AVERROR(EIO);
        } else
            return libsmb2->filesize;
    }

    uint64_t current_offset;
    if (smb2_lseek(libsmb2->smb2, libsmb2->fh, pos, whence, &current_offset) < 0) {
        av_log(h, AV_LOG_ERROR, "smb2_lseek failed. %s\n", smb2_get_error(libsmb2->smb2));
        return AVERROR(errno);
    }
    return current_offset;
}

static int libsmb2_read(URLContext *h, unsigned char *buf, int size)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    int ret = smb2_read_async(libsmb2->smb2, libsmb2->fh, buf, FFMIN(libsmb2->max_read_size, size), read_callback, libsmb2);
    if (0 != ret) {
        av_log(h, AV_LOG_ERROR, "smb2_read_async failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }
    ret = wait_for_reply(libsmb2);
    if (0 != ret) {
        av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2)); 
        goto fail;
    }
    return libsmb2->bytes_read ? libsmb2->bytes_read : AVERROR_EOF;
fail:
    return ret;
}

static int libsmb2_write(URLContext *h, const unsigned char *buf, int size)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    int ret = smb2_write_async(libsmb2->smb2, libsmb2->fh, (uint8_t*)buf, size, write_callback, libsmb2);
    if (0 != ret) {
        av_log(h, AV_LOG_ERROR, "smb2_write_async failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }
    ret = wait_for_reply(libsmb2);
    if (0 != ret) {
        av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2)); 
        goto fail;
    }
    return libsmb2->bytes_written;
fail:
    return ret;
}

static int libsmb2_open_dir(URLContext *h)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    int ret;
    const char* path = NULL;

    if ((ret = libsmb2_connect(h)) < 0) {
        goto fail;
    }

    path = ff_urldecode(libsmb2->url->path);
    ret = smb2_opendir_async(libsmb2->smb2, path, opendir_callback, libsmb2);
    if (0 != ret) {
        av_log(h, AV_LOG_ERROR, "smb2_opendir_async failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto fail;
    }
    ret = wait_for_reply(libsmb2);
    if (0 != ret) {
        av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2)); 
        goto fail;
    }
    if (path)
        av_freep(&path);
    return 0;
fail:
    if (path)
        av_freep(&path);
    libsmb2_close(h);
    return ret;
}

static int libsmb2_read_dir(URLContext *h, AVIODirEntry **next)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    AVIODirEntry *entry;
    struct smb2dirent *ent = NULL;
    char *url = NULL;

    *next = entry = ff_alloc_dir_entry();
    if (!entry)
        return AVERROR(ENOMEM);

    do {
        ent = smb2_readdir(libsmb2->smb2, libsmb2->dir);
        if (!ent) {
            av_freep(next);
            return 0;
        }
        switch (ent->st.smb2_type) {
        case SMB2_TYPE_DIRECTORY:
            entry->type = AVIO_ENTRY_DIRECTORY;
            break;
        case SMB2_TYPE_FILE:
            entry->type = AVIO_ENTRY_FILE;
            break;
        default:
            entry->type = AVIO_ENTRY_UNKNOWN;
            break;
        }
    } while (strcmp(ent->name, ".") ||
             !strcmp(ent->name, ".."));

    entry->name = av_strdup(ent->name);
    if (!entry->name) {
        av_freep(next);
        return AVERROR(ENOMEM);
    }

    url = av_append_path_component(h->filename, ent->name);
    if (url) {
        struct smb2_stat_64 st;
        int ret = smb2_stat_async(libsmb2->smb2, url, &st, generic_callback, libsmb2);
        if (0 == ret ) {
            ret = wait_for_reply(libsmb2);
            if (0 == ret) {
                entry->size = st.smb2_size;
                entry->modification_timestamp = INT64_C(1000000) * st.smb2_mtime;
                entry->access_timestamp =  INT64_C(1000000) * st.smb2_atime;
                entry->status_change_timestamp = INT64_C(1000000) * st.smb2_ctime;
            } else {
                av_log(h, AV_LOG_ERROR, "wait_for_reply(%s) failed. %s\n", url, smb2_get_error(libsmb2->smb2)); 
            }
        } else {
            av_log(h, AV_LOG_ERROR, "smb2_fstat_async(%s) failed. %s\n", url, smb2_get_error(libsmb2->smb2)); 
        }
        av_free(url);
    }

    return 0;
}

static int libsmb2_close_dir(URLContext *h)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    if (libsmb2->dir != NULL) {
        smb2_closedir(libsmb2->smb2, libsmb2->dir);
        libsmb2->dir = NULL;
    }
    libsmb2_close(h);
    return 0;
}

static int libsmb2_delete(URLContext *h)
{
    LIBSMB2Context *libsmb2 = h->priv_data;
    int ret;
    const char* path = NULL;
    struct smb2_stat_64 st;

    if ((ret = libsmb2_connect(h)) < 0)
        goto cleanup;

    path = ff_urldecode(libsmb2->url->path);
    ret = smb2_open_async(libsmb2->smb2, path, O_WRONLY, open_callback, libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "smb2_open_async failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto cleanup;
    }
    ret = wait_for_reply(libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2)); 
        goto cleanup;
    }

    ret = smb2_fstat_async(libsmb2->smb2, libsmb2->fh, &st, generic_callback, libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "smb2_fstat_async failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto cleanup;
    }
    ret = wait_for_reply(libsmb2);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2));
        goto cleanup;
    }

    if (SMB2_TYPE_DIRECTORY == st.smb2_type) {
        ret = smb2_rmdir_async(libsmb2->smb2, h->filename, generic_callback, libsmb2);
        if (ret != 0) {
            av_log(h, AV_LOG_ERROR, "smb2_rmdir_async failed. %s\n", smb2_get_error(libsmb2->smb2));
            goto cleanup;
        }
        ret = wait_for_reply(libsmb2);
        if (ret != 0) {
            av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2));
            goto cleanup;
        }
    } else {
        ret = smb2_unlink_async(libsmb2->smb2, h->filename, generic_callback, libsmb2);
        if (ret != 0) {
            av_log(h, AV_LOG_ERROR, "smb2_unlink_async failed. %s\n", smb2_get_error(libsmb2->smb2)); 
            goto cleanup;
        }
        ret = wait_for_reply(libsmb2);
        if (ret != 0) {
            av_log(h, AV_LOG_ERROR, "wait_for_reply failed. %s\n", smb2_get_error(libsmb2->smb2)); 
            goto cleanup;
        }
    }
    ret = 0;
cleanup:
    if (path)
        av_freep(&path);
    libsmb2_close(h);
    return ret;
}

static int libsmb2_move(URLContext *h_src, URLContext *h_dst)
{
    LIBSMB2Context *libsmb2 = h_src->priv_data;
    int ret;

    if ((ret = libsmb2_connect(h_src)) < 0)
        goto cleanup;

    ret = smb2_rename_async(libsmb2->smb2, h_src->filename, h_dst->filename, generic_callback, libsmb2);
    if (0 != ret) {
        goto cleanup;
    }
    ret = wait_for_reply(libsmb2);
    if (0 != ret) {
        goto cleanup;
    }

    ret = 0;

cleanup:
    libsmb2_close(h_src);
    return ret;
}

#define OFFSET(x) offsetof(LIBSMB2Context, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"timeout",   "set timeout in ms of socket I/O operations",    OFFSET(timeout), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, D|E },
    {"truncate",  "truncate existing files on write",              OFFSET(trunc),   AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, E },
    {"user",      "set the user name used for making connections", OFFSET(user), AV_OPT_TYPE_STRING, { .str = "Guest" }, 0, 0, D|E },
    {"password",  "set the password used for making connections",  OFFSET(password), AV_OPT_TYPE_STRING, { .str = "" }, 0, 0, D|E },
    {"workgroup", "set the workgroup used for making connections", OFFSET(workgroup), AV_OPT_TYPE_STRING, { 0 }, 0, 0, D|E },
    {NULL}
};

static const AVClass libsmb2lient_context_class = {
    .class_name     = "libsmb2",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_libsmb2_protocol = {
    .name                = "smb",
    .url_open            = libsmb2_open,
    .url_read            = libsmb2_read,
    .url_write           = libsmb2_write,
    .url_seek            = libsmb2_seek,
    .url_close           = libsmb2_close,
    .url_delete          = libsmb2_delete,
    .url_move            = libsmb2_move,
    .url_open_dir        = libsmb2_open_dir,
    .url_read_dir        = libsmb2_read_dir,
    .url_close_dir       = libsmb2_close_dir,
    .priv_data_size      = sizeof(LIBSMB2Context),
    .priv_data_class     = &libsmb2lient_context_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
