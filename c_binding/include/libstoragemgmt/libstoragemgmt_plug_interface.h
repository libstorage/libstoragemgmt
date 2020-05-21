/*
 * Copyright (C) 2011-2016 Red Hat, Inc.
 * (C) Copyright 2015-2016 Hewlett Packard Enterprise Development LP
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
 *         Joe Handzik <joseph.t.handzik@hpe.com>
 *         Gris Ge <fge@redhat.com>
 */

#ifndef LIBSTORAGEMGMT_PLUG_INTERFACE_H
#define LIBSTORAGEMGMT_PLUG_INTERFACE_H

#include "libstoragemgmt_common.h"

#include "libstoragemgmt_accessgroups.h"
#include "libstoragemgmt_battery.h"
#include "libstoragemgmt_blockrange.h"
#include "libstoragemgmt_capabilities.h"
#include "libstoragemgmt_disk.h"
#include "libstoragemgmt_error.h"
#include "libstoragemgmt_fs.h"
#include "libstoragemgmt_hash.h"
#include "libstoragemgmt_nfsexport.h"
#include "libstoragemgmt_pool.h"
#include "libstoragemgmt_snapshot.h"
#include "libstoragemgmt_systems.h"
#include "libstoragemgmt_volumes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @file libstoragemgmt_plug_interface.h */

/** \enum lsm_data_type What type of data structure we have */
typedef enum {
    LSM_DATA_TYPE_UNKNOWN = -1, /**< Unknown */
    LSM_DATA_TYPE_NONE,         /**< None */
    LSM_DATA_TYPE_ACCESS_GROUP, /**< Access group */
    LSM_DATA_TYPE_BLOCK_RANGE,  /**< Block range */
    LSM_DATA_TYPE_FS,           /**< File system */
    LSM_DATA_TYPE_NFS_EXPORT,   /**< NFS export */
    LSM_DATA_TYPE_POOL,         /**< Pool */
    LSM_DATA_TYPE_SS,           /**< Snap shot */
    LSM_DATA_TYPE_STRING_LIST,  /**< String list */
    LSM_DATA_TYPE_SYSTEM,       /**< System */
    LSM_DATA_TYPE_VOLUME,       /**< Volume */
    LSM_DATA_TYPE_DISK          /**< Disk */
} lsm_data_type;

/**
 * Opaque data type for plug-ins
 */
typedef struct _lsm_plugin lsm_plugin;

/**
 * Typedef for pointer type
 */
typedef lsm_plugin *lsm_plugin_ptr;

/**
 * Plug-in register callback function signature.
 * @param   c           Valid lsm plugin pointer
 * @param   uri         Connection URI
 * @param   password    Plain text password
 * @param   timeout     Plug-in timeout to array
 * @param   flags       Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plugin_register)(lsm_plugin_ptr c, const char *uri,
                                   const char *password, uint32_t timeout,
                                   lsm_flag flags);

/**
 * Plug-in unregister callback function signature
 * @param   c           Valid lsm plugin pointer
 * @param   flags       Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plugin_unregister)(lsm_plugin_ptr c, lsm_flag flags);

/**
 * Set plug-in time-out value callback function signature
 * @param   c           Valid lsm plug-in pointer
 * @param   timeout     timeout value in milliseconds
 * @param   flags       Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_tmo_set)(lsm_plugin_ptr c, uint32_t timeout,
                                lsm_flag flags);

/**
 * Get the plug-in time-out value callback function signature
 * @param[in]   c           Valid lsm plug-in pointer
 * @param[out]  timeout     Time-out value
 * @param[in]   flags       Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_tmo_get)(lsm_plugin_ptr c, uint32_t *timeout,
                                lsm_flag flags);

/**
 * Retrieve the plug-in capabilities callback function signature
 * @param[in]   c           Valid lsm plug-in pointer
 * @param[in]   sys         System to interrogate
 * @param[out]  cap         Capabilities
 * @param[in]   flags       Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_capabilities)(lsm_plugin_ptr c, lsm_system *sys,
                                     lsm_storage_capabilities **cap,
                                     lsm_flag flags);
/**
 * Retrieve the job status callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   job             Job identifier
 * @param[out]  status          Enumerated value representing status
 * @param[out]  percent_complete    How far completed
 * @param[out]  type            Type of result
 * @param[out]  value           Value of result
 * @param[in]   flags           Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */

typedef int (*lsm_plug_Job_status)(lsm_plugin_ptr c, const char *job,
                                   lsm_job_status *status,
                                   uint8_t *percent_complete,
                                   lsm_data_type *type, void **value,
                                   lsm_flag flags);
/**
 * Instructs the plug-in to release the memory for the specified job id,
 * callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   job_id          Job ID to free memory for
 * @param[in]   flags           Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_job_free)(lsm_plugin_ptr c, char *job_id,
                                 lsm_flag flags);

/**
 * Retrieves a list of pools callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   search_key      Search key
 * @param[in]   search_value    Search value
 * @param[out]  pool_array      List of pools
 * @param[out]  count           Number of items in array
 * @param[in]   flags           Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_pool_list)(lsm_plugin_ptr c, const char *search_key,
                                  const char *search_value,
                                  lsm_pool **pool_array[], uint32_t *count,
                                  lsm_flag flags);

/**
 * Retrieve a list of systems, callback function signature
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[out]  systems         List of systems
 * @param[out]  system_count     Number of systems
 * @param[out]  flags           Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_system_list)(lsm_plugin_ptr c, lsm_system **systems[],
                                    uint32_t *system_count, lsm_flag flags);

/** \struct lsm_mgmt_ops_v1
 *  \brief Callback functions for management operations */
struct lsm_mgmt_ops_v1 {
    lsm_plug_tmo_set tmo_set;          /**< tmo set callback */
    lsm_plug_tmo_get tmo_get;          /**< tmo get callback */
    lsm_plug_capabilities capablities; /**< capabilities callback */
    lsm_plug_Job_status job_status;    /**< status of job */
    lsm_plug_job_free job_free;        /**< Free a job */
    lsm_plug_pool_list pool_list;      /**< List of pools */
    lsm_plug_system_list system_list;  /**< List of systems */
};

