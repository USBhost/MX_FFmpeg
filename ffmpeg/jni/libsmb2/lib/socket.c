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

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_POLL_H
#if defined(ESP_PLATFORM)
#include <sys/poll.h>
#else
#include <poll.h>
#endif
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include "portable-endian.h"
#include <errno.h>

#if !defined(PS2_IOP_PLATFORM)
#include <fcntl.h>
#endif

#if !defined(PS2_EE_PLATFORM) && !defined(PS2_IOP_PLATFORM)
#include <sys/socket.h>
#endif

#include "compat.h"

#include "slist.h"
#include "smb2.h"
#include "libsmb2.h"
#include "smb3-seal.h"
#include "libsmb2-private.h"

#define MAX_URL_SIZE 1024

/* Timeout in ms between 2 consecutive socket connection.
 * The rfc8305 recommends a timeout of 250ms and a minimum timeout of 100ms.
 * Since the smb is most likely used on local network, use an aggressive
 * timeout of 100ms. */
#define HAPPY_EYEBALLS_TIMEOUT 100

static int
smb2_connect_async_next_addr(struct smb2_context *smb2, const struct addrinfo *base);

void
smb2_close_connecting_fds(struct smb2_context *smb2)
{
        size_t i;
        for (i = 0; i < smb2->connecting_fds_count; ++i) {
                int fd = smb2->connecting_fds[i];

                /* Don't close the connected fd */
                if (fd == smb2->fd || fd == -1)
                        continue;

                if (smb2->change_fd) {
                        smb2->change_fd(smb2, fd, SMB2_DEL_FD);
                }
                close(fd);
        }
        free(smb2->connecting_fds);
        smb2->connecting_fds = NULL;
        smb2->connecting_fds_count = 0;

        if (smb2->addrinfos != NULL) {
                freeaddrinfo(smb2->addrinfos);
                smb2->addrinfos = NULL;
        }
        smb2->next_addrinfo = NULL;
}

static int
smb2_get_credit_charge(struct smb2_context *smb2, struct smb2_pdu *pdu)
{
        int credits = 0;

        while (pdu) {
                credits += pdu->header.credit_charge;
                pdu = pdu->next_compound;
        }

        return credits;
}

int
smb2_which_events(struct smb2_context *smb2)
{
        int events = smb2->fd != -1 ? POLLIN : POLLOUT;

        if (smb2->outqueue != NULL &&
            smb2_get_credit_charge(smb2, smb2->outqueue) <= smb2->credits) {
                events |= POLLOUT;
        }

        return events;
}

t_socket smb2_get_fd(struct smb2_context *smb2)
{
        if (smb2->fd != -1) {
                return smb2->fd;
        } else if (smb2->connecting_fds_count > 0) {
                return smb2->connecting_fds[0];
        } else {
                return -1;
        }
}

const t_socket *
smb2_get_fds(struct smb2_context *smb2, size_t *fd_count, int *timeout)
{
        if (smb2->fd != -1) {
                *fd_count = 1;
                *timeout = -1;
                return &smb2->fd;
        } else {
                *fd_count = smb2->connecting_fds_count;
                *timeout = smb2->next_addrinfo != NULL ? HAPPY_EYEBALLS_TIMEOUT : -1;
                return smb2->connecting_fds;
        }
}

