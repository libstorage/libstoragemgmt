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

#ifndef LIBSTORAGEMGMT_H
#define LIBSTORAGEMGMT_H

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

    /**
     * Get a connection to a storage provider.
     * @param[in] uri       Uniform Resource Identifier (see URI documentation)
     * @param[in] password  Password for the storage array (optional, can be NULL)
     * @param[out] conn     The connection to use for all the other library calls
     * @param[in] timeout   Time-out in milli-seconds, (initial value).
     * @param[out] e        Error data if connection failed.
     * @param[in] flags     Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code @see lsmErrorNumber
     */
    int LSM_DLL_EXPORT lsmConnectPassword(const char* uri, const char *password,
        lsmConnectPtr *conn, uint32_t timeout, lsmErrorPtr *e, lsmFlag_t flags);
    /**
     * Closes a connection to a storage provider.
     * @param[in] conn      Valid connection to close
     * @param[in] flags     Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code @see lsmErrorNumber
     */
    int LSM_DLL_EXPORT lsmConnectClose(lsmConnectPtr conn, lsmFlag_t flags);

    /**
     * Sets the time-out for this connection.
     * @param[in] conn          Valid connection @see lsmConnectUserPass
     * @param[in] timeout       Time-out (in ms)
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmConnectSetTimeout(lsmConnectPtr conn,
                                            uint32_t timeout, lsmFlag_t flags);

    /**
     * Gets the time-out for this connection.
     * @param[in]   conn        Valid connection @see lsmConnectUserPass
     * @param[out]  timeout     Time-out (in ms)
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmConnectGetTimeout(lsmConnectPtr conn,
                                            uint32_t *timeout, lsmFlag_t flags);

    /**
     * Check on the status of a job, no data to return on completion.
     * @param[in] conn              Valid connection
     * @param[in] job_id            Job id
     * @param[out] status           Job Status
     * @param[out] percentComplete  Percent job complete
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmJobStatusGet(lsmConnectPtr conn, const char *job_id,
                                lsmJobStatus *status, uint8_t *percentComplete,
                                lsmFlag_t flags);

    /**
     * Check on the status of a job and returns the volume information when
     * complete.
     * @param[in] conn              Valid connection pointer.
     * @param[in] job_id            Job to check status on
     * @param[out] status           What is the job status
     * @param[out] percentComplete  Domain 0..100
     * @param[out] vol              lsmVolumePtr for completed operation.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmJobStatusVolumeGet(lsmConnectPtr conn,
                                const char *job_id, lsmJobStatus *status,
                                uint8_t *percentComplete, lsmVolumePtr *vol,
                                lsmFlag_t flags);


    /**
     * Check on the status of a job and return the fs information when complete.
     * @param[in] conn                  Valid connection pointer
     * @param[in] job_id                Job to check
     * @param[out] status               What is the job status
     * @param[out] percentComplete      Percent of job complete
     * @param[out] fs                   lsmFsPtr for the completed operation
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmJobStatusFsGet(lsmConnectPtr conn, const char *job_id,
                                lsmJobStatus *status, uint8_t *percentComplete,
                                lsmFsPtr *fs, lsmFlag_t flags);

    /**
     * Check on the status of a job and return the snapshot information when
     * compete.
     * @param[in] c                     Valid connection pointer
     * @param[in] job                   Job id to check
     * @param[out] status               Job status
     * @param[out] percentComplete      Percent complete
     * @param[out] ss                   Snap shot information
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmJobStatusSsGet(lsmConnectPtr c, const char *job,
                                lsmJobStatus *status, uint8_t *percentComplete,
                                lsmSsPtr *ss, lsmFlag_t flags);

    /**
     * Frees the resources used by a job.
     * @param[in] conn
     * @param[in] jobNumber
     * @param[in] flags     Reserved for future use, must be zero.
     * @return LSM_ERROR_OK, else error reason.
     */
    int LSM_DLL_EXPORT lsmJobFree(lsmConnectPtr conn, char **jobNumber,
                                    lsmFlag_t flags);
    /**
     * Storage system query functions
     */

    /**
     * Query the capabilities of the storage array.
     * @param[in]   conn    Valid connection @see lsmConnectUserPass
     * @param[in]   system  System of interest
     * @param[out]  cap     The storage array capabilities
     * @param[in] flags     Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsmCapabilities(lsmConnectPtr conn,
                                        lsmSystemPtr system,
                                        lsmStorageCapabilitiesPtr *cap,
                                        lsmFlag_t flags);

    /**
     * Query the list of storage pools on the array.
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[out]  poolArray       Array of storage pools
     * @param[out]  count           Number of storage pools
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsmPoolList(lsmConnectPtr conn, lsmPoolPtr **poolArray,
                                    uint32_t *count, lsmFlag_t flags);

    /**
     * Query the list of initiators known to the array
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[out] initiators       Array of initiators
     * @param[out] count            Number of initiators
     * @param[in] flags             Reserved for future use, must be zero.
     * @return  LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsmInitiatorList(lsmConnectPtr conn,
                                        lsmInitiatorPtr **initiators,
                                        uint32_t *count, lsmFlag_t flags);

    /**
     * Volume management functions
     */

    /**
     * Gets a list of logical units for this array.
     * @param[in]   conn        Valid connection @see lsmConnectUserPass
     * @param[out]   volumes    An array of lsmVolume_t
     * @param[out]   count      Number of elements in the lsmVolume_t array
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsmVolumeList(lsmConnectPtr conn, lsmVolumePtr **volumes,
                                        uint32_t *count, lsmFlag_t flags);

    /**
     * Creates a new volume (aka. LUN).
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[in]   pool            Valid pool @see lsmPool_t
     * @param[in]   volumeName      Human recognizable name (not all arrays support)
     * @param[in]   size            Size of new volume in bytes (actual size will
     *                              be based on array rounding to blocksize)
     * @param[in]   provisioning    Type of volume provisioning to use
     * @param[out]  newVolume       Valid volume @see lsmVolume_t
     * @param[out]  job             Indicates job id
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmVolumeCreate(lsmConnectPtr conn, lsmPoolPtr pool,
                                        const char *volumeName, uint64_t size,
                                        lsmProvisionType provisioning,
                                        lsmVolumePtr *newVolume, char **job,
                                        lsmFlag_t flags);

    /**
     * Resize an existing volume.
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[in]   volume          volume to resize
     * @param[in]   newSize         New size of volume
     * @param[out]  resizedVolume   Pointer to newly resized lun.
     * @param[out]  job             Indicates job id
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmVolumeResize(lsmConnectPtr conn, lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                char **job, lsmFlag_t flags);

    /**
     * Replicates a volume
     * @param[in] conn              Valid connection @see lsmConnectUserPass
     * @param[in] pool              Valid pool
     * @param[in] repType           Type of replication lsmReplicationType
     * @param[in] volumeSrc         Which volume to replicate
     * @param[in] name              Human recognizable name (not all arrays support)
     * @param[out] newReplicant     New replicated volume lsmVolume_t
     * @param[out] job              Indicates job id
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmVolumeReplicate(lsmConnectPtr conn, lsmPoolPtr pool,
                            lsmReplicationType repType, lsmVolumePtr volumeSrc,
                            const char *name, lsmVolumePtr *newReplicant,
                            char **job, lsmFlag_t flags);

    /**
     * Unit of block size for the replicate range method.
     * @param[in] conn                  Valid connection
     * @param[out] bs                   Block size
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumeReplicateRangeBlockSize(lsmConnectPtr conn,
                                                        uint32_t *bs,
                                                        lsmFlag_t flags);

    /**
     * Replicates a portion of a volume to a volume.
     * @param[in] conn                  Valid connection
     * @param[in] repType               Replication type
     * @param[in] source                Source volume
     * @param[in] dest                  Destination volume (can be same as source)
     * @param[in] ranges                An array of block ranges
     * @param[in] num_ranges            Number of entries in ranges.
     * @param[out] job                  Indicates job id
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async., else error code
     */
    int LSM_DLL_EXPORT lsmVolumeReplicateRange(lsmConnectPtr conn,
                                                lsmReplicationType repType,
                                                lsmVolumePtr source,
                                                lsmVolumePtr dest,
                                                lsmBlockRangePtr *ranges,
                                                uint32_t num_ranges, char **job,
                                                lsmFlag_t flags);

    /**
     * Deletes a logical unit and data is lost!
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[in]   volume          Volume that is to be deleted.
     * @param[out]  job             Indicates job id
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmVolumeDelete(lsmConnectPtr conn, lsmVolumePtr volume,
                                        char **job, lsmFlag_t flags);

    /**
     * Set a Volume to online
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] volume                Volume that is to be placed online
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code
     */
    int LSM_DLL_EXPORT lsmVolumeOnline(lsmConnectPtr conn, lsmVolumePtr volume,
                                        lsmFlag_t flags);

    /**
     * Set a Volume to offline
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] volume                Volume that is to be placed online
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code
     */
    int LSM_DLL_EXPORT lsmVolumeOffline(lsmConnectPtr conn,
                                        lsmVolumePtr volume, lsmFlag_t flags);

    /**
     * Set the username/password for CHAP authentication.
     * @param conn                      Valid connection pointer
     * @param initiator                 Valid initiator pointer
     * @param username                  Username
     * @param password                  Password
     * @param flags                     Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code.
     */
    int LSM_DLL_EXPORT lsmISCSIChapAuthInbound(lsmConnectPtr conn,
                                                    lsmInitiatorPtr initiator,
                                                    const char *username,
                                                    const char *password,
                                                    lsmFlag_t flags);

    /**
     * Access control for allowing an initiator to use a volume.
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] initiator_id          Initiator to grant access to volume
     * @param[in] initiator_type        Type of initiator we are adding
     * @param[in] volume                Volume to allow access to
     * @param[in] access                Type of access
     * @param[out] job_id                  Job id when method is async.
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     *          else error code
     */
    int LSM_DLL_EXPORT lsmInitiatorGrant(lsmConnectPtr conn,
                                        const char *initiator_id,
                                        lsmInitiatorType initiator_type,
                                        lsmVolumePtr volume,
                                        lsmAccessType access,
                                        char **job_id,
                                        lsmFlag_t flags);

    /**
     * Revokes privileges an initiator has to a volume
     * @param[in] conn          Valid connection
     * @param[in] initiator     Valid initiator
     * @param[in] volume        Valid volume
     * @param[out] job          Job id for async
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     *          else error code
     */
    int LSM_DLL_EXPORT lsmInitiatorRevoke(lsmConnectPtr conn,
                                        lsmInitiatorPtr initiator,
                                        lsmVolumePtr volume,
                                        char **job,
                                        lsmFlag_t flags);

    /**
     * Retrieves a list of access groups.
     * @param[in] conn              Valid connection @see lsmConnectUserPass
     * @param[out] groups           Array of access groups
     * @param[out] groupCount       Size of array
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupList(lsmConnectPtr conn,
                                            lsmAccessGroupPtr **groups,
                                            uint32_t *groupCount,
                                            lsmFlag_t flags);

    /**
     * Creates a new access group with one initiator in it.
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] name                  Name of access group
     * @param[in] initiator_id          Initiator id to be added to group
     * @param[in] id_type               Initiator type
     * @param[in] system_id             System id to create access group for
     * @param[out] access_group         Returned access group
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmAccessGroupCreate(lsmConnectPtr conn,
                                                const char *name,
                                                const char *initiator_id,
                                                lsmInitiatorType id_type,
                                                const char *system_id,
                                                lsmAccessGroupPtr *access_group,
                                                lsmFlag_t flags);

    /**
     * Deletes an access group.
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] group                 Group to delete
     * @param[out] job                  Job ID
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupDel(lsmConnectPtr conn,
                                            lsmAccessGroupPtr group, char **job,
                                            lsmFlag_t flags);

    /**
     * Adds an initiator to the access group
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] group                 Group to modify
     * @param[in] initiator_id          Initiator to add to group
     * @param[in] id_type               Type of initiator
     * @param[out] job                  job id
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupAddInitiator(lsmConnectPtr conn,
                                lsmAccessGroupPtr group,
                                const char *initiator_id,
                                lsmInitiatorType id_type, char **job,
                                lsmFlag_t flags);

    /**
     * Removes an initiator from an access group.
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] group                 Group to modify
     * @param[in] initiator_id          Initiator to delete from group
     * @param[out] job                  job id
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return[in] LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupDelInitiator(lsmConnectPtr conn,
                                                    lsmAccessGroupPtr group,
                                                    const char *initiator_id,
                                                    char **job,
                                                    lsmFlag_t flags);

    /**
     * Grants access to a volume for the specified group
     * @param[in] conn                  Valid connection
     * @param[in] group                 Valid group pointer
     * @param[in] volume                Valid volume pointer
     * @param[in] access                Desired access
     * @param[out] job                  job id if all async.
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupGrant(lsmConnectPtr conn,
                                            lsmAccessGroupPtr group,
                                            lsmVolumePtr volume,
                                            lsmAccessType access, char **job,
                                            lsmFlag_t flags);

    /**
     * Revokes access to a volume for the specified group
     * @param[in] conn                  Valid connection
     * @param[in] group                 Valid group pointer
     * @param[in] volume                Valid volume pointer
     * @param[out] job                  job id if all async.
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupRevoke(lsmConnectPtr conn,
                                            lsmAccessGroupPtr group,
                                            lsmVolumePtr volume, char **job,
                                            lsmFlag_t flags);

    /**
     * Returns an array of volumes that are accessible by the initiator.
     * @param[in] conn                  Valid connection
     * @param[in] initiator             Valid initiator pointer
     * @param[out] volumes              An array of lsmVolumePtr
     * @param[out] count                Number of elements in array
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumesAccessibleByInitiator(lsmConnectPtr conn,
                                        lsmInitiatorPtr initiator,
                                        lsmVolumePtr **volumes,
                                        uint32_t *count, lsmFlag_t flags);


    /**
     * Returns an array of initiators that have access to a volume.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Volume to interrogate
     * @param[out] initiators           An array of lsmInitiatorPtr
     * @param[out] count                Number of elements in array
     * @param[in] flags                 Reserved for future use, must be zero
      * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmInitiatorsGrantedToVolume(lsmConnectPtr conn,
                                                lsmVolumePtr volume,
                                                lsmInitiatorPtr **initiators,
                                                uint32_t *count,
                                                lsmFlag_t flags);

    /**
     * Returns those volumes that the specified group has access to.
     * @param[in] conn                  Valid connection
     * @param[in] group                 Valid group
     * @param[out] volumes              An array of volumes
     * @param[out] count                Number of volumes
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumesAccessibleByAccessGroup(lsmConnectPtr conn,
                                                        lsmAccessGroupPtr group,
                                                        lsmVolumePtr **volumes,
                                                        uint32_t *count,
                                                        lsmFlag_t flags);

    /**
     * Retrieves the access groups that have access to the specified volume.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] groups               An array of access groups
     * @param[out] groupCount           Number of access groups
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupsGrantedToVolume(lsmConnectPtr conn,
                                                    lsmVolumePtr volume,
                                                    lsmAccessGroupPtr **groups,
                                                    uint32_t *groupCount,
                                                    lsmFlag_t flags);

    /**
     * Returns 1 if the specified volume has child dependencies.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] yes                  1 == Yes, 0 == No
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumeChildDependency(lsmConnectPtr conn,
                                                lsmVolumePtr volume,
                                                uint8_t *yes,
                                                lsmFlag_t flags);

    /**
     * Instructs the array to remove all child dependencies by replicating
     * required storage.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] job                  Job id
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumeChildDependencyRm(lsmConnectPtr conn,
                                                    lsmVolumePtr volume,
                                                    char **job, lsmFlag_t flags);

    /**
     * Retrieves information about the different arrays accessible.
     * NOTE: Free returned systems by calling to lsm
     * @param[in]  conn                 Valid connection
     * @param[out] systems              Array of lsmSystemPtr
     * @param[out] systemCount          Number of systems
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmSystemList(lsmConnectPtr conn, lsmSystemPtr **systems,
                                        uint32_t *systemCount, lsmFlag_t flags);

    /**
     * Retrieves information about the available file systems.
     * @param[in] conn                  Valid connection
     * @param[out] fs                   Array of lsmFsPtr
     * @param[out] fsCount              Number of file systems
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmFsList(lsmConnectPtr conn, lsmFsPtr **fs,
                                    uint32_t *fsCount, lsmFlag_t flags);

    /**
     * Creates a new fils system from the specified pool
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
    int LSM_DLL_EXPORT lsmFsCreate(lsmConnectPtr conn, lsmPoolPtr pool,
                                    const char *name, uint64_t size_bytes,
                                    lsmFsPtr *fs, char **job, lsmFlag_t flags);

    /**
     * Deletes a file system
     * @param[in] conn              Valid connection
     * @param fs                    File system to delete
     * @param job                   Job id if job is created async.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsmFsDelete(lsmConnectPtr conn, lsmFsPtr fs, char **job,
                                    lsmFlag_t flags);

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
    int LSM_DLL_EXPORT lsmFsClone(lsmConnectPtr conn, lsmFsPtr src_fs,
                                    const char *name, lsmSsPtr optional_ss,
                                    lsmFsPtr *cloned_fs,
                                    char **job, lsmFlag_t flags);

    /**
     * Checks to see if the specified file system has a child dependency.
     * @param[in] conn                  Valid connection
     * @param[in] fs                    Specific file system
     * @param[in] files                 Specific files to check (NULL OK)
     * @param[out] yes                  Zero indicates no, else yes
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code.
     */
    int LSM_DLL_EXPORT lsmFsChildDependency( lsmConnectPtr conn, lsmFsPtr fs,
                                                lsmStringListPtr files,
                                                uint8_t *yes, lsmFlag_t flags);

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
    int LSM_DLL_EXPORT lsmFsChildDependencyRm( lsmConnectPtr conn, lsmFsPtr fs,
                                                lsmStringListPtr files,
                                                char **job, lsmFlag_t flags);

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
    int LSM_DLL_EXPORT lsmFsResize(lsmConnectPtr conn, lsmFsPtr fs,
                                    uint64_t new_size_bytes, lsmFsPtr *rfs,
                                    char **job_id, lsmFlag_t flags);

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
    int LSM_DLL_EXPORT lsmFsFileClone(lsmConnectPtr conn, lsmFsPtr fs,
                                        const char *src_file_name,
                                        const char *dest_file_name,
                                        lsmSsPtr snapshot, char **job,
                                        lsmFlag_t flags);

    /**
     * Return a list of snapshots
     * @param[in] conn                  Valid connection
     * @param[in] fs                    File system to check for snapshots
     * @param[out] ss                   An array of snapshot pointers
     * @param[out] ssCount              Number of elements in the array
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmFsSsList(lsmConnectPtr conn, lsmFsPtr fs,
                                    lsmSsPtr **ss, uint32_t *ssCount,
                                    lsmFlag_t flags);

    /**
     * Creates a snapshot
     * @param[in] c                     Valid connection
     * @param[in] fs                    File system to snapshot
     * @param[in] name                  Name of snap shot
     * @param[in] files                 List of file names to snapshot (null OK)
     * @param[out] snapshot             Snapshot that was created
     * @param[out] job                  Job id if the operation is async.
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async.,
     * else error code
     */
    int LSM_DLL_EXPORT lsmFsSsCreate(lsmConnectPtr c, lsmFsPtr fs,
                                    const char *name, lsmStringListPtr files,
                                    lsmSsPtr *snapshot, char **job,
                                    lsmFlag_t flags);

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
    int LSM_DLL_EXPORT lsmFsSsDelete(lsmConnectPtr c, lsmFsPtr fs, lsmSsPtr ss,
                                    char **job, lsmFlag_t flags);

    /**
     * Reverts a file system or files to a previous state as specified in the
     * snapshot.
     * @param c                     Valid connection
     * @param fs                    File system which contains the snapshot
     * @param ss                    Snapshot to revert to
     * @param files                 Optional list of files to revert
     * @param restore_files         Optional list of file names to revert to
     * @param all_files             0 = False else True
     * @param job                   Job id if operation is async.
     * @param[in] flags             Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async.,
     * else error code
     */
    int LSM_DLL_EXPORT lsmFsSsRevert(lsmConnectPtr c, lsmFsPtr fs, lsmSsPtr ss,
                                    lsmStringListPtr files,
                                    lsmStringListPtr restore_files,
                                    int all_files, char **job, lsmFlag_t flags);

    /**
     * Returns the types of NFS client authentication the array supports.
     * @param[in] c                     Valid connection
     * @param[out] types                List of types
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success, else error code.
     */
    int LSM_DLL_EXPORT lsmNfsAuthTypes( lsmConnectPtr c,
                                            lsmStringListPtr *types,
                                            lsmFlag_t flags);

    /**
     * Lists the nfs exports on the specified array.
     * @param[in] c                     Valid connection
     * @param[out] exports              An array of lsmNfsExportPtr
     * @param[out] count                Number of items in array
     * @param[in] flags                 Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success else error code.
     */
    int LSM_DLL_EXPORT lsmNfsList( lsmConnectPtr c,
                                            lsmNfsExportPtr **exports,
                                            uint32_t *count, lsmFlag_t flags);

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
    int LSM_DLL_EXPORT lsmNfsExportFs( lsmConnectPtr c,
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

    /**
     * Remove the export.
     * @param[in] c             Valid connection
     * @param[in] e             NFS export to remove
     * @param[in] flags         Reserved for future use, must be zero.
     * @return LSM_ERR_OK on success else error code.
     */
    int LSM_DLL_EXPORT lsmNfsExportRemove( lsmConnectPtr c, lsmNfsExportPtr e,
                                            lsmFlag_t flags );

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_H */