/**
 * Retrieve a list of volumes.
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   search_key      Search key
 * @param[in]   search_value    Search value
 * @param[out]  vol_array        Array of volumes
 * @param[out]  count           Number of volumes
 * @param[in]   flags           Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_list)(lsm_plugin_ptr c, const char *search_key,
                                    const char *search_val,
                                    lsm_volume **vol_array[], uint32_t *count,
                                    lsm_flag flags);

/**
 * Retrieve a list of volumes.
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   search_key      Search key
 * @param[in]   search_value    Search value
 * @param[out]  disk_array       Array of disk pointers
 * @param[out]  count           Number of disks
 * @param[in]   flags           Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_disk_list)(lsm_plugin_ptr c, const char *search_key,
                                  const char *search_value,
                                  lsm_disk **disk_array[], uint32_t *count,
                                  lsm_flag flags);

/**
 * Retrieve a list of target ports.
 * @param[in]   c                   Valid lsm plugin-in pointer
 * @param[in]   search_key          Search key
 * @param[in]   search_value        Search value
 * @param[out]  target_port_array   Array of target port pointers
 * @param[out]  count               Number of target ports
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_target_port_list)(lsm_plugin_ptr c,
                                         const char *search_key,
                                         const char *search_value,
                                         lsm_target_port **target_port_array[],
                                         uint32_t *count, lsm_flag flags);

/**
 * Creates a volume, callback function signature
 * @param[in] c                     Valid lsm plug-in pointer
 * @param[in] pool                  Pool to allocated storage from
 * @param[in] volume_name           Name of new volume
 * @param[in] size                  Size of volume in bytes
 * @param[in] provisioning          How provisioned
 * @param[out] new_volume           Information on newly created volume
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_create)(lsm_plugin_ptr c, lsm_pool *pool,
                                      const char *volume_name, uint64_t size,
                                      lsm_volume_provision_type provisioning,
                                      lsm_volume **new_volume, char **job,
                                      lsm_flag flags);

/**
 * Volume replicate, callback function signature
 * @param[in] c                     Valid lsm plug-in pointer
 * @param[in] pool                  Pool to allocated replicant from (optional)
 * @param[in] rep_type              Replication type
 * @param[in] volume_src            Source of the replication
 * @param[in] name                  Name of newly replicated volume
 * @param[out] new_replicant        Newly replicated volume
 * @param job
 * @param flags
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_replicate)(lsm_plugin_ptr c, lsm_pool *pool,
                                         lsm_replication_type rep_type,
                                         lsm_volume *volume_src,
                                         const char *name,
                                         lsm_volume **new_replicant, char **job,
                                         lsm_flag flags);

/**
 * Return the block size of a replicated block range.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   system              System to query against
 * @param[out]  bs                  Block size
 * @param[out]  flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_replicate_range_block_size)(lsm_plugin_ptr c,
                                                          lsm_system *system,
                                                          uint32_t *bs,
                                                          lsm_flag flags);

/**
 * Replicate a range of a volume to the same volume or different volume.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   rep_type             What type of replication
 * @param[in]   source              Source of the replication
 * @param[in]   dest                Destination of the replication, can be
 *                                  same as source
 * @param[in]   ranges              An array of ranges
 * @param[in]   num_ranges          Number of items in array
 * @param[out]  job                 Job ID
 * @param flags
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_replicate_range)(
    lsm_plugin_ptr c, lsm_replication_type rep_type, lsm_volume *source,
    lsm_volume *dest, lsm_block_range **ranges, uint32_t num_ranges, char **job,
    lsm_flag flags);

/**
 * Re-size a volume, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to be re-sized
 * @param[in]   new_size            New size of volume in bytes
 * @param[in]   resized_volume      Information about newly re-sized volume
 * @param[out]  job                 The job ID
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_resize)(lsm_plugin_ptr c, lsm_volume *volume,
                                      uint64_t new_size,
                                      lsm_volume **resized_volume, char **job,
                                      lsm_flag flags);

/**
 * Delete a volume, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to be deleted
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_delete)(lsm_plugin_ptr c, lsm_volume *volume,
                                      char **job, lsm_flag flags);
/**
 * Place a volume online, callback function signature.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   v                   Volume to place online
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_enable)(lsm_plugin_ptr c, lsm_volume *v,
                                      lsm_flag flags);

/**
 * Take a volume offline, callback function signature.
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param v
 * @param flags
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_disable)(lsm_plugin_ptr c, lsm_volume *v,
                                       lsm_flag flags);

/**
 * Setup the cap authentication for the specified initiator, callback
 * function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   init_id             Initiator to set chap authentication for
 * @param[in]   in_user             CHAP inbound username
 * @param[in]   in_password         CHAP inbound password
 * @param[in]   out_user            CHAP outbound user name
 * @param[in]   out_password        CHAP outbound user name
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_iscsi_chap_auth)(lsm_plugin_ptr c, const char *init_id,
                                        const char *in_user,
                                        const char *in_password,
                                        const char *out_user,
                                        const char *out_password,
                                        lsm_flag flags);

/**
 * Retrieve a list of access groups, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   search_key          Field to search on
 * @param[in]   search_value        Field value
 * @param[out]  groups              Array of groups
 * @param[out]  group_count          Number of groups
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_access_group_list)(
    lsm_plugin_ptr c, const char *search_key, const char *search_value,
    lsm_access_group **groups[], uint32_t *group_count, lsm_flag flags);
/**
 * Creates an access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   name                Name of access group
 * @param[in]   initiator_id        Initiator to be added to group
 * @param[in]   id_type             Initiator type
 * @param[in]   system              System to create group for
 * @param[out]  access_group        Newly created access group
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_access_group_create)(
    lsm_plugin_ptr c, const char *name, const char *initiator_id,
    lsm_access_group_init_type init_type, lsm_system *system,
    lsm_access_group **access_group, lsm_flag flags);

/**
 * Deletes an access group, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Access group to be deleted
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_access_group_delete)(lsm_plugin_ptr c,
                                            lsm_access_group *group,
                                            lsm_flag flags);

/**
 * Add an initiator to an access group, callback function signature
 * @param[in]   c                       Valid lsm plug-in pointer
 * @param[in]   access_group            Group to add initiator to
 * @param[in]   initiator_id            Initiator to add to group
 * @param[in]   id_type                 Initiator type
 * @param[out]  updated_access_group    Updated access group
 * @param[in]   flags                   Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_access_group_initiator_add)(
    lsm_plugin_ptr c, lsm_access_group *access_group, const char *initiator_id,
    lsm_access_group_init_type id_type, lsm_access_group **updated_access_group,
    lsm_flag flags);

/**
 * Remove an initiator from an access group, callback function signature
 * @param[in]   c                       Valid lsm plug-in pointer
 * @param[in]   access_group            Group to remove initiator from
 * @param[in]   initiator_id            Initiator to remove
 * @param[in]   id_type                 Initiator type
 * @param[out]  updated_access_group    Updated access group
 * @param[in]   flags                   Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_access_group_initiator_delete)(
    lsm_plugin_ptr c, lsm_access_group *access_group, const char *initiator_id,
    lsm_access_group_init_type id_type, lsm_access_group **updated_access_group,
    lsm_flag flags);

/**
 * Grants access to a volume for the specified access group, callback function
 * signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Group to be granted access
 * @param[in]   volume              Volume to be given access too
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_mask)(lsm_plugin_ptr c, lsm_access_group *group,
                                    lsm_volume *volume, lsm_flag flags);

/**
 * Revokes access to a volume for a specified access group, callback function
 * signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Group to revoke access for
 * @param[in]   volume              Volume to which will no longer be
 *                                  accessible by group
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_unmask)(lsm_plugin_ptr c, lsm_access_group *group,
                                      lsm_volume *volume, lsm_flag flags);

/**
 * Retrieve an array of volumes which are accessible by access group, callback
 * function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   group               Group to find volumes for
 * @param[out]  volumes             Array of volumes
 * @param[out]  count               Number of volumes
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volumes_accessible_by_access_group)(
    lsm_plugin_ptr c, lsm_access_group *group, lsm_volume **volumes[],
    uint32_t *count, lsm_flag flags);

/**
 * Retrieve a list of access groups that have access to the specified volume,
 * callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to query
 * @param[out]  groups              Array of access groups
 * @param[out]  group_count          Number of access groups
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_access_groups_granted_to_volume)(
    lsm_plugin_ptr c, lsm_volume *volume, lsm_access_group **groups[],
    uint32_t *group_count, lsm_flag flags);

/**
 * Determine if a volume has child dependencies, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to query
 * @param[out]  yes                 Boolean
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_child_dependency)(lsm_plugin_ptr c,
                                                lsm_volume *volume,
                                                uint8_t *yes, lsm_flag flags);

/**
 * Remove dependencies from a volume, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   volume              Volume to remove dependency for
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_child_dependency_delete)(lsm_plugin_ptr c,
                                                       lsm_volume *volume,
                                                       char **job,
                                                       lsm_flag flags);

/**
 * File system list, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   search_key          Search key
 * @param[in]   search_value        Search value
 * @param[out]  fs                  An array of file systems
 * @param[out]  fs_count             Number of file systems
 * @param[in] flags                 Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_fs_list)(lsm_plugin_ptr c, const char *search_key,
                                const char *search_value, lsm_fs **fs[],
                                uint32_t *fs_count, lsm_flag flags);

/**
 * Create a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   pool                Pool to create file system from
 * @param[in]   name                Name of file system
 * @param[in]   size_bytes          Size of the file system in bytes
 * @param[out]  fs                  Newly created file system
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_fs_create)(lsm_plugin_ptr c, lsm_pool *pool,
                                  const char *name, uint64_t size_bytes,
                                  lsm_fs **fs, char **job, lsm_flag flags);

/**
 * Delete a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to delete
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_fs_delete)(lsm_plugin_ptr c, lsm_fs *fs, char **job,
                                  lsm_flag flags);

/**
 * Clone a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   dest_fs_name        Clone fs name
 * @param[out]  cloned_fs           New clone
 * @param[in]   optional_snapshot   Basis of clone
 * @param[out]  job                 Job ID
 * @param[in]   flags               reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_fs_clone)(lsm_plugin_ptr c, lsm_fs *src_fs,
                                 const char *dest_fs_name, lsm_fs **cloned_fs,
                                 lsm_fs_ss *optional_snapshot, char **job,
                                 lsm_flag flags);
/**
 * Determine if a file system has child dependencies, callback function
 * signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to check
 * @param[in]   files               Specific files to check
 * @param[out]  yes                 Boolean
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_fs_child_dependency)(lsm_plugin_ptr c, lsm_fs *fs,
                                            lsm_string_list *files,
                                            uint8_t *yes);

/**
 * Remove dependencies from a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to remove dependencies for
 * @param[in]   files               Specific files to remove dependencies for
 * @param[out]  job                 Job ID
 * @param[out]  flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_fs_child_dependency_delete)(lsm_plugin_ptr c, lsm_fs *fs,
                                                   lsm_string_list *files,
                                                   char **job, lsm_flag flags);

/**
 * Re-size a file system, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to re-size
 * @param[in]   new_size_bytes      New size of file system
 * @param[out]  rfs                 Re-sized file system
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_fs_resize)(lsm_plugin_ptr c, lsm_fs *fs,
                                  uint64_t new_size_bytes, lsm_fs **rfs,
                                  char **job, lsm_flag flags);

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
typedef int (*lsm_plug_fs_file_clone)(lsm_plugin_ptr c, lsm_fs *fs,
                                      const char *src_file_name,
                                      const char *dest_file_name,
                                      lsm_fs_ss *snapshot, char **job,
                                      lsm_flag flags);

/**
 * Retrieve a list of fs snapshots for a file system, callback function
 * signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system
 * @param[out]  ss                  Array of snap shots
 * @param[out]  ss_count             Count of snapshots
 * @param[in]   flags               Reserved
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_fs_ss_list)(lsm_plugin_ptr c, lsm_fs *fs,
                                   lsm_fs_ss **ss[], uint32_t *ss_count,
                                   lsm_flag flags);

/**
 * Create a fs snapshot of the specified file system and optionally constrain
 * it to a list of files, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to create snapshot for
 * @param[in]   name                Snap shot name
 * @param[out]  snapshot            Newly created snapshot
 * @param[out]  job                 Job ID
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_fs_ss_create)(lsm_plugin_ptr c, lsm_fs *fs,
                                     const char *name, lsm_fs_ss **snapshot,
                                     char **job, lsm_flag flags);
/**
 * Delete a fs snapshot, callback function signature, callback function
 * signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system to delete snapshot for
 * @param[in]   ss                  Snapshot to delete
 * @param[out]  job                 Job ID
 * @param[in]   flags               Reserved
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_fs_ss_delete)(lsm_plugin_ptr c, lsm_fs *fs,
                                     lsm_fs_ss *ss, char **job, lsm_flag flags);

/**
 * Revert the state of a file system or specific files to a previous state,
 * callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   fs                  File system of interest
 * @param[in]   files               Optional list of files
 * @param[in]   restore_files       Optional path and name of restored files
 * @param[in]   all_files           boolean to indicate all files should be
 *                                  restored
 * @param[out]  job                 Job ID
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_fs_ss_restore)(lsm_plugin_ptr c, lsm_fs *fs,
                                      lsm_fs_ss *ss, lsm_string_list *files,
                                      lsm_string_list *restore_files,
                                      int all_files, char **job,
                                      lsm_flag flags);

/**
 * Get a list of NFS client authentication types, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[out]  types               List of authtication types
 * @param[in]   flags               Reserved
 * @return  LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_nfs_auth_types)(lsm_plugin_ptr c,
                                       lsm_string_list **types, lsm_flag flags);

/**
 * Retrieve a list of NFS exports, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   search_key          Search key
 * @param[in]   search_value        Search value
 * @param[out]  exports             An array of exported file systems
 * @param[out]  count               Number of exported file systems
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_nfs_list)(lsm_plugin_ptr c, const char *search_key,
                                 const char *search_value,
                                 lsm_nfs_export **exports[], uint32_t *count,
                                 lsm_flag flags);
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
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_nfs_export_fs)(
    lsm_plugin_ptr c, const char *fs_id, const char *export_path,
    lsm_string_list *root_list, lsm_string_list *rw_list,
    lsm_string_list *ro_list, uint64_t anon_uid, uint64_t anon_gid,
    const char *auth_type, const char *options, lsm_nfs_export **exported,
    lsm_flag flags);

/**
 * Removes a NFS export, callback function signature
 * @param[in]   c                   Valid lsm plug-in pointer
 * @param[in]   e                   Export to remove
 * @param[in]   flags               Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_nfs_export_remove)(lsm_plugin_ptr c, lsm_nfs_export *e,
                                          lsm_flag flags);
/** \struct lsm_san_ops_v1
 *  \brief Block array oriented functions (callback functions)
 * NOTE: This structure cannot change as we need to maintain backwards
 *       compatibility
 */
