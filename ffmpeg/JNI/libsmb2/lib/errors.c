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

#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>

#include "smb2.h"

const char *nterror_to_str(uint32_t status) {
        switch (status) {
        case SMB2_STATUS_SUCCESS:
                return "STATUS_SUCCESS";
        case SMB2_STATUS_CANCELLED:
                return "STATUS_CANCELLED";
        case SMB2_STATUS_PENDING:
                return "STATUS_PENDING";
        case SMB2_STATUS_NO_MORE_FILES:
                return "STATUS_NO_MORE_FILES";
        case SMB2_STATUS_NOT_IMPLEMENTED:
                return "STATUS_NOT_IMPLEMENTED";
        case SMB2_STATUS_INVALID_HANDLE:
                return "STATUS_INVALID_HANDLE";
        case SMB2_STATUS_INVALID_PARAMETER:
                return "STATUS_INVALID_PARAMETER";
        case SMB2_STATUS_NO_SUCH_DEVICE:
                return "STATUS_NO_SUCH_DEVICE";
        case SMB2_STATUS_NO_SUCH_FILE:
                return "STATUS_NO_SUCH_FILE";
        case SMB2_STATUS_INVALID_DEVICE_REQUEST:
                return "STATUS_INVALID_DEVICE_REQUEST";
        case SMB2_STATUS_END_OF_FILE:
                return "STATUS_END_OF_FILE";
        case SMB2_STATUS_NO_MEDIA_IN_DEVICE:
                return "STATUS_NO_MEDIA_IN_DEVICE";
        case SMB2_STATUS_MORE_PROCESSING_REQUIRED:
                return "STATUS_MORE_PROCESSING_REQUIRED";
        case SMB2_STATUS_INVALID_LOCK_SEQUENCE:
                return "STATUS_INVALID_LOCK_SEQUENCE";
        case SMB2_STATUS_INVALID_VIEW_SIZE:
                return "STATUS_INVALID_VIEW_SIZE";
        case SMB2_STATUS_ALREADY_COMMITTED:
                return "STATUS_ALREADY_COMMITTED";
        case SMB2_STATUS_ACCESS_DENIED:
                return "STATUS_ACCESS_DENIED";
        case SMB2_STATUS_OBJECT_TYPE_MISMATCH:
                return "STATUS_OBJECT_TYPE_MISMATCH";
        case SMB2_STATUS_OBJECT_NAME_NOT_FOUND:
                return "STATUS_OBJECT_NAME_NOT_FOUND";
        case SMB2_STATUS_OBJECT_NAME_COLLISION:
                return "STATUS_OBJECT_NAME_COLLISION";
        case SMB2_STATUS_PORT_DISCONNECTED:
                return "STATUS_PORT_DISCONNECTED";
        case SMB2_STATUS_OBJECT_PATH_INVALID:
                return "STATUS_OBJECT_PATH_INVALID";
        case SMB2_STATUS_OBJECT_PATH_NOT_FOUND:
                return "STATUS_OBJECT_PATH_NOT_FOUND";
        case SMB2_STATUS_OBJECT_PATH_SYNTAX_BAD:
                return "STATUS_OBJECT_PATH_SYNTAX_BAD";
        case SMB2_STATUS_DATA_ERROR:
                return "STATUS_DATA_ERROR";
        case SMB2_STATUS_CRC_ERROR:
                return "STATUS_CRC_ERROR";
        case SMB2_STATUS_SECTION_TOO_BIG:
                return "STATUS_SECTION_TOO_BIG";
        case SMB2_STATUS_PORT_CONNECTION_REFUSED:
                return "STATUS_PORT_CONNECTION_REFUSED";
        case SMB2_STATUS_INVALID_PORT_HANDLE:
                return "STATUS_INVALID_PORT_HANDLE";
        case SMB2_STATUS_SHARING_VIOLATION:
                return "STATUS_SHARING_VIOLATION";
        case SMB2_STATUS_THREAD_IS_TERMINATING:
                return "STATUS_THREAD_IS_TERMINATING";
        case SMB2_STATUS_FILE_LOCK_CONFLICT:
                return "STATUS_FILE_LOCK_CONFLICT";
        case SMB2_STATUS_LOCK_NOT_GRANTED:
                return "STATUS_LOCK_NOT_GRANTED";
        case SMB2_STATUS_DELETE_PENDING:
                return "STATUS_DELETE_PENDING";
        case SMB2_STATUS_PRIVILEGE_NOT_HELD:
                return "STATUS_PRIVILEGE_NOT_HELD";
        case SMB2_STATUS_LOGON_FAILURE:
                return "STATUS_LOGON_FAILURE";
        case SMB2_STATUS_ACCOUNT_RESTRICTION:
                return "STATUS_ACCOUNT_RESTRICTION";
        case SMB2_STATUS_INVALID_LOGON_HOURS:
                return "STATUS_INVALID_LOGON_HOURS";
        case SMB2_STATUS_PASSWORD_EXPIRED:
                return "STATUS_PASSWORD_EXPIRED";
        case SMB2_STATUS_ACCOUNT_DISABLED:
                return "STATUS_ACCOUNT_DISABLED";
        case SMB2_STATUS_DISK_FULL:
                return "STATUS_DISK_FULL";
        case SMB2_STATUS_TOO_MANY_PAGING_FILES:
                return "STATUS_TOO_MANY_PAGING_FILES";
        case SMB2_STATUS_DFS_EXIT_PATH_FOUND:
                return "STATUS_DFS_EXIT_PATH_FOUND";
        case SMB2_STATUS_DEVICE_DATA_ERROR:
                return "STATUS_DEVICE_DATA_ERROR";
        case SMB2_STATUS_MEDIA_WRITE_PROTECTED:
                return "STATUS_MEDIA_WRITE_PROTECTED";
        case SMB2_STATUS_ILLEGAL_FUNCTION:
                return "STATUS_ILLEGAL_FUNCTION";
        case SMB2_STATUS_PIPE_DISCONNECTED:
                return "STATUS_PIPE_DISCONNECTED";
        case SMB2_STATUS_FILE_IS_A_DIRECTORY:
                return "STATUS_FILE_IS_A_DIRECTORY";
        case SMB2_STATUS_BAD_NETWORK_PATH:
                return "STATUS_BAD_NETWORK_PATH";
        case SMB2_STATUS_NETWORK_ACCESS_DENIED:
                return "STATUS_NETWORK_ACCESS_DENIED";
        case SMB2_STATUS_BAD_NETWORK_NAME:
                return "STATUS_BAD_NETWORK_NAME";
        case SMB2_STATUS_NOT_SAME_DEVICE:
                return "STATUS_NOT_SAME_DEVICE";
        case SMB2_STATUS_FILE_RENAMED:
                return "STATUS_FILE_RENAMED";
        case SMB2_STATUS_REDIRECTOR_NOT_STARTED:
                return "STATUS_REDIRECTOR_NOT_STARTED";
        case SMB2_STATUS_DIRECTORY_NOT_EMPTY:
                return "STATUS_DIRECTORY_NOT_EMPTY";
        case SMB2_STATUS_NOT_A_DIRECTORY:
                return "STATUS_NOT_A_DIRECTORY";
        case SMB2_STATUS_PROCESS_IS_TERMINATING:
                return "STATUS_PROCESS_IS_TERMINATING";
        case SMB2_STATUS_TOO_MANY_OPENED_FILES:
                return "STATUS_TOO_MANY_OPENED_FILES";
        case SMB2_STATUS_CANNOT_DELETE:
                return "STATUS_CANNOT_DELETE";
        case SMB2_STATUS_FILE_DELETED:
                return "STATUS_FILE_DELETED";
        case SMB2_STATUS_FILE_CLOSED:
                return "STATUS_FILE_CLOSED";
        case SMB2_STATUS_INSUFF_SERVER_RESOURCES:
                return "STATUS_INSUFF_SERVER_RESOURCES";
        case SMB2_STATUS_HANDLE_NOT_CLOSABLE:
                return "STATUS_HANDLE_NOT_CLOSABLE";
        case SMB2_STATUS_NOT_A_REPARSE_POINT:
                return "STATUS_NOT_A_REPARSE_POINT";
        default:
                 return "Unknown";
        }
}

