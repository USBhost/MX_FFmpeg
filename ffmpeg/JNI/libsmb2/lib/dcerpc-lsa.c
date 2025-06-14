/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2020 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

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
#include "libsmb2-dcerpc-lsa.h"
#include "libsmb2-raw.h"
#include "libsmb2-private.h"

#define LSA_UUID    0x12345778, 0x1234, 0xabcd, 0xef000123456789ab

p_syntax_id_t lsa_interface = {
        {LSA_UUID}, 0, 0
};

unsigned char NT_SID_AUTHORITY[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x05 };

/*
 * typedef struct _RPC_SID {
 *      unsigned char Revision;
 *      unsigned char SubAuthorityCount;
 *      byte IdentifierAuthority[6];
 *      [size_is(SubAuthorityCount)] uint32_t SubAuthority[];
 * } RPC_SID, *PRPC_SID, *PSID;
 */
int
lsa_RPC_SID_coder(struct dcerpc_context *dce,
                  struct dcerpc_pdu *pdu,
                  struct smb2_iovec *iov, int offset,
                  void *ptr)
{
        RPC_SID *sid = ptr;
        uint64_t count;
        int i;

        count = sid->SubAuthorityCount;
        offset = dcerpc_uint3264_coder(dce, pdu, iov, offset, &count);

        offset = dcerpc_uint8_coder(dce, pdu, iov, offset, &sid->Revision);
        offset = dcerpc_uint8_coder(dce, pdu, iov, offset, &sid->SubAuthorityCount);
        for (i = 0; i < 6; i++) {
                offset = dcerpc_uint8_coder(dce, pdu, iov, offset, &sid->IdentifierAuthority[i]);
        }

        if (dcerpc_pdu_direction(pdu) == DCERPC_DECODE) {
                sid->SubAuthority = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                                                    dcerpc_get_pdu_payload(pdu),
                                                    count * sizeof(uint32_t));
                if (sid->SubAuthority == NULL) {
                        return -1;
                }
        }

        for (i = 0; i < count; i++) {
                offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &sid->SubAuthority[i]);
        }

        return offset;
}

static int
lsa_PRPC_SID_array_coder(struct dcerpc_context *dce,
                         struct dcerpc_pdu *pdu,
                         struct smb2_iovec *iov, int offset,
                         void *ptr)
{
        PLSAPR_SID_ENUM_BUFFER seb = ptr;
        uint64_t val;
        int i;

        val = seb->Entries;
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &val);

        if (dcerpc_pdu_direction(pdu) == DCERPC_DECODE) {
                seb->SidInfo = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                                               dcerpc_get_pdu_payload(pdu),
                                               val * sizeof(PRPC_SID));
                if (seb->SidInfo == NULL) {
                        return -1;
                }
                for (i = 0; i < val; i++) {
                        seb->SidInfo[i] = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                                               dcerpc_get_pdu_payload(pdu),
                                               sizeof(RPC_SID));
                        if (seb->SidInfo[i] == NULL) {
                                return -1;
                        }
                }
        }

        for (i = 0; i < val; i++) {
                offset = dcerpc_ptr_coder(dce, pdu, iov, offset,
                                          seb->SidInfo[i],
                                          PTR_UNIQUE, lsa_RPC_SID_coder);
        }

        return offset;
}

/*
 * typedef struct _LSAPR_SID_ENUM_BUFFER {
 *      [range(0,20480)] uint32_t Entries;
 *      [size_is(Entries)] PRPC_SID SidInfo;
 * } LSAPR_SID_ENUM_BUFFER, *PLSAPR_SID_ENUM_BUFFER;
 */
static int
lsa_SID_ENUM_BUFFER_coder(struct dcerpc_context *dce,
                          struct dcerpc_pdu *pdu,
                          struct smb2_iovec *iov, int offset,
                          void *ptr)
{
        PLSAPR_SID_ENUM_BUFFER seb = ptr;
        uint32_t val;

        val = seb->Entries;
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &val);
        seb->Entries = val;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, seb,
                                  PTR_UNIQUE, lsa_PRPC_SID_array_coder);

        return offset;
}

