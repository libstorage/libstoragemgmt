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

#ifndef LIBSTORAGEMGMT_PLUG_INTERFACE_H
#define LIBSTORAGEMGMT_PLUG_INTERFACE_H

#include <libxml/uri.h>
#include "libstoragemgmt_common.h"
#include "libstoragemgmt_types.h"
#include "libstoragemgmt_error.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Opaque data type for plug-ins
 */
typedef struct _lsmPlugin lsmPlugin;
typedef lsmPlugin *lsmPluginPtr;

typedef int (*lsmPluginRegister)(  lsmPluginPtr c, xmlURIPtr uri, const char *password,
                        uint32_t timeout );

typedef int (*lsmPluginUnregister)( lsmPluginPtr c );


typedef int (*lsmPlugSetTmo)( lsmPluginPtr c, uint32_t timeout );
typedef int (*lsmPlugGetTmo)( lsmPluginPtr c, uint32_t *timeout );
typedef int (*lsmPlugCapabilities)(lsmPluginPtr conn,
                                        lsmStorageCapabilitiesPtr *cap);
typedef int (*lsmPlugJobStatusVol)(lsmPluginPtr conn, const char *job,
                                        lsmJobStatus *status,
                                        uint8_t *percentComplete,
                                        lsmVolumePtr *vol);
typedef int (*lsmPlugJobFree)(lsmPluginPtr c, char *jobNumber);

/**
 * Callback functions for management operations.
 */
struct lsmMgmtOps {
    lsmPlugSetTmo    tmo_set;                   /**< tmo set callback */
    lsmPlugGetTmo    tmo_get;                   /**< tmo get callback */
    lsmPlugCapabilities     capablities;        /**< capabilities callback */
    lsmPlugJobStatusVol job_status;             /**< status of job */
    lsmPlugJobFree      job_free;               /**< Free a job */
};

typedef int (*lsmPlugPoolList)(lsmPluginPtr conn, lsmPoolPtr *poolArray,
                        uint32_t *count);
typedef int (*lsmPlugGetPools)( lsmPluginPtr c, lsmPoolPtr **poolArray,
                                        uint32_t *count);

typedef int (*lsmPlugGetInits)( lsmPluginPtr c, lsmInitiatorPtr **initArray,
                                        uint32_t *count);

typedef int (*lsmPlugGetVolumes)( lsmPluginPtr c, lsmVolumePtr **volArray,
                                        uint32_t *count);

typedef int (*lsmPlugCreateVolume)(lsmPluginPtr c, lsmPoolPtr pool,
                        const char *volumeName, uint64_t size,
                        lsmProvisionType provisioning, lsmVolumePtr *newVolume,
                        char **job);

typedef int (*lsmPlugReplicateVolume)(lsmPluginPtr c, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        const char *name, lsmVolumePtr *newReplicant,
                        char **job);
typedef int (*lsmPlugResizeVolume)(lsmPluginPtr c, lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                char **job);

typedef int (*lsmPlugDeleteVolume)(lsmPluginPtr c, lsmVolumePtr volume,
                                    char **job);

typedef int (*lsmPlugCreateInit)(lsmPluginPtr c, const char *name,
                                    const char *id, lsmInitiatorType type,
                                    lsmInitiatorPtr *init);

typedef int (*lsmPlugDeleteInit)(lsmPluginPtr c, lsmInitiatorPtr init);

typedef int (*lsmPlugAccessGrant)(lsmPluginPtr c, lsmInitiatorPtr i, lsmVolumePtr v,
                        lsmAccessType access, char **job);

typedef int (*lsmPlugAccessRemove)(lsmPluginPtr c, lsmInitiatorPtr i, lsmVolumePtr v);

/**
 * Block oriented functions
 */