static int
smb2_write_to_socket(struct smb2_context *smb2)
{
        struct smb2_pdu *pdu;

        if (smb2->fd == -1) {
                smb2_set_error(smb2, "trying to write but not connected");
                return -1;
        }

        while ((pdu = smb2->outqueue) != NULL) {
                struct iovec iov[SMB2_MAX_VECTORS];
                struct iovec *tmpiov;
                struct smb2_pdu *tmp_pdu;
                size_t num_done = pdu->out.num_done;
                int i, niov = 1;
                ssize_t count;
                uint32_t spl = 0, tmp_spl, credit_charge = 0;

                for (tmp_pdu = pdu; tmp_pdu; tmp_pdu = tmp_pdu->next_compound) {
                        credit_charge += pdu->header.credit_charge;
                }
                if (smb2->dialect > SMB2_VERSION_0202) {
                        if (credit_charge > smb2->credits) {
                                return 0;
                        }
                }

                if (pdu->seal) {
                        niov = 2;
                        spl = pdu->crypt_len;
                        iov[1].iov_base = pdu->crypt;
                        iov[1].iov_len  = pdu->crypt_len;
                } else {
                        /* Copy all the vectors from all PDUs in the
                         * compound set.
                         */
                        for (tmp_pdu = pdu; tmp_pdu;
                             tmp_pdu = tmp_pdu->next_compound) {
                                for (i = 0; i < tmp_pdu->out.niov;
                                     i++, niov++) {
                                        iov[niov].iov_base = tmp_pdu->out.iov[i].buf;
                                        iov[niov].iov_len = tmp_pdu->out.iov[i].len;
                                        spl += tmp_pdu->out.iov[i].len;
                                }
                        }
                }

                /* Add the SPL vector as the first vector */
                tmp_spl = htobe32(spl);
                iov[0].iov_base = &tmp_spl;
                iov[0].iov_len = SMB2_SPL_SIZE;

                tmpiov = iov;

                /* Skip the vectors we have alredy written */
                while (num_done >= tmpiov->iov_len) {
                        num_done -= tmpiov->iov_len;
                        tmpiov++;
                        niov--;
                }

                /* Adjust the first vector to send */
                tmpiov->iov_base = (char *)tmpiov->iov_base + num_done;
                tmpiov->iov_len -= num_done;

                count = writev(smb2->fd, tmpiov, niov);
                if (count == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                return 0;
                        }
                        smb2_set_error(smb2, "Error when writing to "
                                       "socket :%d %s", errno,
                                       smb2_get_error(smb2));
                        return -1;
                }

                pdu->out.num_done += count;

                if (pdu->out.num_done == SMB2_SPL_SIZE + spl) {
                        SMB2_LIST_REMOVE(&smb2->outqueue, pdu);
                        smb2_change_events(smb2, smb2->fd, smb2_which_events(smb2));
                        while (pdu) {
                                tmp_pdu = pdu->next_compound;

                                /* As we have now sent all the PDUs we
                                 * can remove the chaining.
                                 * On the receive side we will treat all
                                 * PDUs as individual PDUs.
                                 */
                                pdu->next_compound = NULL;
                                smb2->credits -= pdu->header.credit_charge;

                                SMB2_LIST_ADD_END(&smb2->waitqueue, pdu);
                                pdu = tmp_pdu;
                        }
                }
        }
        return 0;
}

typedef ssize_t (*read_func)(struct smb2_context *smb2,
                             const struct iovec *iov, int iovcnt);

