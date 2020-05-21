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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_system_record_copy - Duplicates a system record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_system record.
 *
 * @s:
 *      Pointer of lsm_system to duplicate.
 *
 * Return:
 *      Pointer of lsm_system. NULL on memory allocation failure. Should be
 *      freed by lsm_system_record_free().
 */
lsm_system LSM_DLL_EXPORT *lsm_system_record_copy(lsm_system *s);

/**
 * lsm_system_record_free - Free the lsm_system memory.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for a lsm_system.
 * @s:
 *      Record to release
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_system pointer.
 *
 */
int LSM_DLL_EXPORT lsm_system_record_free(lsm_system *s);

/**
 * lsm_system_record_array_free - Free the memory of lsm_system array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for an array of lsm_system.
 *
 * @s:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_system pointer.
 *
 */
int LSM_DLL_EXPORT lsm_system_record_array_free(lsm_system *s[], uint32_t size);

/**
 * lsm_system_id_get - Retrieves the ID of the system.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the Id for the system.
 *      Note: Address returned is valid until lsm_system gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @s:
 *      System to retrieve id for.
 *
 * Return:
 *      string. NULL if argument 's' is NULL or not a valid lsm_system pointer.
 */
const char LSM_DLL_EXPORT *lsm_system_id_get(lsm_system *s);

/**
 * lsm_system_name_get - Retrieves the name for the system.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the name for the system.
 *      Note: Address returned is valid until lsm_system gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @s:
 *      System to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 's' is NULL or not a valid lsm_system pointer.
 */
const char LSM_DLL_EXPORT *lsm_system_name_get(lsm_system *s);

/**
 * lsm_system_read_cache_pct_get - Retrieves read cache percentage of the
 * system.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves read cache percentage of the specified system.
 *
 * Capability:
 *      LSM_CAP_SYS_READ_CACHE_PCT_GET
 *
 * @s:
 *      System to retrieve read cache percentage for.
 *
 * Return:
 *      int. Possible values are:
 *          * >=0 and <= 100
 *              Success.
 *          * LSM_SYSTEM_READ_CACHE_PCT_NO_SUPPORT
 *              No Support.
 *          * LSM_SYSTEM_READ_CACHE_PCT_UNKNOWN
 *              System pointer is NULL or bug.
 */
int LSM_DLL_EXPORT lsm_system_read_cache_pct_get(lsm_system *s);

/**
 * lsm_system_status_get - Retrieves status of the system.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves status of the specified system which is a bit sensitive field.
 *      Possible values are:
 *          * LSM_SYSTEM_STATUS_UNKNOWN:
 *              Unknown.
 *          * LSM_SYSTEM_STATUS_OK:
 *              Every is OK.
 *          * LSM_SYSTEM_STATUS_ERROR:
 *              An error has occurred causing the system to stop. Example:
 *               * A whole disk enclosure down.
 *               * All controllers down.
 *               * Internal hardware(like, memory) down and no redundant part.
 *
 * @s:
 *      System to retrieve read cache percentage for.
 *
 * Return:
 *      uint32_t. Returns UINT32_MAX if argument 's' is NULL or not a valid
 *      lsm_system pointer.
 */
uint32_t LSM_DLL_EXPORT lsm_system_status_get(lsm_system *s);

/**
 * lsm_system_fw_version_get - Retrieves firmware version of system.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves firmware version of the specified system. Please do not free
 *      returned string pointer, resources will get freed when
 *      lsm_system_record_free() or lsm_system_record_array_free() is called.
 *
 * Capability:
 *      LSM_CAP_SYS_FW_VERSION_GET
 *
 * @s:
 *      System to retrieve firmware version for.
 *
 * Return:
 *      string. NULL if argument 's' is NULL or not a valid lsm_system pointer.
 */
LSM_DLL_EXPORT const char *lsm_system_fw_version_get(lsm_system *s);

/**
 * lsm_system_mode_get - Retrieves system mode.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves system mode, currently only supports retrieving hardware RAID
 *      cards system mode.
 *
 * Capability:
 *      LSM_CAP_SYS_MODE_GET
 *
 * @s:
 *      System to retrieve system mode for.
 *
 * Return:
 *      lsm_system_mode_type. Possible values are:
 *          * LSM_SYSTEM_MODE_UNKNOWN
 *              The value when invalid argument or bug.
 *          * LSM_SYSTEM_MODE_NO_SUPPORT
 *              The value when requested method is not supported.
 *          * LSM_SYSTEM_MODE_HARDWARE_RAID
 *              The storage system is a hardware RAID card(like HP SmartArray
 *              and LSI MegaRAID) and could expose the logical volume(aka,
 *              RAIDed virtual disk) to OS while hardware RAID card is handling
 *              the RAID algorithm. In this mode, storage system cannot expose
 *              physical disk directly to OS.
 *          * LSM_SYSTEM_MODE_HBA
 *              The physical disks can be exposed to OS directly without any
 *              configurations. SCSI enclosure service might be exposed to OS
 *              also.
 */
LSM_DLL_EXPORT lsm_system_mode_type lsm_system_mode_get(lsm_system *s);

#ifdef __cplusplus
}
#endif
#endif
