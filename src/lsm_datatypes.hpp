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
#include <libxml/uri.h>
#include "lsm_ipc.hpp"


#ifdef  __cplusplus
extern "C" {
#endif

#define MAGIC_CHECK(obj, m)     ((obj) && \
                                     ((obj)->magic==(m) ))
#define LSM_DEL_MAGIC(obj)  ((obj & 0x0FFFFFFF) | 0xD0000000)

#define LSM_VOL_MAGIC       0xAA7A0000
#define LSM_IS_VOL(obj)     MAGIC_CHECK(obj, LSM_VOL_MAGIC)

/**
 * Information about storage volumes.
 */
struct LSM_DLL_LOCAL _lsmVolume {
    uint32_t    magic;
    char *id;                           /**< System wide unique identifier */
    char *name;                         /**< Human recognizeable name */
    char *vpd83;                        /**< SCSI page 83 unique ID */
    uint64_t    blockSize;              /**< Block size */
    uint64_t    numberOfBlocks;         /**< Number of blocks */
    uint32_t    status;                 /**< Status */
    char *system_id;
};

#define LSM_POOL_MAGIC       0xAA7A0001
#define LSM_IS_POOL(obj)     MAGIC_CHECK(obj, LSM_POOL_MAGIC)

/**
 * Information about storage pools.
 */
struct LSM_DLL_LOCAL _lsmPool {
    uint32_t    magic;          /**< Used for verfication */
    char *id;                   /**< System wide unique identifier */
    char *name;                 /**< Human recognizeable name */
    uint64_t    totalSpace;     /**< Total size */
    uint64_t    freeSpace;      /**< Free space available */
    char *system_id;
};


#define LSM_INIT_MAGIC       0xAA7A0002
#define LSM_IS_INIT(obj)     MAGIC_CHECK(obj, LSM_INIT_MAGIC)
/**
 * Information about an initiator.
 */
struct LSM_DLL_LOCAL _lsmInitiator {
    uint32_t magic;             /**< Used for verification */
    lsmInitiatorType   idType;  /**< Type of id */
    char *id;                   /**< Identifier */
    char *name;                 /**< Initiator name */
};

/**
 * Capabilities of the plug-in and storage array.
 */
struct _lsmStorageCapabilities {
};

#define LSM_ACCESS_GROUP_MAGIC  0xAA7A0003
#define LSM_IS_ACCESS_GROUP(obj)    MAGIC_CHECK(obj, LSM_ACCESS_GROUP_MAGIC)

/**
 * Information pertaining to a storage group.
 */
struct _lsmAccessGroup {
    uint32_t magic;
    char *id;
    char *name;
    char *system_id;
    lsmStringList *initiators;
};

#define LSM_FILE_SYSTEM_MAGIC  0xAA7A0004
#define LSM_IS_FILE_SYSTEM(obj)    MAGIC_CHECK(obj, LSM_FILE_SYSTEM_MAGIC)

/**
 * Structure for file systems
 */
struct _lsmFileSystem {
    uint32_t magic;
    char *id;
    char *name;
    uint64_t total_space;
    uint64_t free_space;
    char *pool_id;
    char *system_id;
};

#define LSM_SNAP_SHOT_MAGIC  0xAA7A0005
#define LSM_IS_SNAP_SHOT(obj)    MAGIC_CHECK(obj, LSM_SNAP_SHOT_MAGIC)

/**
 * Structure for snapshots.
 */
struct _lsmSnapShot {
    uint32_t magic;
    char *id;
    char *name;
    uint64_t ts;
};

#define LSM_NFS_EXPORT_MAGIC  0xAA7A0006
#define LSM_IS_NFS_EXPORT(obj)    MAGIC_CHECK(obj, LSM_NFS_EXPORT_MAGIC)

/**
 * Structure for NFS export information
 */
struct _lsmNfsExport {
    uint32_t magic;
    char *id;
    char *fs_id;
    char *export_path;
    char *auth_type;
    lsmStringList *root;
    lsmStringList *rw;
    lsmStringList *ro;
    uint64_t anonuid;
    uint64_t anongid;
    char *options;
};

