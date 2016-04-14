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

#ifndef LIBSTORAGEMGMT_VOLUMES_H
#define LIBSTORAGEMGMT_VOLUMES_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees the memory fro an individual volume
 * @param v     Volume pointer to free.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_volume_record_free(lsm_volume *v);

/**
 * Copies a volume record structure.
 * @param vol   Volume record to be copied.
 * @return NULL on error, else record copy.
 */
lsm_volume LSM_DLL_EXPORT *lsm_volume_record_copy(lsm_volume *vol);

/**
 * Frees the memory for each of the volume records and then the array itself.
 * @param init  Array to free.
 * @param size  Size of array.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_volume_record_array_free(lsm_volume *init[],
                                                uint32_t size);

/**
 * Retrieves the volume id.
 * Note: returned value only valid when v is valid!
 * @param v     Volume ptr.
 * @return Volume id.
 */
const char LSM_DLL_EXPORT *lsm_volume_id_get(lsm_volume *v);

/**
 * Retrieves the volume name (human recognizable)
 * Note: returned value only valid when v is valid!
 * @param v     Volume ptr.
 * @return Volume name
 */
const char LSM_DLL_EXPORT *lsm_volume_name_get(lsm_volume *v);

/**
 * Retrieves the SCSI page 83 unique ID.
 * Note: returned value only valid when v is valid!
 * @param v     Volume ptr.
 * @return SCSI page 83 unique ID.
 */
const char LSM_DLL_EXPORT *lsm_volume_vpd83_get(lsm_volume *v);

/**
 * Retrieves the volume block size.
 * @param v     Volume ptr.
 * @return Volume block size.
 */
uint64_t LSM_DLL_EXPORT lsm_volume_block_size_get(lsm_volume *v);

/**
 * Retrieves the number of blocks.
 * @param v     Volume ptr.
 * @return      Number of blocks.
 */
uint64_t LSM_DLL_EXPORT lsm_volume_number_of_blocks_get(lsm_volume *v);

/**
 * Retrieves the admin state of the volume.
 * @param v     Volume ptr.
 * @return Admin state of volume, see LSM_VOLUME_ADMIN_STATE_ENABLED and
 *         LSM_VOLUME_ADMIN_STATE_DISABLED
 *
 */
uint32_t LSM_DLL_EXPORT lsm_volume_admin_state_get(lsm_volume *v);

/**
 * Retrieves the system id of the volume.
 * @param v     Volume ptr.
 * @return System id.
 */
char LSM_DLL_EXPORT *lsm_volume_system_id_get(lsm_volume *v);

/**
 * Retrieves the pool id that the volume is derived from.
 * @param v     Volume ptr.
 * @return Pool id.
 */
char LSM_DLL_EXPORT *lsm_volume_pool_id_get(lsm_volume *v);

/**
 * New in version 1.3. Retrieves volume health status:
 *      LSM_VOLUME_STATUS_NO_SUPPORT
 *          The value when the requested method is not supported.
 *      LSM_VOLUME_STATUS_OTHER
 *          The volume's status does not map to any statuses known to
 *          libstoragemgmt.
 *      LSM_VOLUME_STATUS_OK
 *          The volume has no known health problems.
 *      LSM_VOLUME_STATUS_DEGRADED
 *          One or more of the volume's drives has failed,
 *          but the volume can be recovered to full health and
 *          can still execute IO in this state.
 *      LSM_VOLUME_STATUS_RECONSTRUCTING
 *          The volume is rebuilding to a fully healthy
 *          state (a failed device has recovered or has been
 *          replaced). The volume can still execute IO in this
 *          state.
 *      LSM_VOLUME_STATUS_ERROR
 *          Enough drives have failed behind the volume to
 *          prevent the volume from executing IO. The volume
 *          may not be recoverable.
 * @param       v       Volume to retrieve the status for.
 * @param[out]  status  Volume status 'lsm_volume_status_type' pointer.
 * @return LSM_ERR_OK on success, or LSM_ERR_NO_SUPPORT, or
 *         LSM_ERR_INVALID_ARGUMENT.
 */
int LSM_DLL_EXPORT lsm_volume_status_get(lsm_volume *v,
                                         lsm_volume_status_type *status);

#ifdef  __cplusplus
}
#endif
#endif                          /* LIBSTORAGEMGMT_VOLUMES_H */