static int smb2_read_data(struct smb2_context *smb2, read_func func,
                          int has_xfrmhdr)
{
        struct iovec iov[SMB2_MAX_VECTORS];
        struct iovec *tmpiov;
        int i, niov, is_chained;
        size_t num_done;
        size_t iov_offset = 0;
        static char smb3tfrm[4] = {0xFD, 'S', 'M', 'B'};
        struct smb2_pdu *pdu = smb2->pdu;
        ssize_t count, len;

read_more_data:
        num_done = smb2->in.num_done;

        /* Copy all the current vectors to our work vector */
        niov = smb2->in.niov;
        for (i = 0; i < niov; i++) {
                iov[i].iov_base = smb2->in.iov[i].buf;
                iov[i].iov_len = smb2->in.iov[i].len;
        }
        tmpiov = iov;

        /* Skip the vectors we have already read */
        while (num_done >= tmpiov->iov_len) {
                num_done -= tmpiov->iov_len;
                tmpiov++;
                niov--;
        }

        /* Adjust the first vector to read */
        tmpiov->iov_base = (char *)tmpiov->iov_base + num_done;
        tmpiov->iov_len -= num_done;

        /* Read into our trimmed iovectors */
        count = func(smb2, tmpiov, niov);
        if (count < 0) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEINTR || err == WSAEWOULDBLOCK) {
#else
                int err = errno;
                if (err == EINTR || err == EAGAIN) {
#endif
                        return 0;
                }
                smb2_set_error(smb2, "Read from socket failed, "
                               "errno:%d. Closing socket.", err);
                return -1;
        }
        if (count == 0) {
                /* remote side has closed the socket. */
                return -1;
        }
        smb2->in.num_done += count;

        if (smb2->in.num_done < smb2->in.total_size) {
                goto read_more_data;
        }

        /* At this point we have all the data we need for the current phase */
        switch (smb2->recv_state) {
        case SMB2_RECV_SPL:
                smb2->spl = be32toh(smb2->spl);
                smb2->recv_state = SMB2_RECV_HEADER;
                smb2_add_iovector(smb2, &smb2->in, &smb2->header[0],
                                  SMB2_HEADER_SIZE, NULL);
                goto read_more_data;
        case SMB2_RECV_HEADER:
                if (!memcmp(smb2->in.iov[smb2->in.niov - 1].buf, smb3tfrm, 4)) {
                        smb2->in.iov[smb2->in.niov - 1].len = 52;
                        len = smb2->spl - 52;
                        smb2->in.total_size -= 12;
                        smb2_add_iovector(smb2, &smb2->in,
                                          malloc(len),
                                          len, free);
                        memcpy(smb2->in.iov[smb2->in.niov - 1].buf,
                               &smb2->in.iov[smb2->in.niov - 2].buf[52], 12);
                        smb2->recv_state = SMB2_RECV_TRFM;
                        goto read_more_data;
                }
                if (smb2_decode_header(smb2, &smb2->in.iov[smb2->in.niov - 1],
                                       &smb2->hdr) != 0) {
                        smb2_set_error(smb2, "Failed to decode smb2 "
                                       "header: %s", smb2_get_error(smb2));
                        return -1;
                }
                /* Record the offset for the start of payload data. */
                smb2->payload_offset = smb2->in.num_done;

                smb2->credits += smb2->hdr.credit_request_response;

                if (!(smb2->hdr.flags & SMB2_FLAGS_SERVER_TO_REDIR)) {
                        smb2_set_error(smb2, "received non-reply");
                        return -1;
                }
                if (smb2->hdr.status == SMB2_STATUS_PENDING) {
                        /* Pending. Just treat the rest of the data as
                         * padding then check for and skip processing below.
                         * We will eventually receive a proper reply for this
                         * request sometime later.
                         */

                        len = smb2->spl - smb2->in.num_done;
                        /* If we don't have a transform header we are reading
                         * straight from the socket, and not a buffer,
                         * so we need to take the SPL size into account.
                         */
                        if (!has_xfrmhdr) {
                                len += SMB2_SPL_SIZE;
                        }
                        /* Add padding before the next PDU */
                        smb2->recv_state = SMB2_RECV_PAD;
                        smb2_add_iovector(smb2, &smb2->in,
                                          malloc(len),
                                          len, free);
                        goto read_more_data;
                }

                pdu = smb2->pdu = smb2_find_pdu(smb2, smb2->hdr.message_id);
                if (pdu == NULL) {
                        smb2_set_error(smb2, "no matching PDU found");
                        return -1;
                }
                SMB2_LIST_REMOVE(&smb2->waitqueue, pdu);

                len = smb2_get_fixed_size(smb2, pdu);
                if (len < 0) {
                        smb2_set_error(smb2, "can not determine fixed size");
                        return -1;
                }

                smb2->recv_state = SMB2_RECV_FIXED;
                smb2_add_iovector(smb2, &smb2->in,
                                  malloc(len & 0xfffe),
                                  len & 0xfffe, free);
                goto read_more_data;
        case SMB2_RECV_FIXED:
                len = smb2_process_payload_fixed(smb2, pdu);
                if (len < 0) {
                        smb2_set_error(smb2, "Failed to parse fixed part of "
                                       "command payload. %s",
                                       smb2_get_error(smb2));
                        return -1;
                }

                /* Add application provided iovectors */
                if (len) {
                        for (i = 0; i < pdu->in.niov; i++) {
                                size_t num = pdu->in.iov[i].len;

                                if (num > len) {
                                        num = len;
                                }
                                smb2_add_iovector(smb2, &smb2->in,
                                                  pdu->in.iov[i].buf,
                                                  num, NULL);
                                len -= num;

                                if (len == 0) {
                                        smb2->recv_state = SMB2_RECV_VARIABLE;
                                        goto read_more_data;
                                }
                        }
                        if (len > 0) {
                                smb2->recv_state = SMB2_RECV_VARIABLE;
                                smb2_add_iovector(smb2, &smb2->in,
                                                  malloc(len),
                                                  len, free);
                                goto read_more_data;
                        }
                }

                /* Check for padding */
                if (smb2->hdr.next_command) {
                        len = smb2->hdr.next_command - (SMB2_HEADER_SIZE +
                                  smb2->in.num_done - smb2->payload_offset);
                } else {
                        len = smb2->spl + SMB2_SPL_SIZE - smb2->in.num_done;
                        /*
                         * We never read the SPL when handling decrypted
                         * payloads.
                         */
                        if (smb2->enc) {
                                len -= SMB2_SPL_SIZE;
                        }
                }
                if (len < 0) {
                        smb2_set_error(smb2, "Negative number of PAD bytes "
                                       "encountered during PDU decode of"
                                       "fixed payload");
                        return -1;
                }
                if (len > 0) {
                        /* Add padding before the next PDU */
                        smb2->recv_state = SMB2_RECV_PAD;
                        smb2_add_iovector(smb2, &smb2->in,
                                          malloc(len),
                                          len, free);
                        goto read_more_data;
                }

                /* If len == 0 it means there is no padding and we are finished
                 * reading this PDU */
                break;
        case SMB2_RECV_VARIABLE:
                if (smb2_process_payload_variable(smb2, pdu) < 0) {
                        smb2_set_error(smb2, "Failed to parse variable part of "
                                       "command payload. %s",
                                       smb2_get_error(smb2));
                        return -1;
                }

                /* Check for padding */
                if (smb2->hdr.next_command) {
                        len = smb2->hdr.next_command - (SMB2_HEADER_SIZE +
                                  smb2->in.num_done - smb2->payload_offset);
                } else {
                        len = smb2->spl + SMB2_SPL_SIZE - smb2->in.num_done;
                        /*
                         * We never read the SPL when handling decrypted
                         * payloads.
                         */
                        if (smb2->enc) {
                                len -= SMB2_SPL_SIZE;
                        }
                }
                if (len < 0) {
                        smb2_set_error(smb2, "Negative number of PAD bytes "
                                       "encountered during PDU decode of"
                                       "variable payload");
                        return -1;
                }
                if (len > 0) {
                        /* Add padding before the next PDU */
                        smb2->recv_state = SMB2_RECV_PAD;
                        smb2_add_iovector(smb2, &smb2->in,
                                          malloc(len),
                                          len, free);
                        goto read_more_data;
                }

                /* If len == 0 it means there is no padding and we are finished
                 * reading this PDU */
                break;
        case SMB2_RECV_PAD:
                /* We are finished reading all the data and padding for this
                 * PDU. Break out of the switch and invoke the callback.
                 */
                break;
        case SMB2_RECV_TRFM:
                /* We are finished reading the full payload for the
                 * encrypted packet.
                 */
                smb2->in.num_done = 0;
                if (smb3_decrypt_pdu(smb2)) {
                        return -1;
                }
                /* We are all done now with this PDU. Reset num_done to 0
                 * and restart with a new SPL for the next chain.
                 */
                return 0;
        }

        if (smb2->in.niov < 2) {
                smb2_set_error(smb2, "Too few io vectors in received PDU.");
                return -1;
        }

        if (smb2->hdr.status == SMB2_STATUS_PENDING) {
                /* This was a pending command. Just ignore it and proceed
                 * to read the next chain.
                 */
                smb2->in.num_done = 0;
                return 0;
        }

        /* We don't yet have the signing key until later, once session
         * setup has completed, so we can not yet verify the signature
         * of the final leg of session setup.
         */
        if (smb2->sign &&
            (smb2->hdr.flags & SMB2_FLAGS_SIGNED) &&
            (smb2->hdr.command != SMB2_SESSION_SETUP) ) {
                uint8_t signature[16];
                memcpy(&signature[0], &smb2->in.iov[1 + iov_offset].buf[48], 16);
                if (smb2_calc_signature(smb2, &smb2->in.iov[1 + iov_offset].buf[48],
                                        &smb2->in.iov[1 + iov_offset],
                                        smb2->in.niov - 1 - iov_offset) < 0) {
                        return -1;
                }
                if (memcmp(&signature[0], &smb2->in.iov[1 + iov_offset].buf[48], 16)) {
                        smb2_set_error(smb2, "Wrong signature in received "
                                       "PDU");
                        return -1;
                }
        }

        is_chained = smb2->hdr.next_command;

        pdu->cb(smb2, smb2->hdr.status, pdu->payload, pdu->cb_data);
        smb2_free_pdu(smb2, pdu);
        smb2->pdu = NULL;

        if (is_chained) {
                /* Record at which iov we ended in this loop so we know where to start in the next */
                iov_offset = smb2->in.niov - 1;
                smb2->recv_state = SMB2_RECV_HEADER;
                smb2_add_iovector(smb2, &smb2->in, &smb2->header[0],
                                  SMB2_HEADER_SIZE, NULL);
                goto read_more_data;
        }

        /* We are all done now with this chain. Reset num_done to 0
         * and restart with a new SPL for the next chain.
         */
        smb2->in.num_done = 0;

        return 0;
}

