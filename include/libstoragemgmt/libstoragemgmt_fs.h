/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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
 */
void LSM_DLL_EXPORT lsmFsRecordFree(lsmFs *fs);

/**
 * Copies a file system record.
 * @param source        File system record to copy.
 * @return Pointer to copy of file system record
 */
lsmFs LSM_DLL_EXPORT *lsmFsRecordCopy(lsmFs *source);

/**
 * Frees an array of file system records
 * @param fs        Array of file system record pointers
 * @param size      Number in array to free
 */
void LSM_DLL_EXPORT lsmFsRecordFreeArray(lsmFs * fs[], uint32_t size);

/**
 * Returns the id of the file system.
 * @param fs        File system record pointer
 * @return Pointer to file system id
 */
const char LSM_DLL_EXPORT *lsmFsIdGet(lsmFs *fs);

/**
 * Returns the name associated with the file system.
 * @param fs        File system record pointer
 * @return Pointer to file system name
 */
const char LSM_DLL_EXPORT *lsmFsNameGet(lsmFs *fs);

/**
 * Returns the file system system id.
 * @param fs    File system record pointer
 * @return Pointer to the system id.
 */
const char LSM_DLL_EXPORT *lsmFsSystemIdGet(lsmFs *fs);

/**
 * Returns the pool id associated with the file system
 * @param fs    File system record pointer
 * @return Pointer to pool id
 */
const char LSM_DLL_EXPORT *lsmFsPoolIdGet(lsmFs *fs);

/**
 * Returns total space of file system.
 * @param fs        File system record pointer
 * @return Total size of file system in bytes
 */
uint64_t LSM_DLL_EXPORT lsmFsTotalSpaceGet(lsmFs *fs);

/**
 * Returns the space available on the file system
 * @param fs        File system record pointer
 * @return Total number of bytes that are free.
 */
uint64_t LSM_DLL_EXPORT lsmFsFreeSpaceGet(lsmFs *fs);


#ifdef  __cplusplus
}
#endif

#endif