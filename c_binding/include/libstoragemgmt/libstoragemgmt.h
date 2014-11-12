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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMT_H
#define LIBSTORAGEMGMT_H

#include "libstoragemgmt_types.h"
#include "libstoragemgmt_common.h"

#include "libstoragemgmt_accessgroups.h"
#include "libstoragemgmt_blockrange.h"
#include "libstoragemgmt_capabilities.h"
#include "libstoragemgmt_disk.h"
#include "libstoragemgmt_error.h"
#include "libstoragemgmt_fs.h"
#include "libstoragemgmt_nfsexport.h"
#include "libstoragemgmt_pool.h"
#include "libstoragemgmt_snapshot.h"
#include "libstoragemgmt_systems.h"
#include "libstoragemgmt_targetport.h"
#include "libstoragemgmt_volumes.h"


/*! \mainpage libStorageMgmt
 *
 * \section Introduction
 *
 * The libStorageMgmt package is a storage array independent Application
 * Programming Interface (API). It provides a stable and consistent API that
 * allows developers the ability to programmatically manage different storage
 * arrays and leverage the hardware accelerated features that they provide.
 *
 *  \section additional Additional documentation
 *
 * Full documentation can be found at:
 * http://sourceforge.net/p/libstoragemgmt/wiki/Home/
 *
 */

