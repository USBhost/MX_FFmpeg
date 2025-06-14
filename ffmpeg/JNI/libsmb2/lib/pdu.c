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

#ifndef PS2_IOP_PLATFORM
#include <time.h>
#endif

#include "compat.h"

#include "portable-endian.h"

#include "slist.h"
#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-private.h"
#include "smb3-seal.h"
#include "smb2-signing.h"

int
smb2_pad_to_64bit(struct smb2_context *smb2, struct smb2_io_vectors *v)
{
        static uint8_t zero_bytes[7];
        int i, len = 0;

        for (i = 0; i < v->niov; i++) {
                len += v->iov[i].len;
        }
        if ((len & 0x07) == 0) {
                return 0;
        }
        if (smb2_add_iovector(smb2, v, &zero_bytes[0], 8 - (len & 0x07), NULL)
            == NULL) {
                return -1;
        }

        return 0;
}

struct smb2_pdu *
smb2_allocate_pdu(struct smb2_context *smb2, enum smb2_command command,
                  smb2_command_cb cb, void *cb_data)
{
	struct smb2_pdu *pdu;
        struct smb2_header *hdr;
        char magic[4] = {0xFE, 'S', 'M', 'B'};
        
        pdu = calloc(1, sizeof(struct smb2_pdu));
        if (pdu == NULL) {
                smb2_set_error(smb2, "Failed to allocate pdu");
                return NULL;
        }

        hdr = &pdu->header;
        
        memcpy(hdr->protocol_id, magic, 4);

        /* ZERO out the signature
         * Signature calculation happens by zeroing out
         */
        memset(hdr->signature, 0, 16);

        hdr->struct_size = SMB2_HEADER_SIZE;
        hdr->command = command;
        hdr->flags = 0;
        hdr->sync.process_id = 0xFEFF;

        if (smb2->dialect == SMB2_VERSION_0202) {
                hdr->credit_charge = 0;
        } else if (hdr->command == SMB2_NEGOTIATE) {
                /* We don't have any credits yet during negprot by
                 * looking at traces.
                 */
                hdr->credit_charge = 0;
        } else {
                /* Assume the credits for this PDU will be 1.
                 * READ/WRITE/IOCTL/QUERYDIR that consumes more than
                 * 1 credit will adjusted this after it has marshalled the
                 * fixed part of the PDU.
                 */
                hdr->credit_charge = 1;
        }
        hdr->credit_request_response = MAX_CREDITS - smb2->credits;

        switch (command) {
        case SMB2_NEGOTIATE:
        case SMB2_SESSION_SETUP:
        case SMB2_LOGOFF:
        case SMB2_ECHO:
        /* case SMB2_CANCEL: */
                break;
        default:
                hdr->sync.tree_id = smb2->tree_id;
        }

        switch (command) {
        case SMB2_NEGOTIATE:
                break;
        default:
               hdr->session_id = smb2->session_id;
        }

        pdu->cb = cb;
        pdu->cb_data = cb_data;
        pdu->out.niov = 0;

        smb2_add_iovector(smb2, &pdu->out, pdu->hdr, SMB2_HEADER_SIZE, NULL);
        
        switch (command) {
        case SMB2_NEGOTIATE:
        case SMB2_SESSION_SETUP:
                break;
        default:
                if (smb2->seal) {
                        pdu->seal = 1;
                }
        }

        if (smb2->timeout) {
                pdu->timeout = time(NULL) + smb2->timeout;
        }

        return pdu;
}

void
smb2_add_compound_pdu(struct smb2_context *smb2,
                      struct smb2_pdu *pdu, struct smb2_pdu *next_pdu)
{
        int i, offset;

        /* find the last pdu in the chain */
        while (pdu->next_compound) {
                pdu = pdu->next_compound;
        }
        pdu->next_compound = next_pdu;

        /* Fixup the next offset in the header */
        for (i = 0, offset = 0; i < pdu->out.niov; i++) {
                offset += pdu->out.iov[i].len;
        }

        pdu->header.next_command = offset;
        smb2_set_uint32(&pdu->out.iov[0], 20, pdu->header.next_command);

        /* Fixup flags */
        next_pdu->header.flags |= SMB2_FLAGS_RELATED_OPERATIONS;
        smb2_set_uint32(&next_pdu->out.iov[0], 16, next_pdu->header.flags);
}

void
smb2_free_pdu(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        if (pdu->next_compound) {
                smb2_free_pdu(smb2, pdu->next_compound);
        }

        smb2_free_iovector(smb2, &pdu->out);
        smb2_free_iovector(smb2, &pdu->in);

        free(pdu->payload);
        free(pdu->crypt);
        free(pdu);
}

