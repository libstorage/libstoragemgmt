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

#ifndef LSM_FS_H
#define LSM_FS_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_fs_record_free - Free the lsm_fs memory.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for the specified lsm_fs.
 *
 * @fs:
 *      Record to release
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_fs pointer.
 *
 */
int LSM_DLL_EXPORT lsm_fs_record_free(lsm_fs *fs);

/**
 * lsm_fs_record_copy - Duplicates a lsm_fs record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_fs record.
 *
 * @source:
 *      Pointer of lsm_fs to duplicate.
 *
 * Return:
 *      Pointer of lsm_fs. NULL on memory allocation failure or argument
 *      @source is NULL. Should be freed by lsm_fs_record_free().
 */
lsm_fs LSM_DLL_EXPORT *lsm_fs_record_copy(lsm_fs *source);

/**
 * lsm_fs_record_array_free - Free the memory of lsm_fs array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for an array of lsm_fs.
 *
 * @fs:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_fs pointer.
 *
 */
int LSM_DLL_EXPORT lsm_fs_record_array_free(lsm_fs *fs[], uint32_t size);

/**
 * lsm_fs_id_get - Retrieves the ID of the fs.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the ID for the fs.
 *      Note: Address returned is valid until lsm_fs gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @fs:
 *      File system to retrieve id for.
 *
 * Return:
 *      string. NULL if argument 'fs' is NULL or not a valid lsm_fs pointer.
 *
 */
const char LSM_DLL_EXPORT *lsm_fs_id_get(lsm_fs *fs);

/**
 * lsm_fs_name_get - Retrieves the name for the lsm_fs.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the name for the lsm_fs.
 *      Note: Address returned is valid until lsm_fs gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @fs:
 *      File system to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 'd' is NULL or not a valid lsm_fs pointer.
 *
 */
const char LSM_DLL_EXPORT *lsm_fs_name_get(lsm_fs *fs);

/**
 * lsm_fs_system_id_get - Retrieves the system ID for the lsm_fs.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the system id for the specified lsm_fs.
 *      Note: Address returned is valid until lsm_fs gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @fs:
 *      File system to retrieve system ID for.
 *
 * Return:
 *      string. NULL if argument 'fs' is NULL or not a valid lsm_fs pointer.
 */
const char LSM_DLL_EXPORT *lsm_fs_system_id_get(lsm_fs *fs);

/**
 * lsm_fs_pool_id_get - Retrieves the pool ID for the lsm_fs.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the pool id for the specified lsm_fs.
 *      Note: Address returned is valid until lsm_fs gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @fs:
 *      File system to retrieve pool ID for.
 *
 * Return:
 *      string. NULL if argument 'fs' is NULL or not a valid lsm_fs pointer.
 *
 */
const char LSM_DLL_EXPORT *lsm_fs_pool_id_get(lsm_fs *fs);

/**
 * lsm_fs_total_space_get -  Retrieves the total space for the lsm_fs.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the total space in bytes for the lsm_fs.
 *
 * @fs:
 *      File system to retrieve total space for.
 *
 * Return:
 *      uint64_t. 0 if argument 'fs' is NULL or not a valid lsm_fs pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_fs_total_space_get(lsm_fs *fs);

/**
 * lsm_fs_free_space_get -  Retrieves the free space for the fs.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the free space in bytes for the fs.
 *
 * @fs:
 *      File system to retrieve free space for.
 *
 * Return:
 *      uint64_t. 0 if argument 'fs' is NULL or not a valid lsm_fs pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_fs_free_space_get(lsm_fs *fs);

#ifdef __cplusplus
}
#endif
#endif
