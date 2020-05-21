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

#ifndef LSM_BLOCKRANGE_H
#define LSM_BLOCKRANGE_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_block_range_record_alloc - Allocates memory for a lsm_block_range record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Allocates memory for lsm_block_range opaque data type to store a block
 *      range.
 *
 * @source_start:
 *      The start block of replication source.
 *
 * @dest_start:
 *      The start block of replication destination.
 *
 * @block_count:
 *      The count of blocks for this block range.
 *
 * Return:
 *      Pointer of lsm_block_range. NULL on memory allocation failure or
 *      illegal argument. Should be freed by lsm_block_range_record_free().
 */
lsm_block_range LSM_DLL_EXPORT *
lsm_block_range_record_alloc(uint64_t source_start, uint64_t dest_start,
                             uint64_t block_count);

/**
 * lsm_block range_record_free - Free the lsm_block_range memory.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for a lsm_block_range.
 *
 * @br:
 *      Record to release.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_block_range
 * pointer.
 */
int LSM_DLL_EXPORT lsm_block_range_record_free(lsm_block_range *br);

/**
 * lsm_block_range_record_copy - Duplicates a block_range record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_block_range record.
 *
 * @source:
 *      Pointer of lsm_block_range to duplicate.
 *
 * Return:
 *      Pointer of lsm_block_range. NULL on memory allocation failure or
 *      invalid lsm_block_range pointer.
 *      Should be freed by lsm_block_range_record_free().
 */
lsm_block_range LSM_DLL_EXPORT *
lsm_block_range_record_copy(lsm_block_range *source);

/**
 * lsm_block_range_record_array_alloc - Allocates a lsm_block_range array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Allocates a lsm_block_range pointer array.
 *
 * @size:
 *      uint32_t. The size of lsm_block_range pointer array.
 *
 * Return:
 *      Pointer of lsm_block_range array. NULL on memory allocation failure or
 *      argument size is 0.
 *      Should be freed by lsm_block_range_record_array_free().
 */
lsm_block_range LSM_DLL_EXPORT **
lsm_block_range_record_array_alloc(uint32_t size);

/**
 * lsm_block_range_record_array_free - Free the memory of lsm_block_range array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for an array of lsm_block_range.
 *
 * @br:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_block_range
 *              pointer.
 */
int LSM_DLL_EXPORT lsm_block_range_record_array_free(lsm_block_range *br[],
                                                     uint32_t size);

/**
 * lsm_block_range_source_start_get - Retrieves start block of the source.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the start block number of the replication source.
 *
 * @br:
 *      Block range to retrieve source start block number.
 *
 * Return:
 *      uint64_t. 0 if invalid lsm_block_range pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_block_range_source_start_get(lsm_block_range *br);

/**
 * lsm_block_range_destination_start_get - Retrieves start block of the
 * destination.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the destination start block number of the replication
 *      destination.
 *
 * @br:
 *      Block range to retrieve destination start block number.
 *
 * Return:
 *      uint64_t. 0 if invalid lsm_block_range pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_block_range_dest_start_get(lsm_block_range *br);

/**
 * lsm_block_range_block_count_get - Retrieve block count of the block range.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the block count of the block range.
 *
 * @br:
 *      Block range to retrieve block count.
 *
 * Return:
 *      uint64_t. 0 if invalid lsm_block_range pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_block_range_block_count_get(lsm_block_range *br);

#ifdef __cplusplus
}
#endif
#endif