struct lsm_san_ops_v1 {
    /**  retrieving volumes */
    lsm_plug_volume_list vol_get;
    /**  retrieve disks */
    lsm_plug_disk_list disk_get;
    /**  creating a lun */
    lsm_plug_volume_create vol_create;
    /**  replicating lun */
    lsm_plug_volume_replicate vol_replicate;
    /**  volume replication range block size */
    lsm_plug_volume_replicate_range_block_size vol_rep_range_bs;
    /**  volume replication range */
    lsm_plug_volume_replicate_range vol_rep_range;
    /**  resizing a volume */
    lsm_plug_volume_resize vol_resize;
    /**  deleting a volume */
    lsm_plug_volume_delete vol_delete;
    /**  volume is accessible */
    lsm_plug_volume_enable vol_enable;
    /**  volume is unaccessible */
    lsm_plug_volume_disable vol_disable;
    /**  iscsi chap authentication */
    lsm_plug_iscsi_chap_auth iscsi_chap_auth;
    /**  access groups */
    lsm_plug_access_group_list ag_list;
    /**  access group create */
    lsm_plug_access_group_create ag_create;
    /**  access group delete */
    lsm_plug_access_group_delete ag_delete;
    /**  adding an initiator to an access group */
    lsm_plug_access_group_initiator_add ag_add_initiator;
    /**  deleting an initiator from an access group */
    lsm_plug_access_group_initiator_delete ag_del_initiator;
    /**  acess group grant */
    lsm_plug_volume_mask ag_grant;
    /**  access group revoke */
    lsm_plug_volume_unmask ag_revoke;
    /**  volumes accessible by access group */
    lsm_plug_volumes_accessible_by_access_group vol_accessible_by_ag;
    /**  access groups granted to a volume */
    lsm_plug_access_groups_granted_to_volume ag_granted_to_vol;
    /**  volume child dependencies */
    lsm_plug_volume_child_dependency vol_child_depends;
    /**Callback to remove volume child dependencies */
    lsm_plug_volume_child_dependency_delete vol_child_depends_rm;
    /** Callback to get list of target ports */
    lsm_plug_target_port_list target_port_list;
};

