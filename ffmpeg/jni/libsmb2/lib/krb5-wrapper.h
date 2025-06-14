/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
#ifndef _KRB5_WRAPPER_H_
#define _KRB5_WRAPPER_H_

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

#ifdef HAVE_LIBKRB5

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <gssapi/gssapi.h>

static const gss_OID_desc gss_mech_spnego = {
    6, "\x2b\x06\x01\x05\x05\x02"
};

static const gss_OID_desc spnego_mech_krb5 = {
    9, "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"
};

static const gss_OID_desc spnego_mech_ntlmssp = {
   10, "\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a"
};

struct private_auth_data {
        gss_ctx_id_t context;
        gss_cred_id_t cred;
        gss_name_t user_name;
        gss_name_t target_name;
        gss_const_OID mech_type;
        uint32_t req_flags;
        gss_buffer_desc output_token;
        char *g_server;
};

void
krb5_free_auth_data(struct private_auth_data *auth);

unsigned char *
krb5_get_output_token_buffer(struct private_auth_data *auth_data);

int
krb5_get_output_token_length(struct private_auth_data *auth_data);

struct private_auth_data *
krb5_negotiate_reply(struct smb2_context *smb2,
                     const char *server,
                     const char *domain,
                     const char *user_name,
                     const char *password);

int
krb5_session_get_session_key(struct smb2_context *smb2,
                             struct private_auth_data *auth_data);

int
krb5_session_request(struct smb2_context *smb2,
                     struct private_auth_data *auth_data,
                     unsigned char *buf, int len);

void
krb5_set_gss_error(struct smb2_context *smb2, char *func,
                   uint32_t maj, uint32_t min);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_LIBKRB5 */

#endif /* _KRB5_WRAPPER_H_ */
