/*
 * Copyright (C) 2011-2017 Red Hat, Inc.
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

#ifdef __cplusplus
extern "C" {
#endif

/** @file libstoragemgmt_error.h */

/** \enum lsm_error_number Possible enumerated return codes from library */
typedef enum {
    /** OK */
    LSM_ERR_OK = 0,
    /** Library BUG */
    LSM_ERR_LIB_BUG = 1,
    /** Plugin BUG */
    LSM_ERR_PLUGIN_BUG = 2,
    /** Operation has started */
    LSM_ERR_JOB_STARTED = 7,
    /** Plug-in is un-responsive */
    LSM_ERR_TIMEOUT = 11,
    /** Daemon is not running */
    LSM_ERR_DAEMON_NOT_RUNNING = 12,

    /** Permission denied. Only for library level function. */
    LSM_ERR_PERMISSION_DENIED = 13,

    /** Name exists */
    LSM_ERR_NAME_CONFLICT = 50,

    /** Initiator exists in another access group */
    LSM_ERR_EXISTS_INITIATOR = 52,

    /** Precondition checks failed */
    LSM_ERR_INVALID_ARGUMENT = 101,

    /** Operation completed with no change in array state */
    LSM_ERR_NO_STATE_CHANGE = 125,

    /** Host on network, but not allowing connection */
    LSM_ERR_NETWORK_CONNREFUSED = 140,
    /** Host unreachable on network */
    LSM_ERR_NETWORK_HOSTDOWN = 141,
    /** Generic network error */
    LSM_ERR_NETWORK_ERROR = 142,

    /** Memory allocation failure */
    LSM_ERR_NO_MEMORY = 152,
    /** Feature not supported */
    LSM_ERR_NO_SUPPORT = 153,

    /** Volume masked to Access Group*/
    LSM_ERR_IS_MASKED = 160,

    /** Volume/File system is replication source */
    LSM_ERR_HAS_CHILD_DEPENDENCY = 161,

    /** Specified access group not found */
    LSM_ERR_NOT_FOUND_ACCESS_GROUP = 200,

    /** Specified FS not found */
    LSM_ERR_NOT_FOUND_FS = 201,
    /** Specified JOB not found */
    LSM_ERR_NOT_FOUND_JOB = 202,
    /** Specified POOL not found */
    LSM_ERR_NOT_FOUND_POOL = 203,
    /** Specified snap shot not found */
    LSM_ERR_NOT_FOUND_FS_SS = 204,
    /** Specified volume not found */
    LSM_ERR_NOT_FOUND_VOLUME = 205,
    /** NFS export not found */
    LSM_ERR_NOT_FOUND_NFS_EXPORT = 206,

    /** System not found */
    LSM_ERR_NOT_FOUND_SYSTEM = 208,
    /** Disk not found */
    LSM_ERR_NOT_FOUND_DISK = 209,
    /** Need license for feature */
    LSM_ERR_NOT_LICENSED = 226,

    /** Take offline before performing operation */
    LSM_ERR_NO_SUPPORT_ONLINE_CHANGE = 250,
    /** Needs to be online to perform operation */
    LSM_ERR_NO_SUPPORT_OFFLINE_CHANGE = 251,

    /** Authorization failed */
    LSM_ERR_PLUGIN_AUTH_FAILED = 300,

    /** Inter-process communication between client &
        out of process plug-in encountered connection errors. */
    LSM_ERR_PLUGIN_IPC_FAIL = 301,

    /** Incorrect permission on UNIX domain socket used for IPC */
    LSM_ERR_PLUGIN_SOCKET_PERMISSION = 307,

    /** Plug-in does not appear to exist */
    LSM_ERR_PLUGIN_NOT_EXIST = 311,

    /** Insufficient space */
    LSM_ERR_NOT_ENOUGH_SPACE = 350,

    /** Error comunicating with plug-in */
    LSM_ERR_TRANSPORT_COMMUNICATION = 400,
    /** Transport serialization error */
    LSM_ERR_TRANSPORT_SERIALIZATION = 401,
    /** Parameter transported over IPC is invalid */
    LSM_ERR_TRANSPORT_INVALID_ARG = 402,

    LSM_ERR_LAST_INIT_IN_ACCESS_GROUP = 502,

    /** Unsupport search key */
    LSM_ERR_UNSUPPORTED_SEARCH_KEY = 510,

    LSM_ERR_EMPTY_ACCESS_GROUP = 511,
    LSM_ERR_POOL_NOT_READY = 512,
    LSM_ERR_DISK_NOT_FREE = 513,

} lsm_error_number;