/** \struct  lsm_fs_ops_v1
 *  \brief File system oriented functionality
 * NOTE: This structure cannot change as we need to maintain backwards
 *       compatibility
 */
struct lsm_fs_ops_v1 {
    /** list file systems */
    lsm_plug_fs_list fs_list;
    /** create a file system */
    lsm_plug_fs_create fs_create;
    /** delete a file system */
    lsm_plug_fs_delete fs_delete;
    /** resize a file system */
    lsm_plug_fs_resize fs_resize;
    /** clone a file system */
    lsm_plug_fs_clone fs_clone;
    /** clone files on a file system */
    lsm_plug_fs_file_clone fs_file_clone;
    /** check file system child dependencies */
    lsm_plug_fs_child_dependency fs_child_dependency;
    /** remove file system child dependencies */
    lsm_plug_fs_child_dependency_delete fs_child_dependency_rm;
    /** list snapshots */
    lsm_plug_fs_ss_list fs_ss_list;
    /** create a snapshot */
    lsm_plug_fs_ss_create fs_ss_create;
    /** delete a snapshot */
    lsm_plug_fs_ss_delete fs_ss_delete;
    /** restore a snapshot */
    lsm_plug_fs_ss_restore fs_ss_restore;
};

/** \struct lsm_nas_ops_v1
 * \brief NAS system oriented functionality call back functions
 * NOTE: This structure cannot change as we need to maintain backwards
 *       compatibility
 */
struct lsm_nas_ops_v1 {
    /** List nfs authentication types */
    lsm_plug_nfs_auth_types nfs_auth_types;
    /** List nfs exports */
    lsm_plug_nfs_list nfs_list;
    /** Export a file system */
    lsm_plug_nfs_export_fs nfs_export;
    /** Remove a file export */
    lsm_plug_nfs_export_remove nfs_export_remove;
};

/**
 * Query the RAID information of a volume
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   volume          Volume to be deleted
 * @param[out]  raid_type       Enum of lsm_volume_raid_type
 * @param[out]  strip_size      Size of the strip on each disk or other
 *                              storage extent.
 * @param[out]  disk_count      Count of of disks of RAID group(s) where this
 *                              volume allocated from.
 * @param[out]  min_io_size     Minimum I/O size, also the preferred I/O size
 *                              of random I/O.
 * @param[out]  opt_io_size     Optimal I/O size, also the preferred I/O size
 *                              of sequential I/O.
 * @param[in]   flags           Reserved
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_raid_info)(lsm_plugin_ptr c, lsm_volume *volume,
                                         lsm_volume_raid_type *raid_type,
                                         uint32_t *strip_size,
                                         uint32_t *disk_count,
                                         uint32_t *min_io_size,
                                         uint32_t *opt_io_size, lsm_flag flags);

/**
 * Retrieves the membership of given pool. New in version 1.2.
 * @param[in] c               Valid lsm plug-in pointer
 * @param[in] pool  The lsm_pool ptr.
 * @param[out] raid_type
 *                  Enum of lsm_volume_raid_type.
 * @param[out] member_type
 *                  Enum of lsm_pool_member_type.
 * @param[out] member_ids
 *                  The pointer of lsm_string_list pointer.
 *                  When 'member_type' is LSM_POOL_MEMBER_TYPE_POOL,
 *                  the 'member_ids' will contain a list of parent Pool
 *                  IDs.
 *                  When 'member_type' is LSM_POOL_MEMBER_TYPE_DISK,
 *                  the 'member_ids' will contain a list of disk IDs.
 *                  When 'member_type' is LSM_POOL_MEMBER_TYPE_OTHER or
 *                  LSM_POOL_MEMBER_TYPE_UNKNOWN, the member_ids should
 *                  be NULL.
 * @param[in] flags         Reserved, set to 0
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_pool_member_info)(lsm_plugin_ptr c, lsm_pool *pool,
                                         lsm_volume_raid_type *raid_type,
                                         lsm_pool_member_type *member_type,
                                         lsm_string_list **member_ids,
                                         lsm_flag flags);

/**
 * Query all supported RAID types and strip sizes which could be used
 * in lsm_volume_raid_create() functions.
 * New in version 1.2, only available for hardware RAID cards.
 * @param[in] c     Valid lsm plug-in pointer
 * @param[in] system
 *                  The lsm_sys type.
 * @param[out] supported_raid_types
 *                  The pointer of uint32_t array. Containing
 *                  lsm_volume_raid_type values.
 * @param[out] supported_raid_type_count
 *                  The pointer of uint32_t. Indicate the item count of
 *                  supported_raid_types array.
 * @param[out] supported_strip_sizes
 *                  The pointer of uint32_t array. Containing
 *                  all supported strip sizes.
 * @param[out] supported_strip_size_count
 *                  The pointer of uint32_t. Indicate the item count of
 *                  supported_strip_sizes array.
 * @param[in] flags         Reserved, set to 0
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_raid_create_cap_get)(
    lsm_plugin_ptr c, lsm_system *system, uint32_t **supported_raid_types,
    uint32_t *supported_raid_type_count, uint32_t **supported_strip_sizes,
    uint32_t *supported_strip_size_count, lsm_flag flags);

/**
 * Create a disk RAID pool and allocate entire full space to new volume.
 * New in version 1.2, only available for hardware RAID cards.
 * @param[in] c     Valid lsm plug-in pointer
 * @param[in] name  String. Name for the new volume. It might be ignored or
 *                  altered on some hardwardware raid cards in order to fit
 *                  their limitation.
 * @param[in] raid_type
 *                  Enum of lsm_volume_raid_type.
 * @param[in] disks
 *                  An array of lsm_disk types
 * @param[in] disk_count
 *                  The count of lsm_disk in 'disks' argument.
 * @param[in] strip_size
 *                  uint32_t. The strip size in bytes.
 * @param[out] new_volume
 *                  Newly created volume, Pointer to the lsm_volume type
 *                  pointer.
 * @param[in] flags         Reserved, set to 0
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_raid_create)(
    lsm_plugin_ptr c, const char *name, lsm_volume_raid_type raid_type,
    lsm_disk *disks[], uint32_t disk_count, uint32_t strip_size,
    lsm_volume **new_volume, lsm_flag flags);

/**
 * Enable the IDENT LED for the desired volume.
 * New in version 1.3, only available for hardware RAID cards.
 * @param[in] c         Valid lsm plug-in pointer
 * @param[in] volume    A single lsm_volume
 * @param[in] flags     Reserved, set to 0
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_ident_led_on)(lsm_plugin_ptr c,
                                            lsm_volume *volume, lsm_flag flags);

/**
 * Disable the IDENT LED for the desired volume.
 * New in version 1.3, only available for hardware RAID cards.
 * @param[in] c         Valid lsm plug-in pointer
 * @param[in] volume    A single lsm_volume
 * @param[in] flags     Reserved, set to 0
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_volume_ident_led_off)(lsm_plugin_ptr c,
                                             lsm_volume *volume,
                                             lsm_flag flags);

/**
 * Change the read cache percentage for the desired system.
 * New in version 1.3, only available for hardware RAID cards.
 * @param[in] c             Valid lsm plug-in pointer
 * @param[in] system        A single lsm_system
 * @param[in] read_pct      Desired read cache percentage
 * @param[in] flags         Reserved, set to 0
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
typedef int (*lsm_plug_system_read_cache_pct_update)(lsm_plugin_ptr c,
                                                     lsm_system *system,
                                                     uint32_t read_pct,
                                                     lsm_flag flags);

/** \struct lsm_ops_v1_2
 * \brief Functions added in version 1.2
 */