/*
 * typedef struct _RPC_UNICODE_STRING {
 *       uint16_t Length;
 *       uint16_t MaximumLength;
 *       char *Buffer;
 * } RPC_UNICODE_STRING, *PRPC_UNICODE_STRING;
 */
/* ptr is char ** */
int
lsa_RPC_UNICODE_STRING_coder(struct dcerpc_context *dce,
                             struct dcerpc_pdu *pdu,
                             struct smb2_iovec *iov, int offset,
                             void *ptr)
{
        uint16_t len, maxlen;

        offset = dcerpc_align_3264(dce, offset);

        if (dcerpc_pdu_direction(pdu) == DCERPC_ENCODE) {
                len = strlen(*(char **)ptr) * 2;
                maxlen = (len & 0x02) ? len + 2 : len;
        }
        offset = dcerpc_uint16_coder(dce, pdu, iov, offset, &len);
        offset = dcerpc_uint16_coder(dce, pdu, iov, offset, &maxlen);
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, ptr,
                                   PTR_UNIQUE, dcerpc_utf16_coder);

        return offset;
}

/*
 * typedef struct _LSAPR_TRANSLATED_NAME_EX {
 *      SID_NAME_USE Use;
 *      RPC_UNICODE_STRING Name;
 *      uint32_t DomainIndex;
 *      uint32_t Flags;
 * } LSAPR_TRANSLATED_NAME_EX, *PLSAPR_TRANSLATED_NAME_EX;
 */
static int
lsa_TRANSLATED_NAME_EX_coder(struct dcerpc_context *dce,
                             struct dcerpc_pdu *pdu,
                             struct smb2_iovec *iov, int offset,
                             void *ptr)
{
        LSAPR_TRANSLATED_NAME_EX *tn = ptr;

        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &tn->Use);
        offset = lsa_RPC_UNICODE_STRING_coder(dce, pdu, iov, offset,
                                              &tn->Name);
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &tn->DomainIndex);
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &tn->Flags);

        return offset;
}

/*
 * typedef struct _LSAPR_TRANSLATED_NAMES_EX {
 *      [range(0,20480)] unsigned long Entries;
 *      [size_is(Entries)] PLSAPR_TRANSLATED_NAME_EX Names;
 * } LSAPR_TRANSLATED_NAMES_EX, *PLSAPR_TRANSLATED_NAMES_EX;
 */
static int
TN_array_coder(struct dcerpc_context *dce,
               struct dcerpc_pdu *pdu,
               struct smb2_iovec *iov, int offset,
               void *ptr)
{
        LSAPR_TRANSLATED_NAMES_EX *tn = ptr;
        uint64_t count;
        int i;

        count = tn->Entries;
        offset = dcerpc_uint3264_coder(dce, pdu, iov, offset, &count);
        if (dcerpc_pdu_direction(pdu) == DCERPC_DECODE) {
                tn->Names = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                                            dcerpc_get_pdu_payload(pdu),
                                            count * sizeof(LSAPR_TRANSLATED_NAME_EX));
                if (tn->Names == NULL) {
                        return -1;
                }
        }
        for (i = 0; i < count; i++) {
                offset = lsa_TRANSLATED_NAME_EX_coder(dce, pdu, iov, offset,
                                                      &tn->Names[i]);
        }

        return offset;
}

static int
lsa_TRANSLATED_NAMES_EX_coder(struct dcerpc_context *dce,
                              struct dcerpc_pdu *pdu,
                              struct smb2_iovec *iov, int offset,
                              void *ptr)
{
        LSAPR_TRANSLATED_NAMES_EX *tn = ptr;

        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &tn->Entries);
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, ptr,
                                   PTR_UNIQUE, TN_array_coder);

        return offset;
}

/*
 * typedef struct _LSAPR_OBJECT_ATTRIBUTES {
 *      unsigned long Length = 0;
 *      unsigned char *RootDirectory = NULL;
 *      PSTRING ObjectName = NULL;
 *      unsigned long Attributes = 0;
 *      PLSAPR_SECURITY_DESCRIPTOR SecurityDescriptor = NULL;
 *      PSECURITY_QUALITY_OF_SERVICE SecurityQualityOfService = NULL;
 * } LSAPR_OBJECT_ATTRIBUTES, *PLSAPR_OBJECT_ATTRIBUTES;
 */

