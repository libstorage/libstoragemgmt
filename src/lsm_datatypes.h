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

#ifndef LSM_DATATYPES_H
#define LSM_DATATYPES_H

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_common.h>
#include <stdint.h>
#include <libxml/uri.h>
#include "lsm_ipc.hpp"


#ifdef  __cplusplus
extern "C" {
#endif


#define LSM_VOL_MAGIC       0xFEEDEFFE
#define LSM_IS_VOL(obj)     ((obj) && \
                                ((obj)->magic==LSM_VOL_MAGIC))

/**
 * Information about storage volumes.
 */
struct _lsmVolume {
    uint32_t    magic;
    char *id;                           /**< System wide unique identifier */
    char *name;                         /**< Human recognizeable name */
    char *vpd83;                        /**< SCSI page 83 unique ID */
    uint64_t    blockSize;              /**< Block size */
    uint64_t    numberOfBlocks;         /**< Number of blocks */
    uint32_t    status;                 /**< Status */
};

#define LSM_POOL_MAGIC       0xFEEDF337
#define LSM_IS_POOL(obj)     ((obj) && \
                                ((obj)->magic==LSM_POOL_MAGIC))

/**
 * Information about storage pools.
 */
struct _lsmPool {
    uint32_t    magic;          /**< Used for verfication */
    char *id;                   /**< System wide unique identifier */
    char *name;                 /**< Human recognizeable name */
    uint64_t    totalSpace;     /**< Total size */
    uint64_t    freeSpace;      /**< Free space available */
};


#define LSM_INIT_MAGIC       0xFEED1337
#define LSM_IS_INIT(obj)     ((obj) && \
                                ((obj)->magic==LSM_INIT_MAGIC))
/**
 * Information about an initiator.
 */
struct _lsmInitiator {
    uint32_t magic;             /**< Used for verification */
    lsmInitiatorType   idType; /**< Type of id */
    char *id;                   /**< Identifier */
};

/**
 * Capabilities of the plug-in and storage array.
 */
struct _lsmStorageCapabilities {
};

/**
 * Information pertaining to a storage group.
 */
struct _lsmAccessGroup {
};


#define LSM_CONNECT_MAGIC       0xFEEDB0B0
#define LSM_IS_CONNECT(obj)     ((obj) && \
                                ((obj)->magic==LSM_CONNECT_MAGIC))

/**
 * Function pointer decl. for the functions that the plug-in must export.
 */
typedef int (*lsmRegister)(lsmConnectPtr c, xmlURIPtr uri, const char *password,
                uint32_t timeout, lsmErrorPtr *e);
typedef int (*lsmUnregister)( lsmConnectPtr c );

/**
 * Information pertaining to the plug-in specifics.
 */
struct lsmPlugin {
    char    *desc;                      /**< Description */
    char    *version;                   /**< Version */
    void    *privateData;               /**< Private data for plug-in */
    struct lsmMgmtOps    *mgmtOps;      /**< Callback for management ops */
    struct lsmSanOps    *sanOps;        /**< Callbacks for SAN ops */
    struct lsmNasOps    *nasOps;        /**< Callbacks for NAS ops */
    struct lsmFsOps     *fsOps;         /**< Callbacks for fs ops */
};


/**
 * Information pertaining to the connection.  This is the main structure and
 * opaque data type for the library.
 */
struct _lsmConnect {
    uint32_t    magic;          /**< Magic, used for structure validation */
    uint32_t    flags;          /**< Flags for the connection */
    xmlURIPtr   uri;            /**< URI */
    char        *raw_uri;       /**< Raw URI string */
    lsmError    *error;         /**< Error information */
    //lsmUnregister       unregister;     /**< Callback to unregister */
    //struct lsmPlugin    plugin;         /**< Plug-in information */
    Ipc *tp;                            /**< IPC transport */
};


#define LSM_ERROR_MAGIC       0xDEADB0B0
#define LSM_IS_ERROR(obj)     ((obj) && \
                                (obj)->magic==LSM_ERROR_MAGIC)

/**
 * Used to house error information.
 */
struct _lsmError {
    uint32_t    magic;          /**< Magic, used for struct validation */
    lsmErrorNumber code;        /**< Error code */
    lsmErrorDomain domain;      /**< Where the error occured */
    lsmErrorLevel level;        /**< Severity of the error */
    uint32_t    reserved;       /**< Reserved */
    char *message;              /**< Human readable error message */
    char *exception;            /**< Exception message if present */
    char *debug;                /**< Debug message */
    void *debug_data;           /**< Debug data */
    uint32_t debug_data_size;   /**< Size of the data */
};

/**
 * Returns a pointer to a newly created connection structure.
 * @return NULL on memory exhaustion, else new connection.
 */
LSM_DLL_LOCAL lsmConnectPtr getConnection();

/**
 * De-allocates the connection.
 * @param c     Connection to free.
 */
LSM_DLL_LOCAL void freeConnection(lsmConnectPtr c);

/**
 * Loads the requester driver specified in the uri.
 * @param c             Connection
 * @param uri           URI
 * @param password      Password
 * @param timeout       Initial timeout
 * @param e             Error data
 * @return LSM_ERR_OK on success, else error code.
 */
LSM_DLL_LOCAL int loadDriver(lsmConnectPtr c, xmlURIPtr uri,
                                const char *password, uint32_t timeout,
                                lsmErrorPtr *e);

#ifdef  __cplusplus
}
#endif

#endif  /* LSM_DATATYPES_H */

