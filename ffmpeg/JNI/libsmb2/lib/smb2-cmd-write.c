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
smb2_encode_write_request(struct smb2_context *smb2,
                          struct smb2_pdu *pdu,
                          struct smb2_write_request *req)
{
        int len;
        uint8_t *buf;
        struct smb2_iovec *iov;

        len = SMB2_WRITE_REQUEST_SIZE & 0xfffffffe;
        buf = calloc(len, sizeof(uint8_t));
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate write buffer");
                return -1;
        }

        iov = smb2_add_iovector(smb2, &pdu->out, buf, len, free);

        if (!smb2->supports_multi_credit && req->length > 64 * 1024) {
                req->length = 64 * 1024;
        }
        smb2_set_uint16(iov, 0, SMB2_WRITE_REQUEST_SIZE);
        smb2_set_uint16(iov, 2, SMB2_HEADER_SIZE + 48);
        smb2_set_uint32(iov, 4, req->length);
        smb2_set_uint64(iov, 8, req->offset);
        memcpy(iov->buf + 16, req->file_id, SMB2_FD_SIZE);
        smb2_set_uint32(iov, 32, req->channel);
        smb2_set_uint32(iov, 36, req->remaining_bytes);
        smb2_set_uint16(iov, 42, req->write_channel_info_length);
        smb2_set_uint32(iov, 44, req->flags);

        if (req->write_channel_info_length > 0 ||
            req->write_channel_info != NULL) {
                smb2_set_error(smb2, "ChannelInfo not yet implemented");
                return -1;
        }

        return 0;
}

struct smb2_pdu *
smb2_cmd_write_async(struct smb2_context *smb2,
                     struct smb2_write_request *req,
                     smb2_command_cb cb, void *cb_data)
{
        struct smb2_pdu *pdu;

        pdu = smb2_allocate_pdu(smb2, SMB2_WRITE, cb, cb_data);
        if (pdu == NULL) {
                return NULL;
        }

        if (smb2_encode_write_request(smb2, pdu, req)) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }

        smb2_add_iovector(smb2, &pdu->out, (uint8_t*)req->buf,
                          req->length, NULL);
        
        if (smb2_pad_to_64bit(smb2, &pdu->out) != 0) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }

        /* Adjust credit charge for large payloads */
        if (smb2->supports_multi_credit) {
                pdu->header.credit_charge = (req->length - 1) / 65536 + 1; // 3.1.5.2 of [MS-SMB2]
        }

        return pdu;
}

int
smb2_process_write_fixed(struct smb2_context *smb2,
                         struct smb2_pdu *pdu)
{
        struct smb2_write_reply *rep;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];
        uint16_t struct_size;

        rep = malloc(sizeof(*rep));
        if (rep == NULL) {
                smb2_set_error(smb2, "Failed to allocate write reply");
                return -1;
        }
        pdu->payload = rep;

        smb2_get_uint16(iov, 0, &struct_size);
        if (struct_size != SMB2_WRITE_REPLY_SIZE ||
            (struct_size & 0xfffe) != iov->len) {
                smb2_set_error(smb2, "Unexpected size of Write "
                               "reply. Expected %d, got %d",
                               SMB2_WRITE_REPLY_SIZE,
                               (int)iov->len);
                return -1;
        }

        smb2_get_uint32(iov, 4, &rep->count);
        smb2_get_uint32(iov, 8, &rep->remaining);

        return 0;
}
