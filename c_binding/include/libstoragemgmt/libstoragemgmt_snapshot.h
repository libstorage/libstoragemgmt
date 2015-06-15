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

#ifndef LSM_SNAPSHOT_H
#define LSM_SNAPSHOT_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees a file system snapshot record.
 * @param ss    Snapshot record
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_fs_ss_record_free(lsm_fs_ss *ss);

/**
 * Copies a file system snapshot record.
 * @param source        Source to copy
 * @return Copy of source record snapshot
 */
lsm_fs_ss LSM_DLL_EXPORT *lsm_fs_ss_record_copy(lsm_fs_ss *source);

/**
 * Frees an array of snapshot record.
 * @param ss        An array of snapshot record pointers.
 * @param size      Number of snapshot records.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_fs_ss_record_array_free(lsm_fs_ss *ss[], uint32_t size);

/**
 * Returns the file system snapshot id.
 * @param ss        The snapshot record
 * @return Pointer to id.
 */
const char LSM_DLL_EXPORT *lsm_fs_ss_id_get(lsm_fs_ss *ss);

/**
 * Returns the name.
 * @param ss        The file system snapshot record
 * @return The Name
 */
const char LSM_DLL_EXPORT *lsm_fs_ss_name_get(lsm_fs_ss *ss);

/**
 * Returns the timestamp
 * @param ss    The file system snapshot record.
 * @return The timestamp the file system snapshot was taken
 */
uint64_t LSM_DLL_EXPORT lsm_fs_ss_time_stamp_get(lsm_fs_ss *ss);

#ifdef  __cplusplus
}
#endif
#endif
