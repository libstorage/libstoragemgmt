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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef LSM_FS_H
#define LSM_FS_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees a File system record
 * @param fs    File system to free.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_fs_record_free(lsm_fs *fs);

/**
 * Copies a file system record.
 * @param source        File system record to copy.
 * @return Pointer to copy of file system record
 */
lsm_fs LSM_DLL_EXPORT *lsm_fs_record_copy(lsm_fs *source);

/**
 * Frees an array of file system records
 * @param fs        Array of file system record pointers
 * @param size      Number in array to free
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_fs_record_array_free(lsm_fs * fs[], uint32_t size);

/**
 * Returns the id of the file system.
 * @param fs        File system record pointer
 * @return Pointer to file system id
 */
const char LSM_DLL_EXPORT *lsm_fs_id_get(lsm_fs *fs);

/**
 * Returns the name associated with the file system.
 * @param fs        File system record pointer
 * @return Pointer to file system name
 */
const char LSM_DLL_EXPORT *lsm_fs_name_get(lsm_fs *fs);

/**
 * Returns the file system system id.
 * @param fs    File system record pointer
 * @return Pointer to the system id.
 */
const char LSM_DLL_EXPORT *lsm_fs_system_id_get(lsm_fs *fs);

/**
 * Returns the pool id associated with the file system
 * @param fs    File system record pointer
 * @return Pointer to pool id
 */
const char LSM_DLL_EXPORT *lsm_fs_pool_id_get(lsm_fs *fs);

/**
 * Returns total space of file system.
 * @param fs        File system record pointer
 * @return Total size of file system in bytes
 */
uint64_t LSM_DLL_EXPORT lsm_fs_total_space_get(lsm_fs *fs);

/**
 * Returns the space available on the file system
 * @param fs        File system record pointer
 * @return Total number of bytes that are free.
 */
uint64_t LSM_DLL_EXPORT lsm_fs_free_space_get(lsm_fs *fs);

#ifdef  __cplusplus
}
#endif

#endif