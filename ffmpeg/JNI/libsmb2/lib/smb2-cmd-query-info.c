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
smb2_encode_query_info_request(struct smb2_context *smb2,
                               struct smb2_pdu *pdu,
                               struct smb2_query_info_request *req)
{
        int len;
        uint8_t *buf;
        struct smb2_iovec *iov;

        if (req->input_buffer_length > 0) {
                smb2_set_error(smb2, "No support for input buffers, yet");
                return -1;
        }

        len = SMB2_QUERY_INFO_REQUEST_SIZE & 0xfffffffe;
        buf = calloc(len, sizeof(uint8_t));
        if (buf == NULL) {
                smb2_set_error(smb2, "Failed to allocate query buffer");
                return -1;
        }
        
        iov = smb2_add_iovector(smb2, &pdu->out, buf, len, free);

        smb2_set_uint16(iov, 0, SMB2_QUERY_INFO_REQUEST_SIZE);
        smb2_set_uint8(iov, 2, req->info_type);
        smb2_set_uint8(iov, 3, req->file_info_class);
        smb2_set_uint32(iov,4, req->output_buffer_length);
        smb2_set_uint32(iov,12, req->input_buffer_length);
        smb2_set_uint32(iov,16, req->additional_information);
        smb2_set_uint32(iov,20, req->flags);
        memcpy(iov->buf + 24, req->file_id, SMB2_FD_SIZE);

        /* Remember what we asked for so that we can unmarshall the reply */
        pdu->info_type       = req->info_type;
        pdu->file_info_class = req->file_info_class;

        return 0;
}

struct smb2_pdu *
smb2_cmd_query_info_async(struct smb2_context *smb2,
                          struct smb2_query_info_request *req,
                          smb2_command_cb cb, void *cb_data)
{
        struct smb2_pdu *pdu;

        pdu = smb2_allocate_pdu(smb2, SMB2_QUERY_INFO, cb, cb_data);
        if (pdu == NULL) {
                return NULL;
        }

        if (smb2_encode_query_info_request(smb2, pdu, req)) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }
        
        if (smb2_pad_to_64bit(smb2, &pdu->out) != 0) {
                smb2_free_pdu(smb2, pdu);
                return NULL;
        }

        return pdu;
}

#define IOV_OFFSET (rep->output_buffer_offset - SMB2_HEADER_SIZE - \
                    (SMB2_QUERY_INFO_REPLY_SIZE & 0xfffe))

int
smb2_process_query_info_fixed(struct smb2_context *smb2,
                              struct smb2_pdu *pdu)
{
        struct smb2_query_info_reply *rep;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];
        uint16_t struct_size;

        rep = malloc(sizeof(*rep));
        if (rep == NULL) {
                smb2_set_error(smb2, "Failed to allocate query info reply");
                return -1;
        }
        pdu->payload = rep;

        smb2_get_uint16(iov, 0, &struct_size);
        if (struct_size != SMB2_QUERY_INFO_REPLY_SIZE ||
            (struct_size & 0xfffe) != iov->len) {
                smb2_set_error(smb2, "Unexpected size of Query Info "
                               "reply. Expected %d, got %d",
                               SMB2_QUERY_INFO_REPLY_SIZE,
                               (int)iov->len);
                return -1;
        }

        smb2_get_uint16(iov, 2, &rep->output_buffer_offset);
        smb2_get_uint32(iov, 4, &rep->output_buffer_length);

        if (rep->output_buffer_length == 0) {
                smb2_set_error(smb2, "No output buffer in Query "
                               "Info response");
                return -1;
        }
        if (rep->output_buffer_offset < SMB2_HEADER_SIZE +
            (SMB2_QUERY_INFO_REPLY_SIZE & 0xfffe)) {
                smb2_set_error(smb2, "Output buffer overlaps with "
                               "Query Info reply header");
                return -1;
        }

        /* Return the amount of data that the output buffer will take up.
         * Including any padding before the output buffer itself.
         */
        return IOV_OFFSET + rep->output_buffer_length;
}

int
smb2_process_query_info_variable(struct smb2_context *smb2,
                                 struct smb2_pdu *pdu)
{
        struct smb2_query_info_reply *rep = pdu->payload;
        struct smb2_iovec *iov = &smb2->in.iov[smb2->in.niov - 1];
        struct smb2_iovec vec = {&iov->buf[IOV_OFFSET],
                                 iov->len - IOV_OFFSET,
                                 NULL};
        void *ptr;

