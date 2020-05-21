/*
 * Copyright (C) 2011-2017 Red Hat, Inc.
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

#ifndef LIBSTORAGEMGMT_VOLUMES_H
#define LIBSTORAGEMGMT_VOLUMES_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_volume_record_free - Frees the memory for an individual volume
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for an individual lsm_volume
 *
 * @v:
 *      lsm_volume to release memory for.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_volume pointer.
 */
int LSM_DLL_EXPORT lsm_volume_record_free(lsm_volume *v);

/**
 * lsm_volume_record_copy - Duplicates a volume record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_volume record.
 *
 * @vol:
 *      Pointer of lsm_volume to duplicate.
 *
 * Return:
 *      Pointer of lsm_volume. NULL on memory allocation failure or invalid
 *      lsm_volume pointer. Should be freed by lsm_volume_record_free().
 */
lsm_volume LSM_DLL_EXPORT *lsm_volume_record_copy(lsm_volume *vol);

/**
 * lsm_volume_record_array_free - Frees the memory of volume array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for each of the volumes and then the volume array
 *      itself.
 *
 * @init:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_volume pointer.
 */
int LSM_DLL_EXPORT lsm_volume_record_array_free(lsm_volume *init[],
                                                uint32_t size);

/**
 * lsm_volume_id_get - Retrieves the ID for the volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the ID for the volume.
 *      Note: Address returned is valid until lsm_volume gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @v:
 *      Volume to retrieve ID for.
 *
 * Return:
 *      string. NULL if argument 'v' is NULL or not a valid lsm_volume pointer.
 */
const char LSM_DLL_EXPORT *lsm_volume_id_get(lsm_volume *v);

/**
 * lsm_volume_name_get - Retrieves the name for the volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the name for the volume.
 *      Note: Address returned is valid until lsm_volume gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @v:
 *      Volume to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 'v' is NULL or not a valid lsm_volume pointer.
 */
const char LSM_DLL_EXPORT *lsm_volume_name_get(lsm_volume *v);

/**
 * lsm_volume_vpd83_get - Retrieves the SCSI VPD 0x83 unique ID for the volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the SCSI VPD 0x83 NAA type unique ID for the volume.
 *      Note: Address returned is valid until lsm_volume gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @v:
 *      Volume to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 'v' is NULL or not a valid lsm_volume pointer.
 */
const char LSM_DLL_EXPORT *lsm_volume_vpd83_get(lsm_volume *v);

/**
 * lsm_volume_block_size_get -  Retrieves the block size for the volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the block size in bytes for the volume.
 *
 * @v:
 *      Volume to retrieve block size for.
 *
 * Return:
 *      uint64_t. 0 if argument 'v' is NULL or not a valid lsm_volume pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_volume_block_size_get(lsm_volume *v);

/**
 * lsm_volume_number_of_blocks_get -  Retrieves the block count for the volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the block count for the volume.
 *
 * @v:
 *      Volume to retrieve block count for.
 *
 * Return:
 *      uint64_t. 0 if argument 'v' is NULL or not a valid lsm_volume pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_volume_number_of_blocks_get(lsm_volume *v);

/**
 * lsm_volume_admin_state_get - Retrieves administrative status of the volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves administrative status of the volume.
 *
 * @v:
 *      Volume to retrieve administrative status for.
 *
 * Return:
 *      uint64_t. Status of the specified volume which is a bit sensitive field.
 *      Possible values are:
 *          * LSM_VOLUME_ADMIN_STATE_ENABLED
 *              The volume is not explicitly disabled by administrator. This is
 *              the normal state of a volume.
 *          * LSM_VOLUME_ADMIN_STATE_DISABLED
 *              The volume is explicitly disabled by administrator or via method
 *              lsm_volume_disable(). All block access will be rejected.
 *
 */
uint32_t LSM_DLL_EXPORT lsm_volume_admin_state_get(lsm_volume *v);

/**
 * lsm_volume_system_id_get - Retrieves the system ID for the volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the system id for the specified volume.
 *      Note: Address returned is valid until lsm_volume gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @v:
 *      Volume to retrieve system ID for.
 *
 * Return:
 *      string. NULL if argument 'v' is NULL or not a valid lsm_volume pointer.
 *
 */
char LSM_DLL_EXPORT *lsm_volume_system_id_get(lsm_volume *v);

/**
 * lsm_volume_pool_id_get - Retrieves the pool ID for the volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the pool id for the specified volume.
 *      Note: Address returned is valid until lsm_volume gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @v:
 *      Volume to retrieve pool ID for.
 *
 * Return:
 *      string. NULL if argument 'v' is NULL or not a valid lsm_volume pointer.
 *
 */
char LSM_DLL_EXPORT *lsm_volume_pool_id_get(lsm_volume *v);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMT_VOLUMES_H */
