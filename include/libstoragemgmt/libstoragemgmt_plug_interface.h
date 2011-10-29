/*
 * Copyright (C) 2011 Red Hat, Inc.
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

#include <stdint.h>
#include <libstoragemgmt/libstoragemgmt_types.h>
#include <libstoragemgmt/libstoragemgmt_error.h>


#ifdef  __cplusplus
extern "C" {
#endif


typedef int (*lsmPlugSetTmo)( lsmConnectPtr c, uint32_t timeout );
typedef int (*lsmPlugGetTmo)( lsmConnectPtr c, uint32_t *timeout );
typedef int (*lsmPlugCapabilities)(lsmConnectPtr conn,
                                        lsmStorageCapabilitiesPtr *cap);
typedef int (*lsmPlugJobStatusVol)(lsmConnectPtr conn, uint32_t jobNumber,
                                        lsmJobStatus *status,
                                        uint8_t *percentComplete,
                                        lsmVolumePtr *vol);
typedef int (*lsmPlugJobFree)(lsmConnectPtr c, uint32_t jobNumber);

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

typedef int (*lsmPlugPoolList)(lsmConnectPtr conn, lsmPoolPtr *poolArray,
                        uint32_t *count);
typedef int (*lsmPlugGetPools)( lsmConnectPtr c, lsmPoolPtr **poolArray,
                                        uint32_t *count);

typedef int (*lsmPlugGetInits)( lsmConnectPtr c, lsmInitiatorPtr **initArray,
                                        uint32_t *count);

typedef int (*lsmPlugGetVolumes)( lsmConnectPtr c, lsmVolumePtr **volArray,
                                        uint32_t *count);

typedef int (*lsmPlugCreateVolume)(lsmConnectPtr c, lsmPoolPtr pool,
                        const char *volumeName, uint64_t size,
                        lsmProvisionType provisioning, lsmVolumePtr *newVolume,
                        uint32_t *job);

typedef int (*lsmPlugReplicateVolume)(lsmConnectPtr c, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        const char *name, lsmVolumePtr *newReplicant,
                        uint32_t *job);
typedef int (*lsmPlugResizeVolume)(lsmConnectPtr c, lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                uint32_t *job);

typedef int (*lsmPlugDeleteVolume)(lsmConnectPtr c, lsmVolumePtr volume,
                                    uint32_t *job);

typedef int (*lsmPlugCreateInit)(lsmConnectPtr c, const char *name,
                                    const char *id, lsmInitiatorType type,
                                    lsmInitiatorPtr *init);

typedef int (*lsmPlugAccessGrant)(lsmConnectPtr c, lsmInitiatorPtr i, lsmVolumePtr v,
                        lsmAccessType access, uint32_t *job);

typedef int (*lsmPlugAccessRemove)(lsmConnectPtr c, lsmInitiatorPtr i, lsmVolumePtr v);

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

int lsmRegisterPlugin( lsmConnectPtr conn, const char *desc, const char *version,
                        void * private_data, struct lsmMgmtOps *mgmOps,
                        struct lsmSanOps *sanOp, struct lsmFsOps *fsOp,
                        struct lsmNasOps *nasOp );
void *lsmGetPrivateData( lsmConnectPtr conn );

/**
 * Associate an error with the connection.
 * @param conn          Connection
 * @param error         Error to associate.
 * @return              LSM_ERR_OK, else error reason.
 */
int lsmErrorLog( lsmConnectPtr conn, lsmErrorPtr error);

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
lsmErrorPtr     lsmErrorCreate( lsmErrorNumber code, lsmErrorDomain domain,
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
lsmPoolPtr *lsmPoolRecordAllocArray( uint32_t size );

/**
 * Helper function to allocate a pool record.
 * @param id            System unique identifier
 * @param name          Human readable name
 * @param totalSpace    Total space
 * @param freeSpace     Space available
 * @return LSM_ERR_OK on success, else error reason.
 */
lsmPoolPtr lsmPoolRecordAlloc(const char *id, const char *name,
                                uint64_t totalSpace,
                                uint64_t freeSpace);

/**
 * Allocate the storage needed for and array of Initiator records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsmInitiatorPtr *lsmInitiatorRecordAllocArray( uint32_t size );

/**
 * Allocate the storage needed for one initiator record.
 * @param idType    Type of initiator.
 * @param id        ID of initiator.
 * @return Allocated memory or NULL on error.
 */
lsmInitiatorPtr lsmInitiatorRecordAlloc( lsmInitiatorType idType, const char* id);

/**
 * Allocate the storage needed for and array of Volume records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsmVolumePtr *lsmVolumeRecordAllocArray( uint32_t size);

/**
 * Allocated the storage needed for one volume record.
 * @param id                    ID
 * @param name                  Name
 * @param vpd83                 SCSI vpd 83 id
 * @param blockSize             Volume block size.
 * @param numberOfBlocks        Volume number of blocks.
 * @param status                Volume status
 * @return Allocated memory or NULL on error.
 */
lsmVolumePtr lsmVolumeRecordAlloc( const char *id, const char *name,
                                        const char *vpd83, uint64_t blockSize,
                                        uint64_t numberOfBlocks,
                                        uint32_t status);

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_PLUG_INTERFACE_H */

