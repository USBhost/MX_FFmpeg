/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2016 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

   Portions of this code are copyright 2017 to Primary Data Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef STDC_HEADERS
#include <stddef.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>

#ifndef PS2_IOP_PLATFORM
#include <time.h>
#include <fcntl.h>
#endif

#if !defined(PS2_EE_PLATFORM) && !defined(PS2_IOP_PLATFORM)
#include <sys/socket.h>
#endif

#ifdef _WIN32
#include "asprintf.h"
#endif

#include "compat.h"

#include "sha.h"
#include "sha-private.h"

#include "slist.h"
#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-raw.h"
#include "libsmb2-private.h"
#include "portable-endian.h"
#include "ntlmssp.h"

#ifdef HAVE_LIBKRB5
#include "krb5-wrapper.h"
#endif

#if defined(ESP_PLATFORM)
#define DEFAULT_OUTPUT_BUFFER_LENGTH 512
#elif defined(PS2_EE_PLATFORM) || defined(PS2_IOP_PLATFORM)
#define DEFAULT_OUTPUT_BUFFER_LENGTH 4096
#else
#define DEFAULT_OUTPUT_BUFFER_LENGTH 0xffff
#endif

/* strings used to derive SMB signing and encryption keys */
static const char SMBSigningKey[] = "SMBSigningKey";
static const char SMBC2SCipherKey[] = "SMBC2SCipherKey";
static const char SMBS2CCipherKey[] = "SMBS2CCipherKey";
static const char SMB2AESCMAC[] = "SMB2AESCMAC";
static const char SmbSign[] = "SmbSign";
static const char SMB2AESCCM[] = "SMB2AESCCM";
static const char ServerOut[] = "ServerOut";
static const char ServerIn[] = "ServerIn ";
/* The following strings will be used for deriving other keys
static const char SMB2APP[] = "SMB2APP";
static const char SmbRpc[] = "SmbRpc";
static const char SMBAppKey[] = "SMBAppKey";
*/

#ifndef O_SYNC
#ifndef O_DSYNC
#define O_DSYNC		040000
#endif // !O_DSYNC
#define __O_SYNC	020000000
#define O_SYNC		(__O_SYNC|O_DSYNC)
#endif // !O_SYNC

const smb2_file_id compound_file_id = {
        0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff
};

struct connect_data {
        smb2_command_cb cb;
        void *cb_data;

        const char *server;
        const char *share;
        const char *user;

        /* UNC for the share in utf8 as well as utf16 formats */
        char *utf8_unc;
        struct utf16 *utf16_unc;

        void *auth_data;
};

struct smb2_dirent_internal {
        struct smb2_dirent_internal *next;
        struct smb2dirent dirent;
};

struct smb2dir {
        struct smb2dir *next;
        smb2_command_cb cb;
        void *cb_data;
        smb2_file_id file_id;

        struct smb2_dirent_internal *entries;
        struct smb2_dirent_internal *current_entry;
        int index;
};

struct smb2fh {
        struct smb2fh *next;
        smb2_command_cb cb;
        void *cb_data;

        smb2_file_id file_id;
        int64_t offset;
        int64_t end_of_file;
};

static void
smb2_close_context(struct smb2_context *smb2)
{
        if (smb2 == NULL) {
                return;
        }

        if (smb2->fd != -1) {
                if (smb2->change_fd) {
                        smb2->change_fd(smb2, smb2->fd, SMB2_DEL_FD);
                }
                close(smb2->fd);
                smb2->fd = -1;
        }

        smb2->message_id = 0;
        smb2->session_id = 0;
        smb2->tree_id = 0;
        memset(smb2->signing_key, 0, SMB2_KEY_SIZE);
        if (smb2->session_key) {
                free(smb2->session_key);
                smb2->session_key = NULL;
        }
        smb2->session_key_size = 0;
}

static int
send_session_setup_request(struct smb2_context *smb2,
                           struct connect_data *c_data,
                           unsigned char *buf, int len);

static void
free_smb2dir(struct smb2_context *smb2, struct smb2dir *dir)
{
        SMB2_LIST_REMOVE(&smb2->dirs, dir);
        while (dir->entries) {
                struct smb2_dirent_internal *e = dir->entries->next;

                free(discard_const(dir->entries->dirent.name));
                free(dir->entries);
                dir->entries = e;
        }
        free(dir);
}

void smb2_free_all_dirs(struct smb2_context *smb2)
{
        while (smb2->dirs) {
                free_smb2dir(smb2, smb2->dirs);
        }
}

void
smb2_seekdir(struct smb2_context *smb2, struct smb2dir *dir,
                  long loc)
{
        if (dir == NULL){
                return;
        }
        dir->current_entry = dir->entries;
        dir->index = 0;

        while (dir->current_entry && loc--) {
                dir->current_entry = dir->current_entry->next;
                dir->index++;
        }
}

long
smb2_telldir(struct smb2_context *smb2, struct smb2dir *dir)
{
        if (dir == NULL) {
                return -EINVAL;
        }
        return dir->index;
}

void
smb2_rewinddir(struct smb2_context *smb2,
                    struct smb2dir *dir)
{
        if (dir == NULL) {
                return;
        }
        dir->current_entry = dir->entries;
        dir->index = 0;
}

struct smb2dirent *
smb2_readdir(struct smb2_context *smb2,
             struct smb2dir *dir)
{
        struct smb2dirent *ent;
        if ((dir == NULL) || (dir->current_entry == NULL)) {
                return NULL;
        }

        ent = &dir->current_entry->dirent;
        dir->current_entry = dir->current_entry->next;
        dir->index++;

        return ent;
}

void
smb2_closedir(struct smb2_context *smb2, struct smb2dir *dir)
{
        if ((smb2 == NULL) || (dir == NULL)) {
                return;
        }
        free_smb2dir(smb2, dir);
}

static int
decode_dirents(struct smb2_context *smb2, struct smb2dir *dir,
               struct smb2_iovec *vec)
{
        struct smb2_dirent_internal *ent;
        struct smb2_fileidfulldirectoryinformation fs;
        uint32_t offset = 0;

        do {
                struct smb2_iovec tmp_vec;

                /* Make sure we do not go beyond end of vector */
                if (offset >= vec->len) {
                        smb2_set_error(smb2, "Malformed query reply.");
                        return -1;
                }

                ent = calloc(1, sizeof(struct smb2_dirent_internal));
                if (ent == NULL) {
                        smb2_set_error(smb2, "Failed to allocate "
                                       "dirent_internal");
                        return -1;
                }
                SMB2_LIST_ADD(&dir->entries, ent);


                tmp_vec.buf = &vec->buf[offset];
                tmp_vec.len = vec->len - offset;

                smb2_decode_fileidfulldirectoryinformation(smb2, &fs,
                                                           &tmp_vec);
                /* steal the name */
                ent->dirent.name = fs.name;
                ent->dirent.st.smb2_type = SMB2_TYPE_FILE;
                if (fs.file_attributes & SMB2_FILE_ATTRIBUTE_DIRECTORY) {
                        ent->dirent.st.smb2_type = SMB2_TYPE_DIRECTORY;
                }
                if (fs.file_attributes & SMB2_FILE_ATTRIBUTE_REPARSE_POINT) {
                        ent->dirent.st.smb2_type = SMB2_TYPE_LINK;
                }
                ent->dirent.st.smb2_nlink = 0;
                ent->dirent.st.smb2_ino = fs.file_id;
                ent->dirent.st.smb2_size = fs.end_of_file;
                ent->dirent.st.smb2_atime = fs.last_access_time.tv_sec;
                ent->dirent.st.smb2_atime_nsec = fs.last_access_time.tv_usec * 1000;
                ent->dirent.st.smb2_mtime = fs.last_write_time.tv_sec;
                ent->dirent.st.smb2_mtime_nsec = fs.last_write_time.tv_usec * 1000;
                ent->dirent.st.smb2_ctime = fs.change_time.tv_sec;
                ent->dirent.st.smb2_ctime_nsec = fs.change_time.tv_usec * 1000;
                ent->dirent.st.smb2_btime = fs.creation_time.tv_sec;
                ent->dirent.st.smb2_btime_nsec = fs.creation_time.tv_usec * 1000;

                offset += fs.next_entry_offset;
        } while (fs.next_entry_offset);

        return 0;
}

static void
od_close_cb(struct smb2_context *smb2, int status,
         void *command_data, void *private_data)
{
        struct smb2dir *dir = private_data;

        if (status != SMB2_STATUS_SUCCESS) {
                dir->cb(smb2, -ENOMEM, NULL, dir->cb_data);
                free_smb2dir(smb2, dir);
                return;
        }

        dir->current_entry = dir->entries;
        dir->index = 0;

        /* dir will be freed in smb2_closedir() */
        dir->cb(smb2, 0, dir, dir->cb_data);
}

