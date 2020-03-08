/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2018 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

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

#ifndef _LIBSMB2_DCERPC_H_
#define _LIBSMB2_DCERPC_H_

#ifdef __cplusplus
extern "C" {
#endif

struct dcerpc_context;
struct dcerpc_pdu;

/* Encoder/Decoder for a DCERPC object */
typedef int (*dcerpc_coder)(struct dcerpc_context *dce, struct dcerpc_pdu *pdu,
                            struct smb2_iovec *iov, int offset,
                            void *ptr);

enum ptr_type {
        PTR_REF    = 0,
        PTR_UNIQUE = 1
};

struct dcerpc_uuid {
        uint32_t v1;
        uint16_t v2;
        uint16_t v3;
        uint64_t v4;
};

typedef struct p_syntax_id {
        struct dcerpc_uuid uuid;
        uint16_t vers;
        uint16_t vers_minor;
} p_syntax_id_t;

struct dcerpc_transfer_syntax {
        struct dcerpc_uuid uuid;
        uint16_t vers;
};

extern p_syntax_id_t srvsvc_interface;
        
typedef void (*dcerpc_cb)(struct dcerpc_context *dce, int status,
                          void *command_data, void *cb_data);

struct dcerpc_context *dcerpc_create_context(struct smb2_context *smb2,
                                             const char *path,
                                             p_syntax_id_t *syntax);
void dcerpc_destroy_context(struct dcerpc_context *dce);

struct smb2_context *dcerpc_get_smb2_context(struct dcerpc_context *dce);
void *dcerpc_get_pdu_payload(struct dcerpc_pdu *pdu);

int dcerpc_open_async(struct dcerpc_context *dce, dcerpc_cb cb, void *cb_data);
int dcerpc_bind_async(struct dcerpc_context *dce, dcerpc_cb cb, void *cb_data);
int dcerpc_call_async(struct dcerpc_context *dce, int opnum,
                      dcerpc_coder encoder, void *ptr,
                      dcerpc_coder decoder, int decode_size,
                      dcerpc_cb cb, void *cb_data);

int dcerpc_decode_ptr(struct dcerpc_context *dce, struct dcerpc_pdu *pdu,
                      struct smb2_iovec *iov, int offset, void *ptr,
                      enum ptr_type type, dcerpc_coder coder);
int dcerpc_decode_32(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                     struct smb2_iovec *iov, int offset, void *ptr);
int dcerpc_decode_3264(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                       struct smb2_iovec *iov, int offset, void *ptr);
int dcerpc_decode_ucs2z(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                        struct smb2_iovec *iov, int offset, void *ptr);
int dcerpc_encode_ptr(struct dcerpc_context *dce, struct dcerpc_pdu *pdu,
                      struct smb2_iovec *iov, int offset, void *ptr,
                      enum ptr_type type, dcerpc_coder coder);
int dcerpc_encode_ucs2z(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                        struct smb2_iovec *iov, int offset, void *ptr);
int dcerpc_encode_32(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                     struct smb2_iovec *iov, int offset, void *ptr);
int dcerpc_encode_3264(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                       struct smb2_iovec *iov, int offset, uint64_t val);
int dcerpc_process_deferred_pointers(struct dcerpc_context *ctx,
                                     struct dcerpc_pdu *pdu,
                                     struct smb2_iovec *iov,
                                     int offset);
void dcerpc_add_deferred_pointer(struct dcerpc_context *ctx,
                                 struct dcerpc_pdu *pdu,
                                 dcerpc_coder coder, void *ptr);




#endif /* !_LIBSMB2_DCERPC_H_ */
