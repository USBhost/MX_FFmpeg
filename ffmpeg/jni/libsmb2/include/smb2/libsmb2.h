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

#ifndef _LIBSMB2_H_
#define _LIBSMB2_H_

#ifdef __cplusplus
extern "C" {
#endif

struct smb2_iovec {
        uint8_t *buf;
        size_t len;
        void (*free)(void *);
};

struct smb2_context;

/*
 * Generic callback for completion of smb2_*_async().
 * command_data depends on status.
 */
typedef void (*smb2_command_cb)(struct smb2_context *smb2, int status,
                                void *command_data, void *cb_data);

/* Stat structure */
#define SMB2_TYPE_FILE      0x00000000
#define SMB2_TYPE_DIRECTORY 0x00000001
#define SMB2_TYPE_LINK      0x00000002
struct smb2_stat_64 {
        uint32_t smb2_type;
        uint32_t smb2_nlink;
        uint64_t smb2_ino;
        uint64_t smb2_size;
        uint64_t smb2_atime;
        uint64_t smb2_atime_nsec;
        uint64_t smb2_mtime;
        uint64_t smb2_mtime_nsec;
        uint64_t smb2_ctime;
        uint64_t smb2_ctime_nsec;
    uint64_t smb2_btime;
    uint64_t smb2_btime_nsec;
};

struct smb2_statvfs {
        uint32_t        f_bsize;
        uint32_t        f_frsize;
        uint64_t        f_blocks;
        uint64_t        f_bfree;
        uint64_t        f_bavail;
        uint32_t        f_files;
        uint32_t        f_ffree;
        uint32_t        f_favail;
        uint32_t        f_fsid;
        uint32_t        f_flag;
        uint32_t        f_namemax;
};

struct smb2dirent {
        const char *name;
        struct smb2_stat_64 st;
};

#ifdef _MSC_VER
#include <winsock2.h>
typedef SOCKET t_socket;
#else
typedef int t_socket;
#endif

/*
 * Create an SMB2 context.
 * Function returns
 *  NULL  : Failed to create a context.
 *  *smb2 : A pointer to an smb2 context.
 */
struct smb2_context *smb2_init_context(void);

/*
 * Destroy an smb2 context.
 *
 * Any open "struct smb2fh" will automatically be freed. You can not reference
 * any "struct smb2fh" after the context is destroyed.
 * Any open "struct smb2dir" will automatically be freed. You can not reference
 * any "struct smb2dir" after the context is destroyed.
 * Any pending async commands will be aborted with -ECONNRESET.
 */
void smb2_destroy_context(struct smb2_context *smb2);

/*
 * EVENT SYSTEM INTEGRATION
 * ========================
 * The following functions are used to integrate libsmb2 in an event
 * system.
 *
 * The simplest way is by using smb2_get_fd() and smb2_which_events()
 * in every loop of the event system to detect which fd to use (it can change)
 * and which events should be waited for.
 * This is very simple to use but has the drawback of the overhead having to
 * call these two functions for every loop.
 *
 * This is suitable for trivial apps where you roll your event system
 * using select() or poll().
 *
 * See for example smb2-cat-async.c for an example on how to use these
 * two functions in an event loop.
 */
/*
 * Returns the file descriptor that libsmb2 uses.
 */
t_socket smb2_get_fd(struct smb2_context *smb2);
/*
 * Returns which events that we need to poll for for the smb2 file descriptor.
 */
int smb2_which_events(struct smb2_context *smb2);

/*
 * Returns file descriptors that libsmb2 use or is trying to connect to
 *
 * This function should be used when trying to connect with more than one
 * addresses in parallel, cf. rfc8305: Happy Eyeballs.
 *
 * The timeout, in ms, is valid during the socket connection step. The caller
 * should call smb2_service_fd() with fd = -1 when the timeout is reached.
 * This will trigger a new socket connection on the next resolved address. All
 * connecting fds will be closed when the first fd is connected. The timeout
 * will be -1 (infinite) once connected or if there is no next addresses to
 * connect to.
 */
const t_socket *
smb2_get_fds(struct smb2_context *smb2, size_t *fd_count, int *timeout);

/*
 * A much more scalable way to use smb2_fd_event_callbacks() to register
 * callbacks for libsmb2 to call anytime a filedescriptor is changed or when
 * the  events we are waiting for changes.
 * This way libsmb2 will do callbacks back into the application to inform
 * when fd or events change.
 *
 * This is suitable when you want to plug libsmb2 into a more sophisticated
 * eventsystem or if you use epoll() or similar.
 *
 * See for smb2-ls-async.c for a trivial example of using these callbacks.
 */
#define SMB2_ADD_FD 0
#define SMB2_DEL_FD 1
typedef void (*smb2_change_fd_cb)(struct smb2_context *smb2, int fd, int cmd);
typedef void (*smb2_change_events_cb)(struct smb2_context *smb2, int fd,
                                      int events);
void smb2_fd_event_callbacks(struct smb2_context *smb2,
                             smb2_change_fd_cb change_fd,
                             smb2_change_events_cb change_events);

/*
 * Called to process the events when events become available for the smb2
 * file descriptor.
 *
 * Returns:
 *  0 : Success
 * <0 : Unrecoverable failure. At this point the context can no longer be
 *      used and must be freed by calling smb2_destroy_context().
 *
 */
int smb2_service(struct smb2_context *smb2, int revents);

/*
 * Called to process the events when events become available for the smb2
 * file descriptor.
 *
 * Behave like smb2_service() with some differences:
 *  - must be called with a fd returned by smb2_get_fd() or smb2_get_fds(),
 *  - passing -1 as fd will trigger a new connection attempt on the next
 *  resolved address, cf. smb2_get_fds().
 *
 * Returns:
 *  0 : Success
 * <0 : Unrecoverable failure. At this point the context can no longer be
 *      used and must be freed by calling smb2_destroy_context().
 *
 */
int smb2_service_fd(struct smb2_context *smb2, int fd, int revents);

/*
 * Set the timeout in seconds after which a command will be aborted with
 * SMB2_STATUS_IO_TIMEOUT.
 * If you use timeouts with the async API you must make sure to call
 * smb2_service() at least once every second.
 *
 * Default is 0: No timeout.
 */
void smb2_set_timeout(struct smb2_context *smb2, int seconds);

/*
 * Set which version of SMB to negotiate.
 * Default is to let the server pick the version.
 */
enum smb2_negotiate_version {
        SMB2_VERSION_ANY  = 0,
        SMB2_VERSION_ANY2 = 2,
        SMB2_VERSION_ANY3 = 3,
        SMB2_VERSION_0202 = 0x0202,
        SMB2_VERSION_0210 = 0x0210,
        SMB2_VERSION_0300 = 0x0300,
        SMB2_VERSION_0302 = 0x0302,
        SMB2_VERSION_0311 = 0x0311
};

void smb2_set_version(struct smb2_context *smb2,
                      enum smb2_negotiate_version version);

/*
 * Set the security mode for the connection.
 * This is a combination of the flags SMB2_NEGOTIATE_SIGNING_ENABLED
 * and  SMB2_NEGOTIATE_SIGNING_REQUIRED
 * Default is 0.
 */
void smb2_set_security_mode(struct smb2_context *smb2, uint16_t security_mode);

/*
 * Set whether smb3 encryption should be used or not.
 * 0  : disable encryption. This is the default.
 * !0 : enable encryption.
 */
void smb2_set_seal(struct smb2_context *smb2, int val);

/*
 * Set whether smb2 signing should be required or not
 * 0  : do not require signing. This is the default.
 * !0 : require signing.
 */
void smb2_set_sign(struct smb2_context *smb2, int val);

/*
 * Set authentication method.
 * SMB2_SEC_UNDEFINED (use KRB if available or NTLM if not)
 * SMB2_SEC_NTLMSSP
 * SMB2_SEC_KRB5
 */
void smb2_set_authentication(struct smb2_context *smb2, int val);

/*
 * Set the username that we will try to authenticate as.
 * Default is to try to authenticate as the current user.
 */
void smb2_set_user(struct smb2_context *smb2, const char *user);
/*
 * Set the password that we will try to authenticate as.
 * This function is only needed when libsmb2 is built --without-libkrb5
 */
void smb2_set_password(struct smb2_context *smb2, const char *password);
/*
 * Set the domain when authenticating.
 * This function is only needed when libsmb2 is built --without-libkrb5
 */
void smb2_set_domain(struct smb2_context *smb2, const char *domain);
/*
 * Set the workstation when authenticating.
 * This function is only needed when libsmb2 is built --without-libkrb5
 */
void smb2_set_workstation(struct smb2_context *smb2, const char *workstation);

/*
 * Sets the address to some user defined object. May be used to make
 * additional context data available in the async callbacks.
 */
void smb2_set_opaque(struct smb2_context *smb2, void *opaque);

/*
 * Returns the opaque pointer set with smb2_set_opaque.
 */
void *smb2_get_opaque(struct smb2_context *smb2);


/*
 * Returns the client_guid for this context.
 */
const char *smb2_get_client_guid(struct smb2_context *smb2);

/*
 * Asynchronous call to connect a TCP connection to the server
 *
 * Returns:
 *  0 if the call was initiated and a connection will be attempted. Result of
 * the connection will be reported through the callback function.
 * <0 if there was an error. The callback function will not be invoked.
 *
 * Callback parameters :
 * status can be either of :
 *    0     : Connection was successful. Command_data is NULL.
 *
 *   -errno : Failed to establish the connection. Command_data is NULL.
 */
int smb2_connect_async(struct smb2_context *smb2, const char *server,
                       smb2_command_cb cb, void *cb_data);

/*
 * Async call to connect to a share.
 * On unix, if user is NULL then default to the current user.
 *
 * Returns:
 *  0 if the call was initiated and a connection will be attempted. Result of
 * the connection will be reported through the callback function.
 * -errno if there was an error. The callback function will not be invoked.
 *
 * Callback parameters :
 * status can be either of :
 *    0     : Connection was successful. Command_data is NULL.
 *
 *   -errno : Failed to connect to the share. Command_data is NULL.
 */
int smb2_connect_share_async(struct smb2_context *smb2,
                             const char *server,
                             const char *share,
                             const char *user,
                             smb2_command_cb cb, void *cb_data);

/*
 * Sync call to connect to a share.
 * On unix, if user is NULL then default to the current user.
 *
 * Returns:
 * 0      : Connected to the share successfully.
 * -errno : Failure.
 */
int smb2_connect_share(struct smb2_context *smb2,
                       const char *server,
                       const char *share,
                       const char *user);

/*
 * Async call to disconnect from a share/
 *
 * Returns:
 *  0 if the call was initiated and a connection will be attempted. Result of
 * the disconnect will be reported through the callback function.
 * -errno if there was an error. The callback function will not be invoked.
 *
 * Callback parameters :
 * status can be either of :
 *    0     : Connection was successful. Command_data is NULL.
 *
 *   -errno : Failed to disconnect the share. Command_data is NULL.
 */
int smb2_disconnect_share_async(struct smb2_context *smb2,
                                smb2_command_cb cb, void *cb_data);

/*
 * Sync call to disconnect from a share/
 *
 * Returns:
 * 0      : Disconnected from the share successfully.
 * -errno : Failure.
 */
int smb2_disconnect_share(struct smb2_context *smb2);

/*
 * This function returns a description of the last encountered error.
 */
const char *smb2_get_error(struct smb2_context *smb2);

struct smb2_url {
        const char *domain;
        const char *user;
#ifdef MXTECHS
        const char *password;
#endif
        const char *server;
        const char *share;
        const char *path;
};

/* Convert an smb2/nt error code into a string */
const char *nterror_to_str(uint32_t status);

/* Convert an smb2/nt error code into an errno value */
int nterror_to_errno(uint32_t status);

/*
 * This function is used to parse an SMB2 URL into as smb2_url structure.
 * SMB2 URL format:
 *   smb2://[<domain;][<username>@]<server>/<share>/<path>
 * where <server> has the format:
 *   <host>[:<port>].
 *
 * Function will return a pointer to an iscsi smb2 structure if successful,
 * or it will return NULL and set smb2_get_error() accordingly if there was
 * a problem with the URL.
 *
 * The returned structure is freed by calling smb2_destroy_url()
 */
struct smb2_url *smb2_parse_url(struct smb2_context *smb2, const char *url);
void smb2_destroy_url(struct smb2_url *url);

struct smb2_pdu;
/*
 * The functions are used when creating compound low level commands.
 * The general pattern for compound chains is
 * 1, pdu = smb2_cmd_*_async(smb2, ...)
 *
 * 2, next = smb2_cmd_*_async(smb2, ...)
 * 3, smb2_add_compound_pdu(smb2, pdu, next);
 *
 * 4, next = smb2_cmd_*_async(smb2, ...)
 * 5, smb2_add_compound_pdu(smb2, pdu, next);
 * ...
 * *, smb2_queue_pdu(smb2, pdu);
 *
 * See libsmb2.c and smb2-raw-stat-async.c for examples on how to use
 * this interface.
 */
void smb2_add_compound_pdu(struct smb2_context *smb2,
                           struct smb2_pdu *pdu, struct smb2_pdu *next_pdu);
void smb2_free_pdu(struct smb2_context *smb2, struct smb2_pdu *pdu);
void smb2_queue_pdu(struct smb2_context *smb2, struct smb2_pdu *pdu);

/*
 * OPENDIR
 */
struct smb2dir;
/*
 * Async opendir()
 *
 * Returns
 *  0 : The operation was initiated. Result of the operation will be reported
 * through the callback function.
 * <0 : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 *          Command_data is struct smb2dir.
 *          This structure is freed using smb2_closedir().
 * -errno : An error occured.
 *          Command_data is NULL.
 */
int smb2_opendir_async(struct smb2_context *smb2, const char *path,
                       smb2_command_cb cb, void *cb_data);

/*
 * Sync opendir()
 *
 * Returns NULL on failure.
 */
struct smb2dir *smb2_opendir(struct smb2_context *smb2, const char *path);

/*
 * closedir()
 */
/*
 * smb2_closedir() never blocks, thus no async version is needed.
 */
void smb2_closedir(struct smb2_context *smb2, struct smb2dir *smb2dir);

/*
 * readdir()
 */
/*
 * smb2_readdir() never blocks, thus no async version is needed.
 */
struct smb2dirent *smb2_readdir(struct smb2_context *smb2,
                                struct smb2dir *smb2dir);

/*
 * rewinddir()
 */
/*
 * smb2_rewinddir() never blocks, thus no async version is needed.
 */
void smb2_rewinddir(struct smb2_context *smb2, struct smb2dir *smb2dir);

/*
 * telldir()
 */
/*
 * smb2_telldir() never blocks, thus no async version is needed.
 */
long smb2_telldir(struct smb2_context *smb2, struct smb2dir *smb2dir);

/*
 * seekdir()
 */
/*
 * smb2_seekdir() never blocks, thus no async version is needed.
 */
void smb2_seekdir(struct smb2_context *smb2, struct smb2dir *smb2dir,
                  long loc);

/*
 * OPEN
 */
struct smb2fh;
/*
 * Async open()
 *
 * Opens or creates a file.
 * Supported flags are:
 * O_RDONLY
 * O_WRONLY
 * O_RDWR
 * O_SYNC
 * O_CREAT
 * O_EXCL
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 *          Command_data is struct smb2fh.
 *          This structure is freed using smb2_close().
 * -errno : An error occured.
 *          Command_data is NULL.
 */
int smb2_open_async(struct smb2_context *smb2, const char *path, int flags,
                    smb2_command_cb cb, void *cb_data);

/*
 * Sync open()
 *
 * Returns NULL on failure.
 */
struct smb2fh *smb2_open(struct smb2_context *smb2, const char *path, int flags);

/*
 * CLOSE
 */
/*
 * Async close()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 *
 * Command_data is always NULL.
 */
int smb2_close_async(struct smb2_context *smb2, struct smb2fh *fh,
                     smb2_command_cb cb, void *cb_data);

/*
 * Sync close()
 */
int smb2_close(struct smb2_context *smb2, struct smb2fh *fh);

/*
 * FSYNC
 */
/*
 * Async fsync()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 *
 * Command_data is always NULL.
 */
int smb2_fsync_async(struct smb2_context *smb2, struct smb2fh *fh,
                     smb2_command_cb cb, void *cb_data);

/*
 * Sync fsync()
 */
int smb2_fsync(struct smb2_context *smb2, struct smb2fh *fh);

/*
 * GetMaxReadWriteSize
 * SMB2 servers have a maximum size for read/write data that they support.
 */
uint32_t smb2_get_max_read_size(struct smb2_context *smb2);
uint32_t smb2_get_max_write_size(struct smb2_context *smb2);

struct smb2_read_cb_data {
        struct smb2fh *fh;
        uint8_t *buf;
        uint32_t count;
        uint64_t offset;
};

struct smb2_write_cb_data {
        struct smb2fh *fh;
        const uint8_t *buf;
        uint32_t count;
        uint64_t offset;
};

/*
 * PREAD
 */
/*
 * Async pread()
 * Use smb2_get_max_read_size to discover the maximum data size that the
 * server supports.
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *    >=0 : Number of bytes read.
 * -errno : An error occured.
 *
 * Command_data is struct smb2_read_cb_data, which holds the arguments
 * that were given to smb2_pread_async.
 * This structure is automatically freed.
 */
int smb2_pread_async(struct smb2_context *smb2, struct smb2fh *fh,
                     uint8_t *buf, uint32_t count, uint64_t offset,
                     smb2_command_cb cb, void *cb_data);

/*
 * Sync pread()
 * Use smb2_get_max_read_size to discover the maximum data size that the
 * server supports.
 */
int smb2_pread(struct smb2_context *smb2, struct smb2fh *fh,
               uint8_t *buf, uint32_t count, uint64_t offset);

/*
 * PWRITE
 */
/*
 * Async pwrite()
 * Use smb2_get_max_write_size to discover the maximum data size that the
 * server supports.
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *    >=0 : Number of bytes written.
 * -errno : An error occured.
 *
 * Command_data is struct smb2_write_cb_data, which holds the arguments
 * that were given to smb2_pwrite_async.
 * This structure is automatically freed.
 */
int smb2_pwrite_async(struct smb2_context *smb2, struct smb2fh *fh,
                      const uint8_t *buf, uint32_t count, uint64_t offset,
                      smb2_command_cb cb, void *cb_data);

/*
 * Sync pwrite()
 * Use smb2_get_max_write_size to discover the maximum data size that the
 * server supports.
 */
int smb2_pwrite(struct smb2_context *smb2, struct smb2fh *fh,
                const uint8_t *buf, uint32_t count, uint64_t offset);

/*
 * READ
 */
/*
 * Async read()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *    >=0 : Number of bytes read.
 * -errno : An error occured.
 *
 * Command_data is struct smb2_read_cb_data, which holds the arguments
 * that were given to smb2_read_async. offset denotes the offset in the file
 * at which the read took place.
 * This structure is automatically freed.
 */
int smb2_read_async(struct smb2_context *smb2, struct smb2fh *fh,
                    uint8_t *buf, uint32_t count,
                    smb2_command_cb cb, void *cb_data);

/*
 * Sync read()
 */
int smb2_read(struct smb2_context *smb2, struct smb2fh *fh,
              uint8_t *buf, uint32_t count);

/*
 * WRITE
 */
/*
 * Async write()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *    >=0 : Number of bytes written.
 * -errno : An error occured.
 *
 * Command_data is struct smb2_write_cb_data, which holds the arguments
 * that were given to smb2_write_async. offset denotes the offset in the file
 * at which the write took place.
 * This structure is automatically freed.
 */
int smb2_write_async(struct smb2_context *smb2, struct smb2fh *fh,
                     const uint8_t *buf, uint32_t count,
                     smb2_command_cb cb, void *cb_data);

/*
 * Sync write()
 */
int smb2_write(struct smb2_context *smb2, struct smb2fh *fh,
               const uint8_t *buf, uint32_t count);

/*
 * Sync lseek()
 */
/*
 * smb2_seek() SEEK_SET and SEEK_CUR are fully supported.
 * SEEK_END only returns the end-of-file from the original open.
 * (it will not call fstat to discover the current file size and will not block)
 */
int64_t smb2_lseek(struct smb2_context *smb2, struct smb2fh *fh,
                   int64_t offset, int whence, uint64_t *current_offset);

/*
 * UNLINK
 */
/*
 * Async unlink()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 *
 * Command_data is always NULL.
 */
int smb2_unlink_async(struct smb2_context *smb2, const char *path,
                      smb2_command_cb cb, void *cb_data);

/*
 * Sync unlink()
 */
int smb2_unlink(struct smb2_context *smb2, const char *path);

/*
 * RMDIR
 */
/*
 * Async rmdir()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 *
 * Command_data is always NULL.
 */
int smb2_rmdir_async(struct smb2_context *smb2, const char *path,
                     smb2_command_cb cb, void *cb_data);

/*
 * Sync rmdir()
 */
int smb2_rmdir(struct smb2_context *smb2, const char *path);

/*
 * MKDIR
 */
/*
 * Async mkdir()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 *
 * Command_data is always NULL.
 */
int smb2_mkdir_async(struct smb2_context *smb2, const char *path,
                     smb2_command_cb cb, void *cb_data);

/*
 * Sync mkdir()
 */
int smb2_mkdir(struct smb2_context *smb2, const char *path);

/*
 * STATVFS
 */
/*
 * Async statvfs()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success. Command_data is struct smb2_statvfs
 * -errno : An error occured.
 */
int smb2_statvfs_async(struct smb2_context *smb2, const char *path,
                       struct smb2_statvfs *statvfs,
                       smb2_command_cb cb, void *cb_data);
/*
 * Sync statvfs()
 */
int smb2_statvfs(struct smb2_context *smb2, const char *path,
                 struct smb2_statvfs *statvfs);

/*
 * FSTAT
 */
/*
 * Async fstat()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success. Command_data is struct smb2_stat_64
 * -errno : An error occured.
 */
int smb2_fstat_async(struct smb2_context *smb2, struct smb2fh *fh,
                     struct smb2_stat_64 *st,
                     smb2_command_cb cb, void *cb_data);
/*
 * Sync fstat()
 */
int smb2_fstat(struct smb2_context *smb2, struct smb2fh *fh,
               struct smb2_stat_64 *st);

/*
 * Async stat()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success. Command_data is struct smb2_stat_64
 * -errno : An error occured.
 */
int smb2_stat_async(struct smb2_context *smb2, const char *path,
                    struct smb2_stat_64 *st,
                    smb2_command_cb cb, void *cb_data);
/*
 * Sync stat()
 */
int smb2_stat(struct smb2_context *smb2, const char *path,
              struct smb2_stat_64 *st);

/*
 * Async rename()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 */
int smb2_rename_async(struct smb2_context *smb2, const char *oldpath,
                      const char *newpath, smb2_command_cb cb, void *cb_data);

/*
 * Sync rename()
 */
int smb2_rename(struct smb2_context *smb2, const char *oldpath,
              const char *newpath);

/*
 * Async truncate()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 */
int smb2_truncate_async(struct smb2_context *smb2, const char *path,
                        uint64_t length, smb2_command_cb cb, void *cb_data);
/*
 * Sync truncate()
 * Function returns
 *      0 : Success
 * -errno : An error occured.
 */
int smb2_truncate(struct smb2_context *smb2, const char *path,
                  uint64_t length);

/*
 * Async ftruncate()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 */
int smb2_ftruncate_async(struct smb2_context *smb2, struct smb2fh *fh,
                         uint64_t length, smb2_command_cb cb, void *cb_data);
/*
 * Sync ftruncate()
 * Function returns
 *      0 : Success
 * -errno : An error occured.
 */
int smb2_ftruncate(struct smb2_context *smb2, struct smb2fh *fh,
                   uint64_t length);


/*
 * READLINK
 */
/*
 * Async readlink()
 *
 * Returns
 *  0     : The operation was initiated. The link content will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success. Command_data is the link content.
 * -errno : An error occured.
 */
int smb2_readlink_async(struct smb2_context *smb2, const char *path,
                        smb2_command_cb cb, void *cb_data);

/*
 * Sync readlink()
 */
int smb2_readlink(struct smb2_context *smb2, const char *path, char *buf, uint32_t bufsiz);

/*
 * Async echo()
 *
 * Returns
 *  0     : The operation was initiated. Result of the operation will be
 *          reported through the callback function.
 * -errno : There was an error. The callback function will not be invoked.
 *
 * When the callback is invoked, status indicates the result:
 *      0 : Success.
 * -errno : An error occured.
 */
int smb2_echo_async(struct smb2_context *smb2,
                    smb2_command_cb cb, void *cb_data);

/*
 * Sync echo()
 *
 * Returns:
 * 0      : successfully send the message and received a reply.
 * -errno : Failure.
 */
int smb2_echo(struct smb2_context *smb2);

/*
 * Some symbols have moved over to a different header file to allow better
 * separation between dcerpc and smb2, so we need to include this header
 * here to retain compatibility for apps that depend on those symbols.
 */
#include <smb2/libsmb2-dcerpc-srvsvc.h>

#ifdef __cplusplus
}
#endif
#endif /* !_LIBSMB2_H_ */
