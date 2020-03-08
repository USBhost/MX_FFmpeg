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

#ifndef UINT64_MAX
#include <stdint.h>
#endif

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
	uint32_t	f_bsize;
	uint32_t	f_frsize;
	uint64_t	f_blocks;
	uint64_t	f_bfree;
	uint64_t	f_bavail;
	uint32_t	f_files;
	uint32_t	f_ffree;
	uint32_t	f_favail;
	uint32_t	f_fsid;
	uint32_t	f_flag;
	uint32_t	f_namemax;
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
 * The following three functions are used to integrate libsmb2 in an event
 * system.
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
 * Command_data is always NULL.
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
 * Command_data is always NULL.
 */       
int smb2_pwrite_async(struct smb2_context *smb2, struct smb2fh *fh,
                      uint8_t *buf, uint32_t count, uint64_t offset,
                      smb2_command_cb cb, void *cb_data);

/*
 * Sync pwrite()
 * Use smb2_get_max_write_size to discover the maximum data size that the
 * server supports.
 */
int smb2_pwrite(struct smb2_context *smb2, struct smb2fh *fh,
                uint8_t *buf, uint32_t count, uint64_t offset);

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
 * Command_data is always NULL.
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
 * Command_data is always NULL.
 */
int smb2_write_async(struct smb2_context *smb2, struct smb2fh *fh,
                     uint8_t *buf, uint32_t count,
                     smb2_command_cb cb, void *cb_data);

/*
 * Sync write()
 */
int smb2_write(struct smb2_context *smb2, struct smb2fh *fh,
               uint8_t *buf, uint32_t count);

/*
 * Sync lseek()
 */
/*
 * smb2_seek() never blocks, thus no async version is needed.
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

#ifdef __cplusplus
}
#endif

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
        uint32_t level;
        struct srvsvc_netsharectr *ctr;
        uint32_t total_entries;
        uint32_t resume_handle;

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

#endif /* !_LIBSMB2_H_ */
