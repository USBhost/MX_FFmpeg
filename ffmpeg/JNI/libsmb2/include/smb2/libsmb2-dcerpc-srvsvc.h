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

#define SRVSVC_NETSHAREENUMALL 15

struct dcerpc_context;
struct dcerpc_pdu;

int srvsvc_netshareenumall_decoder(struct dcerpc_context *dce,
                                   struct dcerpc_pdu *pdu,
                                   struct smb2_iovec *iov, int offset,
                                   void *ptr);
int srvsvc_netshareenumall_encoder(struct dcerpc_context *ctx,
                                   struct dcerpc_pdu *pdu,
                                   struct smb2_iovec *iov, int offset,
                                   void *ptr);

#endif /* !_LIBSMB2_DCERPC_SRVSVC_H_ */
