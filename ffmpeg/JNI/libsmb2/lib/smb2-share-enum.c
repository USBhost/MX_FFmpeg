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
#include "libsmb2-dcerpc.h"
#include "libsmb2-dcerpc-srvsvc.h"
#include "libsmb2-raw.h"
#include "libsmb2-private.h"

struct smb2nse {
        struct srvsvc_netshareenumall_req ea_req;

        smb2_command_cb cb;
        void *cb_data;
};

static void
nse_free(struct smb2nse *nse)
{
        free(nse);
}

static void
share_enum_ioctl_cb(struct dcerpc_context *dce, int status,
                    void *command_data, void *cb_data)
{
        struct smb2nse *nse = cb_data;
        struct srvsvc_netshareenumall_rep *rep = command_data;
        struct smb2_context *smb2 = dcerpc_get_smb2_context(dce);

        if (status != SMB2_STATUS_SUCCESS) {
                nse->cb(smb2, status, NULL, nse->cb_data);
                nse_free(nse);
                dcerpc_destroy_context(dce);
                return;
        }

        nse->cb(smb2, rep->status, rep, nse->cb_data);
        nse_free(nse);
        dcerpc_destroy_context(dce);
}

static void
share_enum_bind_cb(struct dcerpc_context *dce, int status,
                      void *command_data, void *cb_data)
{
        struct smb2nse *nse = cb_data;
        struct smb2_context *smb2 = dcerpc_get_smb2_context(dce);

        if (status != SMB2_STATUS_SUCCESS) {
                nse->cb(smb2, status, NULL, nse->cb_data);
                nse_free(nse);
                dcerpc_destroy_context(dce);
                return;
        }

        status = dcerpc_call_async(dce,
                                   SRVSVC_NETSHAREENUMALL,
                                   srvsvc_netshareenumall_encoder, &nse->ea_req,
                                   srvsvc_netshareenumall_decoder,
                                   sizeof(struct srvsvc_netshareenumall_rep),
                                   share_enum_ioctl_cb, nse);
        if (status) {
                nse->cb(smb2, status, NULL, nse->cb_data);
                nse_free(nse);
                dcerpc_destroy_context(dce);
                return;
        }
}

static void
share_enum_connect_cb(struct dcerpc_context *dce, int status,
                      void *command_data, void *cb_data)
{
        struct smb2nse *nse = cb_data;
        struct smb2_context *smb2 = dcerpc_get_smb2_context(dce);

        if (status != SMB2_STATUS_SUCCESS) {
                nse->cb(smb2, status, NULL, nse->cb_data);
                nse_free(nse);
                dcerpc_destroy_context(dce);
                return;
        }

        status = dcerpc_bind_async(dce, share_enum_bind_cb, nse);
        if (status) {
                nse->cb(smb2, status, NULL, nse->cb_data);
                nse_free(nse);
                dcerpc_destroy_context(dce);
                return;
        }
}

int
smb2_share_enum_async(struct smb2_context *smb2,
                      smb2_command_cb cb, void *cb_data)
{
        struct dcerpc_context *dce;
        struct smb2nse *nse;
        int rc;

        dce = dcerpc_create_context(smb2, "srvsvc", &srvsvc_interface);
        if (dce == NULL) {
                return -ENOMEM;
        }
        
        nse = calloc(1, sizeof(struct smb2nse));
        if (nse == NULL) {
                smb2_set_error(smb2, "Failed to allocate nse");
                dcerpc_destroy_context(dce);
                return -ENOMEM;
        }
        nse->cb = cb;
        nse->cb_data = cb_data;

        nse->ea_req.server = smb2->server;
        nse->ea_req.level = 1;
        nse->ea_req.ctr = NULL;
        nse->ea_req.max_buffer = 0xffffffff;
        nse->ea_req.resume_handle = 0;

        rc = dcerpc_open_async(dce, share_enum_connect_cb, nse);
        if (rc) {
                free(nse);
                dcerpc_destroy_context(dce);
                return rc;
        }
        
        return 0;
}
