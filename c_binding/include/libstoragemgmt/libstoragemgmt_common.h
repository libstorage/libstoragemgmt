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

#ifndef LSM_COMMON_H
#define LSM_COMMON_H

#include "libstoragemgmt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined _WIN32 || defined __CYGWIN__
#define LSM_DLL_IMPORT __declspec(dllimport)
#define LSM_DLL_EXPORT __declspec(dllexport)
#define LSM_DLL_LOCAL
#else
#if __GNUC__ >= 4
#define LSM_DLL_IMPORT __attribute__((visibility("default")))
#define LSM_DLL_EXPORT __attribute__((visibility("default")))
#define LSM_DLL_LOCAL  __attribute__((visibility("hidden")))
#else
#define LSM_DLL_IMPORT
#define LSM_DLL_EXPORT
#define LSM_DLL_LOCAL
#endif
#endif

/**
 * lsm_string_list_alloc - Allocates the memory for a string list.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Allocates the memory for a string list.
 *      All elements will be set to NULL.
 *
 * @size:
 *      uint32_t. The count of string.
 *
 * Return:
 *      Pointer of lsm_string. NULL on memory allocation failure or size is 0.
 *      Should be freed by lsm_string_list_free().
 */
lsm_string_list LSM_DLL_EXPORT *lsm_string_list_alloc(uint32_t size);

/**
 * lsm_string_list_alloc - Frees the memory for string list.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for string list.
 *
 * @sl:
 *      Pointer of lsm_string_list to free.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_string_list
 *              pointer.
 */
int LSM_DLL_EXPORT lsm_string_list_free(lsm_string_list *sl);

/**
 * lsm_string_list_record_copy - Duplicates a lsm_string_list record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_string_list record.
 *
 * @src:
 *      Pointer of lsm_string_list to duplicate.
 *
 * Return:
 *      Pointer of lsm_string_list. NULL on memory allocation failure or invalid
 *      lsm_string_list pointer. Should be freed by
 *      lsm_string_list_record_free().
 */
lsm_string_list LSM_DLL_EXPORT *lsm_string_list_copy(lsm_string_list *src);

/**
 * lsm_string_list_elem_set - Sets the specified element of lsm_string_list.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Sets the specified element of lsm_string_list.
 *      The string will be copied and managed by lsm_string_list.
 *      The memory of old string will be freed.
 *      If specified index is larger than lsm_string_list size, the
 *      lsm_string_list will be automatically grow and padding with NULL.
 *
 * @sl:
 *      Pointer of lsm_string_list to update.
 * @index:
 *      The element index, starting from 0.
 * @value:
 *      The string to store in lsm_string_list.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_string_list
 *              pointer.
 *          * LSM_ERR_NO_MEMORY
 *              When no enough memory.
 */
int LSM_DLL_EXPORT lsm_string_list_elem_set(lsm_string_list *sl, uint32_t index,
                                            const char *value);

/**
 * lsm_string_list_elem_get - Retrieve specified index of string from
 * lsm_string_list.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves specified index of string from lsm_string_list.
 *      Note: Address returned is valid until lsm_string_list gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @sl:
 *      lsm_string_list. The string list to retrieve from.
 * @index:
 *      uint32_t. The index of element string to retrieve. Starting from 0.
 *
 * Return:
 *      string. NULL if argument 'sl' is NULL or not a valid
 *      lsm_string_list pointer or out of index.
 */
const char LSM_DLL_EXPORT *lsm_string_list_elem_get(lsm_string_list *sl,
                                                    uint32_t index);

/**
 * Returns the size of the list
 * @param sl        Valid string list pointer
 * @return  size of list, note you cannot create a zero sized list, so
 *          0 indicates error with structure
 *
 * lsm_string_list_size - Retrieve the size of lsm_string_list.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Returns the size of the lsm_string_list.
 *
 * @sl:
 *      lsm_string_list. The string list to retrieve from.
 *
 * Return:
 *      uint32_t. 0 if argument 'sl' is NULL or not a valid
 *      lsm_string_list pointer.
 */
uint32_t LSM_DLL_EXPORT lsm_string_list_size(lsm_string_list *sl);

/**
 * lsm_string_list_append - Append specified string to lsm_string_list.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Append the specified string to lsm_string_list.
 *      The string will be copied and managed by lsm_string_list.
 *      The lsm_string_list will be grown to hold this string.
 *
 * @sl:
 *      Pointer of lsm_string_list to update.
 * @add:
 *      The string to store in lsm_string_list.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_string_list
 *              pointer.
 *          * LSM_ERR_NO_MEMORY
 *              When no enough memory.
 */
int LSM_DLL_EXPORT lsm_string_list_append(lsm_string_list *sl, const char *add);

/**
 * lsm_string_list_delete - Deletes specified element from lsm_string_list.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Deletes the specified element from lsm_string_list.
 *      The string of that element will be freed. The pointer
 *      retrieved by lsm_string_list_elem_get() will be invalid.
 *      The element after this one will moved down, thus if you wanted to
 *      iterate over the list deleting each element one by one you need to
 *      do in reverse order.
 *
 * @sl:
 *      lsm_string_list. Pointer of lsm_string_list to update.
 * @index:
 *      uint32_t. The element index. Start from 0.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_string_list
 *              pointer.
 */
int LSM_DLL_EXPORT lsm_string_list_delete(lsm_string_list *sl, uint32_t index);

/**
 * lsm_initiator_id_verify - Verifies if initiator id is valid.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Verifies whether specified initiator is valid:
 *          * iSCSI:
 *              Starting with "iqn", "eui", or "naa".
 *          * WWPN:
 *              16 hex digits(0-9a-fA-F).
 *
 * @init_id:
 *      String to verify.
 * @init_type:
 *      lsm_access_group pointer. The type of initiator ID.
 *      You may set it to LSM_ACCESS_GROUP_INIT_TYPE_UNKNOWN, this function
 *      will try iSCSI and WWPN, and modify this argument to valid type of
 *      initiator type.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              Is valid initiator.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              Not a valid initiator or any argument is NULL.
 */
int LSM_DLL_EXPORT lsm_initiator_id_verify(
    const char *init_id, lsm_access_group_init_type *init_type);

/**
 * Checks to see if volume vpd83 is valid
 * @param vpd83         VPD string to check
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK if vpd is OK
 * @retval LSM_INVALID_ARGUMENT otherwise.
 * lsm_volume_vpd83_verify - Verifies if volume vpd83 is valid.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Verifies whether specified string is a valid volume vpd83:
 *          * For string start with '2' or '3' or '5', the valid vpd83 should
 *            be 16 hex digits(0-9a-f).
 *          * For string start with '6', the valid vpd83 should be 32 hex
 *            digits(0-9a-f).
 *
 * @vpd83:
 *      String to verify.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              Is valid volume vpd83.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              Not a valid volume vpd83 or argument is NULL.
 */
int LSM_DLL_EXPORT lsm_volume_vpd83_verify(const char *vpd83);

#ifdef __cplusplus
}
#endif
#endif /* LSM_COMMON_H */