struct lsm_ops_v1_2 {
    /** Query volume RAID information*/
    lsm_plug_volume_raid_info vol_raid_info;
    lsm_plug_pool_member_info pool_member_info;
    lsm_plug_volume_raid_create_cap_get vol_create_raid_cap_get;
    lsm_plug_volume_raid_create vol_create_raid;
};

/**
 * New in version 1.3.
 * Retrieve a list of batteries.
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   search_key      Search key
 * @param[in]   search_value    Search value
 * @param[out]  bs              Array of batteries
 * @param[out]  count           Number of batteries
 * @param[in]   flags           Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_battery_list)(lsm_plugin_ptr c, const char *search_key,
                                     const char *search_val, lsm_battery **bs[],
                                     uint32_t *count, lsm_flag flags);

/**
 * New in version 1.3.
 * Allows for battery filtering when a storage system can't support it natively.
 * Note: Filters in place removing and freeing those that don't match.
 * @param search_key        Search field
 * @param search_value      Search value
 * @param[in,out] bs      Array to filter
 * @param[in,out] count     Number of batteries to filter, number remain
 */
void LSM_DLL_EXPORT lsm_plug_battery_search_filter(const char *search_key,
                                                   const char *search_value,
                                                   lsm_battery *bs[],
                                                   uint32_t *count);
/**
 * New in version 1.3.
 * Allocate the storage needed for an array of lsm_battery records.
 * @param size      Number of elements
 * @return Allocated memory or null on error.
 */
lsm_battery LSM_DLL_EXPORT **lsm_battery_record_array_alloc(uint32_t size);

/**
 * New in version 1.3.
 * Allocate a battery record.
 * @param id            Identification
 * @param name          Human readable name
 * @param type          Enumerated lsm_battery_type
 * @param status        Status
 * @param system_id     System id this battery resides in
 * @param plugin_data   Reserved for plugin writer use
 * @return Pointer to allocated disk record or NULL on memory error.
 */
lsm_battery LSM_DLL_EXPORT *
lsm_battery_record_alloc(const char *id, const char *name,
                         lsm_battery_type type, uint64_t status,
                         const char *system_id, const char *plugin_data);

/**
 * New in version 1.3.
 * Used to retrieve the plugin-private data for a specific battery.
 * @param b     Battery to retrieve plugin private data for
 * @return NULL if doesn't exists, else data.
 */
const char LSM_DLL_EXPORT *lsm_battery_plugin_data_get(lsm_battery *b);

/**
 * Query the RAM cache information of a volume
 * @param[in]   c               Valid lsm plug-in pointer
 * @param[in]   volume          Volume to be deleted
 * @param[out]  write_cache_policy
 *                              The write cache policy.
 * @param[out] write_cache_status
 *                              The status of write cache.
 * @param[out] read_cache_policy
 *                              The policy for read cache.
 * @param[out] read_cache_status
 *                              The status of read cache.
 * @param[out] physical_disk_cache
 *                              Whether physical disk's cache is enabled or not.
 * @param[in]   flags           Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_volume_cache_info)(
    lsm_plugin_ptr c, lsm_volume *volume, uint32_t *write_cache_policy,
    uint32_t *write_cache_status, uint32_t *read_cache_policy,
    uint32_t *read_cache_status, uint32_t *physical_disk_cache, lsm_flag flags);

/**
 * New in version 1.3.
 * Change the setting of RAM physical disk cache of specified volume.
 * @param[in] c                   Valid lsm plug-in pointer
 * @param[in] volume              Volume to be changed.
 * @param[in] pdc                 Physical disk cache setting.
 * @param[in] flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_volume_physical_disk_cache_update)(lsm_plugin_ptr c,
                                                          lsm_volume *volume,
                                                          uint32_t pdc,
                                                          lsm_flag flags);
/**
 * New in version 1.3.
 * Change the setting of RAM write disk cache policy of specified volume.
 * @param[in] c                   Valid lsm plug-in pointer
 * @param[in] volume              Volume to be changed.
 * @param[in] wcp                 Write disk cache policy.
 * @param[in] flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_volume_write_cache_policy_update)(lsm_plugin_ptr c,
                                                         lsm_volume *volume,
                                                         uint32_t wcp,
                                                         lsm_flag flags);

/**
 * New in version 1.3.
 * Change the setting of RAM read disk cache policy of specified volume.
 * @param[in] c                   Valid lsm plug-in pointer
 * @param[in] volume              Volume to be changed.
 * @param[in] rcp                 Read disk cache policy.
 * @param[in] flags               Reserved
 * @return LSM_ERR_OK, else error reason
 */
typedef int (*lsm_plug_volume_read_cache_policy_update)(lsm_plugin_ptr c,
                                                        lsm_volume *volume,
                                                        uint32_t rcp,
                                                        lsm_flag flags);

/** \struct lsm_ops_v1_3
 * \brief Functions added in version 1.3
 */
struct lsm_ops_v1_3 {
    lsm_plug_volume_ident_led_on vol_ident_on;
    lsm_plug_volume_ident_led_off vol_ident_off;
    lsm_plug_system_read_cache_pct_update sys_read_cache_pct_update;
    lsm_plug_battery_list battery_list;
    lsm_plug_volume_cache_info vol_cache_info;
    lsm_plug_volume_physical_disk_cache_update vol_pdc_update;
    lsm_plug_volume_write_cache_policy_update vol_wcp_update;
    lsm_plug_volume_read_cache_policy_update vol_rcp_update;
};

