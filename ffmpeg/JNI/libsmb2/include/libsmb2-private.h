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

#ifndef _LIBSMB2_PRIVATE_H_
#define _LIBSMB2_PRIVATE_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MIN(a,b) (((a)<(b))?(a):(b))

#ifndef discard_const
#define discard_const(ptr) ((void *)((intptr_t)(ptr)))
#endif

#define MAX_ERROR_SIZE 256

#define PAD_TO_32BIT(len) ((len + 0x03) & 0xfffffffc)

#define SMB2_SPL_SIZE 4
#define SMB2_HEADER_SIZE 64

#define SMB2_SIGNATURE_SIZE 16
#define SMB2_KEY_SIZE 16

#define SMB2_MAX_VECTORS 256

struct smb2_io_vectors {
        size_t num_done;
        size_t total_size;
        int niov;
        struct smb2_iovec iov[SMB2_MAX_VECTORS];
};

struct smb2_async {
        uint64_t async_id;
};

struct smb2_sync {
        uint32_t process_id;
        uint32_t tree_id;
};
        
struct smb2_header {
        uint8_t protocol_id[4];
        uint16_t struct_size;
        uint16_t credit_charge;
        uint32_t status;
        uint16_t command;
        uint16_t credit_request_response;
        uint32_t flags;
        uint32_t next_command;
        uint64_t message_id;
        union {
                struct smb2_async async;
                struct smb2_sync sync;
        };
        uint64_t session_id;
        uint8_t signature[16];
};

/* States that we transition when we read data back from the server for
 * normal SMB2/3 :
 * 1: SMB2_RECV_SPL        SPL
 * 2: SMB2_RECV_HEADER     SMB2 Header
 * 3: SMB2_RECV_FIXED      The fixed part of the payload. 
 * 4: SMB2_RECV_VARIABLE   Optional variable part of the payload.
 * 5: SMB2_RECV_PAD        Optional padding
 *
 * 2-5 will be repeated for compound commands.
 * 4-5 are optional and may or may not be present depending on the
 *     type of command.
 *
 * States for SMB3 encryption:
 * 1: SMB2_RECV_SPL        SPL
 * 2: SMB2_RECV_HEADER     SMB3 Transform Header
 * 3: SMB2_RECV_TRFM       encrypted payload
 */
enum smb2_recv_state {
        SMB2_RECV_SPL = 0,
        SMB2_RECV_HEADER,
        SMB2_RECV_FIXED,
        SMB2_RECV_VARIABLE,
        SMB2_RECV_PAD,
        SMB2_RECV_TRFM,
};

enum smb2_sec {
        SMB2_SEC_UNDEFINED = 0,
        SMB2_SEC_NTLMSSP,
        SMB2_SEC_KRB5,
};

#define MAX_CREDITS 1024

struct smb2_context {

        t_socket fd;
        int is_connected;

        enum smb2_sec sec;

        uint16_t security_mode;
        int use_cached_creds;

        enum smb2_negotiate_version version;

        const char *server;
        const char *share;
        const char *user;

        /* Only used with --without-libkrb5 */
        const char *password;
        const char *domain;
        const char *workstation;
        char client_challenge[8];

        smb2_command_cb connect_cb;
        void *connect_data;

        int credits;

        char client_guid[16];

        uint32_t tree_id;
        uint64_t message_id;
        uint64_t session_id;
        uint8_t *session_key;
        uint8_t session_key_size;

        uint8_t seal:1;
        uint8_t sign:1;
        uint8_t signing_key[SMB2_KEY_SIZE];
        uint8_t serverin_key[SMB2_KEY_SIZE];
        uint8_t serverout_key[SMB2_KEY_SIZE];

        /*
         * For handling received smb3 encrypted blobs
         */
        unsigned char *enc;
        size_t enc_len;
        int enc_pos;

        /*
         * For sending PDUs
         */
	struct smb2_pdu *outqueue;
	struct smb2_pdu *waitqueue;


        /*
         * For receiving PDUs
         */
        struct smb2_io_vectors in;
        enum smb2_recv_state recv_state;
        /* SPL for the (compound) command we are currently reading */
        uint32_t spl;
        /* buffer to avoid having to malloc the header */
        uint8_t header[SMB2_HEADER_SIZE];
        struct smb2_header hdr;
        /* Offset into smb2->in where the payload for the current PDU starts */
        size_t payload_offset;

        /* Pointer to the current PDU that we are receiving the reply for.
         * Only valid once the full smb2 header has been received.
         */
        struct smb2_pdu *pdu;

        /* Server capabilities */
        uint8_t supports_multi_credit;

