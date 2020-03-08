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

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-dcerpc-srvsvc.h"
#include "libsmb2-dcerpc.h"
#include "libsmb2-raw.h"
#include "libsmb2-private.h"

/*
 * SRVSVC BEGIN:  DEFINITIONS FROM SRVSVC.IDL
 */
/*
	typedef struct {
		[string,charset(UTF16)] uint16 *name;
		srvsvc_ShareType type;
		[string,charset(UTF16)] uint16 *comment;
	} srvsvc_NetShareInfo1;

	typedef struct {
		uint32 count;
		[size_is(count)] srvsvc_NetShareInfo1 *array;
	} srvsvc_NetShareCtr1;

	typedef union {
		[case(0)] srvsvc_NetShareCtr0 *ctr0;
		[case(1)] srvsvc_NetShareCtr1 *ctr1;
		[case(2)] srvsvc_NetShareCtr2 *ctr2;
		[case(501)] srvsvc_NetShareCtr501 *ctr501;
		[case(502)] srvsvc_NetShareCtr502 *ctr502;
		[case(1004)] srvsvc_NetShareCtr1004 *ctr1004;
		[case(1005)] srvsvc_NetShareCtr1005 *ctr1005;
		[case(1006)] srvsvc_NetShareCtr1006 *ctr1006;
		[case(1007)] srvsvc_NetShareCtr1007 *ctr1007;
		[case(1501)] srvsvc_NetShareCtr1501 *ctr1501;
		[default] ;
	} srvsvc_NetShareCtr;
*/
/*****************
 * Function: 0x0f
	WERROR srvsvc_NetShareEnumAll (
		[in]   [string,charset(UTF16)] uint16 *server_unc,
		[in,out,ref]   uint32 *level,
		[in,out,switch_is(level),ref] srvsvc_NetShareCtr *ctr,
		[in]   uint32 max_buffer,
		[out,ref]  uint32 *totalentries,
		[in,out]   uint32 *resume_handle
		);
******************/
static int
srvsvc_NetShareCtr1_encoder(struct dcerpc_context *ctx,
                            struct dcerpc_pdu *pdu,
                            struct smb2_iovec *iov, int offset,
                            void *ptr)
{
        /* just encode a fake array with 0 count and no pointer */
        offset = dcerpc_encode_3264(ctx, pdu, iov, offset, 0);
        offset = dcerpc_encode_3264(ctx, pdu, iov, offset, 0);

        offset = dcerpc_process_deferred_pointers(ctx, pdu, iov, offset);
        return offset;
}

static int
srvsvc_NetShareCtr_encoder(struct dcerpc_context *dce, struct dcerpc_pdu *pdu,
                           struct smb2_iovec *iov, int offset,
                           void *ptr)
{
        /* just encode a fake union with case 1 */
        offset = dcerpc_encode_3264(dce, pdu, iov, offset, 1);
        offset = dcerpc_encode_ptr(dce, pdu, iov, offset, "dummy pointer",
                                   PTR_UNIQUE, srvsvc_NetShareCtr1_encoder);

        offset = dcerpc_process_deferred_pointers(dce, pdu, iov, offset);
        return offset;
}

static int
srvsvc_NetShareCtr1_array_decoder(struct dcerpc_context *ctx,
                                  struct dcerpc_pdu *pdu,
                                  struct smb2_iovec *iov, int offset,
                                  void *ptr)
{
        struct srvsvc_netshareinfo1 *nsi1 = ptr;
        uint64_t p;

        offset = dcerpc_decode_3264(ctx, pdu, iov, offset, &p);

        while (p--) {
                offset = dcerpc_decode_ptr(ctx, pdu, iov, offset, &nsi1->name,
                                           PTR_UNIQUE,
                                           dcerpc_decode_ucs2z);
                offset = dcerpc_decode_32(ctx, pdu, iov, offset, &nsi1->type);
                offset = dcerpc_decode_ptr(ctx, pdu, iov, offset, &nsi1->comment,
                                           PTR_UNIQUE,
                                           dcerpc_decode_ucs2z);
                nsi1++;
        }

        offset = dcerpc_process_deferred_pointers(ctx, pdu, iov, offset);
        return offset;
}