/**
 * Copies the memory pointed to by item with given type t.
 * @param t         Type of item to copy
 * @param item      Pointer to src
 * @return Null, else copy of item.
 */
void LSM_DLL_EXPORT *lsm_data_type_copy(lsm_data_type t, void *item);

/**
 * Initializes the plug-in.
 * @param argc  Command line argument count
 * @param argv  Command line arguments
 * @param reg   Registration function
 * @param unreg Un-Registration function
 * @param desc  Plug-in description
 * @param version   Plug-in version
 * @return exit code for plug-in
 */
int LSM_DLL_EXPORT lsm_plugin_init_v1(int argc, char *argv[],
                                      lsm_plugin_register reg,
                                      lsm_plugin_unregister unreg,
                                      const char *desc, const char *version);

/**
 * Used to register all the data needed for the plug-in operation.
 * @param plug              Pointer provided by the framework
 * @param private_data      Private data to be used for whatever the plug-in
 * needs
 * @param mgm_ops           Function pointers for management operations
 * @param san_ops           Function pointers for SAN operations
 * @param fs_ops            Function pointers for file system operations
 * @param nas_ops           Function pointers for NAS operations
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_register_plugin_v1(lsm_plugin_ptr plug,
                                          void *private_data,
                                          struct lsm_mgmt_ops_v1 *mgm_ops,
                                          struct lsm_san_ops_v1 *san_ops,
                                          struct lsm_fs_ops_v1 *fs_ops,
                                          struct lsm_nas_ops_v1 *nas_ops);

/**
 * Used to register version 1.2 APIs plug-in operation.
 * @param plug              Pointer provided by the framework
 * @param private_data      Private data to be used for whatever the plug-in
 *                          needs
 * @param mgm_ops           Function pointers for struct lsm_mgmt_ops_v1
 * @param san_ops           Function pointers for struct lsm_san_ops_v1
 * @param fs_ops            Function pointers for struct lsm_fs_ops_v1
 * @param nas_ops           Function pointers for struct lsm_nas_ops_v1
 * @param ops_v1_2          Function pointers for struct lsm_ops_v1_2
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_register_plugin_v1_2(
    lsm_plugin_ptr plug, void *private_data, struct lsm_mgmt_ops_v1 *mgm_ops,
    struct lsm_san_ops_v1 *san_ops, struct lsm_fs_ops_v1 *fs_ops,
    struct lsm_nas_ops_v1 *nas_ops, struct lsm_ops_v1_2 *ops_v1_2);

/**
 * Used to register version 1.3 APIs plug-in operation.
 * @param plug              Pointer provided by the framework
 * @param private_data      Private data to be used for whatever the plug-in
 *                          needs
 * @param mgm_ops           Function pointers for struct lsm_mgmt_ops_v1
 * @param san_ops           Function pointers for struct lsm_san_ops_v1
 * @param fs_ops            Function pointers for struct lsm_fs_ops_v1
 * @param nas_ops           Function pointers for struct lsm_nas_ops_v1
 * @param ops_v1_2          Function pointers for struct lsm_ops_v1_2
 * @param ops_v1_3          Function pointers for struct lsm_ops_v1_3
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_register_plugin_v1_3(
    lsm_plugin_ptr plug, void *private_data, struct lsm_mgmt_ops_v1 *mgm_ops,
    struct lsm_san_ops_v1 *san_ops, struct lsm_fs_ops_v1 *fs_ops,
    struct lsm_nas_ops_v1 *nas_ops, struct lsm_ops_v1_2 *ops_v1_2,
    struct lsm_ops_v1_3 *ops_v1_3);

/**
 * Used to retrieve private data for plug-in operation.
 * @param plug  Opaque plug-in pointer.
 */
void LSM_DLL_EXPORT *lsm_private_data_get(lsm_plugin_ptr plug);

/**
 * Logs an error with the plug-in
 * @param plug  Plug-in pointer
 * @param code  Error code to return
 * @param msg   String message
 * @return returns code
 */
int LSM_DLL_EXPORT lsm_log_error_basic(lsm_plugin_ptr plug,
                                       lsm_error_number code, const char *msg);

/**
 * Return an error with the plug-in
 * @param plug          Opaque plug-in
 * @param error         Error to associate.
 * @return              LSM_ERR_OK, else error reason.
 */
int LSM_DLL_EXPORT lsm_plugin_error_log(lsm_plugin_ptr plug,
                                        lsm_error_ptr error);

/**
 * Creates an error record.
 * @param code
 * @param msg
 * @param exception
 * @param debug
 * @param debug_data
 * @param debug_data_size
 * @return Null on error, else valid error error record.
 */
lsm_error_ptr LSM_DLL_EXPORT lsm_error_create(
    lsm_error_number code, const char *msg, const char *exception,
    const char *debug, const void *debug_data, uint32_t debug_data_size);

/**
 * Plug-in macros for creating errors
 */
#define LSM_ERROR_CREATE_PLUGIN_MSG(code, msg)                                 \
    lsm_error_create(code, msg, NULL, NULL, NULL, 0)

#define LSM_ERROR_CREATE_PLUGIN_EXCEPTION(code, msg, exception)                \
    lsm_error_create((code), (msg), (exception), NULL, NULL, 0)

#define LSM_ERROR_CREATE_PLUGIN_DEBUG(code, msg, exception, debug, debug_data, \
                                      debug_len)                               \
    lsm_error_create((code), (msg), (exception), (debug), (debug_data),        \
                     debug_len)

/**
 * Helper function to create an array of lsm_pool *
 * @param size  Number of elements
 * @return Valid pointer or NULL on error.
 */
lsm_pool LSM_DLL_EXPORT **lsm_pool_record_array_alloc(uint32_t size);

/**
 * Used to set the free space on a pool record
 * @param p                 Pool to modify
 * @param free_space        New free space value
 */
void LSM_DLL_EXPORT lsm_pool_free_space_set(lsm_pool *p, uint64_t free_space);

/**
 * Helper function to allocate a pool record.
 * @param id            System unique identifier
 * @param name          Human readable name
 * @param element_type  A bit field which states what the pool can be used to
 *                      create
 * @param unsupported_actions   Things you cannot do with this pool
 * @param total_space   Total space
 * @param free_space    Space available
 * @param status        Pool status, bit field (See LSM_POOL_STATUS_XXXX
 *                      constants)
 * @param status_info   Additional textual information on status
 * @param system_id     System id
 * @param plugin_data   Reserved for plugin writer use
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
lsm_pool LSM_DLL_EXPORT *
lsm_pool_record_alloc(const char *id, const char *name, uint64_t element_type,
                      uint64_t unsupported_actions, uint64_t total_space,
                      uint64_t free_space, uint64_t status,
                      const char *status_info, const char *system_id,
                      const char *plugin_data);

/**
 * Used to retrieve the plugin-private data for a specfic pool
 * @param p     Pool to retrieve plugin private data for
 * @return NULL if donesn't exists, else data.
 */
const char LSM_DLL_EXPORT *lsm_pool_plugin_data_get(lsm_pool *p);

/**
 * Allocate the storage needed for and array of Volume records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsm_volume LSM_DLL_EXPORT **lsm_volume_record_array_alloc(uint32_t size);

/**
 * Allocate the storage needed for tan array of disk records.
 * @param size      Number of elements
 * @return Allocated memory or null on error.
 */