static void
query_cb(struct smb2_context *smb2, int status,
         void *command_data, void *private_data)
{
        struct smb2dir *dir = private_data;
        struct smb2_query_directory_reply *rep = command_data;

        if (status == SMB2_STATUS_SUCCESS) {
                struct smb2_iovec vec;
                struct smb2_query_directory_request req;
                struct smb2_pdu *pdu;

                vec.buf = rep->output_buffer;
                vec.len = rep->output_buffer_length;

                if (decode_dirents(smb2, dir, &vec) < 0) {
                        dir->cb(smb2, -ENOMEM, NULL, dir->cb_data);
                        free_smb2dir(smb2, dir);
                        return;
                }

                /* We need to get more data */
                memset(&req, 0, sizeof(struct smb2_query_directory_request));
                req.file_information_class = SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION;
                req.flags = 0;
                memcpy(req.file_id, dir->file_id, SMB2_FD_SIZE);
                req.output_buffer_length = DEFAULT_OUTPUT_BUFFER_LENGTH;
                req.name = "*";

                pdu = smb2_cmd_query_directory_async(smb2, &req, query_cb, dir);
                if (pdu == NULL) {
                        dir->cb(smb2, -ENOMEM, NULL, dir->cb_data);
                        free_smb2dir(smb2, dir);
                        return;
                }
                smb2_queue_pdu(smb2, pdu);

                return;
        }

        if (status == SMB2_STATUS_NO_MORE_FILES) {
                struct smb2_close_request req;
                struct smb2_pdu *pdu;

                /* We have all the data */
                memset(&req, 0, sizeof(struct smb2_close_request));
                req.flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
                memcpy(req.file_id, dir->file_id, SMB2_FD_SIZE);

                pdu = smb2_cmd_close_async(smb2, &req, od_close_cb, dir);
                if (pdu == NULL) {
                        dir->cb(smb2, -ENOMEM, NULL, dir->cb_data);
                        free_smb2dir(smb2, dir);
                        return;
                }
                smb2_queue_pdu(smb2, pdu);

                return;
        }

        smb2_set_error(smb2, "Query directory failed with (0x%08x) %s. %s",
                       status, nterror_to_str(status),
                       smb2_get_error(smb2));
        dir->cb(smb2, -nterror_to_errno(status), NULL, dir->cb_data);
        free_smb2dir(smb2, dir);
}

static void
opendir_cb(struct smb2_context *smb2, int status,
           void *command_data, void *private_data)
{
        struct smb2dir *dir = private_data;
        struct smb2_create_reply *rep = command_data;
        struct smb2_query_directory_request req;
        struct smb2_pdu *pdu;

        if (status != SMB2_STATUS_SUCCESS) {
                smb2_set_error(smb2, "Opendir failed with (0x%08x) %s.",
                               status, nterror_to_str(status));
                dir->cb(smb2, -nterror_to_errno(status), NULL, dir->cb_data);
                free_smb2dir(smb2, dir);
                return;
        }

        memcpy(dir->file_id, rep->file_id, SMB2_FD_SIZE);

        memset(&req, 0, sizeof(struct smb2_query_directory_request));
        req.file_information_class = SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION;
        req.flags = 0;
        memcpy(req.file_id, dir->file_id, SMB2_FD_SIZE);
        req.output_buffer_length = DEFAULT_OUTPUT_BUFFER_LENGTH;
        req.name = "*";

        pdu = smb2_cmd_query_directory_async(smb2, &req, query_cb, dir);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create query command.");
                dir->cb(smb2, -ENOMEM, NULL, dir->cb_data);
                free_smb2dir(smb2, dir);
                return;
        }
        smb2_queue_pdu(smb2, pdu);
}