static ssize_t smb2_readv_from_socket(struct smb2_context *smb2,
                                      const struct iovec *iov, int iovcnt)
{
        return readv(smb2->fd, iov, iovcnt);
}

static int
smb2_read_from_socket(struct smb2_context *smb2)
{
        /* initialize the input vectors to the spl and the header
         * which are both static data in the smb2 context.
         * additional vectors will be added when we can map this to
         * the corresponding pdu.
         */
        if (smb2->in.num_done == 0) {
                smb2->recv_state = SMB2_RECV_SPL;
                smb2->spl = 0;

                smb2_free_iovector(smb2, &smb2->in);
                smb2_add_iovector(smb2, &smb2->in, (uint8_t *)&smb2->spl,
                                  SMB2_SPL_SIZE, NULL);
        }

        return smb2_read_data(smb2, smb2_readv_from_socket, 0);
}

static ssize_t smb2_readv_from_buf(struct smb2_context *smb2,
                                   const struct iovec *iov, int iovcnt)
{
        int i, len, count = 0;

        for (i=0;i<iovcnt;i++){
                len = iov[i].iov_len;
                if (len > smb2->enc_len - smb2->enc_pos) {
                        len = smb2->enc_len - smb2->enc_pos;
                }
                memcpy(iov[i].iov_base, &smb2->enc[smb2->enc_pos], len);
                smb2->enc_pos += len;
                count += len;
        }
        return count;
}