int
smb2_set_uint8(struct smb2_iovec *iov, int offset, uint8_t value)
{
        if (offset + sizeof(uint8_t) > iov->len) {
                return -1;
        }
        iov->buf[offset] = value;
        return 0;
}

int
smb2_set_uint16(struct smb2_iovec *iov, int offset, uint16_t value)
{
        if (offset + sizeof(uint16_t) > iov->len) {
                return -1;
        }
        *(uint16_t *)(iov->buf + offset) = htole16(value);
        return 0;
}

int
smb2_set_uint32(struct smb2_iovec *iov, int offset, uint32_t value)
{
        if (offset + sizeof(uint32_t) > iov->len) {
                return -1;
        }
        *(uint32_t *)(iov->buf + offset) = htole32(value);
        return 0;
}

int
smb2_set_uint64(struct smb2_iovec *iov, int offset, uint64_t value)
{
        if (offset + sizeof(uint64_t) > iov->len) {
                return -1;
        }
        value = htole64(value);
        memcpy(iov->buf + offset, &value, 8);
        return 0;
}

int
smb2_get_uint8(struct smb2_iovec *iov, int offset, uint8_t *value)
{
        if (offset + sizeof(uint8_t) > iov->len) {
                return -1;
        }
        *value = iov->buf[offset];
        return 0;
}

int
smb2_get_uint16(struct smb2_iovec *iov, int offset, uint16_t *value)
{
        uint16_t tmp;
        
        if (offset + sizeof(uint16_t) > iov->len) {
                return -1;
        }
        memcpy(&tmp, iov->buf + offset, sizeof(uint16_t));
        *value = le16toh(tmp);
        return 0;
}

int
smb2_get_uint32(struct smb2_iovec *iov, int offset, uint32_t *value)
{
        uint32_t tmp;
        
        if (offset + sizeof(uint32_t) > iov->len) {
                return -1;
        }
        memcpy(&tmp, iov->buf + offset, sizeof(uint32_t));
        *value = le32toh(tmp);
        return 0;
}

int
smb2_get_uint64(struct smb2_iovec *iov, int offset, uint64_t *value)
{
        uint64_t tmp;
        
        if (offset + sizeof(uint64_t) > iov->len) {
                return -1;
        }
        memcpy(&tmp, iov->buf + offset, sizeof(uint64_t));
        *value = le64toh(tmp);
        return 0;
}

static void
smb2_encode_header(struct smb2_context *smb2, struct smb2_iovec *iov,
                   struct smb2_header *hdr)
{
        hdr->message_id = smb2->message_id++;
        if (hdr->credit_charge > 1) {
                smb2->message_id += (hdr->credit_charge - 1);
        }

        memcpy(iov->buf, hdr->protocol_id, 4);
        smb2_set_uint16(iov, 4, hdr->struct_size);
        smb2_set_uint16(iov, 6, hdr->credit_charge);
        smb2_set_uint32(iov, 8, hdr->status);
        smb2_set_uint16(iov, 12, hdr->command);
        smb2_set_uint16(iov, 14, hdr->credit_request_response);
        smb2_set_uint32(iov, 16, hdr->flags);
        smb2_set_uint32(iov, 20, hdr->next_command);
        smb2_set_uint64(iov, 24, hdr->message_id);

        if (hdr->flags & SMB2_FLAGS_ASYNC_COMMAND) {
                smb2_set_uint64(iov, 32, hdr->async.async_id);
        } else {
                smb2_set_uint32(iov, 32, hdr->sync.process_id);
                smb2_set_uint32(iov, 36, hdr->sync.tree_id);
        }

        smb2_set_uint64(iov, 40, hdr->session_id);
        memcpy(iov->buf + 48, hdr->signature, 16);
}

int
smb2_decode_header(struct smb2_context *smb2, struct smb2_iovec *iov,
                   struct smb2_header *hdr)
{
        static char smb2sign[4] = {0xFE, 'S', 'M', 'B'};

