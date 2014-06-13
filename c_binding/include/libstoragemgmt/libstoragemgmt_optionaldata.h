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

typedef enum {
    LSM_OPTIONAL_DATA_INVALID       = -2,   /**< Invalid */
    LSM_OPTIONAL_DATA_NOT_FOUND     = -1,   /**< Key not found */
    LSM_OPTIONAL_DATA_STRING        = 1,    /**< Contains a string */
    LSM_OPTIONAL_DATA_SIGN_INT      = 2,    /**< Contains a signed int */
    LSM_OPTIONAL_DATA_UNSIGNED_INT  = 3,    /**< Contains an unsigned int */
    LSM_OPTIONAL_DATA_REAL          = 4,    /**< Contains a real number */
    LSM_OPTIONAL_DATA_STRING_LIST   = 10    /**< Contains a list of strings*/
} lsm_optional_data_type;


/**
 * Returns the type of data stored.
 * @param op    Record
 * @param key   Key to lookup
 * @return One of the enumerated types above.
 */
lsm_optional_data_type LSM_DLL_EXPORT lsm_optional_data_type_get(
                                    lsm_optional_data *op, const char *key);

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
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_optional_data_keys(lsm_optional_data *op,
                                            lsm_string_list **l);

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
lsm_optional_data LSM_DLL_EXPORT *lsm_optional_data_record_copy(
                                                        lsm_optional_data *src);

/**
 * Set the value of a key with a signed integer
 * @param[in] op        Valid optional data pointer
 * @param[in] key       Key to set value for (key is copied)
 * @param[in] value     Value to set
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_optional_data_int64_set(lsm_optional_data *op,
                                                    const char *key,
                                                    int64_t value);

/**
 * Get the value of the key which is a int64.
 * Note: lsm_optional_data_type_get needs to return LSM_OPTIONAL_DATA_SIGN_INT
 * before it is valid to call this function.
 * @param op
 * @param key
 * @return Value, MAX on errors.  To determine if value is valid call
 * lsm_optional_data_type_get first, then you are ensured return value is
 * correct.
 */
int64_t LSM_DLL_EXPORT lsm_optional_data_int64_get(lsm_optional_data *op,
                                                    const char *key);

/**
 * Set the value of a key with an unsigned integer
 * @param[in] op        Valid optional data pointer
 * @param[in] key       Key to set value for (key is copied)
 * @param[in] value     Value to set
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_optional_data_uint64_set(lsm_optional_data *op,
                                                    const char *key,
                                                    uint64_t value);

/**
 * Get the value of the key which is a uint64.
 * Note: lsm_optional_data_type_get needs to return
 * LSM_OPTIONAL_DATA_UNSIGNED_INT before it is valid to call this function.
 * @param [in] op   Valid optional data pointer
 * @param [in] key  Key that exists
 * @return Value, MAX on errors.  To determine if value is valid call
 * lsm_optional_data_type_get first, then you are ensured return value is
 * correct.
 */
uint64_t LSM_DLL_EXPORT lsm_optional_data_uint64_get(lsm_optional_data *op,
                                                    const char *key);

/**
 * Set the value of a key with a real number
 * @param[in] op        Valid optional data pointer
 * @param[in] key       Key to set value for (key is copied)
 * @param[in] value     Value to set
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_optional_data_real_set(lsm_optional_data *op,
                                                    const char *key,
                                                    long double value);

/**
 * Get the value of the key which is a real.
 * Note: lsm_optional_data_type_get needs to return
 * LSM_OPTIONAL_DATA_REAL before it is valid to call this function.
 * @param [in] op   Valid optional data pointer
 * @param [in] key  Key that exists
 * @return Value, MAX on errors.  To determine if value is valid call
 * lsm_optional_data_type_get first, then you are ensured return value is
 * correct.
 */
long double LSM_DLL_EXPORT lsm_optional_data_real_get(lsm_optional_data *op,
                                                        const char *key);


/**
 * Set the value of a key with a string list
 * @param[in] op        Valid optional data pointer
 * @param[in] key       Key to set value for (key is copied)
 * @param[in] value     Value to set
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_optional_data_string_list_set(lsm_optional_data *op,
                                                     const char *key,
                                                     lsm_string_list *sl);

/**
 * Get the value of the key which is a string list.
 * Note: lsm_optional_data_type_get needs to return
 * LSM_OPTIONAL_DATA_STRING_LIST before it is valid to call this function.
 * @param [in] op   Valid optional data pointer
 * @param [in] key  Key that exists
 * @return Value, NULL on errors.  To determine if value is valid call
 * lsm_optional_data_type_get first, then you are ensured return value is
 * correct.
 */
lsm_string_list LSM_DLL_EXPORT *lsm_optional_data_string_list_get(
                                        lsm_optional_data *op, const char *key);

#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_OPTIONALDATA_H */