int
smb2_read_from_buf(struct smb2_context *smb2)
{
        return smb2_read_data(smb2, smb2_readv_from_buf, 1);
}

static void
smb2_close_connecting_fd(struct smb2_context *smb2, int fd)
{
        size_t i;

        close(fd);
        /* Remove the fd from the connecting_fds array */
        for (i = 0; i < smb2->connecting_fds_count; ++i) {
                if (fd == smb2->connecting_fds[i]) {
                        memmove(&smb2->connecting_fds[i],
                                &smb2->connecting_fds[i + 1],
                                smb2->connecting_fds_count - i - 1);
                        smb2->connecting_fds_count--;
                        return;
                }
        }
}

int
smb2_service_fd(struct smb2_context *smb2, int fd, int revents)
{
        int ret = 0;

        if (fd == -1) {
                /* Connect to a new addr in parallel */
                if (smb2->next_addrinfo != NULL) {
                    int err = smb2_connect_async_next_addr(smb2,
                                                           smb2->next_addrinfo);
                    return err == 0 ? 0 : -1;
                }
                goto out;
        } else if (fd != smb2->fd) {
                int fd_found = 0;
                size_t i;
                for (i = 0; i < smb2->connecting_fds_count; ++i) {
                        if (fd == smb2->connecting_fds[i])
                        {
                                fd_found = 1;
                                break;
                        }
                }
                if (fd_found == 0) {
                        /* Not an error, this can happen if more than one
                         * connecting fds had POLLOUT events. In that case,
                         * only the first one is connected and all other FDs
                         * are dropped. */
                        return 0;
                }
        }

        if (revents & POLLERR) {
                int err = 0;
                socklen_t err_size = sizeof(err);

                if (smb2->fd == -1 && smb2->next_addrinfo != NULL) {
                        /* Connecting fd failed, try to connect to the next addr */
                        smb2_close_connecting_fd(smb2, fd);

                        err = smb2_connect_async_next_addr(smb2, smb2->next_addrinfo);
                        /* error already set by connect_async_ai() */
                        if (err == 0) {
                                return 0;
                        }
                } else if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
                               (char *)&err, &err_size) != 0 || err != 0) {
                        if (err == 0) {
                                err = errno;
                        }
                        smb2_set_error(smb2, "smb2_service: socket error "
                                        "%s(%d).",
                                        strerror(err), err);
                } else {
                        smb2_set_error(smb2, "smb2_service: POLLERR, "
                                        "Unknown socket error.");
                }

                if (smb2->connect_cb) {
                        smb2->connect_cb(smb2, err, NULL, smb2->connect_data);
                        smb2->connect_cb = NULL;
                }
                ret = -1;
                goto out;
        }
        if (revents & POLLHUP) {
                smb2_set_error(smb2, "smb2_service: POLLHUP, "
                                "socket error.");
                ret = -1;
                goto out;
        }

        if (smb2->fd == -1 && revents & POLLOUT) {
                int err = 0;
                socklen_t err_size = sizeof(err);

                if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
                               (char *)&err, &err_size) != 0 || err != 0) {
                        if (err == 0) {
                                err = errno;
                        }
                        if (smb2->next_addrinfo != NULL) {
                                /* Connecting fd failed, try to connect to the next addr */
                                smb2_close_connecting_fd(smb2, fd);

                                err = smb2_connect_async_next_addr(smb2, smb2->next_addrinfo);
                                /* error already set by connect_async_ai() */
                                if (err == 0) {
                                        return 0;
                                }
                        } else {
                                smb2_set_error(smb2, "smb2_service: socket error "
                                                "%s(%d) while connecting.",
                                                strerror(err), err);
                        }
                        if (smb2->connect_cb) {
                                smb2->connect_cb(smb2, err,
                                                 NULL, smb2->connect_data);
                                smb2->connect_cb = NULL;
                        }
                        ret = -1;
                        goto out;
                }

                smb2->fd = fd;

                smb2_close_connecting_fds(smb2);

                smb2_change_events(smb2, smb2->fd, smb2_which_events(smb2));
                if (smb2->connect_cb) {
                        smb2->connect_cb(smb2, 0, NULL,        smb2->connect_data);
                        smb2->connect_cb = NULL;
                }
                goto out;
        }

        if (revents & POLLIN) {
                if (smb2_read_from_socket(smb2) != 0) {
                        ret = -1;
                        goto out;
                }
        }

        if (revents & POLLOUT && smb2->outqueue != NULL) {
                if (smb2_write_to_socket(smb2) != 0) {
                        ret = -1;
                        goto out;
                }
        }

 out:
        if (smb2->timeout) {
                smb2_timeout_pdus(smb2);
        }
        return ret;
}