lsm_disk LSM_DLL_EXPORT **lsm_disk_record_array_alloc(uint32_t size);

/**
 * Allocate a disk record.
 * @param id                Identification
 * @param name              Human readable name
 * @param disk_type         Enumerated disk type
 * @param block_size        Number of bytes per logical block
 * @param block_count       Number of blocks for disk
 * @param disk_status       Status
 * @param system_id         System id this disk resides in
 * @return Pointer to allocated disk record or NULL on memory error.
 */
lsm_disk LSM_DLL_EXPORT *
lsm_disk_record_alloc(const char *id, const char *name, lsm_disk_type disk_type,
                      uint64_t block_size, uint64_t block_count,
                      uint64_t disk_status, const char *system_id);

/**
 * New in version 1.3. Set a disk's location.
 * @param disk          Pointer to the disk of interest.
 * @param disk_path     Pointer to the disk's location.
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_disk_location_set(lsm_disk *disk, const char *location);

/**
 * Allocated the storage needed for one volume record.
 * @param id                    ID
 * @param name                  Name
 * @param vpd83                 SCSI vpd 83 id
 * @param block_size            Volume block size
 * @param number_of_blocks      Volume number of blocks
 * @param status                Volume status
 * @param system_id             System id
 * @param pool_id               Pool id this volume is created from
 * @param plugin_data           Private data for plugin use
 * @return Allocated memory or NULL on error.
 */
lsm_volume LSM_DLL_EXPORT *
lsm_volume_record_alloc(const char *id, const char *name, const char *vpd83,
                        uint64_t block_size, uint64_t number_of_blocks,
                        uint32_t status, const char *system_id,
                        const char *pool_id, const char *plugin_data);

/**
 * Retrieve the private plug-in data from the volume record.
 * @param v     Volume pointer
 * @return Private data, else NULL if it doesn't exist.
 */
const char LSM_DLL_EXPORT *lsm_volume_plugin_data_get(lsm_volume *v);

/**
 * Allocate the storage needed for and array of System records.
 * @param size      Number of elements.
 * @return Allocated memory or NULL on error.
 */
lsm_system LSM_DLL_EXPORT **lsm_system_record_array_alloc(uint32_t size);

/**
 * Allocates the storage for one system record.
 * @param[in] id            Id
 * @param[in] name          System name (human readable)
 * @param[in] status        Status of the system
 * @param[in] status_info   Additional text for status
 * @param[in] plugin_data   Private plugin data
 * @return  Allocated memory or NULL on error.
 */
lsm_system LSM_DLL_EXPORT *
lsm_system_record_alloc(const char *id, const char *name, uint32_t status,
                        const char *status_info, const char *plugin_data);

/**
 * New in version 1.3. Set read cache percentage.
 * @param[in] id            Id
 * @param[in] sys           System to update.
 * @param[in] read_pct      Read cache percentage.
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 * @retval LSM_ERR_INVALID_ARGUMENT otherwise.
 */
int LSM_DLL_EXPORT lsm_system_read_cache_pct_set(lsm_system *sys, int read_pct);

/**
 * New in version 1.3. Set firmware version.
 * @param[in] id            Id
 * @param[in] sys           System to update.
 * @param[in] fw_ver        Firmware version string.
 *                          Caller will get LSM_ERR_INVALID_ARGUMENT for
 *                          empty string('\0').
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 * @retval LSM_ERR_NO_MEMORY
 * @retval LSM_ERR_INVALID_ARGUMENT
 */
int LSM_DLL_EXPORT lsm_system_fw_version_set(lsm_system *sys,
                                             const char *fw_ver);

/**
 * New in version 1.3. Set system mode.
 * @param[in] sys           System to update.
 * @param[in] mode          System mode 'lsm_system_mode_type'.
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 * @retval LSM_ERR_INVALID_ARGUMENT otherwise.
 */
int LSM_DLL_EXPORT lsm_system_mode_set(lsm_system *sys,
                                       lsm_system_mode_type mode);

/**
 * Retrieve plugin private data
 * @param s     System
 * @return Optional data, NULL if none exist
 */
const char LSM_DLL_EXPORT *lsm_system_plugin_data_get(lsm_system *s);

/**
 * Allocates storage for Access_group array
 * @param size      Number of elements to store.
 * @return  NULL on error, else pointer to array for use.
 */
lsm_access_group LSM_DLL_EXPORT **
lsm_access_group_record_array_alloc(uint32_t size);

/**
 * Allocates storage for single Access_group
 * @param id                ID of access group
 * @param name              Name of access group
 * @param initiators        List of initiators, can be NULL
 * @param init_type         Initiator group type
 * @param system_id         System id
 * @param plugin_data       Reserved for plug-in use only
 * @return NULL on error, else valid lsm_access_group pointer.
 */
lsm_access_group LSM_DLL_EXPORT *
lsm_access_group_record_alloc(const char *id, const char *name,
                              lsm_string_list *initiators,
                              lsm_access_group_init_type init_type,
                              const char *system_id, const char *plugin_data);

/**
 * Use to change the list of initiators associated with an access group.
 * @param group     Access group to change initiators for
 * @param il        String list of initiators.
 */
void LSM_DLL_EXPORT lsm_access_group_initiator_id_set(lsm_access_group *group,
                                                      lsm_string_list *il);

/**
 * Allocates memory for a file system record
 * @param id                    ID of file system
 * @param name                  Name of file system
 * @param total_space           Total space
 * @param free_space            Free space
 * @param pool_id               Pool id
 * @param system_id             System id
 * @param plugin_data           Reserved for plug-in use only
 * @return lsm_fs, NULL on error
 */
lsm_fs LSM_DLL_EXPORT *
lsm_fs_record_alloc(const char *id, const char *name, uint64_t total_space,
                    uint64_t free_space, const char *pool_id,
                    const char *system_id, const char *plugin_data);

/**
 * Allocates the memory for the array of file system records.
 * @param size      Number of elements
 * @return Allocated memory, NULL on error
 */
lsm_fs LSM_DLL_EXPORT **lsm_fs_record_array_alloc(uint32_t size);

/**
 * Used to retrieve the plug-in private data for a specific pool
 * @param fs     FS to retrieve plug-in private data for
 * @return NULL if doesn't exist, else data.
 */
const char LSM_DLL_EXPORT *lsm_fs_plugin_data_get(lsm_fs *fs);

/**
 * Allocates the memory for single snap shot record.
 * @param id            ID
 * @param name          Name
 * @param ts            Epoch time stamp when snapshot was created
 * @param plugin_data   Private plugin data
 * @return Allocated memory, NULL on error
 */
lsm_fs_ss LSM_DLL_EXPORT *lsm_fs_ss_record_alloc(const char *id,
                                                 const char *name, uint64_t ts,
                                                 const char *plugin_data);

/**
 * Allocates the memory for an array of snapshot records.
 * @param size          Number of elements
 * @return Allocated memory, NULL on error
 */
lsm_fs_ss LSM_DLL_EXPORT **lsm_fs_ss_record_array_alloc(uint32_t size);

/**
 * Retrieve private data from fs_ss.
 * @param fs_ss       Valid fs_ss record
 * @return Private data, else NULL
 */
const char LSM_DLL_EXPORT *lsm_fs_ss_plugin_data_get(lsm_fs_ss *fs_ss);

