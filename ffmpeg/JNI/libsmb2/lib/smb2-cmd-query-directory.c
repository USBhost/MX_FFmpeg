/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2016 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

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

#include <errno.h>

#include "compat.h"

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-private.h"

int
smb2_decode_fileidfulldirectoryinformation(
    struct smb2_context *smb2,
    struct smb2_fileidfulldirectoryinformation *fs,
    struct smb2_iovec *vec)
{
        uint32_t name_len;
        uint64_t t;

        /* Make sure the name fits before end of vector.
         * As the name is the final part of this blob this guarantees
         * that all other fields also fit within the remainder of the
         * vector.
         */
        smb2_get_uint32(vec, 60, &name_len);
        if (name_len > 80 + name_len ||
            80 + name_len > vec->len) {
                smb2_set_error(smb2, "Malformed name in query.\n");
                return -1;
        }

        smb2_get_uint32(vec, 0, &fs->next_entry_offset);
        smb2_get_uint32(vec, 4, &fs->file_index);
        smb2_get_uint64(vec, 40, &fs->end_of_file);
        smb2_get_uint64(vec, 48, &fs->allocation_size);
        smb2_get_uint32(vec, 56, &fs->file_attributes);
        smb2_get_uint32(vec, 64, &fs->ea_size);
        smb2_get_uint64(vec, 72, &fs->file_id);

        fs->name = utf16_to_utf8((uint16_t *)&vec->buf[80], name_len / 2);

        smb2_get_uint64(vec, 8, &t);
        win_to_timeval(t, &fs->creation_time);

        smb2_get_uint64(vec, 16, &t);
        win_to_timeval(t, &fs->last_access_time);

        smb2_get_uint64(vec, 24, &t);
        win_to_timeval(t, &fs->last_write_time);

        smb2_get_uint64(vec, 32, &t);
        win_to_timeval(t, &fs->change_time);

        return 0;
}

static int
smb2_encode_query_directory_request(struct smb2_context *smb2,
                                    struct smb2_pdu *pdu,
                                    struct smb2_query_directory_request *req)
{
        int len;
        uint8_t *buf;
        struct utf16 *name = NULL;
        struct smb2_iovec *iov;

        len = SMB2_QUERY_DIRECTORY_REQUEST_SIZE & 0xfffffffe;
        buf = calloc(len, sizeof(uint8_t));
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate query buffer");
                return -1;
        }

        iov = smb2_add_iovector(smb2, &pdu->out, buf, len, free);

        /* Name */
        if (req->name && req->name[0]) {
                name = utf8_to_utf16(req->name);
                if (name == NULL) {
                        smb2_set_error(smb2, "Could not convert name into UTF-16");
                        return -1;
                }
                smb2_set_uint16(iov, 26, 2 * name->len);
        }
        
        smb2_set_uint16(iov, 0, SMB2_QUERY_DIRECTORY_REQUEST_SIZE);
        smb2_set_uint8(iov, 2, req->file_information_class);
        smb2_set_uint8(iov, 3, req->flags);
        smb2_set_uint32(iov, 4, req->file_index);
        memcpy(iov->buf + 8, req->file_id, SMB2_FD_SIZE);
        smb2_set_uint16(iov, 24, SMB2_HEADER_SIZE + 32);
        smb2_set_uint32(iov, 28, req->output_buffer_length);

        /* Name */
        if (name) {
                buf = malloc(2 * name->len);
                if (buf == NULL) {
                        smb2_set_error(smb2, "Failed to allocate qdir name");
                        free(name);
                        return -1;
                }
                memcpy(buf, &name->val[0], 2 * name->len);
                iov = smb2_add_iovector(smb2, &pdu->out,
                                        buf,
                                        2 * name->len,
                                        free);
        }
        free(name);
        
        return 0;
}

struct smb2_pdu *
smb2_cmd_query_directory_async(struct smb2_context *smb2,
                               struct smb2_query_directory_request *req,
                               smb2_command_cb cb, void *cb_data)
{
        struct smb2_pdu *pdu;

        pdu = smb2_allocate_pdu(smb2, SMB2_QUERY_DIRECTORY, cb, cb_data);
        if (pdu == NULL) {
                return NULL;
        }

        if (smb2_encode_query_directory_request(smb2, pdu, req)) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }

        if (smb2_pad_to_64bit(smb2, &pdu->out) != 0) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }

        /* Adjust credit charge for large payloads */
        if (smb2->supports_multi_credit) {
                pdu->header.credit_charge =
                        (req->output_buffer_length - 1) / 65536 + 1; // 3.1.5.2 of [MS-SMB2]
        }

        return pdu;
}

#define IOV_OFFSET (rep->output_buffer_offset - SMB2_HEADER_SIZE - \
                    (SMB2_QUERY_DIRECTORY_REPLY_SIZE & 0xfffe))

int
smb2_process_query_directory_fixed(struct smb2_context *smb2,
                                   struct smb2_pdu *pdu)
{
        struct smb2_query_directory_reply *rep;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];
        uint16_t struct_size;

        rep = malloc(sizeof(*rep));
        if (rep == NULL) {
                smb2_set_error(smb2, "Failed to allocate query dir reply");
                return -1;
        }
        pdu->payload = rep;

        smb2_get_uint16(iov, 0, &struct_size);
        if (struct_size != SMB2_QUERY_DIRECTORY_REPLY_SIZE ||
            (struct_size & 0xfffe) != iov->len) {
                smb2_set_error(smb2, "Unexpected size of Query Dir "
                               "reply. Expected %d, got %d",
                               SMB2_QUERY_DIRECTORY_REPLY_SIZE,
                               (int)iov->len);
                return -1;
        }

        smb2_get_uint16(iov, 2, &rep->output_buffer_offset);
        smb2_get_uint32(iov, 4, &rep->output_buffer_length);

        if (rep->output_buffer_length == 0) {
                return 0;
        }

        if (rep->output_buffer_offset < SMB2_HEADER_SIZE +
            (SMB2_QUERY_INFO_REPLY_SIZE & 0xfffe)) {
                smb2_set_error(smb2, "Output buffer overlaps with "
                               "Query Dir reply header");
                return -1;
        }

        /* Return the amount of data that the output buffer will take up.
         * Including any padding before the output buffer itself.
         */
        return IOV_OFFSET + rep->output_buffer_length;
}

int
smb2_process_query_directory_variable(struct smb2_context *smb2,
                                      struct smb2_pdu *pdu)
{
        struct smb2_query_directory_reply *rep = pdu->payload;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];

        rep->output_buffer = &iov->buf[IOV_OFFSET];

        return 0;
}
