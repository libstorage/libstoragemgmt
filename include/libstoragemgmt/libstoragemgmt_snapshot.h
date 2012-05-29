/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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

#ifndef LSM_SNAPSHOT_H
#define LSM_SNAPSHOT_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees a snapshot record.
 * @param ss    Snapshot record
 */
void LSM_DLL_EXPORT lsmSsRecordFree(lsmSsPtr ss);

/**
 * Copies a snapshot record.
 * @param source        Source to copy
 * @return Copy of source record snapshot
 */
lsmSsPtr LSM_DLL_EXPORT lsmSsRecordCopy(lsmSsPtr source);

/**
 * Frees an array of snapshot record.
 * @param ss        An array of snapshot record pointers.
 * @param size      Number of snapshot records.
 */
void LSM_DLL_EXPORT lsmSsRecordFreeArray(lsmSsPtr ss[], uint32_t size);

/**
 * Returns the snapshot id.
 * @param ss        The snapshot record
 * @return Pointer to id.
 */
const char LSM_DLL_EXPORT *lsmSsIdGet(lsmSsPtr ss);

/**
 * Returns the name.
 * @param ss        The snapshot record
 * @return The Name
 */
const char LSM_DLL_EXPORT *lsmSsNameGet(lsmSsPtr ss);

/**
 * Returns the timestamp
 * @param ss    The snapshot record.
 * @return The timestamp the snapshot was taken
 */
uint64_t LSM_DLL_EXPORT lsmSsTimeStampGet(lsmSsPtr ss);

#ifdef  __cplusplus
}
#endif

#endif