/**
 * Set a capability
 * @param cap           Valid capability pointer
 * @param t             Which capability to set
 * @param v             Value of the capability
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_capability_set(lsm_storage_capabilities *cap,
                                      lsm_capability_type t,
                                      lsm_capability_value_type v);

/**
 * Sets 1 or more capabilities with the same value v
 * @param cap           Valid capability pointer
 * @param v             The value to set capabilities to
 * @param ...           Which capabilites to set (Make sure to terminate list
 *                      with a -1)
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_capability_set_n(lsm_storage_capabilities *cap,
                                        lsm_capability_value_type v, ...);

/**
 * Allocated storage for capabilities
 * @param value     Set to NULL, used during serialization otherwise.
 * @return Allocated record, or NULL on memory allocation failure.
 */
lsm_storage_capabilities LSM_DLL_EXPORT *
lsm_capability_record_alloc(char const *value);

/**
 * Convenience function for plug-in writer.
 * Note: Make sure to free returned items to prevent memory leaks.
 * @param[in]   uri             URI to parse
 * @param[out]  scheme          returned scheme
 * @param[out]  user            returned user
 * @param[out]  server          returned server
 * @param[out]  port            returned port
 * @param[out]  path            returned path
 * @param[out]  query_params    returned query params
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_uri_parse(const char *uri, char **scheme, char **user,
                                 char **server, int *port, char **path,
                                 lsm_hash **query_params);

/**
 * Provides for volume filtering when an array doesn't support this natively.
 * Note: Filters in place removing and freeing those that don't match.
 * @param search_key        Search field
 * @param search_value      Search value
 * @param[in,out] vols      Array to filter
 * @param[in,out] count     Number of volumes to filter, number remain
 */
void LSM_DLL_EXPORT lsm_plug_volume_search_filter(const char *search_key,
                                                  const char *search_value,
                                                  lsm_volume *vols[],
                                                  uint32_t *count);

/**
 * Provides for pool filtering when an array doesn't support this natively.
 * Note: Filters in place removing and freeing those that don't match.
 * @param search_key        Search field
 * @param search_value      Search value
 * @param[in,out] pools     Array to filter
 * @param[in,out] count     Number of pools to filter, number remain
 */
void LSM_DLL_EXPORT lsm_plug_pool_search_filter(const char *search_key,
                                                const char *search_value,
                                                lsm_pool *pools[],
                                                uint32_t *count);

/**
 * Provides for disk filtering when an array doesn't support this natively.
 * Note: Filters in place removing and freeing those that don't match.
 * @param search_key        Search field
 * @param search_value      Search value
 * @param[in,out] disks     Array to filter
 * @param[in,out] count     Number of disks to filter, number remain
 */
void LSM_DLL_EXPORT lsm_plug_disk_search_filter(const char *search_key,
                                                const char *search_value,
                                                lsm_disk *disks[],
                                                uint32_t *count);

/**
 * Provides for access group filtering when an array doesn't support this
 * natively.
 * Note: Filters in place removing and freeing those that don't match.
 * @param search_key        Search field
 * @param search_value      Search value
 * @param[in,out] ag        Array to filter
 * @param[in,out] count     Number of access groups to filter, number remain
 */
void LSM_DLL_EXPORT lsm_plug_access_group_search_filter(
    const char *search_key, const char *search_value, lsm_access_group *ag[],
    uint32_t *count);

/**
 * Provides for fs filtering when an array doesn't support this natively.
 * Note: Filters in place removing and freeing those that don't match.
 * @param search_key        Search field
 * @param search_value      Search value
 * @param[in,out] fs        Array to filter
 * @param[in,out] count     Number of file systems to filter, number remain
 */
void LSM_DLL_EXPORT lsm_plug_fs_search_filter(const char *search_key,
                                              const char *search_value,
                                              lsm_fs *fs[], uint32_t *count);

/**
 * Provides for nfs filtering when an array doesn't support this natively.
 * Note: Filters in place removing and freeing those that don't match.
 * @param search_key        Search field
 * @param search_value      Search value
 * @param[in,out] exports   Array to filter
 * @param[in,out] count     Number of nfs exports to filter, number remain
 */
void LSM_DLL_EXPORT lsm_plug_nfs_export_search_filter(const char *search_key,
                                                      const char *search_value,
                                                      lsm_nfs_export *exports[],
                                                      uint32_t *count);

/**
 * Retrieve private data from nfs export record.
 * @param exp       Valid nfs export record
 * @return Private data, else NULL
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_plugin_data_get(lsm_nfs_export *exp);

/**
 * Allocate a target port
 * @param id                    ID of target port
 * @param port_type             Port type
 * @param service_address       Service address
 * @param network_address       Network Address
 * @param physical_address      Physical address
 * @param physical_name         Physical name
 * @param system_id             System ID
 * @param plugin_data           Plug-in data
 * @return valid lsm_target_port, else NULL on memory allocation failure
 */
lsm_target_port LSM_DLL_EXPORT *lsm_target_port_record_alloc(
    const char *id, lsm_target_port_type port_type, const char *service_address,
    const char *network_address, const char *physical_address,
    const char *physical_name, const char *system_id, const char *plugin_data);

/**
 * Retrieve the plug-in private data pointer
 * @param tp    Valid target port pointer
 * @return Character pointer to string, NULL on error
 */
const char LSM_DLL_EXPORT *lsm_target_port_plugin_data_get(lsm_target_port *tp);

/**
 * Allocated an array of target pointers
 * @param size      Number of pointers to store
 * @return Allocated memory, NULL on allocation errors
 */
lsm_target_port LSM_DLL_EXPORT **
lsm_target_port_record_array_alloc(uint32_t size);

/**
 * Provides for target port filtering when an array doesn't support this
 * natively.
 * Note: Filters in place removing and freeing those that don't match.
 * @param search_key        Search field
 * @param search_value      Search value
 * @param[in,out] tp        Array to filter
 * @param[in,out] count     Number of target ports to filter, number remain
 */
void LSM_DLL_EXPORT lsm_plug_target_port_search_filter(const char *search_key,
                                                       const char *search_value,
                                                       lsm_target_port *tp[],
                                                       uint32_t *count);

/**
 * New in version 1.3. Set disk VPD83 ID.
 * @param[in] disk          Disk to update.
 * @param[in] vpd83         VPD83 ID string.
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 * @retval LSM_ERR_INVALID_ARGUMENT otherwise.
 */
int LSM_DLL_EXPORT lsm_disk_vpd83_set(lsm_disk *disk, const char *vpd83);

/**
 * New in version 1.3. Set disk rotation speed - revolutions per minute(RPM)
 * @param[in] disk          Disk to update.
 * @param[in] rpm           revolutions per minute(RPM).
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 * @retval LSM_ERR_INVALID_ARGUMENT otherwise.
 */
int LSM_DLL_EXPORT lsm_disk_rpm_set(lsm_disk *disk, int32_t rpm);

/**
 * New in version 1.3. Set disk link type.
 * @param[in] disk          Disk to update.
 * @param[in] link_type     lsm_disk_link_type
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 * @retval LSM_ERR_INVALID_ARGUMENT otherwise.
 */
int LSM_DLL_EXPORT lsm_disk_link_type_set(lsm_disk *disk,
                                          lsm_disk_link_type link_type);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMT_PLUG_INTERFACE_H */