int nterror_to_errno(uint32_t status) {
        switch (status) {
        case SMB2_STATUS_SUCCESS:
        case SMB2_STATUS_END_OF_FILE:
                return 0;
        case SMB2_STATUS_PENDING:
                return EAGAIN;
        case SMB2_STATUS_CANCELLED:
                return ECONNRESET;
        case SMB2_STATUS_NO_SUCH_FILE:
        case SMB2_STATUS_NO_SUCH_DEVICE:
        case SMB2_STATUS_BAD_NETWORK_NAME:
        case SMB2_STATUS_OBJECT_NAME_NOT_FOUND:
        case SMB2_STATUS_OBJECT_PATH_INVALID:
        case SMB2_STATUS_OBJECT_PATH_NOT_FOUND:
        case SMB2_STATUS_OBJECT_PATH_SYNTAX_BAD:
        case SMB2_STATUS_DFS_EXIT_PATH_FOUND:
        case SMB2_STATUS_REDIRECTOR_NOT_STARTED:
#ifdef MXTECHS
        case SMB2_STATUS_BAD_NETWORK_PATH:
#endif
                return ENOENT;
        case SMB2_STATUS_FILE_CLOSED:
        case SMB2_STATUS_SMB_BAD_FID:
        case SMB2_STATUS_INVALID_HANDLE:
        case SMB2_STATUS_OBJECT_TYPE_MISMATCH:
        case SMB2_STATUS_PORT_DISCONNECTED:
        case SMB2_STATUS_INVALID_PORT_HANDLE:
        case SMB2_STATUS_HANDLE_NOT_CLOSABLE:
                return EBADF;
        case SMB2_STATUS_MORE_PROCESSING_REQUIRED:
                return EAGAIN;
        case SMB2_STATUS_ACCESS_DENIED:
        case SMB2_STATUS_NETWORK_ACCESS_DENIED:
        case SMB2_STATUS_ACCOUNT_RESTRICTION:
        case SMB2_STATUS_INVALID_LOGON_HOURS:
        case SMB2_STATUS_PASSWORD_EXPIRED:
        case SMB2_STATUS_ACCOUNT_DISABLED:
                return EACCES;
        case SMB2_STATUS_INVALID_LOCK_SEQUENCE:
        case SMB2_STATUS_INVALID_VIEW_SIZE:
        case SMB2_STATUS_ALREADY_COMMITTED:
        case SMB2_STATUS_PORT_CONNECTION_REFUSED:
        case SMB2_STATUS_THREAD_IS_TERMINATING:
        case SMB2_STATUS_DELETE_PENDING:
        case SMB2_STATUS_PRIVILEGE_NOT_HELD:
        case SMB2_STATUS_FILE_IS_A_DIRECTORY:
        case SMB2_STATUS_FILE_RENAMED:
        case SMB2_STATUS_PROCESS_IS_TERMINATING:
        case SMB2_STATUS_DIRECTORY_NOT_EMPTY:
        case SMB2_STATUS_CANNOT_DELETE:
        case SMB2_STATUS_FILE_DELETED:
                return EPERM;
        case SMB2_STATUS_NO_MORE_FILES:
                return ENODATA;
        case SMB2_STATUS_LOGON_FAILURE:
                return ECONNREFUSED;
        case SMB2_STATUS_NOT_A_DIRECTORY:
                return ENOTDIR;
        case SMB2_STATUS_NOT_IMPLEMENTED:
        case SMB2_STATUS_INVALID_DEVICE_REQUEST:
        case SMB2_STATUS_ILLEGAL_FUNCTION:
        case SMB2_STATUS_INVALID_PARAMETER:
        case SMB2_STATUS_NOT_A_REPARSE_POINT:
                return EINVAL;
        case SMB2_STATUS_TOO_MANY_OPENED_FILES:
                return EMFILE;
        case SMB2_STATUS_SECTION_TOO_BIG:
        case SMB2_STATUS_TOO_MANY_PAGING_FILES:
        case SMB2_STATUS_INSUFF_SERVER_RESOURCES:
                return ENOMEM;
        case SMB2_STATUS_NOT_SAME_DEVICE:
                return EXDEV;
        case SMB2_STATUS_SHARING_VIOLATION:
                return ETXTBSY;
        case SMB2_STATUS_FILE_LOCK_CONFLICT:
        case SMB2_STATUS_LOCK_NOT_GRANTED:
                return EDEADLK;
        case SMB2_STATUS_OBJECT_NAME_COLLISION:
                return EEXIST;
        case SMB2_STATUS_PIPE_DISCONNECTED:
                return EPIPE;
        case SMB2_STATUS_MEDIA_WRITE_PROTECTED:
                return EROFS;
        case SMB2_STATUS_NO_MEDIA_IN_DEVICE:
                return ENODEV;
        case SMB2_STATUS_DATA_ERROR:
        case SMB2_STATUS_CRC_ERROR:
        case SMB2_STATUS_DEVICE_DATA_ERROR:
                return EIO;
        case SMB2_STATUS_DISK_FULL:
                return ENOSPC;
        default:
                return EIO;
        }
}
