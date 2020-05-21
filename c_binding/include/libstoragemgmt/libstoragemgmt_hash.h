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
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 *
 */

#ifndef LIBSTORAGEMGMT_HASH_H
#define LIBSTORAGEMGMT_HASH_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Simple hash table which only stores character strings.
 */

/*
 * Allocate storage for hash.
 * @return Allocated record or NULL on memory allocation failure
 */
lsm_hash LSM_DLL_EXPORT *lsm_hash_alloc(void);

/*
 * Free a lsm hash
 * @param op    Record to free.
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_hash_free(lsm_hash *op);

/*
 * Get the list of 'keys' available in the hash
 * @param [in]  op      Valid optional data pointer
 * @param [out] l       String list pointer
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_hash_keys(lsm_hash *op, lsm_string_list **l);

/*
 * Get the value of a key (string)
 * @param [in]  op      Valid optional data pointer
 * @param [in]  key     Key to retrieve value for
 * @return Pointer to value, pointer valid until optional data memory
 *          gets released.
 */
const char LSM_DLL_EXPORT *lsm_hash_string_get(lsm_hash *op, const char *key);

/*
 * Set the value of a key.
 * Note: If key exists, it is replaced with new one.
 * @param [in]  op      Valid optional data pointer
 * @param [in]  key     Key to set value for (key is duped)
 * @param [in]  value   Value of new key (string is duped)
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_hash_string_set(lsm_hash *op, const char *key,
                                       const char *value);

/*
 * Does a copy of an lsm_hash
 * @param src       lsm_hash to copy
 * @return NULL on error/memory allocation failure, else copy
 */
lsm_hash LSM_DLL_EXPORT *lsm_hash_copy(lsm_hash *src);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMT_HASH_H */
