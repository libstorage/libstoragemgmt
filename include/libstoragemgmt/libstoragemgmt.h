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

#ifndef LIBSTORAGEMGMT_H
#define LIBSTORAGEMGMT_H

#include <stdint.h>
#include "libstoragemgmt_types.h"
#include "libstoragemgmt_error.h"
#include "libstoragemgmt_initiators.h"
#include "libstoragemgmt_pool.h"
#include "libstoragemgmt_volumes.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Get a connection to a storage provider.
 * @param[in] uri       Uniform Resource Identifier (see URI documentation)
 * @param[in] password  Password for the storage array (optional, can be NULL)
 * @param[out] conn     The connection to use for all the other library calls
 * @param[in] timeout   Time-out in seconds, (initial value).
 * @param[out] e        Error data if connection failed.
 * @return LSM_ERR_OK on success, else error code @see lsmErrorNumber
 */
int lsmConnectPassword(char* uri, char *password,
                        lsmConnectPtr *conn, uint32_t timeout, lsmErrorPtr *e);
/**
 * Closes a connection to a storage provider.
 * @param[in] conn          Valid connection to close
 * @return LSM_ERR_OK on success, else error code @see lsmErrorNumber
 */
int lsmConnectClose(lsmConnectPtr conn);

/**
 * Sets the time-out for this connection.
 * @param[in] conn          Valid connection @see lsmConnectUserPass
 * @param[in] timeout       Time-out (in ms)
 * @return LSM_ERR_OK on success, else error reason
 */
int lsmConnectSetTimeout(lsmConnectPtr conn, uint32_t timeout);

/**
 * Gets the time-out for this connection.
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[out]  timeout         Time-out (in ms)
 * @return LSM_ERR_OK on success, else error reason
 */
int lsmConnectGetTimeout(lsmConnectPtr conn, uint32_t *timeout);

/**
 * Check on the status of a job.
 * @param[in] conn              Valid connection pointer.
 * @param[in] jobNumber         Job to check status on
 * @param[out] status           What is the job status
 * @param[out] percentComplete  Domain 0..100
 * @param[out] vol              lsmVolumePtr for completed operation.
 * @return LSM_ERR_OK on success, else error reason.
 */
int lsmJobStatusGet( lsmConnectPtr conn, uint32_t jobNumber,
                        lsmJobStatus *status, uint8_t *percentComplete,
                        lsmVolumePtr *vol);

/**
 * Frees the resources used by a job.
 * @param[in] conn
 * @param[in] jobNumber
 * @return LSM_ERROR_OK, else error reason.
 */
int lsmJobFree(lsmConnectPtr conn, uint32_t jobNumber);
/**
 * Storage system query functions
 */

/**
 * Query the capabilities of the storage array.
 * @param[in]   conn    Valid connection @see lsmConnectUserPass
 * @param[out]  cap     The storage array capabilities
 * @return LSM_ERR_OK on success else error reason
 */
int lsmCapabilities(lsmConnectPtr conn, lsmStorageCapabilitiesPtr *cap);

/**
 * Query the list of storage pools on the array.
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[out]  poolArray       Array of storage pools
 * @param[out]  count           Number of storage pools
 * @return LSM_ERR_OK on success else error reason
 */
int lsmPoolList(lsmConnectPtr conn, lsmPoolPtr *poolArray[],
                        uint32_t *count);

/**
 * Query the list of initiators known to the array
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[out] initiators       Array of initiators
 * @param[out] count            Number of initiators
 * @return  LSM_ERR_OK on success else error reason
 */
