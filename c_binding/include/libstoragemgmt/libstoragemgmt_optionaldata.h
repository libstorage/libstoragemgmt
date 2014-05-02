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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: tasleson
 *
 */

#ifndef LIBSTORAGEMGMT_OPTIONALDATA_H
#define	LIBSTORAGEMGMT_OPTIONALDATA_H

#include "libstoragemgmt_common.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Free a optional data record
 * @param op    Record to free.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_optional_data_record_free(lsm_optional_data *op);

/**
 * Get the list of 'keys' available in the optional data
 * @param [in]  op      Valid optional data pointer
 * @param [out] l       String list pointer
 * @param [out] count   Number of items in string list
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_optional_data_list_get(lsm_optional_data *op,
                                            lsm_string_list **l, uint32_t *count);

/**
 * Get the value of a key (string)
 * @param [in]  op      Valid optional data pointer
 * @param [in]  key     Key to retrieve value for
 * @return Pointer to value, pointer valid until optional data memory
 *          gets released.
 */
const char LSM_DLL_EXPORT *lsm_optional_data_string_get(lsm_optional_data *op,
                                                    const char *key);

/**
 * Set the value of a key.
 * Note: If key exists, it is replaced with new one.
 * @param [in]  op      Valid optional data pointer
 * @param [in]  key     Key to set value for (key is duped)
 * @param [in]  value   Value of new key (string is duped)
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_optional_data_string_set(lsm_optional_data *op,
                                                const char *key,
                                                const char *value);

/**
 * Does a copy of an lsm_optional_data
 * @param src       lsm_optional_data to copy
 * @return NULL on error/memory allocation failure, else copy
 */
lsm_optional_data LSM_DLL_EXPORT *lsm_optional_data_record_copy(lsm_optional_data *src);

#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_OPTIONALDATA_H */
