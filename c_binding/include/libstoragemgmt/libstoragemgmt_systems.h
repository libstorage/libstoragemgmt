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
#ifndef LIBSTORAGEMGMT_SYSTEMS_H
#define LIBSTORAGEMGMT_SYSTEMS_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Duplicated a system record.
 * NOTE: Make sure to free resources with a call to lsm_system_record_free
 * @param s     Record to duplicate
 * @return NULL on memory allocation failure, else duplicated record.
 */
lsm_system LSM_DLL_EXPORT *lsm_system_record_copy(lsm_system *s);


/**
 * Frees the resources for a lsm_system
 * @param s Record to release
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_system_record_free(lsm_system *s);

/**
 * Frees the resources for an array for lsm_system
 * @param s     Array to release memory for
 * @param size  Number of elements.
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_system_record_array_free(lsm_system *s[], uint32_t size);

/**
 * Retrieve the Id for the system.
 * Note: Address returned is valid until lsm_system gets freed, copy return
 * value if you need longer scope.  Do not free returned string.
 * @param s System to retrieve id for.
 * @return NULL on error, else value.
 */
const char LSM_DLL_EXPORT *lsm_system_id_get(lsm_system *s);

/**
 * Retrieve the Id for the system.
 * Note: Address returned is valid until lsm_system gets freed, copy return
 * value if you need longer scope.  Do not free returned string.
 * @param s System to retrieve id for.
 * @return NULL on error, else value.
 */
const char LSM_DLL_EXPORT *lsm_system_name_get(lsm_system *s);

/**
 * New in version 1.3. Retrieves read cache percentage of the specified system.
 * @param       s   System to retrieve read cache percentage for.
 * @return Read cache percentage.
 * @retval >= 0 and <= 100 Success
 * @retval LSM_SYSTEM_READ_CACHE_PCT_NO_SUPPORT No support.
 * @retval LSM_SYSTEM_READ_CACHE_PCT_UNKNOWN system pointer is NULL or bug.
 */
int LSM_DLL_EXPORT lsm_system_read_cache_pct_get(lsm_system *s);

/**
 * Retrieve the status for the system.
 * @param s     System to retrieve status for
 * @return System status which is a bit sensitive field, returns UINT32_MAX on
 * bad system pointer.
 */
uint32_t LSM_DLL_EXPORT lsm_system_status_get(lsm_system *s);

/**
 * New in version 1.3.
 * Retrieves firmware version of the specified system.
 * Please do not free returned string pointer, resources will get freed when
 * lsm_system_record_free() or lsm_system_record_array_free() is called.
 * @param       s       System to retrieve firmware version for.
 * @return System firmware version. NULL if bad system pointer or no support or
 * bug.
 */
LSM_DLL_EXPORT const char *lsm_system_fw_version_get(lsm_system *s);

/**
 * New in version 1.3.
 * Retrieves system mode, currently only supports retrieving hardware RAID cards
 * system mode.
 * @param   s       System to retrieve firmware version for.
 * @return Hardware RAID card system mode.
 * @retval LSM_SYSTEM_MODE_UNKNOWN
 *          The value when invalid argument or bug.
 * @retval LSM_SYSTEM_MODE_NO_SUPPORT
 *          The value when requested method is not supported.
 * @retval LSM_SYSTEM_MODE_HARDWARE_RAID
 *          The storage system is a hardware RAID card(like HP SmartArray and
 *          LSI MegaRAID) and could expose the logical volume(aka, RAIDed
 *          virtual disk) to OS while hardware RAID card is handling the RAID
 *          algorithm. In this mode, storage system cannot expose physical disk
 *          directly to OS.
 * @retval LSM_SYSTEM_MODE_HBA
 *          The physical disks can be exposed to OS directly without any
 *          configurations. SCSI enclosure service might be exposed to OS also.
 */
LSM_DLL_EXPORT lsm_system_mode_type lsm_system_mode_get(lsm_system *s);

#ifdef  __cplusplus
}
#endif
#endif
