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

#ifndef LSM_SNAPSHOT_H
#define LSM_SNAPSHOT_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees a snapshot record.
 * @param ss    Snapshot record
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_ss_record_free(lsm_ss *ss);

/**
 * Copies a snapshot record.
 * @param source        Source to copy
 * @return Copy of source record snapshot
 */
lsm_ss LSM_DLL_EXPORT *lsm_ss_record_copy(lsm_ss *source);

/**
 * Frees an array of snapshot record.
 * @param ss        An array of snapshot record pointers.
 * @param size      Number of snapshot records.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_ss_record_array_free(lsm_ss *ss[], uint32_t size);

/**
 * Returns the snapshot id.
 * @param ss        The snapshot record
 * @return Pointer to id.
 */
const char LSM_DLL_EXPORT *lsm_ss_id_get(lsm_ss *ss);

/**
 * Returns the name.
 * @param ss        The snapshot record
 * @return The Name
 */
const char LSM_DLL_EXPORT *lsm_ss_name_get(lsm_ss *ss);

/**
 * Returns the timestamp
 * @param ss    The snapshot record.
 * @return The timestamp the snapshot was taken
 */
uint64_t LSM_DLL_EXPORT lsm_ss_time_stamp_get(lsm_ss *ss);

#ifdef  __cplusplus
}
#endif

#endif