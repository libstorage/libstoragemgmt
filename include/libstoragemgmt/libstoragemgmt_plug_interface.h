/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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

#include "libstoragemgmt_types.h"
#include "libstoragemgmt_common.h"

#include "libstoragemgmt_accessgroups.h"
#include "libstoragemgmt_blockrange.h"
#include "libstoragemgmt_capabilities.h"
#include "libstoragemgmt_error.h"
#include "libstoragemgmt_fs.h"
#include "libstoragemgmt_initiators.h"
#include "libstoragemgmt_nfsexport.h"
#include "libstoragemgmt_pool.h"
#include "libstoragemgmt_snapshot.h"
#include "libstoragemgmt_systems.h"
#include "libstoragemgmt_volumes.h"

#ifdef  __cplusplus
extern "C" {
#endif

/** @file libstoragemgmt_plug_interface.h */

/** \enum lsmDataType What type of data structure we have */
typedef enum {
    LSM_DATA_TYPE_UNKNOWN = -1,         /**< Unknown */
    LSM_DATA_TYPE_NONE,                 /**< None */
    LSM_DATA_TYPE_ACCESS_GROUP,         /**< Access group */
    LSM_DATA_TYPE_BLOCK_RANGE,          /**< Block range */
    LSM_DATA_TYPE_FS,                   /**< File system */
    LSM_DATA_TYPE_INITIATOR,            /**< Initiator */
    LSM_DATA_TYPE_NFS_EXPORT,           /**< NFS export */
    LSM_DATA_TYPE_POOL,                 /**< Pool */
    LSM_DATA_TYPE_SS,                   /**< Snap shot */
    LSM_DATA_TYPE_STRING_LIST,          /**< String list */
    LSM_DATA_TYPE_SYSTEM,               /**< System */
    LSM_DATA_TYPE_VOLUME                /**< Volume */
} lsmDataType;

/**
 * Opaque data type for plug-ins
 */
typedef struct _lsmPlugin lsmPlugin;

/**
 * Typedef for pointer type
 */
typedef lsmPlugin *lsmPluginPtr;

/**
 * Plug-in register callback function signature.
 * @param   c           Valid lsm plugin pointer
 * @param   uri         Connection URI
 * @param   password    Plain text password
 * @param   timeout     Plug-in timeout to array
 * @param   flags       Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPluginRegister)(  lsmPluginPtr c, xmlURIPtr uri,
                    const char *password, uint32_t timeout, lsmFlag_t flags);

/**
 * Plug-in unregister callback function signature
 * @param   c           Valid lsm plugin pointer
 * @param   flags       Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPluginUnregister)( lsmPluginPtr c, lsmFlag_t flags );

/**
 * Set plug-in time-out value callback function signature
 * @param   c           Valid lsm plug-in pointer
 * @param   timeout     timeout value in milliseconds
 * @param   flags       Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugSetTmo)( lsmPluginPtr c, uint32_t timeout,
                                    lsmFlag_t flags );

/**
 * Get the plug-in time-out value callback function signature
 * @param[in]   c           Valid lsm plug-in pointer
 * @param[out]  timeout     Time-out value
 * @param[in]   flags       Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugGetTmo)( lsmPluginPtr c, uint32_t *timeout,
                                    lsmFlag_t flags );

/**
 * Retrieve the plug-in capabilities callback function signature
 * @param[in]   c           Valid lsm plug-in pointer
 * @param[in]   sys         System to interrogate
 * @param[out]  cap         Capabilities
 * @param[in]   flags       Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugCapabilities)(lsmPluginPtr c, lsmSystem *sys,
                                    lsmStorageCapabilities **cap,
                                    lsmFlag_t flags);
/**
 * Retrieve the job status callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   job             Job identifier
 * @param[out]  status          Enumerated value representing status
 * @param[out]  percentComplete How far completed
 * @param[out]  type            Type of result
 * @param[out]  value           Value of result
 * @param[in]   flags           Reserved
 * @return LSM_ERR_OK, else error reason
 */

typedef int (*lsmPlugJobStatus)(lsmPluginPtr c, const char *job,
                                        lsmJobStatus *status,
                                        uint8_t *percentComplete,
                                        lsmDataType *type,
                                        void **value, lsmFlag_t flags);
/**
 * Instructs the plug-in to release the memory for the specified job id,
 * callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   jobId           Job ID to free memory for
 * @param[in]   flags           Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugJobFree)(lsmPluginPtr c, char *jobId, lsmFlag_t flags);

/**
 * Retrieves a list of pools callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[out]  poolArray       List of pools
 * @param[out]  count           Number of items in array
 * @param[in]   flags           Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugListPools)( lsmPluginPtr c, lsmPool **poolArray[],
                                        uint32_t *count, lsmFlag_t flags);

/**
 * Retrieve a list of systems, callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[out]  systems         List of systems
 * @param[out]  systemCount     Number of systems
 * @param[out]  flags           Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugSystemList)(lsmPluginPtr c, lsmSystem **systems[],
                                        uint32_t *systemCount, lsmFlag_t flags);

/** \struct lsmMgmtOpsV1
 *  \brief Callback functions for management operations */
struct lsmMgmtOpsV1 {
    lsmPlugSetTmo       tmo_set;                /**< tmo set callback */
    lsmPlugGetTmo       tmo_get;                /**< tmo get callback */
    lsmPlugCapabilities capablities;            /**< capabilities callback */
    lsmPlugJobStatus    job_status;             /**< status of job */
    lsmPlugJobFree      job_free;               /**< Free a job */
    lsmPlugListPools    pool_list;              /**< List of pools */
    lsmPlugSystemList   system_list;            /**< List of systems */
};

/**
 * Retrieve a list of initiators, callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[out]  initArray       Array of initiators
 * @param[out]  count           Number of initiators
 * @param[out]  flags           Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugListInits)( lsmPluginPtr c, lsmInitiator **initArray[],
                                        uint32_t *count, lsmFlag_t flags);

/**
 * Retrieve a list of volumes.
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[out]  volArray        Array of volumes
 * @param[out]  count           Number of volumes
 * @param[in]   flags           Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugListVolumes)( lsmPluginPtr c, lsmVolume **volArray[],
                                        uint32_t *count, lsmFlag_t flags);

/**
 * Creates a volume, callback function signature
 * @param[in] c                     Valid lsm plug-in pointer
 * @param[in] pool                  Pool to allocated storage from
 * @param[in] volumeName            Name of new volume
 * @param[in] size                  Size of volume in bytes
 * @param[in] provisioning          How provisioned
 * @param[out] newVolume            Information on newly created volume
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeCreate)(lsmPluginPtr c, lsmPool *pool,
                        const char *volumeName, uint64_t size,
                        lsmProvisionType provisioning, lsmVolume **newVolume,
                        char **job, lsmFlag_t flags);

/**
 * Volume replicate, callback function signature
 * @param[in] c                     Valid lsm plug-in pointer
 * @param[in] pool                  Pool to allocated replicant from (optional)
 * @param[in] repType               Replication type
 * @param[in] volumeSrc             Source of the replication
 * @param[in] name                  Name of newly replicated volume
 * @param[out] newReplicant
 * @param job
 * @param flags
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeReplicate)(lsmPluginPtr c, lsmPool *pool,
                        lsmReplicationType repType, lsmVolume *volumeSrc,
                        const char *name, lsmVolume **newReplicant,
                        char **job, lsmFlag_t flags);

/**
 * Return the block size of a replicated block range.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   system              System to query against
 * @param[out]  bs                  Block size
 * @param[out]  flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeReplicateRangeBlockSize)(lsmPluginPtr c,
                            lsmSystem *system, uint32_t *bs, lsmFlag_t flags);

/**
 * Replicate a range of a volume to the same volume or different volume.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   repType             What type of replication
 * @param[in]   source              Source of the replication
 * @param[in]   dest                Destination of the replication, can be
 *                                  same as source
 * @param[in]   ranges              An array of ranges
 * @param[in]   num_ranges          Number of items in array
 * @param[out]  job                 Job ID
 * @param flags
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeReplicateRange)(lsmPluginPtr c,
                                                lsmReplicationType repType,
                                                lsmVolume *source,
                                                lsmVolume *dest,
                                                lsmBlockRange **ranges,
                                                uint32_t num_ranges, char **job,
                                                lsmFlag_t flags);

/**
 * Re-size a volume, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to be re-sized
 * @param[in]   newSize             New size of volume in bytes
 * @param[in]   resizedVolume       Information about newly re-sized volume
 * @param[out]  job                 The job ID
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeResize)(lsmPluginPtr c, lsmVolume *volume,
                                uint64_t newSize, lsmVolume **resizedVolume,
                                char **job, lsmFlag_t flags);

/**
 * Delete a volume, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to be deleted
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeDelete)(lsmPluginPtr c, lsmVolume *volume,
                                    char **job, lsmFlag_t flags);

/**
 * Grants access to a volume for an initiator, callback function signature.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   i                   Initiator to grant access for
 * @param[in]   v                   Volume of interest
 * @param[in]   access              Requested access
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
int lsmPlugAccessGrant(lsmPluginPtr c, lsmInitiator *i, lsmVolume *v,
                        lsmAccessType access, lsmFlag_t flags);

/**
 * Removes access for an initiator to a volume, callback function signature.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   i                   Initiator to remove access for
 * @param[in]   v                   Volume of interest
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugAccessRemove)(lsmPluginPtr c, lsmInitiator *i,
                lsmVolume *v, lsmFlag_t flags);

/**
 * Check on the status of a volume
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   v                   Volume to retrieve status for
 * @param[out]  status              Status of volume
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeStatus)(lsmPluginPtr c, lsmVolume *v,
                                                lsmVolumeStatusType *status,
                                                lsmFlag_t flags);

/**
 * Place a volume online, callback function signature.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   v                   Volume to place online
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeOnline)(lsmPluginPtr c, lsmVolume *v,
                                    lsmFlag_t flags);

/**
 * Take a volume offline, callback function signature.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param v
 * @param flags
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeOffline)(lsmPluginPtr c, lsmVolume *v,
                                    lsmFlag_t flags);

/**
 * Grants access to an initiator for a specified volume, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   initiator_id        Initiator ID
 * @param[in]   initiator_type      Type of initiator
 * @param[in]   volume              Volume of interest
 * @param[in]   access              Desired access to volume
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugInitiatorGrant)(lsmPluginPtr c, const char *initiator_id,
                                        lsmInitiatorType initiator_type,
                                        lsmVolume *volume,
                                        lsmAccessType access,
                                        lsmFlag_t flags);

/**
 * Revokes access for an initiator, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   init                Initiator to revoke access to
 * @param[in]   volume              Volume of interest
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugInitiatorRevoke)(lsmPluginPtr c, lsmInitiator *init,
                                        lsmVolume *volume,
                                        lsmFlag_t flags);

/**
 * Retrieves an array of initiators that have access to a specified volume
 * , callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to lookup
 * @param[out]  initArray           Array of initiators
 * @param[out]  count               Number of items in array
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugInitiatorsGrantedToVolume)(lsmPluginPtr c,
                                        lsmVolume *volume,
                                        lsmInitiator **initArray[],
                                        uint32_t *count, lsmFlag_t flags);

/**
 * Setup the cap authentication for the specified initiator, callback
 * function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   initiator           Initiator to set chap authentication for
 * @param[in]   in_user             CHAP inbound username
 * @param[in]   in_password         CHAP inbound password
 * @param[in]   out_user            CHAP outbound user name
 * @param[in]   out_password        CHAP outbound user name
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugIscsiChapAuth)(lsmPluginPtr c,
                                                lsmInitiator *initiator,
                                                const char *in_user,
                                                const char *in_password,
                                                const char *out_user,
                                                const char *out_password,
                                                lsmFlag_t flags);

/**
 * Retrieve a list of access groups, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[out]  groups              Array of groups
 * @param[out]  groupCount          Number of groups
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugAccessGroupList)(lsmPluginPtr c,
                                        lsmAccessGroup **groups[],
                                        uint32_t *groupCount, lsmFlag_t flags);
/**
 * Creates an access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   name                Name of access group
 * @param[in]   initiator_id        Initiator to be added to group
 * @param[in]   id_type             Initiator type
 * @param[in]   system_id           System to create group for
 * @param[out]  access_group        Newly created access group
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugAccessGroupCreate)(lsmPluginPtr c,
                                            const char *name,
                                            const char *initiator_id,
                                            lsmInitiatorType id_type,
                                            const char *system_id,
                                            lsmAccessGroup **access_group,
                                            lsmFlag_t flags);

/**
 * Deletes an access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Access group to be deleted
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugAccessGroupDel)(lsmPluginPtr c,
                                            lsmAccessGroup *group, lsmFlag_t flags);

/**
 * Add an initiator to an access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Group to add initiator to
 * @param[in]   initiator_id        Initiator to add to group
 * @param[in]   id_type             Initiator type
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugAccessGroupAddInitiator)(lsmPluginPtr c,
                                lsmAccessGroup *group,
                                const char *initiator_id,
                                lsmInitiatorType id_type, lsmFlag_t flags);

/**
 * Remove an initiator from an access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Group to remove initiator from
 * @param[in]   initiator_id        Initiator to remove
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugAccessGroupDelInitiator)(lsmPluginPtr c,
                                                    lsmAccessGroup *group,
                                                    const char *initiator_id,
                                                    lsmFlag_t flags);

/**
 * Grants access to a volume for the specified access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Group to be granted access
 * @param[in]   volume              Volume to be given access too
 * @param[in]   access              Access type
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugAccessGroupGrant)(lsmPluginPtr c,
                                            lsmAccessGroup *group,
                                            lsmVolume *volume,
                                            lsmAccessType access, lsmFlag_t flags);

/**
 * Revokes access to a volume for a specified access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Group to revoke access for
 * @param[in]   volume              Volume to which will no longer be accessible by group
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugAccessGroupRevoke)(lsmPluginPtr c,
                                            lsmAccessGroup *group,
                                            lsmVolume *volume, lsmFlag_t flags);

/**
 * Retrieve an array of volumes which are accessible by access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Group to find volumes for
 * @param[out]  volumes             Array of volumes
 * @param[out]  count               Number of volumes
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumesAccessibleByAccessGroup)(lsmPluginPtr c,
                                                        lsmAccessGroup *group,
                                                        lsmVolume **volumes[],
                                                        uint32_t *count, lsmFlag_t flags);

/**
 * Retrieve an array of volumes accessible by an initiator, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   initiator           Initiator to find volumes for
 * @param[out]  volumes             Array of volumes
 * @param[out]  count               Number of volumes
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumesAccessibleByInitiator)(lsmPluginPtr c,
                                                        lsmInitiator * initiator,
                                                        lsmVolume **volumes[],
                                                        uint32_t *count, lsmFlag_t flags);

/**
 * Retrieve a list of access groups that have access to the specified volume,
 * callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to query
 * @param[out]  groups              Array of access groups
 * @param[out]  groupCount          Number of access groups
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
 typedef int (*lsmPlugAccessGroupsGrantedToVolume)(lsmPluginPtr c,
                                                    lsmVolume *volume,
                                                    lsmAccessGroup **groups[],
                                                    uint32_t *groupCount, lsmFlag_t flags);

/**
 * Determine if a volume has child dependencies, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to query
 * @param[out]  yes                 Boolean
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeChildDependency)(lsmPluginPtr c,
                                            lsmVolume *volume,
                                            uint8_t *yes, lsmFlag_t flags);

/**
 * Remove dependencies from a volume, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to remove dependency for
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugVolumeChildDependencyRm)(lsmPluginPtr c,
                                            lsmVolume *volume,
                                            char **job, lsmFlag_t flags);

/**
 * File system list, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[out]  fs                  An array of file systems
 * @param[out]  fsCount             Number of file systems
 * @param[in] flags                 Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugFsList)(lsmPluginPtr c, lsmFs **fs[],
                                    uint32_t *fsCount, lsmFlag_t flags);

/**
 * Create a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   pool                Pool to create file system from
 * @param[in]   name                Name of file system
 * @param[in]   size_bytes          Size of the file system in bytes
 * @param[out]  fs                  Newly created file system
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugFsCreate)(lsmPluginPtr c, lsmPool *pool,
                                    const char *name, uint64_t size_bytes,
                                    lsmFs **fs, char **job, lsmFlag_t flags);

/**
 * Delete a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to delete
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugFsDelete)(lsmPluginPtr c, lsmFs *fs, char **job, lsmFlag_t flags);

/**
 * Clone a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   dest_fs_name        Clone fs name
 * @param[out]  cloned_fs           New clone
 * @param[in]   optional_snapshot   Basis of clone
 * @param[out]  job                 Job ID
 * @param[in]   flags               reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugFsClone)(lsmPluginPtr c, lsmFs *src_fs,
                                            const char *dest_fs_name,
                                            lsmFs **cloned_fs,
                                            lsmSs *optional_snapshot,
                                            char **job, lsmFlag_t flags);
/**
 * Determine if a file system has child dependencies, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to check
 * @param[out]  yes                 Boolean
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugFsChildDependency)(lsmPluginPtr c, lsmFs *fs,
                                                lsmStringList *files,
                                                uint8_t *yes);

/**
 * Remove dependencies from a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to remove dependencies for
 * @param[out]  job                 Job ID
 * @param[out]  flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugFsChildDependencyRm)( lsmPluginPtr c, lsmFs *fs,
                                                lsmStringList *files,
                                                char **job, lsmFlag_t flags);

/**
 * Re-size a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to re-size
 * @param[in]   new_size_bytes      New size of file system
 * @param[out]  rfs                 Re-sized file system
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugFsResize)(lsmPluginPtr c, lsmFs *fs,
                                    uint64_t new_size_bytes, lsmFs **rfs,
                                    char **job, lsmFlag_t flags);

/**
 * Clone an individual file on a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system which contains the file to clone
 * @param[in]   src_file_name       Source file name and path
 * @param[in]   dest_file_name      Destination file and path
 * @param[in]   snapshot            Optional backing snapshot
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugFsFileClone)(lsmPluginPtr c, lsmFs *fs,
                                    const char *src_file_name,
                                    const char *dest_file_name,
                                    lsmSs *snapshot, char **job, lsmFlag_t flags);

/**
 * Retrieve a list of snapshots for a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system
 * @param[out]  ss                  Array of snap shots
 * @param[out]  ssCount             Count of snapshots
 * @param[in]   flags               Reserved
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugSsList)(lsmPluginPtr c, lsmFs *fs, lsmSs **ss[],
                                uint32_t *ssCount, lsmFlag_t flags);

/**
 * Create a snapshot of the specified file system and optionally constrain it to
 * a list of files, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to create snapshot for
 * @param[in]   name                Snap shot name
 * @param[in]   files               Optional list of files to specifically snapshot
 * @param[out]  snapshot            Newly created snapshot
 * @param[out]  job                 Job ID
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugSsCreate)(lsmPluginPtr c, lsmFs *fs,
                                    const char *name, lsmStringList *files,
                                    lsmSs **snapshot, char **job, lsmFlag_t flags);
/**
 * Delete a snapshot, callback function signature, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to delete snapshot for
 * @param[in]   ss                  Snapshot to delete
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugSsDelete)(lsmPluginPtr c, lsmFs *fs, lsmSs *ss,
                                    char **job, lsmFlag_t flags);

/**
 * Revert the state of a file system or specific files to a previous state,
 * callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system of interest
 * @param[in]   files               Optional list of files
 * @param[in]   restore_files       Optional path and name of restored files
 * @param[in]   all_files           boolean to indicate all files should be reverted
 * @param[out]  job                 Job ID
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugSsRevert)(lsmPluginPtr c, lsmFs *fs, lsmSs *ss,
                                    lsmStringList *files,
                                    lsmStringList *restore_files,
                                    int all_files, char **job, lsmFlag_t flags);

/**
 * Get a list of NFS client authentication types, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[out]  types               List of authtication types
 * @param[in]   flags               Reserved
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugNfsAuthTypes)( lsmPluginPtr c,
                                            lsmStringList **types, lsmFlag_t flags);

/**
 * Retrieve a list of NFS exports, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[out]  exports             An array of exported file systems
 * @param[out]  count               Number of exported file systems
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugNfsList)( lsmPluginPtr c,
                                            lsmNfsExport **exports[],
                                            uint32_t *count, lsmFlag_t flags);
/**
 * Exports a file system via NFS, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs_id               File system id to export
 * @param[in]   export_path         NFS export path
 * @param[in]   root_list           List of servers with root access
 * @param[in]   rw_list             List of servers with read/write access
 * @param[in]   ro_list             List of servers with read only access
 * @param[in]   anon_uid            UID to be mapped to anonymous
 * @param[in]   anon_gid            GID to be mapped to anonymous
 * @param[in]   auth_type           Client authentication type
 * @param[in]   options             Options
 * @param[out]  exported            Newly created export
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugNfsExportFs)( lsmPluginPtr c,
                                        const char *fs_id,
                                        const char *export_path,
                                        lsmStringList *root_list,
                                        lsmStringList *rw_list,
                                        lsmStringList *ro_list,
                                        uint64_t anon_uid,
                                        uint64_t anon_gid,
                                        const char *auth_type,
                                        const char *options,
                                        lsmNfsExport **exported,
                                        lsmFlag_t flags
                                        );

/**
 * Removes a NFS export, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   e                   Export to remove
 * @param[in]   flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsmPlugNfsExportRemove)( lsmPluginPtr c, lsmNfsExport *e,
                                        lsmFlag_t flags);
/** \struct lsmSanOpsV1
 *  \brief Block array oriented functions (callback functions)
 */
struct lsmSanOpsV1 {
    lsmPlugListInits init_get;              /**<  retrieving initiators */
    lsmPlugListVolumes vol_get;             /**<  retrieving volumes */
    lsmPlugVolumeCreate vol_create;         /**<  creating a lun */
    lsmPlugVolumeReplicate vol_replicate;   /**<  replicating lun */
    lsmPlugVolumeReplicateRangeBlockSize vol_rep_range_bs;  /**<  volume replication range block size */
    lsmPlugVolumeReplicateRange vol_rep_range;              /**<  volume replication range */
    lsmPlugVolumeResize vol_resize;         /**<  resizing a volume */
    lsmPlugVolumeDelete vol_delete;         /**<  deleting a volume */
    lsmPlugVolumeOnline vol_online;         /**<  bringing volume online */
    lsmPlugVolumeOffline vol_offline;       /**<  bringing volume offline */
    lsmPlugInitiatorGrant initiator_grant;      /**<  granting access */
    lsmPlugInitiatorRevoke initiator_revoke;    /**<  revoking access */
    lsmPlugInitiatorsGrantedToVolume initiators_granted_to_vol;     /**<  initiators granted to a volume */
    lsmPlugIscsiChapAuth iscsi_chap_auth;            /**<  iscsi chap authentication */
    lsmPlugAccessGroupList ag_list;     /**<  access groups */
    lsmPlugAccessGroupCreate ag_create; /**<  access group create */
    lsmPlugAccessGroupDel ag_delete;    /**<  access group delete */
    lsmPlugAccessGroupAddInitiator ag_add_initiator;    /**<  adding an initiator to an access group */
    lsmPlugAccessGroupDelInitiator ag_del_initiator;    /**<  deleting an initiator from an access group */
    lsmPlugAccessGroupGrant ag_grant;   /**<  acess group grant */
    lsmPlugAccessGroupRevoke ag_revoke; /**<  access group revoke */
    lsmPlugVolumesAccessibleByAccessGroup vol_accessible_by_ag; /**<  volumes accessible by access group */
    lsmPlugVolumesAccessibleByInitiator vol_accessible_by_init; /**<  volumes accessible by initiator */
    lsmPlugAccessGroupsGrantedToVolume ag_granted_to_vol;       /**<  access groups granted to a volume */
    lsmPlugVolumeChildDependency vol_child_depends;         /**<  volume child dependencies */
    lsmPlugVolumeChildDependencyRm vol_child_depends_rm;    /**<Callback to remove volume child dependencies */
};

/** \struct  lsmFsOpsV1
 *  \brief File system oriented functionality
 */
struct lsmFsOpsV1 {
    lsmPlugFsList   fs_list;        /**< list file systems */
    lsmPlugFsCreate fs_create;      /**< create a file system */
    lsmPlugFsDelete fs_delete;      /**< delete a file system */
    lsmPlugFsResize fs_resize;      /**< resize a file system */
    lsmPlugFsClone  fs_clone;       /**< clone a file system */
    lsmPlugFsFileClone fs_file_clone;   /**< clone files on a file system */
    lsmPlugFsChildDependency fs_child_dependency;       /**< check file system child dependencies */
    lsmPlugFsChildDependencyRm fs_child_dependency_rm;  /**< remove file system child dependencies */
    lsmPlugSsList ss_list;          /**< list snapshots */
    lsmPlugSsCreate ss_create;      /**< create a snapshot */
    lsmPlugSsDelete ss_delete;      /**< delete a snapshot */
    lsmPlugSsRevert ss_revert;      /**< revert a snapshot */
};

/** \struct lsmNasOpsV1
 * \brief NAS system oriented functionality call back functions
 */
struct lsmNasOpsV1 {
    lsmPlugNfsAuthTypes nfs_auth_types;     /**< List nfs authentication types */
    lsmPlugNfsList nfs_list;                /**< List nfs exports */
    lsmPlugNfsExportFs nfs_export;          /**< Export a file system */
    lsmPlugNfsExportRemove nfs_export_remove;   /**< Remove a file export */
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
                        void * private_data, struct lsmMgmtOpsV1 *mgmOps,
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
 * Helper function to create an array of lsmPool *
 * @param size  Number of elements
 * @return Valid pointer or NULL on error.
 */
lsmPool LSM_DLL_EXPORT **lsmPoolRecordAllocArray( uint32_t size );

/**
 * Used to set the free space on a pool record
 * @param p                 Pool to modify
 * @param free_space        New free space value
 */
void LSM_DLL_EXPORT lsmPoolFreeSpaceSet(lsmPool *p, uint64_t free_space);

/**
 * Helper function to allocate a pool record.
 * @param id            System unique identifier
 * @param name          Human readable name
 * @param totalSpace    Total space
 * @param freeSpace     Space available
 * @param system_id     System id
 * @return LSM_ERR_OK on success, else error reason.
 */
lsmPool LSM_DLL_EXPORT *lsmPoolRecordAlloc(const char *id, const char *name,
                                uint64_t totalSpace,
                                uint64_t freeSpace,
                                const char *system_id);

/**
 * Allocate the storage needed for and array of Initiator records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsmInitiator LSM_DLL_EXPORT **lsmInitiatorRecordAllocArray( uint32_t size );

/**
 * Allocate the storage needed for one initiator record.
 * @param idType    Type of initiator.
 * @param id        ID of initiator.
 * @param name      Name of initiator
 * @return Allocated memory or NULL on error.
 */
lsmInitiator LSM_DLL_EXPORT *lsmInitiatorRecordAlloc( lsmInitiatorType idType,
                                                        const char* id,
                                                        const char* name);

/**
 * Allocate the storage needed for and array of Volume records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsmVolume LSM_DLL_EXPORT **lsmVolumeRecordAllocArray( uint32_t size);

/**
 * Allocated the storage needed for one volume record.
 * @param id                    ID
 * @param name                  Name
 * @param vpd83                 SCSI vpd 83 id
 * @param blockSize             Volume block size
 * @param numberOfBlocks        Volume number of blocks
 * @param status                Volume status
 * @param system_id             System id
 * @param pool_id               Pool id this volume is created from
 * @return Allocated memory or NULL on error.
 */
lsmVolume LSM_DLL_EXPORT *lsmVolumeRecordAlloc( const char *id,
                                        const char *name, const char *vpd83,
                                        uint64_t blockSize,
                                        uint64_t numberOfBlocks,
                                        uint32_t status,
                                        const char *system_id,
                                        const char *pool_id);

/**
 * Allocate the storage needed for and array of System records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsmSystem LSM_DLL_EXPORT **lsmSystemRecordAllocArray( uint32_t size );

/**
 * Allocates the storage for one system record.
 * @param[in] id        Id
 * @param[in] name      System name (human readable)
 * @param[in] status    Status of the system
 * @return  Allocated memory or NULL on error.
 */
lsmSystem LSM_DLL_EXPORT *lsmSystemRecordAlloc( const char *id,
                                                  const char *name,
                                                  uint32_t status );

/**
 * Allocates storage for AccessGroup array
 * @param size      Number of elements to store.
 * @return  NULL on error, else pointer to array for use.
 */
lsmAccessGroup LSM_DLL_EXPORT **lsmAccessGroupRecordAllocArray( uint32_t size);


/**
 * Allocates storage for single AccessGroup
 * @param id                ID of access group
 * @param name              Name of access group
 * @param initiators        List of initiators, can be NULL
 * @param system_id         System id
 * @return NULL on error, else valid AccessGroup pointer.
 */
lsmAccessGroup LSM_DLL_EXPORT * lsmAccessGroupRecordAlloc(const char *id,
                                                     const char *name,
                                                     lsmStringList *initiators,
                                                     const char *system_id);

/**
 * Allocates memory for a file system record
 * @param id                    ID of file system
 * @param name                  Name of file system
 * @param total_space           Total space
 * @param free_space            Free space
 * @param pool_id               Pool id
 * @param system_id             System id
 * @return lsmFs, NULL on error
 */
lsmFs LSM_DLL_EXPORT *lsmFsRecordAlloc( const char *id, const char *name,
                                            uint64_t total_space,
                                            uint64_t free_space,
                                            const char *pool_id,
                                            const char *system_id);

/**
 * Allocates the memory for the array of file system records.
 * @param size      Number of elements
 * @return Allocated memory, NULL on error
 */
lsmFs LSM_DLL_EXPORT **lsmFsRecordAllocArray( uint32_t size );

/**
 * Allocates the memory for single snap shot record.
 * @param id            ID
 * @param name          Name
 * @param ts            Epoch time stamp when snapshot was created
 * @return Allocated memory, NULL on error
 */
lsmSs LSM_DLL_EXPORT *lsmSsRecordAlloc( const char *id, const char *name,
                                            uint64_t ts);

/**
 * Allocates the memory for an array of snapshot records.
 * @param size          Number of elements
 * @return Allocated memory, NULL on error
 */
lsmSs LSM_DLL_EXPORT **lsmSsRecordAllocArray( uint32_t size );

/**
 * Set a capability
 * @param cap           Valid capability pointer
 * @param t             Which capability to set
 * @param v             Value of the capability
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsmCapabilitySet(lsmStorageCapabilities *cap, lsmCapabilityType t,
                        lsmCapabilityValueType v);

/**
 * Sets 1 or more capabilities with the same value v
 * @param cap           Valid capability pointer
 * @param v             The value to set capabilities to
 * @param number        Number of Capabilities to set
 * @param ...           Which capabilites to set
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsmCapabilitySetN( lsmStorageCapabilities *cap,
                                        lsmCapabilityValueType v,
                                        uint32_t number, ... );

/**
 * Allocated storage for capabilities
 * @param value     Set to NULL, used during serialization otherwise.
 * @return Allocated record, or NULL on memory allocation failure.
 */
lsmStorageCapabilities LSM_DLL_EXPORT *lsmCapabilityRecordAlloc(char const *value);

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_PLUG_INTERFACE_H */

