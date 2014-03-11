/*
 * Copyright (C) 2014 Red Hat, Inc.
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
 * Author: tasleson
 *
 */

#ifndef LIBSTORAGEMGMT_DISK_H
#define	LIBSTORAGEMGMT_DISK_H

#include "libstoragemgmt_common.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Free the memory for a disk record
 * @param d     Disk memory to free
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsmDiskRecordFree(lsmDisk *d);

/**
 * Copy a disk record
 * @param d     Disk record to copy
 * @return Copy of disk record
 */
lsmDisk LSM_DLL_EXPORT *lsmDiskRecordCopy(lsmDisk *d);

/**
 * Free an array of disk records
 * @param disk      Array of disk records
 * @param size      Size of disk array
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsmDiskRecordArrayFree( lsmDisk *disk[], uint32_t size);

/**
 * Returns the disk id
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return String id
 */
const char LSM_DLL_EXPORT *lsmDiskIdGet(lsmDisk *d);

/**
 * Returns the disk name
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Disk name
 */
const char LSM_DLL_EXPORT *lsmDiskNameGet(lsmDisk *d);

/**
 * Returns the disk type (enumeration)
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Disk type
 */
lsmDiskType LSM_DLL_EXPORT lsmDiskTypeGet(lsmDisk *d);

/**
 * Returns number of blocks for disk
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Number of logical blocks
 */
uint64_t LSM_DLL_EXPORT lsmDiskNumberOfBlocksGet(lsmDisk *d);

/**
 * Returns the block size
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Block size in bytes
 */
uint64_t LSM_DLL_EXPORT lsmDiskBlockSizeGet(lsmDisk *d);

/**
 * Returns the disk status
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Status of the disk
 */
uint64_t LSM_DLL_EXPORT lsmDiskStatusGet(lsmDisk *d);

/**
 * Returns the system id
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Which system the disk belongs too.
 */
const char LSM_DLL_EXPORT *lsmDiskSystemIdGet(lsmDisk *d);

/**
 * Retrieve optional data for this disk
 * @param d     Valid lsmDisk pointer
 * @return NULL if no optional data exists.
 */
lsmOptionalData LSM_DLL_EXPORT *lsmDiskOptionalDataGet(lsmDisk *d);


#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_DISK_H */