int
smb2_service(struct smb2_context *smb2, int revents)
{
        if (smb2->connecting_fds_count > 0) {
                return smb2_service_fd(smb2, smb2->connecting_fds[0], revents);
        } else {
                return smb2_service_fd(smb2, smb2->fd, revents);
        }
}

static void
set_nonblocking(t_socket fd)
{
#if defined(WIN32)
        unsigned long opt = 1;
        ioctlsocket(fd, FIONBIO, &opt);
#else
        unsigned v;
        v = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, v | O_NONBLOCK);
#endif
}

static int
set_tcp_sockopt(t_socket sockfd, int optname, int value)
{
        int level;
#ifndef SOL_TCP
        struct protoent *buf;

        if ((buf = getprotobyname("tcp")) != NULL) {
                level = buf->p_proto;
        } else {
                return -1;
        }
#else
        level = SOL_TCP;
#endif

        return setsockopt(sockfd, level, optname, (char *)&value, sizeof(value));
}

static int
connect_async_ai(struct smb2_context *smb2, const struct addrinfo *ai, int *fd_out)
{
        int family, fd;
        socklen_t socksize;
        struct sockaddr_storage ss;

        memset(&ss, 0, sizeof(ss));
        switch (ai->ai_family) {
        case AF_INET:
                socksize = sizeof(struct sockaddr_in);
                memcpy(&ss, ai->ai_addr, socksize);
#ifdef HAVE_SOCK_SIN_LEN
                ((struct sockaddr_in *)&ss)->sin_len = socksize;
#endif
                break;
        case AF_INET6:
                socksize = sizeof(struct sockaddr_in6);
                memcpy(&ss, ai->ai_addr, socksize);
#ifdef HAVE_SOCK_SIN_LEN
                ((struct sockaddr_in6 *)&ss)->sin6_len = socksize;
#endif
                break;
        default:
                smb2_set_error(smb2, "Unknown address family :%d. "
                                "Only IPv4/IPv6 supported so far.",
                                ai->ai_family);
                return -EINVAL;

        }
        family = ai->ai_family;

        fd = socket(family, SOCK_STREAM, 0);
        if (fd == -1) {
                smb2_set_error(smb2, "Failed to open smb2 socket. "
                               "Errno:%s(%d).", strerror(errno), errno);
                return -EIO;
        }

        set_nonblocking(fd);
        set_tcp_sockopt(fd, TCP_NODELAY, 1);

        if (connect(fd, (struct sockaddr *)&ss, socksize) != 0
#ifndef _MSC_VER
                  && errno != EINPROGRESS) {
#else
                  && WSAGetLastError() != WSAEWOULDBLOCK) {
#endif
                smb2_set_error(smb2, "Connect failed with errno : "
                        "%s(%d)", strerror(errno), errno);
                close(fd);
                return -EIO;
        }

        *fd_out = fd;
        return 0;
}

