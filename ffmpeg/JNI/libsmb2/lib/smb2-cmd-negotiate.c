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

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-private.h"

static int
smb2_encode_negotiate_request(struct smb2_context *smb2,
                              struct smb2_pdu *pdu,
                              struct smb2_negotiate_request *req)
{
        uint8_t *buf;
        int i, len;
        struct smb2_iovec *iov;
        
        len = SMB2_NEGOTIATE_REQUEST_SIZE +
                req->dialect_count * sizeof(uint16_t);
        len = PAD_TO_32BIT(len);
        buf = calloc(len, sizeof(uint8_t));
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate negotiate buffer");
                return -1;
        }
        
        iov = smb2_add_iovector(smb2, &pdu->out, buf, len, free);
        
        smb2_set_uint16(iov, 0, SMB2_NEGOTIATE_REQUEST_SIZE);
        smb2_set_uint16(iov, 2, req->dialect_count);
        smb2_set_uint16(iov, 4, req->security_mode);
        smb2_set_uint32(iov, 8, req->capabilities);
        memcpy(iov->buf + 12, req->client_guid, SMB2_GUID_SIZE);
        smb2_set_uint64(iov, 28, req->client_start_time);
        for (i = 0; i < req->dialect_count; i++) {
                smb2_set_uint16(iov, 36 + i * sizeof(uint16_t),
                                req->dialects[i]);
        }

        return 0;
}

struct smb2_pdu *
smb2_cmd_negotiate_async(struct smb2_context *smb2,
                         struct smb2_negotiate_request *req,
                         smb2_command_cb cb, void *cb_data)
{
        struct smb2_pdu *pdu;
        
        pdu = smb2_allocate_pdu(smb2, SMB2_NEGOTIATE, cb, cb_data);
        if (pdu == NULL) {
                return NULL;
        }

        if (smb2_encode_negotiate_request(smb2, pdu, req)) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }
        
        if (smb2_pad_to_64bit(smb2, &pdu->out) != 0) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }

        return pdu;
}

#define IOV_OFFSET (rep->security_buffer_offset - SMB2_HEADER_SIZE - \
                    (SMB2_NEGOTIATE_REPLY_SIZE & 0xfffe))

int
smb2_process_negotiate_fixed(struct smb2_context *smb2,
                             struct smb2_pdu *pdu)
{
        struct smb2_negotiate_reply *rep;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];
        uint16_t struct_size;

        rep = malloc(sizeof(*rep));
        if (rep == NULL) {
                smb2_set_error(smb2, "Failed to allocate negotiate reply");
                return -1;
        }
        pdu->payload = rep;
               
        smb2_get_uint16(iov, 0, &struct_size);
        if (struct_size != SMB2_NEGOTIATE_REPLY_SIZE ||
            (struct_size & 0xfffe) != iov->len) {
                smb2_set_error(smb2, "Unexpected size of Negotiate "
                               "reply. Expected %d, got %d",
                               SMB2_NEGOTIATE_REPLY_SIZE,
                               (int)iov->len);
                return -1;
        }
        
        smb2_get_uint16(iov, 2, &rep->security_mode);
        smb2_get_uint16(iov, 4, &rep->dialect_revision);
        memcpy(rep->server_guid, iov->buf + 8, SMB2_GUID_SIZE);
        smb2_get_uint32(iov, 24, &rep->capabilities);
        smb2_get_uint32(iov, 28, &rep->max_transact_size);
        smb2_get_uint32(iov, 32, &rep->max_read_size);
        smb2_get_uint32(iov, 36, &rep->max_write_size);
        smb2_get_uint64(iov, 40, &rep->system_time);
        smb2_get_uint64(iov, 48, &rep->server_start_time);
        smb2_get_uint16(iov, 56, &rep->security_buffer_offset);
        smb2_get_uint16(iov, 58, &rep->security_buffer_length);

        if (rep->security_buffer_length == 0) {
                return 0;
        }
        if (rep->security_buffer_offset < SMB2_HEADER_SIZE +
            (SMB2_NEGOTIATE_REPLY_SIZE & 0xfffe)) {
                smb2_set_error(smb2, "Securty buffer overlaps with "
                               "negotiate reply header");
                return -1;
        }

        /* Return the amount of data that the security buffer will take up.
         * Including any padding before the security buffer itself.
         */
        return IOV_OFFSET + rep->security_buffer_length;
}

int
smb2_process_negotiate_variable(struct smb2_context *smb2,
                                struct smb2_pdu *pdu)
{
        struct smb2_negotiate_reply *rep = pdu->payload;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];

        rep->security_buffer = &iov->buf[IOV_OFFSET];

        return 0;
}
