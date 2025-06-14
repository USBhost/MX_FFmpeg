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

static int
smb2_encode_create_request(struct smb2_context *smb2,
                           struct smb2_pdu *pdu,
                           struct smb2_create_request *req)
{
        int i, len;
        uint8_t *buf;
        uint16_t ch;
        struct utf16 *name = NULL;
        struct smb2_iovec *iov;

        len = SMB2_CREATE_REQUEST_SIZE & 0xfffffffe;
        buf = calloc(len, sizeof(uint8_t));
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate create buffer");
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
                /* name length */
                smb2_set_uint16(iov, 46, 2 * name->len);
        }

        smb2_set_uint16(iov, 0, SMB2_CREATE_REQUEST_SIZE);
        smb2_set_uint8(iov, 2, req->security_flags);
        smb2_set_uint8(iov, 3, req->requested_oplock_level);
        smb2_set_uint32(iov, 4, req->impersonation_level);
        smb2_set_uint64(iov, 8, req->smb_create_flags);
        smb2_set_uint32(iov, 24, req->desired_access);
        smb2_set_uint32(iov, 28, req->file_attributes);
        smb2_set_uint32(iov, 32, req->share_access);
        smb2_set_uint32(iov, 36, req->create_disposition);
        smb2_set_uint32(iov, 40, req->create_options);
        /* name offset */
        smb2_set_uint16(iov, 44, SMB2_HEADER_SIZE + 56);
        smb2_set_uint32(iov, 52, req->create_context_length);

        /* Name */
        if (name) {
                buf = malloc(2 * name->len);
                if (buf == NULL) {
                        smb2_set_error(smb2, "Failed to allocate create name");
                        free(name);
                        return -1;
                }
                memcpy(buf, &name->val[0], 2 * name->len);
                iov = smb2_add_iovector(smb2, &pdu->out,
                                        buf,
                                        2 * name->len,
                                        free);
                /* Convert '/' to '\' */
                for (i = 0; i < name->len; i++) {
                        smb2_get_uint16(iov, i * 2, &ch);
                        if (ch == 0x002f) {
                                smb2_set_uint16(iov, i * 2, 0x005c);
                        }
                }
        }
        free(name);

        /* Create Context */
        if (req->create_context_length) {
                smb2_set_error(smb2, "Create context not implemented, yet");
                return -1;
        }

        /* The buffer must contain at least one byte, even if name is "" 
         * and there is no create context.
         */
        if (name == NULL && !req->create_context_length) {
                static uint8_t zero;

                iov = smb2_add_iovector(smb2, &pdu->out,
                                        &zero, 1, NULL);
        }
        
        return 0;
}

struct smb2_pdu *
smb2_cmd_create_async(struct smb2_context *smb2,
                      struct smb2_create_request *req,
                      smb2_command_cb cb, void *cb_data)
{
        struct smb2_pdu *pdu;

        pdu = smb2_allocate_pdu(smb2, SMB2_CREATE, cb, cb_data);
        if (pdu == NULL) {
                return NULL;
        }

        if (smb2_encode_create_request(smb2, pdu, req)) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }
        
        if (smb2_pad_to_64bit(smb2, &pdu->out) != 0) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }

        return pdu;
}

#define IOV_OFFSET (rep->create_context_offset - SMB2_HEADER_SIZE - \
                    (SMB2_CREATE_REPLY_SIZE & 0xfffe))

int
smb2_process_create_fixed(struct smb2_context *smb2,
                          struct smb2_pdu *pdu)
{
        struct smb2_create_reply *rep;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];
        uint16_t struct_size;

        rep = malloc(sizeof(*rep));
        if (rep == NULL) {
                smb2_set_error(smb2, "Failed to allocate create reply");
                return -1;
        }
        pdu->payload = rep;

        smb2_get_uint16(iov, 0, &struct_size);
        if (struct_size != SMB2_CREATE_REPLY_SIZE ||
            (struct_size & 0xfffe) != iov->len) {
                smb2_set_error(smb2, "Unexpected size of Create. "
                               "Expected %d, got %d",
                               SMB2_CREATE_REPLY_SIZE,
                               (int)iov->len);
                return -1;
        }

        smb2_get_uint8(iov, 2, &rep->oplock_level);
        smb2_get_uint8(iov, 3, &rep->flags);
        smb2_get_uint32(iov, 4, &rep->create_action);
        smb2_get_uint64(iov, 8, &rep->creation_time);
        smb2_get_uint64(iov, 16, &rep->last_access_time);
        smb2_get_uint64(iov, 24, &rep->last_write_time);
        smb2_get_uint64(iov, 32, &rep->change_time);
        smb2_get_uint64(iov, 40, &rep->allocation_size);
        smb2_get_uint64(iov, 48, &rep->end_of_file);
        smb2_get_uint32(iov, 56, &rep->file_attributes);
        memcpy(rep->file_id, iov->buf + 64, SMB2_FD_SIZE);
        smb2_get_uint32(iov, 80, &rep->create_context_offset);
        smb2_get_uint32(iov, 84, &rep->create_context_length);

        if (rep->create_context_length == 0) {
                return 0;
        }

        if (rep->create_context_offset < SMB2_HEADER_SIZE +
            (SMB2_CREATE_REPLY_SIZE & 0xfffe)) {
                smb2_set_error(smb2, "Create context overlaps with "
                               "reply header");
                return -1;
        }

        /* Return the amount of data that the security buffer will take up.
         * Including any padding before the security buffer itself.
         */
        return IOV_OFFSET + rep->create_context_length;
}

int
smb2_process_create_variable(struct smb2_context *smb2,
                             struct smb2_pdu *pdu)
{
        struct smb2_create_reply *rep = pdu->payload;

        /* No support for createcontext yet*/
        /* Create Context */
        if (rep->create_context_length) {
                smb2_set_error(smb2, "Create context not implemented, yet");
                return -1;
        }

        return 0;
}
