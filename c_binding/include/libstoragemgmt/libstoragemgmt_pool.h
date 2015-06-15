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

#ifndef LIBSTORAGEMGMT_POOL_H
#define LIBSTORAGEMGMT_POOL_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees the memory for each of the pools and then the pool array itself.
 * @param pa    Pool array to free.
 * @param size  Size of the pool array.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_pool_record_array_free(lsm_pool *pa[], uint32_t size);

/**
 * Frees the memory for an individual pool
 * @param p Valid pool
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_pool_record_free(lsm_pool *p);

/**
 * Copies a lsm_pool record
 * @param to_be_copied    Record to be copied
 * @return NULL on memory exhaustion, else copy.
 */
lsm_pool LSM_DLL_EXPORT *lsm_pool_record_copy(lsm_pool *to_be_copied);

/**
 * Retrieves the name from the pool.
 * Note: Returned value is only valid as long as p is valid!.
 * @param p     Pool
 * @return      The name of the pool.
 */
char LSM_DLL_EXPORT *lsm_pool_name_get(lsm_pool *p);

/**
 * Retrieves the system wide unique identifier for the pool.
 * Note: Returned value is only valid as long as p is valid!.
 * @param p     Pool
 * @return      The System wide unique identifier.
 */
char LSM_DLL_EXPORT *lsm_pool_id_get(lsm_pool *p);

/**
 * Retrieves the total space for the pool.
 * @param p     Pool
 * @return      Total space of the pool.
 */
uint64_t LSM_DLL_EXPORT lsm_pool_total_space_get(lsm_pool *p);

/**
 * Retrieves the remaining free space in the pool.
 * @param p     Pool
 * @return      The amount of free space.
 */
uint64_t LSM_DLL_EXPORT lsm_pool_free_space_get(lsm_pool *p);

/**
 * Retrieve the status for the Pool.
 * @param s Pool to retrieve status for
 * @return  Pool status which is a bit sensitive field, returns UINT64_MAX on
 * bad pool pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_pool_status_get(lsm_pool *s);

/**
 * Retrieve the status info for the Pool.
 * @param s Pool to retrieve status for
 * @return  Pool status info which is a character string.
 */
const char LSM_DLL_EXPORT *lsm_pool_status_info_get(lsm_pool *s);

/**
 * Retrieve the system id for the specified pool.
 * @param p     Pool pointer
 * @return      System ID
 */
char LSM_DLL_EXPORT *lsm_pool_system_id_get(lsm_pool *p);

/**
 * Retrieve what the pool can be used to create
 * @param p     Pool pointer
 * @return Usage value
 */
uint64_t LSM_DLL_EXPORT lsm_pool_element_type_get(lsm_pool *p);

/**
 * Retrieve what the pool cannot be used for.
 * @param p     Pool pointer
 * @return bitmap of actions not supported.
 */
uint64_t LSM_DLL_EXPORT lsm_pool_unsupported_actions_get(lsm_pool *p);

#ifdef  __cplusplus
}
#endif
#endif                          /* LIBSTORAGEMGMT_POOL_H */