static int
smb2_connect_async_next_addr(struct smb2_context *smb2, const struct addrinfo *base)
{
        int err = -1;
        const struct addrinfo *ai;
        for (ai = base; ai != NULL; ai = ai->ai_next) {
                int fd;
                err = connect_async_ai(smb2, ai, &fd);

                if (err == 0) {
                        /* clear the error that could be set by a previous ai
                         * connection */
                        smb2_set_error(smb2, "");
                        smb2->connecting_fds[smb2->connecting_fds_count++] = fd;
                        if (smb2->change_fd) {
                                smb2->change_fd(smb2, fd, SMB2_ADD_FD);
                                smb2_change_events(smb2, fd, POLLOUT);
                        }

                        smb2->next_addrinfo = ai->ai_next;
                        break;
                }
        }

        return err;
}

/* Copied from FFmpeg: libavformat/network.c */
static void interleave_addrinfo(struct addrinfo *base)
{
        struct addrinfo **next = &base->ai_next;
        while (*next) {
                struct addrinfo *cur = *next;
                // Iterate forward until we find an entry of a different family.
                if (cur->ai_family == base->ai_family) {
                        next = &cur->ai_next;
                        continue;
                }
                if (cur == base->ai_next) {
                        // If the first one following base is of a different family, just
                        // move base forward one step and continue.
                        base = cur;
                        next = &base->ai_next;
                        continue;
                }
                // Unchain cur from the rest of the list from its current spot.
                *next = cur->ai_next;
                // Hook in cur directly after base.
                cur->ai_next = base->ai_next;
                base->ai_next = cur;
                // Restart with a new base. We know that before moving the cur element,
                // everything between the previous base and cur had the same family,
                // different from cur->ai_family. Therefore, we can keep next pointing
                // where it was, and continue from there with base at the one after
                // cur.
                base = cur->ai_next;
        }
}