static int
srvsvc_NetShareCtr1_decoder(struct dcerpc_context *dce, struct dcerpc_pdu *pdu,
                            struct smb2_iovec *iov, int offset,
                            void *ptr)
{
        struct srvsvc_netsharectr1 *ctr1 = ptr;

        offset = dcerpc_decode_32(dce, pdu, iov, offset, &ctr1->count);

        ctr1->array = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                dcerpc_get_pdu_payload(pdu),
                ctr1->count * sizeof(struct srvsvc_netshareinfo1));
        if (ctr1->array == NULL) {
                return -1;
        }

        offset = dcerpc_decode_ptr(dce, pdu, iov, offset, ctr1->array,
                                   PTR_UNIQUE,
                                   srvsvc_NetShareCtr1_array_decoder);

        offset = dcerpc_process_deferred_pointers(dce, pdu, iov, offset);
        return offset;
}

static int
srvsvc_NetShareCtr_decoder(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                           struct smb2_iovec *iov, int offset,
                           void *ptr)
{
        struct srvsvc_netsharectr *ctr = ptr;
        uint64_t p;

        offset = dcerpc_decode_3264(ctx, pdu, iov, offset, &p);
        ctr->level = (uint32_t)p;

        switch (ctr->level) {
        case 1:
                offset = dcerpc_decode_ptr(ctx, pdu, iov, offset, &ctr->ctr1,
                                           PTR_UNIQUE,
                                           srvsvc_NetShareCtr1_decoder);
                break;
        };

        offset = dcerpc_process_deferred_pointers(ctx, pdu, iov, offset);
        return offset;
}

int
srvsvc_netshareenumall_encoder(struct dcerpc_context *ctx,
                               struct dcerpc_pdu *pdu,
                               struct smb2_iovec *iov, int offset,
                               void *ptr)
{
        struct srvsvc_netshareenumall_req *req = ptr;
        int len;
        char *server;
        struct ucs2 *ucs2_unc;

        len = strlen(req->server) + 3;
        server = malloc(len);
        if (server == NULL) {
                return -1;
        }

        snprintf(server, len, "\\\\%s", req->server);
        ucs2_unc = utf8_to_ucs2(server);
        free(server);
        if (ucs2_unc == NULL) {
                return -1;
        }

        offset = dcerpc_encode_ptr(ctx, pdu, iov, offset, ucs2_unc,
                                   PTR_UNIQUE, dcerpc_encode_ucs2z);
        offset = dcerpc_encode_ptr(ctx, pdu, iov, offset, &req->level,
                                   PTR_REF, dcerpc_encode_32);
        offset = dcerpc_encode_ptr(ctx, pdu, iov, offset, "dummy pointer",
                                   PTR_REF, srvsvc_NetShareCtr_encoder);
        offset = dcerpc_encode_ptr(ctx, pdu, iov, offset, &req->max_buffer,
                                   PTR_REF, dcerpc_encode_32);
        offset = dcerpc_encode_ptr(ctx, pdu, iov, offset, &req->resume_handle,
                                   PTR_UNIQUE, dcerpc_encode_32);

        offset = dcerpc_process_deferred_pointers(ctx, pdu, iov, offset);
        free(ucs2_unc);

        return offset;
}

int
srvsvc_netshareenumall_decoder(struct dcerpc_context *dce,
                               struct dcerpc_pdu *pdu,
                               struct smb2_iovec *iov, int offset,
                               void *ptr)
{
        struct srvsvc_netshareenumall_rep *rep = ptr;
        struct srvsvc_netsharectr *ctr;

        offset = dcerpc_decode_ptr(dce, pdu, iov, offset, &rep->level,
                                PTR_REF, dcerpc_decode_32);

        ctr = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                              dcerpc_get_pdu_payload(pdu),
                              sizeof(struct srvsvc_netsharectr));
        if (ctr == NULL) {
                return -1;
        }

        rep->ctr = ctr;
        offset = dcerpc_decode_ptr(dce, pdu, iov, offset, rep->ctr,
                                   PTR_REF, srvsvc_NetShareCtr_decoder);

        offset = dcerpc_decode_ptr(dce, pdu, iov, offset, &rep->total_entries,
                                   PTR_REF, dcerpc_decode_32);
        
        offset = dcerpc_decode_ptr(dce, pdu, iov, offset, &rep->resume_handle,
                                   PTR_UNIQUE, dcerpc_decode_32);

        offset = dcerpc_decode_32(dce, pdu, iov, offset, &rep->status);
        
        return offset;
}
