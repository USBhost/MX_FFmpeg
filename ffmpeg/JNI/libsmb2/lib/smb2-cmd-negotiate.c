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
smb2_encode_preauth_context(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        uint8_t *buf;
        int len, i, data_len;
        struct smb2_iovec *iov;

        /* Preauth integrity capability */
        data_len = PAD_TO_64BIT(38);
        len = 8 + data_len;
        buf = malloc(len);
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate preauth context");
                return -1;
        }
        memset(buf, 0, len);

        iov = smb2_add_iovector(smb2, &pdu->out, buf, len, free);
        smb2_set_uint16(iov, 0, SMB2_PREAUTH_INTEGRITY_CAP);
        smb2_set_uint16(iov, 2, data_len);
        smb2_set_uint16(iov, 8, 1);
        smb2_set_uint16(iov, 10, 32);
        smb2_set_uint16(iov, 12, SMB2_HASH_SHA_512);

        for (i = 0; i < SMB2_SALT_SIZE; i++) {
                smb2_set_uint8(iov, 14 + i, smb2->salt[i]);
        }
        return 0;
}

static int
smb2_encode_encryption_context(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        uint8_t *buf;
        int len, data_len;
        struct smb2_iovec *iov;

        data_len = PAD_TO_64BIT(4);
        len = 8 + data_len;
        len = PAD_TO_64BIT(len);
        buf = malloc(len);
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate encryption context");
                return -1;
        }
        memset(buf, 0, len);

        iov = smb2_add_iovector(smb2, &pdu->out, buf, len, free);
        smb2_set_uint16(iov, 0, SMB2_ENCRYPTION_CAP);
        smb2_set_uint16(iov, 2, data_len);
        smb2_set_uint16(iov, 8, 1);
        smb2_set_uint16(iov, 10, SMB2_ENCRYPTION_AES_128_CCM);

        return 0;
}

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
        if (smb2->version == SMB2_VERSION_ANY ||
            smb2->version == SMB2_VERSION_ANY3 ||
            smb2->version == SMB2_VERSION_0311) {
                /* Negotiate contexts are alinged at 64bit boundaries */
                if (len & 0x04) {
                        len += 4;
                }
        }
        buf = calloc(len, sizeof(uint8_t));
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate negotiate buffer");
                return -1;
        }

        iov = smb2_add_iovector(smb2, &pdu->out, buf, len, free);
        
        if (smb2->version == SMB2_VERSION_ANY ||
            smb2->version == SMB2_VERSION_ANY3 ||
            smb2->version == SMB2_VERSION_0311) {
                req->negotiate_context_offset = len + SMB2_HEADER_SIZE;

                if (smb2_encode_preauth_context(smb2, pdu)) {
                        return -1;
                }
                req->negotiate_context_count++;

                if (smb2_encode_encryption_context(smb2, pdu)) {
                        return -1;
                }
                req->negotiate_context_count++;
        }

        smb2_set_uint16(iov, 0, SMB2_NEGOTIATE_REQUEST_SIZE);
        smb2_set_uint16(iov, 2, req->dialect_count);
        smb2_set_uint16(iov, 4, req->security_mode);
        smb2_set_uint32(iov, 8, req->capabilities);
        memcpy(iov->buf + 12, req->client_guid, SMB2_GUID_SIZE);
        smb2_set_uint32(iov, 28, req->negotiate_context_offset);
        smb2_set_uint16(iov, 32, req->negotiate_context_count);
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

static int
smb2_parse_encryption_context(struct smb2_context *smb2,
                              struct smb2_negotiate_reply *rep,
                              struct smb2_iovec *iov,
                              int offset)
{
        smb2_get_uint16(iov, offset, &rep->cypher);
        return 0;
}

static int
smb2_parse_negotiate_contexts(struct smb2_context *smb2,
                              struct smb2_negotiate_reply *rep,
                              struct smb2_iovec *iov,
                              int offset, int count)
{
        uint16_t type, len;

        while (count--) {
                smb2_get_uint16(iov, offset, &type);
                offset += 2;
                smb2_get_uint16(iov, offset, &len);
                offset += 6;

                switch (type) {
                case SMB2_PREAUTH_INTEGRITY_CAP:
                        break;
                case SMB2_ENCRYPTION_CAP:
                        if (smb2_parse_encryption_context(smb2, rep,
                                                          iov, offset)) {
                                return -1;
                        }
                        break;
                default:
                        smb2_set_error(smb2, "Unknown negotiate context "
                                       "type 0x%04x", type);
                        return -1;
                }
                offset += len;
                if (offset > iov->len) {
                        smb2_set_error(smb2, "Bad len in negotiate context\n");
                        return -1;
                }
                offset = PAD_TO_64BIT(offset);
        }

        return 0;
}

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

        smb2_get_uint16(iov, 6, &rep->negotiate_context_count);
        smb2_get_uint32(iov, 60, &rep->negotiate_context_offset);

        if (rep->security_buffer_length == 0) {
                return 0;
        }
        if (rep->security_buffer_offset < SMB2_HEADER_SIZE +
            (SMB2_NEGOTIATE_REPLY_SIZE & 0xfffe)) {
                smb2_set_error(smb2, "Securty buffer overlaps with "
                               "negotiate reply header");
                return -1;
        }

        /*
         * In SMB3.1.1 and later we have negotiate contexts at the end of the
         * blob but we can not compute how big they are from just
         * looking at the smb2 header of the fixed part of the negotiate reply
         * so just return all the remaining data as the variable size.
         * The contexts are technically where padding should be.
         */
        if (rep->dialect_revision >= SMB2_VERSION_0311) {
                return smb2->spl - SMB2_HEADER_SIZE -
                        (SMB2_NEGOTIATE_REPLY_SIZE & 0xfffe);
        } else {
                return IOV_OFFSET + rep->security_buffer_length;
        }
}

int
smb2_process_negotiate_variable(struct smb2_context *smb2,
                                struct smb2_pdu *pdu)
{
        struct smb2_negotiate_reply *rep = pdu->payload;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];
        int offset;

        rep->security_buffer = &iov->buf[IOV_OFFSET];

        if (rep->dialect_revision < SMB2_VERSION_0311 ||
            !rep->negotiate_context_count) {
                return 0;
        }

        offset = rep->negotiate_context_offset - SMB2_HEADER_SIZE -
                (SMB2_NEGOTIATE_REPLY_SIZE & 0xfffe);

        if (offset < 0 || offset > iov->len) {
                return -1;
        }

        if (smb2_parse_negotiate_contexts(smb2, rep, iov, offset,
                                          rep->negotiate_context_count)) {
                return -1;
        }

        return 0;
}
