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
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMT_POOL_H
#define LIBSTORAGEMGMT_POOL_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_pool_record_array_free - Frees the memory of pool array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for each of the pools and then the pool array itself.
 *
 * @pa:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_pool pointer.
 */
int LSM_DLL_EXPORT lsm_pool_record_array_free(lsm_pool *pa[], uint32_t size);

/**
 * lsm_pool_record_free - Frees the memory for an individual pool
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for an individual lsm_pool
 *
 * @p:
 *      lsm_pool to release memory for.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_pool pointer.
 */
int LSM_DLL_EXPORT lsm_pool_record_free(lsm_pool *p);

/**
 * lsm_pool_record_copy - Duplicates a lsm_pool record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_pool record.
 *
 * @to_be_copied:
 *      Pointer of lsm_pool to duplicate.
 *
 * Return:
 *      Pointer of lsm_pool. NULL on memory allocation failure or invalid
 *      lsm_pool pointer. Should be freed by lsm_pool_record_free().
 */
lsm_pool LSM_DLL_EXPORT *lsm_pool_record_copy(lsm_pool *to_be_copied);

/**
 * lsm_pool_name_get - Retrieve the name for the pool.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the name for the pool.
 *      Note: Address returned is valid until lsm_pool gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @p:
 *      Pool to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 'p' is NULL or not a valid lsm_pool pointer.
 */
char LSM_DLL_EXPORT *lsm_pool_name_get(lsm_pool *p);

/**
 * lsm_pool_id_get - Retrieve the ID for the pool.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the ID for the pool.
 *      Note: Address returned is valid until lsm_pool gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @p:
 *      Pool to retrieve ID for.
 *
 * Return:
 *      string. NULL if argument 'p' is NULL or not a valid lsm_pool pointer.
 */
char LSM_DLL_EXPORT *lsm_pool_id_get(lsm_pool *p);

/**
 * lsm_pool_total_space_get -  Retrieves the total space for the pool.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the total space in bytes for the pool.
 *
 * @p:
 *      Pool to retrieve total space for.
 *
 * Return:
 *      uint64_t. 0 if argument 'p' is NULL or not a valid lsm_pool pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_pool_total_space_get(lsm_pool *p);

/**
 * lsm_pool_free_space_get -  Retrieves the free space for the pool.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the free space in bytes for the pool.
 *
 * @p:
 *      Pool to retrieve free space for.
 *
 * Return:
 *      uint64_t. 0 if argument 'p' is NULL or not a valid lsm_pool pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_pool_free_space_get(lsm_pool *p);

/**
 * lsm_pool_status_get - Retrieves status of specified pool.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves status of the specified pool.
 *
 * @s:
 *      Pool to retrieve status for.
 *
 * Return:
 *      uint64_t. Status of the specified pool which is a bit sensitive field.
 *      Possible values are:
 *          * LSM_POOL_STATUS_UNKNOWN
 *              Plugin failed to query out the status of Pool.
 *          * LSM_POOL_STATUS_OK
 *              The data of this pool is accessible with not data lose. But it
 *              might along with LSM_POOL_STATUS_DEGRADED to indicate redundancy
 *              lose.
 *          * LSM_POOL_STATUS_OTHER
 *              Vendor specific status. The status_info property will explain
 *              the detail.
 *          * LSM_POOL_STATUS_DEGRADED
 *              Pool is lost data redundancy due to I/O error or offline of one
 *              or more RAID member. Often come with LSM_POOL_STATUS_OK to
 *              indicate data is still accessible with not data lose. Example:
 *                  * RAID 6 pool lost access to 1 disk or 2 disks.
 *                  * RAID 5 pool lost access to 1 disk.
 *          * LSM_POOL_STATUS_ERROR
 *              Pool data is not accessible due to some members offline.
 *              Example:
 *                  * RAID 5 pool lost access to 2 disks.
 *                  * RAID 0 pool lost access to 1 disks.
 *          * LSM_POOL_STATUS_STARTING
 *              Pool is reviving from STOPPED status. Pool data is not
 *              accessible yet.
 *          * LSM_POOL_STATUS_STOPPING
 *              Pool is stopping by administrator. Pool data is not accessible.
 *          * LSM_POOL_STATUS_STOPPED
 *              Pool is stopped by administrator. Pool data is not accessible.
 *          * LSM_POOL_STATUS_RECONSTRUCTING
 *              Pool is reconstructing the hash data or mirror data. Mostly
 *              happen when disk revive from offline or disk replaced.
 *              Pool.status_info may contain progress of this reconstruction
 *              job. Often come with LSM_POOL_STATUS_DEGRADED and
 *              LSM_POOL_STATUS_OK.
 *          * LSM_POOL_STATUS_VERIFYING
 *              Array is running integrity check on data of current pool. It
 *              might be started by administrator or array itself. The I/O
 *              performance will be impacted. Pool.status_info may contain
 *              progress of this verification job. Often come with
 *              LSM_POOL_STATUS_OK to indicate data is still accessible.
 *          * LSM_POOL_STATUS_GROWING
 *              Pool is growing its size and doing internal jobs.
 *              Pool.status_info can contain progress of this growing job. Often
 *              come with LSM_POOL_STATUS_OK to indicate data is still
 *              accessible.
 *          * LSM_POOL_STATUS_DELETING
 *              Array is deleting current pool.
 */
