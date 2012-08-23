/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMTERROR_H
#define LIBSTORAGEMGMTERROR_H

#include <stdlib.h>
#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Severity of the error.
 */
typedef enum  {
    LSM_ERR_LEVEL_NONE = 0,
    LSM_ERR_LEVEL_WARNING = 1,
    LSM_ERR_LEVEL_ERROR = 2
} lsmErrorLevel;

/*
 * Where the error took place
 */
typedef enum  {
    LSM_ERR_DOMAIN_FRAME_WORK = 0,
    LSM_ERR_DOMAIN_PLUG_IN = 1
} lsmErrorDomain;

/**
 * Possible enumerated return codes from library
 */
typedef enum {
    LSM_ERR_OK = 0,                     /**< OK */
    LSM_ERR_INTERNAL_ERROR = 1,         /**< Internal error */
    LSM_ERR_JOB_STARTED = 7,            /**< Operation has started */
    LSM_ERR_INDEX_BOUNDS = 10,          /**< Out of bounds on string index */
    LSM_ERR_TIMEOUT = 11,               /**< Plug-in is un-responsive */

    LSM_ERR_EXISTS_ACCESS_GROUP = 50,   /**< Access group exists */
    LSM_ERR_EXISTS_FS = 51,             /**< FS exists */
    LSM_ERR_EXISTS_INITIATOR = 52,      /**< Initiator exists */
    LSM_ERR_EXISTS_NAME = 53,           /**< Named item already exists */
    LSM_ERR_FS_NOT_EXPORTED = 54,       /**< FS not nfs exported */
    LSM_ERR_INITIATOR_NOT_IN_ACCESS_GROUP = 55, /**< Initiator not in access group */

    LSM_ERR_INVALID_ACCESS_GROUP = 100, /**< Invalid access group */
    LSM_ERR_INVALID_ARGUMENT = 101,     /**< Precondition checks failed */
    LSM_ERR_INVALID_CONN = 102,         /**< Connection structure is invalid */
    LSM_ERR_INVALID_ERR = 103,          /**< Invalid error structure */
    LSM_ERR_INVALID_FS = 104,           /**< invalid fs */
    LSM_ERR_INVALID_INIT = 105,         /**< Invalid initiator structure */
    LSM_ERR_INVALID_JOB = 106,          /**< Invalid job number */
    LSM_ERR_INVALID_NAME = 107,         /**< Name specified is invalid */
    LSM_ERR_INVALID_NFS = 108,          /**< invalid nfs export record */
    LSM_ERR_INVALID_PLUGIN = 109,       /**< Invalid plugin structure */
    LSM_ERR_INVALID_POOL = 110,         /**< Invalid pool pointer */
    LSM_ERR_INVALID_SL = 111,           /**< Invalid string list */
    LSM_ERR_INVALID_SS = 112,           /**< Invalid snapshot */
    LSM_ERR_INVALID_URI = 113,          /**< Invalid uri */
    LSM_ERR_INVALID_VAL = 114,          /**< Invalid value */
    LSM_ERR_INVALID_VOL = 115,          /**< Invalid volume pointer */
    LSM_ERR_INVALID_CAPABILITY = 116,   /**< Invalid capability pointer */
    LSM_ERR_INVALID_SYSTEM = 117,       /**< Invalid system pointer */
    LSM_ERR_INVALID_IQN = 118,          /**< Invalid IQN */

    LSM_ERR_IS_MAPPED = 125,            /**< Mapping already exists */

    LSM_ERR_NO_CONNECT = 150,           /**< Unable to connect to host */
    LSM_ERR_NO_MAPPING = 151,           /**< There is no access for initiator and volume */
    LSM_ERR_NO_MEMORY = 152,            /**< Memory allocation failure */
    LSM_ERR_NO_SUPPORT = 153,           /**< Feature not supported */

    LSM_ERR_NOT_FOUND_ACCESS_GROUP = 200,   /**< Specified access group not found */
    LSM_ERR_NOT_FOUND_FS = 201,         /**< Specified FS not found */
    LSM_ERR_NOT_FOUND_JOB = 202,        /**< Specified JOB not found */
    LSM_ERR_NOT_FOUND_POOL = 203,       /**< Specified POOL not found */
    LSM_ERR_NOT_FOUND_SS = 204,         /**< Specified snap shot not found */
    LSM_ERR_NOT_FOUND_VOLUME = 205,     /**< Specified volume not found */
    LSM_ERR_NOT_FOUND_NFS_EXPORT = 206, /**< NFS export not found */
    LSM_ERR_NOT_FOUND_INITIATOR = 207,  /**< Initiator not found */

    LSM_ERR_NOT_IMPLEMENTED = 225,      /**< Feature not implemented */
    LSM_ERR_NOT_LICENSED = 226,         /**< Need license for feature */

    LSM_ERR_OFF_LINE = 250,             /**< Specified element is off line */
    LSM_ERR_ON_LINE = 251,              /**< Specified element is on line */

    LSM_ERR_PLUGIN_AUTH_FAILED = 300,   /**< Authorization failed */
    LSM_ERR_PLUGIN_DLOPEN = 301,        /**< dlopen on plugin failed */
    LSM_ERR_PLUGIN_DLSYM = 302,         /**< Required symbols in plugin missing */
    LSM_ERR_PLUGIN_ERROR = 303,         /**< Non-descript plugin error */
    LSM_ERR_PLUGIN_MISSING_HOST = 304,  /**< Missing or invalid hostname */
    LSM_ERR_PLUGIN_MISSING_NS = 305,    /**< Missing namespace */
    LSM_ERR_PLUGIN_MISSING_PORT = 306,  /**< Missing port */
    LSM_ERR_PLUGIN_PERMISSIONS = 307,   /**< Unable to access plugin */
    LSM_ERR_PLUGIN_REGISTRATION = 308,  /**< Error during plug-in registration */
    LSM_ERR_PLUGIN_UNKNOWN_HOST = 309,  /**< Name resolution failed */
    LSM_ERR_PLUGIN_TIMEOUT = 310,       /**< Plug-in timed out talking to array */

    LSM_ERR_SIZE_INSUFFICIENT_SPACE = 350,  /**< Insufficient space */
    LSM_ERR_VOLUME_SAME_SIZE = 351,         /**< Trying to resize to same size */
    LSM_ERR_SIZE_TOO_LARGE = 352,           /**< Size specified is too large */
    LSM_ERR_SIZE_TOO_SMALL = 353,           /**< Size specified is too small */
    LSM_ERR_SIZE_LIMIT_REACHED = 354,       /**< Limit has been reached */

    LSM_ERR_TRANSPORT_COMMUNICATION = 400,    /**< Error comunicating with plug-in */
    LSM_ERR_TRANSPORT_SERIALIZATION = 401,   /**< Transport serialization error */
    LSM_ERR_TRANSPORT_INVALID_ARG = 402,        /**< Parameter transported over IPC is invalid */

    LSM_ERR_UNSUPPORTED_INITIATOR_TYPE = 450,   /**< Unsupported initiator type */
    LSM_ERR_UNSUPPORTED_PROVISIONING = 451,     /**< Unsupported provisioning */
    LSM_ERR_UNSUPPORTED_REPLICATION_TYPE = 452  /**< Unsupported replication type */

} lsmErrorNumber;