static int
lsa_ObjectAttributes_coder(struct dcerpc_context *dce,
                           struct dcerpc_pdu *pdu,
                           struct smb2_iovec *iov, int offset,
                           void *ptr)
{
        uint32_t len;
        uint64_t val;

        /* just encode a fake empty object for OpenPolicy2 */
        len = 24;
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &len);
        val = 0;
        offset = dcerpc_uint3264_coder(dce, pdu, iov, offset, &val);
        offset = dcerpc_uint3264_coder(dce, pdu, iov, offset, &val);
        len = 0;
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &len);
        offset = dcerpc_uint3264_coder(dce, pdu, iov, offset, &val);
        offset = dcerpc_uint3264_coder(dce, pdu, iov, offset, &val);

        return offset;
}

/**********************
 * Function: 0x00
 *	NTSTATUS lsa_Close (
 *		[in,out]     ndr_context_handle handle
 *		);
 **********************/
int
lsa_Close_req_coder(struct dcerpc_context *dce,
                    struct dcerpc_pdu *pdu,
                    struct smb2_iovec *iov, int offset,
                    void *ptr)
{
        struct lsa_close_req *req = ptr;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &req->PolicyHandle,
                                   PTR_REF, dcerpc_context_handle_coder);

        return offset;
}

int
lsa_Close_rep_coder(struct dcerpc_context *dce,
                    struct dcerpc_pdu *pdu,
                    struct smb2_iovec *iov, int offset,
                    void *ptr)
{
        struct lsa_close_rep *rep = ptr;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &rep->PolicyHandle,
                                   PTR_REF, dcerpc_context_handle_coder);

        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &rep->status);

        return offset;
}

/**********************
 * Function:     0x2c
 *      NTSTATUS LsarOpenPolicy2(
 *              [in,unique,string] wchar_t* SystemName,
 *              [in] PLSAPR_OBJECT_ATTRIBUTES ObjectAttributes,
 *              [in] uint32_t DesiredAccess,
 *		[out] ndr_context_handle PolicyHandle
 *              );
 **********************/
int
lsa_OpenPolicy2_req_coder(struct dcerpc_context *dce,
                          struct dcerpc_pdu *pdu,
                          struct smb2_iovec *iov, int offset,
                          void *ptr)
{
        struct lsa_openpolicy2_req *req = ptr;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &req->SystemName,
                                   PTR_UNIQUE, dcerpc_utf16z_coder);
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &req->ObjectAttributes,
                                   PTR_REF, lsa_ObjectAttributes_coder);
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &req->DesiredAccess);
        return offset;
}

int
lsa_OpenPolicy2_rep_coder(struct dcerpc_context *dce,
                          struct dcerpc_pdu *pdu,
                          struct smb2_iovec *iov, int offset,
                          void *ptr)
{
        struct lsa_openpolicy2_rep *rep = ptr;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &rep->PolicyHandle,
                                   PTR_REF, dcerpc_context_handle_coder);

        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &rep->status);

        return offset;
}

/*
 * typedef struct _LSAPR_TRUST_INFORMATION {
 *       RPC_UNICODE_STRING Name;
 *       PRPC_SID Sid;
 * } LSAPR_TRUST_INFORMATION, *PLSAPR_TRUST_INFORMATION;
*/
static int
lsa_TRUST_INFORMATION_coder(struct dcerpc_context *dce,
                            struct dcerpc_pdu *pdu,
                            struct smb2_iovec *iov, int offset,
                            void *ptr)
{
        LSAPR_TRUST_INFORMATION *ti = ptr;

        offset = lsa_RPC_UNICODE_STRING_coder(dce, pdu, iov, offset,
                                              &ti->Name);
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &ti->Sid,
                                   PTR_UNIQUE, lsa_RPC_SID_coder);

        return offset;
}

