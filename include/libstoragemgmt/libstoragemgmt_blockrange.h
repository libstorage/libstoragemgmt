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

#ifndef LSM_BLOCKRANGE_H
#define LSM_BLOCKRANGE_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Creates memory for opaque data type to store a block range
 * @param source_start          Source block number to replicate from
 * @param dest_start            Dest block number to replicate to
 * @param block_count           Number of blocks to replicate
 * @return Valid block range ptr, otherwise NULL
 */
lsmBlockRangePtr LSM_DLL_EXPORT lsmBlockRangeRecordAlloc(uint64_t source_start,
                                                        uint64_t dest_start,
                                                        uint64_t block_count);

/**
 * Frees a block range record.
 * @param br        Block range to free
 */
void LSM_DLL_EXPORT lsmBlockRangeRecordFree( lsmBlockRangePtr br);


/**
 * Copies a block range.
 * @param source            Source of the copy
 * @return copy of source
 */
lsmBlockRangePtr LSM_DLL_EXPORT lsmBlockRangeRecordCopy( lsmBlockRangePtr source );


/**
 * Allocates storage for an array of block ranges.
 * @param size                  Number of elements to store.
 * @return  Pointer to memory for array of block ranges.
 */
lsmBlockRangePtr LSM_DLL_EXPORT *lsmBlockRangeRecordAllocArray( uint32_t size );


/**
 * Frees the memory for the array and all records contained in it.
 * @param br                    Array of block ranges to free
 * @param size                  Number of elements in array.
 */
void LSM_DLL_EXPORT lsmBlockRangeRecordFreeArray( lsmBlockRangePtr br[],
                                                    uint32_t size );

/**
 * Retrieves the source block address.
 * @param br        Valid block range pointer
 * @return value of source start.
 */
uint64_t lsmBlockRangeSourceStartGet(lsmBlockRangePtr br);

/**
 * Retrieves the dest block address.
 * @param br        Valid block range pointer
 * @return value of dest start.
 */
uint64_t lsmBlockRangeDestStartGet(lsmBlockRangePtr br);

/**
 * Retrieves the number of blocks to replicate.
 * @param br        Valid block range pointer
 * @return value of number of blocks
 */
uint64_t lsmBlockRangeBlockCountGet(lsmBlockRangePtr br);

#ifdef  __cplusplus
}
#endif

#endif