uint64_t LSM_DLL_EXPORT lsm_pool_status_get(lsm_pool *s);

/**
 * lsm_pool_status_info_get - Retrieve the status information for pool.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the status information of specified pool.
 *      Normally it explains the status value.
 *      Note: Address returned is valid until lsm_pool gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @s:
 *      Pool to retrieve status information for.
 *
 * Return:
 *      string. NULL if argument 's' is NULL or not a valid lsm_pool pointer.
 */
const char LSM_DLL_EXPORT *lsm_pool_status_info_get(lsm_pool *s);

/**
 * lsm_pool_system_id_get - Retrieve the system ID for the pool.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the system id for the specified pool.
 *      Note: Address returned is valid until lsm_pool gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @p:
 *      Pool to retrieve system ID for.
 *
 * Return:
 *      string. NULL if argument 'p' is NULL or not a valid lsm_pool pointer.
 */
char LSM_DLL_EXPORT *lsm_pool_system_id_get(lsm_pool *p);

/**
 * lsm_pool_element_type_get - Retrieve what the pool can be used to create
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve what type of element could the specified pool can be used to
 *      create.
 *
 * @p:
 *      Pool to retrieve element type for.
 *
 * Return:
 *      uint64_t. Element type which is a bit sensitive filed, possible values
 *      are:
 *          * 0
 *              If unknown or unsupported.
 *          * LSM_POOL_ELEMENT_TYPE_VOLUME
 *              Pool create volume.
 *          * LSM_POOL_ELEMENT_TYPE_FS
 *              Pool create file system.
 *          * LSM_POOL_ELEMENT_TYPE_POOL
 *              Pool create sub-pool.
 *          * LSM_POOL_ELEMENT_TYPE_DELTA
 *              Pool could hold delta data for snapshots.
 *          * LSM_POOL_ELEMENT_TYPE_VOLUME_FULL
 *              Pool could create fully allocated volume.
 *          * LSM_POOL_ELEMENT_TYPE_VOLUME_THIN
 *              Pool could create thin provisioned volume.
 *          * LSM_POOL_ELEMENT_TYPE_SYS_RESERVED
 *              Pool reserved for system internal use.
 *
 */
uint64_t LSM_DLL_EXPORT lsm_pool_element_type_get(lsm_pool *p);

/**
 * lsm_pool_unsupported_actions_get - Retrieve what the pool cannot be used for.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve what actions is not supported by specified pool.
 *
 * @p:
 *      Pool to retrieve unsupported actions for.
 *
 * Return:
 *      uint64_t. Unsupported actions which is a bit sensitive field.
 *      Possible values are:
 *          * 0
 *              If all actions are supported.
 *          * LSM_POOL_UNSUPPORTED_VOLUME_GROW
 *              Pool cannot grow volume on size.
 *          * LSM_POOL_UNSUPPORTED_VOLUME_SHRINK
 *              Pool cannot shrink volume on size.
 *
 */
uint64_t LSM_DLL_EXPORT lsm_pool_unsupported_actions_get(lsm_pool *p);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMT_POOL_H */
