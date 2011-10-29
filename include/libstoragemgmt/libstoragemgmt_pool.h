/*
 * Copyright (C) 2011 Red Hat, Inc.
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

#ifndef LIBSTORAGEMGMT_POOL_H
#define LIBSTORAGEMGMT_POOL_H

#include <libstoragemgmt/libstoragemgmt_types.h>

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees the memory for each of the pools and then the pool array itself.
 * @param pa    Pool array to free.
 * @param size  Size of the pool array.
 */
void lsmPoolRecordFreeArray( lsmPoolPtr pa[], uint32_t size );

/**
 * Frees the memory for an individual pool
 * @param p Valid pool
 */
void lsmPoolRecordFree(lsmPoolPtr p);

/**
 * Copies a lsmPoolRecordCopy
 * @param toBeCopied    Record to be copied
 * @return NULL on memory exhaustion, else copy.
 */
lsmPoolPtr lsmPoolRecordCopy( lsmPoolPtr toBeCopied);

/**
 * Retrieves the name from the pool.
 * Note: Returned value is only valid as long as p is valid!.
 * @param p     Pool
 * @return      The name of the pool.
 */
char *lsmPoolNameGet( lsmPoolPtr p );

/**
 * Retrieves the system wide unique identifier for the pool.
 * Note: Returned value is only valid as long as p is valid!.
 * @param p     Pool
 * @return      The System wide unique identifier.
 */
char *lsmPoolIdGet( lsmPoolPtr p );

/**
 * Retrieves the total space for the pool.
 * @param p     Pool
 * @return      Total space of the pool.
 */
uint64_t lsmPoolTotalSpaceGet( lsmPoolPtr p );

/**
 * Retrieves the remaining free space in the pool.
 * @param p     Pool
 * @return      The amount of free space.
 */
uint64_t lsmPoolFreeSpaceGet( lsmPoolPtr p );

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_POOL_H */

