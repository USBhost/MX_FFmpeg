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

#include "compat.h"

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-dcerpc.h"
#include "libsmb2-dcerpc-srvsvc.h"
#include "libsmb2-raw.h"
#include "libsmb2-private.h"

#define SRVSVC_UUID    0x4b324fc8, 0x1670, 0x01d3, 0x12785a47bf6ee188

p_syntax_id_t srvsvc_interface = {
        {SRVSVC_UUID}, 3, 0
};

/*
 * SRVSVC BEGIN:  DEFINITIONS FROM SRVSVC.IDL
 */
/*
	typedef struct {
		[string,charset(UTF16)] uint16 *name;
		srvsvc_ShareType type;
		[string,charset(UTF16)] uint16 *comment;
	} srvsvc_NetShareInfo1;
*/
static int
srvsvc_NetShareInfo1_coder(struct dcerpc_context *ctx,
                           struct dcerpc_pdu *pdu,
                           struct smb2_iovec *iov, int offset,
                           void *ptr)
{
        struct srvsvc_netshareinfo1 *nsi1 = ptr;

        offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &nsi1->name,
                                   PTR_UNIQUE, dcerpc_utf16z_coder);
        offset = dcerpc_uint32_coder(ctx, pdu, iov, offset, &nsi1->type);
        offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &nsi1->comment,
                                   PTR_UNIQUE, dcerpc_utf16z_coder);
        return offset;
}

static int
srvsvc_NetShareInfo1_array_coder(struct dcerpc_context *ctx,
                                 struct dcerpc_pdu *pdu,
                                 struct smb2_iovec *iov, int offset,
                                 void *ptr)
{
        struct srvsvc_netsharectr1 *ctr1 = ptr;
        struct srvsvc_netshareinfo1 *nsi1 = ctr1->array;
        uint64_t p;

        p = ctr1->count;
        offset = dcerpc_uint3264_coder(ctx, pdu, iov, offset, &p);
        if (p != ctr1->count) {
                return -1;
        }

        while (p--) {
                offset = srvsvc_NetShareInfo1_coder(ctx, pdu, iov, offset,
                                                    nsi1);
                nsi1++;
        }

        return offset;
}

/*
	typedef struct {
		uint32 count;
		[size_is(count)] srvsvc_NetShareInfo1 *array;
	} srvsvc_NetShareCtr1;
*/
static int
srvsvc_NetShareCtr1_coder(struct dcerpc_context *dce, struct dcerpc_pdu *pdu,
                          struct smb2_iovec *iov, int offset,
                          void *ptr)
{
        struct srvsvc_netsharectr1 *ctr1 = ptr;

        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &ctr1->count);

        if (dcerpc_pdu_direction(pdu) == DCERPC_DECODE) {
                ctr1->array = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                                              dcerpc_get_pdu_payload(pdu),
                                              ctr1->count * sizeof(struct srvsvc_netshareinfo1));
                if (ctr1->array == NULL) {
                        return -1;
                }
        }

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset,
                                  ctr1->count ? ctr1 : NULL,
                                  PTR_UNIQUE,
                                  srvsvc_NetShareInfo1_array_coder);

        return offset;
}

/*
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
static int
srvsvc_NetShareCtr_coder(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                         struct smb2_iovec *iov, int offset,
                         void *ptr)
{
        struct srvsvc_netsharectr *ctr = ptr;
        uint64_t p;

        p = ctr->level;
        offset = dcerpc_uint3264_coder(ctx, pdu, iov, offset, &p);
        ctr->level = (uint32_t)p;

        switch (ctr->level) {
        case 1:
                offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &ctr->ctr1,
                                           PTR_UNIQUE,
                                           srvsvc_NetShareCtr1_coder);
                break;
        };

        return offset;
}

/*****************
 * Function: 0x0f
 *	WERROR srvsvc_NetShareEnumAll (
 *		[in]   [string,charset(UTF16)] uint16 *server_unc,
 *		[in,out,ref]   uint32 *level,
 *		[in,out,switch_is(level),ref] srvsvc_NetShareCtr *ctr,
 *		[in]   uint32 max_buffer,
 *		[out,ref]  uint32 *totalentries,
 *		[in,out]   uint32 *resume_handle
 *		);
 ******************/
