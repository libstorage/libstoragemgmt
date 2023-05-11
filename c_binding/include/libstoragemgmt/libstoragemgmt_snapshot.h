/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * 
 * Copyright (C) 2011-2023 Red Hat, Inc.
 *
 * Author: Tony Asleson <tasleson@redhat.com>
 */

#ifndef LSM_SNAPSHOT_H
#define LSM_SNAPSHOT_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_fs_ss_record_free - Frees the memory for an individual file system
 * snapshot
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for an individual file system snapshot.
 *
 * @ss:
 *      lsm_fs_ss to release memory for.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_fs_ss pointer.
 */
int LSM_DLL_EXPORT lsm_fs_ss_record_free(lsm_fs_ss *ss);

/**
 * lsm_fs_ss_record_copy - Duplicates a lsm_fs_ss record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_fs_ss record.
 *
 * @source:
 *      Pointer of lsm_fs_ss to duplicate.
 *
 * Return:
 *      Pointer of lsm_fs_ss. NULL on memory allocation failure or invalid
 *      lsm_fs_ss pointer. Should be freed by lsm_fs_ss_record_free().
 */
lsm_fs_ss LSM_DLL_EXPORT *lsm_fs_ss_record_copy(lsm_fs_ss *source);

/**
 * lsm_fs_ss_record_array_free - Frees the memory of lsm_fs_ss array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for each of lsm_fs_ss and then the array
 *      itself.
 *
 * @ss:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_fs_ss pointer.
 */
int LSM_DLL_EXPORT lsm_fs_ss_record_array_free(lsm_fs_ss *ss[], uint32_t size);

/**
 * lsm_fs_ss_id_get - Retrieves the ID for the file system snapshot.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the ID for the file system snapshot.
 *      Note: Address returned is valid until lsm_fs_ss gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @ss:
 *      File system snapshot to retrieve ID for.
 *
 * Return:
 *      string. NULL if argument 'ss' is NULL or not a valid lsm_fs_ss pointer.
 */
const char LSM_DLL_EXPORT *lsm_fs_ss_id_get(lsm_fs_ss *ss);

/**
 * lsm_fs_ss_id_get - Retrieves the name for the file system snapshot.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the name for the file system snapshot.
 *      Note: Address returned is valid until lsm_fs_ss gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @ss:
 *      File system snapshot to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 'ss' is NULL or not a valid lsm_fs_ss pointer.
 */
const char LSM_DLL_EXPORT *lsm_fs_ss_name_get(lsm_fs_ss *ss);

/**
 * lsm_fs_ss_block_size_get -  Retrieves the timestamp for the file system
 * snapshot
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the POSIX timestamp of the creation of file system snapshot.
 *
 * @ss:
 *      File system snapshot to retrieve timestamp for.
 *
 * Return:
 *      uint64_t. 0 if argument 'ss' is NULL or not a valid lsm_fs_ss pointer.
 *
 */
uint64_t LSM_DLL_EXPORT lsm_fs_ss_time_stamp_get(lsm_fs_ss *ss);

#ifdef __cplusplus
}
#endif
#endif