typedef struct _lsmError lsmError;
typedef lsmError *lsmErrorPtr;

/**
 * Gets the last error structure
 * Note: @see lsmErrorFree to release memory
 * @param c      Connection pointer.
 * @return lsmErrorPtr, Null if no error exists!
 */
lsmErrorPtr LSM_DLL_EXPORT lsmErrorGetLast(lsmConnectPtr c);

/**
 * Frees the error record!
 * @param err   The error to free!
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsmErrorFree(lsmErrorPtr err);

/**
 * Retrieves the error number from the error.
 * @param e     The lsmErrorPtr
 * @return -1 if e is not a valid error pointer, else error number.
 */
lsmErrorNumber LSM_DLL_EXPORT lsmErrorGetNumber(lsmErrorPtr e);

/**
 * Retrieves the domain from the error.
 * @param e     The lsmErrorPtr
 * @return -1 if e is not a valid error pointer, else error domain value.
 */
lsmErrorDomain LSM_DLL_EXPORT lsmErrorGetDomain(lsmErrorPtr e);

/**
 * Retrieves the error level from the error.
 * @param e     The lsmErrorPtr
 * @return -1 if e is not a valid error pointer, else error level.
 */
lsmErrorLevel LSM_DLL_EXPORT lsmErrorGetLevel(lsmErrorPtr e);

/**
 * Retrieves the error message from the error.
 * Note: The returned value is only valid as long as the e is valid, in addition
 * the function will return NULL if e is invalid.  To remove the ambiguity call
 * lsmErrorGetNumber and check return code.
 * @param e     The lsmErrorPtr
 * @return NULL if message data does not exist, else error message.
 */
char LSM_DLL_EXPORT *lsmErrorGetMessage(lsmErrorPtr e);

/**
 * Retrieves the exception message from the error.
 * Note: The returned value is only valid as long as the e is valid, in addition
 * the function will return NULL if e is invalid.  To remove the ambiguity call
 * lsmErrorGetNumber and check return code.
 * @param e     The lsmErrorPtr
 * @return NULL if exception does not exist, else error exception.
 */
char LSM_DLL_EXPORT *lsmErrorGetException(lsmErrorPtr e);

/**
 * Retrieves the error message from the error.
 * Note: The returned value is only valid as long as the e is valid, in addition
 * the function will return NULL if e is invalid.  To remove the ambiguity call
 * lsmErrorGetNumber and check return code.
 * @param e     The lsmErrorPtr
 * @return NULL if does not exist, else debug message.
 */
char LSM_DLL_EXPORT *lsmErrorGetDebug(lsmErrorPtr e);

/**
 * Retrieves the debug data from the error.
 * Note: The returned value is only valid as long as the e is valid, in addition
 * the function will return NULL if e is invalid.  To remove the ambiguity call
 * lsmErrorGetNumber and check return code.
 * @param e             The lsmErrorPtr
 * @param[out] size     Number of bytes of data returned.
 * @return NULL if does not exist, else debug message.
 */
void LSM_DLL_EXPORT *lsmErrorGetDebugData(lsmErrorPtr e, uint32_t *size);

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMTERROR_H */

