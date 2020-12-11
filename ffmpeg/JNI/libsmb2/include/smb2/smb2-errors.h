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

/*
 * NTSTATUS fields
 */
#define SMB2_STATUS_SEVERITY_MASK    0xc0000000
#define SMB2_STATUS_SEVERITY_SUCCESS 0x00000000
#define SMB2_STATUS_SEVERITY_INFO    0x40000000
#define SMB2_STATUS_SEVERITY_WARNING 0x80000000
#define SMB2_STATUS_SEVERITY_ERROR   0xc0000000

#define SMB2_STATUS_CUSTOMER_MASK    0x20000000

#define SMB2_STATUS_FACILITY_MASK    0x0fff0000

#define SMB2_STATUS_CODE_MASK        0x0000ffff


/* Error codes */
#define SMB2_STATUS_SUCCESS                  0x00000000
#define SMB2_STATUS_CANCELLED                0xffffffff
#define SMB2_STATUS_PENDING                  0x00000103
#define SMB2_STATUS_SMB_BAD_FID              0x00060001
#define SMB2_STATUS_NO_MORE_FILES            0x80000006
#define SMB2_STATUS_NOT_IMPLEMENTED          0xC0000002
#define SMB2_STATUS_INVALID_HANDLE           0xC0000008
#define SMB2_STATUS_INVALID_PARAMETER        0xC000000d
#define SMB2_STATUS_NO_SUCH_DEVICE           0xC000000E
#define SMB2_STATUS_NO_SUCH_FILE             0xC000000F
#define SMB2_STATUS_INVALID_DEVICE_REQUEST   0xC0000010
#define SMB2_STATUS_END_OF_FILE              0xC0000011
#define SMB2_STATUS_NO_MEDIA_IN_DEVICE       0xC0000013
#define SMB2_STATUS_MORE_PROCESSING_REQUIRED 0xC0000016
#define SMB2_STATUS_INVALID_LOCK_SEQUENCE    0xC000001E
#define SMB2_STATUS_INVALID_VIEW_SIZE        0xC000001F
#define SMB2_STATUS_ALREADY_COMMITTED        0xC0000021
#define SMB2_STATUS_ACCESS_DENIED            0xC0000022
#define SMB2_STATUS_OBJECT_TYPE_MISMATCH     0xC0000024
#define SMB2_STATUS_OBJECT_NAME_NOT_FOUND    0xC0000034
#define SMB2_STATUS_OBJECT_NAME_COLLISION    0xC0000035
#define SMB2_STATUS_PORT_DISCONNECTED        0xC0000037
#define SMB2_STATUS_OBJECT_PATH_INVALID      0xC0000039
#define SMB2_STATUS_OBJECT_PATH_NOT_FOUND    0xC000003A
#define SMB2_STATUS_OBJECT_PATH_SYNTAX_BAD   0xC000003B
#define SMB2_STATUS_DATA_ERROR               0xC000003E
#define SMB2_STATUS_CRC_ERROR                0xC000003F
#define SMB2_STATUS_SECTION_TOO_BIG          0xC0000040
#define SMB2_STATUS_PORT_CONNECTION_REFUSED  0xC0000041
#define SMB2_STATUS_INVALID_PORT_HANDLE      0xC0000042
#define SMB2_STATUS_SHARING_VIOLATION        0xC0000043
#define SMB2_STATUS_THREAD_IS_TERMINATING    0xC000004B
#define SMB2_STATUS_FILE_LOCK_CONFLICT       0xC0000054
#define SMB2_STATUS_LOCK_NOT_GRANTED         0xC0000055
#define SMB2_STATUS_DELETE_PENDING           0xC0000056
#define SMB2_STATUS_PRIVILEGE_NOT_HELD       0xC0000061
#define SMB2_STATUS_LOGON_FAILURE            0xC000006d
#define SMB2_STATUS_ACCOUNT_RESTRICTION      0xC000006E
#define SMB2_STATUS_INVALID_LOGON_HOURS      0xC000006F
#define SMB2_STATUS_PASSWORD_EXPIRED         0xC0000071
#define SMB2_STATUS_ACCOUNT_DISABLED         0xC0000072
#define SMB2_STATUS_DISK_FULL                0xC000007F
#define SMB2_STATUS_TOO_MANY_PAGING_FILES    0xC0000097
#define SMB2_STATUS_DFS_EXIT_PATH_FOUND      0xC000009B
#define SMB2_STATUS_DEVICE_DATA_ERROR        0xC000009C
#define SMB2_STATUS_MEDIA_WRITE_PROTECTED    0xC00000A2
#define SMB2_STATUS_ILLEGAL_FUNCTION         0xC00000AF
#define SMB2_STATUS_PIPE_DISCONNECTED        0xC00000B0
#define SMB2_STATUS_FILE_IS_A_DIRECTORY      0xC00000BA
#define SMB2_STATUS_BAD_NETWORK_PATH         0xC00000BE
#define SMB2_STATUS_NETWORK_ACCESS_DENIED    0xC00000CA
#define SMB2_STATUS_BAD_NETWORK_NAME         0xC00000CC
#define SMB2_STATUS_NOT_SAME_DEVICE          0xC00000D4
#define SMB2_STATUS_FILE_RENAMED             0xC00000D5
#define SMB2_STATUS_REDIRECTOR_NOT_STARTED   0xC00000FB
#define SMB2_STATUS_DIRECTORY_NOT_EMPTY      0xC0000101
#define SMB2_STATUS_NOT_A_DIRECTORY          0xC0000103
#define SMB2_STATUS_PROCESS_IS_TERMINATING   0xC000010A
#define SMB2_STATUS_TOO_MANY_OPENED_FILES    0xC000011F
#define SMB2_STATUS_CANNOT_DELETE            0xC0000121
#define SMB2_STATUS_FILE_DELETED             0xC0000123
#define SMB2_STATUS_FILE_CLOSED              0xC0000128
#define SMB2_STATUS_INSUFF_SERVER_RESOURCES  0xC0000205
#define SMB2_STATUS_HANDLE_NOT_CLOSABLE      0xC0000235
#define SMB2_STATUS_NOT_A_REPARSE_POINT      0xC0000275