int
srvsvc_NetrShareEnum_req_coder(struct dcerpc_context *ctx,
                               struct dcerpc_pdu *pdu,
                               struct smb2_iovec *iov, int offset,
                               void *ptr)
{
        struct srvsvc_netshareenumall_req *req = ptr;
        struct srvsvc_netsharectr ctr;

        offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &req->server,
                                   PTR_UNIQUE, dcerpc_utf16z_coder);
        offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &req->level,
                                   PTR_REF, dcerpc_uint32_coder);
        ctr.level = 1;
        ctr.ctr1.count = 0;
        ctr.ctr1.array = NULL;
        offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &ctr,
                                   PTR_REF, srvsvc_NetShareCtr_coder);
        offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &req->max_buffer,
                                   PTR_REF, dcerpc_uint32_coder);
        offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &req->resume_handle,
                                   PTR_UNIQUE, dcerpc_uint32_coder);

        return offset;
}

int
srvsvc_NetrShareEnum_rep_coder(struct dcerpc_context *dce,
                               struct dcerpc_pdu *pdu,
                               struct smb2_iovec *iov, int offset,
                               void *ptr)
{
        struct srvsvc_netshareenumall_rep *rep = ptr;
        struct srvsvc_netsharectr *ctr;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &rep->level,
                                PTR_REF, dcerpc_uint32_coder);

        if (dcerpc_pdu_direction(pdu) == DCERPC_DECODE) {
                ctr = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                                      dcerpc_get_pdu_payload(pdu),
                                      sizeof(struct srvsvc_netsharectr));
                if (ctr == NULL) {
                        return -1;
                }
                rep->ctr = ctr;
        }
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, rep->ctr,
                                   PTR_REF, srvsvc_NetShareCtr_coder);

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &rep->total_entries,
                                   PTR_REF, dcerpc_uint32_coder);
        
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &rep->resume_handle,
                                   PTR_UNIQUE, dcerpc_uint32_coder);

        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &rep->status);

        return offset;
}

/*
 *	typedef union {
 *		[case(0)] srvsvc_NetShareInfo0 *info0;
 *		[case(1)] srvsvc_NetShareInfo1 *info1;
 *		[case(2)] srvsvc_NetShareInfo2 *info2;
 *		[case(501)] srvsvc_NetShareInfo501 *info501;
 *		[case(502)] srvsvc_NetShareInfo502 *info502;
 *		[case(1004)] srvsvc_NetShareInfo1004 *info1004;
 *		[case(1005)] srvsvc_NetShareInfo1005 *info1005;
 *		[case(1006)] srvsvc_NetShareInfo1006 *info1006;
 *		[case(1007)] srvsvc_NetShareInfo1007 *info1007;
 *		[case(1501)] sec_desc_buf *info1501;
 *		[default] ;
 *	} srvsvc_NetShareInfo;
 */
static int
srvsvc_NetShareInfo_coder(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                          struct smb2_iovec *iov, int offset,
                          void *ptr)
{
        struct srvsvc_netshareinfo *info = ptr;
        uint64_t p;

        p = info->level;
        offset = dcerpc_uint3264_coder(ctx, pdu, iov, offset, &p);
        info->level = (uint32_t)p;

        switch (info->level) {
        case 1:
                offset = dcerpc_ptr_coder(ctx, pdu, iov, offset, &info->info1,
                                           PTR_UNIQUE,
                                           srvsvc_NetShareInfo1_coder);
                break;
        };

        return offset;
}

/******************
 * Function: 0x10
 *	WERROR srvsvc_NetShareGetInfo(
 *		[in]   [string,charset(UTF16)] uint16 *server_unc,
 *		[in]   [string,charset(UTF16)] uint16 share_name[],
 *		[in]   uint32 level,
 *		[out,switch_is(level),ref] srvsvc_NetShareInfo *info
 *		);
 ******************/
int
srvsvc_NetrShareGetInfo_req_coder(struct dcerpc_context *dce,
                                  struct dcerpc_pdu *pdu,
                                  struct smb2_iovec *iov, int offset,
                                  void *ptr)
{
        struct srvsvc_netrsharegetinfo_req *req = ptr;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &req->ServerName,
                                  PTR_UNIQUE, dcerpc_utf16z_coder);
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset,
                                  discard_const(&req->NetName),
                                  PTR_REF, dcerpc_utf16z_coder);
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &req->Level);

        return offset;
}

int
srvsvc_NetrShareGetInfo_rep_coder(struct dcerpc_context *dce,
                                  struct dcerpc_pdu *pdu,
                                  struct smb2_iovec *iov, int offset,
                                  void *ptr)
{
        struct srvsvc_netrsharegetinfo_rep *rep = ptr;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &rep->info,
                                   PTR_REF, srvsvc_NetShareInfo_coder);

        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &rep->status);

        return offset;
}