typedef struct _lsm_error lsm_error;
typedef lsm_error *lsm_error_ptr;

/**
 * lsm_error_last_get - Retrieves the last error of the lsm connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the last error of the lsm connection.
 *      Note: Address returned is valid until lsm_connect gets freed, copy
 *      return value if you need longer scope. Do not free returned pointer.
 *
 * @conn:
 *      lsm_connect pointer.
 *
 * Return:
 *      lsm_error_ptr. NULL if argument 'c' is NULL or not a valid lsm_connect
 *      pointer or no error exists.
 */
lsm_error_ptr LSM_DLL_EXPORT lsm_error_last_get(lsm_connect *conn);

/**
 * lsm_error_free - Free the lsm_error memory.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for the specified lsm_error.
 *
 * @err:
 *      Record to release
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_error_ptr.
 *
 */
int LSM_DLL_EXPORT lsm_error_free(lsm_error_ptr err);

/**
 * lsm_error_number_get - Retrieves the error number.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the error number of the specified lsm_error.
 *
 * @e:
 *      The lsm_error to retrieves error number from.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *      -1 if invalid lsm_error_ptr.
 */
lsm_error_number LSM_DLL_EXPORT lsm_error_number_get(lsm_error_ptr e);

/**
 * lsm_error_message_get - Retrieves the error message for the lsm_error.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the error message for the specified lsm_error.
 *      Note: Address returned is valid until lsm_error gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @e:
 *      The lsm_error to retrieves error message from.
 *
 * Return:
 *      string. NULL if argument 'e' is NULL or not a valid lsm_error_ptr.
 */
char LSM_DLL_EXPORT *lsm_error_message_get(lsm_error_ptr e);

/**
 * lsm_error_exception_get - Retrieves the exception message from the error.
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the exception message for the specified lsm_error.
 *      Note: Address returned is valid until lsm_error gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @e:
 *      The lsm_error to retrieves exception message from.
 *
 * Return:
 *      string. NULL if argument 'e' is NULL or not a valid lsm_error_ptr.
 */
char LSM_DLL_EXPORT *lsm_error_exception_get(lsm_error_ptr e);

/**
 * lsm_error_debug_get - Retrieves the debug message from the error.
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the debug message for the specified lsm_error.
 *      Note: Address returned is valid until lsm_error gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @e:
 *      The lsm_error to retrieves debug message from.
 *
 * Return:
 *      string. NULL if argument 'e' is NULL or not a valid lsm_error_ptr.
 */
char LSM_DLL_EXPORT *lsm_error_debug_get(lsm_error_ptr e);

/**
 * lsm_error_debug_data_get - Retrieves the debug data from the error.
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the debug data for the specified lsm_error.
 *      Note: Address returned is valid until lsm_error gets freed, copy return
 *      value if you need longer scope. Do not free returned pointer.
 *
 * @e:
 *      The lsm_error to retrieves debug data from.
 * @size:
 *      uint32_t pointer. The output pointer for debug data size.
 *
 * Return:
 *      void *. NULL if argument 'e' is NULL or not a valid lsm_error_ptr.
 */
void LSM_DLL_EXPORT *lsm_error_debug_data_get(lsm_error_ptr e, uint32_t *size);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMTERROR_H */