        uint32_t max_transact_size;
        uint32_t max_read_size;
        uint32_t max_write_size;
        uint16_t dialect;

        char error_string[MAX_ERROR_SIZE];

        /* Open filehandles */
        struct smb2fh *fhs;
        /* Open dirhandles */
        struct smb2dir *dirs;
};

#define SMB2_MAX_PDU_SIZE 16*1024*1024

struct smb2_pdu {
        struct smb2_pdu *next;
        struct smb2_header header;

        struct smb2_pdu *next_compound;

        smb2_command_cb cb;
        void *cb_data;

        /* buffer to avoid having to malloc the headers */
        uint8_t hdr[SMB2_HEADER_SIZE];

        /* pointer to the unmarshalled payload in a reply */
        void *payload;

        /* For sending/receiving
         * out contains at least two vectors:
         * [0]  64 bytes for the smb header
         * [1+] command and and extra parameters
         *
         * in contains at least one vector:
         * [0+] command and and extra parameters
         */
        struct smb2_io_vectors out;
        struct smb2_io_vectors in;

        /* Data we need to retain between request/reply for QUERY INFO */
        uint8_t info_type;
        uint8_t file_info_class;

        /* For encrypted PDUs */
        uint8_t seal:1;
        uint32_t crypt_len;
        unsigned char *crypt;
};

/* UCS2 is always in Little Endianness */
struct ucs2 {
        int len;
        uint16_t val[1];
};

/* Returns a string converted to UCS2 format. Use free() to release
 * the ucs2 string.
 */
struct ucs2 *utf8_to_ucs2(const char *utf8);
        
/* Returns a string converted to UTF8 format. Use free() to release
 * the utf8 string.
 */
const char *ucs2_to_utf8(const uint16_t *str, int len);

/* Convert a win timestamp to a unix timeval */
void win_to_timeval(uint64_t smb2_time, struct smb2_timeval *tv);

/* Covnert unit timeval to a win timestamp */
uint64_t timeval_to_win(struct smb2_timeval *tv);

void smb2_set_error(struct smb2_context *smb2, const char *error_string,
                    ...);

void *smb2_alloc_init(struct smb2_context *smb2, size_t size);
void *smb2_alloc_data(struct smb2_context *smb2, void *memctx, size_t size);

struct smb2_iovec *smb2_add_iovector(struct smb2_context *smb2,
                                     struct smb2_io_vectors *v,
                                     uint8_t *buf, int len,
                                     void (*free)(void *));

int smb2_pad_to_64bit(struct smb2_context *smb2, struct smb2_io_vectors *v);

struct smb2_pdu *smb2_allocate_pdu(struct smb2_context *smb2,
                                   enum smb2_command command,
                                   smb2_command_cb cb, void *cb_data);
int smb2_process_payload_fixed(struct smb2_context *smb2,
                               struct smb2_pdu *pdu);
int smb2_process_payload_variable(struct smb2_context *smb2,
                                  struct smb2_pdu *pdu);
int smb2_get_fixed_size(struct smb2_context *smb2, struct smb2_pdu *pdu);
        
struct smb2_pdu *smb2_find_pdu(struct smb2_context *smb2, uint64_t message_id);
void smb2_free_iovector(struct smb2_context *smb2, struct smb2_io_vectors *v);

int smb2_decode_header(struct smb2_context *smb2, struct smb2_iovec *iov,
                       struct smb2_header *hdr);
        
int smb2_set_uint8(struct smb2_iovec *iov, int offset, uint8_t value);
int smb2_set_uint16(struct smb2_iovec *iov, int offset, uint16_t value);
int smb2_set_uint32(struct smb2_iovec *iov, int offset, uint32_t value);
int smb2_set_uint64(struct smb2_iovec *iov, int offset, uint64_t value);

int smb2_get_uint8(struct smb2_iovec *iov, int offset, uint8_t *value);
int smb2_get_uint16(struct smb2_iovec *iov, int offset, uint16_t *value);
int smb2_get_uint32(struct smb2_iovec *iov, int offset, uint32_t *value);
int smb2_get_uint64(struct smb2_iovec *iov, int offset, uint64_t *value);

int smb2_process_error_fixed(struct smb2_context *smb2,
                             struct smb2_pdu *pdu);
int smb2_process_error_variable(struct smb2_context *smb2,
                                struct smb2_pdu *pdu);
int smb2_process_negotiate_fixed(struct smb2_context *smb2,
                                 struct smb2_pdu *pdu);
int smb2_process_negotiate_variable(struct smb2_context *smb2,
                                    struct smb2_pdu *pdu);
int smb2_process_session_setup_fixed(struct smb2_context *smb2,
                                     struct smb2_pdu *pdu);
