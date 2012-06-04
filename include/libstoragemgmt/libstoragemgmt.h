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
     * @return LSM_ERR_OK on success, else error code @see lsmErrorNumber
     */
    int LSM_DLL_EXPORT lsmConnectPassword(const char* uri, const char *password,
        lsmConnectPtr *conn, uint32_t timeout, lsmErrorPtr *e);
    /**
     * Closes a connection to a storage provider.
     * @param[in] conn          Valid connection to close
     * @return LSM_ERR_OK on success, else error code @see lsmErrorNumber
     */
    int LSM_DLL_EXPORT lsmConnectClose(lsmConnectPtr conn);

    /**
     * Sets the time-out for this connection.
     * @param[in] conn          Valid connection @see lsmConnectUserPass
     * @param[in] timeout       Time-out (in ms)
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmConnectSetTimeout(lsmConnectPtr conn,
                                            uint32_t timeout);

    /**
     * Gets the time-out for this connection.
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[out]  timeout         Time-out (in ms)
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmConnectGetTimeout(lsmConnectPtr conn,
                                            uint32_t *timeout);

    /**
     * Check on the status of a job, no data to return on completion.
     * @param[in] conn              Valid connection
     * @param[int] job_id           Job id
     * @param[out] status           Job Status
     * @param[out] percentComplete  Percent job complete
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmJobStatusGet(lsmConnectPtr conn, const char *job_id,
                                lsmJobStatus *status, uint8_t *percentComplete);

    /**
     * Check on the status of a job and returns the volume information when
     * complete.
     * @param[in] conn              Valid connection pointer.
     * @param[in] job_id            Job to check status on
     * @param[out] status           What is the job status
     * @param[out] percentComplete  Domain 0..100
     * @param[out] vol              lsmVolumePtr for completed operation.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmJobStatusVolumeGet(lsmConnectPtr conn, const char *job_id,
                                lsmJobStatus *status, uint8_t *percentComplete,
                                lsmVolumePtr *vol);


    /**
     * Check on the status of a job and return the fs information when complete.
     * @param[in] conn                  Valid connection pointer
     * @param[in] job_id                Job to check
     * @param[out] status               What is the job status
     * @param[out] percentComplete      Percent of job complete
     * @param[out] fs                   lsmFsPtr for the completed operation
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmJobStatusFsGet(lsmConnectPtr conn, const char *job_id,
                                lsmJobStatus *status, uint8_t *percentComplete,
                                lsmFsPtr *fs);

    /**
     * Check on the status of a job and return the snapshot information when
     * compete.
     * @param[in] c                     Valid connection pointer
     * @param[in] job                   Job id to check
     * @param[out] status               Job status
     * @param[out] percentComplete      Percent complete
     * @param[out] ss                   Snap shot information
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmJobStatusSsGet(lsmConnectPtr c, const char *job,
                                lsmJobStatus *status, uint8_t *percentComplete,
                                lsmSsPtr *ss);

    /**
     * Frees the resources used by a job.
     * @param[in] conn
     * @param[in] jobNumber
     * @return LSM_ERROR_OK, else error reason.
     */
    int LSM_DLL_EXPORT lsmJobFree(lsmConnectPtr conn, char **jobNumber);
    /**
     * Storage system query functions
     */

    /**
     * Query the capabilities of the storage array.
     * @param[in]   conn    Valid connection @see lsmConnectUserPass
     * @param[out]  cap     The storage array capabilities
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsmCapabilities(lsmConnectPtr conn,
                                        lsmStorageCapabilitiesPtr *cap);

    /**
     * Query the list of storage pools on the array.
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[out]  poolArray       Array of storage pools
     * @param[out]  count           Number of storage pools
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsmPoolList(lsmConnectPtr conn, lsmPoolPtr **poolArray,
                                    uint32_t *count);

    /**
     * Query the list of initiators known to the array
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[out] initiators       Array of initiators
     * @param[out] count            Number of initiators
     * @return  LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsmInitiatorList(lsmConnectPtr conn,
        lsmInitiatorPtr **initiators,
        uint32_t *count);

    /**
     * Volume management functions
     */

    /**
     * Gets a list of logical units for this array.
     * @param[in]   conn    Valid connection @see lsmConnectUserPass
     * @param[out]   volumes    An array of lsmVolume_t
     * @param[out]   count   Number of elements in the lsmVolume_t array
     * @return LSM_ERR_OK on success else error reason
     */
    int LSM_DLL_EXPORT lsmVolumeList(lsmConnectPtr conn, lsmVolumePtr **volumes,
                                        uint32_t *count);

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
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmVolumeCreate(lsmConnectPtr conn, lsmPoolPtr pool,
                                        const char *volumeName, uint64_t size,
                                        lsmProvisionType provisioning,
                                        lsmVolumePtr *newVolume, char **job);

    /**
     * Resize an existing volume.
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[in]   volume          volume to resize
     * @param[in]   newSize         New size of volume
     * @param[out]  resizedVolume   Pointer to newly resized lun.
     * @param[out]  job             Indicates job id
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmVolumeResize(lsmConnectPtr conn, lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                char **job);

    /**
     * Replicates a volume
     * @param[in] conn              Valid connection @see lsmConnectUserPass
     * @param[in] pool              Valid pool
     * @param[in] repType           Type of replication lsmReplicationType
     * @param[in] volumeSrc         Which volume to replicate
     * @param[in] name              Human recognizable name (not all arrays support)
     * @param[out] newReplicant     New replicated volume lsmVolume_t
     * @param[out] job              Indicates job id
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmVolumeReplicate(lsmConnectPtr conn, lsmPoolPtr pool,
                            lsmReplicationType repType, lsmVolumePtr volumeSrc,
                            const char *name, lsmVolumePtr *newReplicant,
                            char **job);

    /**
     * Unit of block size for the replicate range method.
     * @param[in] conn                  Valid connection
     * @param[out] bs                   Block size
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumeReplicateRangeBlockSize(lsmConnectPtr conn,
                                                            uint32_t *bs);

    /**
     * Replicates a portion of a volume to a volume.
     * @param[in] conn                  Valid connection
     * @param[in] repType               Replication type
     * @param[in] source                Source volume
     * @param[in] dest                  Destination volume (can be same as source)
     * @param[in] ranges                An array of block ranges
     * @param[in] num_ranges            Number of entries in ranges.
     * @param[out] job                  Indicates job id
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async., else error code
     */
    int LSM_DLL_EXPORT lsmVolumeReplicateRange(lsmConnectPtr conn,
                                                lsmReplicationType repType,
                                                lsmVolumePtr source,
                                                lsmVolumePtr dest,
                                                lsmBlockRangePtr *ranges,
                                                uint32_t num_ranges, char **job);

    /**
     * Deletes a logical unit and data is lost!
     * @param[in]   conn            Valid connection @see lsmConnectUserPass
     * @param[in]   volume          Volume that is to be deleted.
     * @param[out]  job             Indicates job id
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmVolumeDelete(lsmConnectPtr conn, lsmVolumePtr volume,
                                        char **job);

    /**
     * Query the status of a volume
     * @param[in] conn              Valid connection @see lsmConnectUserPass
     * @param[in] volume            Storage volume to get status for
     * @param[out] status           Status of the volume
     * @return LSM_ERR_OK on success, else error code
     */
    int LSM_DLL_EXPORT lsmVolumeStatus(lsmConnectPtr conn, lsmVolumePtr volume,
                                        lsmVolumeStatusType *status);

    /**
     * Set a Volume to online
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] volume                Volume that is to be placed online
     * @return LSM_ERR_OK on success, else error code
     */
    int LSM_DLL_EXPORT lsmVolumeOnline(lsmConnectPtr conn, lsmVolumePtr volume);

    /**
     * Set a Volume to offline
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] volume                Volume that is to be placed online
     * @return LSM_ERR_OK on success, else error code
     */
    int LSM_DLL_EXPORT lsmVolumeOffline(lsmConnectPtr conn, lsmVolumePtr volume);

    /**
     * Used to create a record for an initiator.
     * Note: At the moment, this initiator record will get deleted automatically
     * if you call lsmAccessRemove and it is the last reference to any mapping.
     * @param[in] conn      Valid connection
     * @param[in] name      Name for the initiator (Note: Currently unable to retrieve)
     * @param[in] id        ID for the initiator
     * @param[in] type      Type of ID
     * @param[out] init     Newly created initiator.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmInitiatorCreate(lsmConnectPtr conn, const char *name,
                                            const char *id,
                                            lsmInitiatorType type,
                                            lsmInitiatorPtr *init);

    /**
     * Used to delete an initiator record.
     * @param[in] conn      Valid connection
     * @param[in] init      Initiator to delete
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmInitiatorDelete(lsmConnectPtr conn, lsmInitiatorPtr init);

    /**
     * Access control for allowing an initiator to use a volume.
     * Note: An access group will be created automatically with one initiator in it.
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] initiator             Initiator to grant access to volume
     * @param[in] volume                Volume to allow access to
     * @param[in] access                Type of access
     * @param[out] job                   Indicates job id
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. , else error code
     */
    int LSM_DLL_EXPORT lsmAccessGrant(lsmConnectPtr conn,
                                        lsmInitiatorPtr initiator,
                                        lsmVolumePtr volume,
                                        lsmAccessType access, char **job);

    /**
     * Revokes privileges an initiator has to a volume
     * @param[in] conn          Valid connection
     * @param[in] initiator     Valid initiator
     * @param[in] volume        Valid volume
     * @return LSM_ERR_OK, LSM_ERR_NO_MAPPING else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessRevoke(lsmConnectPtr conn,
                                        lsmInitiatorPtr initiator,
                                        lsmVolumePtr volume);

    /**
     * Retrieves a list of access groups.
     * @param[in] conn              Valid connection @see lsmConnectUserPass
     * @param[out] groups           Array of access groups
     * @param[out] groupCount       Size of array
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupList(lsmConnectPtr conn,
                                            lsmAccessGroupPtr **groups,
                                            uint32_t *groupCount);

    /**
     * Creates a new access group with one initiator in it.
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] name                  Name of access group
     * @param[in] initiator_id          Initiator id to be added to group
     * @param[in] id_type               Initiator type
     * @param[in] system_id             System id to create access group for
     * @param[out] access_group         Returned access group
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmAccessGroupCreate(lsmConnectPtr conn,
                                                const char *name,
                                                const char *initiator_id,
                                                lsmInitiatorType id_type,
                                                const char *system_id,
                                                lsmAccessGroupPtr *access_group);

    /**
     * Deletes an access group.
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] group                 Group to delete
     * @param[out] job                  Job ID
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupDel(lsmConnectPtr conn,
                                            lsmAccessGroupPtr group, char **job);

    /**
     * Adds an initiator to the access group
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] group                 Group to modify
     * @param[in] initiator_id          Initiator to add to group
     * @param[in] id_type               Type of initiator
     * @param[out] job                  job id
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupAddInitiator(lsmConnectPtr conn,
                                lsmAccessGroupPtr group,
                                const char *initiator_id,
                                lsmInitiatorType id_type, char **job);

    /**
     * Removes an initiator from an access group.
     * @param[in] conn                  Valid connection @see lsmConnectUserPass
     * @param[in] group                 Group to modify
     * @param[in] initiator             Initiator to delete from group
     * @param[out] job                  job id
     * @return[in] LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupDelInitiator(lsmConnectPtr conn,
                                                    lsmAccessGroupPtr group,
                                                    lsmInitiatorPtr initiator,
                                                    char **job);

    /**
     * Grants access to a volume for the specified group
     * @param[in] conn                  Valid connection
     * @param[in] group                 Valid group pointer
     * @param[in] volume                Valid volume pointer
     * @param[in] access                Desired access
     * @param[out] job                  job id if all async.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupGrant(lsmConnectPtr conn,
                                            lsmAccessGroupPtr group,
                                            lsmVolumePtr volume,
                                            lsmAccessType access, char **job);

    /**
     * Revokes access to a volume for the specified group
     * @param[in] conn                  Valid connection
     * @param[in] group                 Valid group pointer
     * @param[in] volume                Valid volume pointer
     * @param[out] job                  job id if all async.
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupRevoke(lsmConnectPtr conn,
                                            lsmAccessGroupPtr group,
                                            lsmVolumePtr volume, char **job);

    /**
     * Returns those volumes that the specified group has access to.
     * @param[in] conn                  Valid connection
     * @param[in] group                 Valid group
     * @param[out] volumes              An array of volumes
     * @param[out] count                Number of volumes
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumesAccessibleByAccessGroup(lsmConnectPtr conn,
                                                        lsmAccessGroupPtr group,
                                                        lsmVolumePtr **volumes,
                                                        uint32_t *count);

    /**
     * Retrieves the access groups that have access to the specified volume.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] groups               An array of access groups
     * @param[out] groupCount           Number of access groups
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmAccessGroupsGrantedToVolume(lsmConnectPtr conn,
                                                    lsmVolumePtr volume,
                                                    lsmAccessGroupPtr **groups,
                                                    uint32_t *groupCount);

    /**
     * Returns 1 if the specified volume has child dependencies.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] yes                  1 == Yes, 0 == No
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumeChildDependency(lsmConnectPtr conn,
                                                lsmVolumePtr volume,
                                                uint8_t *yes);

    /**
     * Instructs the array to remove all child dependencies by replicating
     * required storage.
     * @param[in] conn                  Valid connection
     * @param[in] volume                Valid volume
     * @param[out] job                  Job id
     * @return LSM_ERR_OK on success, else error reason.
     */
    int LSM_DLL_EXPORT lsmVolumeChildDependencyRm(lsmConnectPtr conn,
                                                    lsmVolumePtr volume,
                                                    char **job);

    /**
     * Retrieves information about the different arrays accessible.
     * NOTE: Free returned systems by calling to lsm
     * @param[in]  conn                  Valid connection
     * @param[out] systems               Array of lsmSystemPtr
     * @param[out] systemCount           Number of systems
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmSystemList(lsmConnectPtr conn, lsmSystemPtr **systems,
                                        uint32_t *systemCount);

    /**
     * Retrieves information about the available file systems.
     * @param[in] conn                      Valid connection
     * @param[out] fs                       Array of lsmFsPtr
     * @param[out] fsCount                  Number of file systems
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmFsList(lsmConnectPtr conn, lsmFsPtr **fs,
                                    uint32_t *fsCount);

    /**
     * Creates a new fils system from the specified pool
     * @param[in] conn              Valid connection
     * @param[in] pool              Valid pool
     * @param[in] name              File system name
     * @param[in] size_bytes        Size of file system in bytes
     * @param[out] fs               Newly created fs
     * @param[out] job              Job id if job is async.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsmFsCreate(lsmConnectPtr conn, lsmPoolPtr pool,
                                    const char *name, uint64_t size_bytes,
                                    lsmFsPtr *fs, char **job);

    /**
     * Deletes a file system
     * @param[in] conn              Valid connection
     * @param fs                    File system to delete
     * @param job                   Job id if job is created async.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsmFsDelete(lsmConnectPtr conn, lsmFsPtr fs, char **job);

    /**
     * Checks to see if the specified file system has a child dependency.
     * @param[in] conn                  Valid connection
     * @param[in] fs                    Specific file system
     * @param[in] files                 Specific files to check (NULL OK)
     * @param[out] yes                  Zero indicates no, else yes
     * @return LSM_ERR_OK on success, else error code.
     */
    int LSM_DLL_EXPORT lsmFsChildDependency( lsmConnectPtr conn, lsmFsPtr fs,
                                                lsmStringListPtr files,
                                                uint8_t *yes);

    /**
     * Removes child dependencies by duplicating the required storage to remove.
     * Note: This could take a long time to complete based on dependencies.
     * @param[in] conn                      Valid connection
     * @param[in] fs                        File system to remove dependencies for
     * @param[in] files                     Specific files to check (NULL OK)
     * @param[out] job                      Job id for async. identification
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsmFsChildDependencyRm( lsmConnectPtr conn, lsmFsPtr fs,
                                                lsmStringListPtr files,
                                                char **job);

    /**
     * Resizes a file system
     * @param[in] conn                  Valid connection
     * @param[in] fs                    File system to re-size
     * @param[in] new_size_bytes        New size of fs
     * @param[out] rfs                   File system information for re-sized fs
     * @return @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
     * else error code
     */
    int LSM_DLL_EXPORT lsmFsResize(lsmConnectPtr conn, lsmFsPtr fs,
                                    uint64_t new_size_bytes, lsmFsPtr *rfs,
                                    char **job);


    /**
     * Return a list of snapshots
     * @param[in] conn                  Valid connection
     * @param[int] fs                   File system to check for snapshots
     * @param[out] ss                   An array of snapshot pointers
     * @param[out] ssCount                   Number of elements in the array
     * @return LSM_ERR_OK on success, else error reason
     */
    int LSM_DLL_EXPORT lsmSsList(lsmConnectPtr conn, lsmFsPtr fs, lsmSsPtr **ss,
                                uint32_t *ssCount);

    /**
     * Creates a snapshot
     * @param[in] c                     Valid connection
     * @param[in] fs                    File system to snapshot
     * @param[in] name                  Name of snap shot
     * @param[in] files                 List of file names to snapshot (null OK)
     * @param[out] snapshot             Snapshot that was created
     * @param[out] job                  Job id if the operation is async.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async.,
     * else error code
     */
    int LSM_DLL_EXPORT lsmSsCreate(lsmConnectPtr c, lsmFsPtr fs,
                                    const char *name, lsmStringListPtr files,
                                    lsmSsPtr *snapshot, char **job);

    /**
     * Deletes a snapshot
     * @param[in] c                 Valid connection
     * @param[in] fs                File system
     * @param[in] ss                Snapshot to delete
     * @param[out] job              Job id if the operation is async.
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async., else error
     * code.
     */
    int LSM_DLL_EXPORT lsmSsDelete(lsmConnectPtr c, lsmFsPtr fs, lsmSsPtr ss,
                                    char **job);

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
     * @return LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async.,
     * else error code
     */
    int LSM_DLL_EXPORT lsmSsRevert(lsmConnectPtr c, lsmFsPtr fs, lsmSsPtr ss,
                                    lsmStringListPtr files,
                                    lsmStringListPtr restore_files,
                                    int all_files, char **job);

    /**
     * Returns the types of NFS client authentication the array supports.
     * @param[in] c                     Valid connection
     * @param[out] types                List of types
     * @return LSM_ERR_OK on success, else error code.
     */
    int LSM_DLL_EXPORT lsmNfsAuthTypes( lsmConnectPtr c,
                                            lsmStringListPtr *types);

    /**
     * Lists the nfs exports on the specified array.
     * @param c                         Valid connection
     * @param exports                   An array of lsmNfsExportPtr
     * @param count                     Number of items in array
     * @return LSM_ERR_OK on success else error code.
     */
    int LSM_DLL_EXPORT lsmNfsList( lsmConnectPtr c,
                                            lsmNfsExportPtr **exports,
                                            uint32_t *count);

    /**
     * Creates or modifies an NFS export.
     * @param[in] c                         Valid NFS connection
     * @param[in|out] e                     NFS volume to export
     * @return LSM_ERR_OK on success else error code.
     */
    int LSM_DLL_EXPORT lsmNfsExportFs( lsmConnectPtr c, lsmNfsExportPtr *e );

    /**
     *
     * @param c
     * @param e
     * @return LSM_ERR_OK on success else error code.
     */
    int LSM_DLL_EXPORT lsmNfsExportRemove( lsmConnectPtr c, lsmNfsExportPtr *e );

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_H */

