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

#ifndef LIBSTORAGEMGMT_VOLUMES_H
#define LIBSTORAGEMGMT_VOLUMES_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees the memory fro an individual volume
 * @param v     Volume pointer to free.
 */
void LSM_DLL_EXPORT lsmVolumeRecordFree(lsmVolume *v);

/**
 * Copies a volume record structure.
 * @param vol   Volume record to be copied.
 * @return NULL on error, else record copy.
 */
lsmVolume LSM_DLL_EXPORT *lsmVolumeRecordCopy(lsmVolume *vol);

/**
 * Frees the memory for each of the volume records and then the array itself.
 * @param init  Array to free.
 * @param size  Size of array.
 */
void LSM_DLL_EXPORT lsmVolumeRecordFreeArray( lsmVolume *init[], uint32_t size);

/**
 * Retrieves the volume id.
 * Note: returned value only valid when v is valid!
 * @param v     Volume ptr.
 * @return Volume id.
 */
const char LSM_DLL_EXPORT *lsmVolumeIdGet(lsmVolume *v);

/**
 * Retrieves the volume name (human recognizable
 * Note: returned value only valid when v is valid!
 * @param v     Volume ptr.
 * @return Volume name
 */
const char LSM_DLL_EXPORT *lsmVolumeNameGet(lsmVolume *v);

/**
 * Retrieves the SCSI page 83 unique ID.
 * Note: returned value only valid when v is valid!
 * @param v     Volume ptr.
 * @return SCSI page 83 unique ID.
 */
const char LSM_DLL_EXPORT *lsmVolumeVpd83Get(lsmVolume *v);

/**
 * Retrieves the volume block size.
 * @param v     Volume ptr.
 * @return Volume block size.
 */
uint64_t LSM_DLL_EXPORT lsmVolumeBlockSizeGet(lsmVolume *v);

/**
 * Retrieves the number of blocks.
 * @param v     Volume ptr.
 * @return      Number of blocks.
 */
uint64_t LSM_DLL_EXPORT lsmVolumeNumberOfBlocks(lsmVolume *v);

/**
 * Retrieves the operational status of the volume.
 * @param v     Volume ptr.
 * @return Operational status of the volume, @see lsmVolumeOpStatus
 */
uint32_t LSM_DLL_EXPORT lsmVolumeOpStatusGet(lsmVolume *v);

/**
 * Retrieves the system id of the volume.
 * @param v     Volume ptr.
 * @return System id.
 */
char LSM_DLL_EXPORT *lsmVolumeSystemIdGet( lsmVolume *v);

/**
 * Retrieves the pool id that the volume is derived from.
 * @param v     Volume ptr.
 * @return Pool id.
 */
char LSM_DLL_EXPORT *lsmVolumePoolIdGet( lsmVolume *v);

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_VOLUMES_H */