struct lsmSanOps {
    lsmPlugGetPools pool_get;           /**< Callback for retrieving volumes */
    lsmPlugGetInits init_get;           /**< Callback for retrieving initiators */
    lsmPlugGetVolumes vol_get;          /**< Callback for retrieving volumes */
    lsmPlugCreateVolume vol_create;     /**< Callback for creating a lun */
    lsmPlugReplicateVolume vol_replicate; /**< Callback for replicating lun */
    lsmPlugResizeVolume vol_resize;     /**< Callback for resizing a volume */
    lsmPlugDeleteVolume vol_delete;     /**< Callback for deleting a volume */
    lsmPlugCreateInit init_create;      /**< Callback for creating initiator */
    lsmPlugDeleteInit init_delete;      /**< Callback for deleting initiator */
    lsmPlugAccessGrant access_grant;    /**< Callback for granting access */
    lsmPlugAccessRemove access_remove;  /**< Callback for removing access */

};

/**
 * File system oriented functionality
 */
struct lsmFsOps {

};

/**
 * NAS system oriented functionality
 */
struct lsmNasOps {

};

/**
 * Initializes the plug-in.
 * @param argc  Command line argument count
 * @param argv  Command line arguments
 * @param reg   Registration function
 * @param unreg Un-Registration function
 * @return exit code for plug-in
 */
int LSM_DLL_EXPORT lsmPluginInit( int argc, char *argv[], lsmPluginRegister reg,
                                lsmPluginUnregister unreg);


int LSM_DLL_EXPORT lsmRegisterPlugin( lsmPluginPtr plug, const char *desc,
                        const char *version,
                        void * private_data, struct lsmMgmtOps *mgmOps,
                        struct lsmSanOps *sanOp, struct lsmFsOps *fsOp,
                        struct lsmNasOps *nasOp );

/**
 * Used to retrieve private data for plug-in operation.
 * @param plug  Opaque plug-in pointer.
 */
void LSM_DLL_EXPORT *lsmGetPrivateData( lsmPluginPtr plug );


/**
 * Return an error with the plug-in
 * @param plug          Opaque plug-in
 * @param error         Error to associate.
 * @return              LSM_ERR_OK, else error reason.
 */
int LSM_DLL_EXPORT lsmPluginErrorLog( lsmPluginPtr plug, lsmErrorPtr error);

/**
 * Creates an error record.
 * @param code
 * @param domain
 * @param level
 * @param msg
 * @param exception
 * @param debug
 * @param debug_data
 * @param debug_data_size
 * @return Null on error, else valid error error record.
 */
lsmErrorPtr LSM_DLL_EXPORT lsmErrorCreate( lsmErrorNumber code,
                                lsmErrorDomain domain,
                                lsmErrorLevel level, const char* msg,
                                const char *exception, const char *debug,
                                const void *debug_data, uint32_t debug_data_size);


/**
 * Plug-in macros for creating errors
 */
#define LSM_ERROR_CREATE_PLUGIN_MSG( code, msg )        \
        lsmErrorCreate(code, LSM_ERR_DOMAIN_PLUG_IN, LSM_ERR_LEVEL_ERROR, msg, NULL, NULL, NULL, 0)

#define LSM_ERROR_CREATE_PLUGIN_EXCEPTION( code, msg, exception) \
        lsmErrorCreate((code), LSM_ERR_DOMAIN_PLUG_IN, LSM_ERR_LEVEL_ERROR, (msg), (exception), NULL, NULL, 0)

#define LSM_ERROR_CREATE_PLUGIN_DEBUG( code, msg, exception, debug, debug_data, debug_len) \
        lsmErrorCreate((code), LSM_ERR_DOMAIN_PLUG_IN, LSM_ERR_LEVEL_ERROR, (msg), (exception), (debug), (debug_data), debug_len))

/**
 * Helper function to create an array of lsmPoolPtr
 * @param size  Number of elements
 * @return Valid pointer or NULL on error.
 */
lsmPoolPtr LSM_DLL_EXPORT *lsmPoolRecordAllocArray( uint32_t size );

/**
 * Helper function to allocate a pool record.
 * @param id            System unique identifier
 * @param name          Human readable name
 * @param totalSpace    Total space
 * @param freeSpace     Space available
 * @param system_id     System id
 * @return LSM_ERR_OK on success, else error reason.
 */
lsmPoolPtr LSM_DLL_EXPORT lsmPoolRecordAlloc(const char *id, const char *name,
                                uint64_t totalSpace,
                                uint64_t freeSpace,
                                const char *system_id);