#define LSM_BLOCK_RANGE_MAGIC       0xAA7A0007
#define LSM_IS_BLOCK_RANGE(obj)     MAGIC_CHECK(obj, LSM_BLOCK_RANGE_MAGIC)

/**
 * Structure for block range ( a region to be replicated )
 */
struct _lsmBlockRange {
    uint32_t magic;
    uint64_t source_start;
    uint64_t dest_start;
    uint64_t block_count;
};

#define LSM_SYSTEM_MAGIC  0xAA7A0009
#define LSM_IS_SYSTEM(obj)    MAGIC_CHECK(obj, LSM_SYSTEM_MAGIC)

/**
 * Structure for a system
 */
struct _lsmSystem {
    uint32_t magic;
    char *id;
    char *name;
};

#define LSM_CONNECT_MAGIC       0xAA7A000A
#define LSM_IS_CONNECT(obj)     MAGIC_CHECK(obj, LSM_CONNECT_MAGIC)


#define LSM_PLUGIN_MAGIC    0xAA7A000B
#define LSM_IS_PLUGIN(obj)  MAGIC_CHECK(obj, LSM_PLUGIN_MAGIC)

/**
 * Information pertaining to the plug-in specifics.
 */
struct LSM_DLL_LOCAL _lsmPlugin {
    uint32_t    magic;                  /**< Magic, used for structure validation */
    Ipc         *tp;                    /**< IPC transport */
    char    *desc;                      /**< Description */
    char    *version;                   /**< Version */
    void    *privateData;               /**< Private data for plug-in */
    lsmError    *error;                 /**< Error information */
    lsmPluginRegister   reg;            /**< Plug-in registration */
    lsmPluginUnregister unreg;          /**< Plug-in unregistration */
    struct lsmMgmtOps    *mgmtOps;      /**< Callback for management ops */
    struct lsmSanOps    *sanOps;        /**< Callbacks for SAN ops */
    struct lsmNasOps    *nasOps;        /**< Callbacks for NAS ops */
    struct lsmFsOps     *fsOps;         /**< Callbacks for fs ops */
};


/**
 * Information pertaining to the connection.  This is the main structure and
 * opaque data type for the library.
 */
struct LSM_DLL_LOCAL _lsmConnect {
    uint32_t    magic;          /**< Magic, used for structure validation */
    uint32_t    flags;          /**< Flags for the connection */
    xmlURIPtr   uri;            /**< URI */
    char        *raw_uri;       /**< Raw URI string */
    lsmError    *error;         /**< Error information */
    Ipc *tp;                    /**< IPC transport */
};


#define LSM_ERROR_MAGIC       0xAA7A000C
#define LSM_IS_ERROR(obj)     MAGIC_CHECK(obj, LSM_ERROR_MAGIC)

/**
 * Used to house error information.
 */
struct LSM_DLL_LOCAL _lsmError {
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
 * Used to house string collection.
 */
#define LSM_STRING_LIST_MAGIC       0xAA7A000D
#define LSM_IS_STRING_LIST(obj)     MAGIC_CHECK(obj, LSM_STRING_LIST_MAGIC)
struct LSM_DLL_LOCAL _lsmStringList {
    uint32_t    magic;          /**< Magic value */
    uint32_t    size;          /**< Number of elements */
    char *values[0];
};

#define LSM_FS_MAGIC                0xAA7A000E
#define LSM_IS_FS(obj)     MAGIC_CHECK(obj, LSM_FS_MAGIC)
struct LSM_DLL_LOCAL _lsmFs {
    uint32_t magic;
    char *id;
    char *name;
    char *pool_id;
    uint64_t total_space;
    uint64_t free_space;
    char *system_id;
};

#define LSM_SS_MAGIC                0xAA7A000F
#define LSM_IS_SS(obj)     MAGIC_CHECK(obj, LSM_SS_MAGIC)
struct LSM_DLL_LOCAL _lsmSs {
    uint32_t magic;
    char *id;
    char *name;
    uint64_t ts;
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