int
smb2_connect_async(struct smb2_context *smb2, const char *server,
                   smb2_command_cb cb, void *private_data)
{
        char *addr, *host, *port;
        int err;
        size_t addr_count = 0;
        const struct addrinfo *ai;

        if (smb2->fd != -1) {
                smb2_set_error(smb2, "Trying to connect but already "
                               "connected.");
                return -EINVAL;
        }

        addr = strdup(server);
        if (addr == NULL) {
                smb2_set_error(smb2, "Out-of-memory: "
                               "Failed to strdup server address.");
                return -ENOMEM;
        }
        host = addr;
        port = host;

        /* ipv6 in [...] form ? */
        if (host[0] == '[') {
                char *str;

                host++;
                str = strchr(host, ']');
                if (str == NULL) {
                        free(addr);
                        smb2_set_error(smb2, "Invalid address:%s  "
                                "Missing ']' in IPv6 address", server);
                        return -EINVAL;
                }
                *str = 0;
                port = str + 1;
        }

        port = strchr(port, ':');
        if (port != NULL) {
                *port++ = 0;
        } else {
                port = "445";
        }

        /* is it a hostname ? */
        err = getaddrinfo(host, port, NULL, &smb2->addrinfos);
        if (err != 0) {
                free(addr);
#ifdef _WINDOWS
                if (err == WSANOTINITIALISED)
                {
                        smb2_set_error(smb2, "Winsock was not initialized. "
                                "Please call WSAStartup().");
                        return -WSANOTINITIALISED; 
                }
                else
#endif
                {
                        smb2_set_error(smb2, "Invalid address:%s  "
                                "Can not resolv into IPv4/v6.", server);
                }
                switch (err) {
                    case EAI_AGAIN:
                        return -EAGAIN;
                    case EAI_NONAME:
#ifdef EAI_NODATA
#if EAI_NODATA != EAI_NONAME /* Equal in MSCV */
                    case EAI_NODATA:
#endif
#endif
                    case EAI_SERVICE:
                    case EAI_FAIL:
#ifdef EAI_ADDRFAMILY /* Not available in MSVC */
                    case EAI_ADDRFAMILY:
#endif
                        return -EIO;
                    case EAI_MEMORY:
                        return -ENOMEM;
#ifdef EAI_SYSTEM /* Not available in MSVC */
                    case EAI_SYSTEM:
                        return -errno;
#endif
                    default:
                        return -EINVAL;
                }
        }
        free(addr);

        interleave_addrinfo(smb2->addrinfos);

        /* Allocate connecting fds array */
        for (ai = smb2->addrinfos; ai != NULL; ai = ai->ai_next)
                addr_count++;
        smb2->connecting_fds = malloc(sizeof(t_socket) * addr_count);
        if (smb2->connecting_fds == NULL) {
                freeaddrinfo(smb2->addrinfos);
                smb2->addrinfos = NULL;
                return -ENOMEM;
        }

        err = smb2_connect_async_next_addr(smb2, smb2->addrinfos);

        if (err == 0) {
                smb2->connect_cb   = cb;
                smb2->connect_data = private_data;
        } else {
                free(smb2->connecting_fds);
                smb2->connecting_fds = NULL;

                freeaddrinfo(smb2->addrinfos);
                smb2->addrinfos = NULL;
                smb2->next_addrinfo = NULL;
        }

        return err;
}

void smb2_change_events(struct smb2_context *smb2, int fd, int events)
{
        if (smb2->events == events) {
                return;
        }

        if (smb2->change_events) {
                smb2->change_events(smb2, fd, events);
                smb2->events = events;
        }
}