/**
 * Allocate the storage needed for and array of Initiator records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsmInitiatorPtr LSM_DLL_EXPORT *lsmInitiatorRecordAllocArray( uint32_t size );

/**
 * Allocate the storage needed for one initiator record.
 * @param idType    Type of initiator.
 * @param id        ID of initiator.
 * @param name      Name of initiator
 * @return Allocated memory or NULL on error.
 */
lsmInitiatorPtr LSM_DLL_EXPORT lsmInitiatorRecordAlloc( lsmInitiatorType idType,
                                                        const char* id,
                                                        const char* name);

/**
 * Allocate the storage needed for and array of Volume records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsmVolumePtr LSM_DLL_EXPORT *lsmVolumeRecordAllocArray( uint32_t size);

/**
 * Allocated the storage needed for one volume record.
 * @param id                    ID
 * @param name                  Name
 * @param vpd83                 SCSI vpd 83 id
 * @param blockSize             Volume block size.
 * @param numberOfBlocks        Volume number of blocks.
 * @param status                Volume status
 * @param system_id             System id
 * @return Allocated memory or NULL on error.
 */
lsmVolumePtr LSM_DLL_EXPORT lsmVolumeRecordAlloc( const char *id,
                                        const char *name, const char *vpd83,
                                        uint64_t blockSize,
                                        uint64_t numberOfBlocks,
                                        uint32_t status,
                                        const char *system_id);

/**
 * Allocate the storage needed for and array of System records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsmSystemPtr LSM_DLL_EXPORT *lsmSystemRecordAllocArray( uint32_t size );

/**
 * Allocates the storage for one system record.
 * @param id        Id
 * @param name      System name (human readable)
 * @return  Allocated memory or NULL on error.
 */
lsmSystemPtr LSM_DLL_EXPORT lsmSystemRecordAlloc( const char *id,
                                                  const char *name );

/**
 * Allocates storage for AccessGroup array
 * @param size      Number of elements to store.
 * @return  NULL on error, else pointer to array for use.
 */
lsmAccessGroupPtr LSM_DLL_EXPORT *lsmAccessGroupRecordAllocArray( uint32_t size);


/**
 * Allocates storage for single AccessGroup
 * @param id                ID of access group
 * @param name              Name of access group
 * @param initiators        List of initiators, can be NULL
 * @param system_id         System id
 * @return NULL on error, else valid AccessGroup pointer.
 */
lsmAccessGroupPtr LSM_DLL_EXPORT lsmAccessGroupRecordAlloc(const char *id,
                                                     const char *name,
                                                     lsmStringListPtr initiators,
                                                     const char *system_id);

/**
 * Allocates memory for a file system record
 * @param id                    ID of file system
 * @param name                  Name of file system
 * @param total_space           Total space
 * @param free_space            Free space
 * @param pool_id               Pool id
 * @param system_id             System id
 * @return lsmFsPtr, NULL on error
 */
lsmFsPtr LSM_DLL_EXPORT lsmFsRecordAlloc( const char *id, const char *name,
                                            uint64_t total_space,
                                            uint64_t free_space,
                                            const char *pool_id,
                                            const char *system_id);

/**
 * Allocates the memory for the array of file system records.
 * @param size      Number of elements
 * @return Allocated memory, NULL on error
 */
lsmFsPtr LSM_DLL_EXPORT *lsmFsRecordAllocArray( uint32_t size );

/**
 * Allocates the memory for single snap shot record.
 * @param id            ID
 * @param name          Name
 * @param ts            Epoch time stamp when snapshot was created
 * @return Allocated memory, NULL on error
 */
lsmSsPtr LSM_DLL_EXPORT lsmSsRecordAlloc( const char *id, const char *name,
                                            uint64_t ts);

/**
 * Allocates the memory for an array of snapshot records.
 * @param size          Number of elements
 * @return Allocated memory, NULL on error
 */
lsmSsPtr LSM_DLL_EXPORT *lsmSsRecordAllocArray( uint32_t size );

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_PLUG_INTERFACE_H */