int lsmInitiatorList(lsmConnectPtr conn, lsmInitiatorPtr *initiators[],
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
int lsmVolumeList(lsmConnectPtr conn, lsmVolumePtr *volumes[], uint32_t *count);

/**
 * Creates a new volume (aka. LUN).
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[in]   pool            Valid pool @see lsmPool_t
 * @param[in]   volumeName      Human recognizable name (not all arrays support)
 * @param[in]   size            Size of new volume in bytes (actual size will be based on array rounding to blocksize)
 * @param[in]   provisioning    Type of volume provisioning to use
 * @param[out]  newVolume       Valid volume @see lsmVolume_t
 * @param[out]  job             Indicates job number
 * @return LSM_ERR_OK on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmVolumeCreate(lsmConnectPtr conn, lsmPoolPtr pool, char *volumeName,
                        uint64_t size, lsmProvisionType provisioning,
                        lsmVolumePtr *newVolume, uint32_t *job);

/**
 * Resize an existing volume.
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[in]   volume          volume to resize
 * @param[in]   newSize         New size of volume
 * @param[out]  resizedVolume   Pointer to newly resized lun.
 * @param[out]  job             Indicates job number
 * @return LSM_ERR_OK on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmVolumeResize(lsmConnectPtr conn, lsmVolumePtr volume,
                        uint64_t newSize, lsmVolumePtr *resizedVolume,
                        uint32_t *job);

/**
 * Replicates a volume
 * @param[in] conn              Valid connection @see lsmConnectUserPass
 * @param[in] pool              Valid pool
 * @param[in] repType           Type of replication lsmReplicationType
 * @param[in] volumeSrc         Which volume to replicate
 * @param[in] name              Human recognizable name (not all arrays support)
 * @param[out] newReplicant     New replicated volume lsmVolume_t
 * @param[out] job              Indicates job number
 * @return LSM_ERR_OK on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmVolumeReplicate(lsmConnectPtr conn, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        char *name, lsmVolumePtr *newReplicant, uint32_t *job);

/**
 * Deletes a logical unit and data is lost!
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[in]   volume          Volume that is to be deleted.
 * @param[out]  job             Indicates job number
 * @return LSM_ERR_OK on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmVolumeDelete(lsmConnectPtr conn, lsmVolumePtr volume, uint32_t *job);

/**
 * Query the status of a volume
 * @param[in] conn              Valid connection @see lsmConnectUserPass
 * @param[in] volume            Storage volume to get status for
 * @param[out] status           Status of the volume
 * @return LSM_ERR_OK on success, else error code
 */
int lsmVolumeStatus(lsmConnectPtr conn, lsmVolumePtr volume,
                        lsmVolumeStatusType *status);

/**
 * Set a Volume to online
 * @param[in] conn                  Valid connection @see lsmConnectUserPass
 * @param[in] volume                Volume that is to be placed online
 * @return LSM_ERR_OK on success, else error code
 */
int lsmVolumeOnline(lsmConnectPtr conn, lsmVolumePtr volume);

/**
 * Set a Volume to offline
 * @param[in] conn                  Valid connection @see lsmConnectUserPass
 * @param[in] volume                Volume that is to be placed online
 * @return LSM_ERR_OK on success, else error code
 */
int lsmVolumeOffline(lsmConnectPtr conn, lsmVolumePtr volume);

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
int lsmInitiatorCreate(lsmConnectPtr conn, char *name, char *id,
                            lsmInitiatorType type, lsmInitiatorPtr *init);
/**
 * Access control for allowing an initiator to use a volume.
 * Note: An access group will be created automatically with one initiator in it.
 * @param[in] conn                  Valid connection @see lsmConnectUserPass
 * @param[in] initiator             Initiator to grant access to volume
 * @param[in] volume                Volume to allow access to
 * @param[in] access                Type of access
 * @param[out] job                   Indicates job number
 * @return LSM_ERR_OK on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmAccessGrant( lsmConnectPtr conn, lsmInitiatorPtr initiator,
                        lsmVolumePtr volume, lsmAccessType access,
                        uint32_t *job);

/**
 * Removes a volume from being accessed by an initiator.
 * @param[in] conn          Valid connection
 * @param[in] initiator     Valid initiator
 * @param[in] volume        Valid volume
 * @return LSM_ERR_OK, LSM_ERR_NO_MAPPING else error reason.
 */
int lsmAccessRemove( lsmConnectPtr conn, lsmInitiatorPtr initiator,
                        lsmVolumePtr volume);

/**
 * Retrieves a list of access groups.
 * @param[in] conn              Valid connection @see lsmConnectUserPass
 * @param[out] groups           Array of access groups
 * @param[out] groupCount       Size of array
 * @return LSM_ERR_OK on success, else error reason.
 */
int lsmAccessGroupList( lsmConnectPtr conn, lsmAccessGroupPtr *groups[],
                        uint32_t *groupCount);

/**
 * Creates a new access group.
 * @param[in] conn              Valid connection @see lsmConnectUserPass
 * @param[in] name              Name of new access group
 * @return LSM_ERR_OK on success, else error reason.
 */
int lsmAccessGroupCreate( lsmConnectPtr conn, char *name);

/**
 * Deletes an access group.
 * @param[in] conn                  Valid connection @see lsmConnectUserPass
 * @param[in] group                 Group to delete
 * @return LSM_ERR_OK on success, else error reason.
 */
int lsmAccessGroupDel( lsmConnectPtr conn, lsmAccessGroupPtr group);

/**
 * Adds an initiator to the access group
 * @param[in] conn                  Valid connection @see lsmConnectUserPass
 * @param[in] group                 Group to modify
 * @param[in] initiator             Initiator to add to group
 * @param[in] access                Desired access to storage
 * @return LSM_ERR_OK on success, else error reason.
 */
int lsmAccessGroupAddInitiator( lsmConnectPtr conn, lsmAccessGroupPtr group,
                                lsmInitiatorPtr initiator, lsmAccessType access);

/**
 * Removes an initiator from an access group.
 * @param[in] conn                  Valid connection @see lsmConnectUserPass
 * @param[in] group                 Group to modify
 * @param[in] initiator             Initiator to delete from group
 * @return[in] LSM_ERR_OK on success, else error reason.
 */
int lsmAccessGroupDelInitiator( lsmConnectPtr conn, lsmAccessGroupPtr group,
                                lsmInitiatorPtr initiator);

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_H */