static int
RDL_DOMAINS_array_coder(struct dcerpc_context *dce,
                        struct dcerpc_pdu *pdu,
                        struct smb2_iovec *iov, int offset,
                        void *ptr)
{
        LSAPR_REFERENCED_DOMAIN_LIST *rdl = ptr;
        uint64_t entries;
        int i;

        entries = rdl->Entries;
        offset = dcerpc_uint3264_coder(dce, pdu, iov, offset, &entries);
        rdl->Entries = entries;

        if (dcerpc_pdu_direction(pdu) == DCERPC_DECODE) {
                rdl->Domains = smb2_alloc_data(dcerpc_get_smb2_context(dce),
                                               dcerpc_get_pdu_payload(pdu),
                                               rdl->Entries * sizeof(LSAPR_TRUST_INFORMATION));
                if (rdl->Domains == NULL) {
                        return -1;
                }
        }

        for (i = 0; i < entries; i++) {
                offset = lsa_TRUST_INFORMATION_coder(dce, pdu, iov, offset,
                                                     &rdl->Domains[i]);
        }

        return offset;
}


/*
 * typedef struct _LSAPR_REFERENCED_DOMAIN_LIST {
 *      uint32_t Entries;
 *      LSAPR_TRUST_INFORMATION *Domains;
 *      uint32_t MaxEntries;  must be ignored
 * } LSAPR_REFERENCED_DOMAIN_LIST, *PLSAPR_REFERENCED_DOMAIN_LIST;
 */
static int
lsa_REFERENCED_DOMAIN_LIST_coder(struct dcerpc_context *dce,
                                 struct dcerpc_pdu *pdu,
                                 struct smb2_iovec *iov, int offset,
                                 void *ptr)
{
        LSAPR_REFERENCED_DOMAIN_LIST *rdl = ptr;

        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &rdl->Entries);

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, ptr,
                                   PTR_UNIQUE, RDL_DOMAINS_array_coder);
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &rdl->MaxEntries);

        return offset;
}

/**********************
 * Function:     0x39
 * NTSTATUS LsarLookupSids2(
 *       [in] ndr_context_handle PolicyHandle,
 *       [in] PLSAPR_SID_ENUM_BUFFER SidEnumBuffer,
 *       [out] PLSAPR_REFERENCED_DOMAIN_LIST* ReferencedDomains,
 *       [in, out] PLSAPR_TRANSLATED_NAMES_EX TranslatedNames,
 *       [in] LSAP_LOOKUP_LEVEL LookupLevel,
 *       [in, out] unsigned long* MappedCount,
 *       [in] unsigned long LookupOptions, (SHOULD BE 0)
 *       [in] unsigned long ClientRevision
 *       );
 *******************/
int
lsa_LookupSids2_req_coder(struct dcerpc_context *dce,
                          struct dcerpc_pdu *pdu,
                          struct smb2_iovec *iov, int offset,
                          void *ptr)
{
        struct lsa_lookupsids2_req *req = ptr;
        uint32_t val;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &req->PolicyHandle,
                                   PTR_REF, dcerpc_context_handle_coder);
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &req->SidEnumBuffer,
                                   PTR_REF, lsa_SID_ENUM_BUFFER_coder);
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &req->TranslatedNames,
                                   PTR_REF, lsa_TRANSLATED_NAMES_EX_coder);
        val = req->LookupLevel;
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &val);
        req->LookupLevel = val;

        val = 0;
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &val);
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &val);

        val = 2;
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &val);

        return offset;
}

int
lsa_LookupSids2_rep_coder(struct dcerpc_context *dce,
                          struct dcerpc_pdu *pdu,
                          struct smb2_iovec *iov, int offset,
                          void *ptr)
{
        struct lsa_lookupsids2_rep *rep = ptr;

        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &rep->ReferencedDomains,
                                   PTR_UNIQUE, lsa_REFERENCED_DOMAIN_LIST_coder);
        offset = dcerpc_ptr_coder(dce, pdu, iov, offset, &rep->TranslatedNames,
                                   PTR_REF, lsa_TRANSLATED_NAMES_EX_coder);
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &rep->MappedCount);
        offset = dcerpc_uint32_coder(dce, pdu, iov, offset, &rep->status);

        return offset;
}