        switch (pdu->info_type) {
        case SMB2_0_INFO_FILE:
                switch (pdu->file_info_class) {
                case SMB2_FILE_BASIC_INFORMATION:
                        ptr = smb2_alloc_init(smb2,
                                  sizeof(struct smb2_file_basic_info));
                        if (smb2_decode_file_basic_info(smb2, ptr, ptr, &vec)) {
                                smb2_set_error(smb2, "could not decode file "
                                               "basic info. %s",
                                               smb2_get_error(smb2));
                                return -1;
                        }
                        break;
                case SMB2_FILE_STANDARD_INFORMATION:
                        ptr = smb2_alloc_init(smb2,
                                  sizeof(struct smb2_file_standard_info));
                        if (smb2_decode_file_standard_info(smb2, ptr, ptr,
                                                           &vec)) {
                                smb2_set_error(smb2, "could not decode file "
                                               "standard info. %s",
                                               smb2_get_error(smb2));
                                return -1;
                        }
                        break;
                case SMB2_FILE_ALL_INFORMATION:
                        ptr = smb2_alloc_init(smb2,
                                  sizeof(struct smb2_file_all_info));
                        if (smb2_decode_file_all_info(smb2, ptr, ptr, &vec)) {
                                smb2_set_error(smb2, "could not decode file "
                                               "all info. %s",
                                               smb2_get_error(smb2));
                                return -1;
                        }
                        break;
                default:
                        smb2_set_error(smb2, "Can not decode info_type/"
                                       "info_class %d/%d yet",
                                       pdu->info_type,
                                       pdu->file_info_class);
                        return -1;
                }
                break;
        case SMB2_0_INFO_SECURITY:
                ptr = smb2_alloc_init(smb2,
                                      sizeof(struct smb2_security_descriptor));
                if (smb2_decode_security_descriptor(smb2, ptr, ptr, &vec)) {
                        smb2_set_error(smb2, "could not decode security "
                                       "descriptor. %s",
                                       smb2_get_error(smb2));
                        return -1;
                }
                break;
        case SMB2_0_INFO_FILESYSTEM:
                switch (pdu->file_info_class) {
		case SMB2_FILE_FS_VOLUME_INFORMATION:
			ptr = smb2_alloc_init(smb2,
				  sizeof(struct smb2_file_fs_volume_info));
			if (smb2_decode_file_fs_volume_info(smb2, ptr, ptr,
							    &vec)) {
				smb2_set_error(smb2, "could not decode file "
					       "fs volume info. %s",
					       smb2_get_error(smb2));
				return -1;
			}
			break;
                case SMB2_FILE_FS_SIZE_INFORMATION:
                        ptr = smb2_alloc_init(smb2,
                                  sizeof(struct smb2_file_fs_size_info));
                        if (smb2_decode_file_fs_size_info(smb2, ptr, ptr,
                                                          &vec)) {
                                smb2_set_error(smb2, "could not decode file "
                                               "fs size info. %s",
                                               smb2_get_error(smb2));
                                return -1;
                        }
                        break;
                case SMB2_FILE_FS_DEVICE_INFORMATION:
                        ptr = smb2_alloc_init(smb2,
                                  sizeof(struct smb2_file_fs_device_info));
                        if (smb2_decode_file_fs_device_info(smb2, ptr, ptr,
                                                          &vec)) {
                                smb2_set_error(smb2, "could not decode file "
                                               "fs device info. %s",
                                               smb2_get_error(smb2));
                                return -1;
                        }
                        break;
                case SMB2_FILE_FS_CONTROL_INFORMATION:
                        ptr = smb2_alloc_init(smb2,
                                  sizeof(struct smb2_file_fs_control_info));
                        if (smb2_decode_file_fs_control_info(smb2, ptr, ptr,
                                                          &vec)) {
                                smb2_set_error(smb2, "could not decode file "
                                               "fs control info. %s",
                                               smb2_get_error(smb2));
                                return -1;
                        }
                        break;
                case SMB2_FILE_FS_FULL_SIZE_INFORMATION:
                        ptr = smb2_alloc_init(smb2,
                                  sizeof(struct smb2_file_fs_full_size_info));
                        if (smb2_decode_file_fs_full_size_info(smb2, ptr, ptr,
                                                               &vec)) {
                                smb2_set_error(smb2, "could not decode file "
                                               "fs full size info. %s",
                                               smb2_get_error(smb2));
                                return -1;
                        }
                        break;
                case SMB2_FILE_FS_SECTOR_SIZE_INFORMATION:
                        ptr = smb2_alloc_init(smb2,
                                  sizeof(struct smb2_file_fs_sector_size_info));
                        if (smb2_decode_file_fs_sector_size_info(smb2, ptr, ptr,
                                                                 &vec)) {
                                smb2_set_error(smb2, "could not decode file "
                                               "fs sector size info. %s",
                                               smb2_get_error(smb2));
                                return -1;
                        }
                        break;
                default:
                        smb2_set_error(smb2, "Can not decode info_type/"
                                       "info_class %d/%d yet",
                                       pdu->info_type,
                                       pdu->file_info_class);
                        return -1;
                }
                break;
        default:
                smb2_set_error(smb2, "Can not decode file info_type %d yet",
                               pdu->info_type);
                return -1;
        }

        rep->output_buffer = ptr;

        return 0;
}