        if (iov->len < SMB2_HEADER_SIZE) {
                smb2_set_error(smb2, "io vector for header is too small");
                return -1;
        }
        if (memcmp(iov->buf, smb2sign, 4)) {
                smb2_set_error(smb2, "bad SMB signature in header");
                return -1;
        }
        memcpy(&hdr->protocol_id, iov->buf, 4);
        smb2_get_uint16(iov, 4, &hdr->struct_size);
        smb2_get_uint16(iov, 6, &hdr->credit_charge);
        smb2_get_uint32(iov, 8, &hdr->status);
        smb2_get_uint16(iov, 12, &hdr->command);
        smb2_get_uint16(iov, 14, &hdr->credit_request_response);
        smb2_get_uint32(iov, 16, &hdr->flags);
        smb2_get_uint32(iov, 20, &hdr->next_command);
        smb2_get_uint64(iov, 24, &hdr->message_id);

        if (hdr->flags & SMB2_FLAGS_ASYNC_COMMAND) {
                smb2_get_uint64(iov, 32, &hdr->async.async_id);
        } else {
                smb2_get_uint32(iov, 32, &hdr->sync.process_id);
                smb2_get_uint32(iov, 36, &hdr->sync.tree_id);
        }
        
        smb2_get_uint64(iov, 40, &hdr->session_id);
        memcpy(&hdr->signature, iov->buf + 48, 16);

        return 0;
}

static void
smb2_add_to_outqueue(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        SMB2_LIST_ADD_END(&smb2->outqueue, pdu);
        smb2_change_events(smb2, smb2->fd, smb2_which_events(smb2));
}

void
smb2_queue_pdu(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        struct smb2_pdu *p;

        /* Update all the PDU headers in this chain */
        for (p = pdu; p; p = p->next_compound) {
                smb2_encode_header(smb2, &p->out.iov[0], &p->header);
                if (smb2->sign ||
                    (p->header.command == SMB2_TREE_CONNECT && smb2->dialect == SMB2_VERSION_0311 && !smb2->seal)) {
                        if (smb2_pdu_add_signature(smb2, p) < 0) {
                                smb2_set_error(smb2, "Failure to add "
                                               "signature. %s",
                                               smb2_get_error(smb2));
                        }
                }
        }

        smb3_encrypt_pdu(smb2, pdu);

        smb2_add_to_outqueue(smb2, pdu);
}

struct smb2_pdu *
smb2_find_pdu(struct smb2_context *smb2,
              uint64_t message_id) {
        struct smb2_pdu *pdu;
        
        for (pdu = smb2->waitqueue; pdu; pdu = pdu->next) {
                if (pdu->header.message_id == message_id) {
                        break;
                }
        }
        return pdu;
}

static int
smb2_is_error_response(struct smb2_context *smb2,
                       struct smb2_pdu *pdu) {
        if ((smb2->hdr.status & SMB2_STATUS_SEVERITY_MASK) ==
            SMB2_STATUS_SEVERITY_ERROR) {
                switch (smb2->hdr.status) {
                case SMB2_STATUS_MORE_PROCESSING_REQUIRED:
                        return 0;
                default:
                        return 1;
                }
        } else if ((smb2->hdr.status & SMB2_STATUS_SEVERITY_MASK) ==
                 SMB2_STATUS_SEVERITY_WARNING) {
                switch(smb2->hdr.status) {
                case SMB2_STATUS_STOPPED_ON_SYMLINK:
                        return 1;
                default:
                        return 0;
                }
        }
        return 0;
}

int
smb2_get_fixed_size(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        if (smb2_is_error_response(smb2, pdu)) {
                return SMB2_ERROR_REPLY_SIZE & 0xfffe;
        }

        switch (pdu->header.command) {
        case SMB2_NEGOTIATE:
                return SMB2_NEGOTIATE_REPLY_SIZE;
        case SMB2_SESSION_SETUP:
                return SMB2_SESSION_SETUP_REPLY_SIZE;
        case SMB2_LOGOFF:
                return SMB2_LOGOFF_REPLY_SIZE;
        case SMB2_TREE_CONNECT:
                return SMB2_TREE_CONNECT_REPLY_SIZE;
        case SMB2_TREE_DISCONNECT:
                return SMB2_TREE_DISCONNECT_REPLY_SIZE;
        case SMB2_CREATE:
                return SMB2_CREATE_REPLY_SIZE;
        case SMB2_CLOSE:
                return SMB2_CLOSE_REPLY_SIZE;
        case SMB2_FLUSH:
                return SMB2_FLUSH_REPLY_SIZE;
        case SMB2_READ:
                return SMB2_READ_REPLY_SIZE;
        case SMB2_WRITE:
                return SMB2_WRITE_REPLY_SIZE;
        case SMB2_ECHO:
                return SMB2_ECHO_REPLY_SIZE;
        case SMB2_QUERY_DIRECTORY:
                return SMB2_QUERY_DIRECTORY_REPLY_SIZE;
        case SMB2_QUERY_INFO:
                return SMB2_QUERY_INFO_REPLY_SIZE;
        case SMB2_SET_INFO:
                return SMB2_SET_INFO_REPLY_SIZE;
        case SMB2_IOCTL:
                return SMB2_IOCTL_REPLY_SIZE;
        }
        return -1;
}