int smb2_process_session_setup_variable(struct smb2_context *smb2,
                                        struct smb2_pdu *pdu);
int smb2_process_tree_connect_fixed(struct smb2_context *smb2,
                                    struct smb2_pdu *pdu);
int smb2_process_create_fixed(struct smb2_context *smb2,
                              struct smb2_pdu *pdu);
int smb2_process_create_variable(struct smb2_context *smb2,
                                 struct smb2_pdu *pdu);
int smb2_process_query_directory_fixed(struct smb2_context *smb2,
                                       struct smb2_pdu *pdu);
int smb2_process_query_directory_variable(struct smb2_context *smb2,
                                          struct smb2_pdu *pdu);
int smb2_process_query_info_fixed(struct smb2_context *smb2,
                                  struct smb2_pdu *pdu);
int smb2_process_query_info_variable(struct smb2_context *smb2,
                                     struct smb2_pdu *pdu);
int smb2_process_close_fixed(struct smb2_context *smb2,
                             struct smb2_pdu *pdu);
int smb2_process_set_info_fixed(struct smb2_context *smb2,
                                struct smb2_pdu *pdu);
int smb2_process_tree_disconnect_fixed(struct smb2_context *smb2,
                                       struct smb2_pdu *pdu);
int smb2_process_logoff_fixed(struct smb2_context *smb2,
                              struct smb2_pdu *pdu);
int smb2_process_echo_fixed(struct smb2_context *smb2,
                            struct smb2_pdu *pdu);
int smb2_process_flush_fixed(struct smb2_context *smb2,
                             struct smb2_pdu *pdu);
int smb2_process_read_fixed(struct smb2_context *smb2,
                            struct smb2_pdu *pdu);
int smb2_process_write_fixed(struct smb2_context *smb2,
                             struct smb2_pdu *pdu);
int smb2_process_ioctl_fixed(struct smb2_context *smb2,
                             struct smb2_pdu *pdu);
int smb2_process_ioctl_variable(struct smb2_context *smb2,
                                struct smb2_pdu *pdu);

int smb2_decode_fileidfulldirectoryinformation(
        struct smb2_context *smb2,
        struct smb2_fileidfulldirectoryinformation *fs,
        struct smb2_iovec *vec);

int smb2_decode_file_basic_info(struct smb2_context *smb2,
                                void *memctx,
                                struct smb2_file_basic_info *fs,
                                struct smb2_iovec *vec);
int smb2_encode_file_basic_info(struct smb2_context *smb2,
                                struct smb2_file_basic_info *fs,
                                struct smb2_iovec *vec);

int smb2_decode_file_standard_info(struct smb2_context *smb2,
                                   void *memctx,
                                   struct smb2_file_standard_info *fs,
                                   struct smb2_iovec *vec);

int smb2_decode_file_all_info(struct smb2_context *smb2,
                              void *memctx,
                              struct smb2_file_all_info *fs,
                              struct smb2_iovec *vec);

int smb2_decode_security_descriptor(struct smb2_context *smb2,
                                    void *memctx,
                                    struct smb2_security_descriptor *sd,
                                    struct smb2_iovec *vec);

int smb2_decode_file_fs_size_info(struct smb2_context *smb2,
                                  void *memctx,
                                  struct smb2_file_fs_size_info *fs,
                                  struct smb2_iovec *vec);
int smb2_decode_file_fs_device_info(struct smb2_context *smb2,
                                    void *memctx,
                                    struct smb2_file_fs_device_info *fs,
                                    struct smb2_iovec *vec);
int smb2_decode_file_fs_control_info(struct smb2_context *smb2,
                                     void *memctx,
                                     struct smb2_file_fs_control_info *fs,
                                     struct smb2_iovec *vec);
int smb2_decode_file_fs_full_size_info(struct smb2_context *smb2,
                                       void *memctx,
                                       struct smb2_file_fs_full_size_info *fs,
                                       struct smb2_iovec *vec);
int smb2_decode_file_fs_sector_size_info(struct smb2_context *smb2,
                                     void *memctx,
                                     struct smb2_file_fs_sector_size_info *fs,
                                     struct smb2_iovec *vec);
int smb2_decode_reparse_data_buffer(struct smb2_context *smb2,
                                    void *memctx,
                                    struct smb2_reparse_data_buffer *rp,
                                    struct smb2_iovec *vec);
void smb2_free_all_fhs(struct smb2_context *smb2);
void smb2_free_all_dirs(struct smb2_context *smb2);

int smb2_read_from_buf(struct smb2_context *smb2);

#ifdef __cplusplus
}
#endif

#endif /* !_LIBSMB2_PRIVATE_H_ */
