/*
 * Copyright (C) 2011-2014 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMTERROR_H
#define LIBSTORAGEMGMTERROR_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/** @file libstoragemgmt_error.h */



/**< \enum lsm_error_number Possible enumerated return codes from library */
typedef enum {
    LSM_ERR_OK = 0,
    /**^ OK */
    LSM_ERR_LIB_BUG = 1,
    /**^ Library BUG */
    LSM_ERR_PLUGIN_BUG = 2,
    /**^ Plugin BUG */
    LSM_ERR_JOB_STARTED = 7,
    /**^ Operation has started */
    LSM_ERR_TIMEOUT = 11,
    /**^ Plug-in is un-responsive */
    LSM_ERR_DAEMON_NOT_RUNNING = 12,
    /**^ Daemon is not running */

    LSM_ERR_NAME_CONFLICT = 50,
    /**^ Name exists */
    LSM_ERR_EXISTS_INITIATOR = 52,
    /**^ Initiator exists in another access group */

    LSM_ERR_INVALID_ARGUMENT = 101,
    /**^ Precondition checks failed */

    LSM_ERR_NO_STATE_CHANGE = 125,
    /**^ Operation completed with no change in array state */

    LSM_ERR_NETWORK_CONNREFUSED = 140,

    /**^ Host on network, but not allowing connection */
    LSM_ERR_NETWORK_HOSTDOWN = 141,
    /**^ Host unreachable on network */
    LSM_ERR_NETWORK_ERROR = 142,
    /**^ Generic network error */

    LSM_ERR_NO_MEMORY = 152,
    /**^ Memory allocation failure */
    LSM_ERR_NO_SUPPORT = 153,
    /**^ Feature not supported */

    LSM_ERR_IS_MASKED = 160,
    /**^ Volume masked to Access Group*/

    LSM_ERR_NOT_FOUND_ACCESS_GROUP = 200,

    /**^ Specified access group not found */
    LSM_ERR_NOT_FOUND_FS = 201,
    /**^ Specified FS not found */
    LSM_ERR_NOT_FOUND_JOB = 202,
    /**^ Specified JOB not found */
    LSM_ERR_NOT_FOUND_POOL = 203,
    /**^ Specified POOL not found */
    LSM_ERR_NOT_FOUND_FS_SS = 204,
    /**^ Specified snap shot not found */
    LSM_ERR_NOT_FOUND_VOLUME = 205,
    /**^ Specified volume not found */
    LSM_ERR_NOT_FOUND_NFS_EXPORT = 206,

    /**^ NFS export not found */
    LSM_ERR_NOT_FOUND_SYSTEM = 208,
    /**^ System not found */
    LSM_ERR_NOT_FOUND_DISK = 209,

    LSM_ERR_NOT_LICENSED = 226,
    /**^ Need license for feature */

    LSM_ERR_NO_SUPPORT_ONLINE_CHANGE = 250,
    /**^ Take offline before performing operation */
    LSM_ERR_NO_SUPPORT_OFFLINE_CHANGE = 251,
    /**^ Needs to be online to perform operation */

    LSM_ERR_PLUGIN_AUTH_FAILED = 300,

    /**^ Authorization failed */
    LSM_ERR_PLUGIN_IPC_FAIL = 301,
    /**^ Inter-process communication between client &
      out of process plug-in encountered connection errors.**/

    LSM_ERR_PLUGIN_SOCKET_PERMISSION = 307,

    /**^ Incorrect permission on UNIX domain socket used for IPC */
    LSM_ERR_PLUGIN_NOT_EXIST = 311,
    /**^ Plug-in does not appear to exist */

    LSM_ERR_NOT_ENOUGH_SPACE = 350,
    /**^ Insufficient space */

    LSM_ERR_TRANSPORT_COMMUNICATION = 400,
    /**^ Error comunicating with plug-in */
    LSM_ERR_TRANSPORT_SERIALIZATION = 401,
    /**^ Transport serialization error */
    LSM_ERR_TRANSPORT_INVALID_ARG = 402,
    /**^ Parameter transported over IPC is invalid */

    LSM_ERR_LAST_INIT_IN_ACCESS_GROUP = 502,


    LSM_ERR_UNSUPPORTED_SEARCH_KEY = 510,
    /**^ Unsupport search key */

    LSM_ERR_EMPTY_ACCESS_GROUP = 511,
    LSM_ERR_POOL_NOT_READY = 512,
    LSM_ERR_DISK_NOT_FREE = 513,

} lsm_error_number;

typedef struct _lsm_error lsm_error;
typedef lsm_error *lsm_error_ptr;

/**
 * Gets the last error structure
 * Note: @see lsm_error_free to release memory
 * @param c      Connection pointer.
 * @return Error pointer, Null if no error exists!
 */
lsm_error_ptr LSM_DLL_EXPORT lsm_error_last_get(lsm_connect * c);

/**
 * Frees the error record!
 * @param err   The error to free!
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_error_free(lsm_error_ptr err);

/**
 * Retrieves the error number from the error.
 * @param e     The lsm_error_ptr
 * @return -1 if e is not a valid error pointer, else error number.
 */
lsm_error_number LSM_DLL_EXPORT lsm_error_number_get(lsm_error_ptr e);

/**
 * Retrieves the error message from the error.
 * Note: The returned value is only valid as long as the e is valid, in
 * addition the function will return NULL if e is invalid.  To remove the
 * ambiguity call lsm_error_number_get and check return code.
 * @param e     The lsm_error_ptr
 * @return NULL if message data does not exist, else error message.
 */
char LSM_DLL_EXPORT *lsm_error_message_get(lsm_error_ptr e);

/**
 * Retrieves the exception message from the error.
 * Note: The returned value is only valid as long as the e is valid, in
 * addition the function will return NULL if e is invalid.  To remove the
 * ambiguity call lsm_error_number_get and check return code.
 * @param e     The lsm_error_ptr
 * @return NULL if exception does not exist, else error exception.
 */
char LSM_DLL_EXPORT *lsm_error_exception_get(lsm_error_ptr e);

/**
 * Retrieves the error message from the error.
 * Note: The returned value is only valid as long as the e is valid, in
 * addition the function will return NULL if e is invalid.  To remove the
 * ambiguity call lsm_error_number_get and check return code.
 * @param e     The lsm_error_ptr
 * @return NULL if does not exist, else debug message.
 */
char LSM_DLL_EXPORT *lsm_error_debug_get(lsm_error_ptr e);

/**
 * Retrieves the debug data from the error.
 * Note: The returned value is only valid as long as the e is valid, in
 * addition the function will return NULL if e is invalid.  To remove the
 * ambiguity call lsm_error_number_get and check return code.
 * @param e             The lsm_error_ptr
 * @param[out] size     Number of bytes of data returned.
 * @return NULL if does not exist, else debug message.
 */
void LSM_DLL_EXPORT *lsm_error_debug_data_get(lsm_error_ptr e,
                                              uint32_t * size);

#ifdef  __cplusplus
}
#endif
#endif                          /* LIBSTORAGEMGMTERROR_H */