int
smb2_process_payload_fixed(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        if (smb2_is_error_response(smb2, pdu)) {
                return smb2_process_error_fixed(smb2, pdu);
        }

        switch (pdu->header.command) {
        case SMB2_NEGOTIATE:
                return smb2_process_negotiate_fixed(smb2, pdu);
        case SMB2_SESSION_SETUP:
                return smb2_process_session_setup_fixed(smb2, pdu);
        case SMB2_LOGOFF:
                return smb2_process_logoff_fixed(smb2, pdu);
        case SMB2_TREE_CONNECT:
                return smb2_process_tree_connect_fixed(smb2, pdu);
        case SMB2_TREE_DISCONNECT:
                return smb2_process_tree_disconnect_fixed(smb2, pdu);
        case SMB2_CREATE:
                return smb2_process_create_fixed(smb2, pdu);
        case SMB2_CLOSE:
                return smb2_process_close_fixed(smb2, pdu);
        case SMB2_FLUSH:
                return smb2_process_flush_fixed(smb2, pdu);
        case SMB2_READ:
                return smb2_process_read_fixed(smb2, pdu);
        case SMB2_WRITE:
                return smb2_process_write_fixed(smb2, pdu);
        case SMB2_ECHO:
                return smb2_process_echo_fixed(smb2, pdu);
        case SMB2_QUERY_DIRECTORY:
                return smb2_process_query_directory_fixed(smb2, pdu);
        case SMB2_QUERY_INFO:
                return smb2_process_query_info_fixed(smb2, pdu);
        case SMB2_SET_INFO:
                return smb2_process_set_info_fixed(smb2, pdu);
        case SMB2_IOCTL:
                return smb2_process_ioctl_fixed(smb2, pdu);
        }
        return 0;
}

int
smb2_process_payload_variable(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        if (smb2_is_error_response(smb2, pdu)) {
                return smb2_process_error_variable(smb2, pdu);
        }

        switch (pdu->header.command) {
        case SMB2_NEGOTIATE:
                return smb2_process_negotiate_variable(smb2, pdu);
        case SMB2_SESSION_SETUP:
                return smb2_process_session_setup_variable(smb2, pdu);
        case SMB2_LOGOFF:
                return 0;
        case SMB2_TREE_CONNECT:
                return 0;
        case SMB2_TREE_DISCONNECT:
                return 0;
        case SMB2_CREATE:
                return smb2_process_create_variable(smb2, pdu);
        case SMB2_CLOSE:
                return 0;
        case SMB2_FLUSH:
                return 0;
        case SMB2_READ:
                return 0;
        case SMB2_WRITE:
                return 0;
        case SMB2_ECHO:
                return 0;
        case SMB2_QUERY_DIRECTORY:
                return smb2_process_query_directory_variable(smb2, pdu);
        case SMB2_QUERY_INFO:
                return smb2_process_query_info_variable(smb2, pdu); 
        case SMB2_SET_INFO:
                return 0;
        case SMB2_IOCTL:
                return smb2_process_ioctl_variable(smb2, pdu); 
        }
        return 0;
}

void smb2_timeout_pdus(struct smb2_context *smb2)
{
        struct smb2_pdu *pdu, *next;
        time_t t = time(NULL);

        pdu = smb2->outqueue;
        while (pdu) {
                next = pdu->next;
                if (pdu->timeout && pdu->timeout < t) {
                        SMB2_LIST_REMOVE(&smb2->outqueue, pdu);
                        pdu->cb(smb2, SMB2_STATUS_IO_TIMEOUT, NULL,
                                pdu->cb_data);
                        smb2_free_pdu(smb2, pdu);
                }
                pdu = next;
        }

        pdu = smb2->waitqueue;
        while (pdu) {
                next = pdu->next;
                if (pdu->timeout && pdu->timeout < t) {
                        SMB2_LIST_REMOVE(&smb2->waitqueue, pdu);
                        pdu->cb(smb2, SMB2_STATUS_IO_TIMEOUT, NULL,
                                pdu->cb_data);
                        smb2_free_pdu(smb2, pdu);
                }
                pdu = next;
        }
}

