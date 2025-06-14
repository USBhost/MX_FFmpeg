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

#include "compat.h"

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-private.h"

int
smb2_decode_file_fs_volume_info(struct smb2_context *smb2,
                                void *memctx,
                                struct smb2_file_fs_volume_info *fs,
                                struct smb2_iovec *vec)
{
        uint64_t t;
        const char *name;

        smb2_get_uint64(vec,  0, &t);
        win_to_timeval(t, &fs->creation_time);
	smb2_get_uint32(vec,  8, &fs->volume_serial_number);
	smb2_get_uint32(vec, 12, &fs->volume_label_length);
	smb2_get_uint8(vec,  16, &fs->supports_objects);
	smb2_get_uint8(vec,  17, &fs->reserved);
        name = utf16_to_utf8((uint16_t *)&vec->buf[18],
                            fs->volume_label_length / 2);
        fs->volume_label = smb2_alloc_data(smb2, memctx, strlen(name) + 1);
        if (fs->volume_label == NULL) {
                free(discard_const(name));
                return -1;
        }
        strcat(discard_const(fs->volume_label), name);
        free(discard_const(name));

	return 0;
}

int
smb2_decode_file_fs_size_info(struct smb2_context *smb2,
                              void *memctx,
                              struct smb2_file_fs_size_info *fs,
                              struct smb2_iovec *vec)
{
        if (vec->len < 24) {
                return -1;
        }

        smb2_get_uint64(vec,  0, &fs->total_allocation_units);
        smb2_get_uint64(vec,  8, &fs->available_allocation_units);
        smb2_get_uint32(vec, 16, &fs->sectors_per_allocation_unit);
        smb2_get_uint32(vec, 20, &fs->bytes_per_sector);

        return 0;
}

int
smb2_decode_file_fs_device_info(struct smb2_context *smb2,
                                void *memctx,
                                struct smb2_file_fs_device_info *fs,
                                struct smb2_iovec *vec)
{
        if (vec->len < 8) {
                return -1;
        }

        smb2_get_uint32(vec,  0, &fs->device_type);
        smb2_get_uint32(vec,  4, &fs->characteristics);

        return 0;
}

int
smb2_decode_file_fs_control_info(struct smb2_context *smb2,
                                 void *memctx,
                                 struct smb2_file_fs_control_info *fs,
                                 struct smb2_iovec *vec)
{
        if (vec->len < 48) {
                return -1;
        }

        smb2_get_uint64(vec,  0, &fs->free_space_start_filtering);
        smb2_get_uint64(vec,  8, &fs->free_space_threshold);
        smb2_get_uint64(vec, 16, &fs->free_space_stop_filtering);
        smb2_get_uint64(vec, 24, &fs->default_quota_threshold);
        smb2_get_uint64(vec, 32, &fs->default_quota_limit);
        smb2_get_uint32(vec, 40, &fs->file_system_control_flags);

        return 0;
}

int
smb2_decode_file_fs_full_size_info(struct smb2_context *smb2,
                                   void *memctx,
                                   struct smb2_file_fs_full_size_info *fs,
                                   struct smb2_iovec *vec)
{
        if (vec->len < 32) {
                return -1;
        }

        smb2_get_uint64(vec,  0, &fs->total_allocation_units);
        smb2_get_uint64(vec,  8, &fs->caller_available_allocation_units);
        smb2_get_uint64(vec, 16, &fs->actual_available_allocation_units);
        smb2_get_uint32(vec, 24, &fs->sectors_per_allocation_unit);
        smb2_get_uint32(vec, 28, &fs->bytes_per_sector);

        return 0;
}

int
smb2_decode_file_fs_sector_size_info(struct smb2_context *smb2,
                                     void *memctx,
                                     struct smb2_file_fs_sector_size_info *fs,
                                     struct smb2_iovec *vec)
{
        if (vec->len < 28) {
                return -1;
        }

        smb2_get_uint32(vec,  0, &fs->logical_bytes_per_sector);
        smb2_get_uint32(vec,  4,
           &fs->physical_bytes_per_sector_for_atomicity);
        smb2_get_uint32(vec,  8,
           &fs->physical_bytes_per_sector_for_performance);
        smb2_get_uint32(vec, 12,
           &fs->file_system_effective_physical_bytes_per_sector_for_atomicity);
        smb2_get_uint32(vec, 16, &fs->flags);
        smb2_get_uint32(vec, 20, &fs->byte_offset_for_sector_alignment);
        smb2_get_uint32(vec, 24, &fs->byte_offset_for_partition_alignment);

        return 0;
}