int
smb2_opendir_async(struct smb2_context *smb2, const char *path,
                   smb2_command_cb cb, void *cb_data)
{
        struct smb2_create_request req;
        struct smb2dir *dir;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        if (path == NULL) {
                path = "";
        }

        dir = calloc(1, sizeof(struct smb2dir));
        if (dir == NULL) {
                smb2_set_error(smb2, "Failed to allocate smb2dir.");
                return -EINVAL;
        }
        SMB2_LIST_ADD(&smb2->dirs, dir);
        dir->cb = cb;
        dir->cb_data = cb_data;

        memset(&req, 0, sizeof(struct smb2_create_request));
        req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        req.desired_access = SMB2_FILE_LIST_DIRECTORY | SMB2_FILE_READ_ATTRIBUTES;
        req.file_attributes = SMB2_FILE_ATTRIBUTE_DIRECTORY;
        req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE;
        req.create_disposition = SMB2_FILE_OPEN;
        req.create_options = SMB2_FILE_DIRECTORY_FILE;
        req.name = path;

        pdu = smb2_cmd_create_async(smb2, &req, opendir_cb, dir);
        if (pdu == NULL) {
                free_smb2dir(smb2, dir);
                smb2_set_error(smb2, "Failed to create opendir command.");
                return -EINVAL;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

static void
free_c_data(struct smb2_context *smb2, struct connect_data *c_data)
{
        if (c_data->auth_data) {
                if (smb2->sec == SMB2_SEC_NTLMSSP) {
                        ntlmssp_destroy_context(c_data->auth_data);
                }
#ifdef HAVE_LIBKRB5
                else {
                        krb5_free_auth_data(c_data->auth_data);
                }
#endif
        }

        free(c_data->utf8_unc);
        free(c_data->utf16_unc);
        free(discard_const(c_data->server));
        free(discard_const(c_data->share));
        free(discard_const(c_data->user));
        free(c_data);
}


static void
tree_connect_cb(struct smb2_context *smb2, int status,
                void *command_data, void *private_data)
{
        struct connect_data *c_data = private_data;

        if (status != SMB2_STATUS_SUCCESS) {
                smb2_close_context(smb2);
                smb2_set_error(smb2, "Tree Connect failed with (0x%08x) %s. %s",
                               status, nterror_to_str(status),
                               smb2_get_error(smb2));
                c_data->cb(smb2, -nterror_to_errno(status), NULL, c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }

        c_data->cb(smb2, 0, NULL, c_data->cb_data);
        free_c_data(smb2, c_data);
}

void smb2_derive_key(
    uint8_t     *derivation_key,
    uint32_t    derivation_key_len,
    const char  *label,
    uint32_t    label_len,
    const char  *context,
    uint32_t    context_len,
    uint8_t     derived_key[SMB2_KEY_SIZE]
    )
{
        unsigned char nul = 0;
        const uint32_t counter = htobe32(1);
        const uint32_t keylen = htobe32(SMB2_KEY_SIZE * 8);
        uint8_t input_key[SMB2_KEY_SIZE] = {0};
        HMACContext ctx;
        uint8_t digest[USHAMaxHashSize];

        memcpy(input_key, derivation_key, MIN(sizeof(input_key),
                                              derivation_key_len));
        hmacReset(&ctx, SHA256, input_key, sizeof(input_key));
        hmacInput(&ctx, (unsigned char *)&counter, sizeof(counter));
        hmacInput(&ctx, (unsigned char *)label, label_len);
        hmacInput(&ctx, &nul, 1);
        hmacInput(&ctx, (unsigned char *)context, context_len);
        hmacInput(&ctx, (unsigned char *)&keylen, sizeof(keylen));
        hmacResult(&ctx, digest);
        memcpy(derived_key, digest, SMB2_KEY_SIZE);
}

/* MS-SMB2 3.2.5.2 */
static void
smb3_init_preauth_hash(struct smb2_context *smb2)
{
        memset(&smb2->preauthhash[0], 0, SMB2_PREAUTH_HASH_SIZE);
}

/* MS-SMB2 3.2.5.2 */
static int
smb3_update_preauth_hash(struct smb2_context *smb2, int niov,
                         struct smb2_iovec *iov)
{
        int i;
        USHAContext tctx;

        USHAReset(&tctx, SHA512);
        USHAInput(&tctx, smb2->preauthhash, SMB2_PREAUTH_HASH_SIZE);
        for (i = 0; i < niov; i++) {
                USHAInput(&tctx, iov[i].buf, iov[i].len);
        }
        USHAResult(&tctx, smb2->preauthhash);

        return 0;
}

static void
session_setup_cb(struct smb2_context *smb2, int status,
                 void *command_data, void *private_data)
{
        struct connect_data *c_data = private_data;
        struct smb2_session_setup_reply *rep = command_data;
        struct smb2_tree_connect_request req;
        struct smb2_pdu *pdu;
        int ret;

        if (status == SMB2_STATUS_MORE_PROCESSING_REQUIRED &&
            rep->security_buffer) {
                smb3_update_preauth_hash(smb2, smb2->in.niov - 1, &smb2->in.iov[1]);
                if ((ret = send_session_setup_request(
                                smb2, c_data, rep->security_buffer,
                                rep->security_buffer_length)) < 0) {
                        smb2_close_context(smb2);
                        c_data->cb(smb2, ret, NULL, c_data->cb_data);
                        free_c_data(smb2, c_data);
                        return;
                }
                return;

        } else if (status != SMB2_STATUS_SUCCESS) {
                smb2_close_context(smb2);
                smb2_set_error(smb2, "Session setup failed with (0x%08x) %s",
                               status, nterror_to_str(status));
                c_data->cb(smb2, -nterror_to_errno(status), NULL,
                           c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }

        if (rep->session_flags & SMB2_SESSION_FLAG_IS_ENCRYPT_DATA) {
                smb2->seal = 1;
                smb2->sign = 0;
        }

#ifdef HAVE_LIBKRB5
       if (smb2->sec == SMB2_SEC_KRB5) {
                /* For NTLM the status will be
                 * SMB2_STATUS_MORE_PROCESSING_REQUIRED and a second call to
                 * gss_init_sec_context will complete the gss session.
                 * But for krb5 a second call to gss_init_sec_context is
                 * required if GSS_C_MUTUAL_FLAG is set.
                 *
                 * At this point SMB2 layer reported success already, so we
                 * ignore krb5 errors.
                 */
                krb5_session_request(smb2, c_data->auth_data,
                                         rep->security_buffer,
                                         rep->security_buffer_length);
       }
#endif


        if (smb2->sign || smb2->seal || smb2->dialect == SMB2_VERSION_0311) {
                uint8_t zero_key[SMB2_KEY_SIZE] = {0};
                int have_valid_session_key = 1;

                if (smb2->sec == SMB2_SEC_NTLMSSP) {
                        if (ntlmssp_get_session_key(c_data->auth_data,
                                                    &smb2->session_key,
                                                    &smb2->session_key_size) < 0) {
                                have_valid_session_key = 0;
                        }
                }
#ifdef HAVE_LIBKRB5
                else {
                        if (krb5_session_get_session_key(smb2, c_data->auth_data) < 0) {
                                have_valid_session_key = 0;
                        }
                }
#endif
                /* check if the session key is proper */
                if (smb2->session_key == NULL || memcmp(smb2->session_key, zero_key, SMB2_KEY_SIZE) == 0) {
                        have_valid_session_key = 0;
                }
                if (smb2->sign && have_valid_session_key == 0) {
                        smb2_close_context(smb2);
                        smb2_set_error(smb2, "Signing required by server. Session "
                                       "Key is not available %s",
                                       smb2_get_error(smb2));
                        c_data->cb(smb2, -EACCES, NULL, c_data->cb_data);
                        free_c_data(smb2, c_data);
                        return;
                }

                /* Derive the signing key from session key
                 * This is based on negotiated protocol
                 */
                if (smb2->dialect == SMB2_VERSION_0202 ||
                    smb2->dialect == SMB2_VERSION_0210) {
                        /* For SMB2 session key is the signing key */
                        memcpy(smb2->signing_key,
                               smb2->session_key,
                               MIN(smb2->session_key_size, SMB2_KEY_SIZE));
                } else if (smb2->dialect <= SMB2_VERSION_0302) {
                        smb2_derive_key(smb2->session_key,
                                        smb2->session_key_size,
                                        SMB2AESCMAC,
                                        sizeof(SMB2AESCMAC),
                                        SmbSign,
                                        sizeof(SmbSign),
                                        smb2->signing_key);
                        smb2_derive_key(smb2->session_key,
                                        smb2->session_key_size,
                                        SMB2AESCCM,
                                        sizeof(SMB2AESCCM),
                                        ServerIn,
                                        sizeof(ServerIn),
                                        smb2->serverin_key);
                        smb2_derive_key(smb2->session_key,
                                        smb2->session_key_size,
                                        SMB2AESCCM,
                                        sizeof(SMB2AESCCM),
                                        ServerOut,
                                        sizeof(ServerOut),
                                        smb2->serverout_key);
                } else if (smb2->dialect > SMB2_VERSION_0302) {
                        smb2_derive_key(smb2->session_key,
                                        smb2->session_key_size,
                                        SMBSigningKey,
                                        sizeof(SMBSigningKey),
                                        (char *)smb2->preauthhash,
                                        SMB2_PREAUTH_HASH_SIZE,
                                        smb2->signing_key);
                        smb2_derive_key(smb2->session_key,
                                        smb2->session_key_size,
                                        SMBC2SCipherKey,
                                        sizeof(SMBC2SCipherKey),
                                        (char *)smb2->preauthhash,
                                        SMB2_PREAUTH_HASH_SIZE,
                                        smb2->serverin_key);
                        smb2_derive_key(smb2->session_key,
                                        smb2->session_key_size,
                                        SMBS2CCipherKey,
                                        sizeof(SMBS2CCipherKey),
                                        (char *)smb2->preauthhash,
                                        SMB2_PREAUTH_HASH_SIZE,
                                        smb2->serverout_key);
                }

                if (smb2->hdr.flags & SMB2_FLAGS_SIGNED) {
                        uint8_t signature[16];

                        memcpy(&signature[0], &smb2->in.iov[1].buf[48], 16);
                        if (smb2_calc_signature(smb2, &smb2->in.iov[1].buf[48],
                                                &smb2->in.iov[1],
                                                smb2->in.niov - 1) < 0) {
                                c_data->cb(smb2, -EINVAL, NULL, c_data->cb_data);
                                free_c_data(smb2, c_data);
                                return;
                        }
                        if (memcmp(&signature[0], &smb2->in.iov[1].buf[48], 16)) {
                                smb2_set_error(smb2, "Wrong signature in received "
                                               "PDU");
                                c_data->cb(smb2, -EINVAL, NULL, c_data->cb_data);
                                free_c_data(smb2, c_data);
                                return;
                        }
                }
        }

        memset(&req, 0, sizeof(struct smb2_tree_connect_request));
        req.flags       = 0;
        req.path_length = 2 * c_data->utf16_unc->len;
        req.path        = c_data->utf16_unc->val;

        pdu = smb2_cmd_tree_connect_async(smb2, &req, tree_connect_cb, c_data);
        if (pdu == NULL) {
                smb2_close_context(smb2);
                c_data->cb(smb2, -ENOMEM, NULL, c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }
        smb2_queue_pdu(smb2, pdu);
}

/* Returns 0 for success and -errno for failure */
static int
send_session_setup_request(struct smb2_context *smb2,
                           struct connect_data *c_data,
                           unsigned char *buf, int len)
{
        struct smb2_pdu *pdu;
        struct smb2_session_setup_request req;

        /* Session setup request. */
        memset(&req, 0, sizeof(struct smb2_session_setup_request));
        req.security_mode = smb2->security_mode;

        if (smb2->sec == SMB2_SEC_NTLMSSP) {
                if (ntlmssp_generate_blob(smb2, time(NULL), c_data->auth_data,
                                          buf, len,
                                          &req.security_buffer,
                                          &req.security_buffer_length) < 0) {
                        smb2_close_context(smb2);
                        return -1;
                }
        }
#ifdef HAVE_LIBKRB5
        else {
                if (krb5_session_request(smb2, c_data->auth_data,
                                         buf, len) < 0) {
                        smb2_close_context(smb2);
                        return -1;
                }
                req.security_buffer_length =
                        krb5_get_output_token_length(c_data->auth_data);
                req.security_buffer =
                        krb5_get_output_token_buffer(c_data->auth_data);
        }
#endif

        pdu = smb2_cmd_session_setup_async(smb2, &req,
                                           session_setup_cb, c_data);
        if (pdu == NULL) {
                smb2_close_context(smb2);
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);
        smb3_update_preauth_hash(smb2, pdu->out.niov, &pdu->out.iov[0]);

        return 0;
}

static void
negotiate_cb(struct smb2_context *smb2, int status,
             void *command_data, void *private_data)
{
        struct connect_data *c_data = private_data;
        struct smb2_negotiate_reply *rep = command_data;
        int ret;

        smb3_update_preauth_hash(smb2, smb2->in.niov - 1, &smb2->in.iov[1]);

        if (status != SMB2_STATUS_SUCCESS) {
                smb2_close_context(smb2);
                smb2_set_error(smb2, "Negotiate failed with (0x%08x) %s. %s",
                               status, nterror_to_str(status),
                               smb2_get_error(smb2));
                c_data->cb(smb2, -nterror_to_errno(status), NULL,
                           c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }

        /* update the context with the server capabilities */
        if (rep->dialect_revision > SMB2_VERSION_0202) {
                if (rep->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU) {
                        smb2->supports_multi_credit = 1;
                }
        }

        smb2->max_transact_size = rep->max_transact_size;
        smb2->max_read_size     = rep->max_read_size;
        smb2->max_write_size    = rep->max_write_size;
        smb2->dialect           = rep->dialect_revision;
        smb2->cypher            = rep->cypher;

        if (smb2->seal && (smb2->dialect == SMB2_VERSION_0300 ||
                           smb2->dialect == SMB2_VERSION_0302)) {
                if(!(rep->capabilities & SMB2_GLOBAL_CAP_ENCRYPTION)) {
                        smb2_set_error(smb2, "Encryption requested but server "
                                       "does not support encryption.");
                        smb2_close_context(smb2);
                        c_data->cb(smb2, -ENOMEM, NULL, c_data->cb_data);
                        free_c_data(smb2, c_data);
                        return;
                }
        }

        if (smb2->sign &&
            !(rep->security_mode & SMB2_NEGOTIATE_SIGNING_ENABLED)) {
                smb2_set_error(smb2, "Signing requested but server "
                               "does not support signing.");
                smb2_close_context(smb2);
                c_data->cb(smb2, -ENOMEM, NULL, c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }

        if (rep->security_mode & SMB2_NEGOTIATE_SIGNING_REQUIRED) {
                smb2->sign = 1;
        }

        if (smb2->seal) {
                smb2->sign = 0;
        }

        if (smb2->sec == SMB2_SEC_NTLMSSP) {
                c_data->auth_data = ntlmssp_init_context(smb2->user,
                                                         smb2->password,
                                                         smb2->domain,
                                                         smb2->workstation,
                                                         smb2->client_challenge);
        }
#ifdef HAVE_LIBKRB5
        else {
                c_data->auth_data = krb5_negotiate_reply(smb2,
                                                         c_data->server,
                                                         smb2->domain,
                                                         c_data->user,
                                                         smb2->password);
        }
#endif
        if (c_data->auth_data == NULL) {
                smb2_close_context(smb2);
                c_data->cb(smb2, -ENOMEM, NULL, c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }

        if ((ret = send_session_setup_request(smb2, c_data, NULL, 0)) < 0) {
                smb2_close_context(smb2);
                c_data->cb(smb2, ret, NULL, c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }
}

static void
connect_cb(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct connect_data *c_data = private_data;
        struct smb2_negotiate_request req;
        struct smb2_pdu *pdu;

        if (status != 0) {
                smb2_set_error(smb2, "Socket connect failed with %d",
                               status);
                c_data->cb(smb2, -status, NULL, c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }

        memset(&req, 0, sizeof(struct smb2_negotiate_request));
        req.capabilities = SMB2_GLOBAL_CAP_LARGE_MTU;
        if (smb2->version == SMB2_VERSION_ANY  ||
            smb2->version == SMB2_VERSION_ANY3 ||
            smb2->version == SMB2_VERSION_0300 ||
            smb2->version == SMB2_VERSION_0302 ||
            smb2->version == SMB2_VERSION_0311) {
                req.capabilities |= SMB2_GLOBAL_CAP_ENCRYPTION;
        }
        req.security_mode = smb2->security_mode;
        switch (smb2->version) {
        case SMB2_VERSION_ANY:
#ifdef MXTECHS
                req.dialect_count = 4;
#else
                req.dialect_count = 5;
#endif
                req.dialects[0] = SMB2_VERSION_0202;
                req.dialects[1] = SMB2_VERSION_0210;
                req.dialects[2] = SMB2_VERSION_0300;
                req.dialects[3] = SMB2_VERSION_0302;
//it will fail if using SMB2_VERSION_0311 version during connection process in smb2 4.0 library, so remove SMB2_VERSION_0311 in our library
#ifdef MXTECHS

#else
                req.dialects[4] = SMB2_VERSION_0311;
#endif

                break;
        case SMB2_VERSION_ANY2:
                req.dialect_count = 2;
                req.dialects[0] = SMB2_VERSION_0202;
                req.dialects[1] = SMB2_VERSION_0210;
                break;
        case SMB2_VERSION_ANY3:
                req.dialect_count = 3;
                req.dialects[0] = SMB2_VERSION_0300;
                req.dialects[1] = SMB2_VERSION_0302;
                req.dialects[2] = SMB2_VERSION_0311;
                break;
        case SMB2_VERSION_0202:
        case SMB2_VERSION_0210:
        case SMB2_VERSION_0300:
        case SMB2_VERSION_0302:
        case SMB2_VERSION_0311:
                req.dialect_count = 1;
                req.dialects[0] = smb2->version;
                break;
        }

        memcpy(req.client_guid, smb2_get_client_guid(smb2), SMB2_GUID_SIZE);

        if (smb2->sec == SMB2_SEC_UNDEFINED) {
#ifdef HAVE_LIBKRB5
                smb2->sec = SMB2_SEC_KRB5;
#else
                smb2->sec = SMB2_SEC_NTLMSSP;
#endif
        }
        smb3_init_preauth_hash(smb2);
        pdu = smb2_cmd_negotiate_async(smb2, &req, negotiate_cb, c_data);
        if (pdu == NULL) {
                c_data->cb(smb2, -ENOMEM, NULL, c_data->cb_data);
                free_c_data(smb2, c_data);
                return;
        }
        smb2_queue_pdu(smb2, pdu);
        smb3_update_preauth_hash(smb2, pdu->out.niov, &pdu->out.iov[0]);
}

int
smb2_connect_share_async(struct smb2_context *smb2,
                         const char *server,
                         const char *share, const char *user,
                         smb2_command_cb cb, void *cb_data)
{
        struct connect_data *c_data;
        int err;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        if (smb2->server != NULL) {
                free(discard_const(smb2->server));
                smb2->server = NULL;
        }
        if (server == NULL) {
                smb2_set_error(smb2, "No server name provided");
                return -EINVAL;
        }
        smb2->server = strdup(server);

        if (smb2->share) {
                free(discard_const(smb2->share));
        }
        smb2->share = strdup(share);

        if (user) {
                smb2_set_user(smb2, user);
        }

        c_data = calloc(1, sizeof(struct connect_data));
        if (c_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate connect_data");
                return -ENOMEM;
        }
        c_data->server = strdup(smb2->server);
        if (c_data->server == NULL) {
                free_c_data(smb2, c_data);
                smb2_set_error(smb2, "Failed to strdup(server)");
                return -ENOMEM;
        }
        c_data->share = strdup(smb2->share);
        if (c_data->share == NULL) {
                free_c_data(smb2, c_data);
                smb2_set_error(smb2, "Failed to strdup(share)");
                return -ENOMEM;
        }
        c_data->user = strdup(smb2->user);
        if (c_data->user == NULL) {
                free_c_data(smb2, c_data);
                smb2_set_error(smb2, "Failed to strdup(user)");
                return -ENOMEM;
        }
        if (asprintf(&c_data->utf8_unc, "\\\\%s\\%s", c_data->server,
                     c_data->share) < 0) {
                free_c_data(smb2, c_data);
                smb2_set_error(smb2, "Failed to allocate unc string.");
                return -ENOMEM;
        }

        c_data->utf16_unc = utf8_to_utf16(c_data->utf8_unc);
        if (c_data->utf16_unc == NULL) {
                smb2_set_error(smb2, "Count not convert UNC:[%s] into UTF-16",
                               c_data->utf8_unc);
                free_c_data(smb2, c_data);
                return -ENOMEM;
        }

        c_data->cb = cb;
        c_data->cb_data = cb_data;

        err = smb2_connect_async(smb2, server, connect_cb, c_data);
        if (err != 0) {
                free_c_data(smb2, c_data);
                return err;
        }

        return 0;
}

static void
free_smb2fh(struct smb2_context *smb2, struct smb2fh *fh)
{
        SMB2_LIST_REMOVE(&smb2->fhs, fh);
        free(fh);
}

void smb2_free_all_fhs(struct smb2_context *smb2)
{
        while (smb2->fhs) {
                free_smb2fh(smb2, smb2->fhs);
        }
}

static void
open_cb(struct smb2_context *smb2, int status,
        void *command_data, void *private_data)
{
        struct smb2fh *fh = private_data;
        struct smb2_create_reply *rep = command_data;

        if (status != SMB2_STATUS_SUCCESS) {
                smb2_set_error(smb2, "Open failed with (0x%08x) %s.",
                               status, nterror_to_str(status));
                fh->cb(smb2, -nterror_to_errno(status), NULL, fh->cb_data);
                free_smb2fh(smb2, fh);
                return;
        }

        memcpy(fh->file_id, rep->file_id, SMB2_FD_SIZE);
        fh->end_of_file = rep->end_of_file;
        fh->cb(smb2, 0, fh, fh->cb_data);
}

int
smb2_open_async(struct smb2_context *smb2, const char *path, int flags,
                smb2_command_cb cb, void *cb_data)
{
        struct smb2fh *fh;
        struct smb2_create_request req;
        struct smb2_pdu *pdu;
        uint32_t desired_access = 0;
        uint32_t create_disposition = 0;
        uint32_t create_options = 0;
        uint32_t file_attributes = 0;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        fh = calloc(1, sizeof(struct smb2fh));
        if (fh == NULL) {
                smb2_set_error(smb2, "Failed to allocate smbfh");
                return -ENOMEM;
        }
        SMB2_LIST_ADD(&smb2->fhs, fh);

        fh->cb = cb;
        fh->cb_data = cb_data;

        /* Create disposition */
        if (flags & O_CREAT) {
                if (flags & O_EXCL) {
                        create_disposition = SMB2_FILE_CREATE;
                } else if(flags & O_TRUNC) {
                        create_disposition = SMB2_FILE_OVERWRITE_IF;
                } else {
                        create_disposition = SMB2_FILE_OPEN_IF;
                }
        } else {
                if (flags & O_TRUNC) {
                        create_disposition = SMB2_FILE_OVERWRITE;
                } else {
                        create_disposition = SMB2_FILE_OPEN;
                }
        }

        /* desired access */
        if (flags & (O_RDWR | O_WRONLY)) {
                desired_access |= SMB2_FILE_WRITE_DATA |
                        SMB2_FILE_WRITE_EA |
                        SMB2_FILE_WRITE_ATTRIBUTES;
        }
        if (!(flags & O_WRONLY)) {
                desired_access |= SMB2_FILE_READ_DATA |
                        SMB2_FILE_READ_EA |
                        SMB2_FILE_READ_ATTRIBUTES;
        }

        /* create options */
        create_options |= SMB2_FILE_NON_DIRECTORY_FILE;


        if (flags & O_SYNC) {
                desired_access |= SMB2_SYNCHRONIZE;
                create_options |= SMB2_FILE_NO_INTERMEDIATE_BUFFERING;
        }

        memset(&req, 0, sizeof(struct smb2_create_request));
        req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        req.desired_access = desired_access;
        req.file_attributes = file_attributes;
        req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE;
        req.create_disposition = create_disposition;
        req.create_options = create_options;
        req.name = path;

        pdu = smb2_cmd_create_async(smb2, &req, open_cb, fh);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create create command");
                free_smb2fh(smb2, fh);
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

static void
close_cb(struct smb2_context *smb2, int status,
         void *command_data, void *private_data)
{
        struct smb2fh *fh = private_data;

        if (status != SMB2_STATUS_SUCCESS) {
                smb2_set_error(smb2, "Close failed with (0x%08x) %s",
                               status, nterror_to_str(status));
                fh->cb(smb2, -nterror_to_errno(status), NULL, fh->cb_data);
                free_smb2fh(smb2, fh);
                return;
        }

        fh->cb(smb2, 0, NULL, fh->cb_data);
        free_smb2fh(smb2, fh);
}

int
smb2_close_async(struct smb2_context *smb2, struct smb2fh *fh,
                 smb2_command_cb cb, void *cb_data)
{
        struct smb2_close_request req;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
            return -EINVAL;
        }
        if (fh == NULL) {
            smb2_set_error(smb2, "File handle was NULL");
            return -EINVAL;
        }

        fh->cb = cb;
        fh->cb_data = cb_data;

        memset(&req, 0, sizeof(struct smb2_close_request));
        req.flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
        memcpy(req.file_id, fh->file_id, SMB2_FD_SIZE);

        pdu = smb2_cmd_close_async(smb2, &req, close_cb, fh);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create close command");
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

static void
fsync_cb(struct smb2_context *smb2, int status,
         void *command_data, void *private_data)
{
        struct smb2fh *fh = private_data;

        if (status != SMB2_STATUS_SUCCESS) {
                smb2_set_error(smb2, "Flush failed with (0x%08x) %s",
                               status, nterror_to_str(status));
                fh->cb(smb2, -nterror_to_errno(status), NULL, fh->cb_data);
                return;
        }

        fh->cb(smb2, 0, NULL, fh->cb_data);
}

int
smb2_fsync_async(struct smb2_context *smb2, struct smb2fh *fh,
                 smb2_command_cb cb, void *cb_data)
{
        struct smb2_flush_request req;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
            return -EINVAL;
        }
        if (fh == NULL) {
            smb2_set_error(smb2, "File handle was NULL");
            return -EINVAL;
        }

        fh->cb = cb;
        fh->cb_data = cb_data;

        memset(&req, 0, sizeof(struct smb2_flush_request));
        memcpy(req.file_id, fh->file_id, SMB2_FD_SIZE);

        pdu = smb2_cmd_flush_async(smb2, &req, fsync_cb, fh);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create flush command");
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

struct read_data {
        smb2_command_cb cb;
        void *cb_data;

        struct smb2_read_cb_data read_cb_data;
};

static void
read_cb(struct smb2_context *smb2, int status,
      void *command_data, void *private_data)
{
        struct read_data *rd = private_data;
        struct smb2_read_reply *rep = command_data;

        if (status && status != SMB2_STATUS_END_OF_FILE) {
                smb2_set_error(smb2, "Read/Write failed with (0x%08x) %s",
                               status, nterror_to_str(status));
                rd->cb(smb2, -nterror_to_errno(status), &rd->read_cb_data, rd->cb_data);
                free(rd);
                return;
        }

        if (status == SMB2_STATUS_SUCCESS) {
                rd->read_cb_data.fh->offset = rd->read_cb_data.offset + rep->data_length;
        }

        rd->cb(smb2, rep->data_length, &rd->read_cb_data, rd->cb_data);
        free(rd);
}

int
smb2_pread_async(struct smb2_context *smb2, struct smb2fh *fh,
                 uint8_t *buf, uint32_t count, uint64_t offset,
                 smb2_command_cb cb, void *cb_data)
{
        struct smb2_read_request req;
        struct read_data *rd;
        struct smb2_pdu *pdu;
        int needed_credits;

        if (smb2 == NULL) {
                return -EINVAL;
        }
        if (fh == NULL) {
                smb2_set_error(smb2, "File handle was NULL");
                return -EINVAL;
        }

        rd = calloc(1, sizeof(struct read_data));
        if (rd == NULL) {
                smb2_set_error(smb2, "Failed to allocate read_data");
                return -ENOMEM;
        }

        rd->cb = cb;
        rd->cb_data = cb_data;
        rd->read_cb_data.fh = fh;
        rd->read_cb_data.buf = buf;
        rd->read_cb_data.count = count;
        rd->read_cb_data.offset = offset;

        if (count > smb2->max_read_size) {
                count = smb2->max_read_size;
        }
        needed_credits = (count - 1) / 65536 + 1;

        if (smb2->dialect > SMB2_VERSION_0202) {
                if (needed_credits > MAX_CREDITS - 16) {
                        count =  (MAX_CREDITS - 16) * 65536;
                }
                needed_credits = (count - 1) / 65536 + 1;
                if (needed_credits > smb2->credits) {
                        count = smb2->credits * 65536;
                }
        } else {
                if (count > 65536) {
                        count = 65536;
                }
        }
        needed_credits = (count - 1) / 65536 + 1;

        memset(&req, 0, sizeof(struct smb2_read_request));
        req.flags = 0;
        req.length = count;
        req.offset = offset;
        req.buf = buf;
        memcpy(req.file_id, fh->file_id, SMB2_FD_SIZE);
        req.minimum_count = 0;
        req.channel = SMB2_CHANNEL_NONE;
        req.remaining_bytes = 0;

        pdu = smb2_cmd_read_async(smb2, &req, read_cb, rd);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create read command");
                return -EINVAL;
        }

        smb2_queue_pdu(smb2, pdu);

        return 0;
}

int
smb2_read_async(struct smb2_context *smb2, struct smb2fh *fh,
                uint8_t *buf, uint32_t count,
                smb2_command_cb cb, void *cb_data)
{
        if (smb2 == NULL) {
                return -EINVAL;
        }
        if (fh == NULL) {
                smb2_set_error(smb2, "File handle was NULL");
                return -EINVAL;
        }

        return smb2_pread_async(smb2, fh, buf, count, fh->offset,
                                cb, cb_data);
}

struct write_data {
        smb2_command_cb cb;
        void *cb_data;

        struct smb2_write_cb_data write_cb_data;
};

static void
write_cb(struct smb2_context *smb2, int status,
      void *command_data, void *private_data)
{
        struct write_data *wd = private_data;
        struct smb2_write_reply *rep = command_data;

        if (status && status != SMB2_STATUS_END_OF_FILE) {
                smb2_set_error(smb2, "Read/Write failed with (0x%08x) %s",
                               status, nterror_to_str(status));
                wd->cb(smb2, -nterror_to_errno(status), &wd->write_cb_data, wd->cb_data);
                free(wd);
                return;
        }

        if (status == SMB2_STATUS_SUCCESS) {
                wd->write_cb_data.fh->offset = wd->write_cb_data.offset + rep->count;
        }

        wd->cb(smb2, rep->count, &wd->write_cb_data, wd->cb_data);
        free(wd);
}

int
smb2_pwrite_async(struct smb2_context *smb2, struct smb2fh *fh,
                  const uint8_t *buf, uint32_t count, uint64_t offset,
                  smb2_command_cb cb, void *cb_data)
{
        struct smb2_write_request req;
        struct write_data *wr;
        struct smb2_pdu *pdu;
        int needed_credits;

        if (smb2 == NULL) {
                return -EINVAL;
        }
        if (fh == NULL) {
                smb2_set_error(smb2, "File handle was NULL");
                return -EINVAL;
        }

        wr = calloc(1, sizeof(struct write_data));
        if (wr == NULL) {
                smb2_set_error(smb2, "Failed to allocate write_data");
                return -ENOMEM;
        }

        wr->cb = cb;
        wr->cb_data = cb_data;
        wr->write_cb_data.fh = fh;
        wr->write_cb_data.buf = buf;
        wr->write_cb_data.count = count;
        wr->write_cb_data.offset = offset;

        if (count > smb2->max_write_size) {
                count = smb2->max_write_size;
        }
        needed_credits = (count - 1) / 65536 + 1;

        if (smb2->dialect > SMB2_VERSION_0202) {
                if (needed_credits > MAX_CREDITS - 16) {
                        count =  (MAX_CREDITS - 16) * 65536;
                }
                needed_credits = (count - 1) / 65536 + 1;
                if (needed_credits > smb2->credits) {
                        count = smb2->credits * 65536;
                }
        } else {
                if (count > 65536) {
                        count = 65536;
                }
        }
        needed_credits = (count - 1) / 65536 + 1;

        memset(&req, 0, sizeof(struct smb2_write_request));
        req.length = count;
        req.offset = offset;
        req.buf = buf;
        memcpy(req.file_id, fh->file_id, SMB2_FD_SIZE);
        req.channel = SMB2_CHANNEL_NONE;
        req.remaining_bytes = 0;
        req.flags = 0;

        pdu = smb2_cmd_write_async(smb2, &req, write_cb, wr);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create write command");
                return -EINVAL;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

int
smb2_write_async(struct smb2_context *smb2, struct smb2fh *fh,
                 const uint8_t *buf, uint32_t count,
                 smb2_command_cb cb, void *cb_data)
{
        if (smb2 == NULL) {
                return -EINVAL;
        }
        if (fh == NULL) {
                smb2_set_error(smb2, "File handle was NULL");
                return -EINVAL;
        }
        return smb2_pwrite_async(smb2, fh, buf, count, fh->offset,
                                 cb, cb_data);
}

int64_t
smb2_lseek(struct smb2_context *smb2, struct smb2fh *fh,
           int64_t offset, int whence, uint64_t *current_offset)
{
        if (smb2 == NULL) {
                return -EINVAL;
        }
        if (fh == NULL) {
                smb2_set_error(smb2, "File handle was NULL");
                return -EINVAL;
        }

        switch(whence) {
        case SEEK_SET:
                if (offset < 0) {
                        smb2_set_error(smb2, "Lseek() offset would become"
                                        "negative");
                        return -EINVAL;
                }
                fh->offset = offset;
                if (current_offset) {
                        *current_offset = fh->offset;
                }
                return fh->offset;
        case SEEK_CUR:
                if (fh->offset + offset < 0) {
                        smb2_set_error(smb2, "Lseek() offset would become"
                                        "negative");
                        return -EINVAL;
                }
                fh->offset += offset;
                if (current_offset) {
                        *current_offset = fh->offset;
                }
                return fh->offset;
        case SEEK_END:
                fh->offset = fh->end_of_file;
                if (fh->offset + offset < 0) {
                        smb2_set_error(smb2, "Lseek() offset would become"
                                        "negative");
                        return -EINVAL;
                }
                fh->offset += offset;
                if (current_offset) {
                        *current_offset = fh->offset;
                }
                return fh->offset;
        default:
                smb2_set_error(smb2, "Invalid whence(%d) for lseek",
                                    whence);
                return -EINVAL;
        }
}

struct create_cb_data {
        smb2_command_cb cb;
        void *cb_data;
};

static void
create_cb_2(struct smb2_context *smb2, int status,
            void *command_data, void *private_data)
{
        struct create_cb_data *create_data = private_data;

        if (status != SMB2_STATUS_SUCCESS) {
                create_data->cb(smb2, -nterror_to_errno(status),
                       NULL, create_data->cb_data);
                free(create_data);
                return;
        }

        create_data->cb(smb2, 0, NULL, create_data->cb_data);
        free(create_data);
}

static void
create_cb_1(struct smb2_context *smb2, int status,
            void *command_data, void *private_data)
{
        struct create_cb_data *create_data = private_data;
        struct smb2_create_reply *rep = command_data;
        struct smb2_close_request req;
        struct smb2_pdu *pdu;

        if (status != SMB2_STATUS_SUCCESS) {
                create_data->cb(smb2, -nterror_to_errno(status),
                       NULL, create_data->cb_data);
                free(create_data);
                return;
        }

        memset(&req, 0, sizeof(struct smb2_close_request));
        req.flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
        memcpy(req.file_id, rep->file_id, SMB2_FD_SIZE);

        pdu = smb2_cmd_close_async(smb2, &req, create_cb_2, create_data);
        if (pdu == NULL) {
                create_data->cb(smb2, -ENOMEM, NULL, create_data->cb_data);
                free(create_data);
                return;
        }
        smb2_queue_pdu(smb2, pdu);
}

static int
smb2_unlink_internal(struct smb2_context *smb2, const char *path,
                     int is_dir,
                     smb2_command_cb cb, void *cb_data)
{
        struct create_cb_data *create_data;
        struct smb2_create_request req;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        create_data = calloc(1, sizeof(struct create_cb_data));
        if (create_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate create_data");
                return -ENOMEM;
        }

        create_data->cb = cb;
        create_data->cb_data = cb_data;


        memset(&req, 0, sizeof(struct smb2_create_request));
        req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        req.desired_access = SMB2_DELETE;
        if (is_dir) {
                req.file_attributes = SMB2_FILE_ATTRIBUTE_DIRECTORY;
        } else {
                req.file_attributes = SMB2_FILE_ATTRIBUTE_NORMAL;
        }
        req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE |
                SMB2_FILE_SHARE_DELETE;
        req.create_disposition = SMB2_FILE_OPEN;
        req.create_options = SMB2_FILE_DELETE_ON_CLOSE;
        req.name = path;

        pdu = smb2_cmd_create_async(smb2, &req, create_cb_1, create_data);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create create command");
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

int
smb2_unlink_async(struct smb2_context *smb2, const char *path,
                  smb2_command_cb cb, void *cb_data)
{
        return smb2_unlink_internal(smb2, path, 0, cb, cb_data);
}

int
smb2_rmdir_async(struct smb2_context *smb2, const char *path,
                 smb2_command_cb cb, void *cb_data)
{
        return smb2_unlink_internal(smb2, path, 1, cb, cb_data);
}

int
smb2_mkdir_async(struct smb2_context *smb2, const char *path,
                 smb2_command_cb cb, void *cb_data)
{
        struct create_cb_data *create_data;
        struct smb2_create_request req;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        create_data = calloc(1, sizeof(struct create_cb_data));
        if (create_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate create_data");
                return -ENOMEM;
        }

        create_data->cb = cb;
        create_data->cb_data = cb_data;

        memset(&req, 0, sizeof(struct smb2_create_request));
        req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        req.desired_access = SMB2_FILE_READ_ATTRIBUTES;
        req.file_attributes = SMB2_FILE_ATTRIBUTE_DIRECTORY;
        req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE;
        req.create_disposition = SMB2_FILE_CREATE;
        req.create_options = SMB2_FILE_DIRECTORY_FILE;
        req.name = path;

        pdu = smb2_cmd_create_async(smb2, &req, create_cb_1, create_data);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create create command");
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

struct stat_cb_data {
        smb2_command_cb cb;
        void *cb_data;

        uint32_t status;
        uint8_t info_type;
        uint8_t file_info_class;
        void *st;
};

static void
fstat_cb_1(struct smb2_context *smb2, int status,
           void *command_data, void *private_data)
{
        struct stat_cb_data *stat_data = private_data;
        struct smb2_query_info_reply *rep = command_data;
        struct smb2_file_all_info *fs = rep->output_buffer;
        struct smb2_stat_64 *st = stat_data->st;

        if (status != SMB2_STATUS_SUCCESS) {
                stat_data->cb(smb2, -nterror_to_errno(status),
                       NULL, stat_data->cb_data);
                free(stat_data);
                return;
        }

        st->smb2_type = SMB2_TYPE_FILE;
        if (fs->basic.file_attributes & SMB2_FILE_ATTRIBUTE_DIRECTORY) {
                st->smb2_type = SMB2_TYPE_DIRECTORY;
        }
        if (fs->basic.file_attributes & SMB2_FILE_ATTRIBUTE_REPARSE_POINT) {
                st->smb2_type = SMB2_TYPE_LINK;
        }
        st->smb2_nlink      = fs->standard.number_of_links;
        st->smb2_ino        = fs->index_number;
        st->smb2_size       = fs->standard.end_of_file;
        st->smb2_atime      = fs->basic.last_access_time.tv_sec;
        st->smb2_atime_nsec = fs->basic.last_access_time.tv_usec *
                1000;
        st->smb2_mtime      = fs->basic.last_write_time.tv_sec;
        st->smb2_mtime_nsec = fs->basic.last_write_time.tv_usec *
                1000;
        st->smb2_ctime      = fs->basic.change_time.tv_sec;
        st->smb2_ctime_nsec = fs->basic.change_time.tv_usec *
                1000;
        st->smb2_btime      = fs->basic.creation_time.tv_sec;
        st->smb2_btime_nsec = fs->basic.creation_time.tv_usec *
                1000;

        smb2_free_data(smb2, fs);

        stat_data->cb(smb2, 0, st, stat_data->cb_data);
        free(stat_data);
}

int
smb2_fstat_async(struct smb2_context *smb2, struct smb2fh *fh,
                 struct smb2_stat_64 *st,
                 smb2_command_cb cb, void *cb_data)
{
        struct stat_cb_data *stat_data;
        struct smb2_query_info_request req;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }
        if (fh == NULL) {
                smb2_set_error(smb2, "File handle was NULL");
                return -EINVAL;
        }

        stat_data = calloc(1, sizeof(struct stat_cb_data));
        if (stat_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate stat_data");
                return -ENOMEM;
        }

        stat_data->cb = cb;
        stat_data->cb_data = cb_data;
        stat_data->st = st;

        memset(&req, 0, sizeof(struct smb2_query_info_request));
        req.info_type = SMB2_0_INFO_FILE;
        req.file_info_class = SMB2_FILE_ALL_INFORMATION;
        req.output_buffer_length = DEFAULT_OUTPUT_BUFFER_LENGTH;
        req.additional_information = 0;
        req.flags = 0;
        memcpy(req.file_id, fh->file_id, SMB2_FD_SIZE);

        pdu = smb2_cmd_query_info_async(smb2, &req, fstat_cb_1, stat_data);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create query command");
                free(stat_data);
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

static void
getinfo_cb_3(struct smb2_context *smb2, int status,
             void *command_data _U_, void *private_data)
{
        struct stat_cb_data *stat_data = private_data;

        if (stat_data->status == SMB2_STATUS_SUCCESS) {
                stat_data->status = status;
        }

        stat_data->cb(smb2, -nterror_to_errno(stat_data->status),
                      stat_data->st, stat_data->cb_data);
        free(stat_data);
}

static void
getinfo_cb_2(struct smb2_context *smb2, int status,
             void *command_data, void *private_data)
{
        struct stat_cb_data *stat_data = private_data;
        struct smb2_query_info_reply *rep = command_data;

        if (stat_data->status == SMB2_STATUS_SUCCESS) {
                stat_data->status = status;
        }
        if (stat_data->status != SMB2_STATUS_SUCCESS) {
                return;
        }

        if (stat_data->info_type == SMB2_0_INFO_FILE &&
            stat_data->file_info_class == SMB2_FILE_ALL_INFORMATION) {
                struct smb2_stat_64 *st = stat_data->st;
                struct smb2_file_all_info *fs = rep->output_buffer;

                st->smb2_type = SMB2_TYPE_FILE;
                if (fs->basic.file_attributes & SMB2_FILE_ATTRIBUTE_DIRECTORY) {
                        st->smb2_type = SMB2_TYPE_DIRECTORY;
                }
                if (fs->basic.file_attributes & SMB2_FILE_ATTRIBUTE_REPARSE_POINT) {
                        st->smb2_type = SMB2_TYPE_LINK;
                }
                st->smb2_nlink      = fs->standard.number_of_links;
                st->smb2_ino        = fs->index_number;
                st->smb2_size       = fs->standard.end_of_file;
                st->smb2_atime      = fs->basic.last_access_time.tv_sec;
                st->smb2_atime_nsec = fs->basic.last_access_time.tv_usec *
                        1000;
                st->smb2_mtime      = fs->basic.last_write_time.tv_sec;
                st->smb2_mtime_nsec = fs->basic.last_write_time.tv_usec *
                        1000;
                st->smb2_ctime      = fs->basic.change_time.tv_sec;
                st->smb2_ctime_nsec = fs->basic.change_time.tv_usec *
                        1000;
                st->smb2_btime      = fs->basic.creation_time.tv_sec;
                st->smb2_btime_nsec = fs->basic.creation_time.tv_usec *
                        1000;
        } else if (stat_data->info_type == SMB2_0_INFO_FILESYSTEM &&
                   stat_data->file_info_class == SMB2_FILE_FS_FULL_SIZE_INFORMATION) {
                struct smb2_statvfs *statvfs = stat_data->st;
                struct smb2_file_fs_full_size_info *vfs = rep->output_buffer;

                memset(statvfs, 0, sizeof(struct smb2_statvfs));
                statvfs->f_bsize = statvfs->f_frsize =
                        vfs->bytes_per_sector *
                        vfs->sectors_per_allocation_unit;
                statvfs->f_blocks = vfs->total_allocation_units;
                statvfs->f_bfree = statvfs->f_bavail =
                        vfs->caller_available_allocation_units;
        }
        smb2_free_data(smb2, rep->output_buffer);
}

static void
getinfo_cb_1(struct smb2_context *smb2, int status,
             void *command_data _U_, void *private_data)
{
        struct stat_cb_data *stat_data = private_data;

        if (stat_data->status == SMB2_STATUS_SUCCESS) {
                stat_data->status = status;
        }
}

static int
smb2_getinfo_async(struct smb2_context *smb2, const char *path,
                   uint8_t info_type, uint8_t file_info_class,
                   void *st,
                   smb2_command_cb cb, void *cb_data)
{
        struct stat_cb_data *stat_data;
        struct smb2_create_request cr_req;
        struct smb2_query_info_request qi_req;
        struct smb2_close_request cl_req;
        struct smb2_pdu *pdu, *next_pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        stat_data = calloc(1, sizeof(struct stat_cb_data));
        if (stat_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate create_data");
                return -1;
        }

        stat_data->cb = cb;
        stat_data->cb_data = cb_data;
        stat_data->info_type = info_type;
        stat_data->file_info_class = file_info_class;
        stat_data->st = st;

        /* CREATE command */
        memset(&cr_req, 0, sizeof(struct smb2_create_request));
        cr_req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        cr_req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        cr_req.desired_access = SMB2_FILE_READ_ATTRIBUTES | SMB2_FILE_READ_EA;
        cr_req.file_attributes = 0;
        cr_req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE;
        cr_req.create_disposition = SMB2_FILE_OPEN;
        cr_req.create_options = 0;
        cr_req.name = path;

        pdu = smb2_cmd_create_async(smb2, &cr_req, getinfo_cb_1, stat_data);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create create command");
                free(stat_data);
                return -1;
        }

        /* QUERY INFO command */
        memset(&qi_req, 0, sizeof(struct smb2_query_info_request));
        qi_req.info_type = info_type;
        qi_req.file_info_class = file_info_class;
        qi_req.output_buffer_length = DEFAULT_OUTPUT_BUFFER_LENGTH;
        qi_req.additional_information = 0;
        qi_req.flags = 0;
        memcpy(qi_req.file_id, compound_file_id, SMB2_FD_SIZE);

        next_pdu = smb2_cmd_query_info_async(smb2, &qi_req,
                                             getinfo_cb_2, stat_data);
        if (next_pdu == NULL) {
                smb2_set_error(smb2, "Failed to create query command");
                free(stat_data);
                smb2_free_pdu(smb2, pdu);
                return -1;
        }
        smb2_add_compound_pdu(smb2, pdu, next_pdu);

        /* CLOSE command */
        memset(&cl_req, 0, sizeof(struct smb2_close_request));
        cl_req.flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
        memcpy(cl_req.file_id, compound_file_id, SMB2_FD_SIZE);

        next_pdu = smb2_cmd_close_async(smb2, &cl_req, getinfo_cb_3, stat_data);
        if (next_pdu == NULL) {
                stat_data->cb(smb2, -ENOMEM, NULL, stat_data->cb_data);
                free(stat_data);
                smb2_free_pdu(smb2, pdu);
                return -1;
        }
        smb2_add_compound_pdu(smb2, pdu, next_pdu);

        smb2_queue_pdu(smb2, pdu);

        return 0;
}

int
smb2_stat_async(struct smb2_context *smb2, const char *path,
                struct smb2_stat_64 *st,
                smb2_command_cb cb, void *cb_data)
{
        return smb2_getinfo_async(smb2, path,
                                  SMB2_0_INFO_FILE,
                                  SMB2_FILE_ALL_INFORMATION,
                                  st, cb, cb_data);
}

int
smb2_statvfs_async(struct smb2_context *smb2, const char *path,
                   struct smb2_statvfs *statvfs,
                   smb2_command_cb cb, void *cb_data)
{
        return smb2_getinfo_async(smb2, path,
                                  SMB2_0_INFO_FILESYSTEM,
                                  SMB2_FILE_FS_FULL_SIZE_INFORMATION,
                                  statvfs, cb, cb_data);
}

struct trunc_cb_data {
        smb2_command_cb cb;
        void *cb_data;

        uint32_t status;
        uint64_t length;
};

static void
trunc_cb_3(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct trunc_cb_data *trunc_data = private_data;

        if (trunc_data->status == SMB2_STATUS_SUCCESS) {
                trunc_data->status = status;
        }

        trunc_data->cb(smb2, -nterror_to_errno(trunc_data->status),
                       NULL, trunc_data->cb_data);
        free(trunc_data);
}

static void
trunc_cb_2(struct smb2_context *smb2, int status,
           void *command_data, void *private_data)
{
        struct trunc_cb_data *trunc_data = private_data;

        if (trunc_data->status == SMB2_STATUS_SUCCESS) {
                trunc_data->status = status;
        }
}

static void
trunc_cb_1(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct trunc_cb_data *trunc_data = private_data;

        if (trunc_data->status == SMB2_STATUS_SUCCESS) {
                trunc_data->status = status;
        }
}

int
smb2_truncate_async(struct smb2_context *smb2, const char *path,
                    uint64_t length, smb2_command_cb cb, void *cb_data)
{
        struct trunc_cb_data *trunc_data;
        struct smb2_create_request cr_req;
        struct smb2_set_info_request si_req;
        struct smb2_close_request cl_req;
        struct smb2_pdu *pdu, *next_pdu;
        struct smb2_file_end_of_file_info eofi;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        trunc_data = calloc(1, sizeof(struct trunc_cb_data));
        if (trunc_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate trunc_data");
                return -ENOMEM;
        }

        trunc_data->cb = cb;
        trunc_data->cb_data = cb_data;
        trunc_data->length = length;

        /* CREATE command */
        memset(&cr_req, 0, sizeof(struct smb2_create_request));
        cr_req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        cr_req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        cr_req.desired_access = SMB2_GENERIC_WRITE;
        cr_req.file_attributes = 0;
        cr_req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE;
        cr_req.create_disposition = SMB2_FILE_OPEN;
        cr_req.create_options = 0;
        cr_req.name = path;

        pdu = smb2_cmd_create_async(smb2, &cr_req, trunc_cb_1, trunc_data);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create create command");
                free(trunc_data);
                return -EINVAL;
        }

        /* SET INFO command */
        eofi.end_of_file = length;

        memset(&si_req, 0, sizeof(struct smb2_set_info_request));
        si_req.info_type = SMB2_0_INFO_FILE;
        si_req.file_info_class = SMB2_FILE_END_OF_FILE_INFORMATION;
        si_req.additional_information = 0;
        memcpy(si_req.file_id, compound_file_id, SMB2_FD_SIZE);
        si_req.input_data = &eofi;

        next_pdu = smb2_cmd_set_info_async(smb2, &si_req,
                                           trunc_cb_2, trunc_data);
        if (next_pdu == NULL) {
                smb2_set_error(smb2, "Failed to create set command. %s",
                               smb2_get_error(smb2));
                free(trunc_data);
                smb2_free_pdu(smb2, pdu);
                return -EINVAL;
        }
        smb2_add_compound_pdu(smb2, pdu, next_pdu);

        /* CLOSE command */
        memset(&cl_req, 0, sizeof(struct smb2_close_request));
        cl_req.flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
        memcpy(cl_req.file_id, compound_file_id, SMB2_FD_SIZE);

        next_pdu = smb2_cmd_close_async(smb2, &cl_req, trunc_cb_3, trunc_data);
        if (next_pdu == NULL) {
                trunc_data->cb(smb2, -ENOMEM, NULL, trunc_data->cb_data);
                free(trunc_data);
                smb2_free_pdu(smb2, pdu);
                return -EINVAL;
        }
        smb2_add_compound_pdu(smb2, pdu, next_pdu);

        smb2_queue_pdu(smb2, pdu);

        return 0;
}

struct rename_cb_data {
        smb2_command_cb cb;
        void *cb_data;
        uint32_t status;
};

static void
rename_cb_3(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct rename_cb_data *rename_data = private_data;

        if (rename_data->status == SMB2_STATUS_SUCCESS) {
                rename_data->status = status;
        }

        rename_data->cb(smb2, -nterror_to_errno(rename_data->status),
                        NULL, rename_data->cb_data);
        free(rename_data);
}

static void
rename_cb_2(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct rename_cb_data *rename_data = private_data;

        if (rename_data->status == SMB2_STATUS_SUCCESS) {
                rename_data->status = status;
        }
}

static void
rename_cb_1(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct rename_cb_data *rename_data = private_data;

        if (rename_data->status == SMB2_STATUS_SUCCESS) {
                rename_data->status = status;
        }
}

int
smb2_rename_async(struct smb2_context *smb2, const char *oldpath,
                  const char *newpath, smb2_command_cb cb, void *cb_data)
{
        struct rename_cb_data *rename_data;
        struct smb2_create_request cr_req;
        struct smb2_set_info_request si_req;
        struct smb2_close_request cl_req;
        struct smb2_pdu *pdu, *next_pdu;
        struct smb2_file_rename_info rn_info;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        rename_data = calloc(1, sizeof(struct rename_cb_data));
        if (rename_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate rename_data");
                return -ENOMEM;
        }

        rename_data->cb = cb;
        rename_data->cb_data = cb_data;

        /* CREATE command */
        memset(&cr_req, 0, sizeof(struct smb2_create_request));
        cr_req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        cr_req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        cr_req.desired_access = SMB2_GENERIC_READ  | SMB2_FILE_READ_ATTRIBUTES | SMB2_DELETE;
        cr_req.file_attributes = 0;
        cr_req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE | SMB2_FILE_SHARE_DELETE;
        cr_req.create_disposition = SMB2_FILE_OPEN;
        cr_req.create_options = 0;
        cr_req.name = oldpath;

        pdu = smb2_cmd_create_async(smb2, &cr_req, rename_cb_1, rename_data);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create create command");
                free(rename_data);
                return -EINVAL;
        }

        /* SET INFO command */
        rn_info.replace_if_exist = 0;
        rn_info.file_name = discard_const(newpath);

        memset(&si_req, 0, sizeof(struct smb2_set_info_request));
        si_req.info_type = SMB2_0_INFO_FILE;
        si_req.file_info_class = SMB2_FILE_RENAME_INFORMATION;
        si_req.additional_information = 0;
        memcpy(si_req.file_id, compound_file_id, SMB2_FD_SIZE);
        si_req.input_data = &rn_info;

        next_pdu = smb2_cmd_set_info_async(smb2, &si_req,
                                           rename_cb_2, rename_data);
        if (next_pdu == NULL) {
                smb2_set_error(smb2, "Failed to create set command. %s",
                               smb2_get_error(smb2));
                free(rename_data);
                smb2_free_pdu(smb2, pdu);
                return -EINVAL;
        }
        smb2_add_compound_pdu(smb2, pdu, next_pdu);

        /* CLOSE command */
        memset(&cl_req, 0, sizeof(struct smb2_close_request));
        cl_req.flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
        memcpy(cl_req.file_id, compound_file_id, SMB2_FD_SIZE);

        next_pdu = smb2_cmd_close_async(smb2, &cl_req, rename_cb_3, rename_data);
        if (next_pdu == NULL) {
                rename_data->cb(smb2, -ENOMEM, NULL, rename_data->cb_data);
                free(rename_data);
                smb2_free_pdu(smb2, pdu);
                return -EINVAL;
        }
        smb2_add_compound_pdu(smb2, pdu, next_pdu);

        smb2_queue_pdu(smb2, pdu);

        return 0;
}

static void
ftrunc_cb_1(struct smb2_context *smb2, int status,
            void *command_data _U_, void *private_data)
{
        struct create_cb_data *cb_data = private_data;

        cb_data->cb(smb2, -nterror_to_errno(status),
                    NULL, cb_data->cb_data);
        free(cb_data);
}

int
smb2_ftruncate_async(struct smb2_context *smb2, struct smb2fh *fh,
                     uint64_t length, smb2_command_cb cb, void *cb_data)
{
        struct create_cb_data *create_data;
        struct smb2_set_info_request req;
        struct smb2_file_end_of_file_info eofi;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }
        if (fh == NULL) {
                smb2_set_error(smb2, "File handle was NULL");
                return -EINVAL;
        }

        create_data = calloc(1, sizeof(struct create_cb_data));
        if (create_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate create_data");
                return -ENOMEM;
        }

        create_data->cb = cb;
        create_data->cb_data = cb_data;

        eofi.end_of_file = length;

        memset(&req, 0, sizeof(struct smb2_set_info_request));
        req.info_type = SMB2_0_INFO_FILE;
        req.file_info_class = SMB2_FILE_END_OF_FILE_INFORMATION;
        req.additional_information = 0;
        memcpy(req.file_id, fh->file_id, SMB2_FD_SIZE);
        req.input_data = &eofi;

        pdu = smb2_cmd_set_info_async(smb2, &req, ftrunc_cb_1, create_data);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create set info command");
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

struct readlink_cb_data {
        smb2_command_cb cb;
        void *cb_data;

        uint32_t status;
        struct smb2_reparse_data_buffer *reparse;
};

static void
readlink_cb_3(struct smb2_context *smb2, int status,
            void *command_data _U_, void *private_data)
{
        struct readlink_cb_data *cb_data = private_data;
        struct smb2_reparse_data_buffer *rp = cb_data->reparse;
        char *target = "<unknown reparse point type>";

        if (rp) {
                switch (rp->reparse_tag) {
                case SMB2_REPARSE_TAG_SYMLINK:
                        target = rp->symlink.subname;
                }
        }
        cb_data->cb(smb2, -nterror_to_errno(cb_data->status),
                    target, cb_data->cb_data);
        smb2_free_data(smb2, rp);
        free(cb_data);
}

static void
readlink_cb_2(struct smb2_context *smb2, int status,
            void *command_data, void *private_data)
{
        struct readlink_cb_data *cb_data = private_data;
        struct smb2_ioctl_reply *rep = command_data;

        if (cb_data->status == SMB2_STATUS_SUCCESS) {
                cb_data->status = status;
        }
        if (status == SMB2_STATUS_NOT_A_REPARSE_POINT) {
                smb2_set_error(smb2, "Not a reparse point");
        }
        if (status == SMB2_STATUS_SUCCESS) {
                cb_data->reparse = rep->output;
        }
}

static void
readlink_cb_1(struct smb2_context *smb2, int status,
            void *command_data _U_, void *private_data)
{
        struct readlink_cb_data *cb_data = private_data;

        if (status != SMB2_STATUS_SUCCESS) {
                smb2_set_error(smb2, "%s", nterror_to_str(status));
        }
        cb_data->status = status;
}

int
smb2_readlink_async(struct smb2_context *smb2, const char *path,
                    smb2_command_cb cb, void *cb_data)
{
        struct readlink_cb_data *readlink_data;
        struct smb2_create_request cr_req;
        struct smb2_ioctl_request io_req;
        struct smb2_close_request cl_req;
        struct smb2_pdu *pdu, *next_pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        readlink_data = calloc(1, sizeof(struct readlink_cb_data));
        if (readlink_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate readlink_data");
                return -ENOMEM;
        }

        readlink_data->cb = cb;
        readlink_data->cb_data = cb_data;

        /* CREATE command */
        memset(&cr_req, 0, sizeof(struct smb2_create_request));
        cr_req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        cr_req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        cr_req.desired_access = SMB2_FILE_READ_ATTRIBUTES;
        cr_req.file_attributes = 0;
        cr_req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE |
                SMB2_FILE_SHARE_DELETE;
        cr_req.create_disposition = SMB2_FILE_OPEN;
        cr_req.create_options = SMB2_FILE_OPEN_REPARSE_POINT;
        cr_req.name = path;

        pdu = smb2_cmd_create_async(smb2, &cr_req, readlink_cb_1, readlink_data);
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to create create command");
                free(readlink_data);
                return -EINVAL;
        }

        /* IOCTL command */
        memset(&io_req, 0, sizeof(struct smb2_ioctl_request));
        io_req.ctl_code = SMB2_FSCTL_GET_REPARSE_POINT;
        memcpy(io_req.file_id, compound_file_id, SMB2_FD_SIZE);
        io_req.input_count = 0;
        io_req.input = NULL;
        io_req.flags = SMB2_0_IOCTL_IS_FSCTL;

        next_pdu = smb2_cmd_ioctl_async(smb2, &io_req, readlink_cb_2,
                                        readlink_data);
        if (next_pdu == NULL) {
                free(readlink_data);
                smb2_free_pdu(smb2, pdu);
                return -EINVAL;
        }
        smb2_add_compound_pdu(smb2, pdu, next_pdu);

        /* CLOSE command */
        memset(&cl_req, 0, sizeof(struct smb2_close_request));
        cl_req.flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
        memcpy(cl_req.file_id, compound_file_id, SMB2_FD_SIZE);

        next_pdu = smb2_cmd_close_async(smb2, &cl_req, readlink_cb_3,
                                        readlink_data);
        if (next_pdu == NULL) {
                free(readlink_data);
                smb2_free_pdu(smb2, pdu);
                return -EINVAL;
        }
        smb2_add_compound_pdu(smb2, pdu, next_pdu);

        smb2_queue_pdu(smb2, pdu);

        return 0;
}

struct disconnect_data {
        smb2_command_cb cb;
        void *cb_data;
};

static void
disconnect_cb_2(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct disconnect_data *dc_data = private_data;

        dc_data->cb(smb2, 0, NULL, dc_data->cb_data);
        free(dc_data);
        if (smb2->change_fd) {
                smb2->change_fd(smb2, smb2->fd, SMB2_DEL_FD);
        }
        close(smb2->fd);
        smb2->fd = -1;
}

static void
disconnect_cb_1(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct disconnect_data *dc_data = private_data;
        struct smb2_pdu *pdu;

        pdu = smb2_cmd_logoff_async(smb2, disconnect_cb_2, dc_data);
        if (pdu == NULL) {
                dc_data->cb(smb2, -ENOMEM, NULL, dc_data->cb_data);
                free(dc_data);
                return;
        }
        smb2_queue_pdu(smb2, pdu);
}

int
smb2_disconnect_share_async(struct smb2_context *smb2,
                            smb2_command_cb cb, void *cb_data)
{
        struct disconnect_data *dc_data;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        if (smb2->fd == -1) {
                smb2_set_error(smb2, "connection is alreeady disconnected or was never connected");
                return -EINVAL;
        }

        dc_data = calloc(1, sizeof(struct disconnect_data));
        if (dc_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate disconnect_data");
                return -ENOMEM;
        }

        dc_data->cb = cb;
        dc_data->cb_data = cb_data;

        pdu = smb2_cmd_tree_disconnect_async(smb2, disconnect_cb_1, dc_data);
        if (pdu == NULL) {
                free(dc_data);
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

struct echo_data {
        smb2_command_cb cb;
        void *cb_data;
};

static void
echo_cb(struct smb2_context *smb2, int status,
           void *command_data _U_, void *private_data)
{
        struct echo_data *cb_data = private_data;

        cb_data->cb(smb2, -nterror_to_errno(status),
                    NULL, cb_data->cb_data);
        free(cb_data);
}

int
smb2_echo_async(struct smb2_context *smb2,
                smb2_command_cb cb, void *cb_data)
{
        struct echo_data *echo_data;
        struct smb2_pdu *pdu;

        if (smb2 == NULL) {
                return -EINVAL;
        }

        echo_data = calloc(1, sizeof(struct echo_data));
        if (echo_data == NULL) {
                smb2_set_error(smb2, "Failed to allocate echo_data");
                return -ENOMEM;
        }

        echo_data->cb = cb;
        echo_data->cb_data = cb_data;

        pdu = smb2_cmd_echo_async(smb2, echo_cb, echo_data);
        if (pdu == NULL) {
                free(echo_data);
                return -ENOMEM;
        }
        smb2_queue_pdu(smb2, pdu);

        return 0;
}

uint32_t
smb2_get_max_read_size(struct smb2_context *smb2)
{
        return smb2->max_read_size;
}

uint32_t
smb2_get_max_write_size(struct smb2_context *smb2)
{
        return smb2->max_write_size;
}

smb2_file_id *
smb2_get_file_id(struct smb2fh *fh)
{
        return &fh->file_id;
}

struct smb2fh *
smb2_fh_from_file_id(struct smb2_context *smb2, smb2_file_id *fileid)
{
        struct smb2fh *fh;

        fh = calloc(1, sizeof(struct smb2fh));
        if (fh == NULL) {
                return NULL;
        }
        memcpy(fh->file_id, fileid, SMB2_FD_SIZE);
        SMB2_LIST_ADD(&smb2->fhs, fh);

        return fh;
}

void
smb2_fd_event_callbacks(struct smb2_context *smb2,
                        smb2_change_fd_cb change_fd,
                        smb2_change_events_cb change_events)
{
        smb2->change_fd = change_fd;
        smb2->change_events = change_events;
}
