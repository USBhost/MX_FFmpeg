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

#ifndef _LIBSMB2_DCERPC_SRVSVC_H_
#define _LIBSMB2_DCERPC_SRVSVC_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SRVSVC_NETRSHAREENUM      0x0f
#define SRVSVC_NETRSHAREGETINFO   0x10

struct dcerpc_context;
struct dcerpc_pdu;


/* Low 2 bits desctibe the type */
#define SHARE_TYPE_DISKTREE  0
#define SHARE_TYPE_PRINTQ    1
#define SHARE_TYPE_DEVICE    2
#define SHARE_TYPE_IPC       3

#define SHARE_TYPE_TEMPORARY 0x40000000
#define SHARE_TYPE_HIDDEN    0x80000000

struct srvsvc_netshareinfo1 {
        const char *name;
        uint32_t type;
	const char *comment;
};

struct srvsvc_netsharectr1 {
        uint32_t count;
        struct srvsvc_netshareinfo1 *array;
};

struct srvsvc_netsharectr {
        uint32_t level;
        union {
                struct srvsvc_netsharectr1 ctr1;
        };
};

struct srvsvc_netshareenumall_req {
        const char *server;
        uint32_t level;
        struct srvsvc_netsharectr *ctr;
        uint32_t max_buffer;
        uint32_t resume_handle;
};

struct srvsvc_netshareenumall_rep {
        uint32_t status;

        uint32_t level;
        struct srvsvc_netsharectr *ctr;
        uint32_t total_entries;
        uint32_t resume_handle;
};

struct srvsvc_netshareinfo {
        uint32_t level;
        union {
                struct srvsvc_netshareinfo1 info1;
        };
};

struct srvsvc_netrsharegetinfo_req {
        const char *ServerName;
        const char *NetName;
        uint32_t Level;
};

struct srvsvc_netrsharegetinfo_rep {
        uint32_t status;

        struct srvsvc_netshareinfo info;
};

struct srvsvc_rep {
        uint32_t status;
};

/*
 * Async share_enum()
 * This function only works when connected to the IPC$ share.
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success. Command_data is struct srvsvc_netshareenumall_rep *
 *          This pointer must be freed using smb2_free_data().
 * -errno : An error occured.
 */
int smb2_share_enum_async(struct smb2_context *smb2,
                          smb2_command_cb cb, void *cb_data);

int srvsvc_NetrShareEnum_rep_coder(struct dcerpc_context *dce,
                                   struct dcerpc_pdu *pdu,
                                   struct smb2_iovec *iov, int offset,
                                   void *ptr);
int srvsvc_NetrShareEnum_req_coder(struct dcerpc_context *ctx,
                                   struct dcerpc_pdu *pdu,
                                   struct smb2_iovec *iov, int offset,
                                   void *ptr);
int srvsvc_NetrShareGetInfo_rep_coder(struct dcerpc_context *dce,
                                      struct dcerpc_pdu *pdu,
                                      struct smb2_iovec *iov, int offset,
                                      void *ptr);
int srvsvc_NetrShareGetInfo_req_coder(struct dcerpc_context *ctx,
                                      struct dcerpc_pdu *pdu,
                                      struct smb2_iovec *iov, int offset,
                                      void *ptr);
#ifdef __cplusplus
}
#endif

#endif /* !_LIBSMB2_DCERPC_SRVSVC_H_ */
