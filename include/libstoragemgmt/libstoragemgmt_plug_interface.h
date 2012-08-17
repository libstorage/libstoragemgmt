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

#include "libstoragemgmt_systems.h"
#include "libstoragemgmt_volumes.h"
#include "libstoragemgmt_pool.h"
#include "libstoragemgmt_capabilities.h"

#ifdef  __cplusplus
extern "C" {
#endif

typedef enum {
    LSM_DATA_TYPE_UNKNOWN = -1,
    LSM_DATA_TYPE_NONE,
    LSM_DATA_TYPE_ACCESS_GROUP,
    LSM_DATA_TYPE_BLOCK_RANGE,
    LSM_DATA_TYPE_FS,
    LSM_DATA_TYPE_INITIATOR,
    LSM_DATA_TYPE_NFS_EXPORT,
    LSM_DATA_TYPE_POOL,
    LSM_DATA_TYPE_SS,
    LSM_DATA_TYPE_STRING_LIST,
    LSM_DATA_TYPE_SYSTEM,
    LSM_DATA_TYPE_VOLUME,
} lsmDataType;

/**
 * Opaque data type for plug-ins
 */
typedef struct _lsmPlugin lsmPlugin;
typedef lsmPlugin *lsmPluginPtr;

typedef int (*lsmPluginRegister)(  lsmPluginPtr c, xmlURIPtr uri, const char *password,
                        uint32_t timeout, lsmFlag_t flags);

typedef int (*lsmPluginUnregister)( lsmPluginPtr c, lsmFlag_t flags );


typedef int (*lsmPlugSetTmo)( lsmPluginPtr c, uint32_t timeout, lsmFlag_t flags );
typedef int (*lsmPlugGetTmo)( lsmPluginPtr c, uint32_t *timeout, lsmFlag_t flags );
typedef int (*lsmPlugCapabilities)(lsmPluginPtr c, lsmSystemPtr sys,
                                    lsmStorageCapabilitiesPtr *cap, lsmFlag_t flags);
typedef int (*lsmPlugJobStatus)(lsmPluginPtr c, const char *job,
                                        lsmJobStatus *status,
                                        uint8_t *percentComplete,
                                        lsmDataType *type,
                                        void **value, lsmFlag_t flags);
typedef int (*lsmPlugJobFree)(lsmPluginPtr c, char *jobNumber, lsmFlag_t flags);

typedef int (*lsmPlugListPools)( lsmPluginPtr c, lsmPoolPtr **poolArray,
                                        uint32_t *count, lsmFlag_t flags);

typedef int (*lsmPlugSystemList)(lsmPluginPtr c, lsmSystemPtr **systems,
                                        uint32_t *systemCount, lsmFlag_t flags);

/**
 * Callback functions for management operations.
 */
struct lsmMgmtOps {
    lsmPlugSetTmo       tmo_set;                /**< tmo set callback */
    lsmPlugGetTmo       tmo_get;                /**< tmo get callback */
    lsmPlugCapabilities capablities;            /**< capabilities callback */
    lsmPlugJobStatus    job_status;             /**< status of job */
    lsmPlugJobFree      job_free;               /**< Free a job */
    lsmPlugListPools    pool_list;
    lsmPlugSystemList   system_list;            /**< List of systems */
};

typedef int (*lsmPlugListInits)( lsmPluginPtr c, lsmInitiatorPtr **initArray,
                                        uint32_t *count, lsmFlag_t flags);

typedef int (*lsmPlugListVolumes)( lsmPluginPtr c, lsmVolumePtr **volArray,
                                        uint32_t *count, lsmFlag_t flags);

typedef int (*lsmPlugVolumeCreate)(lsmPluginPtr c, lsmPoolPtr pool,
                        const char *volumeName, uint64_t size,
                        lsmProvisionType provisioning, lsmVolumePtr *newVolume,
                        char **job, lsmFlag_t flags);

typedef int (*lsmPlugVolumeReplicate)(lsmPluginPtr c, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        const char *name, lsmVolumePtr *newReplicant,
                        char **job, lsmFlag_t flags);

typedef int (*lsmPlugVolumeReplicateRangeBlockSize)(lsmPluginPtr c, uint32_t *bs, lsmFlag_t flags);

typedef int (*lsmPlugVolumeReplicateRange)(lsmPluginPtr c,
                                                lsmReplicationType repType,
                                                lsmVolumePtr source,
                                                lsmVolumePtr dest,
                                                lsmBlockRangePtr *ranges,
                                                uint32_t num_ranges, char **job, lsmFlag_t flags);

typedef int (*lsmPlugVolumeResize)(lsmPluginPtr c, lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                char **job, lsmFlag_t flags);

typedef int (*lsmPlugVolumeDelete)(lsmPluginPtr c, lsmVolumePtr volume,
                                    char **job, lsmFlag_t flags);

typedef int (*lsmPlugAccessGrant)(lsmPluginPtr c, lsmInitiatorPtr i, lsmVolumePtr v,
                        lsmAccessType access, char **job, lsmFlag_t flags);

typedef int (*lsmPlugAccessRemove)(lsmPluginPtr c, lsmInitiatorPtr i, lsmVolumePtr v, lsmFlag_t flags);

typedef int (*lsmPlugVolumeStatus)(lsmPluginPtr c, lsmVolumePtr v,
                                                lsmVolumeStatusType *status, lsmFlag_t flags);

typedef int (*lsmPlugVolumeOnline)(lsmPluginPtr c, lsmVolumePtr v, lsmFlag_t flags);
typedef int (*lsmPlugVolumeOffline)(lsmPluginPtr c, lsmVolumePtr v, lsmFlag_t flags);

typedef int (*lsmPlugInitiatorGrant)(lsmPluginPtr c, const char *initiator_id,
                                        lsmInitiatorType initiator_type,
                                        lsmVolumePtr volume,
                                        lsmAccessType access,
                                        char **job,
                                        lsmFlag_t flags);

typedef int (*lsmPlugInitiatorRevoke)(lsmPluginPtr c, lsmInitiatorPtr init,
                                        lsmVolumePtr volume, char **job,
                                        lsmFlag_t flags);

typedef int (*lsmPlugInitiatorsGrantedToVolume)(lsmPluginPtr c,
                                        lsmVolumePtr volume,
                                        lsmInitiatorPtr **initArray,
                                        uint32_t *count, lsmFlag_t flags);

typedef int (*lsmPlugIscsiChapAuthInbound)(lsmPluginPtr c,
                                                lsmInitiatorPtr initiator,
                                                const char *username,
                                                const char *password,
                                                lsmFlag_t flags);

typedef int (*lsmPlugAccessGroupList)(lsmPluginPtr c,
                                        lsmAccessGroupPtr **groups,
                                        uint32_t *groupCount, lsmFlag_t flags);
typedef int (*lsmPlugAccessGroupCreate)(lsmPluginPtr c,
                                            const char *name,
                                            const char *initiator_id,
                                            lsmInitiatorType id_type,
                                            const char *system_id,
                                            lsmAccessGroupPtr *access_group, lsmFlag_t flags);

typedef int (*lsmPlugAccessGroupDel)(lsmPluginPtr c,
                                            lsmAccessGroupPtr group,
                                            char **job, lsmFlag_t flags);

typedef int (*lsmPlugAccessGroupAddInitiator)(lsmPluginPtr c,
                                lsmAccessGroupPtr group,
                                const char *initiator_id,
                                lsmInitiatorType id_type, char **job, lsmFlag_t flags);

typedef int (*lsmPlugAccessGroupDelInitiator)(lsmPluginPtr c,
                                                    lsmAccessGroupPtr group,
                                                    const char *initiator_id,
                                                    char **job, lsmFlag_t flags);

typedef int (*lsmPlugAccessGroupGrant)(lsmPluginPtr c,
                                            lsmAccessGroupPtr group,
                                            lsmVolumePtr volume,
                                            lsmAccessType access, char **job, lsmFlag_t flags);

typedef int (*lsmPlugAccessGroupRevoke)(lsmPluginPtr c,
                                            lsmAccessGroupPtr group,
                                            lsmVolumePtr volume, char **job, lsmFlag_t flags);

typedef int (*lsmPlugVolumesAccessibleByAccessGroup)(lsmPluginPtr c,
                                                        lsmAccessGroupPtr group,
                                                        lsmVolumePtr **volumes,
                                                        uint32_t *count, lsmFlag_t flags);

typedef int (*lsmPlugVolumesAccessibleByInitiator)(lsmPluginPtr c,
                                                        lsmInitiatorPtr initiator,
                                                        lsmVolumePtr **volumes,
                                                        uint32_t *count, lsmFlag_t flags);

typedef int (*lsmPlugAccessGroupsGrantedToVolume)(lsmPluginPtr c,
                                                    lsmVolumePtr volume,
                                                    lsmAccessGroupPtr **groups,
                                                    uint32_t *groupCount, lsmFlag_t flags);

typedef int (*lsmPlugVolumeChildDependency)(lsmPluginPtr c,
                                            lsmVolumePtr volume,
                                            uint8_t *yes, lsmFlag_t flags);

typedef int (*lsmPlugVolumeChildDependencyRm)(lsmPluginPtr c,
                                            lsmVolumePtr volume,
                                            char **job, lsmFlag_t flags);

typedef int (*lsmPlugFsList)(lsmPluginPtr c, lsmFsPtr **fs,
                                    uint32_t *fsCount, lsmFlag_t flags);

typedef int (*lsmPlugFsCreate)(lsmPluginPtr c, lsmPoolPtr pool,
                                    const char *name, uint64_t size_bytes,
                                    lsmFsPtr *fs, char **job, lsmFlag_t flags);

typedef int (*lsmPlugFsDelete)(lsmPluginPtr c, lsmFsPtr fs, char **job, lsmFlag_t flags);

typedef int (*lsmPlugFsClone)(lsmPluginPtr c, lsmFsPtr src_fs,
                                            const char *dest_fs_name,
                                            lsmFsPtr *cloned_fs,
                                            lsmSsPtr optional_snapshot,
                                            char **job, lsmFlag_t flags);

typedef int (*lsmPlugFsChildDependency)(lsmPluginPtr c, lsmFsPtr fs,
                                                lsmStringListPtr files,
                                                uint8_t *yes);

typedef int (*lsmPlugFsChildDependencyRm)( lsmPluginPtr c, lsmFsPtr fs,
                                                lsmStringListPtr files,
                                                char **job, lsmFlag_t flags);

typedef int (*lsmPlugFsResize)(lsmPluginPtr c, lsmFsPtr fs,
                                    uint64_t new_size_bytes, lsmFsPtr *rfs,
                                    char **job, lsmFlag_t flags);

typedef int (*lsmPlugFsFileClone)(lsmPluginPtr c, lsmFsPtr fs,
                                    const char *src_file_name,
                                    const char *dest_file_name,
                                    lsmSsPtr snapshot, char **job, lsmFlag_t flags);

typedef int (*lsmPlugSsList)(lsmPluginPtr c, lsmFsPtr fs, lsmSsPtr **ss,
                                uint32_t *ssCount, lsmFlag_t flags);

typedef int (*lsmPlugSsCreate)(lsmPluginPtr c, lsmFsPtr fs,
                                    const char *name, lsmStringListPtr files,
                                    lsmSsPtr *snapshot, char **job, lsmFlag_t flags);

typedef int (*lsmPlugSsDelete)(lsmPluginPtr c, lsmFsPtr fs, lsmSsPtr ss,
                                    char **job, lsmFlag_t flags);

typedef int (*lsmPlugSsRevert)(lsmPluginPtr c, lsmFsPtr fs, lsmSsPtr ss,
                                    lsmStringListPtr files,
                                    lsmStringListPtr restore_files,
                                    int all_files, char **job, lsmFlag_t flags);
typedef int (*lsmPlugNfsAuthTypes)( lsmPluginPtr c,
                                            lsmStringListPtr *types, lsmFlag_t flags);

typedef int (*lsmPlugNfsList)( lsmPluginPtr c,
                                            lsmNfsExportPtr **exports,
                                            uint32_t *count, lsmFlag_t flags);
typedef int (*lsmPlugNfsExportFs)( lsmPluginPtr c,
                                        const char *fs_id,
                                        const char *export_path,
                                        lsmStringListPtr root_list,
                                        lsmStringListPtr rw_list,
                                        lsmStringListPtr ro_list,
                                        uint64_t anon_uid,
                                        uint64_t anon_gid,
                                        const char *auth_type,
                                        const char *options,
                                        lsmNfsExportPtr *exported,
                                        lsmFlag_t flags
                                        );

typedef int (*lsmPlugNfsExportRemove)( lsmPluginPtr c, lsmNfsExportPtr e,
                                        lsmFlag_t flags);
/**
 * Block oriented functions
 */
struct lsmSanOpsV1 {
    lsmPlugListInits init_get;          /**< Callback for retrieving initiators */
    lsmPlugListVolumes vol_get;         /**< Callback for retrieving volumes */
    lsmPlugVolumeCreate vol_create;     /**< Callback for creating a lun */
    lsmPlugVolumeReplicate vol_replicate; /**< Callback for replicating lun */
    lsmPlugVolumeReplicateRangeBlockSize vol_rep_range_bs;
    lsmPlugVolumeReplicateRange vol_rep_range;
    lsmPlugVolumeResize vol_resize;     /**< Callback for resizing a volume */
    lsmPlugVolumeDelete vol_delete;     /**< Callback for deleting a volume */
    lsmPlugVolumeOnline vol_online;     /**< Callback for bringing volume online */
    lsmPlugVolumeOffline vol_offline;   /**< Callback for bringing volume offline */
    lsmPlugInitiatorGrant initiator_grant;      /**< Callback for granting access */
    lsmPlugInitiatorRevoke initiator_revoke;    /**< Callback for revoking access */
    lsmPlugInitiatorsGrantedToVolume initiators_granted_to_vol;
    lsmPlugIscsiChapAuthInbound iscsi_chap_auth_inbound;
    lsmPlugAccessGroupList ag_list;     /**< Callback for access groups */
    lsmPlugAccessGroupCreate ag_create; /**< Callback for access group create */
    lsmPlugAccessGroupDel ag_delete;    /**< Callback for access group delete */
    lsmPlugAccessGroupAddInitiator ag_add_initiator;
    lsmPlugAccessGroupDelInitiator ag_del_initiator;
    lsmPlugAccessGroupGrant ag_grant;
    lsmPlugAccessGroupRevoke ag_revoke;
    lsmPlugVolumesAccessibleByAccessGroup vol_accessible_by_ag;
    lsmPlugVolumesAccessibleByInitiator vol_accessible_by_init;
    lsmPlugAccessGroupsGrantedToVolume ag_granted_to_vol;
    lsmPlugVolumeChildDependency vol_child_depends;
    lsmPlugVolumeChildDependencyRm vol_child_depends_rm;
};

/**
 * File system oriented functionality
 */
struct lsmFsOpsV1 {
    lsmPlugFsList   fs_list;
    lsmPlugFsCreate fs_create;
    lsmPlugFsDelete fs_delete;
    lsmPlugFsResize fs_resize;
    lsmPlugFsClone  fs_clone;
    lsmPlugFsFileClone fs_file_clone;
    lsmPlugFsChildDependency fs_child_dependency;
    lsmPlugFsChildDependencyRm fs_child_dependency_rm;
    lsmPlugSsList ss_list;
    lsmPlugSsCreate ss_create;
    lsmPlugSsDelete ss_delete;
    lsmPlugSsRevert ss_revert;
};

/**
 * NAS system oriented functionality
 */
struct lsmNasOpsV1 {
    lsmPlugNfsAuthTypes nfs_auth_types;
    lsmPlugNfsList nfs_list;
    lsmPlugNfsExportFs nfs_export;
    lsmPlugNfsExportRemove nfs_export_remove;
};

/**
 * Copies the memory pointed to by item with given type t.
 * @param t         Type of item to copy
 * @param item      Pointer to src
 * @return Null, else copy of item.
 */
void LSM_DLL_EXPORT * lsmDataTypeCopy(lsmDataType t, void *item);

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


/**
 * Used to register all the data needed for the plug-in operation.
 * @param plug              Pointer provided by the framework
 * @param desc              Plug-in description
 * @param version           Plug-in version
 * @param private_data      Private data to be used for whatever the plug-in needs
 * @param mgmOps            Function pointers for management operations
 * @param sanOp             Function pointers for SAN operations
 * @param fsOp              Function pointers for file system operations
 * @param nasOp             Function pointers for NAS operations
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsmRegisterPluginV1( lsmPluginPtr plug, const char *desc,
                        const char *version,
                        void * private_data, struct lsmMgmtOps *mgmOps,
                        struct lsmSanOpsV1 *sanOp, struct lsmFsOpsV1 *fsOp,
                        struct lsmNasOpsV1 *nasOp );

/**
 * Used to retrieve private data for plug-in operation.
 * @param plug  Opaque plug-in pointer.
 */
void LSM_DLL_EXPORT *lsmGetPrivateData( lsmPluginPtr plug );


/**
 * Logs an error with the plug-in
 * @param plug  Plug-in pointer
 * @param code  Error code to return
 * @param msg   String message
 * @return returns code
 */
int LSM_DLL_EXPORT lsmLogErrorBasic( lsmPluginPtr plug, lsmErrorNumber code,
                                        const char* msg );

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
 * Used to set the free space on a pool record
 * @param p                 Pool to modify
 * @param free_space        New free space value
 */
void LSM_DLL_EXPORT lsmPoolFreeSpaceSet(lsmPoolPtr p, uint64_t free_space);

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
 * @param[in] id        Id
 * @param[in] name      System name (human readable)
 * @param[in] status    Status of the system
 * @return  Allocated memory or NULL on error.
 */
lsmSystemPtr LSM_DLL_EXPORT lsmSystemRecordAlloc( const char *id,
                                                  const char *name,
                                                  uint32_t status );

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

/**
 * Set a capability
 * @param cap           Valid capability pointer
 * @param t             Which capability to set
 * @param v             Value of the capability
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsmCapabilitySet(lsmStorageCapabilitiesPtr cap, lsmCapabilityType t,
                        lsmCapabilityValueType v);

/**
 * Sets 1 or more capabilities with the same value v
 * @param cap           Valid capability pointer
 * @param v             The value to set capabilities to
 * @param number        Number of Capabilities to set
 * @param ...           Which capabilites to set
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsmCapabilitySetN( lsmStorageCapabilitiesPtr cap,
                                        lsmCapabilityValueType v,
                                        uint32_t number, ... );

/**
 * Allocated storage for capabilities
 * @param value     Set to NULL, used during serialization otherwise.
 * @return Allocated record, or NULL on memory allocation failure.
 */
lsmStorageCapabilitiesPtr LSM_DLL_EXPORT lsmCapabilityRecordAlloc(char const *value);

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_PLUG_INTERFACE_H */