#ifdef  __cplusplus
extern "C" {
#endif

    /**
     * Get a connection to a storage provider.
     * @param[in] uri       Uniform Resource Identifier (see URI documentation)
     * @param[in] password  Password for the storage array (optional, can be NULL)
     * @param[out] conn     The connection to use for all the other library calls
     * @param[in] timeout   Time-out in milliseconds, (initial value).
     * @param[out] e        Error data if connection failed.
     * @param[in] flags     Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code @see lsm_error_number
     */
    int LSM_DLL_EXPORT lsm_connect_password(const char* uri, const char *password,
        lsm_connect **conn, uint32_t timeout, lsm_error_ptr *e, lsm_flag flags);
    /**
     * Closes a connection to a storage provider.
     * @param[in] conn      Valid connection to close
     * @param[in] flags     Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code @see lsm_error_number
     */
    int LSM_DLL_EXPORT lsm_connect_close(lsm_connect *conn, lsm_flag flags);

    /**
     * Retrieve information about the plug-in
     * NOTE: Caller needs to free desc and version!
     * @param[in] conn      Valid connection @see lsm_connect_password
     * @param[out] desc     Plug-in description
     * @param[out] version  Plug-in version
     * @param [in] flags    Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code @see lsm_error_number
     */
    int LSM_DLL_EXPORT lsm_plugin_info_get(lsm_connect *conn, char **desc,
                                        char **version, lsm_flag flags);

	/**
	 * Retrieve a list of available plug-ins.
	 * @param[in] sep		Return data separator
	 * @param[out] plugins	String list of plug-ins with the form
	 *						desc<sep>version
     * @param[in] flags     Reserved for future use, must be zero.
	 * @return LSM_ERR_OK on success, else error code @see lsm_error_number
	 */
	int LSM_DLL_EXPORT lsm_available_plugins_list(const char *sep,
                                                lsm_string_list **plugins,
                                                lsm_flag flags);

    /**
     * Sets the time-out for this connection.
     * @param[in] conn          Valid connection @see lsm_connect_password
     * @param[in] timeout       Time-out (in ms)
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsm_connect_timeout_set(lsm_connect *conn,
                                            uint32_t timeout, lsm_flag flags);

    /**
     * Gets the time-out for this connection.
     * @param[in]   conn        Valid connection @see lsm_connect_password
     * @param[out]  timeout     Time-out (in ms)
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsm_connect_timeout_get(lsm_connect *conn,
                                            uint32_t *timeout, lsm_flag flags);

    /**
     * Check on the status of a job, no data to return on completion.
     * @param[in] conn              Valid connection
     * @param[in] job_id            Job id
     * @param[out] status           Job Status
     * @param[out] percent_complete  Percent job complete
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsm_job_status_get(lsm_connect *conn, const char *job_id,
                                lsm_job_status *status, uint8_t *percent_complete,
                                lsm_flag flags);

    /**
     * Check on the status of a job and return the pool information when
     * complete
     * @param[in]    conn       Valid connection pointer
     * @param[in]    job_id     Job to check status on
     * @param[out]   status     What is the job status
     * @param[out]   percent_complete    Domain 0..100
     * @param[out]   pool       lsm_pool for completed operation
     * @param[in]    flags      Reserved for future use, must be zero
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_job_status_pool_get(lsm_connect *conn,
                                const char *job_id, lsm_job_status *status,
                                uint8_t *percent_complete, lsm_pool **pool,
                                lsm_flag flags);

    /**
     * Check on the status of a job and returns the volume information when
     * complete.
     * @param[in] conn              Valid connection pointer.
     * @param[in] job_id            Job to check status on
     * @param[out] status           What is the job status
     * @param[out] percent_complete  Domain 0..100
     * @param[out] vol              lsm_volume for completed operation.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_job_status_volume_get(lsm_connect *conn,
                                const char *job_id, lsm_job_status *status,
                                uint8_t *percent_complete, lsm_volume **vol,
                                lsm_flag flags);


    /**
     * Check on the status of a job and return the fs information when complete.
     * @param[in] conn                  Valid connection pointer
     * @param[in] job_id                Job to check
     * @param[out] status               What is the job status
     * @param[out] percent_complete      Percent of job complete
     * @param[out] fs                   lsm_fs * for the completed operation
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_job_status_fs_get(lsm_connect *conn, const char *job_id,
                                lsm_job_status *status, uint8_t *percent_complete,
                                lsm_fs **fs, lsm_flag flags);

    /**
     * Check on the status of a job and return the snapshot information when
     * compete.
     * @param[in] c                     Valid connection pointer
     * @param[in] job                   Job id to check
     * @param[out] status               Job status
     * @param[out] percent_complete      Percent complete
     * @param[out] ss                   Snap shot information
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsm_job_status_ss_get(lsm_connect *c, const char *job,
                                lsm_job_status *status, uint8_t *percent_complete,
                                lsm_fs_ss **ss, lsm_flag flags);

    /**
     * Frees the resources used by a job.
     * @param[in] conn          Valid connection pointer
     * @param[in] job_id        Job ID
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK, else error reason.
     */
    int LSM_DLL_EXPORT lsm_job_free(lsm_connect *conn, char **job_id,
                                    lsm_flag flags);
    /**
     * Storage system query functions
     */

    /**
     * Query the capabilities of the storage array.
     * @param[in]   conn    Valid connection @see lsm_connect_password
     * @param[in]   system  System of interest
     * @param[out]  cap     The storage array capabilities
     * @param[in] flags     Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsm_capabilities(lsm_connect *conn,
                                        lsm_system *system,
                                        lsm_storage_capabilities **cap,
                                        lsm_flag flags);

    /**
     * Query the list of storage pools on the array.
     * @param[in]   conn            Valid connection @see lsm_connect_password
     * @param[in]   search_key      Search key (NULL for all)
     * @param[in]   search_value    Search value
     * @param[out]  pool_array      Array of storage pools
     * @param[out]  count           Number of storage pools
     * @param[in]   flags           Reserved, set to 0
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsm_pool_list(lsm_connect *conn, char *search_key,
                                    char *search_value, lsm_pool **pool_array[],
                                    uint32_t *count, lsm_flag flags);

    /**
     * Volume management functions
     */

    /**
     * Gets a list of logical units for this array.
     * @param[in]   conn            Valid connection @see lsm_connect_password
     * @param[in]   search_key      Search key (NULL for all)
     * @param[in]   search_value    Search value
     * @param[out]  volumes         An array of lsm_volume
     * @param[out]  count           Number of elements in the lsm_volume array
     * @param[in]   flags           Reserved set to 0
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsm_volume_list(lsm_connect *conn,
                                        const char *search_key,
                                        const char *search_value,
                                        lsm_volume **volumes[],
                                        uint32_t *count, lsm_flag flags);

    /**
     * Get a list of disk for this array.
     * @param [in]      conn            Valid connection @see
     *                                  lsm_connect_password
     * @param[in]       search_key      Search key (NULL for all)
     * @param[in]       search_value    Search value
     * @param [out]     disks           An array of lsm_disk types
     * @param [out]     count           Number of disks
     * @param [in]      flags           Reserved set to zero
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsm_disk_list(lsm_connect *conn, const char * search_key,
                                        const char *search_value,
                                        lsm_disk **disks[], uint32_t *count,
                                        lsm_flag flags);

    /**
     * Creates a new volume (aka. LUN).
     * @param[in]   conn            Valid connection @see lsm_connect_password
     * @param[in]   pool            Valid pool @see lsm_pool (OPTIONAL, use NULL for plug-in choice)
     * @param[in]   volume_name     Human recognizable name (not all arrays support)
     * @param[in]   size            Size of new volume in bytes (actual size will
     *                              be based on array rounding to block size)
     * @param[in]   provisioning    Type of volume provisioning to use
     * @param[out]  new_volume      Valid volume @see lsm_volume
     * @param[out]  job             Indicates job id
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsm_volume_create(lsm_connect *conn, lsm_pool *pool,
                                        const char *volume_name, uint64_t size,
                                        lsm_volume_provision_type provisioning,
                                        lsm_volume **new_volume, char **job,
                                        lsm_flag flags);

    /**
     * Resize an existing volume.
     * @param[in]   conn            Valid connection @see lsm_connect_password
     * @param[in]   volume          volume to re-size
     * @param[in]   new_size        New size of volume
     * @param[out]  resized_volume  Pointer to newly re-sized lun.
     * @param[out]  job             Indicates job id
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsm_volume_resize(lsm_connect *conn, lsm_volume *volume,
                                uint64_t new_size, lsm_volume **resized_volume,
                                char **job, lsm_flag flags);

    /**
     * Replicates a volume
     * @param[in] conn              Valid connection @see lsm_connect_password
     * @param[in] pool              Valid pool
     * @param[in] rep_type          Type of replication lsm_replication_type
     * @param[in] volume_src        Which volume to replicate
     * @param[in] name              Human recognizable name (not all arrays support)
     * @param[out] new_replicant    New replicated volume lsm_volume_t
     * @param[out] job              Indicates job id
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsm_volume_replicate(lsm_connect *conn, lsm_pool *pool,
                            lsm_replication_type rep_type, lsm_volume *volume_src,
                            const char *name, lsm_volume **new_replicant,
                            char **job, lsm_flag flags);

    /**
     * Unit of block size for the replicate range method.
     * @param[in] conn                  Valid connection
     * @param[in] system                Valid lsm_system
     * @param[out] bs                   Block size
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_volume_replicate_range_block_size(lsm_connect *conn,
                                                        lsm_system *system,
                                                        uint32_t *bs,
                                                        lsm_flag flags);

    /**
     * Replicates a portion of a volume to a volume.
     * @param[in] conn                  Valid connection
     * @param[in] rep_type               Replication type
     * @param[in] source                Source volume
     * @param[in] dest                  Destination volume (can be same as source)
     * @param[in] ranges                An array of block ranges
     * @param[in] num_ranges            Number of entries in ranges.
     * @param[out] job                  Indicates job id
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async., else error code
     */
    int LSM_DLL_EXPORT lsm_volume_replicate_range(lsm_connect *conn,
                                                lsm_replication_type rep_type,
                                                lsm_volume *source,
                                                lsm_volume *dest,
                                                lsm_block_range **ranges,
                                                uint32_t num_ranges, char **job,
                                                lsm_flag flags);

    /**
     * Deletes a logical unit and data is lost!
     * @param[in]   conn            Valid connection @see lsm_connect_password
     * @param[in]   volume          Volume that is to be deleted.
     * @param[out]  job             Indicates job id
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsm_volume_delete(lsm_connect *conn, lsm_volume *volume,
                                        char **job, lsm_flag flags);

    /**
     * Set a Volume to online
     * @param[in] conn                  Valid connection @see lsm_connect_password
     * @param[in] volume                Volume that is to be placed online
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code
     */
    int LSM_DLL_EXPORT lsm_volume_enable(lsm_connect *conn, lsm_volume *volume,
                                        lsm_flag flags);

    /**
     * Set a Volume to offline
     * @param[in] conn                  Valid connection @see lsm_connect_password
     * @param[in] volume                Volume that is to be placed online
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code
     */
    int LSM_DLL_EXPORT lsm_volume_disable(lsm_connect *conn,
                                        lsm_volume *volume, lsm_flag flags);

    /**
     * Set the username password for CHAP authentication, inbound and outbound.
     * @param conn                      Valid connection pointer
     * @param init_id                   Initiator ID
     * @param in_user                   inbound user name
     * @param in_password               inbound password
     * @param out_user                  outbound user name
     * @param out_password              outbound password
     * @param flags                     Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code.
     */
    int LSM_DLL_EXPORT lsm_iscsi_chap_auth(lsm_connect *conn,
                                                    const char *init_id,
                                                    const char *in_user,
                                                    const char *in_password,
                                                    const char *out_user,
                                                    const char *out_password,
                                                    lsm_flag flags);

    /**
     * Retrieves a list of access groups.
     * @param[in] conn              Valid connection @see lsm_connect_password
     * @param[in] search_key        Search key (NULL for all)
     * @param[in] search_value      Search value
     * @param[out] groups           Array of access groups
     * @param[out] group_count      Size of array
     * @param[in] flags             Reserved set to zero
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_access_group_list(lsm_connect *conn,
                                            const char *search_key,
                                            const char *search_value,
                                            lsm_access_group **groups[],
                                            uint32_t *group_count,
                                            lsm_flag flags);

    /**
     * Creates a new access group with one initiator in it.
     * @param[in] conn                  Valid connection @see lsm_connect_password
     * @param[in] name                  Name of access group
     * @param[in] init_id               Initiator id to be added to group
     * @param[in] init_type             Initiator type
     * @param[in] system                System to create access group for
     * @param[out] access_group         Returned access group
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsm_access_group_create(lsm_connect *conn,
                                                const char *name,
                                                const char *init_id,
                                                lsm_access_group_init_type init_type,
                                                lsm_system *system,
                                                lsm_access_group **access_group,
                                                lsm_flag flags);

    /**
     * Deletes an access group.
     * @param[in] conn                  Valid connection @see lsm_connect_password
     * @param[in] access_group          Group to delete
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_access_group_delete(lsm_connect *conn,
                                            lsm_access_group *access_group,
                                            lsm_flag flags);

    /**
     * Adds an initiator to the access group
     * @param[in] conn                  Valid connection @see lsm_connect_password
     * @param[in] access_group          Group to modify
     * @param[in] init_id               Initiator to add to group
     * @param[in] init_type             Type of initiator
     * @param[out] updated_access_group Updated access group
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_access_group_initiator_add(lsm_connect *conn,
                                lsm_access_group *access_group,
                                const char *init_id,
                                lsm_access_group_init_type init_type,
                                lsm_access_group **updated_access_group,
                                lsm_flag flags);

    /**
     * Removes an initiator from an access group.
     * @param[in] conn                  Valid connection @see lsm_connect_password
     * @param[in] access_group          Group to modify
     * @param[in] initiator_id          Initiator to delete from group
     * @param[in] init_type             Type of initiator, enumerated type
     * @param[out] updated_access_group Updated access group
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return[in] LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_access_group_initiator_delete(lsm_connect *conn,
                                        lsm_access_group *access_group,
                                        const char *initiator_id,
                                        lsm_access_group_init_type init_type,
                                        lsm_access_group **updated_access_group,
                                        lsm_flag flags);

    /**
     * Grants access to a volume for the specified group
     * @param[in] conn                  Valid connection
     * @param[in] access_group          Valid group pointer
     * @param[in] volume                Valid volume pointer
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_volume_mask(lsm_connect *conn,
                                            lsm_access_group *access_group,
                                            lsm_volume *volume,
                                            lsm_flag flags);

    /**
     * Revokes access to a volume for the specified group
     * @param[in] conn                  Valid connection
     * @param[in] access_group          Valid group pointer
     * @param[in] volume                Valid volume pointer
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_volume_unmask(lsm_connect *conn,
                                            lsm_access_group *access_group,
                                            lsm_volume *volume,
                                            lsm_flag flags);

    /**
     * Returns those volumes that the specified group has access to.
     * @param[in] conn                  Valid connection
     * @param[in] group                 Valid group
     * @param[out] volumes              An array of volumes
     * @param[out] count                Number of volumes
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_volumes_accessible_by_access_group(lsm_connect *conn,
                                                        lsm_access_group *group,
                                                        lsm_volume **volumes[],
                                                        uint32_t *count,
                                                        lsm_flag flags);

    /**
     * Retrieves the access groups that have access to the specified volume.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] groups               An array of access groups
     * @param[out] group_count          Number of access groups
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_access_groups_granted_to_volume(lsm_connect *conn,
                                                    lsm_volume *volume,
                                                    lsm_access_group **groups[],
                                                    uint32_t *group_count,
                                                    lsm_flag flags);

    /**
     * Returns 1 if the specified volume has child dependencies.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] yes                  1 == Yes, 0 == No
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_volume_child_dependency(lsm_connect *conn,
                                                lsm_volume *volume,
                                                uint8_t *yes,
                                                lsm_flag flags);

    /**
     * Instructs the array to remove all child dependencies by replicating
     * required storage.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] job                  Job id
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_volume_child_dependency_delete(lsm_connect *conn,
                                                    lsm_volume *volume,
                                                    char **job, lsm_flag flags);

    /**
     * Retrieves information about the different arrays accessible.
     * NOTE: Free returned systems by calling to lsm
     * @param[in]  conn                 Valid connection
     * @param[out] systems              Array of lsm_system
     * @param[out] system_count         Number of systems
     * @param[in]  flags                Reserved set to zero
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsm_system_list(lsm_connect *conn, lsm_system **systems[],
                                        uint32_t *system_count, lsm_flag flags);

    /**
     * Retrieves information about the available file systems.
     * @param[in] conn                  Valid connection
     * @param[in]  search_key           Search key (NULL for all)
     * @param[in]  search_value         Search value
     * @param[out] fs                   Array of lsm_fs
     * @param[out] fs_count             Number of file systems
     * @param[in]  flags                Reserved set to zero
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsm_fs_list(lsm_connect *conn, const char *search_key,
                                    const char *search_value, lsm_fs **fs[],
                                    uint32_t *fs_count, lsm_flag flags);

    /**
     * Creates a new file system from the specified pool
     * @param[in] conn              Valid connection
     * @param[in] pool              Valid pool
     * @param[in] name              File system name
     * @param[in] size_bytes        Size of file system in bytes
     * @param[out] fs               Newly created fs
     * @param[out] job              Job id if job is async.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsm_fs_create(lsm_connect *conn, lsm_pool *pool,
                                    const char *name, uint64_t size_bytes,
                                    lsm_fs **fs, char **job, lsm_flag flags);

    /**
     * Deletes a file system
     * @param[in] conn              Valid connection
     * @param fs                    File system to delete
     * @param job                   Job id if job is created async.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsm_fs_delete(lsm_connect *conn, lsm_fs *fs, char **job,
                                    lsm_flag flags);

    /**
     * Clones an existing file system
     * @param conn                  Valid connection
     * @param src_fs                Source file system
     * @param name                  Name of new file system
     * @param optional_ss           Optional snapshot to base clone from
     * @param cloned_fs             Newly cloned file system record
     * @param job                   Job id if operation is async.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on succees, LSM_ERR_JOB_STARTED if async., else
     * error code.
     */
    int LSM_DLL_EXPORT lsm_fs_clone(lsm_connect *conn, lsm_fs *src_fs,
                                    const char *name, lsm_fs_ss *optional_ss,
                                    lsm_fs **cloned_fs,
                                    char **job, lsm_flag flags);

    /**
     * Checks to see if the specified file system has a child dependency.
     * @param[in] conn                  Valid connection
     * @param[in] fs                    Specific file system
     * @param[in] files                 Specific files to check (NULL OK)
     * @param[out] yes                  Zero indicates no, else yes
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code.
     */
    int LSM_DLL_EXPORT lsm_fs_child_dependency( lsm_connect *conn, lsm_fs *fs,
                                                lsm_string_list *files,
                                                uint8_t *yes, lsm_flag flags);

    /**
     * Removes child dependencies by duplicating the required storage to remove.
     * Note: This could take a long time to complete based on dependencies.
     * @param[in] conn                  Valid connection
     * @param[in] fs                    File system to remove dependencies for
     * @param[in] files                 Specific files to check (NULL OK)
     * @param[out] job                  Job id for async. identification
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsm_fs_child_dependency_delete( lsm_connect *conn, lsm_fs *fs,
                                                lsm_string_list *files,
                                                char **job, lsm_flag flags);

    /**
     * Resizes a file system
     * @param[in] conn                  Valid connection
     * @param[in] fs                    File system to re-size
     * @param[in] new_size_bytes        New size of fs
     * @param[out] rfs                  File system information for re-sized fs
     * @param[out] job_id               Job id for async. identification
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsm_fs_resize(lsm_connect *conn, lsm_fs *fs,
                                    uint64_t new_size_bytes, lsm_fs **rfs,
                                    char **job_id, lsm_flag flags);

    /**
     * Clones a file on a file system.
     * @param[in] conn                  Valid connection
     * @param[in] fs                    File system which file resides
     * @param[in] src_file_name         Source file relative name & path
     * @param[in] dest_file_name        Dest. file relative name & path
     * @param[in] snapshot              Optional backing snapshot
     * @param[out] job                  Job id for async. operation
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsm_fs_file_clone(lsm_connect *conn, lsm_fs *fs,
                                        const char *src_file_name,
                                        const char *dest_file_name,
                                        lsm_fs_ss *snapshot, char **job,
                                        lsm_flag flags);

    /**
     * Return a list of snapshots
     * @param[in] conn          Valid connection
     * @param[in] fs            File system to check for snapshots
     * @param[out] ss           An array of snapshot pointers
     * @param[out] ss_count     Number of elements in the array
     * @param[in]  flags        Reserved set to zero
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsm_fs_ss_list(lsm_connect *conn, lsm_fs *fs,
                                    lsm_fs_ss **ss[], uint32_t *ss_count,
                                    lsm_flag flags);

    /**
     * Creates a snapshot
     * @param[in] c                     Valid connection
     * @param[in] fs                    File system to snapshot
     * @param[in] name                  Name of snap shot
     * @param[out] snapshot             Snapshot that was created
     * @param[out] job                  Job id if the operation is async.
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async.,
     * else error code
     */
    int LSM_DLL_EXPORT lsm_fs_ss_create(lsm_connect *c, lsm_fs *fs,
                                    const char *name,
                                    lsm_fs_ss **snapshot, char **job,
                                    lsm_flag flags);

    /**
     * Deletes a snapshot
     * @param[in] c                 Valid connection
     * @param[in] fs                File system
     * @param[in] ss                Snapshot to delete
     * @param[out] job              Job id if the operation is async.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async., else error
     * code.
     */
    int LSM_DLL_EXPORT lsm_fs_ss_delete(lsm_connect *c, lsm_fs *fs, lsm_fs_ss *ss,
                                    char **job, lsm_flag flags);

    /**
     * Restores a file system or files to a previous state as specified in the
     * snapshot.
     * @param c                     Valid connection
     * @param fs                    File system which contains the snapshot
     * @param ss                    Snapshot to restore to
     * @param files                 Optional list of files to restore
     * @param restore_files         Optional list of file names to restore to
     * @param all_files             0 = False else True
     * @param job                   Job id if operation is async.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async.,
     * else error code
     */
    int LSM_DLL_EXPORT lsm_fs_ss_restore(lsm_connect *c, lsm_fs *fs, lsm_fs_ss *ss,
                                    lsm_string_list *files,
                                    lsm_string_list *restore_files,
                                    int all_files, char **job, lsm_flag flags);

    /**
     * Returns the types of NFS client authentication the array supports.
     * @param[in] c                     Valid connection
     * @param[out] types                List of types
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code.
     */
    int LSM_DLL_EXPORT lsm_nfs_auth_types( lsm_connect *c,
                                            lsm_string_list **types,
                                            lsm_flag flags);

    /**
     * Lists the nfs exports on the specified array.
     * @param[in] c                     Valid connection
     * @param[in] search_key            Search key (NULL for all)
     * @param[in] search_value          Search value
     * @param[out] exports              An array of lsm_nfs_export
     * @param[out] count                Number of items in array
     * @param[in]  flags                Reserved set to zero
     * @return LSM_ERR_OK on success else error code.
     */
    int LSM_DLL_EXPORT lsm_nfs_list(lsm_connect *c, const char *search_key,
                                            const char *search_value,
                                            lsm_nfs_export **exports[],
                                            uint32_t *count, lsm_flag flags);

    /**
     * Creates or modifies an NFS export.
     * @param[in] c                  Valid connection
     * @param[in] fs_id              File system ID to export via NFS
     * @param[in] export_path        Export path
     * @param[in] root_list          List of hosts that have root access
     * @param[in] rw_list            List of hosts that have read/write access
     * @param[in] ro_list            List of hosts that have read only access
     * @param[in] anon_uid           UID to map to anonymous
     * @param[in] anon_gid           GID to map to anonymous
     * @param[in] auth_type          Array specific authentication types
     * @param[in] options            Array specific options
     * @param[out]  exported         Export record
     * @param[in]  flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsm_nfs_export_fs( lsm_connect *c,
                                        const char *fs_id,
                                        const char *export_path,
                                        lsm_string_list *root_list,
                                        lsm_string_list *rw_list,
                                        lsm_string_list *ro_list,
                                        uint64_t anon_uid,
                                        uint64_t anon_gid,
                                        const char *auth_type,
                                        const char *options,
                                        lsm_nfs_export **exported,
                                        lsm_flag flags
                                        );

    /**
     * Delete the export.
     * @param[in] c             Valid connection
     * @param[in] e             NFS export to remove
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success else error code.
     */
    int LSM_DLL_EXPORT lsm_nfs_export_delete( lsm_connect *c, lsm_nfs_export *e,
                                            lsm_flag flags );

/**
 * Retrieve a list of target ports
 * @param[in] c                     Valid connection
 * @param[in] search_key            Search key (NULL for all)
 * @param[in] search_value          Search value
 * @param[out] target_ports         Array of target ports
 * @param[out] count                Number of target ports
 * @param[in] flags                 Reserved, set to 0
 * @return LSM_ERR_OK on success else error reason
 */
    int LSM_DLL_EXPORT lsm_target_port_list(lsm_connect *c,
                                            const char *search_key,
                                            const char *search_value,
                                            lsm_target_port **target_ports[],
                                            uint32_t *count,
                                            lsm_flag flags);

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_H */
