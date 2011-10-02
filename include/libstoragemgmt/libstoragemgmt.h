/*
 * Copyright (C) 2011 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File:   libstoragemgmt.h
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMT_H
#define	LIBSTORAGEMGMT_H

#include <stdint.h>
#include "libstoragemgmt_types.h"
#include "libstoragemgmt_error.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Get a connection to a storage provider.
 * @param[in] uri       Uniform Resource Identifier (see URI documentation)
 * @param[in] password  Password for the storage array (optional, can be NULL)
 * @param[out] conn     The connection to use for all the other library calls
 * @param[in] timeout   Time-out in seconds
 * @param[out] e        Error data if connection failed.
 * @return 0 on success, else error code @see lsmErrorNumber
 */
int lsmConnectPassword(char* uri, char *password,
                        lsmConnectPtr *conn, uint32_t timeout, lsmErrorPtr *e);
/**
 * Closes a connection to a storage provider.
 * @param[in] conn          Valid connection to close
 * @return 0 on success, else error code @see lsmErrorNumber
 */
int lsmConnectClose(lsmConnectPtr conn);

/**
 * Sets the time-out for this connection.
 * @param[in] conn          Valid connection @see lsmConnectUserPass
 * @param[in] timeout       Time-out (in ms)
 * @return 0 on success, else -1 on error
 */
int lsmConnectSetTimeout(lsmConnectPtr conn, uint32_t timeout);

/**
 * Gets the time-out for this connection.
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[out]  timeout         Time-out (in ms)
 * @return 0 on success, else -1 on error
 */
int lsmConnectGetTimeout(lsmConnectPtr conn, uint32_t *timeout);

/**
 * Storage system query functions
 */

/**
 * Query the capabilities of the storage array.
 * @param[in]   conn    Valid connection @see lsmConnectUserPass
 * @param[out]  cap     The storage array capabilities
 * @return 0 on success else -1
 */
int lsmCapabilities(lsmConnectPtr conn, lsmStorageCapabilitiesPtr *cap);

/**
 * Query the list of storage pools on the array.
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[out]  poolArray       Array of storage pools
 * @param[out]  count           Number of storage pools
 * @return 0 on success else -1
 */
int lsmPoolList(lsmConnectPtr conn, lsmPoolPtr *poolArray[],
                        uint32_t *count);

/**
 * Query the list of initiators known to the array
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[out] initiators       Array of initiators
 * @param[out] count            Number of initiators
 * @return  0 on success else -1
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
 * @return 0 on success else -1
 */
int lsmVolumeList(lsmConnectPtr conn, lsmVolumePtr *volumes[], uint32_t *count);

/**
 * Creates a new volume.
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[in]   pool            Valid pool @see lsmPool_t
 * @param[in]   volumeName      Human recognizable name (not all arrays support)
 * @param[in]   size            Size of new volume in bytes
 * @param[in]   provisioning    Type of volume provisioning to use
 * @param[out]  newVolume       Valid volume @see lsmVolume_t
 * @param[out]  job             if >=0 indicates job number
 * @return 0 on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmVolumeCreate(lsmConnectPtr conn, lsmPoolPtr pool, char *volumeName,
                        uint64_t size, lsmProvisionType provisioning,
                        lsmVolumePtr *newVolume, int32_t *job);

/**
 * Resize an existing volume.
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[in]   volume          volume to resize
 * @param[in]   newSize         New size of volume
 * @param[out]  job             If >=0 indicates job number
 * @return 0 on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmVolumeResize(lsmConnectPtr conn, lsmVolumePtr *volume,
                        uint64_t newSize, uint32_t *job);

/**
 * Replicates a volume
 * @param[in] conn              Valid connection @see lsmConnectUserPass
 * @param[in] pool              Valid pool
 * @param[in] repType           Type of replication lsmReplicationType
 * @param[in] volumeSrc         Which volume to replicate
 * @param[in] name              Human recognizable name (not all arrays support)
 * @param[out] newReplicant     New replicated volume lsmVolume_t
 * @param[out] job              if >=0 indicates job number
 * @return 0 on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmVolumeReplicate(lsmConnectPtr conn, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        char *name, lsmVolumePtr **newReplicant, int32_t *job);

/**
 * Deletes a logical unit and data is lost!
 * @param[in]   conn            Valid connection @see lsmConnectUserPass
 * @param[in]   volume          Volume that is to be deleted.
 * @return 0 on success, LSM_JOB_STARTED if async. , else error code
 */
int lsmVolumeDelete(lsmConnectPtr conn, lsmVolumePtr volume);

/**
 * Query the status of a volume
 * @param[in] conn              Valid connection @see lsmConnectUserPass
 * @param[in] volume            Storage volume to get status for
 * @param[out] status           Status of the volume
 * @return 0 on success, else error code
 */
int lsmVolumeStatus(lsmConnectPtr conn, lsmVolumePtr volume,
                        lsmVolumeStatusType *status);

/**
 * Set a Volume to online
 * @param conn                  Valid connection @see lsmConnectUserPass
 * @param volume                Volume that is to be placed online
 * @return 0 on success, else error code
 */
int lsmVolumeOnline(lsmConnectPtr conn, lsmVolumePtr volume);

/**
 * Set a Volume to offline
 * @param conn                  Valid connection @see lsmConnectUserPass
 * @param volume                Volume that is to be placed online
 * @return 0 on success, else error code
 */
int lsmVolumeOffline(lsmConnectPtr conn, lsmVolumePtr volume);

/**
 * Retrieves a list of access groups.
 * @param [in]conn              Valid connection @see lsmConnectUserPass
 * @param [out]groups           Array of access groups
 * @param [out]groupCount       Size of array
 * @return 0 on success, else error reason.
 */
int lsmAccessGroupList( lsmConnectPtr conn, lsmAccessGroupPtr *groups[],
                        uint32_t *groupCount);

/**
 * Creates a new access group.
 * @param[in] conn              Valid connection @see lsmConnectUserPass
 * @param[in] name              Name of new access group
 * @return 0 on success, else error reason.
 */
int lsmAccessGroupCreate( lsmConnectPtr conn, char *name);

/**
 * Deletes an access group.
 * @param conn                  Valid connection @see lsmConnectUserPass
 * @param group                 Group to delete
 * @return 0 on success, else error reason.
 */
int lsmAccessGroupDel( lsmConnectPtr conn, lsmAccessGroupPtr group);

/**
 * Adds an initiator to the access group
 * @param conn                  Valid connection @see lsmConnectUserPass
 * @param group                 Group to modify
 * @param initiator             Initiator to add to group
 * @param access                Desired access to storage
 * @return 0 on success, else error reason.
 */
int lsmAccessGroupAddInitiator( lsmConnectPtr conn, lsmAccessGroupPtr group,
                                lsmInitiatorPtr initiator, lsmAccessType access);

/**
 * Removes an initiator from an access group.
 * @param conn                  Valid connection @see lsmConnectUserPass
 * @param group                 Group to modify
 * @param initiator             Initiator to delete from group
 * @return 0 on success, else error reason.
 */
int lsmAccessGroupDelInitiator( lsmConnectPtr conn, lsmAccessGroupPtr group,
                                lsmInitiatorPtr initiator);

#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_H */

